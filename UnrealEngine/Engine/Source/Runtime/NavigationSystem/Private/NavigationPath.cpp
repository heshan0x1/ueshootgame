// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// NavigationPath.cpp —— FNavigationPath（C++ 底层路径） + UNavigationPath
// （UObject 外壳/BP 包装）的实现。
//
// 本文件两大块：
//   1) FNavigationPath：非 UObject 的"纯 C++ 路径数据"
//      - 持有 PathPoints 数组、ShortcutNodeRefs、GoalActor/SourceActor 弱引用、
//        Observer 多播委托、NavigationDataUsed 弱指针等；
//      - ResetForRepath 与 InternalResetNavigationPath 的分工：
//        前者是公开 API（派生类可 override 以保留派生字段），后者做"通用字段
//        的物理重置"，并精心保留 GoalActor、Filter、NavigationDataUsed 等
//        "Repath 复用"所必需的字段。
//      - TickPathObservation：检测 GoalActor 是否偏离 TetherDistance → 请求 Repath。
//      - Invalidate：下层 NavData 变化时广播事件并按 bDoAutoUpdateOnInvalidation
//        决定是否自动 Repath。
//   2) UNavigationPath：对 FNavPathSharedPtr 的 UObject 外壳
//      - BeginDestroy 取消 observer 防回调野指针；
//      - OnPathEvent：底层 FNavigationPath 事件 → 更新 bIsValid、PathPoints →
//        广播到蓝图委托 PathUpdatedNotifier；
//      - SetPath：解绑旧观察 → 绑定新观察 → 同步刷新点。
// ============================================================================

#include "NavigationPath.h"
#include "EngineStats.h"
#include "EngineGlobals.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "NavAreas/NavArea.h"
#include "Debug/DebugDrawService.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationPath)

#define DEBUG_DRAW_OFFSET 0
#define PATH_OFFSET_KEEP_VISIBLE_POINTS 1


//----------------------------------------------------------------------//
// FNavigationPath
//----------------------------------------------------------------------//
// 类型标签：所有 FNavigationPath 原生实例共用此 Type。派生类（如 FNavMeshPath、
// FAbstractNavigationPath）会在自己的源文件中定义独立的 static Type 变量。
const FNavPathType FNavigationPath::Type;

FNavigationPath::FNavigationPath()
	: GoalActorAsNavAgent(nullptr)
	, SourceActorAsNavAgent(nullptr)
	, PathType(FNavigationPath::Type)
	, bDoAutoUpdateOnInvalidation(true)
	, bIgnoreInvalidation(false)
	, bUpdateStartPointOnRepath(true)
	, bUpdateEndPointOnRepath(true)
	, bWaitingForRepath(false)
	, bUseOnPathUpdatedNotify(false)
	, LastUpdateTimeStamp(-1.f)	// indicates that it has not been set
	, GoalActorLocationTetherDistanceSq(-1.f)
	, GoalActorLastLocation(FVector::ZeroVector)
{
	// 调 Internal 版本把 PathPoints/Base/bUpToDate 等"每次重算都清"的字段置为初始值
	InternalResetNavigationPath();
}

// 便捷构造：直接给一串世界坐标构造"已就绪"路径，InBase 做为 Attach 基座
FNavigationPath::FNavigationPath(const TArray<FVector>& Points, AActor* InBase)
	: GoalActorAsNavAgent(nullptr)
	, SourceActorAsNavAgent(nullptr)
	, PathType(FNavigationPath::Type)
	, bDoAutoUpdateOnInvalidation(true)
	, bIgnoreInvalidation(false)
	, bUpdateStartPointOnRepath(true)
	, bUpdateEndPointOnRepath(true)
	, bWaitingForRepath(false)
	, bUseOnPathUpdatedNotify(false)
	, LastUpdateTimeStamp(-1.f)	// indicates that it has not been set
	, GoalActorLocationTetherDistanceSq(-1.f)
	, GoalActorLastLocation(FVector::ZeroVector)
{
	InternalResetNavigationPath();
	MarkReady();

	Base = InBase;

	PathPoints.AddZeroed(Points.Num());
	// 将输入的世界坐标包装成 FBasedPosition 以便跟随 Base 移动（相对坐标存储）
	for (int32 i = 0; i < Points.Num(); i++)
	{
		FBasedPosition BasedPoint(InBase, Points[i]);
		PathPoints[i] = FNavPathPoint(*BasedPoint);
	}
}

FNavigationPath::~FNavigationPath() = default;

// InternalResetNavigationPath vs ResetForRepath 的区别：
//  - InternalResetNavigationPath 只是"物理字段重置"，不会触发 Observer 事件。
//  - ResetForRepath 是 public API：派生类可以 override 它在重置前后做派生字段
//    的保留/更新。
//  关键不变量（注释里"keep"列表）：
//    所有与"寻路目标/源/过滤器/当前 NavData/观察者/自动更新标志"相关的字段
//    都必须保留，这样 Repath 才能 in-place 重用这条 path 实例。
void FNavigationPath::InternalResetNavigationPath()
{
	ShortcutNodeRefs.Reset();
	PathPoints.Reset();
	Base.Reset();

	bUpToDate = true;
	bIsReady = false;
	bIsPartial = false;
	bReachedSearchLimit = false;
	bObservingGoalActor = GoalActor.IsValid();

	// keep:
	// - GoalActor
	// - GoalActorAsNavAgent
	// - SourceActor
	// - SourceActorAsNavAgent
	// - Querier
	// - Filter
	// - PathType
	// - ObserverDelegate
	// - bDoAutoUpdateOnInvalidation
	// - bIgnoreInvalidation
	// - bUpdateStartPointOnRepath
	// - bUpdateEndPointOnRepath
	// - bWaitingForRepath
	// - NavigationDataUsed
	// - LastUpdateTimeStamp
	// - GoalActorLocationTetherDistanceSq
	// - GoalActorLastLocation
}

// 获取寻路"终点"：若观察了 GoalActor 则以 Actor/NavAgent 当前位置为准，否则用 PathPoints 末端
FVector FNavigationPath::GetGoalLocation() const
{
	return GoalActor != NULL ? (GoalActorAsNavAgent != NULL ? GoalActorAsNavAgent->GetNavAgentLocation() : GoalActor->GetActorLocation()) : GetEndLocation();
}

// 获取"起点"：同上逻辑——SourceActor 优先
FVector FNavigationPath::GetPathFindingStartLocation() const
{
	return SourceActor != NULL ? (SourceActorAsNavAgent != NULL ? SourceActorAsNavAgent->GetNavAgentLocation() : SourceActor->GetActorLocation()) : GetStartLocation();
}

// 启用 GoalActor 观察：以后每次 TickPathObservation 都会检查 Actor 是否偏离
// 超过 TetherDistance，超过则触发 Repath。
// 只能对"由 NavData 生成的路径"启用（否则 NavigationDataUsed 为空无法 Repath）。
void FNavigationPath::SetGoalActorObservation(const AActor& ActorToObserve, float TetherDistance)
{
	if (NavigationDataUsed.IsValid() == false)
	{
		// this mechanism is available only for navigation-generated paths
		UE_LOG(LogNavigation, Warning, TEXT("Updating navigation path on goal actor's location change is available only for navigation-generated paths. Called for %s")
			, *GetNameSafe(&ActorToObserve));
		return;
	}

	// register for path observing only if we weren't registered already
	// 若此前没有 GoalActor，则首次注册到 NavData 的 ObservedPaths 列表
	const bool RegisterForPathUpdates = (GoalActor.IsValid() == false);
	GoalActor = &ActorToObserve;
	checkSlow(GoalActor.IsValid());
	GoalActorAsNavAgent = Cast<INavAgentInterface>(&ActorToObserve);
	GoalActorLocationTetherDistanceSq = FMath::Square(TetherDistance);
	bObservingGoalActor = true;
	UpdateLastRepathGoalLocation();

	if (RegisterForPathUpdates)
	{
		NavigationDataUsed->RegisterObservedPath(AsShared());
	}
}

void FNavigationPath::SetSourceActor(const AActor& InSourceActor)
{
	SourceActor = &InSourceActor;
	SourceActorAsNavAgent = Cast<INavAgentInterface>(&InSourceActor);
}

// 缓存 GoalActor 当前位置——作为下次 Tether 检测的参考点
void FNavigationPath::UpdateLastRepathGoalLocation()
{
	if (GoalActor.IsValid())
	{
		GoalActorLastLocation = GoalActorAsNavAgent ? GoalActorAsNavAgent->GetNavAgentLocation() : GoalActor->GetActorLocation();
	}
}

// 由 NavData 每帧调用：若 GoalActor 偏离超过 TetherDistance，返回 RequestRepath
EPathObservationResult::Type FNavigationPath::TickPathObservation()
{
	if (bObservingGoalActor == false || GoalActor.IsValid() == false)
	{
		return EPathObservationResult::NoLongerObserving;
	}

	const FVector GoalLocation = GoalActorAsNavAgent != NULL ? GoalActorAsNavAgent->GetNavAgentLocation() : GoalActor->GetActorLocation();
	return FVector::DistSquared(GoalLocation, GoalActorLastLocation) <= GoalActorLocationTetherDistanceSq ? EPathObservationResult::NoChange : EPathObservationResult::RequestRepath;
}

void FNavigationPath::DisableGoalActorObservation()
{
	GoalActor = NULL;
	GoalActorAsNavAgent = NULL;
	GoalActorLocationTetherDistanceSq = -1.f;
	bObservingGoalActor = false;
}

// 标记路径失效：
//  - bIgnoreInvalidation 可临时屏蔽这类通知（例如批量重建期间）
//  - 广播 Invalidated 事件给所有观察者（UNavigationPath 会收到）
//  - 若启用 AutoUpdate 则顺便发一个 RePath 请求给 NavData
void FNavigationPath::Invalidate()
{
	if (!bIgnoreInvalidation)
	{
		bUpToDate = false;
		ObserverDelegate.Broadcast(this, ENavPathEvent::Invalidated);
		if (bDoAutoUpdateOnInvalidation && NavigationDataUsed.IsValid())
		{
			bWaitingForRepath = true;
			NavigationDataUsed->RequestRePath(AsShared(), ENavPathUpdateType::NavigationChanged);
		}
	}
}

void FNavigationPath::RePathFailed()
{
	ObserverDelegate.Broadcast(this, ENavPathEvent::RePathFailed);
	bWaitingForRepath = false;
}

// 默认实现：直接调用 Internal 版。派生类可 override 以保留自己的附加字段。
void FNavigationPath::ResetForRepath()
{
	InternalResetNavigationPath();
}

// 调试绘制：把路径点画成盒+连线。已经走过的段用灰色（VertIdx < NextPathPointIndex）。
// 若启用了 GoalActor 观察还会画 Tether 圆柱与目标连线。
void FNavigationPath::DebugDraw(const ANavigationData* NavData, FColor PathColor, UCanvas* Canvas, bool bPersistent, float LifeTime, const uint32 NextPathPointIndex) const
{
#if ENABLE_DRAW_DEBUG

	static const FColor Grey(100,100,100);
	const int32 NumPathVerts = PathPoints.Num();

	UWorld* World = NavData->GetWorld();

	for (int32 VertIdx = 0; VertIdx < NumPathVerts-1; ++VertIdx)
	{
		// draw box at vert
		FVector const VertLoc = PathPoints[VertIdx].Location + NavigationDebugDrawing::PathOffset;
		DrawDebugSolidBox(World, VertLoc, NavigationDebugDrawing::PathNodeBoxExtent, VertIdx < int32(NextPathPointIndex) ? Grey : PathColor, bPersistent, LifeTime);

		// draw line to next loc
		FVector const NextVertLoc = PathPoints[VertIdx+1].Location + NavigationDebugDrawing::PathOffset;
		DrawDebugLine(World, VertLoc, NextVertLoc, VertIdx < int32(NextPathPointIndex)-1 ? Grey : PathColor, bPersistent
			, LifeTime, /*DepthPriority*/0
			, /*Thickness*/NavigationDebugDrawing::PathLineThickness);
	}

	// draw last vert
	if (NumPathVerts > 0)
	{
		DrawDebugBox(World, PathPoints[NumPathVerts-1].Location + NavigationDebugDrawing::PathOffset, FVector(15.), PathColor, bPersistent, LifeTime);
	}

	// if observing goal actor draw a radius and a line to the goal
	if (GoalActor.IsValid())
	{
		const FVector GoalLocation = GetGoalLocation() + NavigationDebugDrawing::PathOffset;
		const FVector EndLocation = GetEndLocation() + NavigationDebugDrawing::PathOffset;
		static const FVector CylinderHalfHeight = FVector::UpVector * 10.;
		DrawDebugCylinder(World, EndLocation - CylinderHalfHeight, EndLocation + CylinderHalfHeight, FMath::Sqrt(GoalActorLocationTetherDistanceSq), 16, PathColor, bPersistent, LifeTime);
		DrawDebugLine(World, EndLocation, GoalLocation, Grey, bPersistent, LifeTime);
	}

#endif
}

// 是否包含某多边形 NodeRef：先查 PathPoints[i].NodeRef 逐点，再查 ShortcutNodeRefs
// （String-Pull 过程中被"剪掉"的中间节点）
bool FNavigationPath::ContainsNode(NavNodeRef NodeRef) const
{
	for (int32 Index = 0; Index < PathPoints.Num(); Index++)
	{
		if (PathPoints[Index].NodeRef == NodeRef)
		{
			return true;
		}
	}

	return ShortcutNodeRefs.Find(NodeRef) != INDEX_NONE;
}

// 从给定位置到路径末端的几何距离（给 AI 做"剩余距离"查询用）
// NextPathPointIndex 是下一个未走到的路径点索引；SegmentStart 通常是 Agent 当前位置。
FVector::FReal FNavigationPath::GetLengthFromPosition(FVector SegmentStart, uint32 NextPathPointIndex) const
{
	if (NextPathPointIndex >= (uint32)PathPoints.Num())
	{
		return 0;
	}
	
	const uint32 PathPointsCount = PathPoints.Num();
	FVector::FReal PathDistance = 0.;

	// 迭代目标：从 SegmentStart 出发，沿 PathPoints[NextPathPointIndex..] 顺序累加距离
	for (uint32 PathIndex = NextPathPointIndex; PathIndex < PathPointsCount; ++PathIndex)
	{
		const FVector SegmentEnd = PathPoints[PathIndex].Location;
		PathDistance += FVector::Dist(SegmentStart, SegmentEnd);
		SegmentStart = SegmentEnd;
	}

	return PathDistance;
}

bool FNavigationPath::ContainsCustomLink(FNavLinkId LinkUniqueId) const
{
	if (LinkUniqueId == FNavLinkId::Invalid)
	{
		return false;
	}

	for (int32 i = 0; i < PathPoints.Num(); i++)
	{
		if (PathPoints[i].CustomNavLinkId == LinkUniqueId)
		{
			return true;
		}
	}

	return false;
}

bool FNavigationPath::ContainsAnyCustomLink() const
{
	for (int32 i = 0; i < PathPoints.Num(); i++)
	{
		if (PathPoints[i].CustomNavLinkId != FNavLinkId::Invalid)
		{
			return true;
		}
	}

	return false;
}

// 路径某段与 Box 是否相交。
// - AgentExtent 有值：用"胶囊/Box 扫掠"做相交（考虑 Agent 体积）；
// - AgentExtent 为空：退化为线段-Box 相交。
// 找到第一段相交就返回 true（并可选返回段号）。
FORCEINLINE bool FNavigationPath::DoesPathIntersectBoxImplementation(const FBox& Box, const FVector& StartLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{	
	bool bIntersects = false;

	FVector Start = StartLocation;
	for (int32 PathPointIndex = int32(StartingIndex); PathPointIndex < PathPoints.Num(); ++PathPointIndex)
	{
		const FVector End = PathPoints[PathPointIndex].Location;
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
				bIntersects = true;
				if (IntersectingSegmentIndex != NULL)
				{
					*IntersectingSegmentIndex = PathPointIndex;
				}
				break;
			}
		}

		Start = End;
	}

	return bIntersects;
}

bool FNavigationPath::DoesIntersectBox(const FBox& Box, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	// iterate over all segments and check if any intersects with given box
	bool bIntersects = false;
	int32 PathPointIndex = INDEX_NONE;

	if (PathPoints.Num() > 1 && PathPoints.IsValidIndex(int32(StartingIndex)))
	{
		bIntersects = DoesPathIntersectBoxImplementation(Box, PathPoints[StartingIndex].Location, StartingIndex + 1, IntersectingSegmentIndex, AgentExtent);
	}

	return bIntersects;
}

bool FNavigationPath::DoesIntersectBox(const FBox& Box, const FVector& AgentLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const
{
	// iterate over all segments and check if any intersects with given box
	bool bIntersects = false;
	int32 PathPointIndex = INDEX_NONE;

	if (PathPoints.Num() > 1 && PathPoints.IsValidIndex(int32(StartingIndex)))
	{
		bIntersects = DoesPathIntersectBoxImplementation(Box, AgentLocation, StartingIndex, IntersectingSegmentIndex, AgentExtent);
	}

	return bIntersects;
}

FVector FNavigationPath::GetSegmentDirection(uint32 SegmentEndIndex) const
{
	FVector Result = FNavigationSystem::InvalidLocation;

	// require at least two points
	if (PathPoints.Num() > 1)
	{
		if (PathPoints.IsValidIndex(SegmentEndIndex))
		{
			if (SegmentEndIndex > 0)
			{
				Result = (PathPoints[SegmentEndIndex].Location - PathPoints[SegmentEndIndex - 1].Location).GetSafeNormal();
			}
			else
			{
				// for '0'-th segment returns same as for 1st segment 
				Result = (PathPoints[1].Location - PathPoints[0].Location).GetSafeNormal();
			}
		}
		else if (SegmentEndIndex >= uint32(GetPathPoints().Num()))
		{
			// in this special case return direction of last segment
			Result = (PathPoints[PathPoints.Num() - 1].Location - PathPoints[PathPoints.Num() - 2].Location).GetSafeNormal();
		}
	}

	return Result;
}

FBasedPosition FNavigationPath::GetPathPointLocation(uint32 Index) const
{
	FBasedPosition BasedPt;
	if (PathPoints.IsValidIndex(Index))
	{
		BasedPt.Base = Base.Get();
		BasedPt.Position = PathPoints[Index].Location;
	}

	return BasedPt;
}

#if ENABLE_VISUAL_LOG

void FNavigationPath::DescribeSelfToVisLog(FVisualLogEntry* Snapshot) const 
{
	if (Snapshot == nullptr)
	{
		return;
	}

	const int32 NumPathVerts = PathPoints.Num();
	FVisualLogShapeElement Element(EVisualLoggerShapeElement::Path);
	Element.Category = LogNavigation.GetCategoryName();
	Element.SetColor(FColorList::Green);
	Element.Points.Reserve(NumPathVerts);
	Element.Thickness = 3;
	
	for (int32 VertIdx = 0; VertIdx < NumPathVerts; ++VertIdx)
	{
		Element.Points.Add(PathPoints[VertIdx].Location + NavigationDebugDrawing::PathOffset);
	}

	Snapshot->ElementsToDraw.Add(Element);
}

FString FNavigationPath::GetDescription() const
{
	return FString::Printf(TEXT("NotifyPathUpdate points:%d valid:%s")
		, PathPoints.Num()
		, IsValid() ? TEXT("yes") : TEXT("no"));
}

#endif // ENABLE_VISUAL_LOG

//----------------------------------------------------------------------//
// UNavigationPath
//----------------------------------------------------------------------//

UNavigationPath::UNavigationPath(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsValid(false)
	, bDebugDrawingEnabled(false)
	, DebugDrawingColor(FColor::White)
	, SharedPath(NULL)
{	
	// 为非 CDO 实例预构建一个以自身为 this 的委托，等 SetPath 时绑到 SharedPath 上。
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		PathObserver = FNavigationPath::FPathObserverDelegate::FDelegate::CreateUObject(this, &UNavigationPath::OnPathEvent);
	}
}

// 必须在析构链里反注册 observer，避免 SharedPath 稍后事件回到野指针
void UNavigationPath::BeginDestroy()
{
	if (SharedPath.IsValid())
	{
		SharedPath->RemoveObserver(PathObserverDelegateHandle);
	}
	Super::BeginDestroy();
}

// SharedPath 事件回调：
//  - 先广播到 BP 委托；
//  - 再根据 SharedPath 是否仍然有效同步 bIsValid；
//  - 有效则从 NativePath 更新 PathPoints（FVector 列表）供 BP 读取。
void UNavigationPath::OnPathEvent(FNavigationPath* UpdatedPath, ENavPathEvent::Type PathEvent)
{
	if (UpdatedPath == SharedPath.Get())
	{
		PathUpdatedNotifier.Broadcast(this, PathEvent);
		if (SharedPath.IsValid() && SharedPath->IsValid())
		{
			bIsValid = true;
			SetPathPointsFromPath(*UpdatedPath);
		}
		else
		{
			bIsValid = false;
		}
	}
}

FString UNavigationPath::GetDebugString() const
{
	check((SharedPath.IsValid() && SharedPath->IsValid()) == !!bIsValid);
	if (!bIsValid)
	{
		return TEXT("Invalid path");
	}

	return FString::Printf(TEXT("Path: points %d%s%s"), SharedPath->GetPathPoints().Num()
		, SharedPath->IsPartial() ? TEXT(", partial") : TEXT("")
		, SharedPath->IsUpToDate() ? TEXT("") : TEXT(", OUT OF DATE!")
		);
}

void UNavigationPath::DrawDebug(UCanvas* Canvas, APlayerController*)
{
	if (SharedPath.IsValid())
	{
		SharedPath->DebugDraw(SharedPath->GetNavigationDataUsed(), DebugDrawingColor, Canvas, /*bPersistent=*/false, -1.f);
	}
}

void UNavigationPath::EnableDebugDrawing(bool bShouldDrawDebugData, FLinearColor PathColor)
{
	DebugDrawingColor = PathColor.ToFColor(true);

	if (bDebugDrawingEnabled == bShouldDrawDebugData)
	{
		return;
	}

	bDebugDrawingEnabled = bShouldDrawDebugData;
	if (bShouldDrawDebugData)
	{
		DrawDebugDelegateHandle = UDebugDrawService::Register(TEXT("Navigation"), FDebugDrawDelegate::CreateUObject(this, &UNavigationPath::DrawDebug));
	}
	else
	{
		UDebugDrawService::Unregister(DrawDebugDelegateHandle);
	}
}

void UNavigationPath::EnableRecalculationOnInvalidation(const ENavigationOptionFlag DoRecalculation)
{
	if (DoRecalculation != RecalculateOnInvalidation)
	{
		RecalculateOnInvalidation = DoRecalculation;
		if (!!bIsValid && RecalculateOnInvalidation != ENavigationOptionFlag::Default)
		{
			SharedPath->EnableRecalculationOnInvalidation(RecalculateOnInvalidation == ENavigationOptionFlag::Enable);
		}
	}
}

double UNavigationPath::GetPathLength() const
{
	check((SharedPath.IsValid() && SharedPath->IsValid()) == !!bIsValid);
	return !!bIsValid ? SharedPath->GetLength() : -1.;
}

double UNavigationPath::GetPathCost() const
{
	check((SharedPath.IsValid() && SharedPath->IsValid()) == !!bIsValid);
	return !!bIsValid ? SharedPath->GetCost() : -1.;
}

bool UNavigationPath::IsPartial() const
{
	check((SharedPath.IsValid() && SharedPath->IsValid()) == !!bIsValid);
	return !!bIsValid && SharedPath->IsPartial();
}

bool UNavigationPath::IsValid() const
{
	check((SharedPath.IsValid() && SharedPath->IsValid()) == !!bIsValid);
	return !!bIsValid;
}

bool UNavigationPath::IsStringPulled() const
{
	return false;
}

// 绑/解绑 SharedPath：保证同一时间只有一个 FNavigationPath 被观察。
// 若新 Path 为空，则视作"清空"，发出 ENavPathEvent::Cleared 事件。
// RecalculateOnInvalidation 若不是 Default，会向新路径下发"自动重算"开关。
void UNavigationPath::SetPath(FNavPathSharedPtr NewSharedPath)
{
	FNavigationPath* NewPath = NewSharedPath.Get();
	if (SharedPath.Get() != NewPath)
	{
		if (SharedPath.IsValid())
		{
			SharedPath->RemoveObserver(PathObserverDelegateHandle);
		}
		SharedPath = NewSharedPath;
		if (NewPath != NULL)
		{
			PathObserverDelegateHandle = NewPath->AddObserver(PathObserver);

			if (RecalculateOnInvalidation != ENavigationOptionFlag::Default)
			{
				NewPath->EnableRecalculationOnInvalidation(RecalculateOnInvalidation == ENavigationOptionFlag::Enable);
			}
			
			SetPathPointsFromPath(*NewPath);
		}
		else
		{
			PathPoints.Reset();
		}

		// 手动触发一次 OnPathEvent，让下游蓝图委托立即收到 "NewPath" 或 "Cleared"
		OnPathEvent(NewPath, NewPath != NULL ? ENavPathEvent::NewPath : ENavPathEvent::Cleared);
	}
}

// FNavPathPoint 有额外字段（NodeRef、AreaFlags 等），BP 只关心坐标，这里做投影。
void UNavigationPath::SetPathPointsFromPath(FNavigationPath& NativePath)
{
	PathPoints.Reset(NativePath.GetPathPoints().Num());
	for (const auto& PathPoint : NativePath.GetPathPoints())
	{
		PathPoints.Add(PathPoint.Location);
	}
}