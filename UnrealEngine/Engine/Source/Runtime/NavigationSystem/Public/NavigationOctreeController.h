// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationOctreeController.h —— NavigationOctree 的封装控制器
// -----------------------------------------------------------------------------
// 文件职责：
//   - 持有 FNavigationOctree 的共享指针（NavOctree），外部所有对 Octree 的
//     读写都应通过本结构完成；
//   - 维护 PendingUpdates：本帧还没真正写进 Octree 的 FNavigationDirtyElement 集合；
//   - 维护 Parent→Children 映射（OctreeParentChildNodesMap），支持
//     "父子挂接"导航元素（如 Actor + NavModifierComponent）的批量刷新；
//   - 提供 Lock/Unlock NavOctree 的开关（bNavOctreeLock）。
//
// 与 FNavigationDataHandler 的关系：
//   FNavigationDataHandler 只"提供操作语义"，FNavigationOctreeController
//   才是数据的实际持有者。一个 UNavigationSystemV1 通常持有一份 Controller。
// =============================================================================

#pragma once
#include "NavigationOctree.h"
#include "AI/Navigation/NavigationDirtyElement.h"


// FNavigationDirtyElement 的 KeyFuncs：以 Element 的 FNavigationElementHandle 为键去重，
// 保证 PendingUpdates 中同一个元素只会有一条最新记录。
struct FNavigationDirtyElementKeyFunctions : BaseKeyFuncs<FNavigationDirtyElement, FNavigationElementHandle, /*bInAllowDuplicateKeys*/false>
{
	static FNavigationElementHandle GetSetKey(ElementInitType Element)
	{
		return Element.NavigationElement->GetHandle();
	}

	static bool Matches(KeyInitType A, KeyInitType B)
	{
		return A == B;
	}

	static uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key);
	}
};

// NavigationOctree 容器控制器：所有 Octree 操作的真实宿主。
struct FNavigationOctreeController
{
	// 一次更新的语义标志位（位运算组合）。
	// Default 按 FNavigationElement 自带的 DirtyFlag 决定脏类型；
	// Geometry/Modifiers 强制按几何或修饰符重建；
	// Refresh 表示这是一次刷新（不清空 Pending）；
	// ParentChain 表示这次是沿父子链的递归更新，不能移除节点。
	enum EOctreeUpdateMode
	{
		OctreeUpdate_Default = 0,						// regular update, mark dirty areas depending on exported content
		OctreeUpdate_Geometry = 1,						// full update, mark dirty areas for geometry rebuild
		OctreeUpdate_Modifiers = 2,						// quick update, mark dirty areas for modifier rebuild
		OctreeUpdate_Refresh = 4,						// update is used for refresh, don't invalidate pending queue
		OctreeUpdate_ParentChain = 8,					// update child nodes, don't remove anything
	};

	UE_DEPRECATED(5.5, "Use PendingUpdates instead.")
	TSet<FNavigationDirtyElement> PendingOctreeUpdates;
	// 帧内累积的"待处理元素注册/更新"。会由 FNavigationDataHandler::ProcessPendingOctreeUpdates 逐个出队并写进 NavOctree。
	TSet<FNavigationDirtyElement, FNavigationDirtyElementKeyFunctions> PendingUpdates;
	// 底层 Octree 共享指针。Shared 原因：异步 Tile 生成可能在工作线程只读访问 Octree 快照。
	TSharedPtr<FNavigationOctree, ESPMode::ThreadSafe> NavOctree;

	UE_DEPRECATED(5.5, "This container is no longer used. Use AddChild/RemoveChild/GetChildren methods instead.")
	TMultiMap<UObject*, FWeakObjectPtr> OctreeChildNodesMap;

	/** if set, navoctree updates are ignored, use with caution! */
	// 被置为 true 时 AddElementToNavOctree 会直接跳过，用于构建锁定期间。
	uint8 bNavOctreeLock : 1 = false;

	inline void SetNavigationOctreeLock(bool bLock);

	// 查询 PendingUpdates 是否已经有该 Handle 的一条记录
	NAVIGATIONSYSTEM_API bool HasPendingUpdateForElement(FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use HasPendingUpdateForElement instead.")
	NAVIGATIONSYSTEM_API bool HasPendingObjectNavOctreeId(UObject& Object) const;

	// 真正的节点移除入口（同时清理 ElementToOctreeId 反查表）
	inline void RemoveNode(FOctreeElementId2 ElementId, FNavigationElementHandle GetHandle);

	UE_DEPRECATED(5.5, "Use RemoveNode instead.")
	NAVIGATIONSYSTEM_API void RemoveObjectsNavOctreeId(const UObject& Object);

	// 切换 Octree 的几何存储策略（是否随元素一起落盘几何顶点）
	NAVIGATIONSYSTEM_API void SetNavigableGeometryStoringMode(FNavigationOctree::ENavGeometryStoringMode NavGeometryMode);

	// 完全重置：丢弃 Octree 与 PendingUpdates，保留 bNavOctreeLock 之外的控制位
	NAVIGATIONSYSTEM_API void Reset();

	inline const FNavigationOctree* GetOctree() const;
	inline FNavigationOctree* GetMutableOctree();

	// 反查：根据元素 Handle 找到它在 Octree 里的节点 id
	inline const FOctreeElementId2* GetNavOctreeIdForElement(FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use GetNavOctreeIdForElement instead.")
	NAVIGATIONSYSTEM_API const FOctreeElementId2* GetObjectsNavOctreeId(const UObject& Object) const;

	// 批量读一个元素的脏位 + 最新 Bounds（用于产生 DirtyArea）
	NAVIGATIONSYSTEM_API bool GetNavOctreeElementData(FNavigationElementHandle Element, ENavigationDirtyFlag& DirtyFlags, FBox& DirtyBounds);
	UE_DEPRECATED(5.5, "Use the version taking ENavigationDirtyFlag& and FNavigationElementHandle as parameter instead.")
	NAVIGATIONSYSTEM_API bool GetNavOctreeElementData(const UObject& NodeOwner, int32& DirtyFlags, FBox& DirtyBounds);

	// 常量访问元素的 RelevantData（几何/Modifier 数据块）
	NAVIGATIONSYSTEM_API const FNavigationRelevantData* GetDataForElement(FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use GetDataForElement instead.")
	NAVIGATIONSYSTEM_API const FNavigationRelevantData* GetDataForObject(const UObject& Object) const;

	// 可变访问：只用于在"不动 Bounds"的前提下替换 Area、Modifier 等小改
	NAVIGATIONSYSTEM_API FNavigationRelevantData* GetMutableDataForElement(FNavigationElementHandle Element);
	UE_DEPRECATED(5.5, "Use GetMutableDataForElement instead.")
	NAVIGATIONSYSTEM_API FNavigationRelevantData* GetMutableDataForObject(const UObject& Object);

	inline bool HasElementNavOctreeId(const FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use HasElementNavOctreeId instead.")
	NAVIGATIONSYSTEM_API bool HasObjectsNavOctreeId(const UObject& Object) const;

	inline bool IsNavigationOctreeLocked() const;

	/** basically says if navoctree has been created already */
	bool IsValid() const { return NavOctree.IsValid(); }

	inline bool IsValidElement(const FOctreeElementId2* ElementId) const;
	inline bool IsValidElement(const FOctreeElementId2& ElementId) const;

	bool IsEmpty() const
	{
		return (IsValid() == false) || NavOctree->GetSizeBytes() == 0;
	}

	// Parent/Child 链维护：Parent 变更/移动时可以通过这张表找到所有附着其上的 Child 一并刷新
	inline void AddChild(FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child);
	inline void RemoveChild(FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child);
	inline void GetChildren(FNavigationElementHandle Parent, TArray<const TSharedRef<const FNavigationElement>>& OutChildren) const;

private:
	UE_DEPRECATED(5.5, "This method will no be longer be used by the navigation system.")
	static uint32 HashObject(const UObject& Object);

	/** Map of all elements that are tied to indexed navigation parent */
	// 记录每个 Parent Handle 下挂了哪些子 FNavigationElement。MultiMap 允许一个 Parent 对应多个 Child。
	TMultiMap<FNavigationElementHandle, const TSharedRef<const FNavigationElement>> OctreeParentChildNodesMap;
};

//----------------------------------------------------------------------//
// inlines
//----------------------------------------------------------------------//

// deprecated
inline uint32 FNavigationOctreeController::HashObject(const UObject& Object)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FNavigationOctree::HashObject(Object);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

inline const FOctreeElementId2* FNavigationOctreeController::GetNavOctreeIdForElement(const FNavigationElementHandle Element) const
{ 
	return NavOctree.IsValid()
		? NavOctree->ElementToOctreeId.Find(Element)
		: nullptr;
}

inline bool FNavigationOctreeController::HasElementNavOctreeId(const FNavigationElementHandle Element) const
{
	return NavOctree.IsValid() && (NavOctree->ElementToOctreeId.Find(Element) != nullptr);
}

inline void FNavigationOctreeController::RemoveNode(const FOctreeElementId2 ElementId, const FNavigationElementHandle ElementHandle)
{ 
	if (NavOctree.IsValid())
	{
		NavOctree->RemoveNode(ElementId);
		NavOctree->ElementToOctreeId.Remove(ElementHandle);
	}
}

inline const FNavigationOctree* FNavigationOctreeController::GetOctree() const
{ 
	return NavOctree.Get(); 
}

inline FNavigationOctree* FNavigationOctreeController::GetMutableOctree()
{ 
	return NavOctree.Get(); 
}

inline bool FNavigationOctreeController::IsNavigationOctreeLocked() const
{ 
	return bNavOctreeLock; 
}

inline void FNavigationOctreeController::SetNavigationOctreeLock(bool bLock) 
{ 
	bNavOctreeLock = bLock; 
}

inline bool FNavigationOctreeController::IsValidElement(const FOctreeElementId2* ElementId) const
{
	return ElementId && IsValidElement(*ElementId);
}

inline bool FNavigationOctreeController::IsValidElement(const FOctreeElementId2& ElementId) const
{
	return IsValid() && NavOctree->IsValidElementId(ElementId);
}

void FNavigationOctreeController::AddChild(const FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child)
{
	OctreeParentChildNodesMap.AddUnique(Parent, Child);
}

void FNavigationOctreeController::RemoveChild(const FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child)
{
	OctreeParentChildNodesMap.RemoveSingle(Parent, Child);
}

void FNavigationOctreeController::GetChildren(const FNavigationElementHandle Parent, TArray<const TSharedRef<const FNavigationElement>>& OutChildren) const
{
	OctreeParentChildNodesMap.MultiFind(Parent, OutChildren);
}
