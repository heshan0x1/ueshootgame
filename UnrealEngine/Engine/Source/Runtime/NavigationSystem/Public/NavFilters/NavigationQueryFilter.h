// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义寻路过滤器相关数据结构与 UObject 描述类。
//
// 核心类型：
//   - FNavigationFilterArea：一条"Area 覆写"记录（对某 Area 改 Cost / 标 Excluded）。
//   - FNavigationFilterFlags：16 位导航 Flag 的 POD 封装（Include/Exclude 共用）。
//   - UNavigationQueryFilter：Filter 的 UObject 描述，蓝图可配置；运行时按需
//     初始化为 FNavigationQueryFilter 并缓存在 ANavigationData。
//
// 核心流程：
//   GetQueryFilter(NavData, Querier)
//     └─► 如是 Meta Filter 且 Querier 非空 → GetSimpleFilterForAgent 挑具体类
//     └─► 查 NavData 缓存；没命中则 GetDefaultQueryFilter()->GetCopy() 克隆
//     └─► InitializeFilter() 把 Area 覆写/Flags 写入运行时 Filter
//     └─► 非 bInstantiateForQuerier 时把结果存回 NavData 缓存
//
// 与其它文件的关系：
//   - 实现：Private/NavFilters/NavigationQueryFilter.cpp。
//   - Recast 专用过滤器：URecastFilter_UseDefaultArea 继承本类。
//   - 被 ANavigationData / FPathFindingQuery 广泛使用。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "NavAreas/NavArea.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "NavigationQueryFilter.generated.h"

class ANavigationData;

// 一条 Filter 针对某 Area 的覆写：可改 Cost、标 Excluded。
USTRUCT()
struct FNavigationFilterArea
{
	GENERATED_USTRUCT_BODY()

	/** navigation area class */
	// 被覆写的 Area 类（必须填；运行时查 AreaId）。
	UPROPERTY(EditAnywhere, Category=Area)
	TSubclassOf<UNavArea> AreaClass;

	/** override for travel cost */
	// 通行代价覆写值（仅当 bOverrideTravelCost=true 时生效）。
	UPROPERTY(EditAnywhere, Category=Area, meta=(EditCondition="bOverrideTravelCost",ClampMin=0.001))
	float TravelCostOverride;

	/** override for entering cost */
	// 进入代价覆写值（仅当 bOverrideEnteringCost=true 时生效）。
	UPROPERTY(EditAnywhere, Category=Area, meta=(EditCondition="bOverrideEnteringCost",ClampMin=0))
	float EnteringCostOverride;

	/** mark as excluded */
	// 是否在寻路时完全排除该 Area（优先级高于 Cost 覆写）。
	UPROPERTY(EditAnywhere, Category=Area)
	uint32 bIsExcluded : 1;

	// 是否启用 TravelCostOverride（编辑器 InlineEditConditionToggle）。
	UPROPERTY(EditAnywhere, Category=Area, meta=(InlineEditConditionToggle))
	uint32 bOverrideTravelCost : 1;

	// 是否启用 EnteringCostOverride（编辑器 InlineEditConditionToggle）。
	UPROPERTY(EditAnywhere, Category=Area, meta=(InlineEditConditionToggle))
	uint32 bOverrideEnteringCost : 1;

	FNavigationFilterArea()
	{
		FMemory::Memzero(*this);
		TravelCostOverride = 1.f;
	}
};

// 
// Use UNavigationSystemV1.DescribeFilterFlags() to setup user friendly names of flags
// 
// 16 个导航 Flag 的 USTRUCT：Include/Exclude 两套都复用本结构。
// 每一位的语义由项目自定义；通过 UNavigationSystemV1::DescribeFilterFlags 给每位取友好名。
USTRUCT()
struct FNavigationFilterFlags
{
	GENERATED_USTRUCT_BODY()

#if CPP
	union
	{
		struct
		{
#endif
	// bNavFlag0..15：16 个 Flag 位；只能通过属性面板勾选。
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag0 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag1 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag2 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag3 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag4 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag5 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag6 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag7 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag8 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag9 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag10 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag11 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag12 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag13 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag14 : 1;
	UPROPERTY(EditAnywhere, Category=Flags)
	uint32 bNavFlag15 : 1;
#if CPP
		};
		// union 别名：把 16 位整体压成 uint16，Filter 运行时直接用 Packed。
		uint16 Packed = 0;
	};
#endif
};

/** Class containing definition of a navigation query filter */
// 导航查询 Filter 的 UObject 描述类；蓝图可继承配置 Areas/IncludeFlags/ExcludeFlags。
UCLASS(Abstract, Blueprintable, MinimalAPI)
class UNavigationQueryFilter : public UObject
{
	GENERATED_UCLASS_BODY()
	
	/** list of overrides for navigation areas */
	// Area 覆写列表；编辑器以 AreaClass 字段为标题。
	UPROPERTY(EditAnywhere, Category=Filter, meta = (TitleProperty = "AreaClass"))
	TArray<FNavigationFilterArea> Areas;

	/** required flags of navigation nodes */
	// 必须包含的 Flag 位集（Node 的 Flag & IncludeFlags != 0 才允许扩展）。
	UPROPERTY(EditAnywhere, Category=Filter)
	FNavigationFilterFlags IncludeFlags;

	/** forbidden flags of navigation nodes */
	// 禁止的 Flag 位集（Node 的 Flag & ExcludeFlags != 0 则直接剔除）。
	UPROPERTY(EditAnywhere, Category=Filter)
	FNavigationFilterFlags ExcludeFlags;

	/** get filter for given navigation data and initialize on first access */
	// 获取运行时 Filter；首次访问会初始化并（可选）缓存。Meta Filter 会转发给具体类。
	NAVIGATIONSYSTEM_API FSharedConstNavQueryFilter GetQueryFilter(const ANavigationData& NavData, const UObject* Querier) const;
	
	/** helper functions for accessing filter */
	// 便捷静态函数：直接按 FilterClass 取 Filter（不带 Querier 的路径，不走 Meta）。
	static NAVIGATIONSYSTEM_API FSharedConstNavQueryFilter GetQueryFilter(const ANavigationData& NavData, TSubclassOf<UNavigationQueryFilter> FilterClass);
	// 带 Querier 的版本：支持 Meta Filter 依据 Querier 分派到具体 Filter 类。
	static NAVIGATIONSYSTEM_API FSharedConstNavQueryFilter GetQueryFilter(const ANavigationData& NavData, const UObject* Querier, TSubclassOf<UNavigationQueryFilter> FilterClass);

	// 便捷模板：以类型 T 推断默认 FilterClass（少写一个 StaticClass()）。
	template<class T>
	static FSharedConstNavQueryFilter GetQueryFilter(const ANavigationData& NavData, TSubclassOf<UNavigationQueryFilter> FilterClass = T::StaticClass())
	{
		return GetQueryFilter(NavData, FilterClass);
	}

#if WITH_EDITOR
	// 编辑器改 Filter 属性后清除所有 NavigationSystem 里缓存的该类 Filter 实例。
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:

	/** if set, filter will not be cached by navigation data and can be configured per Querier */
	// 若设 true：每个 Querier 都生成独立 Filter 实例（不共享缓存），适合"按 Pawn/AI 个体动态修改代价"的场景。
	uint32 bInstantiateForQuerier : 1;

	/** if set to true GetSimpleFilterForAgent will be called when determining the actual filter class to be used */
	// 若设 true：本 Filter 是 Meta Filter，查询时会先调用 GetSimpleFilterForAgent 选出具体 Filter 类。
	uint32 bIsMetaFilter : 1;

	/** helper functions for adding area overrides */
	// 便捷添加接口：设置某 Area 的通行代价（会自动打开 bOverrideTravelCost）。
	NAVIGATIONSYSTEM_API void AddTravelCostOverride(TSubclassOf<UNavArea> AreaClass, float TravelCost);
	// 便捷添加接口：设置某 Area 的进入代价（会自动打开 bOverrideEnteringCost）。
	NAVIGATIONSYSTEM_API void AddEnteringCostOverride(TSubclassOf<UNavArea> AreaClass, float EnteringCost);
	// 便捷添加接口：将某 Area 标为 Excluded。
	NAVIGATIONSYSTEM_API void AddExcludedArea(TSubclassOf<UNavArea> AreaClass);

	/** find index of area data */
	// 在 Areas 数组里按 AreaClass 线性查找，未找到返回 INDEX_NONE。
	NAVIGATIONSYSTEM_API int32 FindAreaOverride(TSubclassOf<UNavArea> AreaClass) const;
	
	/** setup filter for given navigation data, use to create custom filters */
	// 将 UObject 配置（Areas + Flags）写入运行时 FNavigationQueryFilter；
	// 派生类可重写以注入自定义实现（例如 Recast 过滤器会先挂 dtQueryFilter）。
	NAVIGATIONSYSTEM_API virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const;

	// Meta Filter 专用：根据 Querier 选出最终使用的 Filter 类；基类默认返回空。
	virtual TSubclassOf<UNavigationQueryFilter> GetSimpleFilterForAgent(const UObject& Querier) const { return nullptr; }
};
