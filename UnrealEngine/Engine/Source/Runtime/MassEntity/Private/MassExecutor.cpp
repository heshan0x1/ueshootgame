// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassExecutor.cpp —— UE::Mass::Executor 自由函数的实现
// -----------------------------------------------------------------------------
// 该文件是 Mass processor 执行引擎的核心。所有"开跑 processor"的请求最终都会
// 经过这里的 RunProcessorsView（同步路径）或 TriggerParallelTasks（并行路径）。
// =============================================================================

#include "MassExecutor.h"
#include "MassProcessingTypes.h"
#include "MassProcessor.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"
#include "MassProcessingContext.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::Mass::Executor
{

/**
 * 内部辅助：按顺序对 processors 依次调用 CallExecute。
 *   * 跳过 IsActive() == false 的 processor —— 这是 processor 的"开关"，运行期可被切换；
 *   * 不做并发派发 —— 这是同步路径；
 *   * EntityManager 与 ExecutionContext 由调用方维护一致性（已被 FScopedProcessing 保护）。
 */
FORCEINLINE void ExecuteProcessors(FMassEntityManager& EntityManager, TArrayView<UMassProcessor* const> Processors, FMassExecutionContext& ExecutionContext)
{
	for (UMassProcessor* Proc : Processors)
	{
		if (LIKELY(Proc->IsActive()))
		{
			Proc->CallExecute(EntityManager, ExecutionContext);
		}
	}
}

// 顺序执行整条 pipeline。
//   * 先校验 DeltaSeconds 与 pipeline 完整性（无 nullptr）；
//   * 转交 RunProcessorsView 真正干活。
void Run(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext)
{
	if (!ensure(ProcessingContext.GetDeltaSeconds() >= 0.f) 
		|| !ensure(RuntimePipeline.GetProcessors().Find(nullptr) == INDEX_NONE))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("MassExecutor Run Pipeline")
	RunProcessorsView(RuntimePipeline.GetMutableProcessors(), ProcessingContext);
}

// 稀疏执行（按 archetype + entity 列表触发） —— observer 路径常用。
// 把 Archetype + Entities 装成 FMassArchetypeEntityCollection 后转交 RunProcessorsView。
// NoDuplicates 标志告诉 collection 构造函数：调用方保证无重复，可以跳过去重排序步骤。
void RunSparse(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext, FMassArchetypeHandle Archetype, TConstArrayView<FMassEntityHandle> Entities)
{
	if (!ensure(RuntimePipeline.GetProcessors().Find(nullptr) == INDEX_NONE) 
		|| RuntimePipeline.Num() == 0 
		|| !ensureMsgf(Archetype.IsValid(), TEXT("The Archetype passed in to UE::Mass::Executor::RunSparse is invalid")))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("MassExecutor RunSparseEntities");

	const FMassArchetypeEntityCollection EntityCollection(Archetype, Entities, FMassArchetypeEntityCollection::NoDuplicates);
	RunProcessorsView(RuntimePipeline.GetMutableProcessors(), ProcessingContext, MakeArrayView(&EntityCollection, 1));
}

// 稀疏执行（已构造好的 collection 版） —— 跳过装箱。
void RunSparse(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection)
{
	if (!ensure(RuntimePipeline.GetProcessors().Find(nullptr) == INDEX_NONE) 
		|| RuntimePipeline.Num() == 0
		|| !ensureMsgf(EntityCollection.GetArchetype().IsValid(), TEXT("The Archetype of EntityCollection passed in to UE::Mass::Executor::RunSparse is invalid")))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("MassExecutor RunSparse");

	RunProcessorsView(RuntimePipeline.GetMutableProcessors(), ProcessingContext, MakeArrayView(&EntityCollection, 1));
}

// 顺序执行单个 processor —— Phase 单线程模式的主路径。
// 内部把 processor 包成单元素 view 后转交 RunProcessorsView。
void Run(UMassProcessor& Processor, FProcessingContext& ProcessingContext)
{
	if (!ensure(ProcessingContext.GetDeltaSeconds() >= 0.f))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("MassExecutor Run")

	UMassProcessor* ProcPtr = &Processor;
	RunProcessorsView(MakeArrayView(&ProcPtr, 1), ProcessingContext);
}

/**
 * 同步执行的核心实现 —— 所有 Run/RunSparse 最终汇聚到这里。
 *
 * 流程：
 *   1) DEBUG 校验 processors 中无 nullptr；
 *   2) 触发 ProcessingContext 的 lazy 构造，拿到 ExecutionContext（含 CommandBuffer/AuxData/...）；
 *   3) 拿到 EntityManager 引用；
 *   4) 申请 FScopedProcessing —— 处理期间禁止破坏性 archetype 操作（保证 ECS 数据一致性）；
 *   5) 若没有 EntityCollections（全量遍历）：直接 ExecuteProcessors；
 *      否则：对每个 collection 依次 SetEntityCollection → ExecuteProcessors → ClearEntityCollection。
 *
 * 命令应用：本函数不主动 flush —— 由 FProcessingContext 析构时按 bFlushCommandBuffer 决定。
 */
void RunProcessorsView(TArrayView<UMassProcessor* const> Processors, FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections)
{
#if WITH_MASSENTITY_DEBUG
	if (Processors.Find(nullptr) != INDEX_NONE)
	{
		UE_LOG(LogMass, Error, TEXT("%s input Processors contains nullptr. Baling out."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}
#endif // WITH_MASSENTITY_DEBUG

	TRACE_CPUPROFILER_EVENT_SCOPE(MassExecutor_RunProcessorsView);

	FMassExecutionContext& ExecutionContext = ProcessingContext.GetExecutionContext();
	FMassEntityManager& EntityManager = *ProcessingContext.GetEntityManager();
	// FScopedProcessing：在作用域内告诉 EntityManager 现在正在执行 processor，
	// 阻止某些破坏性的 archetype 操作（具体语义见 FMassEntityManager 实现）。
	FMassEntityManager::FScopedProcessing ProcessingScope = EntityManager.NewProcessingScope();

	if (EntityCollections.Num() == 0)
	{
		// 全量执行：query 自行决定要 match 哪些 archetype/entity
		ExecuteProcessors(EntityManager, Processors, ExecutionContext);
	}
	else
	{
		// 稀疏执行：按 collection 一份一份地跑 ——  ExecutionContext 中"当前 collection"会
		// 让 EntityQuery 只迭代 collection 内的 entity（而不是匹配 archetype 的全部 entity）
		for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
		{
			ExecutionContext.SetEntityCollection(Collection);
			ExecuteProcessors(EntityManager, Processors, ExecutionContext);
			ExecutionContext.ClearEntityCollection();
		}
	}
}

/**
 * FMassExecutorDoneTask —— 并行执行的"收尾任务"。
 *
 * 当 TriggerParallelTasks 派发的所有 processor task 都完成后，task graph 会调度这个 task 到指定线程
 * （通常 GameThread）做最后的收尾工作：
 *   1) 把 ExecutionContext.Defer() 中累积的命令并回 EntityManager.Defer()（合并主缓冲）；
 *   2) 把 ExecutionContext 切换为"立即 flush"模式并调用 FlushDeferred() —— 真正应用结构变更；
 *   3) 调用调用方提供的 OnDoneNotification 回调（典型回调是 FMassProcessingPhase::OnParallelExecutionDone）。
 *
 * 持有的 ExecutionContext 已是 move 进来的副本（拥有 CommandBuffer），并行 task 期间外层 FProcessingContext
 * 已经析构而不会重复 flush。
 */
struct FMassExecutorDoneTask
{
	FMassExecutorDoneTask(FMassExecutionContext&& InExecutionContext, TFunction<void()> InOnDoneNotification, const FString& InDebugName, ENamedThreads::Type InDesiredThread)
		: ExecutionContext(InExecutionContext)
		, OnDoneNotification(InOnDoneNotification)
		, DebugName(InDebugName)
		, DesiredThread(InDesiredThread)
	{
	}
	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMassExecutorDoneTask, STATGROUP_TaskGraphTasks);
	}

	// 期望运行此收尾 task 的线程（通常是 GameThread —— 因为 flush 命令涉及修改 EntityManager 全局状态）
	ENamedThreads::Type GetDesiredThread()
	{
		return DesiredThread;
	}

	// 允许后续 task 跟踪本 task 的完成事件（外层把它包装为 CompletionEvent 返回给调用方）
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	// task graph 的入口函数
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Flush Deferred Commands Parallel");
		SCOPE_CYCLE_COUNTER(STAT_Mass_Total);

		FMassEntityManager& EntityManagerRef = ExecutionContext.GetEntityManagerChecked();

		// 若 ExecutionContext 用的是独立 deferred 缓冲（而不是 EntityManager 主缓冲），
		// 把它的命令并入主缓冲。MoveAppend 会清空源缓冲。
		if (&ExecutionContext.Defer() != &EntityManagerRef.Defer())
		{
			ExecutionContext.Defer().MoveAppend(EntityManagerRef.Defer());
		}

		UE_LOG(LogMass, Verbose, TEXT("MassExecutor %s tasks DONE"), *DebugName);
		// 切到"立即 flush"模式 —— 之前一直关闭以便让命令累积到本 task 完成
		ExecutionContext.SetFlushDeferredCommands(true);
		ExecutionContext.FlushDeferred();

		// 调用上层回调（典型：FMassProcessingPhase 通知 phase 结束、广播 OnPhaseEnd）
		OnDoneNotification();
	}
private:
	FMassExecutionContext ExecutionContext;       // 从 FProcessingContext move 过来的执行上下文
	TFunction<void()> OnDoneNotification;          // 完成回调
	FString DebugName;                             // 用于 trace/log，通常是 processor 名
	ENamedThreads::Type DesiredThread;             // 期望执行 DoTask 的命名线程
};

/**
 * TriggerParallelTasks —— 并行执行入口。
 *
 * 步骤：
 *   1) 把 rvalue ProcessingContext 中的 ExecutionContext 移走 —— 这样原 context 析构时就不再 flush 命令，
 *      命令缓冲的所有权也跟着 move 给本函数中的 ExecutionContext 局部变量；
 *   2) 调用 Processor.DispatchProcessorTasks —— 由 dependency solver 求解出的拓扑驱动并行派发；
 *      返回的 CompletionEvent 是"所有 processor task 完成"的事件；
 *   3) 若 CompletionEvent 有效，追加一个 FMassExecutorDoneTask（以 CompletionEvent 为前置依赖）做收尾；
 *      新生成的 event 替换为外部观察的 CompletionEvent，让外部既能等所有 processor 跑完，
 *      又能等 OnDoneNotification 跑完。
 *
 * 注意：本函数返回后 ProcessingContext（rvalue 形参）即将销毁；ExecutionContext 已 move 走，
 *       所以析构不会再 flush 命令缓冲 —— 真正的 flush 由 FMassExecutorDoneTask::DoTask 完成。
 */
FGraphEventRef TriggerParallelTasks(UMassProcessor& Processor, FProcessingContext&& ProcessingContext, TFunction<void()> OnDoneNotification, ENamedThreads::Type CurrentThread)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MassExecutor_RunParallel);

	// We need to transfer ProcessingContext's ExecutionContext - otherwise ProcessingContext's destructor will attempt
	// flushing stored commands. 
	// 中文：把 ExecutionContext 从 ProcessingContext 中"搬走"，避免 ProcessingContext 析构时还去 flush 命令
	// （那样会造成"我们还在用 CommandBuffer，外面已经把它应用了"的竞态）。
	FMassExecutionContext ExecutionContext = MoveTemp(ProcessingContext).GetExecutionContext();

	FGraphEventRef CompletionEvent;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Dispatch Processors")
		// 派发所有 processor 到 task graph；事件代表所有 task 的完成
		// 第三参数空 array 表示不限制 entity collections —— 全量执行
		CompletionEvent = Processor.DispatchProcessorTasks(ProcessingContext.GetEntityManager(), ExecutionContext, {});
	}

	if (CompletionEvent.IsValid())
	{
		// 在 processor task 全部完成后，追加一个收尾 task
		const FGraphEventArray Prerequisites = { CompletionEvent };
		CompletionEvent = TGraphTask<FMassExecutorDoneTask>::CreateTask(&Prerequisites)
			.ConstructAndDispatchWhenReady(MoveTemp(ExecutionContext), OnDoneNotification, Processor.GetName(), CurrentThread);
	}

	return CompletionEvent;
}

//-----------------------------------------------------------------------------
// DEPRECATED —— 已废弃 API，仅作向后兼容
//-----------------------------------------------------------------------------
// 5.5 起废弃：单 EntityCollection 指针的 RunProcessorsView。
void RunProcessorsView(TArrayView<UMassProcessor* const> Processors, FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection* EntityCollection)
{
	if (EntityCollection)
	{
		RunProcessorsView(Processors, ProcessingContext, MakeArrayView(EntityCollection, 1));
	}
	else
	{
		RunProcessorsView(Processors, ProcessingContext);
	}
}

// 5.6 起废弃：lvalue 版 TriggerParallelTasks。这里通过拷贝构造一个 LocalContext 后再 move 入 rvalue 版本。
// 注意：LocalContext 仅作"占位"，真正的并行执行用的是从 ProcessingContext 移过来的版本。
inline FGraphEventRef TriggerParallelTasks(UMassProcessor& Processor, FProcessingContext& ProcessingContext, TFunction<void()> OnDoneNotification
		, ENamedThreads::Type CurrentThread)
{
	FProcessingContext LocalContext = ProcessingContext;
	return TriggerParallelTasks(Processor, MoveTemp(ProcessingContext), OnDoneNotification, CurrentThread);
}

} // namespace UE::Mass::Executor
