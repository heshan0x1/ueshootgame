// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// 文件概览：NavMeshPath.cpp
// ----------------------------------------------------------------------------
// FNavMeshPath 的实现；在架构文档"寻路"一节中处于 FPImplRecastNavMesh::FindPath
// 的下游：
//   - GeneratePathCorridorEdges()：从 PathCorridor 懒查 Portal Edges
//   - PerformStringPulling()：调 ARecastNavMesh::FindStraightPath（即 Detour
//     findStraightPath 的 UE 封装），产出拉绳后的 PathPoints
//   - OffsetFromCorners()：利用 Portal Edge 信息把拐角点内移，避免贴墙；
//     并在必要时做"可见性插点"平滑路径
//   - ApplyFlags() / DoesIntersectBox / GetSegmentDirection / Invert 等杂项
// 坐标：PathPoints / PathCorridorEdges 全部为 UE 坐标；下游 Detour 调用在
// ARecastNavMesh 内部完成翻转。
// 线程：GeneratePathCorridorEdges 显式检查 IsInGameThread()，避免跨线程访问 dtNavMesh。
// ============================================================================

#include "NavMesh/NavMeshPath.h"
#include "EngineStats.h"
#include "EngineGlobals.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavAreas/NavArea.h"
#include "Debug/DebugDrawService.h"
#include "Algo/Reverse.h"

#define DEBUG_DRAW_OFFSET 0               // 非 0 时在 OffsetFromCorners 里画调试线
#define PATH_OFFSET_KEEP_VISIBLE_POINTS 1  // 可见性插点阶段是否保留中间点


//----------------------------------------------------------------------//
// FNavMeshPath
//----------------------------------------------------------------------//
const FNavPathType FNavMeshPath::Type;
	
// 默认构造：开启拉绳、不保留走廊（省内存）
FNavMeshPath::FNavMeshPath()
	: bWantsStringPulling(true)
	, bWantsPathCorridor(false)
{
	PathType = FNavMeshPath::Type;
	InternalResetNavMeshPath();
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FNavMeshPath::~FNavMeshPath() = default;
FNavMeshPath::FNavMeshPath(const FNavMeshPath&) = default;
FNavMeshPath::FNavMeshPath(FNavMeshPath&& Other) = default;
FNavMeshPath& FNavMeshPath::operator=(const FNavMeshPath& Other) = default;
FNavMeshPath& FNavMeshPath::operator=(FNavMeshPath&& Other) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

// Repath 前重置；基类先清公共字段，本类再清 NavMesh 专属字段
void FNavMeshPath::ResetForRepath()
{
	Super::ResetForRepath();
	InternalResetNavMeshPath();
}

// 清空本类特有的状态；注意不重置 bWantsStringPulling / bWantsPathCorridor
// （这两个是"查询意图"，Repath 时仍要保持一致）
void FNavMeshPath::InternalResetNavMeshPath()
{
	PathCorridor.Reset();
	PathCorridorCost.Reset();
	CustomNavLinkIds.Reset();
	PathCorridorEdges.Reset();

	bCorridorEdgesGenerated = false;
	bDynamic = false;
	bStringPulled = false;

	// keep:
	// - bWantsStringPulling
	// - bWantsPathCorridor
}

// 从 StartingPoint 起累加 PathPoints 相邻距离；用于 GetTotalPathLength 的"已拉绳"分支
FVector::FReal FNavMeshPath::GetStringPulledLength(const int32 StartingPoint) const
{
	if (IsValid() == false || StartingPoint >= PathPoints.Num())
	{
		return 0.f;
	}

	FVector::FReal TotalLength = 0.f;
	const FNavPathPoint* PrevPoint = PathPoints.GetData() + StartingPoint;
	const FNavPathPoint* PathPoint = PrevPoint + 1;

	// 迭代目标：累加所有相邻折线段的 3D 距离
	for (int32 PathPointIndex = StartingPoint + 1; PathPointIndex < PathPoints.Num(); ++PathPointIndex, ++PathPoint, ++PrevPoint)
	{
		TotalLength += FVector::Dist(PrevPoint->Location, PathPoint->Location);
	}

	return TotalLength;
}

// 走廊估算长度（未拉绳时）：相邻 Portal Edge 中点距离累加。
// 前置条件：bCorridorEdgesGenerated，否则返回 0。
FVector::FReal FNavMeshPath::GetPathCorridorLength(const int32 StartingEdge) const
{
	if (bCorridorEdgesGenerated == false)
	{
		return 0.f;
	}
	else if (StartingEdge >= PathCorridorEdges.Num())
	{
		// 特殊情形：只剩最后一段（起终点直连）
		return StartingEdge == 0 && PathPoints.Num() > 1 ? FVector::Dist(PathPoints[0].Location, PathPoints[PathPoints.Num()-1].Location) : 0.;
	}
	
	const FNavigationPortalEdge* PrevEdge = PathCorridorEdges.GetData() + StartingEdge;
	const FNavigationPortalEdge* CorridorEdge = PrevEdge + 1;
	FVector PrevEdgeMiddle = PrevEdge->GetMiddlePoint();

	// 起点分支：从 PathPoints[0] 到第一个 Portal 中点
	FVector::FReal TotalLength = StartingEdge == 0 ? FVector::Dist(PathPoints[0].Location, PrevEdgeMiddle)
		: FVector::Dist(PrevEdgeMiddle, PathCorridorEdges[StartingEdge - 1].GetMiddlePoint());

	// 迭代目标：把剩余 Portal Edge 中点两两距离求和
	for (int32 PathPolyIndex = StartingEdge + 1; PathPolyIndex < PathCorridorEdges.Num(); ++PathPolyIndex, ++PrevEdge, ++CorridorEdge)
	{
		const FVector CurrentEdgeMiddle = CorridorEdge->GetMiddlePoint();
		TotalLength += FVector::Dist(CurrentEdgeMiddle, PrevEdgeMiddle);
		PrevEdgeMiddle = CurrentEdgeMiddle;
	}
	// @todo add distance to last point here!
	return TotalLength;
}

// 懒计算 Portal Edges：走廊 → 相邻多边形的"门"边序列。
// 仅允许在 GameThread 调用 Detour，用于线程安全兜底。
const TArray<FNavigationPortalEdge>& FNavMeshPath::GeneratePathCorridorEdges() const
{
#if WITH_RECAST
	// mz@todo the underlying recast function queries the navmesh a portal at a time, 
	// which is a waste of performance. A batch-query function has to be added.
	const int32 CorridorLength = PathCorridor.Num();
	if (CorridorLength != 0 && IsInGameThread() && NavigationDataUsed.IsValid())
	{
		const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
		if (MyOwner)
		{
			// 内部每对相邻 Poly 调用一次 dtNavMesh::getPortalPoints
			MyOwner->GetEdgesForPathCorridor(&PathCorridor, &PathCorridorEdges);
			bCorridorEdgesGenerated = (PathCorridorEdges.Num() > 0);
		}
	}
#endif // WITH_RECAST
	return PathCorridorEdges;
}

// 执行 String Pulling：把多边形走廊压缩为最短折线，写入 PathPoints。
// 底层：ARecastNavMesh::FindStraightPath → Detour dtNavMeshQuery::findStraightPath。
// 同时填充 CustomNavLinkIds（路径经过的所有自定义 Link 的 64bit ID）。
void FNavMeshPath::PerformStringPulling(const FVector& StartLoc, const FVector& EndLoc)
{
#if WITH_RECAST
	const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
	if (::IsValid(MyOwner) && PathCorridor.Num())
	{
		bStringPulled = MyOwner->FindStraightPath(StartLoc, EndLoc, PathCorridor, PathPoints, &CustomNavLinkIds);
	}
#endif	// WITH_RECAST
}


#if DEBUG_DRAW_OFFSET
	UWorld* GInternalDebugWorld_ = NULL;   // 调试开关打开时用于 DrawDebugXxx 的 World
#endif

namespace
{
	// OffsetFromCorners 阶段的"候选路径点 + 所属 PortalEdge 两端"辅助结构
	struct FPathPointInfo
	{
		FPathPointInfo() 
		{

		}
		FPathPointInfo( const FNavPathPoint& InPoint, const FVector& InEdgePt0, const FVector& InEdgePt1) 
			: Point(InPoint)
			, EdgePt0(InEdgePt0)
			, EdgePt1(InEdgePt1) 
		{ 
			/** Empty */ 
		}

		FNavPathPoint Point;
		FVector EdgePt0;
		FVector EdgePt1;
	};

	// 可见性检查：StartPoint → EndPoint 的连线（偏移 OffsetDistannce 后）
	// 是否能无阻穿过中间所有 Portal Edge；若在某条边被阻塞，回填 LastVisiblePoint。
	// 这是 "Better Offsets from Corners" 算法的核心，用于补出阻塞点避免穿墙。
	FORCEINLINE bool CheckVisibility(const FPathPointInfo* StartPoint, const FPathPointInfo* EndPoint,  TArray<FNavigationPortalEdge>& PathCorridorEdges, FVector::FReal OffsetDistannce, FPathPointInfo* LastVisiblePoint)
	{
		FVector IntersectionPoint = FVector::ZeroVector;
		FVector StartTrace = StartPoint->Point.Location;
		FVector EndTrace = EndPoint->Point.Location;

		// find closest edge to StartPoint
		// 找到距 Start/End 最近的 Portal Edge，作为"可视区间"两端
		FVector::FReal BestDistance = TNumericLimits<FVector::FReal>::Max();
		FNavigationPortalEdge* CurrentEdge = NULL;

		FVector::FReal BestEndPointDistance = TNumericLimits<FVector::FReal>::Max();
		FNavigationPortalEdge* EndPointEdge = NULL;
		for (int32 EdgeIndex =0; EdgeIndex < PathCorridorEdges.Num(); ++EdgeIndex)
		{
			FVector::FReal DistToEdge = TNumericLimits<FVector::FReal>::Max();
			FNavigationPortalEdge* Edge = &PathCorridorEdges[EdgeIndex];
			if (BestDistance > FMath::Square(KINDA_SMALL_NUMBER))
			{
				DistToEdge= FMath::PointDistToSegmentSquared(StartTrace, Edge->Left, Edge->Right);
				if (DistToEdge < BestDistance)
				{
					BestDistance = DistToEdge;
					CurrentEdge = Edge;
#if DEBUG_DRAW_OFFSET
					DrawDebugLine( GInternalDebugWorld_, Edge->Left, Edge->Right, FColor::White, true );
#endif
				}
			}

			if (BestEndPointDistance > FMath::Square(KINDA_SMALL_NUMBER))
			{
				DistToEdge= FMath::PointDistToSegmentSquared(EndTrace, Edge->Left, Edge->Right);
				if (DistToEdge < BestEndPointDistance)
				{
					BestEndPointDistance = DistToEdge;
					EndPointEdge = Edge;
				}
			}
		}

		if (CurrentEdge == NULL || EndPointEdge == NULL )
		{
			// 找不到起止所属边，视作可见性未知：置零表示"无"
			LastVisiblePoint->Point.Location = FVector::ZeroVector;
			return false;
		}


		if (BestDistance <= FMath::Square(KINDA_SMALL_NUMBER))
		{
			// StartPoint 正好落在某条边上，跳过该边（避免自己挡自己）
			CurrentEdge++;
		}

		if (CurrentEdge == EndPointEdge)
		{
			// 起止同属一条 Portal Edge，当然可见
			return true;
		}

		// 线段沿方向内缩 OffsetDistannce，避免端点贴 Portal 端点造成数值误差
		const FVector RayNormal = (StartTrace-EndTrace) .GetSafeNormal() * OffsetDistannce;
		StartTrace = StartTrace + RayNormal;
		EndTrace = EndTrace - RayNormal;

		bool bIsVisible = true;
#if DEBUG_DRAW_OFFSET
		DrawDebugLine( GInternalDebugWorld_, StartTrace, EndTrace, FColor::Yellow, true );
#endif
		const FNavigationPortalEdge* LaseEdge = &PathCorridorEdges[PathCorridorEdges.Num()-1];
		// 迭代目标：逐条 Portal Edge 求交；若有不相交的边就回填最近点并认定不可见
		while (CurrentEdge <= EndPointEdge)
		{
			FVector Left = CurrentEdge->Left;
			FVector Right = CurrentEdge->Right;

#if DEBUG_DRAW_OFFSET
			DrawDebugLine( GInternalDebugWorld_, Left, Right, FColor::White, true );
#endif
			bool bIntersected = FMath::SegmentIntersection2D(Left, Right, StartTrace, EndTrace, IntersectionPoint);
			if ( !bIntersected)
			{
				// 不相交：该边挡住了"视线"。取视线到边的最近点作为"最后可见点"
				const FVector::FReal EdgeHalfLength = (CurrentEdge->Left - CurrentEdge->Right).Size() * 0.5f;
				const FVector::FReal Distance = FMath::Min(OffsetDistannce, EdgeHalfLength) *  0.1f;
				Left = CurrentEdge->Left + Distance * (CurrentEdge->Right - CurrentEdge->Left).GetSafeNormal();
				Right = CurrentEdge->Right + Distance * (CurrentEdge->Left - CurrentEdge->Right).GetSafeNormal();
				FVector ClosestPointOnRay, ClosestPointOnEdge;
				FMath::SegmentDistToSegment(StartTrace, EndTrace, Right, Left, ClosestPointOnRay, ClosestPointOnEdge);
#if DEBUG_DRAW_OFFSET
				DrawDebugSphere( GInternalDebugWorld_, ClosestPointOnEdge, 10, 8, FColor::Red, true );
#endif
				LastVisiblePoint->Point.Location = ClosestPointOnEdge;
				LastVisiblePoint->EdgePt0= CurrentEdge->Left ;
				LastVisiblePoint->EdgePt1= CurrentEdge->Right ;
				return false;
			}
#if DEBUG_DRAW_OFFSET
			DrawDebugSphere( GInternalDebugWorld_, IntersectionPoint, 8, 8, FColor::White, true );
#endif
			CurrentEdge++;
			bIsVisible = true;
		}

		return bIsVisible;
	}
}

// 将 NavDataFlags（ERecastPathFlags 的位）应用到路径偏好；
// 由 FPImplRecastNavMesh::FindPath 调用，让 Query 传入的标志影响后处理。
void FNavMeshPath::ApplyFlags(int32 NavDataFlags)
{
	if (NavDataFlags & ERecastPathFlags::SkipStringPulling)
	{
		bWantsStringPulling = false;
	}

	if (NavDataFlags & ERecastPathFlags::GenerateCorridor)
	{
		bWantsPathCorridor = true;
	}
}

// 追加 SourcePoints[Index] 到 PathPoints；会过滤掉 NodeRef==0 的无效点
void AppendPathPointsHelper(TArray<FNavPathPoint>& PathPoints, const TArray<FPathPointInfo>& SourcePoints, int32 Index)
{
	if (SourcePoints.IsValidIndex(Index) && SourcePoints[Index].Point.NodeRef != 0)
	{
		PathPoints.Add(SourcePoints[Index].Point);
	}
}

// ---------------------------------------------------------------------------
// OffsetFromCorners：路径"倒角"处理。
// 若拉绳得到的路径点紧贴 Portal Edge 端点（多边形角落），Agent 行走时容易被墙角卡住。
// 做法：
//   Pass 1：对每个路径点找其关联 Portal Edge，若距端点 < Distance 则把该点沿边内移；
//   Pass 2（仅当 bUseBetterOffsetsFromCorners 且点数≥3）：
//        基于 CheckVisibility 再做一次"可见性"贪婪简化，处理多层内移后的折线断裂。
// 注意：路径超 100 点时直接放弃（性能考虑）。
// ---------------------------------------------------------------------------
void FNavMeshPath::OffsetFromCorners(FVector::FReal Distance)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_OffsetFromCorners);

	const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
	if (MyOwner == nullptr || PathPoints.Num() == 0 || PathPoints.Num() > 100)
	{
		// skip it, there is not need to offset that path from performance point of view
		return;
	}

#if DEBUG_DRAW_OFFSET
	GInternalDebugWorld_ = MyOwner->GetWorld();
	FlushDebugStrings(GInternalDebugWorld_);
	FlushPersistentDebugLines(GInternalDebugWorld_);
#endif

	if (bCorridorEdgesGenerated == false)
	{
		GeneratePathCorridorEdges(); 
	}
	const FVector::FReal DistanceSq = Distance * Distance;
	int32 CurrentEdge = 0;
	bool bNeedToCopyResults = false;
	int32 SingleNodePassCount = 0;

	FNavPathPoint* PathPoint = PathPoints.GetData();
	// it's possible we'll be inserting points into the path, so we need to buffer the result
	// 先写进 FirstPassPoints 缓冲，不直接改 PathPoints（避免迭代时插入）
	TArray<FPathPointInfo> FirstPassPoints;
	FirstPassPoints.Reserve(PathPoints.Num() + 2);
	FirstPassPoints.Add(FPathPointInfo(*PathPoint, FVector::ZeroVector, FVector::ZeroVector));
	++PathPoint;

	// for every point on path find a related corridor edge
	// Pass 1：遍历中间路径点（排除首尾），根据对应 Portal Edge 决定是否内移
	for (int32 PathNodeIndex = 1; PathNodeIndex < PathPoints.Num()-1 && CurrentEdge < PathCorridorEdges.Num();)
	{
		if (FNavMeshNodeFlags(PathPoint->Flags).PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION)
		{
			// put both ends
			// OffMeshConnection 两端必须保留原始坐标（Link 自身规定位置）
			FirstPassPoints.Add(FPathPointInfo(*PathPoint, FVector(0), FVector(0)));
			FirstPassPoints.Add(FPathPointInfo(*(PathPoint+1), FVector(0), FVector(0)));
			PathNodeIndex += 2;
			PathPoint += 2;
			continue;
		}

		// 查找"含本 PathPoint 的 Portal Edge"；用点到线段距离≈0 作为命中条件
		int32 CloserPoint = -1;
		const FNavigationPortalEdge* Edge = &PathCorridorEdges[CurrentEdge];
		for (int32 EdgeIndex = CurrentEdge; EdgeIndex < PathCorridorEdges.Num(); ++Edge, ++EdgeIndex)
		{
			const FVector::FReal DistToSequence = FMath::PointDistToSegmentSquared(PathPoint->Location, Edge->Left, Edge->Right);
			if (DistToSequence <= FMath::Square(KINDA_SMALL_NUMBER))
			{
				const FVector::FReal LeftDistanceSq = FVector::DistSquared(PathPoint->Location, Edge->Left);
				const FVector::FReal RightDistanceSq = FVector::DistSquared(PathPoint->Location, Edge->Right);
				if (LeftDistanceSq > DistanceSq && RightDistanceSq > DistanceSq)
				{
					// 点落在边的中段（距离两端都远），无需内移；推进到下一条边
					++CurrentEdge;
				}
				else
				{
					// 点靠近 Left 或 Right 端点；记录"近哪一端"以便朝另一端内移
					CloserPoint = LeftDistanceSq < RightDistanceSq ? 0 : 1;
					CurrentEdge = EdgeIndex;
				}
				break;
			}
		}

		if (CloserPoint >= 0)
		{
			bNeedToCopyResults = true;

			Edge = &PathCorridorEdges[CurrentEdge];
			// 限幅：偏移量不超过半条边长度
			const FVector::FReal ActualOffset = FPlatformMath::Min(Edge->GetLength()/2, Distance);

			FNavPathPoint NewPathPoint = *PathPoint;
			// apply offset 
			// 从近端点沿边向另一端内移 ActualOffset
			const FVector EdgePt0 = Edge->GetPoint(CloserPoint);
			const FVector EdgePt1 = Edge->GetPoint((CloserPoint+1)%2);
			const FVector EdgeDir = EdgePt1 - EdgePt0;
			const FVector EdgeOffset = EdgeDir.GetSafeNormal() * ActualOffset;
			NewPathPoint.Location = EdgePt0 + EdgeOffset;
			// update NodeRef (could be different if this is n-th pass on the same PathPoint
			// NodeRef 指向这条 Portal 的"通向的多边形"——同一 PathPoint 可能被两条相邻 Portal 内移两次
			NewPathPoint.NodeRef = Edge->ToRef;
			FirstPassPoints.Add(FPathPointInfo(NewPathPoint, EdgePt0, EdgePt1));

			// if we've found a matching edge it's possible there's also another one there using the same edge. 
			// that's why we need to repeat the process with the same path point and next edge
			++CurrentEdge;

			// we need to know if we did more than one iteration on a given point
			// if so then we should not add that point in following "else" statement
			++SingleNodePassCount;
		}
		else
		{
			if (SingleNodePassCount == 0)
			{
				// store unchanged
				// 本点未命中任何 Portal，原样保留
				FirstPassPoints.Add(FPathPointInfo(*PathPoint, FVector(0), FVector(0)));
			}
			else
			{
				// 已被内移过，不再重复加入
				SingleNodePassCount = 0;
			}

			++PathNodeIndex;
			++PathPoint;
		}
	}

	if (bNeedToCopyResults)
	{
		// 点太少或关闭了 BetterOffsets：直接把 Pass1 的结果写回，不做可见性优化
		if (FirstPassPoints.Num() < 3 || !MyOwner->bUseBetterOffsetsFromCorners)
		{
			FNavPathPoint EndPt = PathPoints.Last();

			PathPoints.Reset();
			for (int32 Index=0; Index < FirstPassPoints.Num(); ++Index)
			{
				PathPoints.Add(FirstPassPoints[Index].Point);
			}

			PathPoints.Add(EndPt);
			return;
		}

		// Pass 2：可见性贪婪简化
		TArray<FNavPathPoint> DestinationPathPoints;
		DestinationPathPoints.Reserve(FirstPassPoints.Num() + 2);

		// don't forget the last point
		FirstPassPoints.Add(FPathPointInfo(PathPoints[PathPoints.Num()-1], FVector::ZeroVector, FVector::ZeroVector));

		int32 StartPointIndex = 0;
		int32 LastVisiblePointIndex = 0;
		int32 TestedPointIndex = 1;
		int32 LastPointIndex = FirstPassPoints.Num()-1;

		const int32 MaxSteps = 200;    // 防死循环保险丝
		// 迭代目标：从 StartPointIndex 向后拉"最长可见连接"；不可见时回退到 LastVisible 并插点
		for (int32 StepsLeft = MaxSteps; StepsLeft >= 0; StepsLeft--)
		{ 
			if (StartPointIndex == TestedPointIndex || StepsLeft == 0)
			{
				// something went wrong, or exceeded limit of steps (= went even more wrong)
				DestinationPathPoints.Reset();
				break;
			}

			const FNavMeshNodeFlags LastVisibleFlags(FirstPassPoints[LastVisiblePointIndex].Point.Flags);
			const FNavMeshNodeFlags StartPointFlags(FirstPassPoints[StartPointIndex].Point.Flags);
			bool bWantsVisibilityInsert = true;

			if (StartPointFlags.PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION) 
			{
				// 遇到 NavLink：两端必须原样保留，跳过可见性处理
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex + 1);

				StartPointIndex++;
				LastVisiblePointIndex = StartPointIndex;
				TestedPointIndex = LastVisiblePointIndex + 1;
				
				// skip inserting new points
				bWantsVisibilityInsert = false;
			}
			
			bool bVisible = false; 
			// 只在同一 Area 内做可见性简化（跨 Area 可能代价不同，不可随意跨越）
			if (((LastVisibleFlags.PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION) == 0) && (StartPointFlags.Area == LastVisibleFlags.Area))
			{
				FPathPointInfo LastVisiblePoint;
				bVisible = CheckVisibility( &FirstPassPoints[StartPointIndex], &FirstPassPoints[TestedPointIndex], PathCorridorEdges, Distance, &LastVisiblePoint );
				if (!bVisible)
				{
					if (LastVisiblePoint.Point.Location.IsNearlyZero())
					{
						DestinationPathPoints.Reset();
						break;
					}
					else if (StartPointIndex == LastVisiblePointIndex)
					{
						/** add new point only if we don't see our next location otherwise use last visible point*/
						LastVisiblePoint.Point.Flags = FirstPassPoints[LastVisiblePointIndex].Point.Flags;
						LastVisiblePointIndex = FirstPassPoints.Insert( LastVisiblePoint, StartPointIndex+1 );
						LastPointIndex = FirstPassPoints.Num()-1;

						// TODO: potential infinite loop - keeps inserting point without visibility
					}
				}
			}

			if (bWantsVisibilityInsert)
			{
				if (bVisible) 
				{ 
#if PATH_OFFSET_KEEP_VISIBLE_POINTS
					// 保留 Start 并推进到 Tested（贪心）
					AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
					LastVisiblePointIndex = TestedPointIndex;
					StartPointIndex = LastVisiblePointIndex;
					TestedPointIndex++;
#else
					LastVisiblePointIndex = TestedPointIndex;
					TestedPointIndex++;
#endif
				} 
				else
				{ 
					// 不可见：提交 Start，回退到 LastVisible 继续
					AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
					StartPointIndex = LastVisiblePointIndex;
					TestedPointIndex = LastVisiblePointIndex + 1;
				} 
			}

			// if reached end of path, add current and last points to close it and leave loop
			if (TestedPointIndex > LastPointIndex) 
			{
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, LastPointIndex);
				break; 
			} 
		} 

		if (DestinationPathPoints.Num())
		{
			PathPoints = DestinationPathPoints;
		}
	}
}

// 判断 PathSegmentStartIndex 开始的段是否为 NavLink
bool FNavMeshPath::IsPathSegmentANavLink(const int32 PathSegmentStartIndex) const
{
	return PathPoints.IsValidIndex(PathSegmentStartIndex)
		&& FNavMeshNodeFlags(PathPoints[PathSegmentStartIndex].Flags).IsNavLink();
}

// 调试绘制：基类画折线，本类额外画 Portal Edge 和各路径点的 Area 名称
void FNavMeshPath::DebugDraw(const ANavigationData* NavData, const FColor PathColor, UCanvas* Canvas, const bool bPersistent, const float LifeTime, const uint32 NextPathPointIndex) const
{
	Super::DebugDraw(NavData, PathColor, Canvas, bPersistent, LifeTime, NextPathPointIndex);

#if WITH_RECAST && ENABLE_DRAW_DEBUG
	const ARecastNavMesh* RecastNavMesh = Cast<const ARecastNavMesh>(NavData);		
	const TArray<FNavigationPortalEdge>& Edges = GetPathCorridorEdges();
	const int32 CorridorEdgesCount = Edges.Num();
	const UWorld* World = NavData->GetWorld();

	// 迭代目标：画出每条 Portal Edge（蓝色）
	for (int32 EdgeIndex = 0; EdgeIndex < CorridorEdgesCount; ++EdgeIndex)
	{
		DrawDebugLine(World, Edges[EdgeIndex].Left + NavigationDebugDrawing::PathOffset, Edges[EdgeIndex].Right + NavigationDebugDrawing::PathOffset
			, FColor::Blue, bPersistent, LifeTime, /*DepthPriority*/0
			, /*Thickness*/NavigationDebugDrawing::PathLineThickness);
	}

	if (Canvas && RecastNavMesh && RecastNavMesh->bDrawLabelsOnPathNodes)
	{
		// 在每个路径点上方 Project 到屏幕并绘制"序号: Area 类名"
		UFont* RenderFont = GEngine->GetSmallFont();
		for (int32 VertIdx = 0; VertIdx < PathPoints.Num(); ++VertIdx)
		{
			// draw box at vert
			FVector const VertLoc = PathPoints[VertIdx].Location 
				+ FVector(0, 0, NavigationDebugDrawing::PathNodeBoxExtent.Z*2)
				+ NavigationDebugDrawing::PathOffset;
			const FVector ScreenLocation = Canvas->Project(VertLoc);

			FNavMeshNodeFlags NodeFlags(PathPoints[VertIdx].Flags);
			const UClass* NavAreaClass = RecastNavMesh->GetAreaClass(NodeFlags.Area);

			Canvas->DrawText(RenderFont, FString::Printf(TEXT("%d: %s"), VertIdx, *GetNameSafe(NavAreaClass)), UE_REAL_TO_FLOAT(ScreenLocation.X), UE_REAL_TO_FLOAT(ScreenLocation.Y));
		}
	}
#endif // WITH_RECAST && ENABLE_DRAW_DEBUG
}

// 判断 Other 的 PathCorridor 是否完整是 this 的"末尾子序列"。
// 用途：Repath 时若目标未变且走廊末尾一致，可不中断移动。
bool FNavMeshPath::ContainsWithSameEnd(const FNavMeshPath* Other) const
{
	if (PathCorridor.Num() < Other->PathCorridor.Num())
	{
		return false;
	}

	const NavNodeRef* ThisPathNode = &PathCorridor[PathCorridor.Num()-1];
	const NavNodeRef* OtherPathNode = &Other->PathCorridor[Other->PathCorridor.Num()-1];
	bool bAreTheSame = true;

	// 迭代目标：从末端逆向逐 Poly 比较
	for (int32 NodeIndex = Other->PathCorridor.Num() - 1; NodeIndex >= 0 && bAreTheSame; --NodeIndex, --ThisPathNode, --OtherPathNode)
	{
		bAreTheSame = *ThisPathNode == *OtherPathNode;
	}	

	return bAreTheSame;
}

namespace
{
	// 两点间线段/扩展体盒是否与 Box 相交（用于 DoesPathIntersectBoxImpl）
	FORCEINLINE
	bool CheckIntersectBetweenPoints(const FBox& Box, const FVector* AgentExtent, const FVector& Start, const FVector& End)
	{
		if (FVector::DistSquared(Start, End) > SMALL_NUMBER)
		{
			const FVector Direction = (End - Start);

			FVector HitLocation, HitNormal;
			float HitTime;

			// If we have a valid AgentExtent, then we use an extent box to represent the path
			// Otherwise we use a line to represent the path
			if ((AgentExtent && FMath::LineExtentBoxIntersection(Box, Start, End, *AgentExtent, HitLocation, HitNormal, HitTime)) ||
				(!AgentExtent && FMath::LineBoxIntersection(Box, Start, End, Direction)))
			{
				return true;
			}
		}

		return false;
	}
}

// 走廊版路径与 Box 相交测试：依次用"Portal Edge 中点"串联成折线，做扩展线段 vs Box 求交。
// IntersectingSegmentIndex：首次命中段的 PortalEdge 索引（用于 Repath 分段）。
bool FNavMeshPath::DoesPathIntersectBoxImplementation(const FBox& Box, const FVector& StartLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	bool bIntersects = false;	
	const TArray<FNavigationPortalEdge>& CorridorEdges = GetPathCorridorEdges();
	const uint32 NumCorridorEdges = CorridorEdges.Num();

	// if we have a valid corridor, but the index is out of bounds, we could
	// be checking just the last point, but that would be inconsistent with 
	// FNavMeshPath::DoesPathIntersectBoxImplementation implementation
	// so in this case we just say "Nope, doesn't intersect"
	if (NumCorridorEdges <= 0 || StartingIndex > NumCorridorEdges)
	{
		return false;
	}

	// note that it's a bit simplified. It works
	FVector Start = StartLocation;
	if (CorridorEdges.IsValidIndex(StartingIndex))
	{
		// make sure that Start is initialized correctly when testing from the middle of path (StartingIndex > 0)
		if (CorridorEdges.IsValidIndex(StartingIndex - 1))
		{
			// 从中段开始时，Start 用上一条 Portal Edge 的中点（+AgentExtent.Z 把扫描盒抬到 Agent 中心高度）
			const FNavigationPortalEdge& Edge = CorridorEdges[StartingIndex - 1];
			Start = Edge.Right + (Edge.Left - Edge.Right) / 2 + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);
		}

		// 迭代目标：沿着后续 Portal 中点连线逐段 vs Box 求交
		for (uint32 PortalIndex = StartingIndex; PortalIndex < NumCorridorEdges; ++PortalIndex)
		{
			const FNavigationPortalEdge& Edge = CorridorEdges[PortalIndex];
			const FVector End = Edge.Right + (Edge.Left - Edge.Right) / 2 + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);
			
			if (CheckIntersectBetweenPoints(Box, AgentExtent, Start, End))
			{
				bIntersects = true;
				if (IntersectingSegmentIndex != NULL)
				{
					*IntersectingSegmentIndex = PortalIndex;
				}
				break;
			}

			Start = End;
		}

		// test the last portal->path end line. 
		// 最后一段：从最后一个 Portal 到终点位置
		if (bIntersects == false)
		{
			ensure(PathPoints.Num() >= 2);
			const FVector End = PathPoints.Last().Location + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);

			if (CheckIntersectBetweenPoints(Box, AgentExtent, Start, End))
			{
				bIntersects = true;
				if (IntersectingSegmentIndex != NULL)
				{
					*IntersectingSegmentIndex = NumCorridorEdges;
				}
			}
		}
	}
	else if (NumCorridorEdges > 0 && StartingIndex == NumCorridorEdges) //at last polygon, just after last edge so direct line check 
	{
		// 在最后一个 Poly 中：仅需 StartLocation → 终点直连线 vs Box
		const FVector End = PathPoints.Last().Location + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);
			
		if (CheckIntersectBetweenPoints(Box, AgentExtent, Start, End))
		{
			bIntersects = true;
			if (IntersectingSegmentIndex != NULL)
			{
				*IntersectingSegmentIndex = CorridorEdges.Num();
			}
		}
	}
	
	// just check if path's end is inside the tested box
	// 兜底：路径终点直接落在 Box 内也算相交
	if (bIntersects == false && Box.IsInside(PathPoints.Last().Location))
	{
		bIntersects = true;
		if (IntersectingSegmentIndex != NULL)
		{
			*IntersectingSegmentIndex = CorridorEdges.Num();
		}
	}

	return bIntersects;
}

// 对外入口：已拉绳 → 走基类折线；未拉绳 → 走走廊实现
bool FNavMeshPath::DoesIntersectBox(const FBox& Box, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	if (IsStringPulled())
	{
		return Super::DoesIntersectBox(Box, StartingIndex, IntersectingSegmentIndex);
	}

	bool bParametersValid = true;
	FVector StartLocation = PathPoints[0].Location;

	const TArray<FNavigationPortalEdge>& CorridorEdges = GetPathCorridorEdges();
	if (StartingIndex < uint32(CorridorEdges.Num()))
	{
		// 按 "Portal 中点连线" 的模型重建 Start
		StartLocation = CorridorEdges[StartingIndex].Right + (CorridorEdges[StartingIndex].Left - CorridorEdges[StartingIndex].Right) / 2;
		++StartingIndex;
	}
	else if (StartingIndex > uint32(CorridorEdges.Num()))
	{
		bParametersValid = false;
	}
	// else will be handled by DoesPathIntersectBoxImplementation

	return bParametersValid && DoesPathIntersectBoxImplementation(Box, StartLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
}

// 带 Agent 当前位置的变体；已拉绳走基类，未拉绳直接用 AgentLocation 作为 Start
bool FNavMeshPath::DoesIntersectBox(const FBox& Box, const FVector& AgentLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	if (IsStringPulled())
	{
		return Super::DoesIntersectBox(Box, AgentLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
	}

	return DoesPathIntersectBoxImplementation(Box, AgentLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
}

// 读取节点 Flags：已拉绳 → 从 PathPoints.Flags 解包；未拉绳 → 从 dtNavMesh 查 Poly Flags
bool FNavMeshPath::GetNodeFlags(int32 NodeIdx, FNavMeshNodeFlags& Flags) const
{
	bool bResult = false;

	if (IsStringPulled())
	{
		if (PathPoints.IsValidIndex(NodeIdx))
		{
			Flags = FNavMeshNodeFlags(PathPoints[NodeIdx].Flags);
			bResult = true;
		}
	}
	else
	{
		if (PathCorridor.IsValidIndex(NodeIdx))
		{
#if WITH_RECAST
			const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
			if (MyOwner)
			{
				MyOwner->GetPolyFlags(PathCorridor[NodeIdx], Flags);
				bResult = true;
			}
#endif	// WITH_RECAST
		}
	}

	return bResult;
}

// 取段 SegmentEndIndex 的方向向量（归一化）。
// 未拉绳：走廊"Portal 中点-到-上一中点"方向，首段用"第一个中点 − PathPoints[0]"。
FVector FNavMeshPath::GetSegmentDirection(uint32 SegmentEndIndex) const
{
	if (IsStringPulled())
	{
		return Super::GetSegmentDirection(SegmentEndIndex);
	}
	
	FVector Result = FNavigationSystem::InvalidLocation;
	const TArray<FNavigationPortalEdge>& Corridor = GetPathCorridorEdges();

	if (Corridor.Num() > 0 && PathPoints.Num() > 1)
	{
		if (Corridor.IsValidIndex(SegmentEndIndex))
		{
			if (SegmentEndIndex > 0)
			{
				Result = (Corridor[SegmentEndIndex].GetMiddlePoint() - Corridor[SegmentEndIndex - 1].GetMiddlePoint()).GetSafeNormal();
			}
			else
			{
				Result = (Corridor[0].GetMiddlePoint() - GetPathPoints()[0].Location).GetSafeNormal();
			}
		}
		else if (SegmentEndIndex >= uint32(Corridor.Num()))
		{
			// in this special case return direction of last segment
			Result = (Corridor[Corridor.Num() - 1].GetMiddlePoint() - GetPathPoints()[0].Location).GetSafeNormal();
		}
	}

	return Result;
}

// 反转路径；同时反转走廊/代价/边缓存。
// 用途：反向追寻/调试；Repath 时一般不调用。
void FNavMeshPath::Invert()
{
	Algo::Reverse(PathPoints);
	Algo::Reverse(PathCorridor);
	Algo::Reverse(PathCorridorCost);
	if (bCorridorEdgesGenerated)
	{
		Algo::Reverse(PathCorridorEdges);
	}
}

#if ENABLE_VISUAL_LOG

// VisLog 快照：画多边形走廊 + Area 类名标注。
// 使用 BeginBatchQuery/FinishBatchQuery 聚合批量读锁，避免频繁加锁。
void FNavMeshPath::DescribeSelfToVisLog(FVisualLogEntry* Snapshot) const
{
	if (Snapshot == nullptr)
	{
		return;
	}

	if (IsStringPulled())
	{
		// draw path points only for string pulled paths
		Super::DescribeSelfToVisLog(Snapshot);
	}

	// draw corridor
#if WITH_RECAST
	FVisualLogShapeElement CorridorPoly(EVisualLoggerShapeElement::Polygon);
	CorridorPoly.SetColor(FColorList::Cyan.WithAlpha(100));
	CorridorPoly.Category = LogNavigation.GetCategoryName();
	CorridorPoly.Verbosity = ELogVerbosity::Verbose;
	CorridorPoly.Points.Reserve(PathCorridor.Num() * 6);

	const FVector CorridorOffset = NavigationDebugDrawing::PathOffset * 1.25f;
	int32 NumAreaMark = 1;

	ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(GetNavigationDataUsed());
	if (NavMesh == nullptr)
	{
		return;
	}
	NavMesh->BeginBatchQuery();

	TArray<FVector> Verts;
	// 迭代目标：对走廊里每个多边形，取其 Area/颜色/顶点，画成带透明度的多边形
	for (int32 Idx = 0; Idx < PathCorridor.Num(); Idx++)
	{
		const int32 AreaID = IntCastChecked<int32>(NavMesh->GetPolyAreaID(PathCorridor[Idx]));
		const UClass* AreaClass = NavMesh->GetAreaClass(AreaID);
		
		Verts.Reset();
		const bool bPolyResult = NavMesh->GetPolyVerts(PathCorridor[Idx], Verts);
		if (!bPolyResult || Verts.Num() == 0)
		{
			// probably invalidated polygon, etc. (time sensitive and rare to reproduce issue)
			// Tile 可能在查询期间被卸载；跳过无效 Poly
			continue;
		}

		const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
		const TSubclassOf<UNavAreaBase> DefaultWalkableArea = FNavigationSystem::GetDefaultWalkableArea();
		const FColor PolygonColor = AreaClass != DefaultWalkableArea ? (DefArea ? DefArea->DrawColor : NavMesh->GetConfig().Color) : FColorList::Cyan;

		CorridorPoly.SetColor(PolygonColor.WithAlpha(100));
		CorridorPoly.Points.Reset();
		CorridorPoly.Points.Append(Verts);
		Snapshot->ElementsToDraw.Add(CorridorPoly);

		if (AreaClass && AreaClass != DefaultWalkableArea)
		{
			// 非默认 Area 额外画一根竖线标记，方便区分不同 Area 的多边形
			FVector CenterPt = FVector::ZeroVector;
			for (int32 VIdx = 0; VIdx < Verts.Num(); VIdx++)
			{
				CenterPt += Verts[VIdx];
			}
			CenterPt /= Verts.Num();

			FVisualLogShapeElement AreaMarkElem(EVisualLoggerShapeElement::Segment);
			AreaMarkElem.SetColor(FColorList::Orange);
			AreaMarkElem.Category = LogNavigation.GetCategoryName();
			AreaMarkElem.Verbosity = ELogVerbosity::Verbose;
			AreaMarkElem.Thickness = 2;
			AreaMarkElem.Description = AreaClass->GetName();

			AreaMarkElem.Points.Add(CenterPt + CorridorOffset);
			AreaMarkElem.Points.Add(CenterPt + CorridorOffset + FVector(0,0,100 + static_cast<double>(NumAreaMark) * 50));
			Snapshot->ElementsToDraw.Add(AreaMarkElem);

			NumAreaMark = (NumAreaMark + 1) % 5;
		}
	}

	NavMesh->FinishBatchQuery();
	//Snapshot->ElementsToDraw.Add(CorridorElem);
#endif
}

// VisLog 中简短描述
FString FNavMeshPath::GetDescription() const
{
	return FString::Printf(TEXT("NotifyPathUpdate points:%d corridor length %d valid:%s")
		, PathPoints.Num()
		, PathCorridor.Num()
		, IsValid() ? TEXT("yes") : TEXT("no"));
}

#endif // ENABLE_VISUAL_LOG


//----------------------------------------------------------------------//
// FNavMeshPath
//----------------------------------------------------------------------//
const FNavPathType FNavMeshPath::Type;
	
FNavMeshPath::FNavMeshPath()
	: bWantsStringPulling(true)
	, bWantsPathCorridor(false)
{
	PathType = FNavMeshPath::Type;
	InternalResetNavMeshPath();
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FNavMeshPath::~FNavMeshPath() = default;
FNavMeshPath::FNavMeshPath(const FNavMeshPath&) = default;
FNavMeshPath::FNavMeshPath(FNavMeshPath&& Other) = default;
FNavMeshPath& FNavMeshPath::operator=(const FNavMeshPath& Other) = default;
FNavMeshPath& FNavMeshPath::operator=(FNavMeshPath&& Other) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FNavMeshPath::ResetForRepath()
{
	Super::ResetForRepath();
	InternalResetNavMeshPath();
}

void FNavMeshPath::InternalResetNavMeshPath()
{
	PathCorridor.Reset();
	PathCorridorCost.Reset();
	CustomNavLinkIds.Reset();
	PathCorridorEdges.Reset();

	bCorridorEdgesGenerated = false;
	bDynamic = false;
	bStringPulled = false;

	// keep:
	// - bWantsStringPulling
	// - bWantsPathCorridor
}

FVector::FReal FNavMeshPath::GetStringPulledLength(const int32 StartingPoint) const
{
	if (IsValid() == false || StartingPoint >= PathPoints.Num())
	{
		return 0.f;
	}

	FVector::FReal TotalLength = 0.f;
	const FNavPathPoint* PrevPoint = PathPoints.GetData() + StartingPoint;
	const FNavPathPoint* PathPoint = PrevPoint + 1;

	for (int32 PathPointIndex = StartingPoint + 1; PathPointIndex < PathPoints.Num(); ++PathPointIndex, ++PathPoint, ++PrevPoint)
	{
		TotalLength += FVector::Dist(PrevPoint->Location, PathPoint->Location);
	}

	return TotalLength;
}

FVector::FReal FNavMeshPath::GetPathCorridorLength(const int32 StartingEdge) const
{
	if (bCorridorEdgesGenerated == false)
	{
		return 0.f;
	}
	else if (StartingEdge >= PathCorridorEdges.Num())
	{
		return StartingEdge == 0 && PathPoints.Num() > 1 ? FVector::Dist(PathPoints[0].Location, PathPoints[PathPoints.Num()-1].Location) : 0.;
	}
	
	const FNavigationPortalEdge* PrevEdge = PathCorridorEdges.GetData() + StartingEdge;
	const FNavigationPortalEdge* CorridorEdge = PrevEdge + 1;
	FVector PrevEdgeMiddle = PrevEdge->GetMiddlePoint();

	FVector::FReal TotalLength = StartingEdge == 0 ? FVector::Dist(PathPoints[0].Location, PrevEdgeMiddle)
		: FVector::Dist(PrevEdgeMiddle, PathCorridorEdges[StartingEdge - 1].GetMiddlePoint());

	for (int32 PathPolyIndex = StartingEdge + 1; PathPolyIndex < PathCorridorEdges.Num(); ++PathPolyIndex, ++PrevEdge, ++CorridorEdge)
	{
		const FVector CurrentEdgeMiddle = CorridorEdge->GetMiddlePoint();
		TotalLength += FVector::Dist(CurrentEdgeMiddle, PrevEdgeMiddle);
		PrevEdgeMiddle = CurrentEdgeMiddle;
	}
	// @todo add distance to last point here!
	return TotalLength;
}

const TArray<FNavigationPortalEdge>& FNavMeshPath::GeneratePathCorridorEdges() const
{
#if WITH_RECAST
	// mz@todo the underlying recast function queries the navmesh a portal at a time, 
	// which is a waste of performance. A batch-query function has to be added.
	const int32 CorridorLength = PathCorridor.Num();
	if (CorridorLength != 0 && IsInGameThread() && NavigationDataUsed.IsValid())
	{
		const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
		if (MyOwner)
		{
			MyOwner->GetEdgesForPathCorridor(&PathCorridor, &PathCorridorEdges);
			bCorridorEdgesGenerated = (PathCorridorEdges.Num() > 0);
		}
	}
#endif // WITH_RECAST
	return PathCorridorEdges;
}

void FNavMeshPath::PerformStringPulling(const FVector& StartLoc, const FVector& EndLoc)
{
#if WITH_RECAST
	const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
	if (::IsValid(MyOwner) && PathCorridor.Num())
	{
		bStringPulled = MyOwner->FindStraightPath(StartLoc, EndLoc, PathCorridor, PathPoints, &CustomNavLinkIds);
	}
#endif	// WITH_RECAST
}


#if DEBUG_DRAW_OFFSET
	UWorld* GInternalDebugWorld_ = NULL;
#endif

namespace
{
	struct FPathPointInfo
	{
		FPathPointInfo() 
		{

		}
		FPathPointInfo( const FNavPathPoint& InPoint, const FVector& InEdgePt0, const FVector& InEdgePt1) 
			: Point(InPoint)
			, EdgePt0(InEdgePt0)
			, EdgePt1(InEdgePt1) 
		{ 
			/** Empty */ 
		}

		FNavPathPoint Point;
		FVector EdgePt0;
		FVector EdgePt1;
	};

	FORCEINLINE bool CheckVisibility(const FPathPointInfo* StartPoint, const FPathPointInfo* EndPoint,  TArray<FNavigationPortalEdge>& PathCorridorEdges, FVector::FReal OffsetDistannce, FPathPointInfo* LastVisiblePoint)
	{
		FVector IntersectionPoint = FVector::ZeroVector;
		FVector StartTrace = StartPoint->Point.Location;
		FVector EndTrace = EndPoint->Point.Location;

		// find closest edge to StartPoint
		FVector::FReal BestDistance = TNumericLimits<FVector::FReal>::Max();
		FNavigationPortalEdge* CurrentEdge = NULL;

		FVector::FReal BestEndPointDistance = TNumericLimits<FVector::FReal>::Max();
		FNavigationPortalEdge* EndPointEdge = NULL;
		for (int32 EdgeIndex =0; EdgeIndex < PathCorridorEdges.Num(); ++EdgeIndex)
		{
			FVector::FReal DistToEdge = TNumericLimits<FVector::FReal>::Max();
			FNavigationPortalEdge* Edge = &PathCorridorEdges[EdgeIndex];
			if (BestDistance > FMath::Square(KINDA_SMALL_NUMBER))
			{
				DistToEdge= FMath::PointDistToSegmentSquared(StartTrace, Edge->Left, Edge->Right);
				if (DistToEdge < BestDistance)
				{
					BestDistance = DistToEdge;
					CurrentEdge = Edge;
#if DEBUG_DRAW_OFFSET
					DrawDebugLine( GInternalDebugWorld_, Edge->Left, Edge->Right, FColor::White, true );
#endif
				}
			}

			if (BestEndPointDistance > FMath::Square(KINDA_SMALL_NUMBER))
			{
				DistToEdge= FMath::PointDistToSegmentSquared(EndTrace, Edge->Left, Edge->Right);
				if (DistToEdge < BestEndPointDistance)
				{
					BestEndPointDistance = DistToEdge;
					EndPointEdge = Edge;
				}
			}
		}

		if (CurrentEdge == NULL || EndPointEdge == NULL )
		{
			LastVisiblePoint->Point.Location = FVector::ZeroVector;
			return false;
		}


		if (BestDistance <= FMath::Square(KINDA_SMALL_NUMBER))
		{
			CurrentEdge++;
		}

		if (CurrentEdge == EndPointEdge)
		{
			return true;
		}

		const FVector RayNormal = (StartTrace-EndTrace) .GetSafeNormal() * OffsetDistannce;
		StartTrace = StartTrace + RayNormal;
		EndTrace = EndTrace - RayNormal;

		bool bIsVisible = true;
#if DEBUG_DRAW_OFFSET
		DrawDebugLine( GInternalDebugWorld_, StartTrace, EndTrace, FColor::Yellow, true );
#endif
		const FNavigationPortalEdge* LaseEdge = &PathCorridorEdges[PathCorridorEdges.Num()-1];
		while (CurrentEdge <= EndPointEdge)
		{
			FVector Left = CurrentEdge->Left;
			FVector Right = CurrentEdge->Right;

#if DEBUG_DRAW_OFFSET
			DrawDebugLine( GInternalDebugWorld_, Left, Right, FColor::White, true );
#endif
			bool bIntersected = FMath::SegmentIntersection2D(Left, Right, StartTrace, EndTrace, IntersectionPoint);
			if ( !bIntersected)
			{
				const FVector::FReal EdgeHalfLength = (CurrentEdge->Left - CurrentEdge->Right).Size() * 0.5f;
				const FVector::FReal Distance = FMath::Min(OffsetDistannce, EdgeHalfLength) *  0.1f;
				Left = CurrentEdge->Left + Distance * (CurrentEdge->Right - CurrentEdge->Left).GetSafeNormal();
				Right = CurrentEdge->Right + Distance * (CurrentEdge->Left - CurrentEdge->Right).GetSafeNormal();
				FVector ClosestPointOnRay, ClosestPointOnEdge;
				FMath::SegmentDistToSegment(StartTrace, EndTrace, Right, Left, ClosestPointOnRay, ClosestPointOnEdge);
#if DEBUG_DRAW_OFFSET
				DrawDebugSphere( GInternalDebugWorld_, ClosestPointOnEdge, 10, 8, FColor::Red, true );
#endif
				LastVisiblePoint->Point.Location = ClosestPointOnEdge;
				LastVisiblePoint->EdgePt0= CurrentEdge->Left ;
				LastVisiblePoint->EdgePt1= CurrentEdge->Right ;
				return false;
			}
#if DEBUG_DRAW_OFFSET
			DrawDebugSphere( GInternalDebugWorld_, IntersectionPoint, 8, 8, FColor::White, true );
#endif
			CurrentEdge++;
			bIsVisible = true;
		}

		return bIsVisible;
	}
}

void FNavMeshPath::ApplyFlags(int32 NavDataFlags)
{
	if (NavDataFlags & ERecastPathFlags::SkipStringPulling)
	{
		bWantsStringPulling = false;
	}

	if (NavDataFlags & ERecastPathFlags::GenerateCorridor)
	{
		bWantsPathCorridor = true;
	}
}

void AppendPathPointsHelper(TArray<FNavPathPoint>& PathPoints, const TArray<FPathPointInfo>& SourcePoints, int32 Index)
{
	if (SourcePoints.IsValidIndex(Index) && SourcePoints[Index].Point.NodeRef != 0)
	{
		PathPoints.Add(SourcePoints[Index].Point);
	}
}

void FNavMeshPath::OffsetFromCorners(FVector::FReal Distance)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_OffsetFromCorners);

	const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
	if (MyOwner == nullptr || PathPoints.Num() == 0 || PathPoints.Num() > 100)
	{
		// skip it, there is not need to offset that path from performance point of view
		return;
	}

#if DEBUG_DRAW_OFFSET
	GInternalDebugWorld_ = MyOwner->GetWorld();
	FlushDebugStrings(GInternalDebugWorld_);
	FlushPersistentDebugLines(GInternalDebugWorld_);
#endif

	if (bCorridorEdgesGenerated == false)
	{
		GeneratePathCorridorEdges(); 
	}
	const FVector::FReal DistanceSq = Distance * Distance;
	int32 CurrentEdge = 0;
	bool bNeedToCopyResults = false;
	int32 SingleNodePassCount = 0;

	FNavPathPoint* PathPoint = PathPoints.GetData();
	// it's possible we'll be inserting points into the path, so we need to buffer the result
	TArray<FPathPointInfo> FirstPassPoints;
	FirstPassPoints.Reserve(PathPoints.Num() + 2);
	FirstPassPoints.Add(FPathPointInfo(*PathPoint, FVector::ZeroVector, FVector::ZeroVector));
	++PathPoint;

	// for every point on path find a related corridor edge
	for (int32 PathNodeIndex = 1; PathNodeIndex < PathPoints.Num()-1 && CurrentEdge < PathCorridorEdges.Num();)
	{
		if (FNavMeshNodeFlags(PathPoint->Flags).PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION)
		{
			// put both ends
			FirstPassPoints.Add(FPathPointInfo(*PathPoint, FVector(0), FVector(0)));
			FirstPassPoints.Add(FPathPointInfo(*(PathPoint+1), FVector(0), FVector(0)));
			PathNodeIndex += 2;
			PathPoint += 2;
			continue;
		}

		int32 CloserPoint = -1;
		const FNavigationPortalEdge* Edge = &PathCorridorEdges[CurrentEdge];
		for (int32 EdgeIndex = CurrentEdge; EdgeIndex < PathCorridorEdges.Num(); ++Edge, ++EdgeIndex)
		{
			const FVector::FReal DistToSequence = FMath::PointDistToSegmentSquared(PathPoint->Location, Edge->Left, Edge->Right);
			if (DistToSequence <= FMath::Square(KINDA_SMALL_NUMBER))
			{
				const FVector::FReal LeftDistanceSq = FVector::DistSquared(PathPoint->Location, Edge->Left);
				const FVector::FReal RightDistanceSq = FVector::DistSquared(PathPoint->Location, Edge->Right);
				if (LeftDistanceSq > DistanceSq && RightDistanceSq > DistanceSq)
				{
					++CurrentEdge;
				}
				else
				{
					CloserPoint = LeftDistanceSq < RightDistanceSq ? 0 : 1;
					CurrentEdge = EdgeIndex;
				}
				break;
			}
		}

		if (CloserPoint >= 0)
		{
			bNeedToCopyResults = true;

			Edge = &PathCorridorEdges[CurrentEdge];
			const FVector::FReal ActualOffset = FPlatformMath::Min(Edge->GetLength()/2, Distance);

			FNavPathPoint NewPathPoint = *PathPoint;
			// apply offset 

			const FVector EdgePt0 = Edge->GetPoint(CloserPoint);
			const FVector EdgePt1 = Edge->GetPoint((CloserPoint+1)%2);
			const FVector EdgeDir = EdgePt1 - EdgePt0;
			const FVector EdgeOffset = EdgeDir.GetSafeNormal() * ActualOffset;
			NewPathPoint.Location = EdgePt0 + EdgeOffset;
			// update NodeRef (could be different if this is n-th pass on the same PathPoint
			NewPathPoint.NodeRef = Edge->ToRef;
			FirstPassPoints.Add(FPathPointInfo(NewPathPoint, EdgePt0, EdgePt1));

			// if we've found a matching edge it's possible there's also another one there using the same edge. 
			// that's why we need to repeat the process with the same path point and next edge
			++CurrentEdge;

			// we need to know if we did more than one iteration on a given point
			// if so then we should not add that point in following "else" statement
			++SingleNodePassCount;
		}
		else
		{
			if (SingleNodePassCount == 0)
			{
				// store unchanged
				FirstPassPoints.Add(FPathPointInfo(*PathPoint, FVector(0), FVector(0)));
			}
			else
			{
				SingleNodePassCount = 0;
			}

			++PathNodeIndex;
			++PathPoint;
		}
	}

	if (bNeedToCopyResults)
	{
		if (FirstPassPoints.Num() < 3 || !MyOwner->bUseBetterOffsetsFromCorners)
		{
			FNavPathPoint EndPt = PathPoints.Last();

			PathPoints.Reset();
			for (int32 Index=0; Index < FirstPassPoints.Num(); ++Index)
			{
				PathPoints.Add(FirstPassPoints[Index].Point);
			}

			PathPoints.Add(EndPt);
			return;
		}

		TArray<FNavPathPoint> DestinationPathPoints;
		DestinationPathPoints.Reserve(FirstPassPoints.Num() + 2);

		// don't forget the last point
		FirstPassPoints.Add(FPathPointInfo(PathPoints[PathPoints.Num()-1], FVector::ZeroVector, FVector::ZeroVector));

		int32 StartPointIndex = 0;
		int32 LastVisiblePointIndex = 0;
		int32 TestedPointIndex = 1;
		int32 LastPointIndex = FirstPassPoints.Num()-1;

		const int32 MaxSteps = 200;
		for (int32 StepsLeft = MaxSteps; StepsLeft >= 0; StepsLeft--)
		{ 
			if (StartPointIndex == TestedPointIndex || StepsLeft == 0)
			{
				// something went wrong, or exceeded limit of steps (= went even more wrong)
				DestinationPathPoints.Reset();
				break;
			}

			const FNavMeshNodeFlags LastVisibleFlags(FirstPassPoints[LastVisiblePointIndex].Point.Flags);
			const FNavMeshNodeFlags StartPointFlags(FirstPassPoints[StartPointIndex].Point.Flags);
			bool bWantsVisibilityInsert = true;

			if (StartPointFlags.PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION) 
			{
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex + 1);

				StartPointIndex++;
				LastVisiblePointIndex = StartPointIndex;
				TestedPointIndex = LastVisiblePointIndex + 1;
				
				// skip inserting new points
				bWantsVisibilityInsert = false;
			}
			
			bool bVisible = false; 
			if (((LastVisibleFlags.PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION) == 0) && (StartPointFlags.Area == LastVisibleFlags.Area))
			{
				FPathPointInfo LastVisiblePoint;
				bVisible = CheckVisibility( &FirstPassPoints[StartPointIndex], &FirstPassPoints[TestedPointIndex], PathCorridorEdges, Distance, &LastVisiblePoint );
				if (!bVisible)
				{
					if (LastVisiblePoint.Point.Location.IsNearlyZero())
					{
						DestinationPathPoints.Reset();
						break;
					}
					else if (StartPointIndex == LastVisiblePointIndex)
					{
						/** add new point only if we don't see our next location otherwise use last visible point*/
						LastVisiblePoint.Point.Flags = FirstPassPoints[LastVisiblePointIndex].Point.Flags;
						LastVisiblePointIndex = FirstPassPoints.Insert( LastVisiblePoint, StartPointIndex+1 );
						LastPointIndex = FirstPassPoints.Num()-1;

						// TODO: potential infinite loop - keeps inserting point without visibility
					}
				}
			}

			if (bWantsVisibilityInsert)
			{
				if (bVisible) 
				{ 
#if PATH_OFFSET_KEEP_VISIBLE_POINTS
					AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
					LastVisiblePointIndex = TestedPointIndex;
					StartPointIndex = LastVisiblePointIndex;
					TestedPointIndex++;
#else
					LastVisiblePointIndex = TestedPointIndex;
					TestedPointIndex++;
#endif
				} 
				else
				{ 
					AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
					StartPointIndex = LastVisiblePointIndex;
					TestedPointIndex = LastVisiblePointIndex + 1;
				} 
			}

			// if reached end of path, add current and last points to close it and leave loop
			if (TestedPointIndex > LastPointIndex) 
			{
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, StartPointIndex);
				AppendPathPointsHelper(DestinationPathPoints, FirstPassPoints, LastPointIndex);
				break; 
			} 
		} 

		if (DestinationPathPoints.Num())
		{
			PathPoints = DestinationPathPoints;
		}
	}
}

bool FNavMeshPath::IsPathSegmentANavLink(const int32 PathSegmentStartIndex) const
{
	return PathPoints.IsValidIndex(PathSegmentStartIndex)
		&& FNavMeshNodeFlags(PathPoints[PathSegmentStartIndex].Flags).IsNavLink();
}

void FNavMeshPath::DebugDraw(const ANavigationData* NavData, const FColor PathColor, UCanvas* Canvas, const bool bPersistent, const float LifeTime, const uint32 NextPathPointIndex) const
{
	Super::DebugDraw(NavData, PathColor, Canvas, bPersistent, LifeTime, NextPathPointIndex);

#if WITH_RECAST && ENABLE_DRAW_DEBUG
	const ARecastNavMesh* RecastNavMesh = Cast<const ARecastNavMesh>(NavData);		
	const TArray<FNavigationPortalEdge>& Edges = GetPathCorridorEdges();
	const int32 CorridorEdgesCount = Edges.Num();
	const UWorld* World = NavData->GetWorld();

	for (int32 EdgeIndex = 0; EdgeIndex < CorridorEdgesCount; ++EdgeIndex)
	{
		DrawDebugLine(World, Edges[EdgeIndex].Left + NavigationDebugDrawing::PathOffset, Edges[EdgeIndex].Right + NavigationDebugDrawing::PathOffset
			, FColor::Blue, bPersistent, LifeTime, /*DepthPriority*/0
			, /*Thickness*/NavigationDebugDrawing::PathLineThickness);
	}

	if (Canvas && RecastNavMesh && RecastNavMesh->bDrawLabelsOnPathNodes)
	{
		UFont* RenderFont = GEngine->GetSmallFont();
		for (int32 VertIdx = 0; VertIdx < PathPoints.Num(); ++VertIdx)
		{
			// draw box at vert
			FVector const VertLoc = PathPoints[VertIdx].Location 
				+ FVector(0, 0, NavigationDebugDrawing::PathNodeBoxExtent.Z*2)
				+ NavigationDebugDrawing::PathOffset;
			const FVector ScreenLocation = Canvas->Project(VertLoc);

			FNavMeshNodeFlags NodeFlags(PathPoints[VertIdx].Flags);
			const UClass* NavAreaClass = RecastNavMesh->GetAreaClass(NodeFlags.Area);

			Canvas->DrawText(RenderFont, FString::Printf(TEXT("%d: %s"), VertIdx, *GetNameSafe(NavAreaClass)), UE_REAL_TO_FLOAT(ScreenLocation.X), UE_REAL_TO_FLOAT(ScreenLocation.Y));
		}
	}
#endif // WITH_RECAST && ENABLE_DRAW_DEBUG
}

bool FNavMeshPath::ContainsWithSameEnd(const FNavMeshPath* Other) const
{
	if (PathCorridor.Num() < Other->PathCorridor.Num())
	{
		return false;
	}

	const NavNodeRef* ThisPathNode = &PathCorridor[PathCorridor.Num()-1];
	const NavNodeRef* OtherPathNode = &Other->PathCorridor[Other->PathCorridor.Num()-1];
	bool bAreTheSame = true;

	for (int32 NodeIndex = Other->PathCorridor.Num() - 1; NodeIndex >= 0 && bAreTheSame; --NodeIndex, --ThisPathNode, --OtherPathNode)
	{
		bAreTheSame = *ThisPathNode == *OtherPathNode;
	}	

	return bAreTheSame;
}

namespace
{
	FORCEINLINE
	bool CheckIntersectBetweenPoints(const FBox& Box, const FVector* AgentExtent, const FVector& Start, const FVector& End)
	{
		if (FVector::DistSquared(Start, End) > SMALL_NUMBER)
		{
			const FVector Direction = (End - Start);

			FVector HitLocation, HitNormal;
			float HitTime;

			// If we have a valid AgentExtent, then we use an extent box to represent the path
			// Otherwise we use a line to represent the path
			if ((AgentExtent && FMath::LineExtentBoxIntersection(Box, Start, End, *AgentExtent, HitLocation, HitNormal, HitTime)) ||
				(!AgentExtent && FMath::LineBoxIntersection(Box, Start, End, Direction)))
			{
				return true;
			}
		}

		return false;
	}
}
bool FNavMeshPath::DoesPathIntersectBoxImplementation(const FBox& Box, const FVector& StartLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	bool bIntersects = false;	
	const TArray<FNavigationPortalEdge>& CorridorEdges = GetPathCorridorEdges();
	const uint32 NumCorridorEdges = CorridorEdges.Num();

	// if we have a valid corridor, but the index is out of bounds, we could
	// be checking just the last point, but that would be inconsistent with 
	// FNavMeshPath::DoesPathIntersectBoxImplementation implementation
	// so in this case we just say "Nope, doesn't intersect"
	if (NumCorridorEdges <= 0 || StartingIndex > NumCorridorEdges)
	{
		return false;
	}

	// note that it's a bit simplified. It works
	FVector Start = StartLocation;
	if (CorridorEdges.IsValidIndex(StartingIndex))
	{
		// make sure that Start is initialized correctly when testing from the middle of path (StartingIndex > 0)
		if (CorridorEdges.IsValidIndex(StartingIndex - 1))
		{
			const FNavigationPortalEdge& Edge = CorridorEdges[StartingIndex - 1];
			Start = Edge.Right + (Edge.Left - Edge.Right) / 2 + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);
		}

		for (uint32 PortalIndex = StartingIndex; PortalIndex < NumCorridorEdges; ++PortalIndex)
		{
			const FNavigationPortalEdge& Edge = CorridorEdges[PortalIndex];
			const FVector End = Edge.Right + (Edge.Left - Edge.Right) / 2 + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);
			
			if (CheckIntersectBetweenPoints(Box, AgentExtent, Start, End))
			{
				bIntersects = true;
				if (IntersectingSegmentIndex != NULL)
				{
					*IntersectingSegmentIndex = PortalIndex;
				}
				break;
			}

			Start = End;
		}

		// test the last portal->path end line. 
		if (bIntersects == false)
		{
			ensure(PathPoints.Num() >= 2);
			const FVector End = PathPoints.Last().Location + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);

			if (CheckIntersectBetweenPoints(Box, AgentExtent, Start, End))
			{
				bIntersects = true;
				if (IntersectingSegmentIndex != NULL)
				{
					*IntersectingSegmentIndex = NumCorridorEdges;
				}
			}
		}
	}
	else if (NumCorridorEdges > 0 && StartingIndex == NumCorridorEdges) //at last polygon, just after last edge so direct line check 
	{
		const FVector End = PathPoints.Last().Location + (AgentExtent ? FVector(0.f, 0.f, AgentExtent->Z) : FVector::ZeroVector);
			
		if (CheckIntersectBetweenPoints(Box, AgentExtent, Start, End))
		{
			bIntersects = true;
			if (IntersectingSegmentIndex != NULL)
			{
				*IntersectingSegmentIndex = CorridorEdges.Num();
			}
		}
	}
	
	// just check if path's end is inside the tested box
	if (bIntersects == false && Box.IsInside(PathPoints.Last().Location))
	{
		bIntersects = true;
		if (IntersectingSegmentIndex != NULL)
		{
			*IntersectingSegmentIndex = CorridorEdges.Num();
		}
	}

	return bIntersects;
}

bool FNavMeshPath::DoesIntersectBox(const FBox& Box, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	if (IsStringPulled())
	{
		return Super::DoesIntersectBox(Box, StartingIndex, IntersectingSegmentIndex);
	}

	bool bParametersValid = true;
	FVector StartLocation = PathPoints[0].Location;

	const TArray<FNavigationPortalEdge>& CorridorEdges = GetPathCorridorEdges();
	if (StartingIndex < uint32(CorridorEdges.Num()))
	{
		StartLocation = CorridorEdges[StartingIndex].Right + (CorridorEdges[StartingIndex].Left - CorridorEdges[StartingIndex].Right) / 2;
		++StartingIndex;
	}
	else if (StartingIndex > uint32(CorridorEdges.Num()))
	{
		bParametersValid = false;
	}
	// else will be handled by DoesPathIntersectBoxImplementation

	return bParametersValid && DoesPathIntersectBoxImplementation(Box, StartLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
}

bool FNavMeshPath::DoesIntersectBox(const FBox& Box, const FVector& AgentLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	if (IsStringPulled())
	{
		return Super::DoesIntersectBox(Box, AgentLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
	}

	return DoesPathIntersectBoxImplementation(Box, AgentLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
}

bool FNavMeshPath::GetNodeFlags(int32 NodeIdx, FNavMeshNodeFlags& Flags) const
{
	bool bResult = false;

	if (IsStringPulled())
	{
		if (PathPoints.IsValidIndex(NodeIdx))
		{
			Flags = FNavMeshNodeFlags(PathPoints[NodeIdx].Flags);
			bResult = true;
		}
	}
	else
	{
		if (PathCorridor.IsValidIndex(NodeIdx))
		{
#if WITH_RECAST
			const ARecastNavMesh* MyOwner = Cast<ARecastNavMesh>(GetNavigationDataUsed());
			if (MyOwner)
			{
				MyOwner->GetPolyFlags(PathCorridor[NodeIdx], Flags);
				bResult = true;
			}
#endif	// WITH_RECAST
		}
	}

	return bResult;
}

FVector FNavMeshPath::GetSegmentDirection(uint32 SegmentEndIndex) const
{
	if (IsStringPulled())
	{
		return Super::GetSegmentDirection(SegmentEndIndex);
	}
	
	FVector Result = FNavigationSystem::InvalidLocation;
	const TArray<FNavigationPortalEdge>& Corridor = GetPathCorridorEdges();

	if (Corridor.Num() > 0 && PathPoints.Num() > 1)
	{
		if (Corridor.IsValidIndex(SegmentEndIndex))
		{
			if (SegmentEndIndex > 0)
			{
				Result = (Corridor[SegmentEndIndex].GetMiddlePoint() - Corridor[SegmentEndIndex - 1].GetMiddlePoint()).GetSafeNormal();
			}
			else
			{
				Result = (Corridor[0].GetMiddlePoint() - GetPathPoints()[0].Location).GetSafeNormal();
			}
		}
		else if (SegmentEndIndex >= uint32(Corridor.Num()))
		{
			// in this special case return direction of last segment
			Result = (Corridor[Corridor.Num() - 1].GetMiddlePoint() - GetPathPoints()[0].Location).GetSafeNormal();
		}
	}

	return Result;
}

void FNavMeshPath::Invert()
{
	Algo::Reverse(PathPoints);
	Algo::Reverse(PathCorridor);
	Algo::Reverse(PathCorridorCost);
	if (bCorridorEdgesGenerated)
	{
		Algo::Reverse(PathCorridorEdges);
	}
}

#if ENABLE_VISUAL_LOG

void FNavMeshPath::DescribeSelfToVisLog(FVisualLogEntry* Snapshot) const
{
	if (Snapshot == nullptr)
	{
		return;
	}

	if (IsStringPulled())
	{
		// draw path points only for string pulled paths
		Super::DescribeSelfToVisLog(Snapshot);
	}

	// draw corridor
#if WITH_RECAST
	FVisualLogShapeElement CorridorPoly(EVisualLoggerShapeElement::Polygon);
	CorridorPoly.SetColor(FColorList::Cyan.WithAlpha(100));
	CorridorPoly.Category = LogNavigation.GetCategoryName();
	CorridorPoly.Verbosity = ELogVerbosity::Verbose;
	CorridorPoly.Points.Reserve(PathCorridor.Num() * 6);

	const FVector CorridorOffset = NavigationDebugDrawing::PathOffset * 1.25f;
	int32 NumAreaMark = 1;

	ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(GetNavigationDataUsed());
	if (NavMesh == nullptr)
	{
		return;
	}
	NavMesh->BeginBatchQuery();

	TArray<FVector> Verts;
	for (int32 Idx = 0; Idx < PathCorridor.Num(); Idx++)
	{
		const int32 AreaID = IntCastChecked<int32>(NavMesh->GetPolyAreaID(PathCorridor[Idx]));
		const UClass* AreaClass = NavMesh->GetAreaClass(AreaID);
		
		Verts.Reset();
		const bool bPolyResult = NavMesh->GetPolyVerts(PathCorridor[Idx], Verts);
		if (!bPolyResult || Verts.Num() == 0)
		{
			// probably invalidated polygon, etc. (time sensitive and rare to reproduce issue)
			continue;
		}

		const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
		const TSubclassOf<UNavAreaBase> DefaultWalkableArea = FNavigationSystem::GetDefaultWalkableArea();
		const FColor PolygonColor = AreaClass != DefaultWalkableArea ? (DefArea ? DefArea->DrawColor : NavMesh->GetConfig().Color) : FColorList::Cyan;

		CorridorPoly.SetColor(PolygonColor.WithAlpha(100));
		CorridorPoly.Points.Reset();
		CorridorPoly.Points.Append(Verts);
		Snapshot->ElementsToDraw.Add(CorridorPoly);

		if (AreaClass && AreaClass != DefaultWalkableArea)
		{
			FVector CenterPt = FVector::ZeroVector;
			for (int32 VIdx = 0; VIdx < Verts.Num(); VIdx++)
			{
				CenterPt += Verts[VIdx];
			}
			CenterPt /= Verts.Num();

			FVisualLogShapeElement AreaMarkElem(EVisualLoggerShapeElement::Segment);
			AreaMarkElem.SetColor(FColorList::Orange);
			AreaMarkElem.Category = LogNavigation.GetCategoryName();
			AreaMarkElem.Verbosity = ELogVerbosity::Verbose;
			AreaMarkElem.Thickness = 2;
			AreaMarkElem.Description = AreaClass->GetName();

			AreaMarkElem.Points.Add(CenterPt + CorridorOffset);
			AreaMarkElem.Points.Add(CenterPt + CorridorOffset + FVector(0,0,100 + static_cast<double>(NumAreaMark) * 50));
			Snapshot->ElementsToDraw.Add(AreaMarkElem);

			NumAreaMark = (NumAreaMark + 1) % 5;
		}
	}

	NavMesh->FinishBatchQuery();
	//Snapshot->ElementsToDraw.Add(CorridorElem);
#endif
}

FString FNavMeshPath::GetDescription() const
{
	return FString::Printf(TEXT("NotifyPathUpdate points:%d corridor length %d valid:%s")
		, PathPoints.Num()
		, PathCorridor.Num()
		, IsValid() ? TEXT("yes") : TEXT("no"));
}

#endif // ENABLE_VISUAL_LOG
