// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityQuery.h"
#include "MassDebugger.h"
#include "MassEntityManager.h"
#include "MassArchetypeData.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"
#include "VisualLogger/VisualLogger.h"
#include "Async/ParallelFor.h"
#include "Containers/UnrealString.h"
#include "MassProcessor.h"
#include "MassEntityTrace.h"
#include "MassRequirementAccessDetector.h"
#if WITH_MASSENTITY_DEBUG
#include "MassDebugLogging.h"
#endif // WITH_MASSENTITY_DEBUG

namespace UE::Mass::Tweakables
{
	/**
	 * Controls whether ParallelForEachEntityChunk actually performs ParallelFor operations.
	 * If `false` the call is passed to the regular ForEachEntityChunk call.
	 *
	 * 【全局开关】通过 cvar `mass.AllowQueryParallelFor` 控制。
	 *  关闭时，ParallelForEachEntityChunk 会无条件回退到串行的 ForEachEntityChunk。
	 *  调试性能问题、复现并发 bug 时常用。
	 */
	bool bAllowParallelExecution = true;

	namespace
	{
		static FAutoConsoleVariableRef AnonymousCVars[] = {
			{	TEXT("mass.AllowQueryParallelFor"), bAllowParallelExecution, TEXT("Controls whether EntityQueries are allowed to utilize ParallelFor construct"), ECVF_Cheat }
		};
	}
}

//-----------------------------------------------------------------------------
// FScopedEntityQueryContext
//-----------------------------------------------------------------------------
/**
 * 【作用域辅助类】把 query 与 ExecutionContext 的"绑定/解绑"封装成 RAII。
 *  构造时：
 *    - PushQuery：让 ExecutionContext 知道当前正在执行的是哪个 query（用于 fragment view 解析等）。
 *    - CacheSubsystemRequirements：把 query 声明的 subsystem 需求绑定到 context（即从 World 拉对应 USubsystem* 指针）。
 *      若有 subsystem 拉不到 → 视为"必需的子系统不可用"，bSubsystemRequirementsCached=false，外层应直接放弃执行。
 *    - ScopedAccessDetector：调试构建中开启读写冲突检测（让 RequirementAccessDetector 锁住相关数据）。
 *  析构时：
 *    - ClearExecutionData / FlushDeferred：刷新延迟命令、清理 per-query 数据。
 *    - PopQuery：恢复 ExecutionContext 的 query 栈。
 */
struct FScopedEntityQueryContext
{
	FScopedEntityQueryContext(FMassEntityQuery& InQuery, FMassExecutionContext& ExecutionContext)
		: Query(InQuery), CachedExecutionContext(ExecutionContext)
		, ScopedAccessDetector(InQuery)
	{
		CachedExecutionContext.PushQuery(InQuery);

		bSubsystemRequirementsCached = CachedExecutionContext.CacheSubsystemRequirements(InQuery);
	}

	~FScopedEntityQueryContext()
	{
		if (IsSuccessfullySetUp())
		{
			CachedExecutionContext.ClearExecutionData();
			CachedExecutionContext.FlushDeferred();
		}
		CachedExecutionContext.PopQuery(Query);
	}

	// 是否 subsystem 全部拿到。否则外层应直接 return，避免在缺资源情况下执行。
	bool IsSuccessfullySetUp() const
	{
		return bSubsystemRequirementsCached;
	}

	FMassEntityQuery& Query;
	FMassExecutionContext& CachedExecutionContext;
	UE::Mass::Debug::FScopedRequirementAccessDetector ScopedAccessDetector;
	bool bSubsystemRequirementsCached;
};

//-----------------------------------------------------------------------------
// FMassEntityQuery
//-----------------------------------------------------------------------------
// 【构造：绑定到 processor】常规路径：在 UMassProcessor 的 ConfigureQueries 阶段调用。
FMassEntityQuery::FMassEntityQuery(UMassProcessor& Owner)
{
	UE_TRACE_MASS_QUERY_CREATED()
	RegisterWithProcessor(Owner);
}

// 【构造：仅绑定 EntityManager】不挂 processor 的"游离 query"，常用于 subsystem/工具代码。
FMassEntityQuery::FMassEntityQuery(const TSharedPtr<FMassEntityManager>& EntityManager)
	: FMassFragmentRequirements(EntityManager)
{
	UE_TRACE_MASS_QUERY_CREATED()
}

// 【便利构造】列出一组 fragment，全部按 ReadWrite + All 添加为需求；适合一行声明出"我要这几个 fragment"。
FMassEntityQuery::FMassEntityQuery(const TSharedRef<FMassEntityManager>& EntityManager, std::initializer_list<UScriptStruct*> InitList)
	: FMassFragmentRequirements(EntityManager)
{
	UE_TRACE_MASS_QUERY_CREATED()
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassEntityQuery::FMassEntityQuery(const TSharedRef<FMassEntityManager>& EntityManager, TConstArrayView<const UScriptStruct*> InitList)
	: FMassFragmentRequirements(EntityManager)
{
	UE_TRACE_MASS_QUERY_CREATED()
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

// 【绑定到 processor】告诉 processor "本 query 由你管理"，让它能在 ExecutionContext 类型校验中通过。
void FMassEntityQuery::RegisterWithProcessor(UMassProcessor& Owner)
{
	UE_TRACE_MASS_QUERY_REGISTERED_TO_PROCESSOR(&Owner)

	ExpectedContextType = EMassExecutionContextType::Processor;
	Owner.RegisterQuery(*this);
#if WITH_MASSENTITY_DEBUG
	bRegistered = true;
#endif // WITH_MASSENTITY_DEBUG
}

// =============================================================================
// CacheArchetypes —— 缓存匹配的 archetype 集合
// =============================================================================
// 【算法概览】两阶段失效检测 + 增量收集：
//   1) 检查 EntityManager 是否变化（指针 hash）或 Requirements 是否变化（HasIncrementalChanges）：
//        是 → 完全清空缓存，并对 Requirements 排序（SortRequirements）。
//   2) 检查 EntityManager.ArchetypeDataVersion 是否变化：
//        是 → 增量遍历"上次更新后新出现的" archetype，对每个跑 DoesArchetypeMatchRequirements / 自定义 Match。
//        否 → 直接返回（zero work）。
//   3) 对每个新加入 ValidArchetypes 的 archetype：预算它在本 query 下的 fragment 列索引映射，
//        分别处理 entity / chunk / shared / const-shared 四类。
//   4) 若设了 GroupBy → 调 SortArchetypes 重排 OrderedArchetypeIndices；否则填顺序索引。
//
// 【为什么这么设计】archetype 集合在帧间几乎稳定（实体增删不会改 archetype，只有添加/移除组件才会）。
// 每帧重算太浪费，所以用版本号增量更新；首次执行成本最高，之后大多数帧只是版本号比对。
void FMassEntityQuery::CacheArchetypes()
{
	check(CachedEntityManager);

	// 用 EntityManager 指针 hash 当"绑定身份"标记
	const uint32 InEntityManagerHash = PointerHash(CachedEntityManager.Get());

	// Do an incremental update if the last updated archetype data version is different 
	// 【判定 1】只要版本号不一致，就需要"至少做增量"
    bool bUpdateArchetypes = CachedEntityManager->GetArchetypeDataVersion() != LastUpdatedArchetypeDataVersion;

	// Force a full update if the entity system changed or if the requirements changed
	// 【判定 2】EntityManager 换了，或者用户改过 Requirements → 必须做"完整"重算
	if (EntitySubsystemHash != InEntityManagerHash || HasIncrementalChanges())
	{
		bUpdateArchetypes = true;
		EntitySubsystemHash = InEntityManagerHash;
		// 清空所有缓存以便从 0 开始扫描所有 archetype
		ValidArchetypes.Reset();
		OrderedArchetypeIndices.Reset();
		CachedGroupIDs.Reset();
		LastUpdatedArchetypeDataVersion = 0;
		ArchetypeFragmentMapping.Reset();

		if (HasIncrementalChanges())
		{
			ConsumeIncrementalChangesCount();
			if (CheckValidity())
			{
				// Requirements 排序很重要：执行时绑定 view 的顺序必须与 ArchetypeFragmentMapping 计算时一致
				SortRequirements();
			}
			else
			{
				// 不合法的需求集合（如同一 fragment 既 All 又 None）→ 跳过本次缓存，记一条错误日志
				bUpdateArchetypes = false;
				UE_VLOG_UELOG(CachedEntityManager->GetOwner(), LogMass, Error, TEXT("FMassEntityQuery::CacheArchetypes: requirements not valid: %s"), *FMassDebugger::GetRequirementsDescription(*this));
			}
		}
	}
	
	// Process any new archetype that is newer than the LastUpdatedArchetypeDataVersion
	// 【主循环】扫描"上次更新后新增的" archetype；如果是完整重算则 LastUpdatedArchetypeDataVersion=0，等价于全扫描。
	if (bUpdateArchetypes)
	{
		TArray<FMassArchetypeHandle> NewValidArchetypes;
#if WITH_EDITOR
		if (bHasArchetypeMatchOverride)
		{
			// 编辑器分支：用用户自定义谓词替代 DoesArchetypeMatchRequirements
			CachedEntityManager->ForEachArchetype(static_cast<int32>(LastUpdatedArchetypeDataVersion), TNumericLimits<int32>::Max() /*last*/, [this, &NewValidArchetypes](const FMassEntityManager& EntityManager, const FMassArchetypeHandle& Handle)
			{
				const FMassArchetypeCompositionDescriptor& Composition = EntityManager.GetArchetypeComposition(Handle);
				if (ArchetypeMatchOverride.Match(&ArchetypeMatchOverride.Data, Composition))
				{
					NewValidArchetypes.Add(Handle);
				}
			});
		}
		else
#endif
		{
			// 标准分支：让 EntityManager 拿当前 query 去比对所有 archetype（内部用 fragment composition mask 做位运算）
			CachedEntityManager->GetMatchingArchetypes(*this, NewValidArchetypes, LastUpdatedArchetypeDataVersion);
		}
		// 把"我已经看过到这个版本"记下来，下次再调本函数时只看更新的部分
		LastUpdatedArchetypeDataVersion = CachedEntityManager->GetArchetypeDataVersion();
		if (NewValidArchetypes.Num())
		{
			const int32 FirstNewArchetype = ValidArchetypes.Num();
			ValidArchetypes.Append(NewValidArchetypes);

			// 【为新 archetype 预算"需求序号 → fragment 列索引"的映射】
			// 这步是 query 性能的关键：运行时不用再做 hash map 查找，直接拿索引即可。
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass RequirementsBinding")
			const TConstArrayView<FMassFragmentRequirementDescription> LocalRequirements = GetFragmentRequirements();
			ArchetypeFragmentMapping.AddDefaulted(NewValidArchetypes.Num());
			for (int i = FirstNewArchetype; i < ValidArchetypes.Num(); ++i)
			{
				FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ValidArchetypes[i]);
				// 1) entity-level fragment 映射（最常见）
				ArchetypeData.GetRequirementsFragmentMapping(LocalRequirements, ArchetypeFragmentMapping[i].EntityFragments);
				// 2) chunk-level fragment 映射（如 ChunkLOD 之类的 chunk 共享数据）
				if (ChunkFragmentRequirements.Num())
				{
					ArchetypeData.GetRequirementsChunkFragmentMapping(ChunkFragmentRequirements, ArchetypeFragmentMapping[i].ChunkFragments);
				}
				// 3) const shared fragment 映射
				if (ConstSharedFragmentRequirements.Num())
				{
					ArchetypeData.GetRequirementsConstSharedFragmentMapping(ConstSharedFragmentRequirements, ArchetypeFragmentMapping[i].ConstSharedFragments);
				}
				// 4) shared fragment 映射
				if (SharedFragmentRequirements.Num())
				{
					ArchetypeData.GetRequirementsSharedFragmentMapping(SharedFragmentRequirements, ArchetypeFragmentMapping[i].SharedFragments);
				}
			}

			// 维护 OrderedArchetypeIndices：要么按分组排序，要么是 0..N-1
			if (IsGrouping())
			{
				SortArchetypes(FirstNewArchetype);
			}
			else
			{
				BuildOrderedArchetypeIndices(FirstNewArchetype);
			}
		}
	}
}

// 【对一组 collection 串行调用单 collection 路径】
void FMassEntityQuery::ForEachEntityChunkInCollections(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
	, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	for (const FMassArchetypeEntityCollection& EntityCollection : EntityCollections)
	{
		ForEachEntityChunk(EntityCollection, ExecutionContext, ExecuteFunction);
	}
}

// 【单 EntityCollection 路径】把 collection 临时放到 ExecutionContext 上，然后走通用 ForEachEntityChunk。
// 通用版会通过 GetEntityCollection().IsEmpty() 走"显式 collection"分支。
void FMassEntityQuery::ForEachEntityChunk(const FMassArchetypeEntityCollection& EntityCollection, FMassExecutionContext& ExecutionContext
	, const FMassExecuteFunction& ExecuteFunction)
{
	// mz@todo I don't like that we're copying data here.
	ExecutionContext.SetEntityCollection(EntityCollection);
	ForEachEntityChunk(ExecutionContext, ExecuteFunction);
	ExecutionContext.ClearEntityCollection();
}

// =============================================================================
// ForEachEntityChunk —— 主执行入口
// =============================================================================
// 【三条执行路径】
//   A) Requirements 为空 + 显式 EntityCollection: 直接对 collection 中指定的 ranges 调用 ExecuteFunction。
//      （空 query 唯一支持的形态：observer 等"我已经知道要处理哪些实体"的场景）
//   B) 有 Requirements + 显式 EntityCollection: 验证 collection 的 archetype 在 ValidArchetypes 中，再处理。
//   C) 有 Requirements + 无 EntityCollection: 遍历 OrderedArchetypeIndices 全量执行（最常见）。
//
// 【调用约束】
//   - 必须先 ConfigureQueries 阶段声明完所有 Requirements，运行期再 Add 会改 IncrementalChanges 触发完整重算。
//   - processor 路径下需 RegisterWithProcessor 过；ExecutionType 必须与 ExpectedContextType 匹配。
void FMassEntityQuery::ForEachEntityChunk(FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH()

	// 【前置校验 1】query 与 context 必须共享同一个 EntityManager
	checkf(CachedEntityManager == ExecutionContext.GetSharedEntityManager() && CachedEntityManager.IsValid()
		, TEXT("FMassEntityQuery needs to be initialized with a valid EntityManager and the execution context has to come from the same manager"));

#if WITH_MASSENTITY_DEBUG
	// 【前置校验 2】processor 路径下必须 RegisterWithProcessor 过；context 类型也要匹配
	checkf(ExecutionContext.GetExecutionType() == ExpectedContextType && (ExpectedContextType == EMassExecutionContextType::Local || bRegistered)
		, TEXT("ExecutionContextType mismatch, make sure all the queries run as part of processor execution are registered with some processor with a FMassEntityQuery::RegisterWithProcessor call"));
#endif

	// RAII：把 query 推入 ExecutionContext，绑定 subsystem，作用域结束自动 PopQuery + Flush deferred 命令
	FScopedEntityQueryContext ScopedQueryContext(*this, ExecutionContext);

	if (ScopedQueryContext.IsSuccessfullySetUp() == false)
	{
		// required subsystems are not available, bail out.
		// 必需的 subsystem 拿不到（比如 World 还没 init 完）→ 静默跳过本次执行
		return;
	}

	if (FMassFragmentRequirements::IsEmpty())
	{
		// 【路径 A】空 query：必须配合显式 EntityCollection 使用
		if (ensureMsgf(ExecutionContext.GetEntityCollection().IsEmpty() == false, TEXT("Using empty queries is only supported in combination with Entity Collections that explicitly indicate entities to process")))
		{
			static const FMassQueryRequirementIndicesMapping EmptyMapping;

			// 直接对 collection 的 ranges 执行；ChunkCondition 仍可用
			const FMassArchetypeHandle& ArchetypeHandle = ExecutionContext.GetEntityCollection().GetArchetype();
			FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
			ArchetypeData.ExecuteFunction(ExecutionContext, ExecuteFunction
				, EmptyMapping
				, ExecutionContext.GetEntityCollection().GetRanges()
				, ChunkCondition);
		}
	}
	else
	{
		// note that the following function will usually only resort to verifying that the data is up to date by
			// checking the version number. In rare cases when it would result in non trivial cost we actually
			// do need those calculations.
		// 【刷新 archetype 缓存】常见情况下只是版本号比较，几乎零开销
		CacheArchetypes();

		// if there's a chunk collection set by the external code - use that
		if (ExecutionContext.GetEntityCollection().IsEmpty() == false)
		{
			// 【路径 B】外部指定了 EntityCollection：先验证它的 archetype 是否在 ValidArchetypes 中
			const FMassArchetypeHandle& ArchetypeHandle = ExecutionContext.GetEntityCollection().GetArchetype();
			const int32 ArchetypeIndex = ValidArchetypes.Find(ArchetypeHandle);

			// if given ArchetypeHandle cannot be found in ValidArchetypes then it doesn't match the query's requirements
			if (ArchetypeIndex == INDEX_NONE)
			{
				// archetype 不匹配 → 静默放弃（observer 场景下属于正常情况，故只是 verbose 日志）
				UE_VLOG_UELOG(CachedEntityManager->GetOwner(), LogMass, Verbose, TEXT("Attempted to execute FMassEntityQuery with an incompatible Archetype: %s. Note that this is fine for observers.")
					, *FMassDebugger::GetArchetypeRequirementCompatibilityDescription(*this, ArchetypeHandle));

				return;
			}

			// 在 context 上绑定本 query 的所有 fragment 需求（subsystem 已经在 ScopedContext 里绑过）
			ExecutionContext.ApplyFragmentRequirements(*this);

			FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
			
			UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH_REPORT_ARCHETYPE(ArchetypeData)
			
			// 用 archetype 缓存的 mapping + collection 提供的 ranges 调 ExecuteFunction
			ArchetypeData.ExecuteFunction(ExecutionContext, ExecuteFunction
				, GetRequirementsMappingForArchetype(ArchetypeHandle)
				, ExecutionContext.GetEntityCollection().GetRanges()
				, ChunkCondition);
		}
		else
		{
			// it's important to set requirements after caching archetypes due to that call potentially sorting the requirements and the order is relevant here.
			// 【路径 C】最常见：遍历所有匹配 archetype。
			// 注意：必须在 CacheArchetypes 之后再 ApplyFragmentRequirements，因为前者可能对 Requirements 重排过。
			ExecutionContext.ApplyFragmentRequirements(*this);

			// note that this checkSlow is here on purpose, for debugging purposes, not data verification purposes.
			checkSlow(OrderedArchetypeIndices.Num() == ValidArchetypes.Num());
			// 按排序后的顺序遍历每个 archetype
			for (const int32 ArchetypeIndex : OrderedArchetypeIndices)
			{
				const FMassArchetypeHandle& ArchetypeHandle = ValidArchetypes[ArchetypeIndex];
				FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
				
				UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH_REPORT_ARCHETYPE(ArchetypeData)

				// 把"该 archetype 的所有 chunk"全部喂给 ExecuteFunction（ChunkCondition 在这里逐 chunk 评估）
				ArchetypeData.ExecuteFunction(ExecutionContext, ExecuteFunction, ArchetypeFragmentMapping[ArchetypeIndex], ChunkCondition);
				// 清掉本 archetype 绑定的 fragment view（避免下一个 archetype 拿到错的 view）
				ExecutionContext.ClearFragmentViews(*this);
			}
		}
	}
}

// =============================================================================
// ForEachEntityChunk(Limiter) —— 带"分批续接"语义的版本
// =============================================================================
// 【关键特性】
//   - Limiter 既是输入也是输出：上次执行到哪 archetype/chunk，本次从那继续。
//   - 一旦 EntityCountRemaining<=0 立刻停止（但当前 chunk 一定会处理完，保证 chunk 是处理粒度）。
//   - 支持环绕：处理到末尾自动绕回开头，直到回到起始 archetype 或处理够数。
//   - 不支持显式 EntityCollection（断言保护）。
//
// 【典型场景】"每帧只算 100 个实体的 AI，下一帧接着算"。
void FMassEntityQuery::ForEachEntityChunk(FMassExecutionContext& ExecutionContext, UE::Mass::FExecutionLimiter& Limiter, const FMassExecuteFunction& ExecuteFunction)
{
	UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH()

	checkf(CachedEntityManager == ExecutionContext.GetSharedEntityManager() && CachedEntityManager.IsValid()
		, TEXT("FMassEntityQuery needs to be initialized with a valid EntityManager and the execution context has to come from the same manager"));

#if WITH_MASSENTITY_DEBUG
	checkf(ExecutionContext.GetExecutionType() == ExpectedContextType && (ExpectedContextType == EMassExecutionContextType::Local || bRegistered)
		, TEXT("ExecutionContextType mismatch, make sure all the queries run as part of processor execution are registered with some processor with a FMassEntityQuery::RegisterWithProcessor call"));
#endif

	FScopedEntityQueryContext ScopedQueryContext(*this, ExecutionContext);

	if (ScopedQueryContext.IsSuccessfullySetUp() == false)
	{
		// required subsystems are not available, bail out.
		return;
	}

	// FExecutionLimiter is not supported for use with manually set entity collections.
	// 限制器版本不支持显式 collection，断言以防止误用
	checkf(FMassFragmentRequirements::IsEmpty() == false, TEXT("FExecutionLimiter is not supported for use with manually set entity collections."));
	checkf(ExecutionContext.GetEntityCollection().IsEmpty(), TEXT("FExecutionLimiter is not supported for use with manually set entity collections."));

	// note that the following function will usually only resort to verifying that the data is up to date by
	// checking the version number. In rare cases when it would result in non trivial cost we actually
	// do need those calculations.
	CacheArchetypes();

	// it's important to set requirements after caching archetypes due to that call potentially sorting the requirements and the order is relevant here.
	ExecutionContext.ApplyFragmentRequirements(*this);

	// note that this checkSlow is here on purpose, for debugging purposes, not data verification purposes.
	const int32 NumArchetypes = ValidArchetypes.Num();
	checkSlow(OrderedArchetypeIndices.Num() == NumArchetypes);

	if (NumArchetypes == 0)
	{
		return;
	}

	// 限制器的位置如果非法（首次/超界），重置到 0
	if (Limiter.ChunkIndex < 0 || Limiter.ArchetypeIndex < 0 || Limiter.ArchetypeIndex >= NumArchetypes)
	{
		Limiter.ArchetypeIndex = 0;
		Limiter.ChunkIndex = 0;
	}
	// 记下起始位置，用来检测"绕一圈回到起点"
	const int32 StartingChunkIndex = Limiter.ChunkIndex;
	const int32 StartingArchetypeIndex = Limiter.ArchetypeIndex;
	Limiter.EntityCountRemaining = Limiter.EntityLimit;
	Limiter.MaxChunkIndex = TNumericLimits<int32>::Max();
	bool bLooped = false;
	bool bDone = false;

	// 【主循环】每次处理一个 archetype（可能从中间某个 chunk 开始）
	do
	{
		const int32 ArchetypeIndex = OrderedArchetypeIndices[Limiter.ArchetypeIndex];
		const FMassArchetypeHandle& ArchetypeHandle = ValidArchetypes[ArchetypeIndex];
		FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);

		if (bLooped && StartingChunkIndex != 0)
		{
			// archetype was started past the first chunk so if we loop we need to process it again
			// but stop before the starting chunk is reprocessed
			// 【绕回到起点 archetype 时的边界处理】只处理前面那部分（避免重复处理起点 chunk）
			Limiter.MaxChunkIndex = StartingChunkIndex;
			bDone = true;
		}

		UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH_REPORT_ARCHETYPE(ArchetypeData)

		// 将 Limiter 透传进去，让 ArchetypeData 在 chunk 步进时检查剩余 entity 数
		ArchetypeData.ExecuteFunction(ExecutionContext, ExecuteFunction, ArchetypeFragmentMapping[ArchetypeIndex], ChunkCondition, &Limiter);
		ExecutionContext.ClearFragmentViews(*this);
		
		if (Limiter.ChunkIndex >= ArchetypeData.GetChunkCount())
		{
			// all chunks in the archetype were processed, so move to the next
			// 当前 archetype 处理完了，移到下一个；末尾绕回到 0
			Limiter.ChunkIndex = 0;
			if (++Limiter.ArchetypeIndex >= NumArchetypes)
			{
				Limiter.ArchetypeIndex = 0;
			}
			// 检测是否已经绕回到起点 archetype
			if (Limiter.ArchetypeIndex == StartingArchetypeIndex)
			{
				if (StartingChunkIndex == 0)
				{
					// no need to process the starting archetype again because the initial pass hit all the chunks
					// 起点是 chunk 0，等于已经全部处理完，直接结束
					bDone = true;
				}
				bLooped = true;
			}
		}
		
	} while (Limiter.EntityCountRemaining > 0 && !bDone);
}

// =============================================================================
// ParallelForEachEntityChunkInCollection —— 并行 + collection 入口
// =============================================================================
// 把每个 collection 当成一个 ParallelFor job；job 内部再走单 collection 的并行路径。
// 注意：拷贝 ExecutionContext 是为了让每个 worker 拥有独立的本地 context。
void FMassEntityQuery::ParallelForEachEntityChunkInCollection(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
	, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction
	, const EParallelExecutionFlags Flags)
{
	// 全局禁并行 + 没强制 → 退化为串行
	if (UE::Mass::Tweakables::bAllowParallelExecution == false && !EnumHasAnyFlags(Flags, EParallelExecutionFlags::Force))
	{
		ForEachEntityChunkInCollections(EntityCollections, ExecutionContext, ExecuteFunction);
		return;
	}

	ParallelFor(EntityCollections.Num(), [this, &ExecutionContext, &ExecuteFunction, &EntityCollections, Flags](const int32 JobIndex)
	{
		FMassExecutionContext LocalExecutionContext = ExecutionContext; 
		LocalExecutionContext.SetEntityCollection(EntityCollections[JobIndex]);
		ParallelForEachEntityChunk(LocalExecutionContext, ExecuteFunction, Flags);
	});
}

// =============================================================================
// ParallelForEachEntityChunk —— 并行执行入口（核心）
// =============================================================================
// 【整体流程】
//   1) 检查并行允许 / grouping 退化（grouping 隐含顺序约束 → 不能并行）
//   2) 通过 ScopedQueryContext 绑定 query 到 context，并拉取 subsystem
//   3) 收集所有 (archetype, ArchetypeIndex, EntityRange) 三元组到 Jobs[]
//      - 空 query + 显式 collection: 仅 collection 内的 ranges
//      - 有 query + 显式 collection: 验证后取 collection 的 ranges
//      - 有 query + 无 collection:   全 archetype × 全 ranges
//   4) ParallelFor 投递 jobs：
//      - bAllowParallelCommands=true → 每个 worker 一个 lazy 创建的 FMassCommandBuffer，结束时 MoveAppend 合并
//      - 否则不带 command buffer（要求回调不下达延迟命令）
void FMassEntityQuery::ParallelForEachEntityChunk(FMassExecutionContext& ExecutionContext
	, const FMassExecuteFunction& ExecuteFunction, const EParallelExecutionFlags Flags)
{
	if (UE::Mass::Tweakables::bAllowParallelExecution == false && !EnumHasAnyFlags(Flags, EParallelExecutionFlags::Force))
	{
		// 全局禁并行 + 没强制 → 串行
		ForEachEntityChunk(ExecutionContext, ExecuteFunction);
		return;
	}
	else if (IsGrouping())
	{
		// grouping 隐含跨 archetype 的顺序约束，无法并行；记一条 warning 后退化
		UE_VLOG_UELOG(CachedEntityManager->GetOwner(), LogMass, Warning, TEXT("Calling %hs is not supported for grouping queries yet. Calling regular ForEachEntityChunk instead."), __FUNCTION__);
		ForEachEntityChunk(ExecutionContext, ExecuteFunction);
		return;
	}

	checkf(CachedEntityManager == ExecutionContext.GetSharedEntityManager() && CachedEntityManager.IsValid()
		, TEXT("FMassEntityQuery needs to be initialized with a valid EntityManager and the execution context has to come from the same manager"));

#if WITH_MASSENTITY_DEBUG
	checkf(ExecutionContext.GetExecutionType() == ExpectedContextType && (ExpectedContextType == EMassExecutionContextType::Local || bRegistered)
		, TEXT("ExecutionContextType mismatch, make sure all the queries run as part of processor execution are registered with some processor with a FMassEntityQuery::RegisterWithProcessor call"));
#endif

	FScopedEntityQueryContext ScopedQueryContext(*this, ExecutionContext);

	if (ScopedQueryContext.IsSuccessfullySetUp() == false)
	{
		// required subsystems are not available, bail out.
		return;
	}

	// 【一个并行 job 的描述】每个 job 对应一个 (archetype × range) 组合，被 ParallelFor 派发到 worker
	struct FChunkJob
	{
		FMassArchetypeData& Archetype;
		const int32 ArchetypeIndex;
		const FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange;
	};
	TArray<FChunkJob> Jobs;

	if (FMassFragmentRequirements::IsEmpty())
	{
		// 路径 A：空 query + 显式 collection
		if (ensureMsgf(ExecutionContext.GetEntityCollection().IsEmpty() == false, TEXT("Using empty queries is only supported in combination with Entity Collections that explicitly indicate entities to process")))
		{
			const FMassArchetypeHandle& ArchetypeHandle = ExecutionContext.GetEntityCollection().GetArchetype();
			FMassArchetypeData& ArchetypeRef = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
			for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange : ExecutionContext.GetEntityCollection().GetRanges())
			{
				// ArchetypeIndex=INDEX_NONE 表示"没有缓存的 fragment 映射"，运行时走慢路径
				Jobs.Add({ ArchetypeRef, INDEX_NONE, EntityRange });
			}
		}
	}
	else
	{

		// note that the following function will usualy only resort to verifying that the data is up to date by
		// checking the version number. In rare cases when it would result in non trivial cost we actually
		// do need those calculations.
		CacheArchetypes();

		// if there's a chunk collection set by the external code - use that
		if (ExecutionContext.GetEntityCollection().IsEmpty() == false)
		{
			// 路径 B：有 query + 显式 collection
			const FMassArchetypeHandle& ArchetypeHandle = ExecutionContext.GetEntityCollection().GetArchetype();
			const int32 ArchetypeIndex = ValidArchetypes.Find(ArchetypeHandle);

			// if given ArchetypeHandle cannot be found in ValidArchetypes then it doesn't match the query's requirements
			if (ArchetypeIndex == INDEX_NONE)
			{
				UE_VLOG_UELOG(CachedEntityManager->GetOwner(), LogMass, Verbose, TEXT("Attempted to execute FMassEntityQuery with an incompatible Archetype: %s. Note that this is fine for observers.")
					, *FMassDebugger::GetArchetypeRequirementCompatibilityDescription(*this, ExecutionContext.GetEntityCollection().GetArchetype()));

				return;
			}

			ExecutionContext.ApplyFragmentRequirements(*this);

			FMassArchetypeData& ArchetypeRef = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
			for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange : ExecutionContext.GetEntityCollection().GetRanges())
			{
				Jobs.Add({ ArchetypeRef, ArchetypeIndex, EntityRange });
			}
		}
		else
		{
			// 路径 C：有 query + 无 collection（最常见）
			ExecutionContext.ApplyFragmentRequirements(*this);
			for (int ArchetypeIndex = 0; ArchetypeIndex < ValidArchetypes.Num(); ++ArchetypeIndex)
			{
				FMassArchetypeHandle& ArchetypeHandle = ValidArchetypes[ArchetypeIndex];
				FMassArchetypeData& ArchetypeRef = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
				// 把整个 archetype 当成一个"全量 collection"，拿出它的 chunk ranges
				const FMassArchetypeEntityCollection AsEntityCollection(ArchetypeHandle);
				for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange : AsEntityCollection.GetRanges())
				{
					Jobs.Add({ ArchetypeRef, ArchetypeIndex, EntityRange });
				}
			}
		}
	}

	if (Jobs.Num())
	{
		// AutoBalance → ParallelFor 用 Unbalanced（任务队列方式），否则按 worker 平均分块
		const EParallelForFlags ParallelForFlags = 
			EnumHasAnyFlags(Flags, EParallelExecutionFlags::AutoBalance) ? EParallelForFlags::Unbalanced : EParallelForFlags::None;
		if (bAllowParallelCommands)
		{
			// 【FTaskContext】每个 worker 持有一个延迟创建的 CommandBuffer。
			// 关键：必须在"实际执行 worker 的线程上"创建 buffer，否则 thread-local id 不一致。
			// ParallelFor 内部可能让 worker 跨线程，因此每次 GetCommandBuffer 还要 ForceUpdateCurrentThreadID。
			struct FTaskContext
			{
				FTaskContext() = default;

				TSharedPtr<FMassCommandBuffer> GetCommandBuffer()
				{
					if (!CommandBuffer)
					{
						// lazily creating the command buffer to ensure we create it in the same thread it's going to be used in
						CommandBuffer = MakeShared<FMassCommandBuffer>();
					}
					else
					{
						// force-updating the thread ID because ParallelFor can move workers between threads.
						CommandBuffer->ForceUpdateCurrentThreadID();
					}
					return CommandBuffer;
				}
			private:
				TSharedPtr<FMassCommandBuffer> CommandBuffer;
			};

			TArray<FTaskContext> TaskContext;

			// 【ParallelForWithTaskContext】每个 worker 拥有自己的 FTaskContext，避免争用同一个 buffer。
			ParallelForWithTaskContext(TaskContext, Jobs.Num(), [this, &ExecutionContext, &ExecuteFunction, &Jobs](FTaskContext& TaskContext, const int32 JobIndex)
				{
					// 在 worker 线程上构造一个本地 ExecutionContext，使用 worker 自己的 command buffer
					FMassExecutionContext LocalExecutionContext(ExecutionContext, *this, TaskContext.GetCommandBuffer());
					Jobs[JobIndex].Archetype.ExecutionFunctionForChunk(LocalExecutionContext, ExecuteFunction
						, Jobs[JobIndex].ArchetypeIndex != INDEX_NONE ? ArchetypeFragmentMapping[Jobs[JobIndex].ArchetypeIndex] : FMassQueryRequirementIndicesMapping()
						, Jobs[JobIndex].EntityRange
						, ChunkCondition);
					LocalExecutionContext.PopQuery(*this);
				}, ParallelForFlags);

			// merge all command buffers
			// 【合并命令缓冲】worker 各自的 buffer 都搬到主 context 的 deferred 里，等出 ScopedQueryContext 时一并 flush
			for (FTaskContext& CommandContext : TaskContext)
			{
				ExecutionContext.Defer().MoveAppend(*CommandContext.GetCommandBuffer());
			}
		}
		else
		{
			// 关闭 parallel commands：worker 不持有 command buffer。回调若下达 deferred 命令会 crash。
			ParallelFor(Jobs.Num(), [this, &ExecutionContext, &ExecuteFunction, &Jobs](const int32 JobIndex)
				{
					FMassExecutionContext LocalExecutionContext(ExecutionContext, *this);
					Jobs[JobIndex].Archetype.ExecutionFunctionForChunk(LocalExecutionContext, ExecuteFunction
						, Jobs[JobIndex].ArchetypeIndex != INDEX_NONE ? ArchetypeFragmentMapping[Jobs[JobIndex].ArchetypeIndex] : FMassQueryRequirementIndicesMapping()
						, Jobs[JobIndex].EntityRange
						, ChunkCondition);
					LocalExecutionContext.PopQuery(*this);
				}, ParallelForFlags);
		}
	}
}

// 【统计当前 query 会处理的实体总数】会主动刷新 archetype 缓存
int32 FMassEntityQuery::GetNumMatchingEntities()
{
	CacheArchetypes();
	int32 TotalEntities = 0;
	for (FMassArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		if (const FMassArchetypeData* Archetype = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle))
		{
			TotalEntities += Archetype->GetNumEntities();
		}
	}
	return TotalEntities;
}

// 【针对一组 collection 统计实体数】不刷 ValidArchetypes 缓存，只对每个 collection 即时判断 archetype 是否匹配
int32 FMassEntityQuery::GetNumMatchingEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections)
{
	int32 TotalEntities = 0;
	for (const FMassArchetypeEntityCollection& EntityCollection : EntityCollections)
	{
		if (DoesArchetypeMatchRequirements(EntityCollection.GetArchetype()))
		{
			for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange : EntityCollection.GetRanges())
			{
				TotalEntities += EntityRange.Length;
			}
		}
	}
	return TotalEntities;
}

// 【是否有任何匹配实体】比 GetNumMatchingEntities>0 更快（命中即返回）
bool FMassEntityQuery::HasMatchingEntities()
{
	CacheArchetypes();

	for (FMassArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		const FMassArchetypeData* Archetype = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
		if (Archetype && Archetype->GetNumEntities() > 0)
		{
			return true;
		}
	}
	return false;
}

// 【把每个匹配 archetype 都包装成一个 EntityCollection】常用于配合批量 API（销毁、移动等）
TArray<FMassArchetypeEntityCollection> FMassEntityQuery::CreateMatchingEntitiesCollection()
{
	TArray<FMassArchetypeEntityCollection> Collections;
	CacheArchetypes();

	Collections.Reserve(ValidArchetypes.Num());
	for (FMassArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		Collections.Emplace(ArchetypeHandle);
	}

	return Collections;
}

// 【把所有匹配实体的 handle 平铺到一个数组】注意成本与实体总数成正比
TArray<FMassEntityHandle> FMassEntityQuery::GetMatchingEntityHandles()
{
	TArray<FMassEntityHandle> Handles;
	CacheArchetypes();

	for (FMassArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
		ArchetypeData.ExportEntityHandles(Handles);
	}

	return Handles;
}

// 【按某分组维度排序遍历】默认按 GroupID 升序
void FMassEntityQuery::GroupBy(UE::Mass::FArchetypeGroupType GroupType)
{
	GroupBy(GroupType, [](const UE::Mass::FArchetypeGroupID A, const UE::Mass::FArchetypeGroupID B)
	{
		return A < B;
	});
}

// 【按 GroupType + 自定义 Predicate 排序遍历】
//   多次调用会形成多级排序：第一次是首要键，第二次是次要键……
//   修改 grouping → IncrementChangeCounter → 下次 CacheArchetypes 会重排
void FMassEntityQuery::GroupBy(UE::Mass::FArchetypeGroupType GroupType, const TFunction<bool(const UE::Mass::FArchetypeGroupID, const UE::Mass::FArchetypeGroupID)>& Predicate)
{
	GroupSortingSteps.Emplace(GroupType, Predicate);
	IncrementChangeCounter();
}

// 清空 grouping，恢复"按 ValidArchetypes 原始顺序遍历"
void FMassEntityQuery::ResetGrouping()
{
	GroupSortingSteps.Reset();
	IncrementChangeCounter();
}

// 【无 grouping 情况下的退化版】OrderedArchetypeIndices 填 [FirstNew..N-1] = 自身索引
void FMassEntityQuery::BuildOrderedArchetypeIndices(const int32 FirstNewArchetypeIndex)
{
	OrderedArchetypeIndices.SetNumUninitialized(ValidArchetypes.Num());
	for (int32 ArchetypeIndex = FirstNewArchetypeIndex; ArchetypeIndex < ValidArchetypes.Num(); ++ArchetypeIndex)
	{
		OrderedArchetypeIndices[ArchetypeIndex] = ArchetypeIndex;
	}
}

// =============================================================================
// SortArchetypes —— 多级 GroupBy 排序
// =============================================================================
// 【算法：递归分段排序】
//   第 1 步：用 GroupSortingSteps[0].Predicate 对整个 OrderedArchetypeIndices 按 GroupIDs[*][0] 排序，
//           然后扫描相邻元素 GroupID 是否变化，把"同 GroupID 段"切成子区间放进 Ranges 队列。
//   第 2 步：对 Ranges 中每个子区间按 GroupSortingSteps[1] 再排序，再切分……
//   终止：步数用完，或所有子区间长度都为 1。
//
// 【数据结构】
//   - CachedGroupIDs[archetypeIdx][stepIdx] = 该 archetype 在该步分组维度下的 GroupID。
//   - Ranges 是 (start, end) 的子区间队列，初始 (0, N)；每完成一步会被替换为更细的子区间。
//
// 注意：FirstNewArchetypeIndex 参数虽然存在，本算法实际上是全量重排（不是真正的"增量"）。
void FMassEntityQuery::SortArchetypes(const int32 FirstNewArchetypeIndex)
{
	if (GroupSortingSteps.Num() == 0)
	{
		BuildOrderedArchetypeIndices(FirstNewArchetypeIndex);
		return;
	}

	check(CachedEntityManager);

	// first we need to cache the required group IDs from the new archetypes
	// 【步骤 1】为每个 archetype 预取在每一步分组维度下的 GroupID
	CachedGroupIDs.SetNum(ValidArchetypes.Num());
	OrderedArchetypeIndices.SetNumUninitialized(ValidArchetypes.Num());

	for (int32 NewArchetypeIndex = FirstNewArchetypeIndex; NewArchetypeIndex < ValidArchetypes.Num(); ++NewArchetypeIndex)
	{
		OrderedArchetypeIndices[NewArchetypeIndex] = NewArchetypeIndex;

		TArray<UE::Mass::FArchetypeGroupID>& ArchetypeGroupIDs = CachedGroupIDs[NewArchetypeIndex];
		ArchetypeGroupIDs.Reserve(GroupSortingSteps.Num());

		const UE::Mass::FArchetypeGroups& ArchetypeGroups = CachedEntityManager->GetGroupsForArchetype(ValidArchetypes[NewArchetypeIndex]);

		for (const FArchetypeGroupingStep& Step : GroupSortingSteps)
		{
			// ArchetypeGroups.GetID will return InvalidArchetypeGroupID if the given group type is not in ArchetypeGroups.
			// This is what we want.
			// 注意：archetype 不属于该 group 类型时 GetID 返回 InvalidArchetypeGroupID，会被一起排到一段
			ArchetypeGroupIDs.Add(ArchetypeGroups.GetID(Step.GroupType));
		}
	}

	// 【步骤 2】递归分段排序
	TConstArrayView<TArray<UE::Mass::FArchetypeGroupID>> GroupIDs = MakeConstArrayView(CachedGroupIDs);
	TArray<TTuple<int32, int32>> Ranges;
	Ranges.Add({0, OrderedArchetypeIndices.Num()});  // 初始：整段
	int32 MaxRangeSize = OrderedArchetypeIndices.Num();
	int32 Step = 0;
	int32 RangesProcessed = 0;

	while (Step < GroupSortingSteps.Num() && MaxRangeSize > 1)
	{
		const bool bLastIteration = (Step == GroupSortingSteps.Num() - 1);
		const int32 RangesThisIteration = Ranges.Num();
		MaxRangeSize = 0;
		for (; RangesProcessed < RangesThisIteration; ++RangesProcessed)
		{
			const TTuple<int32, int32> Range = Ranges[RangesProcessed];
			const int32 RangeLength = Range.Get<1>() - Range.Get<0>();
			// 在当前子区间内按本步 Predicate 排序
			MakeArrayView(&OrderedArchetypeIndices[Range.Get<0>()], RangeLength)
				.Sort([Step, &GroupIDs, Sorter = GroupSortingSteps[Step].Predicate](const int32 A, const int32 B)
				{
					return Sorter(GroupIDs[A][Step], GroupIDs[B][Step]);
				});

			// figure out new ranges
			// 最后一步无需再切分（不再有更深的级别）
			if (bLastIteration == false)
			{
				// 扫描相邻元素 GroupID，每变一次就切一个子区间
				int32 SubRangeStart = Range.Get<0>();
				UE::Mass::FArchetypeGroupID PrevValue = GroupIDs[OrderedArchetypeIndices[SubRangeStart]][Step];
				for (int32 Index = SubRangeStart + 1; Index < Range.Get<1>(); ++Index)
				{
					const UE::Mass::FArchetypeGroupID NewValue = GroupIDs[OrderedArchetypeIndices[Index]][Step];
					if (NewValue != PrevValue)
					{
						PrevValue = NewValue;
						Ranges.Push({SubRangeStart, Index});
						MaxRangeSize = FMath::Max(MaxRangeSize, Index - SubRangeStart);
						SubRangeStart = Index;
					}
				}

				// the loop above doesn't create any ranges when there's no change in GroupID among processed archetypes.
				// Similarly, it doesn't store the "last" range. We're addressing this now.
				// 把"最后一段"也补上（无论循环是否切过）
				Ranges.Push({SubRangeStart, Range.Get<1>()});
			}
		}

		check(MaxRangeSize >= 1 || bLastIteration);
		++Step;
	};
}

// 【取出某 archetype 的 fragment 列索引映射】运行时高频调用：让 ApplyFragmentRequirements 直接查索引
const FMassQueryRequirementIndicesMapping& FMassEntityQuery::GetRequirementsMappingForArchetype(const FMassArchetypeHandle ArchetypeHandle) const
{
	static const FMassQueryRequirementIndicesMapping FallbackEmptyMapping;
	// 调用前必须先 CacheArchetypes 同步过；HasIncrementalChanges 应该为 false
	checkf(HasIncrementalChanges() == false, TEXT("Fetching cached fragments mapping while the query's cached data is out of sync!"));
	const int32 ArchetypeIndex = ValidArchetypes.Find(ArchetypeHandle);
	return ArchetypeIndex != INDEX_NONE ? ArchetypeFragmentMapping[ArchetypeIndex] : FallbackEmptyMapping;
}

// 【把本 query 的所有需求合并导出】供 processor 做依赖图、读写冲突检测
void FMassEntityQuery::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	FMassSubsystemRequirements::ExportRequirements(OutRequirements);
	FMassFragmentRequirements::ExportRequirements(OutRequirements);
}

// 【调试】启用 entity owner 日志：把 FMassDebugLogFragment 加为 ReadOnly+Optional
// 这样 query 在运行时可以读取每个实体的"日志归属 UObject"，做有上下文的可视化日志
void FMassEntityQuery::DebugEnableEntityOwnerLogging()
{
#if WITH_MASSENTITY_DEBUG
	if (RequiredOptionalFragments.Contains<FMassDebugLogFragment>() == false)
	{
		AddRequirement<FMassDebugLogFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	}
#endif // WITH_MASSENTITY_DEBUG
}

//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------
// deprecated
FMassEntityQuery::FMassEntityQuery(std::initializer_list<UScriptStruct*>)
	: FMassEntityQuery()
{
}

// deprecated
FMassEntityQuery::FMassEntityQuery(TConstArrayView<const UScriptStruct*>)
	: FMassEntityQuery()
{
}

// deprecated
void FMassEntityQuery::ForEachEntityChunk(FMassEntityManager&, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	ForEachEntityChunk(ExecutionContext, ExecuteFunction);
}

// deprecated
void FMassEntityQuery::ForEachEntityChunk(const FMassArchetypeEntityCollection& EntityCollection, FMassEntityManager&
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	ForEachEntityChunk(EntityCollection, ExecutionContext, ExecuteFunction);
}

// deprecated
void FMassEntityQuery::ParallelForEachEntityChunk(FMassEntityManager&, FMassExecutionContext& ExecutionContext
		, const FMassExecuteFunction& ExecuteFunction, const EParallelForMode ParallelMode)
{
	const EParallelExecutionFlags Flags = EnumHasAnyFlags(ParallelMode, ForceParallelExecution)
		? EParallelExecutionFlags::Force
		: EParallelExecutionFlags::Default;
	ParallelForEachEntityChunk(ExecutionContext, ExecuteFunction, Flags);
}

// deprecated
void FMassEntityQuery::ForEachEntityChunkInCollections(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, FMassEntityManager&
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	ForEachEntityChunkInCollections(EntityCollections, ExecutionContext, ExecuteFunction);
}

// deprecated
void FMassEntityQuery::ParallelForEachEntityChunkInCollection(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, FMassEntityManager&, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction
		, const EParallelForMode ParallelMode)
{
	const EParallelExecutionFlags Flags = EnumHasAnyFlags(ParallelMode, ForceParallelExecution)
		? EParallelExecutionFlags::Force
		: EParallelExecutionFlags::Default;
	ParallelForEachEntityChunkInCollection(EntityCollections, ExecutionContext, ExecuteFunction, Flags);
}

// deprecated
void FMassEntityQuery::CacheArchetypes(const FMassEntityManager&)
{
	CacheArchetypes();
}

// deprecated
int32 FMassEntityQuery::GetNumMatchingEntities(FMassEntityManager&)
{
	return GetNumMatchingEntities();
}

// deprecated
bool FMassEntityQuery::HasMatchingEntities(FMassEntityManager&)
{
	return HasMatchingEntities();
}
