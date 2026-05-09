// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationDataHandler.h —— Octree 与 DirtyAreas 的统一门面
// -----------------------------------------------------------------------------
// 文件职责：
//   - 把 FNavigationOctreeController 与 FNavigationDirtyAreasController 组合成
//     一个纯 struct 门面 FNavigationDataHandler；
//   - 提供"注册/注销/更新"NavigationElement 的一致入口（以 FNavigationElement
//     或 UObject+INavRelevantInterface 两种方式，5.5 后首选前者）；
//   - ProcessPendingOctreeUpdates：把 PendingUpdates 里的元素真正写进 Octree
//     并下发相应的 DirtyArea；
//   - AddLevelCollisionToOctree / RemoveLevelCollisionFromOctree：关卡级碰撞批量入网。
//
// 这是一层"薄"代理：本身不持有状态（两个引用都是外部传入的），可以被
// UNavigationSystemV1、FRecastNavMeshGenerator 等不同调用者复用。
// =============================================================================

#pragma once

#include "NavigationOctreeController.h"
#include "NavigationDirtyAreasController.h"

struct FNavigationDirtyElement;
class UNavArea;

// 将 OctreeController + DirtyAreasController 组合的门面。
// 所有"对 NavOctree/DirtyArea 的写操作"都应经由本 struct，保证两边状态一致。
struct FNavigationDataHandler
{
	FNavigationOctreeController& OctreeController;       // 引用：Octree 容器 + PendingUpdates
	FNavigationDirtyAreasController& DirtyAreasController; // 引用：脏区域累积器

	NAVIGATIONSYSTEM_API FNavigationDataHandler(FNavigationOctreeController& InOctreeController, FNavigationDirtyAreasController& InDirtyAreasController);

	// 分配 NavOctree（若尚未分配），并配置数据收集模式与"Modifier 采集超时警告阈值"
	NAVIGATIONSYSTEM_API void ConstructNavOctree(const FVector& Origin, const double Radius, const ENavDataGatheringModeConfig DataGatheringMode, const float GatheringNavModifiersWarningLimitTime);

	UE_DEPRECATED(5.5, "Use RegisterFromNavOctree instead.")
	NAVIGATIONSYSTEM_API void RemoveNavOctreeElementId(const FOctreeElementId2& ElementId, int32 UpdateFlags);
	/**
	 * Removes the octree node and the NavigationElementHandle-OctreeElementId pair associated to the specified OctreeElementId.
	 * It will also dirty the area base of the NavigationElement values and the specified update flags.
	 */
	// 真正从 Octree 摘除节点，同时把移除覆盖的 AABB 标脏，保证 NavData 重新构建该区域。
	NAVIGATIONSYSTEM_API void RemoveFromNavOctree(const FOctreeElementId2& ElementId, int32 UpdateFlags);

	// 把一个 FNavigationElement 推进 PendingUpdates 集合。
	// 注意：不会立刻插入 Octree，真正插入由 ProcessPendingOctreeUpdates 统一做。
	// 返回 PendingUpdates 里的 set id 以便后续引用。
	NAVIGATIONSYSTEM_API FSetElementId RegisterElementWithNavOctree(const TSharedRef<const FNavigationElement>& ElementRef, int32 UpdateFlags);
	UE_DEPRECATED(5.5, "Use RegisterNavigationElementWithNavOctree instead.")
	NAVIGATIONSYSTEM_API FSetElementId RegisterNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags);
	// 把"单个 Pending 元素"转成 Octree 节点；由 ProcessPendingOctreeUpdates 每项调用。
	NAVIGATIONSYSTEM_API void AddElementToNavOctree(const FNavigationDirtyElement& DirtyElement);

	/**
	 * Removes associated NavOctreeElement and invalidates associated pending updates. Also removes element from the list of children
	 * of the NavigationParent, if any.
	 * @param ElementRef		Navigation element for which we must remove the associated NavOctreeElement
	 * @param UpdateFlags		Flags indicating in which context the method is called to allow/forbid certain operations
	 *
	 * @return True if associated NavOctreeElement has been removed or pending update has been invalidated; false otherwise.
	 */
	// 注销元素：若仍在 PendingUpdates 则把该条目 invalidate；若已进 Octree 则 RemoveFromNavOctree。
	// 同时处理 NavigationParent 的 Children 维护。
	NAVIGATIONSYSTEM_API bool UnregisterElementWithNavOctree(const TSharedRef<const FNavigationElement>& ElementRef, int32 UpdateFlags);
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API bool UnregisterNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags);

	/**
	 * Unregister element associated with the provided handle and register the new element.
	 * Also update any pending update associated to that element.
	 */
	// 同一个 Handle 下的元素替换：先 Unregister 再 Register，同步更新 Pending 条目。
	// 典型场景：组件的 Owner/Transform/Bounds 变动需要以新 Element 接管。
	NAVIGATIONSYSTEM_API void UpdateNavOctreeElement(FNavigationElementHandle ElementHandle, const TSharedRef<const FNavigationElement>& UpdatedElement, int32 UpdateFlags);
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API void UpdateNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags);

	UE_DEPRECATED(5.5, "This method is no longer public and should not be called directly.")
	NAVIGATIONSYSTEM_API void UpdateNavOctreeParentChain(UObject& ElementOwner, bool bSkipElementOwnerUpdate = false);

	// 只更新 Bounds（避免整体 re-gather）。用于纯粹的 Transform 平移场景。
	// DirtyAreas 是"本次 Bounds 变更所涉及的外包 AABB 列表"（通常是老 AABB + 新 AABB）。
	NAVIGATIONSYSTEM_API bool UpdateNavOctreeElementBounds(FNavigationElementHandle Element, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas);
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API bool UpdateNavOctreeElementBounds(UObject& Object, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas);

	// 几何查询：找出在 QueryBox 内且通过 Filter 的所有 Octree 元素（拷贝到 Elements）。
	NAVIGATIONSYSTEM_API void FindElementsInNavOctree(const FBox& QueryBox, const FNavigationOctreeFilter& Filter, TArray<FNavigationOctreeElement>& Elements);

	// 只替换 Element 所在节点的 AreaClass（不动几何）。用于门/陷阱 Area 切换。
	NAVIGATIONSYSTEM_API bool ReplaceAreaInOctreeData(FNavigationElementHandle Element, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool bReplaceChildClasses) const;
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API bool ReplaceAreaInOctreeData(const UObject& Object, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool bReplaceChildClasses);

	// 关卡（ULevel）整体进/出 Octree：用于流式关卡加载/卸载时一次性处理里面的静态几何。
	NAVIGATIONSYSTEM_API void AddLevelCollisionToOctree(ULevel& Level);
	NAVIGATIONSYSTEM_API void RemoveLevelCollisionFromOctree(ULevel& Level);

	UE_DEPRECATED(5.5, "This method will be removed. Use UNavigationSystemV1 version instead.")
	NAVIGATIONSYSTEM_API void UpdateActorAndComponentsInNavOctree(AActor& Actor);

	// 把 PendingUpdates 里所有条目逐个 AddElementToNavOctree，然后清空 Pending。
	// 这是"从 Pending 到真正进入 Octree"的单向流，每帧 Tick 调用一次。
	NAVIGATIONSYSTEM_API void ProcessPendingOctreeUpdates();

	// 惰性 gather：Recast tile 生成时若发现某 Element 的 Data 仍是 Pending lazy，
	// 可由此立即补充一次采集（见 FNavigationOctree::DemandLazyDataGathering）。
	NAVIGATIONSYSTEM_API void DemandLazyDataGathering(FNavigationRelevantData& ElementData);

private:
	// 当元素自身发生变更时，沿"父子链"递归更新所有挂在同一 NavigationParent 下的子元素。
	// 典型场景：Actor 移动后它下面的 NavModifierComponent 都要刷新 Bounds。
	void UpdateNavOctreeParentChain(const TSharedRef<const FNavigationElement>& Element, bool bSkipElementOwnerUpdate = false);
};
