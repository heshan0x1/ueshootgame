// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// AbstractNavData.cpp —— AAbstractNavData 的占位实现。
//  - FindPathAbstract 返回"起点→终点"两点平凡路径；
//  - TestPath/Raycast 全部返回 false；
//  - PostLoad 里直接 MarkAsGarbage，保证同一世界只留一个实例。
// ============================================================================

#include "AbstractNavData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbstractNavData)

// 静态常量：FAbstractNavigationPath 的类型标签（用于 RTTI-like 判等）
const FNavPathType FAbstractNavigationPath::Type;

FAbstractNavigationPath::FAbstractNavigationPath()
{
	// 构造时记录自己的 Type，使 PathType == FAbstractNavigationPath::Type 成立
	PathType = FAbstractNavigationPath::Type;
}

// 过滤器拷贝：直接 new 一个新的空过滤器（因为本类无状态）
INavigationQueryFilterInterface* FAbstractQueryFilter::CreateCopy() const
{
	return new FAbstractQueryFilter();
}

// 构造函数：把 FindPath* / TestPath* / Raycast 的函数指针全部指向本类的 *Abstract 版本。
// 目的：保证寻路入口（ANavigationData::FindPath 里通过函数指针调度）能正常返回结果。
AAbstractNavData::AAbstractNavData(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bEditable = false;
	bListedInSceneOutliner = false;
#endif

	bCanBeMainNavData = false;
	bCanSpawnOnRebuild = false;

	// CDO 不做运行时绑定
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// 非虚函数指针：避免 FindPath 每次虚表查找（参见架构文档 4.4 节）
		FindPathImplementation = FindPathAbstract;
		FindHierarchicalPathImplementation = FindPathAbstract;

		TestPathImplementation = TestPathAbstract;
		TestHierarchicalPathImplementation = TestPathAbstract;

		RaycastImplementationWithAdditionalResults = RaycastAbstract;

		// 让 DefaultQueryFilter 持有 FAbstractQueryFilter 作为底层实现
		DefaultQueryFilter->SetFilterType<FAbstractQueryFilter>();
	}
}

// PostLoad：把自己标成 Garbage，防止同一世界里出现多个 AAbstractNavData
// （它只是占位符，多个实例没意义）
void AAbstractNavData::PostLoad()
{
	Super::PostLoad();
	SetFlags(RF_Transient);
	// marking as pending kill might seem an overkill, but one of the things 
	// this changes aims to achieve is to get rig of the excess number of 
	// AAbstractNavData instances. "There should be only one!"
	MarkAsGarbage();
}

// 构造"起点+终点"两点路径；若 Query 已传入 PathInstanceToFill 则复用该实例
// （典型的"重算"场景），否则新建一个 FAbstractNavigationPath。
FPathFindingResult AAbstractNavData::FindPathAbstract(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query)
{
	const ANavigationData* Self = Query.NavData.Get();
	if (Self == NULL)
	{
		return ENavigationQueryResult::Error;
	}

	FPathFindingResult Result;
	// 分支：是重算（复用 PathInstanceToFill）还是新建路径
	if (Query.PathInstanceToFill.IsValid())
	{
		Result.Path = Query.PathInstanceToFill;
		// 只重置 dirty 相关状态，保留 observer/PathType 等。详见 FNavigationPath::ResetForRepath
		Result.Path->ResetForRepath();
	}
	else
	{
		Result.Path = Self->CreatePathInstance<FAbstractNavigationPath>(Query);
	}

	// 填两个点：[Start, End]
	Result.Path->GetPathPoints().Reset();
	Result.Path->GetPathPoints().Add(FNavPathPoint(Query.StartLocation));
	Result.Path->GetPathPoints().Add(FNavPathPoint(Query.EndLocation));
	Result.Path->MarkReady();
	Result.Result = ENavigationQueryResult::Success;

	return Result;
}

// 抽象实现不做路径测试
bool AAbstractNavData::TestPathAbstract(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes)
{
	return false;
}

// 旧签名转调新签名
bool AAbstractNavData::RaycastAbstract(const ANavigationData* NavDataInstance, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier)
{
	return RaycastAbstract(NavDataInstance, RayStart, RayEnd, HitLocation, nullptr, QueryFilter, Querier);
}

// 抽象实现的 Raycast 始终无遮挡
bool AAbstractNavData::RaycastAbstract(const ANavigationData* NavDataInstance, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FNavigationRaycastAdditionalResults* AdditionalResults, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier)
{
	return false;
}

