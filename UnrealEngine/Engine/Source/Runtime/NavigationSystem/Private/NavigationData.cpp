// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationData.cpp —— ANavigationData 基类实现 + 辅助结构
// -----------------------------------------------------------------------------
// 本文件包含：
//   - FPathFindingQuery / FAsyncPathFindingQuery 的各种构造重载（含"按已有 Path
//     重算"的构造分支：会按 bUpdateStartPointOnRepath/EndPointOnRepath 覆盖
//     起止点，并沿用原 NavData 的 DefaultQueryFilter）。
//   - FSupportedAreaData 构造（NavArea 类 ↔ AreaID 的单项描述）。
//   - FNavigationPath::RemoveOverlappingPoints（仅这一 method；FNavigationPath
//     的其它方法在 NavigationPath.cpp）。
//   - ANavigationData 的全部基类实现：
//       * 生命周期：PostInitProperties → RequestRegistration → OnRegistered →
//         TickActor（观察 GoalActor/处理 RepathRequests）→ OnUnregistered →
//         UnregisterAndCleanUp；
//       * 生成器：ConditionalConstructGenerator / RebuildAll /
//         EnsureBuildCompletion / CancelBuild / TickAsyncBuild；
//       * 脏区下发：RebuildDirtyAreas / SetRebuildingSuspended
//         （挂起时累积到 SuspendedDirtyAreas，受 ai.navigation.MaxDirtyAreasCountWhileSuspended 限制）；
//       * 路径池：PurgeUnusedPaths / RegisterActivePath（线程安全，使用 ActivePathsLock）；
//       * NavArea 管理：OnNavAreaAdded/Removed/Event、GetNewAreaID 等，
//         维护 SupportedAreas（数组）与 AreaClassToIdMap（哈希）双向索引。
//
// 架构文档参考：第 4.1 节"启动与 World 初始化"、第 4.3 节"Recast Tile 生成"、
//              第 4.4 节"寻路（同步）"。
// =============================================================================

#include "NavigationData.h"

#include "AI/NavDataGenerator.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "AI/Navigation/NavAreaBase.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "AssetCompilingManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationSystem.h"
#include "VisualLogger/VisualLogger.h"
#include "Misc/ScopeLock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationData)

// set to NAVMESHVER_LANDSCAPE_HEIGHT at the moment of refactoring navigation
// code out of the engine module. No point in using RecastNavMesh versioning 
// for NavigationData
// NAVDATAVER_LATEST：ANavigationData 的"数据版本号"，仅在基类序列化中用到；
// 重构时从 Engine 模块里割出来，跟 Recast 的 NAVMESHVER_* 已无关联。
#define NAVDATAVER_LATEST 13	

namespace UE::Navigation::Private
{
	// CVar: ai.DestroyNavDataInCleanUpAndMarkPendingKill
	// 为 1 时 CleanUpAndMarkPendingKill 直接 Destroy Actor；为 0 时仅 MarkAsGarbage。
	// 关闭可以用于在某些调用路径（如 Serialize 期间）避免 Destroy 触发的二次重注册崩溃。
	static int32 bDestroyNavDataInCleanUpAndMarkPendingKill = 1;
	static FAutoConsoleVariableRef CVarDestroyNavDataInCleanUpAndMarkPendingKill(
		TEXT("ai.DestroyNavDataInCleanUpAndMarkPendingKill"),
		bDestroyNavDataInCleanUpAndMarkPendingKill,
		TEXT("If set to 1 NavData will be destroyed in CleanUpAndMarkPendingKill rather than being marked as garbage.\n"),
		ECVF_Default);

	// This prevents accumulating SuspendedDirtyAreas indefinitely and eventually running out of memory. That could happen if SetRebuildSuspended
	// is enabled for long periods of time. The limit cannot be too low because moving complex geometry made of hundreds or thousands of Actors
	// can quickly create several thousands of DirtyAreas.
	// Careful: Reaching this limit causes a Rebuild All for this navigation data (if it ever resumes generation).
	// CVar: ai.navigation.MaxDirtyAreasCountWhileSuspended
	// 暂停期间允许累积的最大 DirtyArea 条数；超过则清空并打标 bReachedSuspendedAreasMaxCount，
	// Resume 时强制 RebuildAll（代价大但比永远累积更可控）。
	static int32 MaxDirtyAreasCountWhileSuspended = 32768;
	static FAutoConsoleVariableRef CVarMaxDirtyAreasCountWhileSuspended(
		TEXT("ai.navigation.MaxDirtyAreasCountWhileSuspended"),
		MaxDirtyAreasCountWhileSuspended,
		TEXT("If a navigation data accumulates too many dirty areas because it's been indefinitely suspended, we'll stop accumulating them and rebuild the entire navigation data if it ever resumes building.\n"),
		ECVF_Default);
}

//----------------------------------------------------------------------//
// FPathFindingQuery
//----------------------------------------------------------------------//
// 构造 1：由"任意 Owner UObject"发起的查询。没有外部 Filter 时退化到 NavData 的 DefaultQueryFilter。
FPathFindingQuery::FPathFindingQuery(const UObject* InOwner, const ANavigationData& InNavData, const FVector& Start, const FVector& End, FSharedConstNavQueryFilter SourceQueryFilter, FNavPathSharedPtr InPathInstanceToFill, const FVector::FReal CostLimit, const bool bInRequireNavigableEndLocation) :
	FPathFindingQueryData(InOwner, Start, End, SourceQueryFilter, 0 /*InNavDataFlags*/, true /*bInAllowPartialPaths*/, CostLimit, bInRequireNavigableEndLocation),
	NavData(&InNavData), PathInstanceToFill(InPathInstanceToFill), NavAgentProperties(InNavData.GetConfig())
{
	if (!QueryFilter.IsValid() && NavData.IsValid())
	{
		QueryFilter = NavData->GetDefaultQueryFilter();
	}
}

// 构造 2：Owner 本身就实现了 INavAgentInterface。直接取用它的 FNavAgentProperties（不用走 NavData.GetConfig）。
FPathFindingQuery::FPathFindingQuery(const INavAgentInterface& InNavAgent, const ANavigationData& InNavData, const FVector& Start, const FVector& End, FSharedConstNavQueryFilter SourceQueryFilter, FNavPathSharedPtr InPathInstanceToFill, const FVector::FReal CostLimit, const bool bInRequireNavigableEndLocation) :
	FPathFindingQueryData(Cast<UObject>(&InNavAgent), Start, End, SourceQueryFilter, 0 /*InNavDataFlags*/, true /*bInAllowPartialPaths*/, CostLimit, bInRequireNavigableEndLocation),
	NavData(&InNavData), PathInstanceToFill(InPathInstanceToFill), NavAgentProperties(InNavAgent.GetNavAgentPropertiesRef())
{
	if (!QueryFilter.IsValid() && NavData.IsValid())
	{
		QueryFilter = NavData->GetDefaultQueryFilter();
	}
}

// 构造 3：基于一条"已有路径"的 Repath 请求。
// - 起点：若路径设置了 bUpdateStartPointOnRepath 且 SourceActor 仍有效，则用 SourceActor 当前位置；
//        否则沿用 QueryData 中的 StartLocation（通常是上次路径的当前段起点）。
// - 终点：若 bUpdateEndPointOnRepath 且 GoalActor 仍有效，则用 GoalActor 当前位置（支持 moving target）。
// - Filter/NavAgent：若外部没传，从 NavData 取默认值，保证 Repath 和原查询使用相同的代价模型。
FPathFindingQuery::FPathFindingQuery(FNavPathSharedRef PathToRecalculate, const ANavigationData* NavDataOverride) :
	FPathFindingQueryData(PathToRecalculate->GetQueryData()),
	NavData(NavDataOverride ? NavDataOverride : PathToRecalculate->GetNavigationDataUsed()), PathInstanceToFill(PathToRecalculate), NavAgentProperties(FNavAgentProperties::DefaultProperties)
{
	if (PathToRecalculate->ShouldUpdateStartPointOnRepath() && (PathToRecalculate->GetSourceActor() != nullptr))
	{
		const FVector NewStartLocation = PathToRecalculate->GetPathFindingStartLocation();
		if (FNavigationSystem::IsValidLocation(NewStartLocation))
		{
			StartLocation = NewStartLocation;
		}
	}

	if (PathToRecalculate->ShouldUpdateEndPointOnRepath() && (PathToRecalculate->GetGoalActor() != nullptr))
	{
		const FVector NewEndLocation = PathToRecalculate->GetGoalLocation();
		if (FNavigationSystem::IsValidLocation(NewEndLocation))
		{
			EndLocation = NewEndLocation;
		}
	}

	if (NavData.IsValid())
	{
		if (!QueryFilter.IsValid())
		{
			QueryFilter = NavData->GetDefaultQueryFilter();
		}

		NavAgentProperties = NavData->GetConfig();
	}
}

// 由启发式估价反推 A* 的 CostLimit：CostLimit = clamp(CostLimitFactor * HeuristicScale * Dist, MinimumCostLimit, +inf)。
// CostLimitFactor==FLT_MAX 时表示"不限制"。
// 使用场景：上层业务只知道一个"相对放大倍数 + 最低兜底"，用距离启发式快速换算出绝对代价上限，
// 避免 A* 在不可达目标时 burn 大量 Open 节点。
FVector::FReal FPathFindingQuery::ComputeCostLimitFromHeuristic(const FVector& StartPos, const FVector& EndPos, const FVector::FReal HeuristicScale, const FVector::FReal CostLimitFactor, const FVector::FReal MinimumCostLimit)
{
	if (CostLimitFactor == FLT_MAX)
	{
		return FLT_MAX;
	}
	else
	{
		const FVector::FReal OriginalHeuristicEstimate = HeuristicScale * FVector::Dist(StartPos, EndPos);
		return FMath::Clamp(CostLimitFactor * OriginalHeuristicEstimate, MinimumCostLimit, TNumericLimits<FVector::FReal>::Max());
	}
}

//----------------------------------------------------------------------//
// FAsyncPathFindingQuery
//----------------------------------------------------------------------//
uint32 FAsyncPathFindingQuery::LastPathFindingUniqueID = INVALID_NAVQUERYID;

FAsyncPathFindingQuery::FAsyncPathFindingQuery(const UObject* InOwner, const ANavigationData& InNavData, const FVector& Start, const FVector& End, const FNavPathQueryDelegate& Delegate, FSharedConstNavQueryFilter SourceQueryFilter, const FVector::FReal CostLimit)
: FPathFindingQuery(InOwner, InNavData, Start, End, SourceQueryFilter)
, QueryID(GetUniqueID())
, OnDoneDelegate(Delegate)
, Mode(EPathFindingMode::Regular)
{

}

FAsyncPathFindingQuery::FAsyncPathFindingQuery(const FPathFindingQuery& Query, const FNavPathQueryDelegate& Delegate, const EPathFindingMode::Type QueryMode)
: FPathFindingQuery(Query)
, QueryID(GetUniqueID())
, OnDoneDelegate(Delegate)
, Mode(QueryMode)
{

}

//----------------------------------------------------------------------//
// FSupportedAreaData
//----------------------------------------------------------------------//
// FSupportedAreaData 构造：把 UClass* 与 AreaID 打包存储；
// AreaClassName 用字符串保存是为了"脱离 UClass 依赖"（存盘不带 UClass 指针，加载时用名字反查）
FSupportedAreaData::FSupportedAreaData(TSubclassOf<UNavArea> NavAreaClass, int32 InAreaID)
	: AreaID(InAreaID), AreaClass(NavAreaClass)
{
	if (AreaClass != NULL)
	{
		AreaClassName = AreaClass->GetName();
	}
	else
	{
		AreaClassName = TEXT("Invalid");
	}
}

//----------------------------------------------------------------------//
// FNavigationPath
//----------------------------------------------------------------------//
// 去掉相邻重合（在容差内）的路径点。一般出现在：
//   - StringPull 生成路径后，起点/终点与首末多边形中心高度重合
//   - 自定义链接进出点拼接时两端坐标一致
// 算法：线性扫描；发现重合就 RemoveAt 当前项、不前进；否则 ++Index。
void FNavigationPath::RemoveOverlappingPoints(const FVector& Tolerance)
{
	// Remove overlapping points according to the tolerance.
	for (int32 Index = 0; Index+1 < PathPoints.Num();)
	{
		const FVector Delta = PathPoints[Index+1].Location - PathPoints[Index].Location;
		const bool bSameLocation = FMath::Abs(Delta.X) <= Tolerance.X && FMath::Abs(Delta.Y) <= Tolerance.Y && FMath::Abs(Delta.Z) <= Tolerance.Z;
		if (bSameLocation)
		{
			PathPoints.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
}

//----------------------------------------------------------------------//
// ANavigationData                                                                
//----------------------------------------------------------------------//
// 默认构造：
//   - bCanBeMainNavData=true：默认允许本实例被选为"主 NavData"
//   - RuntimeGeneration=LegacyGeneration：占位，待 PostInitProperties 时按旧字段 bRebuildAtRuntime_DEPRECATED 转成 Static/Dynamic
//   - 创建一个 Static 的 SceneComponent 作为 Root：有了 Root 才能监听 Actor 坐标变化（见 ARecastNavMesh::PostRegisterAllComponents）
//   - DefaultQueryFilter：永远至少有一份可用的空 filter，避免 Query 空指针
ANavigationData::ANavigationData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bEnableDrawing(false)
	, bForceRebuildOnLoad(false)
	, bAutoDestroyWhenNoNavigation(false)
	, bCanBeMainNavData(true)
	, bCanSpawnOnRebuild(true)
	, RuntimeGeneration(ERuntimeGenerationType::LegacyGeneration) //TODO: set to a valid value once bRebuildAtRuntime_DEPRECATED is removed
	, DataVersion(NAVDATAVER_LATEST)
	, FindPathImplementation(NULL)
	, FindHierarchicalPathImplementation(NULL)
	, bRegistered(false)
	, bRebuildingSuspended(false)
#if WITH_EDITORONLY_DATA
	, bIsBuildingOnLoad(false)
#endif
	, NavDataUniqueID(GetNextUniqueID())
{
	PrimaryActorTick.bCanEverTick = true;
	bNetLoadOnClient = false;
	SetCanBeDamaged(false);
	DefaultQueryFilter = MakeShareable(new FNavigationQueryFilter());
	ObservedPathsTickInterval = 0.5;

	// by giving NavigationData a root component we can detect changes to 
	// actor's location and react to it (see ARecastNavMesh::PostRegisterAllComponents)
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}

// 分配全局唯一 16 位 ID。INVALID_NAVDATA=0 保留给"未注册"，所以从 Increment 开始。
uint16 ANavigationData::GetNextUniqueID()
{
	static FThreadSafeCounter StaticID(INVALID_NAVDATA);
	return IntCastChecked<uint16>(StaticID.Increment());
}

// UObject 初始化末尾钩子：
// - CDO（类默认对象）：仅迁移旧字段 bRebuildAtRuntime_DEPRECATED → RuntimeGeneration
// - 普通实例：按规则决定是否在客户端加载，向 NavSystem 请求注册（见 RequestRegistration），
//   并构造可视化组件（仅 UE_ENABLE_DEBUG_DRAWING 下，Shipping 包不产生绘制开销）。
void ANavigationData::PostInitProperties()
{
	Super::PostInitProperties();

	if (!IsValidChecked(this))
	{
		return;
	}

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		if (RuntimeGeneration == ERuntimeGenerationType::LegacyGeneration)
		{
			RuntimeGeneration = bRebuildAtRuntime_DEPRECATED ? ERuntimeGenerationType::Dynamic : ERuntimeGenerationType::Static;
		}
	}
	else
	{
		bNetLoadOnClient = FNavigationSystem::ShouldLoadNavigationOnClient(*this);
		RequestRegistration();
#if UE_ENABLE_DEBUG_DRAWING
		RenderingComp = ConstructRenderingComponent();
#endif // UE_ENABLE_DEBUG_DRAWING
	}
}

// 组件都初始化完成后的最终兜底：
// 如果发现"这个 World 根本没 NavSystem 也不该在客户端加载"等情况，按 bAutoDestroyWhenNoNavigation
// 配置自毁。多人客户端不需要服务器端的 NavData 时，这里会把它 MarkPendingKill，避免浪费内存。
void ANavigationData::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	
	UWorld* MyWorld = GetWorld();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(MyWorld);

	if (bAutoDestroyWhenNoNavigation && (MyWorld == nullptr ||
		(MyWorld->GetNetMode() != NM_Client && NavSys == nullptr) ||
		(MyWorld->GetNetMode() == NM_Client && !bNetLoadOnClient)))
	{
		UE_VLOG_UELOG(this, LogNavigation, Log, TEXT("Marking %s as PendingKill due to %s"), *GetName()
			, !MyWorld ? TEXT("No World") : (MyWorld->GetNetMode() == NM_Client ? TEXT("not creating navigation on clients") : TEXT("missing navigation system")));
		CleanUpAndMarkPendingKill();
	}
}

// 关卡加载完成后：重新构造渲染组件（编辑器重加载场景时需要），再次请求注册。
void ANavigationData::PostLoad() 
{
	Super::PostLoad();

	InstantiateAndRegisterRenderingComponent();

	bNetLoadOnClient = FNavigationSystem::ShouldLoadNavigationOnClient(*this);
	RequestRegistration();
}

// 请求把自己注册进 NavSystem。注意：调用"Deferred"而不是立即注册——此时 NavSystem 可能还没
// 完成 GatherNavigationBounds，立即注册会走错分支；延迟到 NavSystem 下一个 Tick 统一处理。
void ANavigationData::RequestRegistration()
{
	if (IsRegistered() == false
		&& HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			NavSys->RequestRegistrationDeferred(*this);
		}
	}
}

// TickActor：本 NavData 的每帧 Tick。完成三件事：
//   1) PurgeUnusedPaths：清理 ActivePaths 中已无 shared owner 的 FNavPathWeakPtr（弱引用池维护）
//   2) ObservedPathsTickInterval 周期处理 ObservedPaths：对每条观察中的路径调 TickPathObservation，
//      若 GoalActor 偏离 TetherDistance 就生成一条 FNavPathRecalculationRequest 到 RepathRequests。
//   3) 批量处理 RepathRequests：对每条请求调用 FindPath，成功则 DoneUpdating 广播事件，失败则 RePathFailed。
// MaxProcessedRequests=1000：一帧最多跑 1000 条重算；超出部分推迟到下帧，且打印 error（通常说明业务层
// 每帧生成重算请求的节奏失衡，需要排查）。
void ANavigationData::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);

	// 1) 回收 ActivePaths 里的失效弱引用
	PurgeUnusedPaths();

	INC_DWORD_STAT_BY(STAT_Navigation_ObservedPathsCount, ObservedPaths.Num());

	// 2) 定期处理"观察路径"：每 ObservedPathsTickInterval 秒一次
	if (NextObservedPathsTickInSeconds >= 0.f)
	{
		NextObservedPathsTickInSeconds -= DeltaTime;
		if (NextObservedPathsTickInSeconds <= 0.f)
		{
			RepathRequests.Reserve(ObservedPaths.Num());

			// 反向遍历便于 RemoveAtSwap
			for (int32 PathIndex = ObservedPaths.Num() - 1; PathIndex >= 0; --PathIndex)
			{
				if (ObservedPaths[PathIndex].IsValid())
				{
					FNavPathSharedPtr SharedPath = ObservedPaths[PathIndex].Pin();
					FNavigationPath* Path = SharedPath.Get();
					// TickPathObservation：根据 GoalActor 当前位置与 GoalActorLastLocation 的偏差决定是否请求重算
					EPathObservationResult::Type Result = Path->TickPathObservation();
					switch (Result)
					{
					case EPathObservationResult::NoLongerObserving:
						// 观察已停止（GoalActor 被销毁等）：从 ObservedPaths 里摘掉
						ObservedPaths.RemoveAtSwap(PathIndex, EAllowShrinking::No);
						break;

					case EPathObservationResult::NoChange:
						// do nothing
						break;

					case EPathObservationResult::RequestRepath:
						// 目标偏离超阈值：加入 RepathRequests 排队
						RepathRequests.Add(FNavPathRecalculationRequest(SharedPath, ENavPathUpdateType::GoalMoved));
						break;
					
					default:
						check(false && "unhandled EPathObservationResult::Type in ANavigationData::TickActor");
						break;
					}
				}
				else
				{
					// 路径 shared 引用已归零：直接移除
					ObservedPaths.RemoveAtSwap(PathIndex, EAllowShrinking::No);
				}
			}

			if (ObservedPaths.Num() > 0)
			{
				NextObservedPathsTickInSeconds = ObservedPathsTickInterval;
			}
		}
	}

	// 3) 批量处理 Repath 请求
	if (RepathRequests.Num() > 0)
	{
		double TimeStamp = GetWorldTimeStamp();
		const UWorld* World = GetWorld();

		// @todo batch-process it!

		// 单帧重算上限；超过则打 error 并推迟（下帧再次出现）
		const int32 MaxProcessedRequests = 1000;

		// make a copy of path requests and reset (remove up to MaxProcessedRequests) from navdata's array
		// this allows storing new requests in the middle of loop (e.g. used by meta path corrections)
		// 拷一份 WorkQueue 并清空原 RepathRequests，这样循环内部再次 AddUnique 新请求不会影响本轮；
		// meta 路径（分段路径）的内部续作会往 RepathRequests 新增。
		TArray<FNavPathRecalculationRequest> WorkQueue(RepathRequests);
		if (WorkQueue.Num() > MaxProcessedRequests)
		{
			UE_VLOG(this, LogNavigation, Error, TEXT("Too many repath requests! (%d/%d)"), WorkQueue.Num(), MaxProcessedRequests);

			// 仅本轮处理前 MaxProcessedRequests 条，其余保留到下轮
			WorkQueue.RemoveAt(MaxProcessedRequests, WorkQueue.Num() - MaxProcessedRequests);
			RepathRequests.RemoveAt(0, MaxProcessedRequests);
		}
		else
		{
			RepathRequests.Reset();
		}

		for (int32 Idx = 0; Idx < WorkQueue.Num(); Idx++)
		{
			FNavPathRecalculationRequest& RecalcRequest = WorkQueue[Idx];

			// check if it can be updated right now
			FNavPathSharedPtr PinnedPath = RecalcRequest.Path.Pin();
			if (PinnedPath.IsValid() == false)
			{
				continue;
			}

			// Querier 实现了 ShouldPostponePathUpdates（Agent 正被 scripted 强制移动等场景）：
			// 把请求回塞，留给下一帧再试
			const UObject* PathQuerier = PinnedPath->GetQuerier();
			const INavAgentInterface* PathNavAgent = Cast<const INavAgentInterface>(PathQuerier);
			if (PathNavAgent && PathNavAgent->ShouldPostponePathUpdates())
			{
				RepathRequests.Add(RecalcRequest);
				continue;
			}

			// 构造 Repath 专用 Query（会从 PinnedPath 里摘出 Start/End/Filter/NavAgent），然后原地填充 PinnedPath
			FPathFindingQuery Query(PinnedPath.ToSharedRef());
			const FPathFindingResult Result = FindPath(Query.NavAgentProperties, Query.SetPathInstanceToUpdate(PinnedPath));

			// update time stamp to give observers any means of telling if it has changed
			PinnedPath->SetTimeStamp(TimeStamp);

			// partial paths are still valid and can change to full path when moving goal gets back on navmesh
			// 部分路径也算成功——目标可能是走出了 NavMesh 的 moving actor，路径先保留到最近可达点
			if (Result.IsSuccessful() || Result.IsPartial())
			{
				PinnedPath->UpdateLastRepathGoalLocation();
				PinnedPath->DoneUpdating(RecalcRequest.Reason);
				if (RecalcRequest.Reason == ENavPathUpdateType::NavigationChanged)
				{
					// 由"底层导航数据变化"触发的重算：重新登记为 ActivePath（该路径可能因 Invalidate 被摘掉了）
					RegisterActivePath(PinnedPath);
				}
			}
			else
			{
				PinnedPath->RePathFailed();
			}
		}
	}
}

#if WITH_EDITOR
void ANavigationData::RerunConstructionScripts()
{
	Super::RerunConstructionScripts();

	InstantiateAndRegisterRenderingComponent();
}
#endif

// OnRegistered：NavSystem 把本 NavData 纳入 NavDataSet 成功后回调。
// - 渲染组件按需创建
// - bRegistered 置位
// - 构造 NavDataGenerator（若派生类支持）——之后 Tile 构建就可以开始了
void ANavigationData::OnRegistered() 
{ 
	InstantiateAndRegisterRenderingComponent();

	bRegistered = true;
	ConditionalConstructGenerator();
}

void ANavigationData::OnUnregistered()
{
	bRegistered = false;
}

// 若 RenderingComp 不在/失效，就 Rename 老的（解除命名冲突）然后用 ConstructRenderingComponent 创建新实例。
// 仅在 UE_ENABLE_DEBUG_DRAWING 下生效（Shipping 包绘制相关代码被完全剔除）。
void ANavigationData::InstantiateAndRegisterRenderingComponent()
{
#if UE_ENABLE_DEBUG_DRAWING
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (IsThisNotNull(this, "ANavigationData::InstantiateAndRegisterRenderingComponent") && IsValidChecked(this) && !IsValid(RenderingComp))
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const bool bRootIsRenderComp = (RenderingComp == RootComponent);
		if (RenderingComp)
		{
			// rename the old rendering component out of the way
			RenderingComp->Rename(NULL, NULL, REN_DontCreateRedirectors | REN_ForceGlobalUnique | REN_DoNotDirty | REN_NonTransactional);
		}

		RenderingComp = ConstructRenderingComponent();

		UWorld* World = GetWorld();
		if (World && World->bIsWorldInitialized && RenderingComp)
		{
			RenderingComp->RegisterComponent();
		}

		if (bRootIsRenderComp)
		{
			RootComponent = RenderingComp;
		}
	}
#endif // UE_ENABLE_DEBUG_DRAWING
}

// 清理 ActivePaths 中 shared 引用已归零的弱指针。必须在 GameThread 执行。
// 注意：ActivePaths 可能被异步寻路线程 RegisterActivePath 写入，所以遍历时需要 ActivePathsLock 锁保护。
void ANavigationData::PurgeUnusedPaths()
{
	check(IsInGameThread());

	// Paths can be registered from async pathfinding thread 
	// while unused paths are purged in main thread (actor tick)
	UE::TScopeLock PathLock(ActivePathsLock);

	const int32 Count = ActivePaths.Num();
	// 反向遍历保证 RemoveAtSwap 不影响后续索引
	for (int32 PathIndex = Count - 1; PathIndex >= 0; --PathIndex)
	{
		FNavPathWeakPtr* WeakPathPtr = &ActivePaths[PathIndex];
		if (WeakPathPtr->IsValid() == false)
		{
			ActivePaths.RemoveAtSwap(PathIndex, EAllowShrinking::No);
		}
	}
}

// 把一条 FNavPathSharedPtr 登记为 ActivePath（弱引用）。
// 线程：可能在主线程（CreatePathInstance）或异步寻路工作线程上调用——必须加锁。
void ANavigationData::RegisterActivePath(FNavPathSharedPtr SharedPath)
{
	// Paths can be registered from main thread and async pathfinding thread
	UE::TScopeLock PathLock(ActivePathsLock);
	ActivePaths.Add(SharedPath);
}

#if WITH_EDITOR
void ANavigationData::PostEditUndo()
{
	// make sure that rendering component is not pending kill before trying to register all components
	InstantiateAndRegisterRenderingComponent();

	Super::PostEditUndo();

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys)
	{
		if (IsPendingKillPending())
		{
			NavSys->UnregisterNavData(this);
		}
		else
		{
			NavSys->RequestRegistrationDeferred(*this);
		}
	}
}
#endif // WITH_EDITOR

// Agent 等价性比较：NavDataConfig.IsEquivalent 会按 AgentRadius/Height/StepHeight/Slope 等字段做"近似相等"
bool ANavigationData::DoesSupportAgent(const FNavAgentProperties& AgentProps) const
{
	return NavDataConfig.IsEquivalent(AgentProps);
}

void ANavigationData::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAndCleanUp();
	Super::EndPlay(EndPlayReason);
}

void ANavigationData::Destroyed()
{
	UnregisterAndCleanUp();
	Super::Destroyed();
}
// 从 NavSystem 摘除 + 派生类清理。即便未注册也要调 CleanUp（释放内部资源）。
void ANavigationData::UnregisterAndCleanUp()
{
	if (bRegistered)
	{
		bRegistered = false;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			NavSys->UnregisterNavData(this);
		}
	}

	// Cleanup is not tied to the registration state.
	CleanUp();
}

void ANavigationData::CleanUp()
{
	bRegistered = false;
}

// 空实现：ANavigationData 的世界偏移由 NavSystem 统一处理；派生类按需 override（如 ARecastNavMesh 需要挪 Tile）
void ANavigationData::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	// do nothing, will be handled by NavigationSystem
}

// 彻底下线本 NavData：先 CleanUp 派生资源，再根据 CVar 选择 Destroy 或 MarkAsGarbage。
// 注意：Serialize 期间会误入此路径（World=nullptr），此时直接 Destroy 会导致 NavSys 注册流程崩溃，
// 所以必须判 IsValid(GetWorld()) 再 Destroy。
void ANavigationData::CleanUpAndMarkPendingKill()
{
	CleanUp();

	/* Need to check if the world is valid since, when this is called from Serialize, the World won't be set and Destroy will do nothing, 
	 * in which case it will crash when it tries to register with the NavSystem in UNavigationSystemV1::ProcessRegistrationCandidates. */
	if (UE::Navigation::Private::bDestroyNavDataInCleanUpAndMarkPendingKill && IsValid(GetWorld()))
	{
		Destroy();
	}
	else
	{
		// 降级路径：隐藏、从网络 Actor 表里摘除、打 garbage 标；由 GC 异步回收
		SetActorHiddenInGame(true);

		if (UWorld* World = GetWorld())
		{
			// This part is not thread-safe and should only happen on the GT...
			check(IsInGameThread());
			World->RemoveNetworkActor(this);
		}
		MarkAsGarbage();
		MarkComponentsAsGarbage();
	}
}

bool ANavigationData::SupportsRuntimeGeneration() const
{
	return false;
}

bool ANavigationData::SupportsStreaming() const
{
	return false;
}

// 基类不构造任何 Generator；派生类（如 ARecastNavMesh）会 override：
//   - 检查 RuntimeGeneration 配置
//   - 创建 FRecastNavMeshGenerator 等
// 在 OnRegistered 和 OnNavigationBoundsChanged 等多处被触发，所以名为 "Conditional"。
void ANavigationData::ConditionalConstructGenerator()
{
}

// 完全重建：
// 1) LoadBeforeGeneratorRebuild：派生类的前置钩子（例如等待流式加载完成）
// 2) FinishAllCompilation：确保 AssetCompiler 已把所有异步烘焙资源跑完，否则会漏几何
// 3) PostLoadPreRebuild：再给派生类一个最后调整 NavData 的机会
// 4) ConditionalConstructGenerator：重新构造一个干净的 Generator
// 5) Generator->RebuildAll：真正开始 Tile 全量重生成
// WITH_EDITOR：非加载期的重建意味着 navmesh 数据改了，需要 MarkPackageDirty 让编辑器提示保存
void ANavigationData::RebuildAll()
{
	const double LoadTime = FPlatformTime::Seconds();
	LoadBeforeGeneratorRebuild();
	FAssetCompilingManager::Get().FinishAllCompilation();
	UE_LOG(LogNavigationDataBuild, Display, TEXT("   %s load time: %.2fs"), ANSI_TO_TCHAR(__FUNCTION__), (FPlatformTime::Seconds() - LoadTime));

	PostLoadPreRebuild();
	
	ConditionalConstructGenerator(); //recreate generator
	
	if (NavDataGenerator.IsValid())
	{
#if WITH_EDITOR		
		if (!IsBuildingOnLoad())
		{
			MarkPackageDirty();
		}
#endif // WITH_EDITOR

		NavDataGenerator->RebuildAll();
	}
}

// 阻塞等待当前构建完成（flushing）。编辑器保存/PIE 启动前常用。
void ANavigationData::EnsureBuildCompletion()
{
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->EnsureBuildCompletion();
	}
}

// 取消正在进行中的异步构建。不阻塞。
void ANavigationData::CancelBuild()
{
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
	}
}

// 导航边界（ANavMeshBoundsVolume 集合）发生变化时回调：
// 若还没 Generator 就先造一个，然后交给 Generator 重新计算需要生成/移除的 Tile。
void ANavigationData::OnNavigationBoundsChanged()
{
	// Create generator if it wasn't yet
	if (NavDataGenerator.Get() == nullptr)
	{
		ConditionalConstructGenerator();
	}
	
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->OnNavigationBoundsChanged();
	}
}

// 每帧驱动异步构建。当 Generator 是"时间切片"模式时，NavSystem 会按需决定是否分给本 NavData
// 足够的时间窗口；因此 DeltaSeconds 并不总等于游戏帧时间，而是当前帧分配给本 Generator 的预算。
void ANavigationData::TickAsyncBuild(float DeltaSeconds)
{
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->TickAsyncBuild(DeltaSeconds);
	}
}

// RebuildDirtyAreas：把外部（DirtyAreasController）推来的脏区下发给 Generator。
// 如果正处于 Suspended 状态：
//   - 已达到 MaxDirtyAreasCountWhileSuspended：再也不累积，等 Resume 时 RebuildAll
//   - 还有容量：追加到 SuspendedDirtyAreas（Resume 时会一次性再次调用本函数）
// 正常状态：直接透传给 NavDataGenerator。
void ANavigationData::RebuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas)
{
	if (bRebuildingSuspended)
	{
		if (bReachedSuspendedAreasMaxCount)
		{
			return; // Already reached max count
		}

		const int32 TotalDirtyAreasCount = SuspendedDirtyAreas.Num() + DirtyAreas.Num();
		if (TotalDirtyAreasCount > UE::Navigation::Private::MaxDirtyAreasCountWhileSuspended)
		{
			// Rebuilding has been suspended for too long and we're accumulating too many dirty areas.
			// Let's just stop accumulating them and remember that the whole navigation data might now be invalid.
			// 超阈值保护：清空 SuspendedDirtyAreas、记一个"稍后需要 RebuildAll"的标记
			UE_VLOG_UELOG(this, LogNavigation, Display, TEXT("%s reached ai.navigation.MaxDirtyAreasCountWhileSuspended (%d). Ignoring dirty areas until rebuilding is unsuspended."),
				*GetName(),
				UE::Navigation::Private::MaxDirtyAreasCountWhileSuspended);

			bReachedSuspendedAreasMaxCount = true;
			SuspendedDirtyAreas.Empty();
		}
		else
		{
			SuspendedDirtyAreas.Append(DirtyAreas);
		}
	}
	else
	{
		if (NavDataGenerator.IsValid())
		{
			NavDataGenerator->RebuildDirtyAreas(DirtyAreas);
		}
	}
}

// SetRebuildingSuspended：挂起/恢复 Dirty→Generator 的下发。
// 典型用法：关卡流式加载期间调 SetRebuildingSuspended(true)，避免一次加载触发大量 Tile 增量重建；
// 加载结束再 SetRebuildingSuspended(false) 一次性 flush。
// 注意：
//   - 当前正在跑的 Tile 构建不会被挂起，本函数只影响后续的 DirtyArea 下发
//   - 挂起期间累积超过 MaxDirtyAreasCountWhileSuspended 会在 Resume 时强制 RebuildAll（代价较大）
void ANavigationData::SetRebuildingSuspended(const bool bNewSuspend)
{
	if (bRebuildingSuspended != bNewSuspend)
	{
		bRebuildingSuspended = bNewSuspend;
		UE_VLOG_UELOG(this, LogNavigation, Verbose, TEXT("%s nav generation %s")
			, *GetName(), bNewSuspend ? TEXT("SUSPENDED") : TEXT("ACTIVE"));

		if (bNewSuspend == false)
		{
			// 恢复路径
			if (bReachedSuspendedAreasMaxCount)
			{
				// 曾经超限：整份 NavData 可能已不一致，只能 RebuildAll
				UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("%s resuming nav generation after MaxDirtyAreasCountWhileSuspended was reached. This will rebuild the entire navigation data."), *GetName());

				// We stopped tracking dirty areas so we'll need to rebuild all.
				bReachedSuspendedAreasMaxCount = false;
				RebuildAll();
			}
			else if (SuspendedDirtyAreas.Num() > 0)
			{
				UE_VLOG_UELOG(this, LogNavigation, Verbose, TEXT("%s resuming nav generation with %d dirty areas")
					, *GetName(), SuspendedDirtyAreas.Num());

				// resuming the generation so we need to utilize SuspendedDirtyAreas and clean it
				// 一次性回放累积的脏区；RebuildDirtyAreas 在非 Suspended 状态会直接透传给 Generator
				RebuildDirtyAreas(SuspendedDirtyAreas);
				SuspendedDirtyAreas.Empty();
			}
		}
	}
}

// 返回本 NavData 关注的所有 NavMeshBoundsVolume AABB。实际由 NavSystem 集中管理并按 NavData 过滤。
TArray<FBox> ANavigationData::GetNavigableBounds() const
{
	TArray<FBox> Result;
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<const UNavigationSystemV1>(GetWorld());
	
	if (NavSys)
	{
		// @note this has been switched over from calling GetNavigationBounds
		// to get navigable bounds relevant to this one nav data instance
		// This implements the original intension of the function
		NavSys->GetNavigationBoundsForNavData(*this, Result);
	}
	
	return Result;
}
	
// 指定关卡内的导航边界子集（用于流式关卡场景：只关心 InLevel 里的 NavMeshBoundsVolume）
TArray<FBox> ANavigationData::GetNavigableBoundsInLevel(ULevel* InLevel) const
{
	TArray<FBox> Result;
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<const UNavigationSystemV1>(GetWorld());

	if (NavSys)
	{
		NavSys->GetNavigationBoundsForNavData(*this, Result, InLevel);
	}

	return Result;
}

void ANavigationData::DrawDebugPath(FNavigationPath* Path, const FColor PathColor, UCanvas* Canvas, const bool bPersistent, const float LifeTime, const uint32 NextPathPointIndex) const
{
	Path->DebugDraw(this, PathColor, Canvas, bPersistent, LifeTime, NextPathPointIndex);
}

double ANavigationData::GetWorldTimeStamp() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

// OnNavAreaAdded：NavSystem 通知"新注册了一个 NavArea 类"。流程：
// 1) 拒绝 MetaArea（它们不是最终生效的类）和"不支持本 Agent"的 Area
// 2) 若该类已经在 SupportedAreas 里（按字符串比对）：只刷新 UClass 指针与 AreaClassToIdMap，保持 AreaID 不变
// 3) 否则检查 MaxSupportedAreas 上限（Recast 里为 ~64）——超限则 error
// 4) GetNewAreaID 分配最小未使用 ID，登记到 SupportedAreas + AreaClassToIdMap 双向映射
void ANavigationData::OnNavAreaAdded(const UClass* NavAreaClass, int32 AgentIndex)
{
	// check if area can be added
	const UNavArea* DefArea = GetDefault<UNavArea>(const_cast<UClass*>(NavAreaClass));
	const bool bIsMetaArea = DefArea != nullptr && DefArea->IsMetaArea();
	if (!DefArea || bIsMetaArea || !DefArea->IsSupportingAgent(AgentIndex))
	{
		UE_VLOG_UELOG(this, LogNavigation, Verbose, TEXT("%s discarded area %s (valid:%s meta:%s validAgent[%d]:%s)"),
			*GetName(), *GetNameSafe(NavAreaClass),
			DefArea ? TEXT("yes") : TEXT("NO"),
			bIsMetaArea ? TEXT("YES") : TEXT("no"),
			AgentIndex, (DefArea && DefArea->IsSupportingAgent(AgentIndex)) ? TEXT("yes") : TEXT("NO"));
		return;
	}

	// check if area is already on supported list
	FString AreaClassName = NavAreaClass->GetName();
	for (int32 i = 0; i < SupportedAreas.Num(); i++)
	{
		if (SupportedAreas[i].AreaClassName == AreaClassName)
		{
			SupportedAreas[i].AreaClass = NavAreaClass;
			AreaClassToIdMap.Add(NavAreaClass, SupportedAreas[i].AreaID);
			UE_VLOG_UELOG(this, LogNavigation, Verbose, TEXT("%s: updated area %s with ID %d"), *GetFullName(), *AreaClassName, SupportedAreas[i].AreaID);
			return;
		}
	}

	// try adding new one
	const int32 MaxSupported = GetMaxSupportedAreas();
	if (SupportedAreas.Num() >= MaxSupported)
	{
		UE_VLOG_UELOG(this, LogNavigation, Error, TEXT("%s can't support area %s - limit reached! (%d)"), *GetName(), *AreaClassName, MaxSupported);
		return;
	}

	FSupportedAreaData NewAgentData;
	NewAgentData.AreaClass = NavAreaClass;
	NewAgentData.AreaClassName = AreaClassName;
	NewAgentData.AreaID = GetNewAreaID(NavAreaClass);
	SupportedAreas.Add(NewAgentData);
	AreaClassToIdMap.Add(NavAreaClass, NewAgentData.AreaID);

	UE_VLOG_UELOG(this, LogNavigation, Verbose, TEXT("%s registered area %s with ID %d"), *GetName(), *AreaClassName, NewAgentData.AreaID);
}

// NavSystem 统一的 Area 变更入口：分发 Added/Removed，然后调一次 OnNavAreaChanged 让派生类刷新索引/过滤器
void ANavigationData::OnNavAreaEvent(const UClass* NavAreaClass, ENavAreaEvent::Type Event)
{
	if (Event == ENavAreaEvent::Registered)
	{
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<const UNavigationSystemV1>(GetWorld());
		const int32 AgentIndex = NavSys->GetSupportedAgentIndex(this);

		OnNavAreaAdded(NavAreaClass, AgentIndex);
	}
	else // Unregistered
	{
		OnNavAreaRemoved(NavAreaClass);
	}

	OnNavAreaChanged();
}

void ANavigationData::OnNavAreaRemoved(const UClass* NavAreaClass)
{
	for (int32 i = 0; i < SupportedAreas.Num(); i++)
	{
		if (SupportedAreas[i].AreaClass == NavAreaClass)
		{
			AreaClassToIdMap.Remove(NavAreaClass);
			SupportedAreas.RemoveAt(i);
			break;
		}
	}
}

void ANavigationData::OnNavAreaChanged()
{
	// empty in base class
}

// 在 NavSystem 一次性注入所有已知 Area 类；适用于 NavData 刚被注册时的批量同步
void ANavigationData::ProcessNavAreas(const TSet<const UClass*>& AreaClasses, int32 AgentIndex)
{
	for (const UClass* AreaClass : AreaClasses)
	{
		OnNavAreaAdded(AreaClass, AgentIndex);
	}

	OnNavAreaChanged();
}

// 找一个最小的未被占用的整数 ID。
// 循环不变量：[0..TestId) 都已被占用。遇到第一个未占用即返回。
// 实现成本：O(N^2) —— 但 N <= MaxSupportedAreas（Recast 最多 ~64），可以接受。
int32 ANavigationData::GetNewAreaID(const UClass* AreaClass) const
{
	int TestId = 0;
	while (TestId < SupportedAreas.Num())
	{
		const bool bIsTaken = IsAreaAssigned(TestId);
		if (bIsTaken)
		{
			TestId++;
		}
		else
		{
			break;
		}
	}

	return TestId;
}

// 按 AreaID → UClass 线性反查。调用频率不高（大多走 AreaClassToIdMap 正向查询），无需建反向哈希。
const UClass* ANavigationData::GetAreaClass(int32 AreaID) const
{
	for (int32 i = 0; i < SupportedAreas.Num(); i++)
	{
		if (SupportedAreas[i].AreaID == AreaID)
		{
			return SupportedAreas[i].AreaClass;
		}
	}

	return NULL;
}

bool ANavigationData::IsAreaAssigned(int32 AreaID) const
{
	for (int32 i = 0; i < SupportedAreas.Num(); i++)
	{
		if (SupportedAreas[i].AreaID == AreaID)
		{
			return true;
		}
	}

	return false;
}

int32 ANavigationData::GetAreaID(const UClass* AreaClass) const
{
	const int32* PtrId = AreaClassToIdMap.Find(AreaClass);
	return PtrId ? *PtrId : INDEX_NONE;
}

// 打开/关闭本 NavData 的可视化。MarkComponentsRenderStateDirty 会触发 RenderingComp 重建 SceneProxy
void ANavigationData::SetNavRenderingEnabled(bool bEnable)
{
	if (bEnableDrawing != bEnable)
	{
		bEnableDrawing = bEnable;
		MarkComponentsRenderStateDirty();
	}
}

void ANavigationData::UpdateCustomLink(const INavLinkCustomInterface* CustomLink)
{
	// no implementation for abstract class
}

FSharedConstNavQueryFilter ANavigationData::GetQueryFilter(TSubclassOf<UNavigationQueryFilter> FilterClass) const
{
	return QueryFilters.FindRef(FilterClass);
}

void ANavigationData::StoreQueryFilter(TSubclassOf<UNavigationQueryFilter> FilterClass, FSharedConstNavQueryFilter NavFilter)
{
	QueryFilters.Add(FilterClass, NavFilter);
}

void ANavigationData::RemoveQueryFilter(TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	QueryFilters.Remove(FilterClass);
}

// 内存使用统计：ActivePaths（加锁读）+ SupportedAreas + QueryFilters + AreaClassToIdMap + Generator。
// 仅用于 'nav.memstat' 等调试命令；非性能路径。
uint32 ANavigationData::LogMemUsed() const
{
	SIZE_T ActivePathsMemSize = 0;
	{
		// Paths can be registered from async pathfinding thread
		// while logging is requested on main thread (console command)
		UE::TScopeLock PathLock(ActivePathsLock);
		ActivePathsMemSize = ActivePaths.GetAllocatedSize();
	}

	const uint32 MemUsed = IntCastChecked<uint32>(ActivePathsMemSize + SupportedAreas.GetAllocatedSize() +
		QueryFilters.GetAllocatedSize() + AreaClassToIdMap.GetAllocatedSize());

	UE_VLOG_UELOG(this, LogNavigation, Display, TEXT("%s: ANavigationData: %u\n    self: %d"), *GetName(), MemUsed, sizeof(ANavigationData));

	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->LogMemUsed();
	}

	return MemUsed;
}

// SetConfig：同步 NavAgentProperties 与 NavDataConfig。
// 调用场景：NavData 注册到 NavSystem 时被传入"确定的 Agent 参数"；初始化用，不适合运行期频繁切换。
// 注意：派生类（ARecastNavMesh）可能 override 以触发 Tile 重建——因为 cell/tile 尺寸可能变了。
void ANavigationData::SetConfig(const FNavDataConfig& Src)
{
	SetNavAgentProperties(Src);
	NavDataConfig = Src;
}