// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassExecutionContext.h"
#include "MassArchetypeData.h"
#include "MassEntityManager.h"
#if WITH_MASSENTITY_DEBUG
#include "MassDebugger.h"
#endif // WITH_MASSENTITY_DEBUG
#include "MassTestableEnsures.h"

//-----------------------------------------------------------------------------
// FMassExecutionContext
//-----------------------------------------------------------------------------
/**
 * 主构造：
 *  - SubsystemAccess 用 EntityManager 关联的 World 初始化（World 可以为 nullptr）。
 *  - DeferredCommandBuffer 暂为空，需要外部 Set；多数路径由 EntityManager 默认提供一个全局 buffer。
 *  - QueriesStack/FragmentViews 等容器都是默认空构造。
 */
FMassExecutionContext::FMassExecutionContext(FMassEntityManager& InEntityManager, const float InDeltaTimeSeconds, const bool bInFlushDeferredCommands)
	: SubsystemAccess(InEntityManager.GetWorld())
	, DeltaTimeSeconds(InDeltaTimeSeconds)
	, EntityManager(InEntityManager.AsShared())
	, bFlushDeferredCommands(bInFlushDeferredCommands)
{
}

/**
 * 拷贝构造：
 *  - 用 private operator= 把所有标量/视图/SubsystemAccess 状态从 Other 复制过来；
 *  - **强制清空 QueriesStack**：子 Context 不继承父的 query 栈
 *    （父子 Context 在不同 thread/任务中独立 Push/PopQuery）。
 */
FMassExecutionContext::FMassExecutionContext(const FMassExecutionContext& Other)
	: EntityManager(Other.EntityManager)
{
	// we're using assignment operator here as a setup helper, as per operator=='s comment.
	*this = Other;

	QueriesStack.Reset();
}

/**
 * 派生构造（单 query）：
 *  - 先复用上面的拷贝构造（栈被清空）；
 *  - 再把 Other 当前栈顶的 FQueryTransientRuntime 拷贝一份压回栈，
 *    这样子 Context 就只\"知道\"父正在执行的那条 query；
 *  - 替换为子线程独立的 CommandBuffer。
 *
 * 调用约束：传入的 Query 必须等于父栈顶（ensureMsgf 校验）；否则说明调用方语义有误。
 */
FMassExecutionContext::FMassExecutionContext(const FMassExecutionContext& Other, FMassEntityQuery& Query, const TSharedPtr<FMassCommandBuffer>& InCommandBuffer)
	: FMassExecutionContext(Other)
{
	ensureMsgf(Other.QueriesStack.Last().Query == &Query, TEXT("Creating a single-query execution context but the Query doesn't match the source context."));
	QueriesStack.Add(Other.QueriesStack.Last());
	SetDeferredCommandBuffer(InCommandBuffer);
}

/**
 * 析构：
 *  ensure QueriesStack 为空；非空意味着某条 query Push 了却没 Pop，或 Push/Pop 不匹配。
 *  仅 ensure 不 check：避免在 shutdown/异常路径上把进程拉死。
 */
FMassExecutionContext::~FMassExecutionContext()
{
	ensureMsgf(QueriesStack.Num() == 0, TEXT("Destroying a FMassExecutionContext instance while not all queries have been popped is unexpected."));
}

/**
 * 把 DeferredCommandBuffer 中累积的命令一次性提交给 EntityManager 执行。
 *  - 仅 bFlushDeferredCommands == true 且 buffer 非空时才 flush；
 *  - flush 内部由 EntityManager 串行处理所有命令并触发结构性变化（archetype 迁移等）。
 */
void FMassExecutionContext::FlushDeferred()
{
	if (bFlushDeferredCommands && DeferredCommandBuffer)
	{
		EntityManager->FlushCommands(DeferredCommandBuffer);
	}
}

/**
 * 重置当前 chunk 相关的所有状态（不影响 SubsystemAccess、DeferredCommandBuffer、EntityManager 引用、QueriesStack）。
 * 调用时机：
 *  - archetype 切换前：清旧 archetype 的状态；
 *  - 整个 query 结束前的清理。
 * 注意：4 类 FragmentView 容器是 Reset 而不是 Empty —— 释放但保留 InlineAllocator 容量，避免再分配。
 */
void FMassExecutionContext::ClearExecutionData()
{
	FragmentViews.Reset();
	ChunkFragmentViews.Reset();
	ConstSharedFragmentViews.Reset();
	SharedFragmentViews.Reset();
	CurrentArchetypeCompositionDescriptor.Reset();
	EntityListView = {};
	ChunkSerialModificationNumber = -1;
#if WITH_MASSENTITY_DEBUG
	DebugColor = FColor();
#endif // WITH_MASSENTITY_DEBUG
}

/** 转发到 SubsystemAccess::CacheSubsystemRequirements。详见其注释。 */
bool FMassExecutionContext::CacheSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
{
	return SubsystemAccess.CacheSubsystemRequirements(SubsystemRequirements);
}

/** 拷贝版 SetEntityCollection：要求当前 EntityCollection 为空（防覆盖错误）。 */
void FMassExecutionContext::SetEntityCollection(const FMassArchetypeEntityCollection& InEntityCollection)
{
	check(EntityCollection.IsEmpty());
	EntityCollection = InEntityCollection;
}

/**
 * 移动版 SetEntityCollection：除了"当前必须为空"，还多一条"InEntityCollection 必须 up-to-date"，
 * 因为移动后调用方失去对它的所有权，无法再检查/更新。
 */
void FMassExecutionContext::SetEntityCollection(FMassArchetypeEntityCollection&& InEntityCollection)
{
	check(EntityCollection.IsEmpty());
	check(InEntityCollection.IsUpToDate());
	EntityCollection = MoveTemp(InEntityCollection);
}

/**
 * 把 query 的 FragmentRequirements 翻译为 4 类 FragmentView 容器条目（仅 Requirement 描述部分；
 * 真正的视图绑定由 archetype 在切到 chunk 时填）。
 *
 * 调用时机：
 *  - 通常由 ApplyFragmentRequirements(const FMassEntityQuery&) 内部转发来；
 *  - 此函数会先 Reset 所有 4 个容器，再按 query.GetXxxFragmentRequirements() 顺序追加。
 *
 * "RequiresBinding()" 的语义：只有在 query 中明确声明 ReadOnly/ReadWrite 的 fragment 才需要 view 绑定，
 * 单纯 All/Any/None 这种 archetype 过滤用 requirement 不会进 FragmentViews（因为不需要在 lambda 中访问数据）。
 */
void FMassExecutionContext::SetFragmentRequirements(const FMassFragmentRequirements& FragmentRequirements)
{
	FragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			FragmentViews.Emplace(Requirement);
		}
	}

	ChunkFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetChunkFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			ChunkFragmentViews.Emplace(Requirement);
		}
	}

	ConstSharedFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetConstSharedFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			ConstSharedFragmentViews.Emplace(Requirement);
		}
	}

	SharedFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetSharedFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			SharedFragmentViews.Emplace(Requirement);
		}
	}
}

/** 直接转发：World 由 EntityManager 持有。可能为 nullptr（编辑器场景）。 */
UWorld* FMassExecutionContext::GetWorld() 
{ 
	return EntityManager->GetWorld(); 
}

/**
 * 把 InQuery 入栈，并保存当前 SubsystemAccess 位掩码现场到栈顶。
 *
 * 关于 IteratorSerialNumber：每次 PushQuery 都会自增生成器并赋给栈顶 runtime。
 * FEntityIterator 构造时会拷贝这个序号；用于诊断"chunk/query 切换后旧迭代器复用"等错误。
 *
 * Debug 路径：
 *  - bCheckProcessorBreaks：MassDebugger 中是否对当前 processor 设了任何断点；
 *  - 然后把所有"对 fragment 写访问且有写入断点"的类型记录到 FragmentTypesToBreakOn 数组，
 *    供 FEntityIterator::TestBreakpoints 在 ++ 时快速命中。
 *  - 注意 ConstSharedFragments 不会被检查写断点（它们本就只读）。
 */
void FMassExecutionContext::PushQuery(FMassEntityQuery& InQuery)
{
	FQueryTransientRuntime& RuntimeData = QueriesStack.Add_GetRef({&InQuery});
	GetSubsystemRequirementBits(RuntimeData.ConstSubsystemsBitSet, RuntimeData.MutableSubsystemsBitSet);

#if WITH_MASSENTITY_DEBUG
	// check if this could possibly trigger a break before iterating to avoid extraneous breakpoint checks
	FMassEntityManager& EntityManagerRef = GetEntityManagerChecked();

	RuntimeData.bCheckProcessorBreaks = FMassDebugger::HasAnyProcessorBreakpoints(EntityManagerRef, DebugGetProcessor());
	
	if (UNLIKELY(FMassDebugger::HasAnyFragmentWriteBreakpoints(EntityManagerRef)))
	{
		// 内嵌 lambda：扫描某类 requirement 列表，对每个 ReadWrite 且有断点的 StructType 加入 FragmentTypesToBreakOn。
		auto CheckFragmentRequirement = [&RuntimeData, &EntityManagerRef](TConstArrayView<FMassFragmentRequirementDescription> Requirements) -> void
			{
				for (const FMassFragmentRequirementDescription& Req : Requirements)
				{
					if (Req.AccessMode == EMassFragmentAccess::ReadWrite &&
						FMassDebugger::HasAnyFragmentWriteBreakpoints(EntityManagerRef, Req.StructType))
					{
						if (ensureMsgf(RuntimeData.BreakFragmentsCount < FQueryTransientRuntime::MaxFragmentBreakpointCount, 
							TEXT("Fragment write breakpoint count limit exceeded for this query.")))
						{
							RuntimeData.FragmentTypesToBreakOn[RuntimeData.BreakFragmentsCount++] = Req.StructType;
						}
					}
				}
			};

		// don't need to check ConstSharedFragmentRequirements because those can't write
		CheckFragmentRequirement(InQuery.GetFragmentRequirements());
		CheckFragmentRequirement(InQuery.GetChunkFragmentRequirements());
		CheckFragmentRequirement(InQuery.GetSharedFragmentRequirements());
	}
#endif // WITH_MASSENTITY_DEBUG

	RuntimeData.IteratorSerialNumber = ++IteratorSerialNumberGenerator;
}

/**
 * 出栈：
 *  - 校验 InQuery == 栈顶 Query（LIFO 一致性，否则提示嵌套写错了）；
 *  - 用栈顶保存的位掩码恢复 SubsystemAccess（这样外层 query 的 subsystem 访问权限重新生效）；
 *  - 弹出栈顶元素。
 *
 * EAllowShrinking::No：不缩容数组以保持 InlineAllocator 命中。
 */
void FMassExecutionContext::PopQuery(const FMassEntityQuery& InQuery)
{
	const FQueryTransientRuntime& RuntimeData = QueriesStack.Last();
	checkf(&InQuery == RuntimeData.Query, TEXT("Queries are stored in a stack and as such it requires elements to be added in LIFO order"));

	SetSubsystemRequirementBits(RuntimeData.ConstSubsystemsBitSet, RuntimeData.MutableSubsystemsBitSet);

	QueriesStack.RemoveAt(QueriesStack.Num() - 1, EAllowShrinking::No);
}

/**
 * 创建当前 chunk 的迭代器。
 *  - 栈非空时走正式构造（绑定到栈顶 runtime）；
 *  - 栈为空时 testableEnsureMsgf 警告并返回容错的"仅持有 Context 但无 runtime"的迭代器，
 *    其 SerialNumber==0 → operator bool 永远 false → for 循环立即结束，不会触发 UB。
 */
FMassExecutionContext::FEntityIterator FMassExecutionContext::CreateEntityIterator()
{
	if (!testableEnsureMsgf(QueriesStack.Num(), TEXT("Attempting to create an Entity Iterator when no entity query is being executed.")))
	{
		return FEntityIterator(*this);
	}

	return FEntityIterator(*this, QueriesStack.Last());
}

/**
 * 全局 dummy Context 单例。
 * 用于 FEntityIterator 默认构造时引用一个合法但内部空的 ExecutionContext，避免裸 nullptr。
 *
 * @note 这里 new FMassEntityManager() 后立即 wrap 成 TSharedRef 并在 dummy 内永久持有。
 *       该 dummy 在进程生命周期内存在，由 static 局部变量管理。
 */
FMassExecutionContext& FMassExecutionContext::GetDummyInstance()
{
	static FMassExecutionContext DummyContext(*TSharedRef<FMassEntityManager>(MakeShareable(new FMassEntityManager())));
	return DummyContext;
}

//-----------------------------------------------------------------------------
// FMassExecutionContext::FQueryTransientRuntime
//-----------------------------------------------------------------------------
/**
 * 全局 dummy FQueryTransientRuntime 单例。
 * 配合 FEntityIterator 默认构造，提供一个合法的 Query 指针避免 TNotNull 校验失败。
 */
FMassExecutionContext::FQueryTransientRuntime& FMassExecutionContext::FQueryTransientRuntime::GetDummyInstance()
{
	static FMassEntityQuery DummyQuery;
	static FQueryTransientRuntime DummyInstance = { &DummyQuery };
	return DummyInstance;
}

//-----------------------------------------------------------------------------
// FMassExecutionContext::FEntityIterator
//-----------------------------------------------------------------------------
/**
 * 默认构造：把 ExecutionContext / QueryRuntime 都指向 dummy 单例。
 * 此时 SerialNumber==0、NumEntities==INDEX_NONE，operator bool() 始终为 false，
 * 适合作为 end() 哨兵或容错路径下的"空迭代器"。
 */
FMassExecutionContext::FEntityIterator::FEntityIterator()
	: ExecutionContext(FMassExecutionContext::GetDummyInstance())
	, QueryRuntime(FQueryTransientRuntime::GetDummyInstance())
{
	
}

/**
 * 容错构造：有合法 Context 但无合法 QueryRuntime（CreateEntityIterator 在栈空时使用）。
 * SerialNumber 仍为 0 → 迭代器立即结束。
 */
FMassExecutionContext::FEntityIterator::FEntityIterator(FMassExecutionContext& InExecutionContext)
	: ExecutionContext(InExecutionContext)
	, QueryRuntime(FQueryTransientRuntime::GetDummyInstance())
{
	
}

/**
 * 正式构造：
 *  - NumEntities 取自 Context.GetNumEntities()（即 EntityListView.Num()）。
 *  - SerialNumber 拷贝自 QueryRuntime.IteratorSerialNumber，保证非 0。
 *  - 末尾调用 ++(*this) 把 EntityIndex 从 INDEX_NONE 推进到 0，并触发首个 entity 的断点检查。
 *    这样调用方写 for (FEntityIterator It = ...; It; ++It) 时，
 *    第一次取 *It 已经是 entity 0 而非 INDEX_NONE。
 */
FMassExecutionContext::FEntityIterator::FEntityIterator(FMassExecutionContext& InExecutionContext, FQueryTransientRuntime& InQueryRuntime)
	: ExecutionContext(InExecutionContext)
	, QueryRuntime(InQueryRuntime)
	, NumEntities(InExecutionContext.GetNumEntities())
	, SerialNumber(InQueryRuntime.IteratorSerialNumber)
{
	this->operator++();
}

#if WITH_MASSENTITY_DEBUG

// 关闭优化：避免编译器把 UE_DEBUG_BREAK 周围的局部变量优化掉，
// 调试时能在 Watch 窗口中读写 bDisableThisBreakpoint 来禁用单个断点。
UE_DISABLE_OPTIMIZATION_SHIP
/**
 * 在 ++iterator 时检查是否命中 MassDebugger 的断点（基于当前 entity）。
 * 检查两类：
 *   1) processor-级断点（针对当前 processor + entity 的组合）；
 *   2) fragment-write-级断点（针对当前 entity 中即将被写的某 fragment 类型）。
 *
 * 命中后：
 *   - 触发 UE_DEBUG_BREAK 让调试器停下；
 *   - 提供 bDisableThisBreakpoint 局部变量，用户在 Watch 中改成 true 即可禁用此断点继续调试。
 *   - 同一帧内同一 (entity, processor) 命中后立即 return，避免重复触发。
 */
void FMassExecutionContext::FEntityIterator::TestBreakpoints()
{
	FMassEntityManager& EntityManagerRef = ExecutionContext.GetEntityManagerChecked();
	FMassEntityHandle Entity = ExecutionContext.GetEntity(EntityIndex);
	if (QueryRuntime.bCheckProcessorBreaks)
	{
		if (UE::Mass::Debug::FBreakpointHandle BreakHandle = FMassDebugger::ShouldProcessorBreak(EntityManagerRef, ExecutionContext.DebugGetProcessor(), Entity))
		{
			bool bDisableThisBreakpoint = false;
			//====================================================================
			//= A breakpoint for this entity set in the MassDebugger has triggered
			//= Step out of this function to debug the actual code being run for the entity
			//=
			//= To disable this specific breakpoint use the Watch window to set
			//= bDisableThisBreakpoint to `true` or 1
			//====================================================================
			UE_DEBUG_BREAK();

			if (bDisableThisBreakpoint)
			{
				FMassDebugger::SetBreakpointEnabled(BreakHandle, false);
			}

			// bailing out, no point to hit multiple breakpoints for the given entity/processor pair
			return;
		}
	}

	for (const UScriptStruct* Fragment : QueryRuntime.FragmentTypesToBreakOn)
	{
		if (UE::Mass::Debug::FBreakpointHandle BreakHandle = FMassDebugger::ShouldBreakOnFragmentWrite(EntityManagerRef, Fragment, Entity))
		{
			bool bDisableThisBreakpoint = false;
			//====================================================================
			//= A breakpoint for this entity set in the MassDebugger has triggered
			//= Step out of this function to debug the actual code being run for the entity
			// 
			//= To disable this specific breakpoint use the Watch window to set
			//= bDisableThisBreakpoint to `true` or 1
			//====================================================================
			UE_DEBUG_BREAK();

			if (bDisableThisBreakpoint)
			{
				FMassDebugger::SetBreakpointEnabled(BreakHandle, false);
			}

			// bailing out, no point to hit multiple breakpoints for the given entity/processor pair
			return;
		}
	}
}
UE_ENABLE_OPTIMIZATION_SHIP

#endif // WITH_MASSENTITY_DEBUG