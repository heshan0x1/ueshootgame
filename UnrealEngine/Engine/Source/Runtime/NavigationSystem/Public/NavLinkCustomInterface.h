// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 INavLinkCustomInterface —— 自定义 NavLink（门/梯子/跳跃/弹射）接口。
//   每条"可动态控制"的链接实例都应实现该接口；它向 NavMesh 提供链接的端点、
//   Area 类、唯一 ID 以及 AI 到达链接时的回调钩子。
//
// 核心接口：
//   INavLinkCustomInterface —— 提供 GetLinkData / GetId / IsLinkPathfindingAllowed /
//     OnLinkMoveStarted / OnLinkMoveFinished 等生命周期回调；
//     并提供静态辅助 GetUniqueId / UpdateUniqueId / GetModifier。
//
// 与其它文件的关系：
//   - 实现文件：Private/NavigationSystemTypes.cpp 里有静态成员与辅助函数实现。
//   - 主要使用者：UNavLinkCustomComponent（SmartLink，NavSystem Module E）、
//     ARecastNavMesh 的 OffMeshConnection 构建流程。
//   - 5.3 开始 LinkId 改用 FNavLinkId（64 位，稳定可序列化），旧的自增 uint32
//     API 均已 UE_DEPRECATED。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"
#include "NavAreas/NavArea.h"
#include "AI/Navigation/NavLinkDefinition.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "Engine/World.h"
#endif
#include "Engine/WorldInitializationValues.h"
#include "NavLinkCustomInterface.generated.h"

/** 
 *  Interface for custom navigation links
 *
 *  They can affect path finding requests without navmesh rebuilds (e.g. opened/closed doors),
 *  allows updating their area class without navmesh rebuilds (e.g. dynamic path cost)
 *  and give hooks for supporting custom movement (e.g. ladders),
 *
 *  Owner is responsible for registering and unregistering links in NavigationSystem:
 *  - RegisterCustomLink
 *  - UnregisterCustomLink
 *
 *  See also: NavLinkCustomComponent
 */

// UInterface 壳：用于反射注册，不在蓝图中实现。
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UNavLinkCustomInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class INavLinkCustomInterface
{
	GENERATED_IINTERFACE_BODY()

	/** Get basic link data: two points (relative to owner) and direction */
	// 返回链接两端在 Owner 局部坐标下的位置，以及链接方向（单向/双向）。
	virtual void GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const {};

	/** Get agents supported by this link */
	// 返回该链接支持哪些 Agent（按 FNavAgentSelector 位集）。
	virtual void GetSupportedAgents(FNavAgentSelector& OutSupportedAgents) const {};

	/** Get basic link data: area class (null = default walkable) */
	// 返回链接所属的 Area 类（影响代价/Filter），nullptr 表示默认可行走区域。
	virtual TSubclassOf<UNavArea> GetLinkAreaClass() const { return nullptr; }

	// 辅助 ID：用于在生成阶段或外部工具里追踪"同一个逻辑链接"的多份实体。
	virtual FNavLinkAuxiliaryId GetAuxiliaryId() const { return FNavLinkAuxiliaryId::Invalid; }

	UE_DEPRECATED(5.3, "LinkIds are now based on a FNavLinkId. Call GetId() instead. This function only returns Invalid Id.")
	virtual uint32 GetLinkId() const final { return static_cast<uint32>(FNavLinkId::Invalid.GetId()); }

	/** Get unique ID number for custom link
	 *  Owner should get its unique ID by calling INavLinkCustomInterface::GetUniqueId() and store it
	 */
	// 返回链接的稳定唯一 ID（5.3+ 为 FNavLinkId，64 位哈希）。
	virtual FNavLinkId GetId() const { return FNavLinkId::Invalid; }

	UE_DEPRECATED(5.3, "LinkIds are now based on a FNavLinkId. Call the version of this function that takes a FNavLinkId. This function now has no effect.")
	virtual void UpdateLinkId(uint32 NewUniqueId) final {}

	/** Update unique ID number for custom link by navigation system. */
	// NavigationSystem 发现 ID 冲突/重分配时回调实现者更新 ID。
	virtual void UpdateLinkId(FNavLinkId NewUniqueId) {}

	/** Get object owner of navigation link, used for creating containers with multiple links */
	// 返回此链接的 UObject 所有者；默认把 this 作为 UObject 返回。
	NAVIGATIONSYSTEM_API virtual UObject* GetLinkOwner() const;

	/** Check if link allows path finding
	 *  Querier is usually an AIController trying to find path
	 */
	// 关键回调：寻路时询问"此链接对指定 Querier 是否允许通行"；例如关闭的门返回 false。
	// 返回 false 可在不重建 Tile 的前提下让寻路绕开该链接。
	virtual bool IsLinkPathfindingAllowed(const UObject* Querier) const { return true; }

	/** Notify called when agent starts using this link for movement.
	 *  returns true = custom movement, path following will NOT update velocity until FinishUsingCustomLink() is called on it
	 */
	// Agent 到达链接入口时回调；返回 true 代表接管移动（爬梯/弹射等），PathFollowing 停止更新速度直到 FinishUsingCustomLink。
	virtual bool OnLinkMoveStarted(class UObject* PathComp, const FVector& DestPoint) { return false; }

	/** Notify called when agent finishes using this link for movement */
	// 对应的结束回调（无论自定义移动成功与否都会调用）。
	virtual void OnLinkMoveFinished(class UObject* PathComp) {}

	/** Whether or not this link has custom reach conditions that need to override the default reach checks done by the path following component. */
	// 是否用自定义"到达检测"覆盖 PathFollowing 默认实现。
	virtual bool IsLinkUsingCustomReachCondition(const UObject* PathComp) const { return false; }

	/** Function that replaces the default reach check when IsLinkUsingCustomReachCondition is true. 
	 *  Returns true if CurrentLocation has reached the start of the link.
	 */
	// 自定义到达判定：在 IsLinkUsingCustomReachCondition 返回 true 时生效。
	virtual bool HasReachedLinkStart(const UObject* PathComp, const FVector& CurrentLocation, const FNavPathPoint& LinkStart, const FNavPathPoint& LinkEnd) const { return true; }

	UE_DEPRECATED(5.3, "LinkIds are now based on FNavLinkId using FNavLinkId::GenerateUniqueId(). This function will still generate an incremental Id however it does not work well in all circumstances.")
	static NAVIGATIONSYSTEM_API uint32 GetUniqueId();

	/** Helper function: bump unique ID numbers above given one */
	UE_DEPRECATED(5.3, "LinkIds are now based on FNavLinkId. If your project is still using any of the old incremental Ids (saved in actors in levels or licensee code) then this function must be called still (typically by existing engine code), otherwise it is not necessary.")
	static NAVIGATIONSYSTEM_API void UpdateUniqueId(FNavLinkId AlreadyUsedId);

	UE_DEPRECATED(5.3, "LinkIds are now based on FNavLinkId. You may need to call the other version of this function that takes a FNavLinkId. This function has no effect.")
	static void UpdateUniqueId(uint32 AlreadyUsedId) {}

	/** Helper function: create modifier for navigation data export */
	// 从某条 CustomLink 生成一个 FNavigationLink（导出给 Tile 构造 OffMeshConnection 用）。
	static NAVIGATIONSYSTEM_API FNavigationLink GetModifier(const INavLinkCustomInterface* CustomNavLink);
	
	UE_DEPRECATED(5.3, "LinkIds are now based on a FNavLinkId Hash. If your project is still using any of the old incremental Ids then this function must be called still (typically by existing engine code), otherwise it is not necessary.")
	static NAVIGATIONSYSTEM_API void ResetUniqueId();

	// 世界初始化前回调：重置遗留的自增 ID 计数器（仅旧 LinkId 兼容路径使用）。
	static NAVIGATIONSYSTEM_API void OnPreWorldInitialization(UWorld* World, const FWorldInitializationValues IVS);

	UE_DEPRECATED(5.3, "LinkIds are now based on FNavLinkId using FNavLinkId::GenerateUniqueId().")
	static uint32 NextUniqueId;
};

