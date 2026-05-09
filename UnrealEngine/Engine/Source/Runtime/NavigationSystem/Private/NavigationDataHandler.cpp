// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationDataHandler.cpp —— FNavigationDataHandler 的全部实现
// -----------------------------------------------------------------------------
// 本文件是"Octree + DirtyAreas"统一门面的具体实现。所有对 NavOctree 的
// 增/删/改都汇聚在此，保证 Octree 与 DirtyAreasController 两侧状态严格一致。
//
// 主要函数与执行流程：
//   ConstructNavOctree                 —— 初次分配 FNavigationOctree 并配置采集模式
//   RegisterElementWithNavOctree       —— 把新元素塞进 PendingUpdates（不立刻进 Octree）
//   AddElementToNavOctree              —— 把 Pending 元素真正写进 Octree + 下发 DirtyArea
//   UnregisterElementWithNavOctree     —— 若在 Pending 则 invalidate；若已在 Octree 则摘除
//   UpdateNavOctreeElement             —— "先 Unregister 再 Register"的替换模式
//   UpdateNavOctreeElementBounds       —— 只搬 Bounds、不重新采集几何的快路径
//   UpdateNavOctreeParentChain         —— NavigationParent 父子链的级联刷新
//   AddLevelCollisionToOctree / Remove —— 关卡级静态碰撞批量进/出
//   ReplaceAreaInOctreeData            —— 运行时换 Area 不重建几何（门/陷阱）
//   ProcessPendingOctreeUpdates        —— 每帧 Tick 调一次，把 PendingUpdates 全部落地
//   DemandLazyDataGathering            —— Tile 构建时按需触发一次延迟几何采集
//
// 架构文档参考：第 4.2 节"Navigation Octree 更新路径"。
// =============================================================================

#include "NavigationDataHandler.h"
#include "AI/Navigation/NavigationDirtyElement.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "NavAreas/NavArea.h"
#include "NavMesh/RecastGeometryExport.h"
#include "VisualLogger/VisualLogger.h"

#if WITH_RECAST
#include "DetourCrowd/DetourCrowd.h"
#endif // WITH_RECAST

DEFINE_LOG_CATEGORY_STATIC(LogNavOctree, Warning, All);

namespace UE::NavigationHelper::Private
{
	// 根据 UpdateFlags 位选择脏标志：
	//   OctreeUpdate_Geometry   -> ENavigationDirtyFlag::All（几何变了，Tile 全量刷）
	//   OctreeUpdate_Modifiers  -> ENavigationDirtyFlag::DynamicModifier（仅 Area/Modifier 刷）
	//   其它                    -> DefaultValue（由调用者/元素自身决定）
	ENavigationDirtyFlag GetDirtyFlag(const int32 UpdateFlags, const ENavigationDirtyFlag DefaultValue)
	{
		return ((UpdateFlags & FNavigationOctreeController::OctreeUpdate_Geometry) != 0) ? ENavigationDirtyFlag::All :
			((UpdateFlags & FNavigationOctreeController::OctreeUpdate_Modifiers) != 0) ? ENavigationDirtyFlag::DynamicModifier :
			DefaultValue;
	}
}
	
FNavigationDataHandler::FNavigationDataHandler(FNavigationOctreeController& InOctreeController, FNavigationDirtyAreasController& InDirtyAreasController)
		: OctreeController(InOctreeController), DirtyAreasController(InDirtyAreasController)
{}

// 初次构造 NavOctree：创建包含整个世界的根立方体（以 Origin 为中心、Radius 为半径）。
// 每个 NavigationSystem 只调一次（或在 Reset 之后再调），之后一切插入都走 AddNode/AppendToNode。
// - DataGatheringMode：控制"元素被插入时是立刻采集几何还是 lazy 延后到 Tile 构建时"。
// - GatheringNavModifiersWarningLimitTime：非 Shipping 下用于性能警告阈值。
void FNavigationDataHandler::ConstructNavOctree(const FVector& Origin, const double Radius, const ENavDataGatheringModeConfig DataGatheringMode, const float GatheringNavModifiersWarningLimitTime)
{
	UE_LOG(LogNavOctree, Log, TEXT("CREATE (Origin:%s Radius:%.2f)"), *Origin.ToString(), Radius);

	OctreeController.Reset();
	OctreeController.NavOctree = MakeShareable(new FNavigationOctree(Origin, Radius));
	OctreeController.NavOctree->SetDataGatheringMode(DataGatheringMode);
#if !UE_BUILD_SHIPPING
	OctreeController.NavOctree->SetGatheringNavModifiersTimeLimitWarning(GatheringNavModifiersWarningLimitTime);
#endif // !UE_BUILD_SHIPPING
}

// Deprecated
void FNavigationDataHandler::RemoveNavOctreeElementId(const FOctreeElementId2& ElementId, const int32 UpdateFlags)
{
	RemoveFromNavOctree(ElementId, UpdateFlags);
}

// 从 Octree 摘除一个节点，并把它原本覆盖的 Bounds 登记为 DirtyArea，
// 以便下一帧 NavData 重建该区域的 Tile。
// 调用者：RemoveLevelCollisionFromOctree / UnregisterElementWithNavOctree。
// 副作用：修改 OctreeController + 累计 DirtyAreasController（GameThread 预期）。
void FNavigationDataHandler::RemoveFromNavOctree(const FOctreeElementId2& ElementId, const int32 UpdateFlags)
{
	if (ensure(OctreeController.IsValidElement(ElementId)))
	{
		const FNavigationOctreeElement& ElementData = OctreeController.NavOctree->GetElementById(ElementId);
		// mark area occupied by given element as dirty except if explicitly set to skip this default behavior
		// 常规元素：把它原本的 AABB 登记为脏区域（除非该元素显式声明"不需要脏"——比如纯装饰物）
		if (!ElementData.Data->bShouldSkipDirtyAreaOnAddOrRemove)
		{
			const ENavigationDirtyFlag DirtyFlag = UE::NavigationHelper::Private::GetDirtyFlag(UpdateFlags, ElementData.Data->GetDirtyFlag());
			DirtyAreasController.AddArea(
				ElementData.Bounds.GetBox(),
				DirtyFlag,
				[&ElementData]
				{
					return ElementData.Data->SourceElement;
				},
				/*DirtyElement*/nullptr,
				"Remove from navoctree");
		}

		// 真正从 Octree 树节点链表摘下；同时清理 ElementHandle→ElementId 的索引
		OctreeController.RemoveNode(ElementId, ElementData.Data.Get().SourceElement.Get().GetHandle());
	}
}

// Deprecated
FSetElementId FNavigationDataHandler::RegisterNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags)
{
	check(false);
	return RegisterElementWithNavOctree(FNavigationElement::CreateFromNavRelevantInterface(ElementInterface), UpdateFlags);
}

// RegisterElementWithNavOctree：把一个 FNavigationElement 放进 PendingUpdates。
// ——这里**不会立刻**修改 Octree 树节点。真正的 AddNode 发生在 ProcessPendingOctreeUpdates 里。
// 为什么要这么设计：
//   1) 一帧内可能同一元素被多次移动/更新，只需要最后一次结果进 Octree；
//   2) 大规模 SpawnActor 时批量延迟可以显著减少 Octree 写开销；
//   3) 统一 PendingUpdates 之后可以与 UpdateNavOctreeElementBounds 协同（快路径）。
// 返回值：Pending 集合的 SetElementId，便于调用方（UpdateNavOctreeElement）补上 Prev 信息。
FSetElementId FNavigationDataHandler::RegisterElementWithNavOctree(const TSharedRef<const FNavigationElement>& ElementRef, const int32 UpdateFlags)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_RegisterNavOctreeElement);

	FSetElementId SetId;
	const FNavigationElement& NavigationElement = ElementRef.Get();

	// Octree 还没创建（例如 Navigation 被整个禁用）：直接忽略，不会报错
	if (OctreeController.IsValid() == false)
	{
		UE_LOG(LogNavOctree, VeryVerbose, TEXT("IGNORE(%hs) %s: octree not created yet"),
			__FUNCTION__, *NavigationElement.GetPathName());
		return SetId;
	}

	// Octree 处于锁定期（如 Recast 构建在读）：此时不允许任何写入，忽略
	if (OctreeController.IsNavigationOctreeLocked())
	{
		UE_LOG(LogNavOctree, Log, TEXT("IGNORE(%hs) %s: navigation octree locked"),
			__FUNCTION__, *NavigationElement.GetPathName());
		return SetId;
	}

	UE_LOG(LogNavOctree, Log, TEXT("REG %s"), *NavigationElement.GetPathName());

	bool bCanAdd = false;
	// 有 NavigationParent：本元素不会单独进 Octree，而是"挂"到父节点下（AppendToNode）
	if (const TWeakObjectPtr<const UObject>& NavigationParent = NavigationElement.GetNavigationParent(); !NavigationParent.IsExplicitlyNull())
	{
		OctreeController.AddChild(FNavigationElementHandle(NavigationParent), ElementRef);
		bCanAdd = true;
	}
	else
	{
		// 无父节点：只有当它尚未在 Octree 里才需要加（避免重复插入）
		bCanAdd = (OctreeController.HasElementNavOctreeId(NavigationElement.GetHandle()) == false);
	}

	if (bCanAdd)
	{
		FNavigationDirtyElement UpdateInfo(ElementRef, UE::NavigationHelper::Private::GetDirtyFlag(UpdateFlags, ENavigationDirtyFlag::None), DirtyAreasController.bUseWorldPartitionedDynamicMode);

		// 同一 Handle 可能在本帧内多次注册；这里做合并：保留前一次累计的 ExplicitAreasToDirty
		SetId = OctreeController.PendingUpdates.FindId(NavigationElement.GetHandle());
		if (SetId.IsValidId())
		{
			// make sure this request stays, in case it has been invalidated already and keep any dirty areas
			UpdateInfo.ExplicitAreasToDirty = OctreeController.PendingUpdates[SetId].ExplicitAreasToDirty;
			OctreeController.PendingUpdates[SetId] = UpdateInfo;
		}
		else
		{
			SetId = OctreeController.PendingUpdates.Add(UpdateInfo);
		}
	}

	return SetId;
}

// AddElementToNavOctree：把**单个** FNavigationDirtyElement 真正塞进 Octree，并派发 DirtyArea。
// 仅由 ProcessPendingOctreeUpdates（每帧）循环调用；以及 NavigationParent 链里"先补父、再处理子"时递归调用。
// 副作用：
//   - OctreeController.NavOctree 新增节点
//   - DirtyAreasController 追加若干脏区
//   - 可能触发元素的 GeometryExportDelegate 做惰性几何采集（在 FNavigationOctree::AddNode 内部）
void FNavigationDataHandler::AddElementToNavOctree(const FNavigationDirtyElement& DirtyElement)
{
	check(OctreeController.IsValid());
	LLM_SCOPE_BYTAG(NavigationOctree);

	// handle invalidated requests first
	// 已被 Unregister 标为 invalid：不插入 Octree，但仍需把它之前覆盖的区域标脏（保证该区域会重算）
	if (DirtyElement.bInvalidRequest)
	{
		if (DirtyElement.bHasPrevData)
		{
			DirtyAreasController.AddArea(DirtyElement.PrevBounds,
				DirtyElement.PrevFlags,
				[&DirtyElement]
				{
					return DirtyElement.NavigationElement;
				},
				&DirtyElement,
				"Addition to navoctree (invalid request)");
		}

		return;
	}

	FNavigationOctreeElement OctreeElement(DirtyElement.NavigationElement);
	const FNavigationElement& NavigationElement = DirtyElement.NavigationElement.Get();

	const TWeakObjectPtr<const UObject> ElementWeakUObject = NavigationElement.GetWeakUObject();
	if (!ElementWeakUObject.IsExplicitlyNull())
	{
		UE_VLOG_UELOG(ElementWeakUObject.Get(), LogNavOctree, Verbose, TEXT("Create FNavigationOctreeElement for %s"),
			*NavigationElement.GetPathName());
	}

	// In WP dynamic mode, store if this is loaded data.
	// WorldPartition 动态模式：标记本节点是否由"流式加载"引入（决定后续 Tile 重建策略）
	if (DirtyAreasController.bUseWorldPartitionedDynamicMode)
	{
		OctreeElement.Data->bLoadedData = DirtyElement.bIsFromVisibilityChange || NavigationElement.IsFromLevelVisibilityChange();
	}

	const FBox ElementBounds = NavigationElement.GetBounds();

	// 分支 A：本元素挂了 NavigationParent —— 需要作为 child 追加到父 Octree 节点上
	if (const TWeakObjectPtr<const UObject>& NavigationParent = NavigationElement.GetNavigationParent(); !NavigationParent.IsExplicitlyNull())
	{
		const FNavigationElementHandle ParentKey(NavigationParent);

		// check if parent node is waiting in queue
		// 父节点可能仍在 PendingUpdates 里（尚未进 Octree）：此时先递归处理父，
		// 保证子能找到合法的 ParentId
		const FSetElementId ParentRequestId = OctreeController.PendingUpdates.FindId(ParentKey);
		const FOctreeElementId2* ParentId = OctreeController.GetNavOctreeIdForElement(ParentKey);
		if (ParentRequestId.IsValidId() && ParentId == nullptr)
		{
			FNavigationDirtyElement& ParentDirtyElement = OctreeController.PendingUpdates[ParentRequestId];
			AddElementToNavOctree(ParentDirtyElement);

			// mark as invalid so it won't be processed twice
			// 标脏防止 ProcessPendingOctreeUpdates 主循环再次处理该父
			ParentDirtyElement.bInvalidRequest = true;
		}

		const FOctreeElementId2* ElementId = ParentId ? ParentId : OctreeController.GetNavOctreeIdForElement(ParentKey);
		if (ElementId && ensure(OctreeController.IsValidElement(*ElementId)))
		{
			UE_LOG(LogNavOctree, Log, TEXT("ADD %s to %s"), *NavigationElement.GetPathName(), *GetNameSafe(NavigationParent.Get()));
			// 把 child 的几何/修饰合并进父节点（共享同一个 ElementId）
			OctreeController.NavOctree->AppendToNode(*ElementId, DirtyElement.NavigationElement, ElementBounds, OctreeElement);
		}
		else
		{
			// 父节点不存在——通常是父 Actor 自己不参与导航（不实现 INavRelevantInterface），
			// 但仍被子组件引用作为 NavigationParent；这里仅警告，子数据会丢失
			UE_LOG(LogNavOctree, Warning, TEXT("Can't add node [%s] - parent [%s] not found in octree!"),
				*NavigationElement.GetPathName(),
				*GetNameSafe(NavigationParent.Get()));
		}
	}
	else
	{
		// 分支 B：无父节点 —— 作为独立节点插入 Octree。
		// AddNode 内部会调用 NavigationElement 的 GeometryExportDelegate 执行几何采集（或延迟标记）
		OctreeController.NavOctree->AddNode(ElementBounds, OctreeElement);
		UE_SUPPRESS(LogNavOctree, Verbose,
			{
				const FOctreeElementId2* ElementId = OctreeController.GetNavOctreeIdForElement(DirtyElement.NavigationElement->GetHandle());
				UE_VLOG_UELOG(NavigationElement.GetWeakUObject().Get(), LogNavOctree, Log, TEXT("ADD %s - %s"),
					*NavigationElement.GetPathName(),
					ElementId ? *LexToString(*ElementId) : TEXT("No element"));
			});
	}

	// mark area occupied by given element as dirty except if explicitly set to skip this default behavior
	// 挑选脏标志：调用方有 Override 用 Override，否则用 Element 自身的 DirtyFlag
	const ENavigationDirtyFlag DirtyFlag = DirtyElement.FlagsOverride != ENavigationDirtyFlag::None ? DirtyElement.FlagsOverride : OctreeElement.Data->GetDirtyFlag();
	if (OctreeElement.Data->bShouldSkipDirtyAreaOnAddOrRemove)
	{
		// Skip 模式：元素自己声明"我不需要全 AABB 标脏"，但可能给了若干显式的 ExplicitAreasToDirty
		if (DirtyElement.ExplicitAreasToDirty.Num() > 0)
		{
			DirtyAreasController.AddAreas(
				DirtyElement.ExplicitAreasToDirty,
				DirtyFlag, 
				[ElementOwner = DirtyElement.NavigationElement]
				{
					return ElementOwner;
				},
				&DirtyElement,
				"Addition to navoctree");
		}
	}
	else if (!OctreeElement.IsEmpty())
	{
		// 常规：用插入后计算出的 Octree 节点 AABB 作为脏区
		DirtyAreasController.AddArea(
			OctreeElement.Bounds.GetBox(),
			DirtyFlag,
			[ElementOwner = DirtyElement.NavigationElement]
			{
				return ElementOwner;
			},
			&DirtyElement,
			"Addition to navoctree");
	}
}

// Deprecated
bool FNavigationDataHandler::UnregisterNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags)
{
	return UnregisterElementWithNavOctree(FNavigationElement::CreateFromNavRelevantInterface(ElementInterface), UpdateFlags);
}

// UnregisterElementWithNavOctree：注销流程。两种情形：
//   1) 元素已经进了 Octree → 调用 RemoveFromNavOctree 做标准移除 + 标脏
//   2) 元素只在 PendingUpdates 里（还没真正进 Octree）→ 把该 Pending 条目标记为 invalid，
//      AddElementToNavOctree 会在检测到 invalid 时只做 AABB 标脏，不会再插入 Octree。
// 当元素有 NavigationParent 且本次不是 ParentChain 级联（避免循环）时，
// 还要从父节点 Children 列表里摘掉自己，并触发父节点的级联刷新以重建父的 OctreeElement。
// 返回值：true 仅当"原本确实存在于 Octree 或有合法 Pending 请求"——用于告诉上层是否做了实质工作。
bool FNavigationDataHandler::UnregisterElementWithNavOctree(const TSharedRef<const FNavigationElement>& ElementRef, const int32 UpdateFlags)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_UnregisterNavOctreeElement);

	const FNavigationElement& NavRelevantElement = ElementRef.Get();
	if (OctreeController.IsValid() == false)
	{
		UE_LOG(LogNavOctree, VeryVerbose, TEXT("IGNORE(%hs) %s: octree not created yet"),
			__FUNCTION__, *NavRelevantElement.GetPathName());
		return false;
	}

	if (OctreeController.IsNavigationOctreeLocked())
	{
		UE_LOG(LogNavOctree, Log, TEXT("IGNORE(%hs) %s: octree locked"),
			__FUNCTION__, *ElementRef.Get().GetPathName());
		return false;
	}

	const FNavigationElementHandle NavRelevantElementHandle = NavRelevantElement.GetHandle();

	const FOctreeElementId2* OctreeElementId = OctreeController.GetNavOctreeIdForElement(NavRelevantElementHandle);
	UE_VLOG_UELOG(NavRelevantElement.GetWeakUObject().Get(), LogNavOctree, Log, TEXT("UNREG %s %s"),
		*NavRelevantElement.GetPathName(),
		OctreeElementId ? *FString::Printf(TEXT("[exists %s]"), *LexToString(*OctreeElementId)) : TEXT("[doesn\'t exist]"));

	bool bUnregistered = false;

	// 情形 1：在 Octree 里 —— 标准移除
	if (OctreeElementId != nullptr)
	{
		RemoveFromNavOctree(*OctreeElementId, UpdateFlags);
		bUnregistered = true;
	}
	// 情形 1.5：不在 Octree，但可能是 NavigationParent 的 child；且非 ParentChain 级联时才处理
	else if (const bool bCanRemoveChildNode = (UpdateFlags & FNavigationOctreeController::OctreeUpdate_ParentChain) == 0)
	{
		if (const TWeakObjectPtr<const UObject>& NavigationParent = NavRelevantElement.GetNavigationParent(); !NavigationParent.IsExplicitlyNull())
		{
			// if node has navigation parent (= doesn't exist in octree on its own)
			// and it's not part of parent chain update
			// remove it from map and force update on parent to rebuild octree element
			// 从父的 children 列表摘除后，强制父级联刷新（父本身需要 re-gather，因为它的几何聚合里少了本 child）
			const FNavigationElementHandle ParentKey(NavigationParent);
			OctreeController.RemoveChild(ParentKey, ElementRef);

			if (const FNavigationRelevantData* NavigationData = OctreeController.GetDataForElement(ParentKey))
			{
				UpdateNavOctreeParentChain(NavigationData->SourceElement);
			}
		}
	}

	// mark pending update as invalid, it will be dirtied according to currently active settings
	// 情形 2：命中 PendingUpdates 中的项 —— 标记 invalid（非 Refresh 模式时，Refresh 模式不 invalidate，
	// 因为后续 Register 会直接覆写这条 Pending）
	if (const bool bCanInvalidateQueue = (UpdateFlags & FNavigationOctreeController::OctreeUpdate_Refresh) == 0)
	{
		const FSetElementId RequestId = OctreeController.PendingUpdates.FindId(NavRelevantElementHandle);
		if (RequestId.IsValidId())
		{
			FNavigationDirtyElement& DirtyElement = OctreeController.PendingUpdates[RequestId];

			// Only consider as unregistered when pending update was not already invalidated since return value must indicate
			// that ElementOwner was fully added or about to be added (valid pending update).
			bUnregistered |= (DirtyElement.bInvalidRequest == false);

			DirtyElement.bInvalidRequest = true;
		}
	}

	return bUnregistered;
}

// Deprecated
void FNavigationDataHandler::UpdateNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags)
{
	UpdateNavOctreeElement(FNavigationElementHandle(&ElementOwner), FNavigationElement::CreateFromNavRelevantInterface(ElementInterface), UpdateFlags);
}

// UpdateNavOctreeElement：同一 ElementHandle 下"换身体"的标准更新流程。
// 经典模式：Unregister 旧 → Register 新 → 把旧的 Bounds/Flags 写回到 Pending 条目的 Prev* 字段。
// 为什么要记 Prev：若本帧后续又 Unregister，AddElementToNavOctree(invalidRequest=true) 时可以用
// PrevBounds/PrevFlags 标出"旧 AABB"的脏区，保证旧位置的 Tile 会被正确刷新。
// 注意：这里强制加上 OctreeUpdate_Refresh 位，让 Unregister 不把 Pending 条目 invalidate。
void FNavigationDataHandler::UpdateNavOctreeElement(FNavigationElementHandle ElementHandle, const TSharedRef<const FNavigationElement>& UpdatedElement, int32 UpdateFlags)
{
	INC_DWORD_STAT(STAT_Navigation_UpdateNavOctree);

	if (OctreeController.IsValid() == false)
	{
		UE_LOG(LogNavOctree, VeryVerbose, TEXT("IGNORE(%hs) %s: octree not created yet"),
			__FUNCTION__, *UpdatedElement.Get().GetPathName());
		return;
	}

	if (OctreeController.IsNavigationOctreeLocked())
	{
		UE_LOG(LogNavOctree, Log, TEXT("IGNORE(%hs) %s: octree locked"),
			__FUNCTION__, *UpdatedElement.Get().GetPathName());
		return;
	}

	// grab existing octree data
	FBox CurrentBounds;
	ENavigationDirtyFlag CurrentFlags;
	const bool bAlreadyExists = OctreeController.GetNavOctreeElementData(ElementHandle, CurrentFlags, CurrentBounds);

	// don't invalidate pending requests
	UpdateFlags |= FNavigationOctreeController::OctreeUpdate_Refresh;

	// Use local shared reference to make sure to keep element alive to register back
	// since unregistering might remove the only reference.
	TSharedRef<const FNavigationElement> LocalElementRef = UpdatedElement;

	// Always try to unregister, even if element owner doesn't exist in octree (parent nodes).
	// This is also why we need to provide the new element and not only the handle, so we can access the parent (expected to be always the same for an update).
	UnregisterElementWithNavOctree(LocalElementRef, UpdateFlags);

	const FSetElementId RequestId = RegisterElementWithNavOctree(LocalElementRef, UpdateFlags);

	// add original data to pending registration request
	// so it could be dirtied properly when system receive unregister request while actor is still queued
	if (RequestId.IsValidId())
	{
		FNavigationDirtyElement& UpdateInfo = OctreeController.PendingUpdates[RequestId];
		UpdateInfo.PrevFlags = CurrentFlags;
		if (UpdateInfo.PrevBounds.IsValid)
		{
			// If we have something stored already we want to 
			// sum it up, since we care about the whole bounding
			// box of changes that potentially took place
			UpdateInfo.PrevBounds += CurrentBounds;
		}
		else
		{
			UpdateInfo.PrevBounds = CurrentBounds;
		}
		UpdateInfo.bHasPrevData = bAlreadyExists;
	}

	UpdateNavOctreeParentChain(UpdatedElement, /*bSkipElementOwnerUpdate=*/ true);
}

// Deprecated
void FNavigationDataHandler::UpdateNavOctreeParentChain(UObject& ElementOwner, bool bSkipElementOwnerUpdate)
{
	if (const INavRelevantInterface* NavRelevantInterface = Cast<INavRelevantInterface>(&ElementOwner))
	{
		UpdateNavOctreeParentChain(FNavigationElement::CreateFromNavRelevantInterface(*NavRelevantInterface), bSkipElementOwnerUpdate);
	}
}

// UpdateNavOctreeParentChain：父子链级联刷新核心实现（私有版）。
// 设计目标：当 Parent 自身发生变更时，挂在它下面的 children 需要"先全部脱离父、再把父重建、再把 children 重新挂回"。
// 因为 Octree 里 child 是以 AppendToNode 融进父节点的，所以父重建时几何/修饰必须重新汇总。
//
// 参数 bSkipElementOwnerUpdate = true 的用法：
//   UpdateNavOctreeElement 已经先对 Element 自身做过一次 Unregister/Register，
//   这里的 ElementOwnerUpdateFunc 不应再做第二次；但 children 的 Unregister/Register 还是要走。
void FNavigationDataHandler::UpdateNavOctreeParentChain(const TSharedRef<const FNavigationElement>& Element, const bool bSkipElementOwnerUpdate)
{
	constexpr int32 UpdateFlags = FNavigationOctreeController::OctreeUpdate_ParentChain | FNavigationOctreeController::OctreeUpdate_Refresh;

	TArray<const TSharedRef<const FNavigationElement>> ChildNodes;
	OctreeController.GetChildren(Element->GetHandle(), ChildNodes);

	// 内部 lambda：把 Element 自身做一次 Unregister+Register。
	// 若 ElementOwner 原本就不在 Octree/Pending，就不做任何事（返回 false 表示 children 也不必重挂）。
	auto ElementOwnerUpdateFunc = [&]()->bool
	{
		bool bShouldRegisterChildren = true;
		if (bSkipElementOwnerUpdate == false)
		{
			// Use local shared reference to make sure to keep element alive to register back
			// since unregistering might remove the only reference.
			TSharedRef<const FNavigationElement> LocalElementRef = Element;

			// We don't want to register NavOctreeElement if owner was not already registered or queued
			// so we use Unregister/Register combo instead of UpdateNavOctreeElement
			if (UnregisterElementWithNavOctree(LocalElementRef, UpdateFlags))
			{
				const FSetElementId NewId = RegisterElementWithNavOctree(LocalElementRef, UpdateFlags);
				bShouldRegisterChildren = NewId.IsValidId();
			}
			else
			{
				bShouldRegisterChildren = false;
			}
		}
		return bShouldRegisterChildren;
	};

	if (ChildNodes.Num() == 0)
	{
		// Last child was removed, only need to rebuild owner's NavOctreeElement
		// 没有 children：只需重建父本身，快路径返回
		ElementOwnerUpdateFunc();
		return;
	}

	// 常规路径：先把全部 children Unregister → 重建父 → 再把 children 重新 Register 回去
	for (const TSharedRef<const FNavigationElement>& ChildNode : ChildNodes)
	{
		UnregisterElementWithNavOctree(ChildNode, UpdateFlags);
	}

	if (const bool bShouldRegisterChildren = ElementOwnerUpdateFunc())
	{
		for (const TSharedRef<const FNavigationElement>& ChildNode : ChildNodes)
		{
			RegisterElementWithNavOctree(ChildNode, UpdateFlags);
		}
	}
}

// Deprecated
bool FNavigationDataHandler::UpdateNavOctreeElementBounds(UObject& ElementOwner, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas)
{
	return UpdateNavOctreeElementBounds(FNavigationElementHandle(&ElementOwner), NewBounds, DirtyAreas);
}

// UpdateNavOctreeElementBounds：仅修改 AABB 的"快路径"。
// 适用场景：Actor/Component 平移了一点距离，但它贡献的几何和修饰都没变（例如 Component->SetWorldLocation）。
// 相比 UpdateNavOctreeElement（Unregister+Register，会触发一次完整的 GeometryExport）：
//   - 不重新采集几何（FNavigationRelevantData 保持不变）
//   - 只更新 Octree 树内节点所在的桶（NavOctree->UpdateNode）
//   - DirtyAreas 仅包含 old+new 两个 AABB 的并集
// 返回：是否真实完成了更新；元素既不在 Octree 也不在 Pending 时返回 false。
bool FNavigationDataHandler::UpdateNavOctreeElementBounds(const FNavigationElementHandle ElementHandle, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas)
{
	const FOctreeElementId2* ElementId = OctreeController.GetNavOctreeIdForElement(ElementHandle);
	// 情形 1：已经在 Octree 中 —— 用 UpdateNode 快速搬桶
	if (ElementId != nullptr && ensure(OctreeController.IsValidElement(*ElementId)))
	{
		OctreeController.NavOctree->UpdateNode(*ElementId, NewBounds);

		// Dirty areas
		if (DirtyAreas.Num() > 0)
		{
			// Refresh ElementId since object may be stored in a different node after updating bounds
			// UpdateNode 可能把元素搬到了另一个 Octree 桶里，ElementId 指针会失效，需重查
			ElementId = OctreeController.GetNavOctreeIdForElement(ElementHandle);
			if (ElementId != nullptr && ensure(OctreeController.IsValidElement(*ElementId)))
			{
				const FNavigationOctreeElement& ElementData = OctreeController.NavOctree->GetElementById(*ElementId);
				DirtyAreasController.AddAreas(
					DirtyAreas,
					ElementData.Data->GetDirtyFlag(),
					[SourceElement = ElementData.Data->SourceElement]
					{
						return SourceElement;
					},
					/*DirtyElement*/ nullptr,
					"Bounds change");
			}
		}

		return true;
	}

	// Update bounds and to append dirty areas to a pending update since the element is not added yet.
	// 情形 2：仍在 PendingUpdates 中（尚未进入 Octree）—— 直接修改 Pending 条目里的 NavigationElement 的 Bounds，
	// 并把脏区域合并到 ExplicitAreasToDirty；AddElementToNavOctree 后续会一次性应用。
	if (const FSetElementId PendingElementId = OctreeController.PendingUpdates.FindId(ElementHandle); PendingElementId.IsValidId())
	{
		if (FNavigationDirtyElement& DirtyElement = OctreeController.PendingUpdates[PendingElementId]; !DirtyElement.bInvalidRequest)
		{
			// 拷贝一份可写的 FNavigationElement（FNavigationDirtyElement 里原本持 const 引用）
			const TSharedRef<FNavigationElement> Updated = MakeShared<FNavigationElement>(DirtyElement.NavigationElement.Get());
			Updated->SetBounds(NewBounds);
			DirtyElement.NavigationElement = Updated;
			DirtyElement.ExplicitAreasToDirty.Append(DirtyAreas);
			return true;
		}
	}

	return false;
}

// 几何查询：收集落在 QueryBox 内、且通过 Filter 的所有 Octree 元素。主要给 Recast Tile 生成线程用。
void FNavigationDataHandler::FindElementsInNavOctree(const FBox& QueryBox, const FNavigationOctreeFilter& Filter, TArray<FNavigationOctreeElement>& Elements)
{
	if (OctreeController.IsValid() == false)
	{
		UE_LOG(LogNavOctree, Warning, TEXT("FNavigationDataHandler::FindElementsInNavOctree gets called while NavOctree is null"));
		return;
	}

	OctreeController.NavOctree->FindElementsWithBoundsTest(QueryBox, [&Elements, &Filter](const FNavigationOctreeElement& Element)
	{
		if (Element.IsMatchingFilter(Filter))
		{
			Elements.Add(Element);
		}
	});
}

// Deprecated
bool FNavigationDataHandler::ReplaceAreaInOctreeData(const UObject& Object, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool bReplaceChildClasses)
{
	return ReplaceAreaInOctreeData(FNavigationElementHandle(&Object), OldArea, NewArea, bReplaceChildClasses);
}

// ReplaceAreaInOctreeData：把 Octree 元素数据里所有"旧 Area 类"替换为"新 Area 类"，不重建几何。
// 关键用途：门/陷阱/可被玩家破坏的遮挡物——打开/关闭时只需要换 Area 就能让 Tile 的 cost 立刻变化，
// 省去一次昂贵的 Tile re-generate（因为几何没变）。
// 作用范围：Modifier 里的 Area、SimpleLink 中每条 Link / SegmentLink 的 AreaClass。
// 调用方若开启 bReplaceChildClasses，任何派生自 OldArea 的类都会被替换。
bool FNavigationDataHandler::ReplaceAreaInOctreeData(
	const FNavigationElementHandle Element,
	const TSubclassOf<UNavArea> OldArea,
	const TSubclassOf<UNavArea> NewArea,
	const bool bReplaceChildClasses) const
{
	FNavigationRelevantData* Data = OctreeController.GetMutableDataForElement(Element);

	if (Data == nullptr || Data->HasModifiers() == false)
	{
		return false;
	}

	// 遍历所有 AreaModifier（体积型 Modifier）
	for (FAreaNavModifier& AreaModifier : Data->Modifiers.GetMutableAreas())
	{
		if (AreaModifier.GetAreaClass() == OldArea
			|| (bReplaceChildClasses && AreaModifier.GetAreaClass()->IsChildOf(OldArea)))
		{
			AreaModifier.SetAreaClass(NewArea);
		}
	}
	// 遍历 SimpleLink：每个 SimpleLink 里又有若干 Link / SegmentLink，都要挨个检查
	for (FSimpleLinkNavModifier& SimpleLink : Data->Modifiers.GetSimpleLinks())
	{
		for (FNavigationLink& Link : SimpleLink.Links)
		{
			if (Link.GetAreaClass() == OldArea
				|| (bReplaceChildClasses && Link.GetAreaClass()->IsChildOf(OldArea)))
			{
				Link.SetAreaClass(NewArea);
			}
		}
		for (FNavigationSegmentLink& Link : SimpleLink.SegmentLinks)
		{
			if (Link.GetAreaClass() == OldArea
				|| (bReplaceChildClasses && Link.GetAreaClass()->IsChildOf(OldArea)))
			{
				Link.SetAreaClass(NewArea);
			}
		}
	}

	// CustomLink 路径还没实现；若有 CustomLink 正调用这个 API，得把实现补上（见 ensure 信息）
	ensureMsgf(Data->Modifiers.GetCustomLinks().IsEmpty(), TEXT("Not implemented yet"));

	return true;
}

// 关卡级静态碰撞一次性进 Octree：流式加载一个 ULevel 时调用一次。
// 仅 WITH_RECAST 且 Octree 存储模式为 StoreNavGeometry 才生效——没有 Recast 的构建不需要这些静态几何。
// 数据来源：ULevel::GetStaticNavigableGeometry() —— 这是 cook 时已离线生成好的几何 soup。
// 一次性把整个关卡的静态几何打包成一个 FNavigationElement（ID=INDEX_NONE 表示"关卡级"），
// 能显著降低 NavOctree 元素数（相比每个 StaticMesh 一个元素）。
void FNavigationDataHandler::AddLevelCollisionToOctree(ULevel& Level)
{
#if WITH_RECAST
	if (OctreeController.IsValid() &&
		OctreeController.NavOctree->GetNavGeometryStoringMode() == FNavigationOctree::StoreNavGeometry)
	{
		const FNavigationElementHandle ElementKey(&Level);
		const TArray<FVector>* LevelGeom = Level.GetStaticNavigableGeometry();
		const FOctreeElementId2* ElementId = OctreeController.GetNavOctreeIdForElement(ElementKey);

		if (!ElementId && LevelGeom && LevelGeom->Num() > 0)
		{
			TSharedRef<const FNavigationElement> NavigationElement = MakeShared<const FNavigationElement>(Level, INDEX_NONE);
			FNavigationOctreeElement BSPElem(NavigationElement);

			// In WP dynamic mode, store if this is loaded data.
			// WP 动态模式：若关卡正在做 visibility 请求，则打上 bLoadedData 标记
			if (DirtyAreasController.bUseWorldPartitionedDynamicMode)
			{
				BSPElem.Data->bLoadedData = Level.HasVisibilityChangeRequestPending();
			}
			
			// 把 VertexSoup 形式的关卡几何"导出"成 Recast 预期的 FNavigationRelevantData 几何缓冲
			FRecastGeometryExport::ExportVertexSoupGeometry(*LevelGeom, *BSPElem.Data);

			const FBox& Bounds = BSPElem.Data->Bounds;
			if (!Bounds.GetExtent().IsNearlyZero())
			{
				OctreeController.NavOctree->AddNode(Bounds, BSPElem);
				DirtyAreasController.AddArea(
					Bounds, 
					ENavigationDirtyFlag::All,
					[SourceElement = NavigationElement]
					{
						return SourceElement;
					},
					/*DirtyElement*/ nullptr,
					"Add level");

				UE_LOG(LogNavOctree, Log, TEXT("ADD %s"), *NavigationElement.Get().GetPathName());
			}
		}
	}
#endif// WITH_RECAST
}

// 关卡卸载：把之前 AddLevelCollisionToOctree 登记的关卡级节点移除。
// 使用 OctreeUpdate_Geometry 位，保证 DirtyFlag 被提升为 All（几何变化要 Tile 全量刷）。
void FNavigationDataHandler::RemoveLevelCollisionFromOctree(ULevel& Level)
{
	if (OctreeController.IsValid())
	{
		const FNavigationElementHandle NavigationElementHandle(&Level);
		if (const FOctreeElementId2* OctreeElementId = OctreeController.GetNavOctreeIdForElement(NavigationElementHandle))
		{
			UE_LOG(LogNavOctree, Log, TEXT("UNREG %s %s"), *Level.GetPathName(), OctreeElementId ? TEXT("[exists]") : TEXT(""));
			RemoveFromNavOctree(*OctreeElementId, FNavigationOctreeController::OctreeUpdate_Geometry);
		}
	}
}

// ProcessPendingOctreeUpdates：每帧 Tick 必调一次，把 PendingUpdates 集合的条目逐个落地进 Octree。
// 迭代陷阱：AddElementToNavOctree 会（经由 WaitUntilAsyncPropertyReleased / UpdateComponent /
// RegisterElementWithNavOctree）**再次写 PendingUpdates**，常规 range-for 会使迭代器失效。
// 历史 bug：旧实现就是这样导致跳过新插入条目甚至 UAF。因此这里用 "CreateIterator + RemoveCurrent"
// 的边迭代边移除写法，直到集合为空。
// 收尾用 Empty(32) 预分配下一帧的容量，避免频繁 realloc。
void FNavigationDataHandler::ProcessPendingOctreeUpdates()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Navigation_ProcessPendingOctreeUpdates);

	if (OctreeController.NavOctree)
	{
		// AddElementToNavOctree (through some of its resulting function calls) modifies PendingUpdates so invalidates the iterators,
		// (via WaitUntilAsyncPropertyReleased() / UpdateComponentInNavOctree() / RegisterElementWithNavOctree()). This means we can't iterate
		// through this set in the normal way. Previously the code iterated through this which also left us open to other potential bugs
		// in that we may have tried to modify elements we had already processed.
		while (TSet<FNavigationDirtyElement, FNavigationDirtyElementKeyFunctions>::TIterator It = OctreeController.PendingUpdates.CreateIterator())
		{
			// 先拷贝出 DirtyElement 再 RemoveCurrent，保证后续 AddElementToNavOctree 里即使
			// 再往 PendingUpdates 插入新条目也不会扰乱当前迭代。
			FNavigationDirtyElement DirtyElement = *It;
			It.RemoveCurrent();
			AddElementToNavOctree(DirtyElement);
		}
	}
	OctreeController.PendingUpdates.Empty(32);
}

// DemandLazyDataGathering：惰性模式下的"即刻补采集"。
// 当 Octree 构造时使用了 Lazy 数据采集策略（DataGatheringMode=Lazy），
// 元素进 Octree 时只登记 Bounds、并不做几何/Modifier 采集；
// 直到 Recast Tile 真的需要本元素时，由 Tile 生成器调用此函数按需补齐。
// 同时会递归地把 child 元素的 lazy modifier 也拉起（父 re-gather 时需要 child 的最新修饰）。
void FNavigationDataHandler::DemandLazyDataGathering(FNavigationRelevantData& ElementData)
{
	// Do the lazy gathering on the element
	OctreeController.NavOctree->DemandLazyDataGathering(ElementData);

    // Check if any child asked for some lazy gathering
	// 父触发了重新采集：需要把所有 children 的 lazy modifier 也一次性合并进来
	if (ElementData.IsPendingChildLazyModifiersGathering())
	{
		TArray<const TSharedRef<const FNavigationElement>> ChildNodes;
		OctreeController.GetChildren(ElementData.SourceElement->GetHandle(), ChildNodes);

		for (const TSharedRef<const FNavigationElement>& ChildNode : ChildNodes)
		{
			OctreeController.NavOctree->DemandChildLazyDataGathering(ElementData, ChildNode.Get());
		}
		ElementData.bPendingChildLazyModifiersGathering = false;
	}
}