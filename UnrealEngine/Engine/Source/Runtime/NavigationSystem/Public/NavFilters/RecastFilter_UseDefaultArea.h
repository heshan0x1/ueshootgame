// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   URecastFilter_UseDefaultArea —— Recast 专用过滤器：
//   运行时 Filter 的底层实现挂载为 ARecastNavMesh 命名过滤器
//   ERecastNamedFilter::FilterOutAreas（仅接受 Default Area，过滤掉其它 Area）。
//
//   常用于"临时把某些 Area 屏蔽掉，只让 Agent 走默认可行走区"的查询。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "NavFilters/NavigationQueryFilter.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "RecastFilter_UseDefaultArea.generated.h"

class ANavigationData;
class UObject;
struct FNavigationQueryFilter;

/** Regular navigation area, applied to entire navigation data by default */
// Recast 过滤器：把 Filter 实现切到 FilterOutAreas 命名过滤器，只保留 Default Area。
UCLASS(MinimalAPI)
class URecastFilter_UseDefaultArea : public UNavigationQueryFilter
{
	GENERATED_UCLASS_BODY()

	// 重写：先挂 Recast 命名过滤器实现，再走基类的 Area/Flag 覆写逻辑。
	virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const override;
};
