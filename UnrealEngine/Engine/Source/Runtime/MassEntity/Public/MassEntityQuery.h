// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "MassArchetypeGroup.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassExternalSubsystemTraits.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassRequirements.h"

#define UE_API MASSENTITY_API

#ifndef WITH_ARCHETYPE_MATCH_OVERRIDE
#define WITH_ARCHETYPE_MATCH_OVERRIDE (WITH_EDITOR)
#endif //  WITH_ARCHETYPE_MATCH_OVERRIDE

#ifndef MASS_ACHETYPE_OVERRIDE_MAX_SIZE
#define MASS_ACHETYPE_OVERRIDE_MAX_SIZE 16
#endif
	
#ifndef MASS_ACHETYPE_OVERRIDE_MAX_ALIGNMENT
#define MASS_ACHETYPE_OVERRIDE_MAX_ALIGNMENT 8
#endif

#if WITH_ARCHETYPE_MATCH_OVERRIDE
// 【ArchetypeMatchOverride 概念约束】
// 编辑器/调试场景下，允许用户提供一个自定义的"archetype 是否匹配"的判定对象。
// 该对象必须实现 bool Match(const FMassArchetypeCompositionDescriptor&) const 方法。
// concept 在编译期校验 T 满足该接口，否则 SetArchetypeMatchOverride<T> 不会被实例化。
template<typename T>
concept TArchetypeMatchOverrideConcept = requires(const FMassArchetypeCompositionDescriptor& Descriptor)
{
	{ static_cast<const T*>(nullptr)->Match(Descriptor) } -> std::convertible_to<bool>;
};
#endif

struct FMassEntityManager;

/** 
 *  FMassEntityQuery is a structure that is used to trigger calculations on cached set of valid archetypes as described 
 *  by requirements. See the parent classes FMassFragmentRequirements and FMassSubsystemRequirements for setting up the 
 *	required fragments and subsystems.
 * 
 *  A query to be considered valid needs declared at least one EMassFragmentPresence::All, EMassFragmentPresence::Any 
 *  EMassFragmentPresence::Optional fragment requirement.
 *
 *  【FMassEntityQuery 中文说明】
 *  Query 是 Mass ECS 的"查询入口"。它本质上是 (FragmentRequirements + SubsystemRequirements) 的组合，
 *  外加：
 *    1) 一份缓存：满足这些 Requirements 的 archetype 列表（ValidArchetypes）以及对应的"需求序号→列索引"映射
 *       （ArchetypeFragmentMapping），避免每一帧都重新做一遍匹配/查找。
 *    2) 一组执行入口：ForEachEntityChunk / ParallelForEachEntityChunk 等，把缓存的 archetype 集合喂给用户回调。
 *
 *  【为什么需要缓存？】
 *  在典型的游戏运行中，archetype 集合在帧间几乎是稳定的（实体添加/删除并不会改变 archetype，只有添加/删除组件才会
 *  生成新的 archetype）。每次执行 query 都重算"哪些 archetype 满足要求"是巨大浪费。Query 通过两个版本号
 *  （EntitySubsystemHash + LastUpdatedArchetypeDataVersion）来确认缓存仍然有效，只在 EntityManager 真的有新增
 *  archetype 时增量更新。
 *
 *  【生命周期】
 *    构造（FMassEntityQuery(Owner) 把自己注册到 Processor）
 *      └─ ConfigureQueries 阶段： AddRequirement<T>(...) 累计 fragment/tag/shared/chunk 需求
 *           └─ 第一次 ForEachEntityChunk：内部触发 CacheArchetypes()，扫描 EntityManager.AllArchetypes
 *                └─ 每帧调用 ForEachEntityChunk：缓存命中，直接遍历 ValidArchetypes
 *                     └─ EntityManager 新增 archetype → ArchetypeDataVersion 递增 → 下次执行时增量补充
 *                          └─ 调用 Clear() 或 修改 Requirements → DirtyCachedData → 下次完整重算
 *
 *  【一个有效的 Query 至少声明一项 All/Any/Optional 类型的 fragment 需求；纯 None（黑名单）不算合法。】
 */
struct FMassEntityQuery : public FMassFragmentRequirements, public FMassSubsystemRequirements
{
	static constexpr int32 ArchetypeMatchOverrideSize = MASS_ACHETYPE_OVERRIDE_MAX_SIZE;
	static constexpr uint32 ArchetypeMatchOverrideAlignment = MASS_ACHETYPE_OVERRIDE_MAX_ALIGNMENT;
	
	friend struct FMassDebugger;

private:
	// 【自定义 archetype 匹配谓词的"类型擦除"包装】
	// 编辑器场景下用户可以通过 SetArchetypeMatchOverride<T>(ctx) 注入一个自定义的 Match 谓词，
	// 这里用一个固定大小的对齐缓冲区 + 函数指针 来存放它，避免 std::function 的堆分配。
	struct FArchetypeMatchOverride
    {
		using MatchFunction = bool (*)(const void* /*Context*/, const FMassArchetypeCompositionDescriptor&);
		
		// 实际的匹配函数，会从 Data 中取回类型化对象再调用其 Match
		MatchFunction Match = nullptr;
		// 用户上下文对象的字节缓冲（要求 trivially copyable / destructible，且大小 <= ArchetypeMatchOverrideSize）
    	TAlignedBytes<ArchetypeMatchOverrideSize, ArchetypeMatchOverrideAlignment> Data;
    };

public:
	/**
	 * 【并行执行模式枚举】控制 ParallelForEachEntityChunk 的并行行为。
	 *  - Default     : 使用全局 cvar 配置（mass.AllowQueryParallelFor 等）。
	 *  - Force       : 即使全局禁用并行，也强制并行。
	 *  - AutoBalance : 默认是把 chunk 平均分给线程；若 chunk 处理时长差异大，
	 *                  开此 flag 会让线程"按需取任务"，启动更贵但整体利用率更高。
	 */
	enum class EParallelExecutionFlags
	{
		// Use whatever the whole system has been configured for.
		Default = 0,
		// Force parallel execution of a processor for each chunk even when parallel execution has been disabled.
		Force = 1 << 0,
		// The default behavior for parallel execution assigns each chunk to a thread before execution. This implicitly assumes all chunks
		// take roughly the same amount of time to process. If chunks vary in the time it takes to process this flag can be used to queue
		// chunks so threads can pick them up as soon as possible. This makes starting the processing of a chunk more expensive but can
		// result in better overall utilization of threads.
		AutoBalance = 1 << 1
	};

	// 默认构造：query 不绑定任何 EntityManager，需要后续手动初始化（基本只用于内部 DummyQuery）
	FMassEntityQuery() = default;
	// 推荐构造路径：在 UMassProcessor::ConfigureQueries 阶段把 query 注册到自己所属的 processor 上。
	UE_API FMassEntityQuery(UMassProcessor& Owner);
	// 不依附 processor 的 query：直接绑定一个 EntityManager（一般用于框架外/工具代码）
	UE_API FMassEntityQuery(const TSharedPtr<FMassEntityManager>& EntityManager);
	// 便利构造：列出一组 fragment 类型，全部按 ReadWrite + All Presence 添加为需求
	UE_API FMassEntityQuery(const TSharedRef<FMassEntityManager>& EntityManager, std::initializer_list<UScriptStruct*> InitList);
	UE_API FMassEntityQuery(const TSharedRef<FMassEntityManager>& EntityManager, TConstArrayView<const UScriptStruct*> InitList);

	// 把 query 关联到 processor，使得后续通过 processor 执行管线调用时能正确派发并通过 ExpectedContextType 校验。
	UE_API void RegisterWithProcessor(UMassProcessor& Owner);

	/** Runs ExecuteFunction on all entities matching Requirements
	 *  【对所有匹配该 query 的 chunk 触发 ExecuteFunction】
	 *  - 内部第一次会调用 CacheArchetypes 收集 ValidArchetypes（之后只增量更新）。
	 *  - 调用线程：取决于 ExecutionContext.ExecutionType；processor 流水线场景下由调度器决定。
	 *  - 仅在 processor 调用阶段（ConfigureQueries 之后）调用，调用前 Requirements 必须已声明完毕。
	 */
	UE_API void ForEachEntityChunk(FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	/**
	 * Runs ExecuteFunction on entities matching Requirements up to the EntityLimit specified in the ExecutionLimiter.
	 * Resumes from last index set in Limiter.
	 * Will always complete the current chunk once the limit is reached.
	 * Does not account for changes to entity count or organization between iterations (Entities could be skipped).
	 * Identical to the unlimited version if the Limit exceeds the number of applicable entities.
	 *
	 * 【带"分批/续接"语义的版本】
	 *  - Limiter 同时充当输入(从哪续)和输出(下次从哪续)：内部记录 ArchetypeIndex / ChunkIndex / 剩余 entity 数。
	 *  - 一旦达到 EntityLimit 会先把当前 chunk 处理完才停（保证 chunk 是处理粒度的最小单位）。
	 *  - 不支持配合手动 EntityCollection 使用（运行时会断言）。
	 *  - 适合"每帧只处理 N 个实体，下帧接着处理"的渐进式工作。
	 */
	UE_API void ForEachEntityChunk(FMassExecutionContext& ExecutionContext, UE::Mass::FExecutionLimiter& Limiter, const FMassExecuteFunction& ExecuteFunction);
	
	/** Will first verify that the archetype given with Collection matches the query's requirements, and if so will run the other, more generic ForEachEntityChunk implementation
	 *  【针对一个已知 EntityCollection（已知一批实体在哪些 chunk）的版本】
	 *  - Collection 的 archetype 必须出现在 ValidArchetypes 中，否则提前返回（observer 场景下也算正常）。
	 *  - 不会触发 ChunkCondition（chunk 过滤只对全 archetype 遍历有效）。
	 */
	UE_API void ForEachEntityChunk(const FMassArchetypeEntityCollection& EntityCollection
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	/**
	 * Attempts to process every chunk of every affected archetype in parallel.
	 *
	 * 【并行版本】把每个 (archetype, chunk) 作为一个 Job 投递到 ParallelFor。
	 *  - 如果 query 启用了 grouping（GroupBy），并行版会回退为串行（因为 grouping 隐含顺序约束）。
	 *  - 默认每个 worker 会拥有自己的 FMassCommandBuffer，最后合并到 ExecutionContext.Defer()。
	 *    可以通过 SetParallelCommandBufferEnabled(false) 禁用以省去 buffer 分配开销，但要求回调不得 issue 命令。
	 */
	UE_API void ParallelForEachEntityChunk(FMassExecutionContext& ExecutionContext
		, const FMassExecuteFunction& ExecuteFunction, const EParallelExecutionFlags Flags = EParallelExecutionFlags::Default);

	// 【遍历多组 EntityCollection】对每个 collection 依次走单 collection 的 ForEachEntityChunk 路径（串行）。
	UE_API void ForEachEntityChunkInCollections(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	// 【并行版本】 对 EntityCollections 进行并行 ParallelFor：每个 collection 是一个 job，job 内部再走并行 chunk 处理。
	UE_API void ParallelForEachEntityChunkInCollection(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction
		, const EParallelExecutionFlags Flags = EParallelExecutionFlags::Default);

	/** Will gather all archetypes from InEntityManager matching this->Requirements.
	 *  Note that no work will be done if the cached data is up to date (as tracked by EntitySubsystemHash and 
	 *	ArchetypeDataVersion properties).
	 *
	 *  【缓存匹配 archetype 的核心函数】
	 *  调用时机：每个 ForEachEntityChunk 入口内部都会调一次（成本通常仅是版本号比较），
	 *  也可由用户主动调用以预热缓存（比如 GetNumMatchingEntities 等只读查询）。
	 */
	UE_API void CacheArchetypes();

	// 【完整重置】清空 fragment/subsystem 需求、grouping 步骤，并把缓存标脏。下一次执行会重新做完整匹配。
	void Clear()
	{
		FMassFragmentRequirements::Reset();
		FMassSubsystemRequirements::Reset();
		ResetGrouping();
		DirtyCachedData();
	}

	// 【把缓存标脏】把两个版本号清零，强制 CacheArchetypes 走完整路径。修改 Requirements 后应调用。
	inline void DirtyCachedData()
	{
		EntitySubsystemHash = 0;
		LastUpdatedArchetypeDataVersion = 0;
	}

	using FMassSubsystemRequirements::AddSubsystemRequirement;

	// 【添加非模板版的 Subsystem 需求】把 cached EntityManager 透传给基类，使其能解析 subsystem traits（GameThreadOnly 等）。
	FMassSubsystemRequirements& AddSubsystemRequirement(const TSubclassOf<USubsystem> SubsystemClass, const EMassFragmentAccess AccessMode)
	{
		FMassSubsystemRequirements::AddSubsystemRequirement(SubsystemClass, AccessMode, CachedEntityManager.ToSharedRef());
		return *this;
	}
	
	// 【是否必须在 GameThread 执行】fragment、subsystem、显式 mutating world 任一要求 GT 即返回 true。
	bool DoesRequireGameThreadExecution() const 
	{ 
		return FMassFragmentRequirements::DoesRequireGameThreadExecution() 
			|| FMassSubsystemRequirements::DoesRequireGameThreadExecution() 
			|| bRequiresMutatingWorldAccess;
	}

	// 显式声明该 query 会写入 World 状态（如 spawn actor）→ 强制 GameThread。
	void RequireMutatingWorldAccess() { bRequiresMutatingWorldAccess = true; }

	// 是否一个需求都没有声明。
	bool IsEmpty() const { return FMassFragmentRequirements::IsEmpty() && FMassSubsystemRequirements::IsEmpty(); }

	// 【返回当前已缓存的、匹配本 query 的 archetype handle 列表】注意：调用前需保证缓存已新鲜。
	const TArray<FMassArchetypeHandle>& GetArchetypes() const
	{ 
		return ValidArchetypes; 
	}

	/** 
	 * Goes through ValidArchetypes and sums up the number of entities contained in them.
	 * Note that the function is not const because calling it can result in re-caching of ValidArchetypes 
	 * @return the number of entities this given query would process if called "now"
	 *
	 * 【统计当前 query 会处理多少实体】会主动触发 CacheArchetypes，因此非 const。
	 */
	UE_API int32 GetNumMatchingEntities();

	/** 
	 * Sums the entity range lengths for each collection in EntityCollections, where the collection's 
	 * archetype matches the query's requirements.
	 * @return the number of entities this given query would process if called "now" for EntityCollections
	 *
	 * 【针对一组预先构造的 EntityCollection 统计实体数】不刷新 ValidArchetypes 缓存，只用 DoesArchetypeMatchRequirements 即时判断。
	 */
	UE_API int32 GetNumMatchingEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections);

	/**
	 * Checks if any of ValidArchetypes has any entities.
	 * Note that the function is not const because calling it can result in re-caching of ValidArchetypes
	 * @return "true" if any of the ValidArchetypes has any entities, "false" otherwise
	 *
	 * 【是否有任何匹配的实体】比 GetNumMatchingEntities>0 更快（命中即返回）。
	 */
	UE_API bool HasMatchingEntities();

	/**
	 * Creates an array of FMassArchetypeEntityCollection instances that identify all the entities
	 * currently matching this query.
	 *
	 * 【把所有匹配 archetype 打包成 EntityCollection 数组】之后可与其他 API 交叉使用（比如批量销毁）。
	 */
	UE_API TArray<FMassArchetypeEntityCollection> CreateMatchingEntitiesCollection();

	/**
	 * Fetches entity handles of all the entities currently matching this query.
	 *
	 * 【拉出所有匹配实体的 handle 列表】注意这会逐 archetype 把内部 entity 全量导出，开销与实体总数成正比。
	 */
	UE_API TArray<FMassEntityHandle> GetMatchingEntityHandles();

	/** 
	 * Sets a chunk filter condition that will be applied to each chunk of all valid archetypes. Note 
	 * that this condition won't be applied when a specific entity collection is used (via FMassArchetypeEntityCollection )
	 * The value returned by InFunction controls whether to allow execution (true) or block it (false).
	 *
	 * 【chunk 级过滤函数】给每个 chunk 一个"是否处理"的二次判定。常用于按 chunk fragment 状态过滤
	 * （如"只处理 LOD<=1 的 chunk"）。注意：不会对显式 EntityCollection 路径生效。
	 * 一次只能存在一个 ChunkCondition；切换前必须 ClearChunkFilter()。
	 */
	void SetChunkFilter(const FMassChunkConditionFunction& InFunction);

	void ClearChunkFilter() { ChunkCondition.Reset(); }

	bool HasChunkFilter() const { return bool(ChunkCondition); }

	// 【按 ArchetypeGroup 排序遍历】GroupType 决定按哪个分组维度排序，默认按 GroupID 升序。
	UE_API void GroupBy(UE::Mass::FArchetypeGroupType GroupType);
	// 自定义比较谓词的 GroupBy。多次调用会形成"先按第一个分组、组内再按第二个分组"的多级排序。
	UE_API void GroupBy(UE::Mass::FArchetypeGroupType GroupType, const TFunction<bool(const UE::Mass::FArchetypeGroupID, const UE::Mass::FArchetypeGroupID)>& Predicate);
	// 清空所有 grouping 设置。
	UE_API void ResetGrouping();

	/** 
	 * @return whether the query is configured to use archetype group information to group and sort
	 *		archetypes to be processed.
	 */
	bool IsGrouping() const;

#if WITH_ARCHETYPE_MATCH_OVERRIDE
	// 【编辑器/调试专用】注入一个自定义的 archetype 匹配谓词，用于绕过常规 Requirements 的 archetype 选择逻辑。
	template<typename T> requires TArchetypeMatchOverrideConcept<T>
	void SetArchetypeMatchOverride(const T& Override);
#endif

	const TSharedPtr<FMassEntityManager>& GetEntityManager() const;

	/** 
	 * If ArchetypeHandle is among ValidArchetypes then the function retrieves requirements mapping cached for it,
	 * otherwise an empty mapping will be returned (and the requirements binding will be done the slow way).
	 *
	 * 【取出某 archetype 在本 query 下的"需求序号→列索引"映射】
	 * 这份映射是 CacheArchetypes 阶段预算好的，让运行时绑定 fragment view 时无需再做一次类型 hash 查找。
	 */
	UE_API const FMassQueryRequirementIndicesMapping& GetRequirementsMappingForArchetype(const FMassArchetypeHandle ArchetypeHandle) const;

	// 把本 query 的 fragment + subsystem 需求合并导出（供 processor 做依赖分析、读写冲突检测使用）。
	UE_API void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

	/** 
	 * Controls whether ParallelForEachEntityChunk creates separate command buffers for each job.
	 * @see bAllowParallelCommands for more details
	 */
	void SetParallelCommandBufferEnabled(const bool bInAllowParallelCommands) { bAllowParallelCommands = bInAllowParallelCommands; }

	/**
	 * Configures the query to support per-entity logging based on their individual UObject "owners",
	 * as declared via debug fragments.
	 *
	 * 【调试辅助】启用基于 FMassDebugLogFragment 的 per-entity 日志归属。会自动添加一个 ReadOnly+Optional 的 fragment 需求。
	 */
	UE_API void DebugEnableEntityOwnerLogging();

private:
	/**
	 * Incrementally sorts all ValidArchetypes to fill OrderedArchetypeIndices with the expected order of archetype processing.
	 * This function will only ever get called when there are actual sorting steps registered (@see GroupBy)
	 *
	 * 【按 GroupSortingSteps 多级排序填 OrderedArchetypeIndices】
	 * 算法：对当前所有索引做"按第一级 group 排序 → 同 group 内按第二级排序 → ..."的递归分段排序。
	 * FirstNewArchetypeIndex 是增量入口：只有 ValidArchetypes 末尾追加了新元素时才需要重排（其实算法仍是全量重排）。
	 */
	void SortArchetypes(const int32 FirstNewArchetypeIndex = 0);
	/**
	 * An alternative to SortArchetypes that will get called in the absence of archetype sorting steps to maintain OrderedArchetypeIndices
	 * and have it reflect the order of ValidArchetypes.
	 *
	 * 【无 grouping 情况下的退化版本】只是把 OrderedArchetypeIndices 填成 [0, 1, 2, ...]。
	 */
	void BuildOrderedArchetypeIndices(const int32 FirstNewArchetypeIndex = 0);

	/** 
	 * This function represents a condition that will be called for every chunk to be processed before the actual 
	 * execution function is called. The chunk fragment requirements are already bound and ready to be used by the time 
	 * ChunkCondition is executed.
	 *
	 * 【chunk 级过滤回调】运行时在每个 chunk 进入用户回调前评估一次；返回 false 则跳过该 chunk。
	 * 注意 chunk fragment 已经绑定到 context，可以在 ChunkCondition 中直接读 chunk fragment。
	 */
	FMassChunkConditionFunction ChunkCondition;

	// 【缓存失效检测：EntityManager 维度】记录上次缓存绑定的 EntityManager 指针 hash。
	// 当 query 被复用到不同的 EntityManager 上（极少见），或第一次绑定时，这个值会变化，触发完整重算。
	uint32 EntitySubsystemHash = 0;
	// 【缓存失效检测：archetype 维度】记录上次匹配过的 EntityManager.ArchetypeDataVersion。
	// EntityManager 每次创建新 archetype 都会递增这个版本号，query 据此判断要不要做"增量"补充。
	uint32 LastUpdatedArchetypeDataVersion = 0;

	// 【匹配到的 archetype 列表】CacheArchetypes 的产物。运行时按它驱动遍历。
	TArray<FMassArchetypeHandle> ValidArchetypes;
	// 【遍历顺序索引】到 ValidArchetypes 的间接索引数组。无 grouping 时是 0..N-1；有 grouping 时是排序后的顺序。
	TArray<int32> OrderedArchetypeIndices;
	// 【与 ValidArchetypes 一一对应】每个 archetype 一份"需求序号 → 该 archetype 中 fragment 列索引"的映射，
	// 包含 entity/chunk/shared/const-shared 四类。运行时绑定 fragment view 时直接拿索引，避免 hash 查找。
	TArray<FMassQueryRequirementIndicesMapping> ArchetypeFragmentMapping;

	// 【单步 grouping 配置】GroupType 指明分组维度，Predicate 是在该维度内排序的比较函数。
	struct FArchetypeGroupingStep
	{
		FArchetypeGroupingStep() = default;
		FArchetypeGroupingStep(UE::Mass::FArchetypeGroupType InGroupType, const TFunction<bool(const UE::Mass::FArchetypeGroupID, const UE::Mass::FArchetypeGroupID)>& InPredicate)
			: GroupType(InGroupType), Predicate(InPredicate)
		{
		}
		UE::Mass::FArchetypeGroupType GroupType;
		TFunction<bool(const UE::Mass::FArchetypeGroupID, const UE::Mass::FArchetypeGroupID)> Predicate;
	};
	// 多级 grouping 步骤；GroupBy 多次调用就追加一步。
	TArray<FArchetypeGroupingStep> GroupSortingSteps;
	// 每个 archetype 在每一步分组维度下的 GroupID（外层 index 同 ValidArchetypes，内层 index 同 GroupSortingSteps）。
	TArray<TArray<UE::Mass::FArchetypeGroupID>> CachedGroupIDs;

	/** 
	 * Controls whether ParallelForEachEntityChunk created dedicated command buffer for each job. This is required 
	 * to ensure thread safety. Disable by calling SetParallelCommandBufferEnabled(false) if execution function doesn't 
	 * issue commands. Disabling will save some performance since it will avoid dynamic allocation of command buffers.
	 * 
	 * @Note that disabling parallel commands will result in no command buffer getting passed to execution which in turn
	 *	will cause crashes if the underlying code does try to issue commands. 
	 *
	 * 【是否在并行执行时为每个 worker 单独建 CommandBuffer】
	 * 默认 true：worker 各自记录 deferred 命令，结束时合并到主 context；保证线程安全。
	 * 如果用户回调"承诺"不会下达任何 deferred 命令，可关掉以省 buffer 分配。
	 */
	uint8 bAllowParallelCommands : 1 = true;
	// 【是否会写 World】DoesRequireGameThreadExecution 的影响因子之一。
	uint8 bRequiresMutatingWorldAccess : 1 = false;
#if WITH_ARCHETYPE_MATCH_OVERRIDE
	// 是否注入了自定义匹配谓词。一旦设了就不可再 set（断言保护）。
	uint8 bHasArchetypeMatchOverride : 1 = false;
#endif

	// 【期望的执行 context 类型】Local 表示游离调用，Processor 表示作为 processor 流水线一部分。
	// 调用 ForEachEntityChunk 时会与 ExecutionContext 的实际类型比对，错配则断言。
	EMassExecutionContextType ExpectedContextType = EMassExecutionContextType::Local;

#if WITH_MASSENTITY_DEBUG
	// 是否已经通过 RegisterWithProcessor 注册到 processor。Processor 路径下必须 true。
	uint8 bRegistered : 1 = false;
#endif // WITH_MASSENTITY_DEBUG

#if WITH_ARCHETYPE_MATCH_OVERRIDE
	// 自定义匹配谓词的存储槽。
	FArchetypeMatchOverride ArchetypeMatchOverride;
#endif

public:
	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
	UE_DEPRECATED(5.6, "This type of FMassEntityQuery is no longer supported. Use one of the other constructors instead.")
	UE_API FMassEntityQuery(std::initializer_list<UScriptStruct*> InitList);

	UE_DEPRECATED(5.6, "This type of FMassEntityQuery is no longer supported. Use one of the other constructors instead.")
	UE_API FMassEntityQuery(TConstArrayView<const UScriptStruct*> InitList);

	enum EParallelForMode
	{
		Default = static_cast<int32>(EParallelExecutionFlags::Default),
		ForceParallelExecution = static_cast<int32>(EParallelExecutionFlags::Force),
	};

	UE_DEPRECATED(5.6, "ForEachEntityChunk is deprecated. New version doesn't require the FMassEntityManager parameter")
	UE_API void ForEachEntityChunk(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);
	
	UE_DEPRECATED(5.6, "ForEachEntityChunk is deprecated. New version doesn't require the FMassEntityManager parameter")
	UE_API void ForEachEntityChunk(const FMassArchetypeEntityCollection& EntityCollection, FMassEntityManager& EntityManager
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	UE_DEPRECATED(5.6, "ParallelForEachEntityChunk is deprecated. New version doesn't require the FMassEntityManager parameter. Also ParallelMode parameter changed type, usee EParallelExecutionFlags instead.")
	UE_API void ParallelForEachEntityChunk(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext
		, const FMassExecuteFunction& ExecuteFunction, const EParallelForMode ParallelMode = Default);

	UE_DEPRECATED(5.6, "ForEachEntityChunkInCollections is deprecated. New version doesn't require the FMassEntityManager parameter")
	UE_API void ForEachEntityChunkInCollections(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, FMassEntityManager& EntityManager
		, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	UE_DEPRECATED(5.6, "ParallelForEachEntityChunkInCollection is deprecated. New version doesn't require the FMassEntityManager parameter. Also ParallelMode parameter changed type, usee EParallelExecutionFlags instead.")
	UE_API void ParallelForEachEntityChunkInCollection(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction
		, const EParallelForMode ParallelMode);

	UE_DEPRECATED(5.6, "This flavor of CacheArchetypes is deprecated. EntityQueries are not tied to a specific EntityManager instance and there's no need to pass it in as a parameter")
	UE_API void CacheArchetypes(const FMassEntityManager& InEntityManager);

	UE_DEPRECATED(5.6, "This flavor of GetNumMatchingEntities is deprecated. EntityQueries are not tied to a specific EntityManager instance and there's no need to pass it in as a parameter")
	UE_API int32 GetNumMatchingEntities(FMassEntityManager& InEntityManager);

	UE_DEPRECATED(5.6, "This flavor of HasMatchingEntities is deprecated. EntityQueries are not tied to a specific EntityManager instance and there's no need to pass it in as a parameter")
	UE_API bool HasMatchingEntities(FMassEntityManager& InEntityManager);
};

ENUM_CLASS_FLAGS(FMassEntityQuery::EParallelExecutionFlags);

//-----------------------------------------------------------------------------
// INLINES
//-----------------------------------------------------------------------------
#if WITH_ARCHETYPE_MATCH_OVERRIDE
// 【SetArchetypeMatchOverride 模板实现】
//   编译期约束：
//     - sizeof(T) <= ArchetypeMatchOverrideSize（默认 16），保证用户上下文能塞进内嵌缓冲区。
//     - alignof(T) <= ArchetypeMatchOverrideAlignment（默认 8）。
//     - T 必须是 trivially copyable / destructible（因为我们用 Memcpy 直接拷贝原始字节，不做析构）。
//   运行期：
//     - bHasArchetypeMatchOverride 只允许置 true 一次（断言保护，避免覆盖前一个谓词）。
//     - 通过 lambda 把"调用 Context->Match(Descriptor)"编码为函数指针，达成类型擦除。
template <typename T> requires TArchetypeMatchOverrideConcept<T>
void FMassEntityQuery::SetArchetypeMatchOverride(const T& Context)
{
	static_assert(sizeof(T) <= sizeof(FArchetypeMatchOverride));
	static_assert(alignof(T) <= alignof(FArchetypeMatchOverride));
	static_assert(std::is_trivially_copyable_v<T>);
	static_assert(std::is_trivially_destructible_v<T>);

	check(!bHasArchetypeMatchOverride);
	bHasArchetypeMatchOverride = true;

	ArchetypeMatchOverride.Match = [](const void* TypeErasedContext, const FMassArchetypeCompositionDescriptor& Descriptor)->bool
	{
		const T* Context = static_cast<const T*>(TypeErasedContext);
		return Context->Match(Descriptor);	
	};
	FMemory::Memcpy(&ArchetypeMatchOverride.Data, &Context, sizeof(T));
}
#endif

inline void FMassEntityQuery::SetChunkFilter(const FMassChunkConditionFunction& InFunction)
{
	checkf(!HasChunkFilter(), TEXT("Chunk filter needs to be cleared before setting a new one."));
	ChunkCondition = InFunction;
}

inline bool FMassEntityQuery::IsGrouping() const
{
	return !GroupSortingSteps.IsEmpty();
}

inline const TSharedPtr<FMassEntityManager>& FMassEntityQuery::GetEntityManager() const
{
	return CachedEntityManager;
}

#undef MASS_ACHETYPE_OVERRIDE_MAX_SIZE
#undef MASS_ACHETYPE_OVERRIDE_MAX_ALIGNMENT

#undef UE_API 