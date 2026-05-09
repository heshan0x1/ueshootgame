// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// AbstractNavData.h —— "空壳"导航数据实现
// -----------------------------------------------------------------------------
// 文件职责：
//   - 当某个 Agent 的 NavData 尚未就绪（例如世界里没有 ARecastNavMesh），
//     NavigationSystem 仍需要一个占位对象来接收 FindPath 请求而不崩溃；
//   - 本文件提供 AAbstractNavData（派生 ANavigationData）与它所产生的
//     FAbstractNavigationPath（派生 FNavigationPath）——返回"起点→终点直线"
//     的平凡路径，所有几何/投影/随机点查询均返回无效；
//   - FAbstractQueryFilter 实现空 INavigationQueryFilterInterface。
//
// 使用者：UNavigationSystemV1::CreateAbstractNavData / RequestAbstractNavData。
// =============================================================================

#pragma once

#include "AI/Navigation/NavQueryFilter.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "AI/Navigation/NavigationTypes.h"
#endif
#include "Containers/Array.h"
#include "Containers/ContainersFwd.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "Math/Box.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "Math/UnrealMathSSE.h"
#endif
#include "Math/Vector.h"
#include "NavigationData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "AbstractNavData.generated.h"

class UClass;
class UObject;
struct FPathFindingQuery;

// 抽象路径：只有两个点（起点、终点），用于无真实 NavMesh 的场景。
struct FAbstractNavigationPath : public FNavigationPath
{
	typedef FNavigationPath Super;

	NAVIGATIONSYSTEM_API FAbstractNavigationPath();

	// 路径类型标签（用于 RTTI-like 判等），所有 FAbstractNavigationPath 共享同一个 Type 实例。
	static NAVIGATIONSYSTEM_API const FNavPathType Type;
};

// 空过滤器：全部接口返回默认值，配合 FAbstractNavigationPath 使用。
class FAbstractQueryFilter : public INavigationQueryFilterInterface
{
public:
	virtual void Reset() override {}
	virtual void SetAreaCost(uint8 AreaType, float Cost) override {}
	virtual void SetFixedAreaEnteringCost(uint8 AreaType, float Cost) override {}
	virtual void SetExcludedArea(uint8 AreaType) override {}
	virtual void SetAllAreaCosts(const float* CostArray, const int32 Count) override {}
	virtual void GetAllAreaCosts(float* CostArray, float* FixedCostArray, const int32 Count) const override {}
	virtual void SetBacktrackingEnabled(const bool bBacktracking) override {}
	virtual bool IsBacktrackingEnabled() const override { return false; }
	virtual float GetHeuristicScale() const override { return 1.f; }
	virtual bool IsEqual(const INavigationQueryFilterInterface* Other) const override { return true; }
	virtual void SetIncludeFlags(uint16 Flags) override {}
	virtual uint16 GetIncludeFlags() const override { return 0; }
	virtual void SetExcludeFlags(uint16 Flags) override {}
	virtual uint16 GetExcludeFlags() const override { return 0; }
	virtual FVector GetAdjustedEndLocation(const FVector& EndLocation) const override { return EndLocation; }
	NAVIGATIONSYSTEM_API virtual INavigationQueryFilterInterface* CreateCopy() const override;
};

// 占位 NavData：所有查询 API 返回"无效"；FindPath 仅返回起终点两点路径。
// 构造时把 FindPathImplementation 指向 FindPathAbstract 静态函数指针。
UCLASS(MinimalAPI)
class AAbstractNavData : public ANavigationData
{
	GENERATED_BODY()

public:
	NAVIGATIONSYSTEM_API AAbstractNavData(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// PostLoad：标记 RF_Transient 与 MarkAsGarbage，防止存盘与多实例
	NAVIGATIONSYSTEM_API virtual void PostLoad() override;

#if WITH_EDITOR
	// Begin AActor overrides
	virtual bool SupportsExternalPackaging() const override { return false; }
	// End AActor overrides
#endif

	// Begin ANavigationData overrides
	virtual void BatchRaycast(TArray<FNavigationRaycastWork>& Workload, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier = NULL) const override {};
	virtual bool FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return false;  };
	virtual bool FindOverlappingEdges(const FNavLocation& StartLocation, TConstArrayView<FVector> ConvexPolygon, TArray<FVector>& OutEdges, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return false; };
	virtual bool GetPathSegmentBoundaryEdges(const FNavigationPath& Path, const FNavPathPoint& StartPoint, const FNavPathPoint& EndPoint, const TConstArrayView<FVector> SearchArea, TArray<FVector>& OutEdges, const float MaxAreaEnterCost, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return false; }
	virtual FBox GetBounds() const override { return FBox(ForceInit); };
	virtual FNavLocation GetRandomPoint(FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return FNavLocation();  }
	virtual bool GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return false; }
	virtual bool GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return false; }
	virtual bool ProjectPoint(const FVector& Point, FNavLocation& OutLocation, const FVector& Extent, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override { return false; }
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, const FVector& Extent, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override {};
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, FSharedConstNavQueryFilter Filter = NULL, const UObject* Querier = NULL) const override {}
	virtual ENavigationQueryResult::Type CalcPathCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathCost, FSharedConstNavQueryFilter QueryFilter = NULL, const UObject* Querier = NULL) const override { return ENavigationQueryResult::Invalid; }
	virtual ENavigationQueryResult::Type CalcPathLength(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FSharedConstNavQueryFilter QueryFilter = NULL, const UObject* Querier = NULL) const override { return ENavigationQueryResult::Invalid; }
	virtual ENavigationQueryResult::Type CalcPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FVector::FReal& OutPathCost, FSharedConstNavQueryFilter QueryFilter = NULL, const UObject* Querier = NULL) const override { return ENavigationQueryResult::Invalid; }
	virtual bool DoesNodeContainLocation(NavNodeRef NodeRef, const FVector& WorldSpaceLocation) const override { return true; }
	virtual void OnNavAreaAdded(const UClass* NavAreaClass, int32 AgentIndex) override {}
	virtual void OnNavAreaRemoved(const UClass* NavAreaClass) override {};
	virtual bool IsNodeRefValid(NavNodeRef NodeRef) const override { return true; };
	// End ANavigationData overrides

	// 静态 FindPath：返回只包含起点 + 终点的 FAbstractNavigationPath。
	// 用作 ANavigationData::FindPathImplementation 函数指针。
	static NAVIGATIONSYSTEM_API FPathFindingResult FindPathAbstract(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query);
	// 始终返回 false（抽象实现不做真正测试）
	static NAVIGATIONSYSTEM_API bool TestPathAbstract(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes);
	// 旧签名：转调新签名，AdditionalResults 传 nullptr
	static NAVIGATIONSYSTEM_API bool RaycastAbstract(const ANavigationData* NavDataInstance, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier);
	// 始终返回 false，无射线遮挡
	static NAVIGATIONSYSTEM_API bool RaycastAbstract(const ANavigationData* NavDataInstance, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FNavigationRaycastAdditionalResults* AdditionalResults, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier);
};
