// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// NavigationOctree.cpp —— FNavigationOctree / FNavigationOctreeSemantics 的实现。
// 关键要点：
//   1) AddNode / AppendToNode / UpdateNode / RemoveNode 都要保持
//      ElementToOctreeId 反查表与底层 TOctree2 节点的一一对应；
//   2) 节点插入时由 FNavigationOctreeSemantics::SetElementId 回调写入反查表，
//      所以 AddElement 一定要在 Element.Data 填充完毕后再调；
//   3) DemandLazyDataGathering 处理两类懒数据：Geometry 与 Modifier，
//      成功采集后 Shrink 回收多余容量，并同步 NodesMemory 计数；
//   4) !UE_BUILD_SHIPPING 下可用 console 开关
//      'ai.debug.nav.validateConsistencyWhenAddingOctreeNode' 校验
//      "FNavigationElement 入队时拍下的快照" 与 "真正入 Octree 时 NavRelevantInterface
//      返回的最新值" 是否一致（Bounds / Parent）——不一致通常意味着某处忘记 UpdateElement。
// ============================================================================

#include "NavigationOctree.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "AI/Navigation/NavigationElement.h"
#include "Interfaces/Interface_AsyncCompilation.h"
#include "NavigationSystem.h"
#include "UObject/Package.h"

LLM_DEFINE_TAG(NavigationOctree);

namespace UE::NavigationOctree::Private
{
#if !UE_BUILD_SHIPPING
bool bValidateConsistencyWhenAddingNode = false;

FAutoConsoleVariableRef ConsoleVariables[] =
{
	FAutoConsoleVariableRef(
		TEXT("ai.debug.nav.validateConsistencyWhenAddingOctreeNode"),
		bValidateConsistencyWhenAddingNode,
		TEXT("Used to validate that registered FNavigationElement matches the values "
			"returned by INavRelevantInterface when processing pending updates to add elements to the octree."))
};
#endif // !UE_BUILD_SHIPPING

} // UE::NavigationOctree::Private

//----------------------------------------------------------------------//
// FNavigationOctree
//----------------------------------------------------------------------//
// 构造：透传 TOctree2 的 (Origin, Radius)，初始化默认 gather 模式 = Instant。
FNavigationOctree::FNavigationOctree(const FVector& Origin, FVector::FReal Radius)
	: TOctree2<FNavigationOctreeElement, FNavigationOctreeSemantics>(Origin, Radius)
	, DefaultGeometryGatheringMode(ENavDataGatheringMode::Instant)
	, bGatherGeometry(false)
	, NodesMemory(0)
#if !UE_BUILD_SHIPPING
	, GatheringNavModifiersTimeLimitWarning(-1.0f)
#endif // !UE_BUILD_SHIPPING
{
	INC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
// 析构：清掉 ElementToOctreeId 反查表；底层 TOctree2 由基类析构回收节点。
FNavigationOctree::~FNavigationOctree()
{
	DEC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );
	DEC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, NodesMemory);
	
	ElementToOctreeId.Empty();
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

// 只有 Invalid 不允许；其它值转成 ENavDataGatheringMode 枚举值保存。
void FNavigationOctree::SetDataGatheringMode(ENavDataGatheringModeConfig Mode)
{
	check(Mode != ENavDataGatheringModeConfig::Invalid);
	DefaultGeometryGatheringMode = ENavDataGatheringMode(Mode);
}

void FNavigationOctree::SetNavigableGeometryStoringMode(ENavGeometryStoringMode NavGeometryMode)
{
	bGatherGeometry = (NavGeometryMode == FNavigationOctree::StoreNavGeometry);
}

// 对 Pending Lazy 的元素做补采集：
//   - Geometry 部分（若不是 slice 方式）调用 GeometryExportDelegate 同步拉满
//   - Modifier 部分调用 NavigationDataExportDelegate 收集 NavArea / Link 等
// 完成后 ValidateAndShrink，并把 NodesMemory 差值同步到统计量。
// 调用者：Recast tile 生成前、或 FNavigationDataHandler::DemandLazyDataGathering。
void FNavigationOctree::DemandLazyDataGathering(FNavigationRelevantData& ElementData)
{
	LLM_SCOPE_BYTAG(NavigationOctree);

	bool bShrink = false;
	const int32 OrgElementMemory = IntCastChecked<int32>(ElementData.GetGeometryAllocatedSize());

	// 分支：本元素支持"几何切片"（一次一小块）的话，这里不必再整体拉；让 tile 生成走 slice 接口
	if (ElementData.IsPendingLazyGeometryGathering() == true && ElementData.SupportsGatheringGeometrySlices() == false)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMeshGenerator_LazyGeometryExport);

		GeometryExportDelegate.ExecuteIfBound(ElementData.SourceElement.Get(), ElementData);
		bShrink = true;

		// mark this element as no longer needing geometry gathering
		ElementData.bPendingLazyGeometryGathering = false;
	}

	if (ElementData.IsPendingLazyModifiersGathering())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMeshGenerator_LazyModifiersExport);

#if !UE_BUILD_SHIPPING
		const bool bCanOutputDurationWarning = GatheringNavModifiersTimeLimitWarning >= 0.0f;
		const double StartTime = bCanOutputDurationWarning ? FPlatformTime::Seconds() : 0.0f;
#endif //!UE_BUILD_SHIPPING

		ElementData.SourceElement->NavigationDataExportDelegate.ExecuteIfBound(ElementData.SourceElement.Get(), ElementData);
		ElementData.bPendingLazyModifiersGathering = false;
		bShrink = true;

#if !UE_BUILD_SHIPPING
		// If GatheringNavModifiersWarningLimitTime is positive, it will print a Warning if the time taken to call GetNavigationData is more than GatheringNavModifiersWarningLimitTime
		if (bCanOutputDurationWarning)
		{
			const double DeltaTime = FPlatformTime::Seconds() - StartTime;
			if (DeltaTime > GatheringNavModifiersTimeLimitWarning)
			{
				UE_LOG(LogNavigation, Warning, TEXT("The time (%f sec) for gathering navigation data on a navigation element exceeded the time limit (%f sec) | Element = %s"),
					DeltaTime,
					GatheringNavModifiersTimeLimitWarning,
					*ElementData.SourceElement->GetName());
			}
		}
#endif //!UE_BUILD_SHIPPING
	}

	if (bShrink)
	{
		// validate exported data
		// shrink arrays before counting memory
		// it will be reallocated when adding to octree and RemoveNode will have different value returned by GetAllocatedSize()
		ElementData.ValidateAndShrink();
	}

	// 统计内存变化（可能为负——Shrink 之后）
	const int32 ElementMemoryChange = IntCastChecked<int32>(ElementData.GetGeometryAllocatedSize()) - OrgElementMemory;
	NodesMemory += ElementMemoryChange;
	INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, ElementMemoryChange);
}

// Deprecated
void FNavigationOctree::DemandChildLazyDataGathering(FNavigationRelevantData& ElementData, INavRelevantInterface& ChildNavInterface)
{
	const TSharedRef<const FNavigationElement> TmpElement = FNavigationElement::CreateFromNavRelevantInterface(ChildNavInterface);
	DemandChildLazyDataGathering(ElementData, TmpElement.Get());
}

void FNavigationOctree::DemandChildLazyDataGathering(FNavigationRelevantData& ElementData, const FNavigationElement& ChildElement) const
{
	LLM_SCOPE_BYTAG(NavigationOctree);

	if (IsLazyGathering(ChildElement))
	{
		if (ChildElement.NavigationDataExportDelegate.ExecuteIfBound(ChildElement, ElementData))
		{
			ElementData.ValidateAndShrink();
		}
	}
}

#if !UE_BUILD_SHIPPING
void FNavigationOctree::SetGatheringNavModifiersTimeLimitWarning(const float Threshold)
{
	GatheringNavModifiersTimeLimitWarning = Threshold;
}
#endif // !UE_BUILD_SHIPPING

// Deprecated
bool FNavigationOctree::IsLazyGathering(const INavRelevantInterface& ChildNavInterface) const
{
	const TSharedRef<const FNavigationElement> TmpElement = FNavigationElement::CreateFromNavRelevantInterface(ChildNavInterface);
	return IsLazyGathering(TmpElement.Get());
}

bool FNavigationOctree::IsLazyGathering(const FNavigationElement& NavigationElement) const
{
	const ENavDataGatheringMode GatheringMode = NavigationElement.GetGeometryGatheringMode();
	const bool bDoInstantGathering = ((GatheringMode == ENavDataGatheringMode::Default && DefaultGeometryGatheringMode == ENavDataGatheringMode::Instant)
		|| GatheringMode == ENavDataGatheringMode::Instant);

	return !bDoInstantGathering;
}

// Deprecated
void FNavigationOctree::AddNode(UObject* ElementOb, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Element)
{
	if (NavElement)
	{
		AddNode(Bounds, Element);
	}
}

// AddNode：把新元素插入 Octree。
// 顺序要点：
//   1) 校验 Bounds 合法（Invalid 或体积接近 0 直接跳过）；
//   2) 把 Bounds 写到 OctreeElement.Bounds；
//   3) 若 Data 还空：按 bGatherGeometry + bDoInstantGathering 立刻 export
//      或者打上 Pending 标记延后；
//   4) ValidateAndShrink 回收多余容量；
//   5) AddElement → 底层 TOctree2 插入 → 触发 SetElementId → 写 ElementToOctreeId。
// 第 5 步保证了"节点入树"与"Handle→Id 反查表更新"是原子一体的。
void FNavigationOctree::AddNode(const FBox& Bounds, FNavigationOctreeElement& OctreeElement)
{
	LLM_SCOPE_BYTAG(NavigationOctree);

	const TSharedRef<const FNavigationElement>& ElementRef = OctreeElement.Data->SourceElement;
	const FNavigationElement& SourceElement = ElementRef.Get();

	UE_LOG(LogNavigation, VeryVerbose, TEXT("%hs: '%s' bounds: [%s]"), __FUNCTION__, *SourceElement.GetName(), *Bounds.ToString());

#if !UE_BUILD_SHIPPING
	// 调试一致性校验：FNavigationElement 入队时的快照 vs. 真正入 Octree 时 interface 返回的最新值
	if (UE::NavigationOctree::Private::bValidateConsistencyWhenAddingNode)
	{
		if (const INavRelevantInterface* NavRelevantInterface = Cast<INavRelevantInterface>(SourceElement.GetWeakUObject().Get()))
		{
			// The following validations are used to detect scenarios where properties of a NavigationElement created from
			// a navigation relevant UObject (implementing INavRelevantInterface and returning 'true' to IsNavigationRelevant())
			// differs from the values that are provided by that same UObject when the pending registration of the element is processed.
			// These indicate that the values were not up-to-date when the element was added to a pending FNavigationDirtyElement,
			// or during that frame since an update would have refreshed the pending DirtyElement.
			if (const FBox NewBounds = NavRelevantInterface->GetNavigationBounds(); !Bounds.Equals(NewBounds))
			{
				UE_LOG(LogNavigation, Warning, TEXT("%hs: '%s' bounds changed between element's creation and its addition to the octree: [%s] --> [%s]"),
					__FUNCTION__, *SourceElement.GetName(), *Bounds.ToString(), *NewBounds.ToString());
			}

			if (const UObject* NewParent = NavRelevantInterface->GetNavigationParent(); NewParent != SourceElement.GetNavigationParent())
			{
				UE_LOG(LogNavigation, Warning, TEXT("%hs: '%s' parent changed between element's creation and its addition to the octree: [%s] --> [%s]"),
					__FUNCTION__, *SourceElement.GetName(), *GetNameSafe(SourceElement.GetNavigationParent().Get()), *GetNameSafe(NewParent));
			}
		}
	}
#endif // !UE_BUILD_SHIPPING

	// 防御：非法/空 Bounds 不入树，避免污染 Octree 统计与后续 Dirty 扩散
	if (UNLIKELY(!Bounds.IsValid || Bounds.GetSize().IsNearlyZero()))
	{
		UE_LOG(LogNavigation, Warning, TEXT("%hs: %s bounds, ignoring %s."), __FUNCTION__, !Bounds.IsValid ? TEXT("Invalid") : TEXT("Empty"), *SourceElement.GetFullName());
		return;
	}

	OctreeElement.Bounds = Bounds;
	OctreeElement.Data->bShouldSkipDirtyAreaOnAddOrRemove = !SourceElement.GetDirtyAreaOnRegistration();

	// Only gather geometry and navigation data if not already provided.
	// We don't want to use the default geometry export since it will clear the navigation data.
	if (OctreeElement.Data->IsEmpty())
	{
		const bool bDoInstantGathering = !IsLazyGathering(SourceElement);

		// 几何：优先走 Delegate 立即采集；否则打上 Lazy 标记等 tile 生成时补
		if (bGatherGeometry)
		{
			if (bDoInstantGathering)
			{
				GeometryExportDelegate.ExecuteIfBound(SourceElement, *OctreeElement.Data);
			}
			else
			{
				OctreeElement.Data->bPendingLazyGeometryGathering = true;
				OctreeElement.Data->bSupportsGatheringGeometrySlices = SourceElement.GeometrySliceExportDelegate.IsBound();
			}
		}

		SCOPE_CYCLE_COUNTER(STAT_Navigation_GatheringNavigationModifiersSync);
		// Modifier：同样的立即/延后分支
		if (bDoInstantGathering)
		{
#if !UE_BUILD_SHIPPING
			const bool bCanOutputDurationWarning = GatheringNavModifiersTimeLimitWarning >= 0.0f;
			const double StartTime = bCanOutputDurationWarning ? FPlatformTime::Seconds() : 0.0f;
#endif //!UE_BUILD_SHIPPING

			SourceElement.NavigationDataExportDelegate.ExecuteIfBound(SourceElement, *OctreeElement.Data);

#if !UE_BUILD_SHIPPING
			// If GatheringNavModifiersWarningLimitTime is positive, it will print a Warning if the time taken to call GetNavigationData is more than GatheringNavModifiersWarningLimitTime			
			if (bCanOutputDurationWarning)
			{
				if (const double DeltaTime = FPlatformTime::Seconds() - StartTime; DeltaTime > GatheringNavModifiersTimeLimitWarning)
				{
					UE_LOG(LogNavigation, Warning, TEXT("The time (%f sec) for gathering navigation data on a navigation element exceeded the time limit (%f sec) | Element = %s"),
						DeltaTime,
						GatheringNavModifiersTimeLimitWarning,
						*SourceElement.GetName());
				}
			}
#endif //!UE_BUILD_SHIPPING
		}
		else
		{
			OctreeElement.Data->bPendingLazyModifiersGathering = true;
		}

		// validate exported data
		// shrink arrays before counting memory
		// it will be reallocated when adding to octree and RemoveNode will have different value returned by GetAllocatedSize()
		OctreeElement.ValidateAndShrink();
	}

	const int32 ElementMemory = OctreeElement.GetAllocatedSize();
	NodesMemory += ElementMemory;
	INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, ElementMemory);

	// 真正插入底层 Octree；TOctree2 回调 FNavigationOctreeSemantics::SetElementId → 写反查表
	AddElement(OctreeElement);
}

// Deprecated
void FNavigationOctree::AppendToNode(const FOctreeElementId2& Id, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Element)
{
	if (NavElement)
	{
		AppendToNode(Id, FNavigationElement::CreateFromNavRelevantInterface(*NavElement), Bounds, Element);
	}
}

// AppendToNode：把子元素的"导出数据"合并到父节点上。
// 流程：
//   1) 复制原节点 → Element（用于"先改后写"）；
//   2) Bounds 取并集；
//   3) 根据子元素的 gather 模式做即采/延后；
//   4) ValidateAndShrink 并更新 NodesMemory delta；
//   5) RemoveElement + AddElement：TOctree2 不支持原地更新 Bounds，
//      所以这里一定是"移除旧 → 加新"的序列，中间 ElementToOctreeId 会在
//      AddElement 回调中重写。
void FNavigationOctree::AppendToNode(const FOctreeElementId2& Id, const TSharedRef<const FNavigationElement>& ElementRef, const FBox& Bounds, FNavigationOctreeElement& Element)
{
	LLM_SCOPE_BYTAG(NavigationOctree);

	const FNavigationOctreeElement OrgData = GetElementById(Id);

	Element = OrgData;
	Element.Bounds = Bounds + OrgData.Bounds.GetBox();

	SCOPE_CYCLE_COUNTER(STAT_Navigation_GatheringNavigationModifiersSync);
	if (IsLazyGathering(ElementRef.Get()))
	{
		Element.Data->bPendingChildLazyModifiersGathering = true;
	}
	else
	{
		ElementRef->NavigationDataExportDelegate.ExecuteIfBound(ElementRef.Get(), *Element.Data);
	}

	// validate exported data
	// shrink arrays before counting memory
	// it will be reallocated when adding to octree and RemoveNode will have different value returned by GetAllocatedSize()
	Element.ValidateAndShrink();

	// 内存统计按 delta 校正，避免重复计数
	const int32 OrgElementMemory = OrgData.GetAllocatedSize();
	const int32 NewElementMemory = Element.GetAllocatedSize();
	const int32 MemoryDelta = NewElementMemory - OrgElementMemory;

	NodesMemory += MemoryDelta;
	INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, MemoryDelta);

	RemoveElement(Id);
	AddElement(Element);
}

// 仅改 Bounds 的原子操作：RemoveElement + AddElement（保持 Data 原样）。
// 注意这里并不调用 ValidateAndShrink / NodesMemory 校正，因为 Data 没改。
void FNavigationOctree::UpdateNode(const FOctreeElementId2& Id, const FBox& NewBounds)
{
	FNavigationOctreeElement ElementCopy = GetElementById(Id);
	RemoveElement(Id);
	ElementCopy.Bounds = NewBounds;
	AddElement(ElementCopy);
}

// 节点移除：先扣 NodesMemory 统计，再 RemoveElement。
// 注意：ElementToOctreeId 反查表的清理由 FNavigationOctreeController::RemoveNode 完成，
// 本方法仅处理 TOctree2 内部状态。
void FNavigationOctree::RemoveNode(const FOctreeElementId2& Id)
{
	const FNavigationOctreeElement& Element = GetElementById(Id);
	const int32 ElementMemory = Element.GetAllocatedSize();
	NodesMemory -= ElementMemory;
	DEC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, ElementMemory);

	RemoveElement(Id);
}

const FNavigationRelevantData* FNavigationOctree::GetDataForID(const FOctreeElementId2& Id) const
{
	return Id.IsValidId() ? &*GetElementById(Id).Data : nullptr;
}

FNavigationRelevantData* FNavigationOctree::GetMutableDataForID(const FOctreeElementId2& Id)
{
	return Id.IsValidId() ? &*GetElementById(Id).Data : nullptr;
}

// 反查表写入：由 FNavigationOctreeSemantics::SetElementId 回调。
// 本函数是 AddElement → 回调 → 反查表 的最后一环，保障 Handle→Id 一致性。
void FNavigationOctree::SetElementIdImpl(const FNavigationElementHandle ElementHandle, const FOctreeElementId2 Id)
{
	ElementToOctreeId.Add(ElementHandle, Id);
}

//----------------------------------------------------------------------//
// FNavigationOctreeSemantics
//----------------------------------------------------------------------//
#if NAVSYS_DEBUG
FORCENOINLINE
#endif // NAVSYS_DEBUG
// TOctree2 的回调：把每次 AddElement 分配到的 Id 交给我们维护的反查表。
void FNavigationOctreeSemantics::SetElementId(FOctree& OctreeOwner, const FNavigationOctreeElement& Element, const FOctreeElementId2 Id)
{
	static_cast<FNavigationOctree&>(OctreeOwner).SetElementIdImpl(Element.Data.Get().SourceElement.Get().GetHandle(), Id);
}