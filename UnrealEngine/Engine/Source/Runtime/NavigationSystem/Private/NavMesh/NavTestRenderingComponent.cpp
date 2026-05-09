// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NavTestRenderingComponent.cpp —— 中文总览
// ---------------------------------------------------------------------------
// 实现 ANavigationTestingActor 的可视化：
//  - FNavTestSceneProxy 构造时把 Actor 上的路径/搜索步数据快照到渲染线程；
//  - GetDynamicMeshElements 每帧组织 Wire/Fill 几何（路径箭头、投影点球、查询框、
//    Raycast 命中、Open/Closed 集合三角形、节点连线等）；
//  - GatherPathStep 从 FRecastDebugPathfindingData 还原单步 A* 搜索；
//  - DelegateHelper 把节点文字画到 Canvas。
//
// 关键线程约束：
//  - 构造函数在游戏线程执行，之后 Proxy 不再访问 UObject（@todo 注释里也明确了）
//  - DebugSteps 仅在 WITH_EDITORONLY_DATA && WITH_RECAST 下可用
// =====================================================================================

#include "NavMesh/NavTestRenderingComponent.h"
#include "NavigationTestingActor.h"
#include "NavMesh/RecastNavMesh.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/DebugDrawService.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawingUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavTestRenderingComponent)

// Open Set 节点的填充色（暖黄）
static constexpr FColor NavMeshRenderColor_OpenSet(255,128,0,255);
// Closed Set 节点的填充色（金色）
static constexpr FColor NavMeshRenderColor_ClosedSet(255,196,0,255);
// diff 模式下"本步被修改过"的节点透明度（不透明）
static constexpr uint8 NavMeshRenderAlpha_Modified = 255;
// diff 模式下"未被修改"的节点透明度（淡）
static constexpr uint8 NavMeshRenderAlpha_NonModified = 64;

// UE SceneProxy 类型识别：用同一个静态地址作为哈希源
SIZE_T FNavTestSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

// 构造函数（游戏线程）：从 NavTestActor 一次性拷贝需要展示的数据
// 目的：在进入渲染线程之前完成 UObject 访问，之后只读 Proxy 自身字段
FNavTestSceneProxy::FNavTestSceneProxy(const UNavTestRenderingComponent* InComponent)
	: FDebugRenderSceneProxy(InComponent)
	, NavMeshDrawOffset(0,0,10)
	, NavTestActor(nullptr)
{
	// ShowFlag 名，与 UEngine 的 Navigation 可见性旗标联动
	ViewFlagName = TEXT("Navigation");

	if (InComponent == nullptr)
	{
		return;
	}

	NavTestActor = Cast<ANavigationTestingActor>(InComponent->GetOwner());
	if (NavTestActor == nullptr)
	{
		return;
	}

	// 根据 Agent 半径微调绘制偏移，避免粗 Agent 的圆柱体盖住路径
	NavMeshDrawOffset.Z += NavTestActor->NavAgentProps.AgentRadius / 10.f;
	bShowNodePool = NavTestActor->bShowNodePool;
	bShowBestPath = NavTestActor->bShowBestPath;
	bShowDiff = NavTestActor->bShowDiffWithPreviousStep;

	// 只在用户勾选 DrawDistanceToWall 时保留最近墙点
	ClosestWallLocation = NavTestActor->bDrawDistanceToWall ? NavTestActor->ClosestWallLocation : FNavigationSystem::InvalidLocation;

	// 路径 + A* 单步调试数据拷贝
	GatherPathPoints();
	GatherPathStep();
}

// 渲染线程入口：为每个可见视图组织调试几何（路径/查询/Debug 节点）
// 调用者：UE RenderThread 通过 FPrimitiveSceneProxy::GetDynamicMeshElements 下发
void FNavTestSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	FRHICommandList& RHICmdList = Collector.GetRHICommandList();

	// 逐视图（主视图 + 阴影/镜像等）绘制
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

			if (NavTestActor)
			{
				//DrawArc(PDI, Link.Left, Link.Right, 0.4f, NavMeshColors[Link.AreaID], SDPG_World, 3.5f);
				//const FVector VOffset(0,0,FVector::Dist(Link.Left, Link.Right)*1.333f);
				//DrawArrowHead(PDI, Link.Right, Link.Left+VOffset, 30.f, NavMeshColors[Link.AreaID], SDPG_World, 3.5f);

				//@todo - the rendering thread should never read from UObjects directly!  These are race conditions, the properties should be mirrored on the proxy
				// 注：下方从 NavTestActor 读取字段严格说属于竞态（官方也有 @todo）
				// 实际中由于 Testing Actor 调试用途，可接受
				const FVector ActorLocation = NavTestActor->GetActorLocation();
				// 投影点：导航系统 ProjectPoint 的结果，失败时给一个向下偏移的默认位置
				const FVector ProjectedLocation = NavTestActor->bProjectedLocationValid ? (NavTestActor->ProjectedLocation + (FVector)NavMeshDrawOffset) : (ActorLocation - FVector(0, 0, NavTestActor->QueryingExtent.Z));
				const FColor ProjectedColor = (NavTestActor->bProjectedLocationValid ? FColor::Green : FColor::Red).WithAlpha(120);
				const FColor ClosestWallColor = FColorList::Orange;
				const FVector BoxExtent(20, 20, 20);

				// 每帧分配一次的材质代理（OneFrameResource 自动在帧末释放）
				const FMaterialRenderProxy* const ColoredMeshInstance = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(GEngine->DebugMeshMaterial->GetRenderProxy(), ProjectedColor);
				//DrawBox(PDI, FTransform(ProjectedLocation).ToMatrixNoScale(),BoxExtent, ColoredMeshInstance, SDPG_World);
				// 投影成功：在导航面上画一个球体
				if (NavTestActor->bProjectedLocationValid)
				{
					GetSphereMesh(ProjectedLocation, BoxExtent, 10, 7, ColoredMeshInstance, SDPG_World, false, ViewIndex, Collector);
				}

				//DrawWireBox(PDI, FBox(ProjectedLocation-BoxExtent, ProjectedLocation+BoxExtent), ProjectedColor, false);
				// Actor 自身位置的白色线框立方体（代表 Agent 初始位置）
				DrawWireBox(PDI, FBox(ActorLocation - BoxExtent, ActorLocation + BoxExtent), FColor::White, false);
				// Actor → 投影位置的箭头
				const FVector LineEnd = NavTestActor->bProjectedLocationValid ? ProjectedLocation - (ProjectedLocation - ActorLocation).GetSafeNormal()*BoxExtent.X : ProjectedLocation;
				PDI->DrawLine(LineEnd, ActorLocation, ProjectedColor, SDPG_World, 2.5);
				DrawArrowHead(PDI, LineEnd, ActorLocation, 20.f, ProjectedColor, SDPG_World, 2.5f);

				// draw query extent
				// 蓝色框：ProjectPoint 允许的半范围（Agent 周围的 QueryingExtent）
				DrawWireBox(PDI, FBox(ActorLocation - NavTestActor->QueryingExtent, ActorLocation + NavTestActor->QueryingExtent), FColor::Blue, false);

				// 绘制到最近墙的连线（FindDistanceToWall 结果）
				if (FNavigationSystem::IsValidLocation(ClosestWallLocation))
				{
					PDI->DrawLine(ClosestWallLocation, ActorLocation, ClosestWallColor, SDPG_World, 2.5);
				}

				// 绘制半径圆柱，颜色反映 NavData 是否在半径内都已准备好
				if (NavTestActor->bDrawIfNavDataIsReadyInRadius)
				{
					constexpr double HalfHeight = 1000;
					constexpr int32 NumSides = 32;
					DrawWireCylinder(PDI, ActorLocation, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1),
						NavTestActor->bNavDataIsReadyInRadius ? FColor::Green : FColor::Red, 
						NavTestActor->RadiusUsedToValidateNavData, HalfHeight,  
						NumSides, SDPG_World);
				}

				// 绘制一个从 Actor 到 TargetActor 的胶囊，颜色反映沿线 NavData 是否就绪
				if (NavTestActor->bDrawIfNavDataIsReadyToQueryTargetActor && NavTestActor->QueryTargetActor)
				{
					const FVector QueryTargetLocation = NavTestActor->QueryTargetActor->GetActorLocation();
					float Distance;
					FVector Forward, Right, Up;
					(QueryTargetLocation-ActorLocation).ToDirectionAndLength(Up, Distance);
					Up.FindBestAxisVectors(Forward, Right);

					constexpr int32 NumSides = 32;
					DrawWireCapsule(PDI, ActorLocation + 0.5f * Distance * Up, Forward, Right, Up,
						NavTestActor->bNavDataIsReadyToQueryTargetActor ? FColor::Green : FColor::Red, 
						NavTestActor->RadiusUsedToValidateNavData, 0.5f*Distance + NavTestActor->RadiusUsedToValidateNavData,  
						NumSides, SDPG_World);
				}

				// 绘制 Raycast 到 TargetActor 的结果：红=命中、绿=整条走廊通、橙=部分通
				if (NavTestActor->bDrawRaycastToQueryTargetActor && NavTestActor->QueryTargetActor)
				{
					const FVector QueryTargetLocation = NavTestActor->QueryTargetActor->GetActorLocation();
					PDI->DrawLine(ActorLocation, QueryTargetLocation, NavTestActor->bRaycastToQueryTargetActorResult ? FColor::Red : NavTestActor->bRaycastToQueryTargetEndsInCorridor ? FColor::Green : FColor::Orange, SDPG_World, 2.5);

					// 命中点用红色小球标记
					if (FNavigationSystem::IsValidLocation(NavTestActor->RaycastHitLocation))
					{
						DrawWireSphere(PDI, NavTestActor->RaycastHitLocation, FColor::Red, 25.f, 6, SDPG_World);
					}
				}
			}

			// draw path
			// 绘制最终路径折线：只在 !bShowBestPath 或无调试节点时画
			// （当有 A* 单步调试且 bShowBestPath=true 时，路径由 NodeDebug 的节点连线表现）
			if (!bShowBestPath || !NodeDebug.Num())
			{
				for (int32 PointIndex = 1; PointIndex < PathPoints.Num(); PointIndex++)
				{
					PDI->DrawLine(PathPoints[PointIndex-1], PathPoints[PointIndex], FLinearColor::Red, SDPG_World, 2.0f, 0.0f);
					DrawArrowHead(PDI, PathPoints[PointIndex], PathPoints[PointIndex - 1], 25.f, FLinearColor::Red, SDPG_World, 2.0f);
				}
			}

			// draw path debug data
			// 绘制 A* 单步调试：Open/Closed 集合的三角形填充
			if (bShowNodePool)
			{
				if (ClosedSetIndices.Num())
				{
					const FColoredMaterialRenderProxy *MeshColorInstance = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(GEngine->DebugMeshMaterial->GetRenderProxy(), NavMeshRenderColor_ClosedSet);						
					FDynamicMeshBuilder	MeshBuilder(View->GetFeatureLevel());
					MeshBuilder.AddVertices(ClosedSetVerts);
					MeshBuilder.AddTriangles(ClosedSetIndices);
					MeshBuilder.GetMesh(FMatrix::Identity, MeshColorInstance, IntCastChecked<uint8>((int32)GetDepthPriorityGroup(View)), false, false, ViewIndex, Collector);
				}

				if (OpenSetIndices.Num())
				{
					const FColoredMaterialRenderProxy *MeshColorInstance = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(GEngine->DebugMeshMaterial->GetRenderProxy(), NavMeshRenderColor_OpenSet);						
					FDynamicMeshBuilder	MeshBuilder(View->GetFeatureLevel());
					MeshBuilder.AddVertices(OpenSetVerts);
					MeshBuilder.AddTriangles(OpenSetIndices);
					MeshBuilder.GetMesh(FMatrix::Identity, MeshColorInstance, IntCastChecked<uint8>((int32)GetDepthPriorityGroup(View)), false, false, ViewIndex, Collector);
				}
			}

			// 遍历每个调试节点，画"节点 → 父节点"的连线（组成搜索树）
			for (TSet<FNodeDebugData>::TConstIterator It(NodeDebug); It; ++It)
			{
				const FNodeDebugData& NodeData = *It;

				FColor LineColor(FColor::Blue);
				// 最优路径高亮为红
				if (bShowBestPath && NodeData.bBestPath)
				{
					LineColor = FColor::Red;
				}

				// diff 模式下：本步修改的线不透明，未修改变淡
				if (bShowDiff)
				{
					LineColor.A = NodeData.bModified ? NavMeshRenderAlpha_Modified : NavMeshRenderAlpha_NonModified;
				}

				// 获取父节点坐标（若无父=根节点则连向自己）
				FVector ParentPos(NodeData.ParentId.IsValidId() ? NodeDebug[NodeData.ParentId].Position : NodeData.Position);

				// 未修改节点用细线，修改节点用带厚度 + 深度测试的线
				if (bShowDiff && !NodeData.bModified)
				{
					PDI->DrawLine(NodeData.Position, ParentPos, LineColor, SDPG_World);
				}
				else
				{
					PDI->DrawLine(NodeData.Position, ParentPos, LineColor, SDPG_World, 2.0f, 0.0, true);
				}

				// 通过 OffMeshLink 到达的节点：绘制一个立方体标记
				if (NodeData.bOffMeshLink)
				{
					DrawWireBox(PDI, FBox::BuildAABB(NodeData.Position, FVector(10.0f)), LineColor, SDPG_World);
				}

				// diff 模式：被修改节点额外画一根绿色竖线更显眼
				if (bShowDiff && NodeData.bModified)
				{
					PDI->DrawLine(NodeData.Position + FVector(0,0,10), NodeData.Position + FVector(0,0,100), FColor::Green, SDPG_World);
				}
			}
		}
	}
}

// 把 NavTestActor->LastPath 里的路径点拷贝到 PathPoints
// 每个点附带 "Index-AreaFlags" 文字显示
void FNavTestSceneProxy::GatherPathPoints()
{
	if (NavTestActor && NavTestActor->LastPath.IsValid())
	{
		for (int32 PointIndex = 0; PointIndex < NavTestActor->LastPath->GetPathPoints().Num(); PointIndex++)
		{
			PathPoints.Add(NavTestActor->LastPath->GetPathPoints()[PointIndex].Location);
			PathPointFlags.Add(FString::Printf(TEXT("%d-%d"), PointIndex, FNavMeshNodeFlags(NavTestActor->LastPath->GetPathPoints()[PointIndex].Flags).AreaFlags));
		}
	}
}

// 读取 A* 单步调试数据（FRecastDebugPathfindingData）并组装成可视化几何
// 数据流：PImplRecastNavMesh::FindPath → 记录每步到 DebugSteps → 这里取第 ShowStepIndex 步
// 注意：仅在 WITH_EDITORONLY_DATA && WITH_RECAST 下可用
void FNavTestSceneProxy::GatherPathStep()
{
	OpenSetVerts.Reset();
	ClosedSetVerts.Reset();
	OpenSetIndices.Reset();
	ClosedSetIndices.Reset();
	NodeDebug.Empty(NodeDebug.Num());
	BestNodeId = FSetElementId();

#if WITH_EDITORONLY_DATA && WITH_RECAST
	// DebugSteps are only available for: WITH_EDITORONLY_DATA && WITH_RECAST
	if (NavTestActor && NavTestActor->DebugSteps.Num() && NavTestActor->ShowStepIndex >= 0)
	{
		// 限制步号范围
		const int32 ShowIdx = FMath::Min(NavTestActor->ShowStepIndex, NavTestActor->DebugSteps.Num() - 1);
		const FRecastDebugPathfindingData& DebugStep = NavTestActor->DebugSteps[ShowIdx];
		int32 BaseOpen = 0;     // 下一个 Open Set 节点在 OpenSetVerts 中的基址
		int32 BaseClosed = 0;   // 下一个 Closed Set 节点的基址

		// 遍历本步所有节点：把多边形 FanTriangulate 成三角形
		for (TSet<FRecastDebugPathfindingNode>::TConstIterator It(DebugStep.Nodes); It; ++It)
		{
			const FRecastDebugPathfindingNode& DebugNode = *It;
			if (DebugNode.bOpenSet)
			{
				// Open Set：把多边形顶点追加到 OpenSetVerts，扇形三角化（0, i-1, i）
				for (int32 iv = 0; iv < DebugNode.Verts.Num(); iv++)
				{
					OpenSetVerts.Add(DebugNode.Verts[iv] + NavMeshDrawOffset);
				}

				for (int32 iv = 2; iv < DebugNode.Verts.Num(); iv++)
				{
					OpenSetIndices.Add(BaseOpen + 0);
					OpenSetIndices.Add(BaseOpen + iv - 1);
					OpenSetIndices.Add(BaseOpen + iv);
				}

				BaseOpen += DebugNode.Verts.Num();
			}
			else
			{
				// Closed Set：同样扇形三角化，但进入另一组 VB/IB
				for (int32 iv = 0; iv < DebugNode.Verts.Num(); iv++)
				{
					ClosedSetVerts.Add(DebugNode.Verts[iv] + NavMeshDrawOffset);
				}

				for (int32 iv = 2; iv < DebugNode.Verts.Num(); iv++)
				{
					ClosedSetIndices.Add(BaseClosed + 0);
					ClosedSetIndices.Add(BaseClosed + iv - 1);
					ClosedSetIndices.Add(BaseClosed + iv);
				}

				BaseClosed += DebugNode.Verts.Num();
			}

			// 组装用于文本和连线的节点记录
			FNodeDebugData NewNodeData;

			// 根据 CostDisplayMode 选显示哪个代价值
			FVector::FReal DisplayedCost = TNumericLimits<FVector::FReal>::Max();
			switch (NavTestActor->CostDisplayMode)
			{
			case ENavCostDisplay::TotalCost:
				DisplayedCost = DebugNode.TotalCost;
				break;
			case ENavCostDisplay::RealCostOnly:
				DisplayedCost = DebugNode.Cost;
				break;
			case ENavCostDisplay::HeuristicOnly:
				DisplayedCost = DebugNode.GetHeuristicCost();
				break;
			default:
				break;
			}

			NewNodeData.Desc = FString::Printf(TEXT("%.2f%s"), DisplayedCost, DebugNode.bOffMeshLink ? TEXT(" [link]") : TEXT(""));

			NewNodeData.Position = DebugNode.NodePos;
			NewNodeData.PolyRef = DebugNode.PolyRef;
			NewNodeData.bClosedSet = !DebugNode.bOpenSet;
			// BestNode: 本步的"最小 f 值"节点，后续回溯形成最优路径
			NewNodeData.bBestPath = (It.GetId() == DebugStep.BestNode);
			NewNodeData.bModified = DebugNode.bModified;
			NewNodeData.bOffMeshLink = DebugNode.bOffMeshLink;

			const FSetElementId NewId = NodeDebug.Add(NewNodeData);
			if (NewNodeData.bBestPath)
			{
				BestNodeId = NewId;
			}
		}

		// 第 2 遍：回填 ParentId。利用 PolyRef 作为 TSet key 定位父节点
		FRecastDebugPathfindingNode ThisNode;
		FNodeDebugData ParentDebugNode;

		for (TSet<FNodeDebugData>::TIterator It(NodeDebug); It; ++It)
		{
			FNodeDebugData& MyDebugNode = *It;
				
			ThisNode.PolyRef = MyDebugNode.PolyRef;
			const FRecastDebugPathfindingNode* MyNode = DebugStep.Nodes.Find(ThisNode);

			if (MyNode)
			{
				ParentDebugNode.PolyRef = MyNode->ParentRef;
				MyDebugNode.ParentId = NodeDebug.FindId(ParentDebugNode);
			}
		}

		// 第 3 遍：沿 BestNode 向上回溯，把整条最优路径上的节点 bBestPath=true
		FSetElementId BestPathId = BestNodeId;
		while (BestPathId.IsValidId())
		{
			FNodeDebugData& MyDebugNode = NodeDebug[BestPathId];

			MyDebugNode.bBestPath = true;
			BestPathId = MyDebugNode.ParentId;
		}
	}
#endif // WITH_EDITORONLY_DATA && WITH_RECAST
}

// 告诉渲染系统：本 Proxy 参与动态通道 + 半透明通道（编辑器下）
FPrimitiveViewRelevance FNavTestSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bDynamicRelevance = true;
	// ideally the TranslucencyRelevance should be filled out by the material, here we do it conservative
	Result.bSeparateTranslucency = Result.bNormalTranslucency = IsShown(View) && GIsEditor;
	return Result;
}

// 统计数组/字符串分配量，用于 MemReport 命令
uint32 FNavTestSceneProxy::GetAllocatedSizeInternal() const
{
	SIZE_T InternalAllocSize = 0;
	for (TSet<FNodeDebugData>::TConstIterator It(NodeDebug); It; ++It)
	{
		InternalAllocSize += (*It).Desc.GetAllocatedSize();
	}

	return IntCastChecked<uint32>(FDebugRenderSceneProxy::GetAllocatedSize() + PathPoints.GetAllocatedSize()
		+ PathPointFlags.GetAllocatedSize()
		+ OpenSetVerts.GetAllocatedSize() + OpenSetIndices.GetAllocatedSize()
		+ ClosedSetVerts.GetAllocatedSize() + ClosedSetIndices.GetAllocatedSize()
		+ NodeDebug.GetAllocatedSize() + InternalAllocSize);

}

// DebugDraw 委托辅助：把 Proxy 中绘制文字所需的数据拷贝出来（Proxy 生命周期内）
// 原因：DrawDebugLabels 会在每帧由 DebugDrawService 回调，比 Proxy 存活周期长
void FNavTestDebugDrawDelegateHelper::SetupFromProxy(const FNavTestSceneProxy* InSceneProxy)
{
	PathPoints.Reset();
	PathPoints.Append(InSceneProxy->PathPoints);
	PathPointFlags.Reset();
	PathPointFlags.Append(InSceneProxy->PathPointFlags);
	NodeDebug.Reset();
	NodeDebug.Append(InSceneProxy->NodeDebug);
	NavTestActor = InSceneProxy->NavTestActor;
	BestNodeId = InSceneProxy->BestNodeId;
	bShowBestPath = InSceneProxy->bShowBestPath;
	bShowDiff = InSceneProxy->bShowDiff;
}

// 把节点描述/路径点文本绘制到 Canvas（HUD）
// 如果有调试节点则优先显示节点 Desc；否则显示路径点编号+AreaFlag
void FNavTestDebugDrawDelegateHelper::DrawDebugLabels(UCanvas* Canvas, APlayerController*)
{
	if (NavTestActor == nullptr)
	{
		return;
	}

	const FColor OldDrawColor = Canvas->DrawColor;
	Canvas->SetDrawColor(FColor::White);
	const FSceneView* View = Canvas->SceneView;

	if (NodeDebug.Num())
	{
		const UFont* RenderFont = GEngine->GetSmallFont();
		for (TSet<FNavTestSceneProxy::FNodeDebugData>::TConstIterator It(NodeDebug); It; ++It)
		{
			const FNavTestSceneProxy::FNodeDebugData& NodeData = *It;

			// 只画视锥内节点，避免 Canvas Project 到屏幕外的点
			if (FNavTestSceneProxy::LocationInView(NodeData.Position, View))
			{
				// Closed=深灰、Open=白；若 !bShowBestPath 则把 BestNode 染红
				FColor MyColor = NodeData.bClosedSet ? FColor(64, 64, 64) : FColor::White;
				if (!bShowBestPath && It.GetId() == BestNodeId)
				{
					MyColor = FColor::Red;
				}
				if (bShowDiff)
				{
					MyColor.A = NodeData.bModified ? NavMeshRenderAlpha_Modified : NavMeshRenderAlpha_NonModified;
				}

				Canvas->SetDrawColor(MyColor);

				// 投影到屏幕 + NavTestActor 上设置的文字偏移
				const FVector3f ScreenLoc(Canvas->Project(NodeData.Position) + FVector(NavTestActor->TextCanvasOffset, 0.f));
				Canvas->DrawText(RenderFont, NodeData.Desc, ScreenLoc.X, ScreenLoc.Y);
			}
		}
	}
	else
	{
		// 路径点上贴 "index-areaFlag"
		for (int32 PointIndex = 0; PointIndex < PathPoints.Num(); ++PointIndex)
		{
			if (FNavTestSceneProxy::LocationInView(PathPoints[PointIndex], View))
			{
				const FVector3f PathPointLoc(Canvas->Project(PathPoints[PointIndex]));
				const UFont* RenderFont = GEngine->GetSmallFont();
				Canvas->DrawText(RenderFont, PathPointFlags[PointIndex], PathPointLoc.X, PathPointLoc.Y);

			}
		}
	}

	Canvas->SetDrawColor(OldDrawColor);
}
#endif // UE_ENABLE_DEBUG_DRAWING

#if UE_ENABLE_DEBUG_DRAWING
// 构造 SceneProxy 并同步文字绘制辅助
// 由 UDebugDrawComponent 在 MarkRenderStateDirty 时调用（游戏线程）
FDebugRenderSceneProxy* UNavTestRenderingComponent::CreateDebugSceneProxy()
{
	FNavTestSceneProxy* NewSceneProxy = new FNavTestSceneProxy(this);
	NavTestDebugDrawDelegateHelper.SetupFromProxy(NewSceneProxy);
	return NewSceneProxy;
}
#endif

// 计算组件包围盒：包含 Actor 自身所有子组件 + 路径点 + 所有调试节点顶点
// 目的：保证视锥裁剪不会误伤调试绘制
FBoxSphereBounds UNavTestRenderingComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BoundingBox(ForceInit);

	ANavigationTestingActor* TestActor = Cast<ANavigationTestingActor>(GetOwner());
	if (TestActor)
	{
		BoundingBox = TestActor->GetComponentsBoundingBox(true);
	
		// 合并路径点
		if (TestActor->LastPath.IsValid())
		{
			for (int32 PointIndex = 0; PointIndex < TestActor->LastPath->GetPathPoints().Num(); PointIndex++)
			{
				BoundingBox += TestActor->LastPath->GetPathPoints()[PointIndex].Location;
			}
		}
#if WITH_EDITORONLY_DATA && WITH_RECAST
		// DebugSteps are only available for: WITH_EDITORONLY_DATA && WITH_RECAST
		// 合并当前步 A* 所有节点的多边形顶点
		if (TestActor->DebugSteps.Num() && TestActor->ShowStepIndex >= 0)
		{
			const int32 ShowIdx = FMath::Min(TestActor->ShowStepIndex, TestActor->DebugSteps.Num() - 1);
			const FRecastDebugPathfindingData& DebugStep = TestActor->DebugSteps[ShowIdx];
			for (TSet<FRecastDebugPathfindingNode>::TConstIterator It(DebugStep.Nodes); It; ++It)
			{
				const FRecastDebugPathfindingNode& DebugNode = *It;
				for (int32 iv = 0; iv < DebugNode.Verts.Num(); iv++)
				{
					BoundingBox += (FVector)DebugNode.Verts[iv];
				}
			}
		}
#endif // WITH_EDITORONLY_DATA && WITH_RECAST
	}

	return FBoxSphereBounds(BoundingBox);
}

