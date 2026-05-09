// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// NavigationOctreeController.cpp —— FNavigationOctreeController 的非 inline 成员实现。
// 主要是：
//   - Reset：销毁 Octree、清空 PendingUpdates；
//   - HasPendingUpdateForElement：询问 Pending 集合是否包含某 Handle；
//   - GetNavOctreeElementData / GetDataForElement / GetMutableDataForElement：
//     以 Handle 为 Key 的几组访问器；
//   - 5.5 之前基于 UObject 的旧 API 全部转调为 FNavigationElementHandle 版本。
// ============================================================================

#include "NavigationOctreeController.h"
#include "NavigationSystem.h"


//----------------------------------------------------------------------//
// FNavigationOctreeController
//----------------------------------------------------------------------//
// 整体复位：销毁底层 Octree 并清空 Pending 集合。保留一些控制位（如 bNavOctreeLock）。
// 调用者：UNavigationSystemV1 在 World 关闭/切换时；在 ConditionalPopulateNavOctree 前。
void FNavigationOctreeController::Reset()
{
	if (NavOctree.IsValid())
	{
		NavOctree->Destroy();
		NavOctree = nullptr;
	}
	PendingUpdates.Empty(32);
}

// 查询 Pending 集合（以 Handle 为 Key）是否已经登记了该元素的待处理更新。
bool FNavigationOctreeController::HasPendingUpdateForElement(const FNavigationElementHandle Element) const
{ 
	return PendingUpdates.Contains(Element);
}

// 切换是否在 Octree 中保存几何顶点。FNavigationOctree::bGatherGeometry 做下决策。
void FNavigationOctreeController::SetNavigableGeometryStoringMode(FNavigationOctree::ENavGeometryStoringMode NavGeometryMode)
{
	check(NavOctree.IsValid());
	NavOctree->SetNavigableGeometryStoringMode(NavGeometryMode);
}

// 读取某元素当前在 Octree 里记录的 DirtyFlags + Bounds。
// 调用者：FNavigationDataHandler::RemoveFromNavOctree 需要用这两项生成 DirtyArea。
bool FNavigationOctreeController::GetNavOctreeElementData(const FNavigationElementHandle Element, ENavigationDirtyFlag& OutDirtyFlags, FBox& OutDirtyBounds)
{
	const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(Element);
	if (ElementId != nullptr && IsValidElement(*ElementId))
	{
		// mark area occupied by given actor as dirty
		const FNavigationOctreeElement& ElementData = NavOctree->GetElementById(*ElementId);
		OutDirtyFlags = ElementData.Data->GetDirtyFlag();
		OutDirtyBounds = ElementData.Bounds.GetBox();
		return true;
	}

	return false;
}

// Deprecated
bool FNavigationOctreeController::GetNavOctreeElementData(const UObject& NodeOwner, int32& DirtyFlags, FBox& DirtyBounds)
{
	ENavigationDirtyFlag TmpDirtyFlags = ENavigationDirtyFlag::None;
	const bool bSuccess = GetNavOctreeElementData(FNavigationElementHandle(&NodeOwner), TmpDirtyFlags, DirtyBounds);
	DirtyFlags = static_cast<int32>(TmpDirtyFlags);
	return bSuccess;
}

// Deprecated
const FNavigationRelevantData* FNavigationOctreeController::GetDataForObject(const UObject& Object) const
{
	return GetDataForElement(FNavigationElementHandle(&Object));
}

// 常量访问：Handle → ElementId → RelevantData。中间任何一步失败返回 nullptr。
const FNavigationRelevantData* FNavigationOctreeController::GetDataForElement(const FNavigationElementHandle Element) const
{
	if (const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(Element); IsValidElement(ElementId))
	{
		return NavOctree->GetDataForID(*ElementId);
	}

	return nullptr;
}

// Deprecated
FNavigationRelevantData* FNavigationOctreeController::GetMutableDataForObject(const UObject& Object)
{
	return GetMutableDataForElement(FNavigationElementHandle(&Object));
}

// 可变访问：使用场景极窄——仅用于"不改 Bounds，只改 Modifier/Area" 的原地小改。
// 如果要改 Bounds，应走 UpdateNode / UpdateNavOctreeElementBounds 路径。
FNavigationRelevantData* FNavigationOctreeController::GetMutableDataForElement(const FNavigationElementHandle Element)
{
	if (const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(Element); IsValidElement(ElementId))
	{
		return NavOctree->GetMutableDataForID(*ElementId);
	}

	return nullptr;
}

//----------------------------------------------------------------------//
// Deprecated methods
//----------------------------------------------------------------------//

PRAGMA_DISABLE_DEPRECATION_WARNINGS

// Deprecated
bool FNavigationOctreeController::HasPendingObjectNavOctreeId(UObject& Object) const
{
	return HasPendingUpdateForElement(FNavigationElementHandle(&Object));
}

// Deprecated
bool FNavigationOctreeController::HasObjectsNavOctreeId(const UObject& Object) const
{
	return HasElementNavOctreeId(FNavigationElementHandle(&Object));
}

// Deprecated
const FOctreeElementId2* FNavigationOctreeController::GetObjectsNavOctreeId(const UObject& Object) const
{
	return GetNavOctreeIdForElement(FNavigationElementHandle(&Object));
}

// Deprecated：直接由 Object 移除节点（同时清 ElementToOctreeId）。
// 新代码请用 RemoveNode(ElementId, Handle)。
void FNavigationOctreeController::RemoveObjectsNavOctreeId(const UObject& Object)
{
	const FNavigationElementHandle ElementHandle(&Object);
	if (const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(ElementHandle); IsValidElement(ElementId))
	{
		RemoveNode(*ElementId, ElementHandle);
	}	
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS