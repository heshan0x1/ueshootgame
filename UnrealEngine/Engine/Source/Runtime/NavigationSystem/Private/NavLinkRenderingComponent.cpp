// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkRenderingComponent.cpp
// -----------------------------------------------------------------------------
// NavLink 的编辑器可视化实现。
// 职责划分：
//   - UNavLinkRenderingComponent：PrimitiveComponent 外壳。只负责创建 SceneProxy 与
//     计算 Bounds。不参与导航生成，不存链接数据。
//   - FNavLinkRenderingProxy：真正的调试绘制代理。构造时从 Host 接口拉链接数组并
//     烘成 FNavLinkDrawing/FNavLinkSegmentDrawing 世界坐标快照。
//     GetDynamicMeshElements 里读 NavSys 里所有 ARecastNavMesh 的 AgentMask + 
//     MaxStepHeight，交给 GetLinkMeshes 画弧线 + 箭头 + 端点圆柱（snap 半径/高度）。
//
// DrawLinks vs GetLinkMeshes：
//   - GetLinkMeshes 通过 FMeshElementCollector（更贴合引擎渲染管线，Mesh 可接受光照）
//   - DrawLinks 直接向 FPrimitiveDrawInterface 发 debug 线，供没有 SceneProxy 的场景
//     （例如 NavMeshRenderingComponent 里也要绘 link 时）复用。
// =============================================================================

#include "NavLinkRenderingComponent.h"
#include "EngineGlobals.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshElementCollector.h"
#include "Engine/CollisionProfile.h"
#include "PrimitiveDrawingUtils.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "NavLinkRenderingProxy.h"
#include "NavLinkHostInterface.h"
#include "NavMesh/RecastNavMesh.h"
#include "SceneView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavLinkRenderingComponent)

//----------------------------------------------------------------------//
// UNavLinkRenderingComponent
//----------------------------------------------------------------------//
// 构造：Stationary + 无碰撞 + Editor Only。
// Mobility=Stationary 让渲染在移动时依然能重建 Proxy，但静止时走优化路径。
UNavLinkRenderingComponent::UNavLinkRenderingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// properties

	// Allows updating in game, while optimizing rendering for the case that it is not modified
	Mobility = EComponentMobility::Stationary;

	BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	bIsEditorOnly = true;

	SetGenerateOverlapEvents(false);
}

// Bounds 计算：向 Owner 的 INavLinkHostInterface 要出链接列表并取端点 AABB。
// 若 Host 通过 NavLinkClasses 暴露（UNavLinkDefinition 子类），则遍历所有类的端点。
// 最终把 Actor 局部空间 Bounds 变换到 Component 局部空间再乘 InLocalToWorld。
FBoxSphereBounds UNavLinkRenderingComponent::CalcBounds(const FTransform& InLocalToWorld) const
{
	AActor* LinkOwnerActor = GetOwner();
	if (LinkOwnerActor != NULL)
	{
		FBox BoundingBox(ForceInit);

		INavLinkHostInterface* LinkOwnerHost = Cast<INavLinkHostInterface>(LinkOwnerActor);
		if (LinkOwnerHost != NULL)
		{
			TArray<TSubclassOf<UNavLinkDefinition> > NavLinkClasses;
			TArray<FNavigationLink> SimpleLinks;
			TArray<FNavigationSegmentLink> DummySegmentLinks;

			if (LinkOwnerHost->GetNavigationLinksClasses(NavLinkClasses))
			{
				for (int32 NavLinkClassIdx = 0; NavLinkClassIdx < NavLinkClasses.Num(); ++NavLinkClassIdx)
				{
					if (NavLinkClasses[NavLinkClassIdx] != NULL)
					{
						const TArray<FNavigationLink>& Links = UNavLinkDefinition::GetLinksDefinition(NavLinkClasses[NavLinkClassIdx]);
						for (const auto& Link : Links)
						{
							BoundingBox += Link.Left;
							BoundingBox += Link.Right;
						}
					}
				}
			}
			if (LinkOwnerHost->GetNavigationLinksArray(SimpleLinks, DummySegmentLinks))
			{
				for (const auto& Link : SimpleLinks)
				{
					BoundingBox += Link.Left;
					BoundingBox += Link.Right;
				}
			}
		}

		// BoundingBox is in actor space. Incorporate provided InLocalToWorld transform via component space.
		const FTransform ActorToWorld = LinkOwnerActor->ActorToWorld();
		const FTransform WorldToComponent = GetComponentTransform().Inverse();
		return FBoxSphereBounds(BoundingBox).TransformBy(ActorToWorld * WorldToComponent * InLocalToWorld);
	}

	return FBoxSphereBounds(ForceInitToZero);
}

FPrimitiveSceneProxy* UNavLinkRenderingComponent::CreateSceneProxy()
{
	return new FNavLinkRenderingProxy(this);
}

#if WITH_EDITOR
// 本组件在编辑器里不可被框选（绘制纯调试用途，不该干扰选择）
bool UNavLinkRenderingComponent::ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const
{
	// NavLink rendering components not treated as 'selectable' in editor
	return false;
}

bool UNavLinkRenderingComponent::ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const
{
	// NavLink rendering components not treated as 'selectable' in editor
	return false;
}
#endif

//----------------------------------------------------------------------//
// FNavLinkRenderingProxy
//----------------------------------------------------------------------//
// 构造：向 Owner（或挂它的 Component）查询 INavLinkHostInterface，拉出链接数据转成绘制快照。
// 注意：两种数据源都处理——
//   1) GetNavigationLinksClasses + UNavLinkDefinition::GetLinksDefinition 的"模板定义"链接
//   2) GetNavigationLinksArray 的"实例数据"链接（UNavLinkComponent 用的）
FNavLinkRenderingProxy::FNavLinkRenderingProxy(const UPrimitiveComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	LinkOwnerActor = InComponent->GetOwner();
	LinkOwnerHost = Cast<INavLinkHostInterface>((UPrimitiveComponent*)InComponent);

	if (LinkOwnerHost == nullptr)
	{
		LinkOwnerHost = Cast<INavLinkHostInterface>(LinkOwnerActor);
	}

	if (LinkOwnerActor != NULL && LinkOwnerHost != NULL)
	{
		const FTransform LinkOwnerLocalToWorld = LinkOwnerActor->ActorToWorld();
		TArray<TSubclassOf<UNavLinkDefinition> > NavLinkClasses;
		LinkOwnerHost->GetNavigationLinksClasses(NavLinkClasses);

		for (int32 NavLinkClassIdx = 0; NavLinkClassIdx < NavLinkClasses.Num(); ++NavLinkClassIdx)
		{
			if (NavLinkClasses[NavLinkClassIdx] != NULL)
			{
				StorePointLinks(LinkOwnerLocalToWorld, UNavLinkDefinition::GetLinksDefinition(NavLinkClasses[NavLinkClassIdx]));
				StoreSegmentLinks(LinkOwnerLocalToWorld, UNavLinkDefinition::GetSegmentLinksDefinition(NavLinkClasses[NavLinkClassIdx]));
			}
		}

		TArray<FNavigationLink> PointLinks;
		TArray<FNavigationSegmentLink> SegmentLinks;
		if (LinkOwnerHost->GetNavigationLinksArray(PointLinks, SegmentLinks))
		{
			StorePointLinks(LinkOwnerLocalToWorld, PointLinks);
			StoreSegmentLinks(LinkOwnerLocalToWorld, SegmentLinks);
		}
	}
}

SIZE_T FNavLinkRenderingProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

// 把 FNavigationLink 数组变成 FNavLinkDrawing（世界坐标 + 颜色 + snap 参数）
void FNavLinkRenderingProxy::StorePointLinks(const FTransform& InLocalToWorld, const TArray<FNavigationLink>& LinksArray)
{
	OffMeshPointLinks.Reserve(OffMeshPointLinks.Num() + LinksArray.Num());
	for (const FNavigationLink& Link : LinksArray)
	{
		OffMeshPointLinks.Emplace(InLocalToWorld, Link);
	}
}

// 段链接版本，处理对象是 FNavigationSegmentLink
void FNavLinkRenderingProxy::StoreSegmentLinks(const FTransform& InLocalToWorld, const TArray<FNavigationSegmentLink>& LinksArray)
{
	OffMeshSegmentLinks.Reserve(OffMeshSegmentLinks.Num() + LinksArray.Num());
	for (const FNavigationSegmentLink& Link : LinksArray)
	{
		OffMeshSegmentLinks.Emplace(InLocalToWorld, Link);
	}
}

// 渲染入口：
//   1) 遍历 NavSys->NavDataSet 收集"每个 RecastNavMesh 的 Agent 位 + MaxStepHeight"。
//      - AgentMask：仅绘制开了调试显示的 NavMesh 所对应的 Agent 位。
//      - StepHeights：端点圆柱的高度取值（反映该 Agent 的跨步高度）。
//   2) 每个可见 View 调 GetLinkMeshes，把弧线 + 箭头 + 圆柱 塞进 Collector。
void FNavLinkRenderingProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const 
{
	if (LinkOwnerActor && LinkOwnerActor->GetWorld())
	{
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<const UNavigationSystemV1>(LinkOwnerActor->GetWorld());
		TArray<float> StepHeights;
		uint32 AgentMask = 0;
		if (NavSys != NULL)
		{
			StepHeights.Reserve(NavSys->NavDataSet.Num());
			for(int32 DataIndex = 0; DataIndex < NavSys->NavDataSet.Num(); ++DataIndex)
			{
				const ARecastNavMesh* NavMesh = Cast<const ARecastNavMesh>(NavSys->NavDataSet[DataIndex]);

				if (NavMesh != NULL)
				{
					AgentMask = NavMesh->IsDrawingEnabled() ? AgentMask | (1 << DataIndex) : AgentMask;
#if WITH_RECAST
					const float AgentMaxStepHeight = NavMesh->GetAgentMaxStepHeight(ENavigationDataResolution::Default);
					if (AgentMaxStepHeight > 0 && NavMesh->IsDrawingEnabled())
					{
						StepHeights.Add(AgentMaxStepHeight);
					}
#endif // WITH_RECAST
				}
			}
		}

		static const FColor RadiusColor(150, 160, 150, 48);
		FMaterialRenderProxy* const MeshColorInstance = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(GEngine->DebugMeshMaterial->GetRenderProxy(), RadiusColor);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				FNavLinkRenderingProxy::GetLinkMeshes(OffMeshPointLinks, OffMeshSegmentLinks, StepHeights, MeshColorInstance, ViewIndex, Collector, AgentMask);
			}
		}
	}
}

// 真正"画 Mesh 版本"的绘制函数——Collector 形式可被渲染线程复用。
// 每条点链接：弧线 + 方向箭头 + 两端圆柱（snap 范围）。
// 每条段链接：两条起/止弧 + 箭头 + 四端圆柱。
// AgentMask 过滤：若链接声明了 SupportedAgents 与当前开了绘制的 NavMesh 无交集则跳过。
void FNavLinkRenderingProxy::GetLinkMeshes(const TArray<FNavLinkDrawing>& OffMeshPointLinks, const TArray<FNavLinkSegmentDrawing>& OffMeshSegmentLinks, TArray<float>& StepHeights, FMaterialRenderProxy* const MeshColorInstance, int32 ViewIndex, FMeshElementCollector& Collector, uint32 AgentMask)
{
	static const FColor LinkColor(0,0,166);
	static const float LinkArcThickness = 3.f;
	static const float LinkArcHeight = 0.4f;

	if (StepHeights.Num() == 0)
	{
		StepHeights.Add(FNavigationSystem::FallbackAgentHeight / 2);
	}

	FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

	for (int32 LinkIndex = 0; LinkIndex < OffMeshPointLinks.Num(); ++LinkIndex)
	{
		const FNavLinkDrawing& Link = OffMeshPointLinks[LinkIndex];
		if ((Link.SupportedAgentsBits & AgentMask) == 0)
		{
			continue;
		}
		const FVector::FReal RealSegments = FMath::Max(LinkArcHeight * (Link.Right - Link.Left).Size() / 10., 8.);
		check(RealSegments >= 0 && RealSegments <= (FVector::FReal)TNumericLimits<uint32>::Max());
		const uint32 Segments = static_cast<uint32>(RealSegments);
		DrawArc(PDI, Link.Left, Link.Right, LinkArcHeight, Segments, Link.Color, SDPG_World, 3.5f);
		const FVector VOffset(0,0,FVector::Dist(Link.Left, Link.Right)*1.333f);

		switch (Link.Direction)
		{
		case ENavLinkDirection::LeftToRight:
			DrawArrowHead(PDI, Link.Right, Link.Left+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::RightToLeft:
			DrawArrowHead(PDI, Link.Left, Link.Right+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::BothWays:
		default:
			DrawArrowHead(PDI, Link.Right, Link.Left+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.Left, Link.Right+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		}

		// draw snap-spheres on both ends
		if (Link.SnapHeight < 0)
		{
			for (int32 StepHeightIndex = 0; StepHeightIndex < StepHeights.Num(); ++StepHeightIndex)
			{
				GetCylinderMesh(Link.Right, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
				GetCylinderMesh(Link.Left, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
			}
		}
		else
		{
			GetCylinderMesh(Link.Right, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
			GetCylinderMesh(Link.Left, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
		}
	}

	static const float SegmentArcHeight = 0.25f;
	for (int32 LinkIndex = 0; LinkIndex < OffMeshSegmentLinks.Num(); ++LinkIndex)
	{
		const FNavLinkSegmentDrawing& Link = OffMeshSegmentLinks[LinkIndex];
		if ((Link.SupportedAgentsBits & AgentMask) == 0)
		{
			continue;
		}
		const FVector::FReal RealSegmentsStart = FMath::Max(SegmentArcHeight * (Link.RightStart - Link.LeftStart).Size() / 10., 8.);
		const FVector::FReal RealSegmentsEnd= FMath::Max(SegmentArcHeight * (Link.RightEnd - Link.LeftEnd).Size() / 10., 8.);
		check(RealSegmentsStart >= 0 && RealSegmentsStart <= (FVector::FReal)TNumericLimits<uint32>::Max());
		check(RealSegmentsEnd >= 0 && RealSegmentsEnd <= (FVector::FReal)TNumericLimits<uint32>::Max());
		const uint32 SegmentsStart = static_cast<uint32>(RealSegmentsStart);
		const uint32 SegmentsEnd = static_cast<uint32>(RealSegmentsEnd);
		DrawArc(PDI, Link.LeftStart, Link.RightStart, SegmentArcHeight, SegmentsStart, Link.Color, SDPG_World, 3.5f);
		DrawArc(PDI, Link.LeftEnd, Link.RightEnd, SegmentArcHeight, SegmentsEnd, Link.Color, SDPG_World, 3.5f);
		const FVector VOffset(0,0,FVector::Dist(Link.LeftStart, Link.RightStart)*1.333f);

		switch (Link.Direction)
		{
		case ENavLinkDirection::LeftToRight:
			DrawArrowHead(PDI, Link.RightStart, Link.LeftStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.RightEnd, Link.LeftEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::RightToLeft:
			DrawArrowHead(PDI, Link.LeftStart, Link.RightStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.LeftEnd, Link.RightEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::BothWays:
		default:
			DrawArrowHead(PDI, Link.RightStart, Link.LeftStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.RightEnd, Link.LeftEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.LeftStart, Link.RightStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.LeftEnd, Link.RightEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		}

		// draw snap-spheres on both ends
		if (Link.SnapHeight < 0)
		{
			for (int32 StepHeightIndex = 0; StepHeightIndex < StepHeights.Num(); ++StepHeightIndex)
			{
				GetCylinderMesh(Link.RightStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
				GetCylinderMesh(Link.RightEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
				GetCylinderMesh(Link.LeftStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
				GetCylinderMesh(Link.LeftEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
			}
		}
		else
		{
			GetCylinderMesh(Link.RightStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
			GetCylinderMesh(Link.RightEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
			GetCylinderMesh(Link.LeftStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
			GetCylinderMesh(Link.LeftEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World, ViewIndex, Collector);
		}
	}
}

// 另一路绘制：直接调 PDI 的 DrawLine / DrawCylinder。
// 用途：NavMeshRenderingComponent 里需要在不构造 SceneProxy 的情况下也能绘 link（如 VLog）。
// 与 GetLinkMeshes 基本等价，只是底层 API 不同（PDI 版没有光照 mesh）。
void FNavLinkRenderingProxy::DrawLinks(FPrimitiveDrawInterface* PDI, TArray<FNavLinkDrawing>& OffMeshPointLinks, TArray<FNavLinkSegmentDrawing>& OffMeshSegmentLinks, TArray<float>& StepHeights, FMaterialRenderProxy* const MeshColorInstance, uint32 AgentMask)
{
	static const FColor LinkColor(0,0,166);
	static const float LinkArcThickness = 3.f;
	static const float LinkArcHeight = 0.4f;

	if (StepHeights.Num() == 0)
	{
		StepHeights.Add(FNavigationSystem::FallbackAgentHeight / 2);
	}

	for (int32 LinkIndex = 0; LinkIndex < OffMeshPointLinks.Num(); ++LinkIndex)
	{
		const FNavLinkDrawing& Link = OffMeshPointLinks[LinkIndex];
		if ((Link.SupportedAgentsBits & AgentMask) == 0)
		{
			continue;
		}

		const FVector::FReal RealSegments = FMath::Max(LinkArcHeight * (Link.Right - Link.Left).Size() / 10., 8.);
		check(RealSegments >= 0 && RealSegments <= (FVector::FReal)TNumericLimits<uint32>::Max());
		const uint32 Segments = static_cast<uint32>(RealSegments);
		DrawArc(PDI, Link.Left, Link.Right, LinkArcHeight, Segments, Link.Color, SDPG_World, 3.5f);
		const FVector VOffset(0,0,FVector::Dist(Link.Left, Link.Right)*1.333f);

		switch (Link.Direction)
		{
		case ENavLinkDirection::LeftToRight:
			DrawArrowHead(PDI, Link.Right, Link.Left+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::RightToLeft:
			DrawArrowHead(PDI, Link.Left, Link.Right+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::BothWays:
		default:
			DrawArrowHead(PDI, Link.Right, Link.Left+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.Left, Link.Right+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		}

		// draw snap-spheres on both ends
		if (Link.SnapHeight < 0)
		{
			for (int32 StepHeightIndex = 0; StepHeightIndex < StepHeights.Num(); ++StepHeightIndex)
			{
				DrawCylinder(PDI, Link.Right, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World);
				DrawCylinder(PDI, Link.Left, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World);
			}
		}
		else
		{
			DrawCylinder(PDI, Link.Right, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World);
			DrawCylinder(PDI, Link.Left, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World);
		}
	}

	static const float SegmentArcHeight = 0.25f;
	for (int32 LinkIndex = 0; LinkIndex < OffMeshSegmentLinks.Num(); ++LinkIndex)
	{
		const FNavLinkSegmentDrawing& Link = OffMeshSegmentLinks[LinkIndex];
		if ((Link.SupportedAgentsBits & AgentMask) == 0)
		{
			continue;
		}

		const FVector::FReal RealSegmentsStart = FMath::Max(SegmentArcHeight * (Link.RightStart - Link.LeftStart).Size() / 10., 8.);
		const FVector::FReal RealSegmentsEnd = FMath::Max(SegmentArcHeight * (Link.RightEnd - Link.LeftEnd).Size() / 10., 8.);
		check(RealSegmentsStart >= 0 && RealSegmentsStart <= (FVector::FReal)TNumericLimits<uint32>::Max());
		check(RealSegmentsEnd >= 0 && RealSegmentsEnd <= (FVector::FReal)TNumericLimits<uint32>::Max());
		const uint32 SegmentsStart = static_cast<uint32>(RealSegmentsStart);
		const uint32 SegmentsEnd = static_cast<uint32>(RealSegmentsEnd);
		DrawArc(PDI, Link.LeftStart, Link.RightStart, SegmentArcHeight, SegmentsStart, Link.Color, SDPG_World, 3.5f);
		DrawArc(PDI, Link.LeftEnd, Link.RightEnd, SegmentArcHeight, SegmentsEnd, Link.Color, SDPG_World, 3.5f);
		const FVector VOffset(0,0,FVector::Dist(Link.LeftStart, Link.RightStart)*1.333f);

		switch (Link.Direction)
		{
		case ENavLinkDirection::LeftToRight:
			DrawArrowHead(PDI, Link.RightStart, Link.LeftStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.RightEnd, Link.LeftEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::RightToLeft:
			DrawArrowHead(PDI, Link.LeftStart, Link.RightStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.LeftEnd, Link.RightEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		case ENavLinkDirection::BothWays:
		default:
			DrawArrowHead(PDI, Link.RightStart, Link.LeftStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.RightEnd, Link.LeftEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.LeftStart, Link.RightStart+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			DrawArrowHead(PDI, Link.LeftEnd, Link.RightEnd+VOffset, 30.f, Link.Color, SDPG_World, 3.5f);
			break;
		}

		// draw snap-spheres on both ends
		if (Link.SnapHeight < 0)
		{
			for (int32 StepHeightIndex = 0; StepHeightIndex < StepHeights.Num(); ++StepHeightIndex)
			{
				DrawCylinder(PDI, Link.RightStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World);
				DrawCylinder(PDI, Link.RightEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World);
				DrawCylinder(PDI, Link.LeftStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World);
				DrawCylinder(PDI, Link.LeftEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, StepHeights[StepHeightIndex], 10, MeshColorInstance, SDPG_World);
			}
		}
		else
		{
			DrawCylinder(PDI, Link.RightStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World);
			DrawCylinder(PDI, Link.RightEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World);
			DrawCylinder(PDI, Link.LeftStart, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World);
			DrawCylinder(PDI, Link.LeftEnd, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Link.SnapRadius, Link.SnapHeight, 10, MeshColorInstance, SDPG_World);
		}
	}
}

// 可见性：只有开启导航 ShowFlag 且组件被选中时才画 —— 避免普通编辑状态下视口被 link 铺满。
FPrimitiveViewRelevance FNavLinkRenderingProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && IsSelected() && (View && View->Family && View->Family->EngineShowFlags.Navigation);
	Result.bDynamicRelevance = true;
	// ideally the TranslucencyRelevance should be filled out by the material, here we do it conservative
	Result.bSeparateTranslucency = Result.bNormalTranslucency = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
	return Result;
}

uint32 FNavLinkRenderingProxy::GetMemoryFootprint( void ) const 
{ 
	return( sizeof( *this ) + GetAllocatedSize() ); 
}

uint32 FNavLinkRenderingProxy::GetAllocatedSize( void ) const 
{ 
	return IntCastChecked<uint32>(FPrimitiveSceneProxy::GetAllocatedSize() + OffMeshPointLinks.GetAllocatedSize() + OffMeshSegmentLinks.GetAllocatedSize());
}

