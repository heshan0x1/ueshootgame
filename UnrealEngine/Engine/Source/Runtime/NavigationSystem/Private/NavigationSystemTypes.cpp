// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   NavigationSystemTypes.h 与多个接口头（NavLinkHostInterface / NavLinkCustomInterface /
//   NavigationPathGenerator）的实现单元，包括：
//     - NavigationHelper::GatherCollision 系列（BodySetup/AggGeom → Recast 顶点/索引）
//     - NavigationHelper::FNavLinkOwnerData 构造
//     - NavLink 默认处理器 DefaultNavLinkProcessorImpl / SegmentProcessor
//       （包含端点交换与 MaxFallDownLength 的地面投射校正）
//     - ProcessNavLink*AndAppend / SetNavLinkProcessorDelegate 等委托机制
//     - INavLinkCustomInterface 静态成员与 NextUniqueId（Legacy 自增 ID 逻辑）
//
// 副作用/线程约束：
//   - RawGeometryFall 使用 World LineTrace（主线程）；仅在 NavLink 处理阶段调用。
//   - 本文件里的 NavLinkProcessor / NavLinkSegmentProcessor 全局委托应在
//     StartupModule 一次性设置，不要运行时频繁切换。
// =============================================================================

#include "NavigationSystemTypes.h"
#include "NavLinkCustomInterface.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastGeometryExport.h"
#include "Engine/World.h"
#include "NavCollision.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/StaticMeshComponent.h"
#include "VisualLogger/VisualLogger.h"

namespace
{
	// 从 FallStart 垂直向下做一次 LineTrace，返回碰到静态几何的跌落距离；
	// 没有静态网格命中返回 0。用于"NavLink 右端点自动贴地"的 MaxFallDownLength 处理。
	// 仅主线程调用（依赖 UWorld::LineTraceSingleByChannel）。
	FORCEINLINE_DEBUGGABLE FVector::FReal RawGeometryFall(const AActor* Querier, const FVector& FallStart, const FVector::FReal FallLimit)
	{
		FVector::FReal FallDownHeight = 0.;

		UE_VLOG_SEGMENT(Querier, LogNavigation, Log, FallStart, FallStart + FVector(0, 0, -FallLimit)
			, FColor::Red, TEXT("TerrainTrace"));

		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RawGeometryFall), true, Querier);
		FHitResult Hit;
		const bool bHit = Querier->GetWorld()->LineTraceSingleByChannel(Hit, FallStart, FallStart + FVector(0., 0., -FallLimit), ECC_WorldStatic, TraceParams);
		if (bHit)
		{
			UE_VLOG_LOCATION(Querier, LogNavigation, Log, Hit.Location, 15, FColor::Red, TEXT("%s")
				, *Hit.HitObjectHandle.GetName());

			// 仅接受 StaticMesh 命中：动态物体可能移动，不适合在 Tile 生成时固化跌落位置。
			if (Cast<UStaticMeshComponent>(Hit.Component.Get()))
			{
				const FVector Loc = Hit.ImpactPoint;
				FallDownHeight = FallStart.Z - Loc.Z;
			}
		}

		return FallDownHeight;
	}
}


namespace NavigationHelper
{
	// 从 UBodySetup 抽取几何到外部缓冲；空 RigidBody 直接返回，不触发计算。
	void GatherCollision(UBodySetup* RigidBody, TNavStatArray<FVector>& OutVertexBuffer, TNavStatArray<int32>& OutIndexBuffer, const FTransform& LocalToWorld, FBox& OutBounds)
	{
		if (RigidBody == NULL)
		{
			return;
		}
#if WITH_RECAST
		FRecastGeometryExport::ExportRigidBodyGeometry(*RigidBody, OutVertexBuffer, OutIndexBuffer, OutBounds, LocalToWorld);
#endif // WITH_RECAST
	}

	// 把 UBodySetup 的三个几何类别（TriMesh / Convex / AggGeom）一次性导出到 UNavCollision 的内部缓冲；
	// 走 DDC 缓存的 StaticMesh 使用。
	void GatherCollision(UBodySetup* RigidBody, UNavCollision* NavCollision)
	{
		if (RigidBody == NULL || NavCollision == NULL)
		{
			return;
		}
#if WITH_RECAST
		FRecastGeometryExport::ExportRigidBodyGeometry(*RigidBody
			, NavCollision->GetMutableTriMeshCollision().VertexBuffer, NavCollision->GetMutableTriMeshCollision().IndexBuffer
			, NavCollision->GetMutableConvexCollision().VertexBuffer, NavCollision->GetMutableConvexCollision().IndexBuffer
			, NavCollision->ConvexShapeIndices
			, NavCollision->Bounds);
#endif // WITH_RECAST
	}

	// 仅导出 AggregateGeom 的凸多边形/简单形状（不支持 TriMesh）到 UNavCollision。
	void GatherCollision(const FKAggregateGeom& AggGeom, UNavCollision& NavCollision)
	{
#if WITH_RECAST
		FRecastGeometryExport::ExportAggregatedGeometry(
			AggGeom,
			NavCollision.GetMutableConvexCollision().VertexBuffer,
			 NavCollision.GetMutableConvexCollision().IndexBuffer,
			 NavCollision.ConvexShapeIndices,
			 NavCollision.Bounds);
#endif // WITH_RECAST
	}

	// 从 Actor 构造 OwnerData：LinkToWorld = Actor 世界变换。
	FNavLinkOwnerData::FNavLinkOwnerData(const AActor& InActor)
	{
		Actor = &InActor;
		LinkToWorld = InActor.GetActorTransform();
	}

	// 从 Component 构造 OwnerData：LinkToWorld = 组件世界变换，Actor 取其 Owner。
	FNavLinkOwnerData::FNavLinkOwnerData(const USceneComponent& InComponent)
	{
		Actor = InComponent.GetOwner();
		LinkToWorld = InComponent.GetComponentTransform();
	}

	// NavLink 默认处理器：把 NavLink 数组调整后包成 FSimpleLinkNavModifier 加入 CompositeModifier。
	// 主要处理两件事：
	//   1) RightToLeft 单向链接：交换 Left/Right，使内部统一以"Left→Right"为正方向。
	//   2) MaxFallDownLength / LeftProjectHeight > 0 时，对端点做一次向下的地面投射，
	//      把链接端点贴到真实地面 Z（Tile 生成时匹配 NavMesh 表面）。
	void DefaultNavLinkProcessorImpl(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationLink>& IN NavLinks)
	{
		FSimpleLinkNavModifier SimpleLink(NavLinks, OwnerData.LinkToWorld);

		// adjust links
		// 迭代目标：对每条 NavLink 按方向与自动投射要求调整端点。
		for (int32 LinkIndex = 0; LinkIndex < SimpleLink.Links.Num(); ++LinkIndex)
		{
			FNavigationLink& Link = SimpleLink.Links[LinkIndex];

			// this one needs adjusting
			// 把方向统一为 LeftToRight 或 Both；RightToLeft 通过交换端点等价化简。
			if (Link.Direction == ENavLinkDirection::RightToLeft)
			{
				Swap(Link.Left, Link.Right);
			}

			// 分支：若配置了 MaxFallDownLength，尝试把 Right 端点向下投影到地面。
			if (Link.MaxFallDownLength > 0)
			{
				const FVector WorldRight = OwnerData.LinkToWorld.TransformPosition(Link.Right);
				const FVector::FReal FallDownHeight = RawGeometryFall(OwnerData.Actor, WorldRight, Link.MaxFallDownLength);

				if (FallDownHeight > 0.)
				{
					// @todo maybe it's a good idea to clear ModifiedLink.MaxFallDownLength here
					UE_VLOG_SEGMENT(OwnerData.Actor, LogNavigation, Log, WorldRight, WorldRight + FVector(0, 0, -FallDownHeight), FColor::Green, TEXT("FallDownHeight %d"), LinkIndex);

					Link.Right.Z -= FallDownHeight;
				}
			}

			// 分支：若配置了 LeftProjectHeight，Left 端点也做一次向下投影（常用于入口靠地板对齐）。
			if (Link.LeftProjectHeight > 0)
			{
				const FVector WorldLeft = OwnerData.LinkToWorld.TransformPosition(Link.Left);
				const FVector::FReal FallDownHeight = RawGeometryFall(OwnerData.Actor, WorldLeft, Link.LeftProjectHeight);

				if (FallDownHeight > 0.)
				{
					// @todo maybe it's a good idea to clear ModifiedLink.LeftProjectHeight here
					UE_VLOG_SEGMENT(OwnerData.Actor, LogNavigation, Log, WorldLeft, WorldLeft + FVector(0, 0, -FallDownHeight), FColor::Green, TEXT("LeftProjectHeight %d"), LinkIndex);

					Link.Left.Z -= FallDownHeight;
				}
			}
		}

		CompositeModifier->Add(SimpleLink);
	}

	// SegmentLink 版默认处理器：对线段两端（Start/End）分别做方向交换 + 地面投射。
	void DefaultNavLinkSegmentProcessorImpl(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationSegmentLink>& IN NavLinks)
	{
		FSimpleLinkNavModifier SimpleLink(NavLinks, OwnerData.LinkToWorld);

		// adjust links if needed
		// 迭代目标：对每条 SegmentLink 的两个端点做同样的方向归一化和地面投射。
		for (int32 LinkIndex = 0; LinkIndex < SimpleLink.SegmentLinks.Num(); ++LinkIndex)
		{
			FNavigationSegmentLink& Link = SimpleLink.SegmentLinks[LinkIndex];

			// this one needs adjusting
			if (Link.Direction == ENavLinkDirection::RightToLeft)
			{
				Swap(Link.LeftStart, Link.RightStart);
				Swap(Link.LeftEnd, Link.RightEnd);
			}

			if (Link.MaxFallDownLength > 0)
			{
				const FVector WorldRightStart = OwnerData.LinkToWorld.TransformPosition(Link.RightStart);
				const FVector WorldRightEnd = OwnerData.LinkToWorld.TransformPosition(Link.RightEnd);

				const FVector::FReal FallDownHeightStart = RawGeometryFall(OwnerData.Actor, WorldRightStart, Link.MaxFallDownLength);
				const FVector::FReal FallDownHeightEnd = RawGeometryFall(OwnerData.Actor, WorldRightEnd, Link.MaxFallDownLength);

				if (FallDownHeightStart > 0.)
				{
					// @todo maybe it's a good idea to clear ModifiedLink.MaxFallDownLength here
					UE_VLOG_SEGMENT(OwnerData.Actor, LogNavigation, Log, WorldRightStart, WorldRightStart + FVector(0, 0, -FallDownHeightStart), FColor::Green, TEXT("FallDownHeightStart %d"), LinkIndex);

					Link.RightStart.Z -= FallDownHeightStart;
				}
				if (FallDownHeightEnd > 0.)
				{
					// @todo maybe it's a good idea to clear ModifiedLink.MaxFallDownLength here
					UE_VLOG_SEGMENT(OwnerData.Actor, LogNavigation, Log, WorldRightEnd, WorldRightEnd + FVector(0, 0, -FallDownHeightEnd), FColor::Green, TEXT("FallDownHeightEnd %d"), LinkIndex);

					Link.RightEnd.Z -= FallDownHeightEnd;
				}
			}
		}

		CompositeModifier->Add(SimpleLink);
	}

	// 全局单例委托：默认指向上面两个 DefaultXxxImpl。业务可 SetNavLinkProcessorDelegate 替换。
	FNavLinkProcessorDataDelegate NavLinkProcessor = FNavLinkProcessorDataDelegate::CreateStatic(DefaultNavLinkProcessorImpl);
	FNavLinkSegmentProcessorDataDelegate NavLinkSegmentProcessor = FNavLinkSegmentProcessorDataDelegate::CreateStatic(DefaultNavLinkSegmentProcessorImpl);

	// 兼容旧签名：Actor* 版包装成 FNavLinkOwnerData 再调用新版。
	void ProcessNavLinkAndAppend(FCompositeNavModifier* OUT CompositeModifier, const AActor* Actor, const TArray<FNavigationLink>& IN NavLinks)
	{
		if (Actor)
		{
			ProcessNavLinkAndAppend(CompositeModifier, FNavLinkOwnerData(*Actor), NavLinks);
		}
	}

	// 新版：带 CYCLE 计数器，空数组快路径返回。
	void ProcessNavLinkAndAppend(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationLink>& IN NavLinks)
	{
		SCOPE_CYCLE_COUNTER(STAT_Navigation_AdjustingNavLinks);

		if (NavLinks.Num())
		{
			check(NavLinkProcessor.IsBound());
			NavLinkProcessor.Execute(CompositeModifier, OwnerData, NavLinks);
		}
	}

	// SegmentLink 的 Actor* 兼容入口。
	void ProcessNavLinkSegmentAndAppend(FCompositeNavModifier* OUT CompositeModifier, const AActor* Actor, const TArray<FNavigationSegmentLink>& IN NavLinks)
	{
		if (Actor)
		{
			ProcessNavLinkSegmentAndAppend(CompositeModifier, FNavLinkOwnerData(*Actor), NavLinks);
		}
	}

	// SegmentLink 新版入口。
	void ProcessNavLinkSegmentAndAppend(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationSegmentLink>& IN NavLinks)
	{
		SCOPE_CYCLE_COUNTER(STAT_Navigation_AdjustingNavLinks);

		if (NavLinks.Num())
		{
			check(NavLinkSegmentProcessor.IsBound());
			NavLinkSegmentProcessor.Execute(CompositeModifier, OwnerData, NavLinks);
		}
	}

	// 注入自定义 NavLink 处理器；输入委托必须已绑定，否则 check 触发。
	void SetNavLinkProcessorDelegate(const FNavLinkProcessorDataDelegate& NewDelegate)
	{
		check(NewDelegate.IsBound());
		NavLinkProcessor = NewDelegate;
	}

	// 注入自定义 SegmentLink 处理器。
	void SetNavLinkSegmentProcessorDelegate(const FNavLinkSegmentProcessorDataDelegate& NewDelegate)
	{
		check(NewDelegate.IsBound());
		NavLinkSegmentProcessor = NewDelegate;
	}

	// 判定 BodySetup 是否对导航生成有贡献：
	//   需同时满足：有几何 + 阻挡 Pawn/Vehicle + 开启 QueryAndPhysics。
	bool IsBodyNavigationRelevant(const UBodySetup& BodySetup)
	{
		const bool bBodyHasGeometry = (BodySetup.AggGeom.GetElementCount() > 0 || BodySetup.TriMeshGeometries.Num() > 0);

		// has any colliding geometry
		return bBodyHasGeometry
			// AND blocks any of Navigation-relevant 
			&& (BodySetup.DefaultInstance.GetResponseToChannel(ECC_Pawn) == ECR_Block || BodySetup.DefaultInstance.GetResponseToChannel(ECC_Vehicle) == ECR_Block)
			// AND has full colliding capabilities 
			&& BodySetup.DefaultInstance.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics;
	}
}

#include "NavLinkHostInterface.h"
//----------------------------------------------------------------------//
// interfaces
//----------------------------------------------------------------------//
#include "NavigationPathGenerator.h"
// UNavigationPathGenerator 默认构造：UHT 反射注册用。
UNavigationPathGenerator::UNavigationPathGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// INavLinkCustomInterface 的全局自增 ID 计数器（Legacy 路径；5.3+ 建议用 FNavLinkId）。
uint32 INavLinkCustomInterface::NextUniqueId = 1;

// UNavLinkHostInterface 默认构造：UHT 反射注册用。
UNavLinkHostInterface::UNavLinkHostInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// UNavLinkCustomInterface 默认构造：UHT 反射注册用。
UNavLinkCustomInterface::UNavLinkCustomInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 默认实现：把 this 作为 UObject 返回（假定实现者本身继承自 UObject 并实现了本接口）。
UObject* INavLinkCustomInterface::GetLinkOwner() const
{
	return Cast<UObject>((INavLinkCustomInterface*)this);
}

// Legacy：取下一个自增 ID 并递增；已 UE_DEPRECATED，请改用 FNavLinkId::GenerateUniqueId()。
uint32 INavLinkCustomInterface::GetUniqueId()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_LOG(LogNavLink, VeryVerbose, TEXT("%hs id: %u."), __FUNCTION__, NextUniqueId);
	return NextUniqueId++;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// Legacy：告知已用掉某 ID，把 NextUniqueId 调整到其之后以避免碰撞。
// 仅对旧式自增 ID 生效；FNavLinkId 哈希 ID 不需要。
void INavLinkCustomInterface::UpdateUniqueId(FNavLinkId AlreadyUsedId)
{
	// Only update NextUniqueId for old style incremental Ids.
	// 分支：只在传入的是旧式 Legacy ID 时才推进计数器，避免把哈希 ID 误当序号。
	if (AlreadyUsedId.IsLegacyId())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UE_CLOG(AlreadyUsedId.GetId() + 1 > NextUniqueId, LogNavLink, VeryVerbose, TEXT("%hs, updating NextUniqueId to: %llu."), __FUNCTION__, AlreadyUsedId.GetId() + 1)
		const uint64 NextId = FMath::Max((uint64)NextUniqueId, AlreadyUsedId.GetId() + 1);
		ensureMsgf(NextId <= TNumericLimits<uint32>::Max(), TEXT("Overflowing uint32 using legacy nav link id system!"));

		NextUniqueId = (uint32)NextId;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

// 从自定义链接对象构造一个 FNavigationLink（给 Tile 生成用的 Modifier）。
// 读取 Area / LinkId / 端点 / 方向 / SupportedAgents 五块信息。
FNavigationLink INavLinkCustomInterface::GetModifier(const INavLinkCustomInterface* CustomNavLink)
{
	FNavigationLink LinkMod;
	LinkMod.SetAreaClass(CustomNavLink->GetLinkAreaClass());
	LinkMod.NavLinkId = CustomNavLink->GetId();

	ENavLinkDirection::Type LinkDirection = ENavLinkDirection::BothWays;
	CustomNavLink->GetLinkData(LinkMod.Left, LinkMod.Right, LinkDirection);
	CustomNavLink->GetSupportedAgents(LinkMod.SupportedAgents);
	LinkMod.Direction = LinkDirection;

	return LinkMod;
}

// 世界创建前：重置 Legacy 自增 ID 计数器，避免跨 World 累积冲突。
void INavLinkCustomInterface::OnPreWorldInitialization(UWorld* World, const FWorldInitializationValues IVS)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ResetUniqueId();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// Legacy：把 NextUniqueId 复位为 1；仅旧 ID 路径使用。
void INavLinkCustomInterface::ResetUniqueId()
{
	UE_LOG(LogNavLink, VeryVerbose, TEXT("Reset navlink id."));
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	NextUniqueId = 1;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
