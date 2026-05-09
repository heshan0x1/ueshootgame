// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassExecutor.h —— Mass 处理器执行的统一入口
// -----------------------------------------------------------------------------
// 命名空间 UE::Mass::Executor 下提供一组自由函数，作为"把 processor 真正跑起来"的
// 唯一对外入口。所有 Phase / Observer / 手动派发场景都最终汇聚到这里。
//
// 调用模型：
//     ┌─────────────────┐  Run/RunSparse/...   ┌──────────────────┐
//     │ FProcessingCtx  │ ───────────────────▶│  ExecutionContext │
//     │ (调用方填票据)   │                      │ (processor 用上下文)│
//     └─────────────────┘                      └──────────────────┘
//                                                       │
//                                                       ▼
//                                               UMassProcessor::CallExecute
//                                                       │
//                                                       ▼
//                                               EntityQuery → ECS 数据
//
// 三类执行：
//   1) Run            —— 顺序执行，按数组顺序串行 CallExecute。
//   2) RunSparse      —— 顺序执行，但只针对指定的 entity 子集（observer/事件触发场景）。
//   3) TriggerParallelTasks —— 并行执行，把 processor 派到 task graph，返回 FGraphEventRef。
//
// 命令应用：所有同步路径在 FProcessingContext 析构时统一 flush（或 append）；
// 并行路径单独在 FMassExecutorDoneTask 中 flush（见 .cpp）。
// =============================================================================

#pragma once

#include "MassArchetypeTypes.h"
#include "Async/TaskGraphInterfaces.h"


struct FMassRuntimePipeline;
namespace UE::Mass
{
	struct FProcessingContext;
}
struct FMassEntityHandle;
struct FMassArchetypeEntityCollection;
class UMassProcessor;


namespace UE::Mass::Executor
{
	/** Executes processors in a given RuntimePipeline */
	// 中文：顺序执行 Pipeline 中的所有 processor。
	//   * 内部把 RuntimePipeline.GetMutableProcessors() 转成 view，转交 RunProcessorsView。
	//   * DeltaSeconds 必须 >= 0（ensure 检查）；pipeline 不能含 nullptr 元素。
	//   * 调用线程：通常 GameThread；如果调用方明确知道是线程安全的也可 worker 线程。
	MASSENTITY_API void Run(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext);

	/** Executes given Processor. Used mainly for triggering calculations via MassCompositeProcessors, e.g processing phases */
	// 中文：顺序执行单个 processor —— Phase 派发的主路径
	//   FMassProcessingPhase::ExecuteTick 在单线程模式下就调它，传入 PhaseProcessor（一个 UMassCompositeProcessor）。
	//   composite processor 会递归把 child processors 跑完。
	MASSENTITY_API void Run(UMassProcessor& Processor, FProcessingContext& ProcessingContext);

	/** Similar to the Run function, but instead of using all the entities hosted by ProcessingContext.EntitySubsystem 
	 *  it is processing only the entities given by EntityID via the Entities input parameter. 
	 *  Note that all the entities need to be of Archetype archetype. 
	 *  Under the hood the function converts Archetype-Entities pair to FMassArchetypeEntityCollection and calls the other flavor of RunSparse
	 *
	 *  中文：稀疏执行（按 entity 子集触发） —— 主要用于 observer 场景
	 *  例如某 entity 加上一个 fragment，observer 需要立刻只对这个 entity 触发一次某 processor。
	 *  @param Archetype  被触发的 entity 必须共属同一个 archetype
	 *  @param Entities   要处理的 entity 列表（不允许重复，由 NoDuplicates 标志保证）
	 */
	MASSENTITY_API void RunSparse(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext, FMassArchetypeHandle Archetype, TConstArrayView<FMassEntityHandle> Entities);

	/** Similar to the Run function, but instead of using all the entities hosted by ProcessingContext.EntitySubsystem 
	 *  it is processing only the entities given by SparseEntities input parameter.
	 *  @todo rename
	 *
	 *  中文：同上，但直接接受已经构造好的 EntityCollection（避免重复装箱）。
	 *  典型流程：observer 已经把"被触发的 entity"组织成 collection；这里直接喂进来。
	 */
	MASSENTITY_API void RunSparse(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection);

	/** Executes given Processors array view. This function gets called under the hood by the rest of Run* functions */
	// 中文：所有 Run/RunSparse 最终都走这里 —— 真正的"执行循环"。
	//   * 创建 EntityManager 的 FScopedProcessing（防止处理过程中 archetype 结构被破坏性修改）；
	//   * 若 EntityCollections 为空 → 全量遍历；
	//   * 若有 collections → 对每个 collection 设置 ExecutionContext.EntityCollection 后再跑一遍 processor 链。
	//   * 跳过 IsActive() == false 的 processor。
	MASSENTITY_API void RunProcessorsView(TArrayView<UMassProcessor* const> Processors, FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections = {});

	/** 
	 *  Triggers tasks executing Processor (and potentially it's children) and returns the task graph event representing 
	 *  the task (the event will be "completed" once all the processors finish running). 
	 *  @param OnDoneNotification will be called after all the processors are done, just after flushing the command buffer.
	 *    Note that OnDoneNotification will be executed on GameThread.
	 *
	 *  中文：把 processor 派发到 task graph 并行执行。
	 *  返回值：FGraphEventRef —— 调用方可以 DontCompleteUntil 它，等并行工作完成。
	 *  典型调用：FMassProcessingPhase::ExecuteTick 在并行模式下用 TriggerParallelTasks 把 PhaseProcessor 派出去，
	 *  并把返回的 event 串到 tick 函数的 MyCompletionGraphEvent 上，让 UE 调度系统知道"phase 还没结束"。
	 *
	 *  实现要点（见 .cpp）：
	 *    * ProcessingContext 是 rvalue 传入 —— 内部 ExecutionContext 被 move 走，原 context 析构时不再 flush；
	 *    * Processor::DispatchProcessorTasks 是真正的并行派发（由 dependency solver 的求解结果决定 task 拓扑）；
	 *    * 派发完后追加一个 FMassExecutorDoneTask，等所有 processor task 完成后到 GameThread 做：
	 *        - 把 ExecutionContext.Defer() 中累积的命令并回 EntityManager；
	 *        - 标记 SetFlushDeferredCommands(true)，调用 FlushDeferred() 真正应用命令；
	 *        - 调用 OnDoneNotification（典型回调：FMassProcessingPhase::OnParallelExecutionDone）。
	 *
	 *  @param Processor             要执行的 processor（可以是 composite，会递归派发子 processor）
	 *  @param ProcessingContext     必须按 rvalue 传入 —— 所有权转移
	 *  @param OnDoneNotification    完成回调，在 GameThread 上执行
	 *  @param CurrentThread         指定 OnDoneNotification 期望运行的 named thread
	 */
	MASSENTITY_API FGraphEventRef TriggerParallelTasks(UMassProcessor& Processor, FProcessingContext&& ProcessingContext, TFunction<void()> OnDoneNotification
		, ENamedThreads::Type CurrentThread = ENamedThreads::GameThread);

	// 5.5 起废弃：单个 EntityCollection* 的 RunProcessorsView 重载。请改用 TConstArrayView 版本。
	UE_DEPRECATED(5.5, "This flavor of RunProcessorsView is deprecated. Use the one with TConstArrayView<FMassArchetypeEntityCollection> parameter instead.")
	MASSENTITY_API void RunProcessorsView(TArrayView<UMassProcessor* const> Processors, FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection* EntityCollection);

	// 5.6 起废弃：lvalue 版 TriggerParallelTasks。改用 rvalue 版本，强调 ProcessingContext 所有权转移。
	UE_DEPRECATED(5.6, "lvalue flavor of TriggerParallelTasks has been deprevate. Use the rvalue version.")
	MASSENTITY_API FGraphEventRef TriggerParallelTasks(UMassProcessor& Processor, FProcessingContext& ProcessingContext, TFunction<void()> OnDoneNotification
		, ENamedThreads::Type CurrentThread = ENamedThreads::GameThread);
};
