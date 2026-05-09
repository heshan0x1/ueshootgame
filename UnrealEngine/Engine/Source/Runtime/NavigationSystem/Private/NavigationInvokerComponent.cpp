// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationInvokerComponent.cpp
// -----------------------------------------------------------------------------
// UNavigationInvokerComponent 实现。典型数据流（见架构文档 4.6）：
//   Activate()
//     → UNavigationSystemV1::RegisterNavigationInvoker(Owner, GenR, RemR, Agents, Pri)
//     → 进入 UNavigationSystemV1::Invokers 映射表
//   每 ActiveTilesUpdateInterval 秒：
//     UpdateInvokers() 读取 Invokers → 构造 InvokerLocations 快照
//     → UpdateNavDataActiveTiles() 差集对比 → PendingDirtyTiles
// =============================================================================

#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"
#include "AI/Navigation/NavigationInvokerPriority.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationInvokerComponent)

// 构造：默认 3000/5000 生成/移除半径（约 30m/50m），Priority=Default，自动激活。
// 注：SupportedAgents 必须显式 MarkInitialized，否则会被当成"未初始化"忽略。
UNavigationInvokerComponent::UNavigationInvokerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TileGenerationRadius(3000)
	, TileRemovalRadius(5000)
	, Priority(ENavigationInvokerPriority::Default)
{
	bAutoActivate = true;
	SupportedAgents.MarkInitialized();
}

// Activate：注册到 NavigationSystem；同时挂 OnNavigationInitStart 监听，
// 应对"NavSystem 在组件激活后才创建"的时序（例如关卡流式加载）。
void UNavigationInvokerComponent::Activate(bool bReset)
{
	Super::Activate(bReset);

	UNavigationSystemBase::OnNavigationInitStartStaticDelegate().AddUObject(this, &UNavigationInvokerComponent::OnNavigationInitStart);

	AActor* Owner = GetOwner();
	if (Owner)
	{
		UNavigationSystemV1::RegisterNavigationInvoker(*Owner, TileGenerationRadius, TileRemovalRadius, SupportedAgents, Priority);
	}
}

// Deactivate：从 Invokers 表移除，同时解绑 Init 回调。
void UNavigationInvokerComponent::Deactivate()
{
	Super::Deactivate();

	AActor* Owner = GetOwner();
	if (Owner)
	{
		UNavigationSystemV1::UnregisterNavigationInvoker(*Owner);
	}

	UNavigationSystemBase::OnNavigationInitStartStaticDelegate().RemoveAll(this);
}

void UNavigationInvokerComponent::PostInitProperties()
{
	Super::PostInitProperties();
	// 补一次 MarkInitialized，确保从资产反序列化来的实例也被视为已初始化
	SupportedAgents.MarkInitialized();
}

// 供外部显式把自己登记到指定 NavSys（例如 NavSystem 重建时批量挂回）。
void UNavigationInvokerComponent::RegisterWithNavigationSystem(UNavigationSystemV1& NavSys)
{
	if (IsActive())
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			NavSys.RegisterInvoker(*Owner, TileGenerationRadius, TileRemovalRadius, SupportedAgents, Priority);
		}
	}
}

// 运行时改半径 —— 仅修改字段，下次 UpdateInvokers 周期时才生效。
void UNavigationInvokerComponent::SetGenerationRadii(const float GenerationRadius, const float RemovalRadius)
{
	TileGenerationRadius = GenerationRadius;
	TileRemovalRadius = RemovalRadius;
}

// NavigationSystem 即将开始初始化：把自己从可能的旧表里先移除再注册。
// 目的：避免 Actor 跨 World 时残留在旧 NavSys。
void UNavigationInvokerComponent::OnNavigationInitStart(const UNavigationSystemBase& NavSys)
{
	if (NavSys.GetWorld() != GetWorld())
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		UNavigationSystemV1::UnregisterNavigationInvoker(*Owner);
		UNavigationSystemV1::RegisterNavigationInvoker(*Owner, TileGenerationRadius, TileRemovalRadius, SupportedAgents, Priority);
	}
}
