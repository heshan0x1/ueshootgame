// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkCustomComponent.h  ── SmartLink
// -----------------------------------------------------------------------------
// 同时实现 INavLinkCustomInterface 与 INavRelevantInterface（经 UNavRelevantComponent）。
// 它既是"一条可动态开关/可拦截寻路/可广播状态"的自定义链接，也是会被 Octree
// 收录的导航贡献者。
//
// 主要能力：
//   1) Enable/Disable 时只切换 EnabledAreaClass/DisabledAreaClass —— 不用重建 Tile！
//      - 寻路过滤器看到的是 Area Flag，切换 Area 意味着寻路直接视之为"禁入/可行走"。
//   2) 可配置 ObstacleArea / 偏移 / Extent：在链接端点附近挖一块"禁止区"
//      （典型：门关闭时把门前空间标成 Null，把寻路挤开）。
//   3) 可广播状态：BroadcastStateChange 通过一次 SphereTrace 找出周围 Pawn，
//      按 OnBroadcastFilter 过滤后把 link state 通知出去。
//   4) OnLinkMoveStarted：Agent 到达链接端点时回调，返回 true 代表"我接管 Movement"
//      （爬梯、跳跃、弹射均可自定义）；结束时调用 OnLinkMoveFinished。
//
// 与架构文档 4.5 节对应。
// 注意：ID 方案 UE5.3+ 使用 FNavLinkId；遗留 NavLinkUserId 标记为 DEPRECATED。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "NavAreas/NavArea.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "NavLinkCustomInterface.h"
#include "NavRelevantComponent.h"
#include "NavLinkCustomComponent.generated.h"


struct FNavigationRelevantData;

/**
 *  Encapsulates NavLinkCustomInterface interface, can be used with Actors not relevant for navigation
 *  
 *  Additional functionality:
 *  - can be toggled
 *  - can create obstacle area for easier/forced separation of link end points
 *  - can broadcast state changes to nearby agents
 */
// SmartLink：可开关 / 可带障碍 / 可广播的自定义链接组件。
UCLASS(MinimalAPI)
class UNavLinkCustomComponent : public UNavRelevantComponent, public INavLinkCustomInterface
{
	GENERATED_UCLASS_BODY()

	// Agent 到达链接端点时的通知；第三个参数是目标点
	DECLARE_DELEGATE_ThreeParams(FOnMoveReachedLink, UNavLinkCustomComponent* /*ThisComp*/, UObject* /*PathComp*/, const FVector& /*DestPoint*/);
	// 广播前的过滤器：填充 NotifyList 决定谁收到通知
	DECLARE_DELEGATE_TwoParams(FBroadcastFilter, UNavLinkCustomComponent* /*ThisComp*/, TArray<UObject*>& /*NotifyList*/);

	// BEGIN INavLinkCustomInterface
	// 返回世界空间下的端点与方向（LinkRelativeStart/End 叠加 Owner 变换）
	NAVIGATIONSYSTEM_API virtual void GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const override;
	// 允许寻路的代理位集
	NAVIGATIONSYSTEM_API virtual void GetSupportedAgents(FNavAgentSelector& OutSupportedAgents) const override;
	// 当前状态下使用的 AreaClass（Enabled / Disabled）
	NAVIGATIONSYSTEM_API virtual TSubclassOf<UNavArea> GetLinkAreaClass() const override;
	NAVIGATIONSYSTEM_API virtual FNavLinkAuxiliaryId GetAuxiliaryId() const override;
	NAVIGATIONSYSTEM_API virtual FNavLinkId GetId() const override;
	NAVIGATIONSYSTEM_API virtual void UpdateLinkId(FNavLinkId NewUniqueId) override;
	// 寻路过滤回调：外部可据 Querier 决定放行
	NAVIGATIONSYSTEM_API virtual bool IsLinkPathfindingAllowed(const UObject* Querier) const override;
	// 到达起点时触发；return true = 我接管 Movement
	NAVIGATIONSYSTEM_API virtual bool OnLinkMoveStarted(UObject* PathComp, const FVector& DestPoint) override;
	// 离开链接时的清理通知
	NAVIGATIONSYSTEM_API virtual void OnLinkMoveFinished(UObject* PathComp) override;
	// END INavLinkCustomInterface

	//~ Begin UNavRelevantComponent Interface
	// 把当前链接描述、可选的 Obstacle Box 塞入 FNavigationRelevantData
	NAVIGATIONSYSTEM_API virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	// Bounds：两端点 + Obstacle 的并集
	NAVIGATIONSYSTEM_API virtual void CalcAndCacheBounds() const override;
	//~ End UNavRelevantComponent Interface

	//~ Begin UActorComponent Interface
	// 向 NavigationSystem 注册自己，分配 FNavLinkId
	NAVIGATIONSYSTEM_API virtual void OnRegister() override;
	NAVIGATIONSYSTEM_API virtual void OnUnregister() override;
	// 用于 Rerun Construction 保持 ID 稳定
	NAVIGATIONSYSTEM_API virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;
	//~ End UActorComponent Interface

	// 把 InstanceData 中保存的 CustomLinkId / Auxiliary 还原到本组件
	NAVIGATIONSYSTEM_API void ApplyComponentInstanceData(struct FNavLinkCustomInstanceData* ComponentInstanceData);

	//~ Begin UObject Interface
	// Serialize：处理 UE5.3 ID 升级等版本迁移
	NAVIGATIONSYSTEM_API virtual void Serialize(FArchive& Ar) override;
	NAVIGATIONSYSTEM_API virtual void PostLoad() override;
#if WITH_EDITOR
	NAVIGATIONSYSTEM_API virtual void PostEditImport() override;
#endif
	//~ End UObject Interface

	/** set basic link data: end points and direction */
	// 设置链接端点/方向；会触发 RefreshNavigationModifiers 重新登记
	NAVIGATIONSYSTEM_API void SetLinkData(const FVector& RelativeStart, const FVector& RelativeEnd, ENavLinkDirection::Type Direction);
	// 生成一个 FNavigationLink 描述当前状态（供外部需要时读取）
	NAVIGATIONSYSTEM_API virtual FNavigationLink GetLinkModifier() const;

	/** set area class to use when link is enabled */
	// 切换启用态的 Area 类（例如 Default Walkable）。只改 Area，不重建 Tile。
	NAVIGATIONSYSTEM_API void SetEnabledArea(TSubclassOf<UNavArea> AreaClass);
	TSubclassOf<UNavArea> GetEnabledArea() const { return EnabledAreaClass; }

	/** set area class to use when link is disabled */
	// 切换禁用态的 Area 类（例如 UNavArea_Null）。只改 Area，不重建 Tile。
	NAVIGATIONSYSTEM_API void SetDisabledArea(TSubclassOf<UNavArea> AreaClass);
	TSubclassOf<UNavArea> GetDisabledArea() const { return DisabledAreaClass; }

	void SetSupportedAgents(const FNavAgentSelector& InSupportedAgents) { SupportedAgents = InSupportedAgents; }
	FNavAgentSelector GetSupportedAgents() const { return SupportedAgents; }

	/** add box obstacle during generation of navigation data
	  * this can be used to create empty area under doors */
	// 开启 Obstacle Box —— 在链接端点附近留一块空区（例如门前/门后避免穿墙路径）
	NAVIGATIONSYSTEM_API void AddNavigationObstacle(TSubclassOf<UNavArea> AreaClass, const FVector& BoxExtent, const FVector& BoxOffset = FVector::ZeroVector);

	/** removes simple obstacle */
	NAVIGATIONSYSTEM_API void ClearNavigationObstacle();

	/** set properties of trigger around link entry point(s), that will notify nearby agents about link state change */
	// 配置广播：半径 + Trace 通道 + 周期（0 表示只广播一次）
	NAVIGATIONSYSTEM_API void SetBroadcastData(float Radius, ECollisionChannel TraceChannel = ECC_Pawn, float Interval = 0.0f);

	NAVIGATIONSYSTEM_API void SendBroadcastWhenEnabled(bool bEnabled);
	NAVIGATIONSYSTEM_API void SendBroadcastWhenDisabled(bool bEnabled);

	/** set delegate to filter  */
	// 广播前的过滤器：可剔除不感兴趣的目标
	NAVIGATIONSYSTEM_API void SetBroadcastFilter(FBroadcastFilter const& InDelegate);

	/** change state of smart link (used area class) */
	// 切换启/禁状态 —— 核心：只切 AreaClass，不重建 Tile；配合广播通知正在路过的 Agent
	NAVIGATIONSYSTEM_API void SetEnabled(bool bNewEnabled);
	bool IsEnabled() const { return bLinkEnabled; }
	
	/** set delegate to notify about reaching this link during path following */
	// OnLinkMoveStarted 被调用时会再调用这个委托（调用层级：Interface → 本组件内部分发）
	NAVIGATIONSYSTEM_API void SetMoveReachedLink(FOnMoveReachedLink const& InDelegate);

	/** check is any agent is currently moving though this link */
	NAVIGATIONSYSTEM_API bool HasMovingAgents() const;

	/** get link start point in world space */
	NAVIGATIONSYSTEM_API FVector GetStartPoint() const;

	/** get link end point in world space */
	NAVIGATIONSYSTEM_API FVector GetEndPoint() const;

	TSubclassOf<UNavArea> GetObstacleAreaClass() const { return ObstacleAreaClass; }

	//////////////////////////////////////////////////////////////////////////
	// helper functions for setting delegates
	// 模板辅助：直接用成员函数指针绑定委托
	template< class UserClass >	
	inline void SetMoveReachedLink(UserClass* TargetOb, typename FOnMoveReachedLink::TMethodPtr< UserClass > InFunc)
	{
		SetMoveReachedLink(FOnMoveReachedLink::CreateUObject(TargetOb, InFunc));
	}
	template< class UserClass >	
	inline void SetMoveReachedLink(UserClass* TargetOb, typename FOnMoveReachedLink::TConstMethodPtr< UserClass > InFunc)
	{
		SetMoveReachedLink(FOnMoveReachedLink::CreateUObject(TargetOb, InFunc));
	}

	template< class UserClass >	
	inline void SetBroadcastFilter(UserClass* TargetOb, typename FBroadcastFilter::TMethodPtr< UserClass > InFunc)
	{
		SetBroadcastFilter(FBroadcastFilter::CreateUObject(TargetOb, InFunc));
	}
	template< class UserClass >	
	inline void SetBroadcastFilter(UserClass* TargetOb, typename FBroadcastFilter::TConstMethodPtr< UserClass > InFunc)
	{
		SetBroadcastFilter(FBroadcastFilter::CreateUObject(TargetOb, InFunc));
	}

protected:
#if WITH_EDITOR
	// 编辑器下 NavArea 注册/反注册监听
	NAVIGATIONSYSTEM_API void OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass);
	NAVIGATIONSYSTEM_API void OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass);
#endif // WITH_EDITOR

protected:

	UE_DEPRECATED(5.3, "LinkIds are now based on FNavLinkId. Use CustomLinkId instead. This Id is no longer used by the engine.")
	UPROPERTY()
	uint32 NavLinkUserId;

	/** link Id assigned by navigation system */
	// 5.3+ 正式 ID：全局唯一（FNavLinkId），由 NavigationSystem 登记时分配或升级得到
	UPROPERTY()
	FNavLinkId CustomLinkId;

	/** 
	 *  Assigned in the constructor. This uniquely identifies a component in an Actor, but will not be unique between duplicate level instances.
	 *  containing the same Actor.
	 *  This is Hashed with the Actor Instance FGuid to create the CustomLinkId so that Actors with more than one UNavLinkCustomComponent can have a 
	 *  completely unique ID per UNavLinkCustomComponent even across level instances.
	 **/
	// 构造时随机分配；与 Actor FGuid 合哈希得到 CustomLinkId，保证同一 Actor 多组件 ID 互不冲突
	UPROPERTY()
	FNavLinkAuxiliaryId AuxiliaryCustomLinkId;

	/** area class to use when link is enabled */
	// 启用态 Area（默认 Default Walkable）
	UPROPERTY(EditAnywhere, Category=SmartLink)
	TSubclassOf<UNavArea> EnabledAreaClass;

	/** area class to use when link is disabled */
	// 禁用态 Area（默认 Null，即禁入）
	UPROPERTY(EditAnywhere, Category=SmartLink)
	TSubclassOf<UNavArea> DisabledAreaClass;

	/** restrict area only to specified agents */
	// 只对指定代理开放（不在此位集内的代理寻路时视为禁入）
	UPROPERTY(EditAnywhere, Category=SmartLink)
	FNavAgentSelector SupportedAgents;

	/** start point, relative to owner */
	UPROPERTY(EditAnywhere, Category=SmartLink)
	FVector LinkRelativeStart;

	/** end point, relative to owner */
	UPROPERTY(EditAnywhere, Category=SmartLink)
	FVector LinkRelativeEnd;

	/** direction of link */
	UPROPERTY(EditAnywhere, Category=SmartLink)
	TEnumAsByte<ENavLinkDirection::Type> LinkDirection;

	/** is link currently in enabled state? (area class) */
	// 当前是否启用——决定 GetLinkAreaClass 返回 Enabled 还是 Disabled
	UPROPERTY(EditAnywhere, Category=SmartLink)
	uint32 bLinkEnabled : 1;

	/** should link notify nearby agents when it changes state to enabled */
	UPROPERTY(EditAnywhere, Category=Broadcast)
	uint32 bNotifyWhenEnabled : 1;

	/** should link notify nearby agents when it changes state to disabled */
	UPROPERTY(EditAnywhere, Category=Broadcast)
	uint32 bNotifyWhenDisabled : 1;

	/** if set, box obstacle area will be added to generation */
	// 是否生成 Obstacle Box
	UPROPERTY(EditAnywhere, Category=Obstacle)
	uint32 bCreateBoxObstacle : 1;

	/** offset of simple box obstacle */
	UPROPERTY(EditAnywhere, Category=Obstacle)
	FVector ObstacleOffset;

	/** extent of simple box obstacle */
	UPROPERTY(EditAnywhere, Category=Obstacle)
	FVector ObstacleExtent;

	/** area class for simple box obstacle */
	UPROPERTY(EditAnywhere, Category=Obstacle)
	TSubclassOf<UNavArea> ObstacleAreaClass;

	/** radius of state change broadcast */
	UPROPERTY(EditAnywhere, Category=Broadcast)
	float BroadcastRadius;

	/** interval for state change broadcast (0 = single broadcast) */
	// 广播间隔；0 = 只广播一次
	UPROPERTY(EditAnywhere, Category=Broadcast, Meta=(UIMin="0.0", ClampMin="0.0"))
	float BroadcastInterval;

	/** trace channel for state change broadcast */
	UPROPERTY(EditAnywhere, Category=Broadcast)
	TEnumAsByte<ECollisionChannel> BroadcastChannel;

	/** delegate to call when link is reached */
	// 广播前的过滤器委托
	FBroadcastFilter OnBroadcastFilter;

	/** list of agents moving though this link */
	// 正在穿越本链接的 Agent（弱引用，存 PathFollowingComp）
	TArray<TWeakObjectPtr<UObject> > MovingAgents;

	/** delegate to call when link is reached */
	// Agent 到达时回调
	FOnMoveReachedLink OnMoveReachedLink;

	/** Handle for efficient management of BroadcastStateChange timer */
	// 周期广播的定时器句柄
	FTimerHandle TimerHandle_BroadcastStateChange;

	/** notify nearby agents about link changing state */
	// 实际发射广播：组织 Trace + 过滤 + 调 OnNavLinkStateChanged
	NAVIGATIONSYSTEM_API void BroadcastStateChange();
	
	/** gather agents to notify about state change */
	// 半径球形 OverlapMulti 找出附近 Pawn，填入 NotifyList
	NAVIGATIONSYSTEM_API void CollectNearbyAgents(TArray<UObject*>& NotifyList);

#if WITH_EDITOR
	FDelegateHandle OnNavAreaRegisteredDelegateHandle;
	FDelegateHandle OnNavAreaUnregisteredDelegateHandle;
#endif // WITH_EDITOR
};

/** Used to store navlink data during RerunConstructionScripts */
// Rerun Construction 期间保存 ID，避免 ID 重抽导致 NavMesh 上的 OffMeshConnection 失效。
USTRUCT()
struct FNavLinkCustomInstanceData : public FActorComponentInstanceData
{
	GENERATED_BODY()

public:
	FNavLinkCustomInstanceData() = default;
	FNavLinkCustomInstanceData(const UNavLinkCustomComponent* SourceComponent)
		: FActorComponentInstanceData(SourceComponent)
	{}

	virtual ~FNavLinkCustomInstanceData() = default;

	virtual bool ContainsData() const override
	{
		return true;
	}

	// 在用户构造脚本跑完之后把 ID 还原到组件上
	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		Super::ApplyToComponent(Component, CacheApplyPhase);

		if (CacheApplyPhase == ECacheApplyPhase::PostUserConstructionScript)
		{
			CastChecked<UNavLinkCustomComponent>(Component)->ApplyComponentInstanceData(this);
		}
	}

	UPROPERTY()
	FNavLinkId CustomLinkId;

	UPROPERTY()
	FNavLinkAuxiliaryId AuxiliaryCustomLinkId;
};

