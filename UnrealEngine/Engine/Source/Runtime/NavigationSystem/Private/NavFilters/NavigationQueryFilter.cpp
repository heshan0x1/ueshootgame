// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavigationQueryFilter 的实现。核心是 GetQueryFilter / InitializeFilter —— 
//   把 UObject 层配置（Areas、IncludeFlags、ExcludeFlags）落到运行时
//   FNavigationQueryFilter 实例上，再缓存进 ANavigationData 供重复使用。
//
// 关键流程：
//   GetQueryFilter(NavData, Querier)
//     ├─ Meta Filter + 有 Querier → GetSimpleFilterForAgent 挑具体类后递归
//     ├─ 非 InstantiateForQuerier → 先查 NavData 缓存
//     └─ 未命中 → 克隆 NavData 默认 Filter，InitializeFilter 填内容，可选写回缓存
// =============================================================================

#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "NavAreas/NavArea.h"
#include "NavigationData.h"
#include "EngineGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationQueryFilter)

//----------------------------------------------------------------------//
// UNavigationQueryFilter
//----------------------------------------------------------------------//
// 构造：默认所有 Flag 都接受（Include 全 1），不禁用任何（Exclude 全 0）；非 Querier 实例化；非 Meta。
UNavigationQueryFilter::UNavigationQueryFilter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	IncludeFlags.Packed = 0xffff;
	ExcludeFlags.Packed = 0;
	bInstantiateForQuerier = false;
	bIsMetaFilter = false;
}

// 获取运行时 Filter 的主入口（实例方法）。
// 先处理 Meta 分派，再走常规缓存/构造路径。
FSharedConstNavQueryFilter UNavigationQueryFilter::GetQueryFilter(const ANavigationData& NavData, const UObject* Querier) const
{
	// 分支：Meta Filter 且有 Querier → 委托给 GetSimpleFilterForAgent 挑出的具体 Filter 类。
	// 注意：仅当挑选出来的类本身不是 Meta 时才递归调用，避免死循环。
	if (bIsMetaFilter && Querier != nullptr)
	{
		TSubclassOf<UNavigationQueryFilter> SimpleFilterClass = GetSimpleFilterForAgent(*Querier);
		if (*SimpleFilterClass)
		{
			const UNavigationQueryFilter* DefFilterOb = SimpleFilterClass.GetDefaultObject();
			check(DefFilterOb);
			if (DefFilterOb->bIsMetaFilter == false)
			{
				return DefFilterOb->GetQueryFilter(NavData, Querier);
			}
		}
	}
	
	// the default, simple filter implementation
	// 非 Querier 独立实例化时优先查 NavData 的 Filter 缓存，命中直接返回。
	FSharedConstNavQueryFilter SharedFilter = bInstantiateForQuerier ? nullptr : NavData.GetQueryFilter(GetClass());
	if (!SharedFilter.IsValid())
	{
		// Clone the default filter so we get search nodes and other fields
		// 克隆 NavData 默认 Filter（带 Recast 过滤实现等基础设施），再填入本 UObject 的配置。
		FSharedNavQueryFilter NavFilter = NavData.GetDefaultQueryFilter()->GetCopy();

		InitializeFilter(NavData, Querier, *NavFilter.Get());

		SharedFilter = NavFilter;
		// 仅共享模式下写回缓存，供下次同一 FilterClass 复用。
		if (!bInstantiateForQuerier)
		{
			const_cast<ANavigationData&>(NavData).StoreQueryFilter(GetClass(), SharedFilter);
		}
	}

	return SharedFilter;
}

// 把 Areas 列表中的 Cost/Excluded 覆写写到运行时 Filter，再把 Include/Exclude Flag 压进去。
void UNavigationQueryFilter::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
	// apply overrides
	// 迭代目标：对 Areas 配置列表里的每个 AreaClass 查到 NavData 里的 AreaId 并施加 Cost/Excluded 覆写。
	for (int32 i = 0; i < Areas.Num(); i++)
	{
		const FNavigationFilterArea& AreaData = Areas[i];
		
		const int32 AreaId = NavData.GetAreaID(AreaData.AreaClass);
		// 分支：AreaClass 未在此 NavData 注册则跳过（例如仅某些 NavData 支持该 Area）。
		if (AreaId == INDEX_NONE)
		{
			continue;
		}

		// 分支：Excluded 比 Cost 覆写优先——只要标了 bIsExcluded，其它 Cost 字段无效。
		if (AreaData.bIsExcluded)
		{
			Filter.SetExcludedArea(IntCastChecked<uint8>(AreaId));
		}
		else
		{
			// 通行代价覆写：至少为 1.0 避免零/负代价扭曲 A*。
			if (AreaData.bOverrideTravelCost)
			{
				Filter.SetAreaCost(IntCastChecked<uint8>(AreaId), FMath::Max(1.0f, AreaData.TravelCostOverride));
			}

			// 进入代价覆写：至少 0.0，允许"无固定入场费"。
			if (AreaData.bOverrideEnteringCost)
			{
				Filter.SetFixedAreaEnteringCost(IntCastChecked<uint8>(AreaId), FMath::Max(0.0f, AreaData.EnteringCostOverride));
			}
		}
	}

	// apply flags
	// 把 16 位 Include/Exclude Flag 整块压入 Filter（Filter 内部与 Node Flag 做位运算）。
	Filter.SetIncludeFlags(IncludeFlags.Packed);
	Filter.SetExcludeFlags(ExcludeFlags.Packed);
}

// 静态入口：按 FilterClass 取 CDO，再走实例版 GetQueryFilter（无 Querier，走普通路径）。
FSharedConstNavQueryFilter UNavigationQueryFilter::GetQueryFilter(const ANavigationData& NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	if (FilterClass)
	{
		const UNavigationQueryFilter* DefFilterOb = FilterClass.GetDefaultObject();
		// no way we have not default object here
		check(DefFilterOb);
		return DefFilterOb->GetQueryFilter(NavData, nullptr);
	}

	return nullptr;
}

// 静态入口：带 Querier 版本（触发 Meta 分派）。
FSharedConstNavQueryFilter UNavigationQueryFilter::GetQueryFilter(const ANavigationData& NavData, const UObject* Querier, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	if (FilterClass)
	{
		const UNavigationQueryFilter* DefFilterOb = FilterClass.GetDefaultObject();
		// no way we have not default object here
		check(DefFilterOb);
		return DefFilterOb->GetQueryFilter(NavData, Querier);
	}

	return nullptr;
}

// 便捷 API：为某 Area 添加/更新通行代价覆写（若数组里已有同 AreaClass 记录则复用）。
void UNavigationQueryFilter::AddTravelCostOverride(TSubclassOf<UNavArea> AreaClass, float TravelCost)
{
	int32 Idx = FindAreaOverride(AreaClass);
	if (Idx == INDEX_NONE)
	{
		FNavigationFilterArea AreaData;
		AreaData.AreaClass = AreaClass;

		Idx = Areas.Add(AreaData);
	}

	Areas[Idx].bOverrideTravelCost = true;
	Areas[Idx].TravelCostOverride = TravelCost;
}

// 便捷 API：为某 Area 添加/更新进入代价覆写。
void UNavigationQueryFilter::AddEnteringCostOverride(TSubclassOf<UNavArea> AreaClass, float EnteringCost)
{
	int32 Idx = FindAreaOverride(AreaClass);
	if (Idx == INDEX_NONE)
	{
		FNavigationFilterArea AreaData;
		AreaData.AreaClass = AreaClass;

		Idx = Areas.Add(AreaData);
	}

	Areas[Idx].bOverrideEnteringCost = true;
	Areas[Idx].EnteringCostOverride = EnteringCost;
}

// 便捷 API：把某 Area 标记为 Excluded（寻路时完全排除）。
void UNavigationQueryFilter::AddExcludedArea(TSubclassOf<UNavArea> AreaClass)
{
	int32 Idx = FindAreaOverride(AreaClass);
	if (Idx == INDEX_NONE)
	{
		FNavigationFilterArea AreaData;
		AreaData.AreaClass = AreaClass;

		Idx = Areas.Add(AreaData);
	}

	Areas[Idx].bIsExcluded = true;
}

// 线性查找 Areas 中与 AreaClass 匹配的记录索引；未找到返回 INDEX_NONE。
int32 UNavigationQueryFilter::FindAreaOverride(TSubclassOf<UNavArea> AreaClass) const
{
	// 迭代目标：逐条比对 AreaClass（数量通常在个位数，线性扫描足够）。
	for (int32 i = 0; i < Areas.Num(); i++)
	{
		if (Areas[i].AreaClass == AreaClass)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

#if WITH_EDITOR
// 编辑器侧改了 Filter CDO 后，把所有 World 里 NavigationSystem 缓存的该类 Filter 实例清掉，
// 强制下次 GetQueryFilter 时重新构造，及时反映新的配置。
void UNavigationQueryFilter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// remove cached filter settings from existing NavigationSystems
	// 迭代目标：遍历 Engine 当前所有 WorldContext，逐个让其 NavigationSystem 重置本 FilterClass 的缓存。
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context.World());
		if (NavSys)
		{
			NavSys->ResetCachedFilter(GetClass());
		}
	}
}
#endif

