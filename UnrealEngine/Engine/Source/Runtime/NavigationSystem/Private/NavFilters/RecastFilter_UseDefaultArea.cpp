// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   URecastFilter_UseDefaultArea 的实现。
//   关键：InitializeFilter 里把运行时 Filter 的底层实现替换为
//   ARecastNavMesh 注册的命名过滤器 ERecastNamedFilter::FilterOutAreas
//   （该 Recast 过滤器内部只允许默认 Area，其它 Area 全部视为不可达）。
// =============================================================================

#include "NavFilters/RecastFilter_UseDefaultArea.h"
#include "Templates/Casts.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastQueryFilter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RecastFilter_UseDefaultArea)

URecastFilter_UseDefaultArea::URecastFilter_UseDefaultArea(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

// 初始化 Filter：先挂 Recast 命名过滤器底层实现，再执行父类常规初始化（写入 Area 覆写与 Flag）。
// 仅 WITH_RECAST 生效；否则仅走父类默认逻辑。
void URecastFilter_UseDefaultArea::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
#if WITH_RECAST
	// 把 Filter 底层实现替换为 "FilterOutAreas"：Detour 层只通过 Default Area 的多边形。
	Filter.SetFilterImplementation(dynamic_cast<const INavigationQueryFilterInterface*>(ARecastNavMesh::GetNamedFilter(ERecastNamedFilter::FilterOutAreas)));
#endif // WITH_RECAST

	Super::InitializeFilter(NavData, Querier, Filter);
}

