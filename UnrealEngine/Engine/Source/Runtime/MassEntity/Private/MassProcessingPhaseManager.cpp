// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassProcessingPhaseManager.cpp —— phase 调度协调员的实现
// -----------------------------------------------------------------------------
// 实现 .h 中声明的运行时调度逻辑：
//   * Phase ↔ TickGroup 映射表
//   * FMassProcessingPhase::ExecuteTick —— phase 的"心跳"
//   * FMassPhaseProcessorConfigurationHelper::Configure —— 依赖图构建
//   * FMassProcessingPhaseManager 全部成员实现
// =============================================================================

#include "MassProcessingPhaseManager.h"
#include "MassProcessingTypes.h"
#include "MassDebugger.h"
#include "MassProcessor.h"
#include "MassExecutor.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "MassCommandBuffer.h"
#include "MassEntityTrace.h"
#include "MassProcessingContext.h"
#include "Misc/StringOutputDevice.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassProcessingPhaseManager)

#define LOCTEXT_NAMESPACE "Mass"

// 性能计数器：phase tick 总耗时 / 一次配置 pipeline 创建耗时
DECLARE_CYCLE_STAT(TEXT("Mass Phase Tick"), STAT_Mass_PhaseTick, STATGROUP_Mass);
DECLARE_CYCLE_STAT(TEXT("Mass Phase Configure Pipeline Creation"), STAT_Mass_PhaseConfigurePipelineCreation, STATGROUP_Mass);

namespace UE::Mass::Tweakables
{
	// mass.FullyParallel —— 是否启用全并行模式（开关在 phase 起始处同步到每个 phase）
	bool bFullyParallel = MASS_DO_PARALLEL;
	// mass.MakePrePhysicsTickFunctionHighPriority —— 是否把 PrePhysics phase 设为高优先级
	// 收益：让 task graph 尽早开始并行工作，最小化 GameThread 等待
	bool bMakePrePhysicsTickFunctionHighPriority = true;

	FAutoConsoleVariableRef CVars[] = {
		{TEXT("mass.FullyParallel"), bFullyParallel, TEXT("Enables mass processing distribution to all available thread (via the task graph)")},
		{TEXT("mass.MakePrePhysicsTickFunctionHighPriority"), bMakePrePhysicsTickFunctionHighPriority, TEXT("Whether to make the PrePhysics tick function high priority - can minimise GameThread waits by starting parallel work as soon as possible")},
	};
}

namespace UE::Mass::Private
{
	// EMassProcessingPhase → ETickingGroup 的映射表
	// PrePhysics    → TG_PrePhysics      （UE 内置：物理之前）
	// StartPhysics  → TG_StartPhysics    （物理开始前一刻）
	// DuringPhysics → TG_DuringPhysics   （与物理并行）
	// EndPhysics    → TG_EndPhysics      （物理结束后一刻）
	// PostPhysics   → TG_PostPhysics     （物理之后）
	// FrameEnd      → TG_LastDemotable   （帧最末尾，可被降级到下一帧的 group —— 对于不紧急的工作合适）
	ETickingGroup PhaseToTickingGroup[int(EMassProcessingPhase::MAX)]
	{
		ETickingGroup::TG_PrePhysics, // EMassProcessingPhase::PrePhysics
		ETickingGroup::TG_StartPhysics, // EMassProcessingPhase::StartPhysics
		ETickingGroup::TG_DuringPhysics, // EMassProcessingPhase::DuringPhysics
		ETickingGroup::TG_EndPhysics,	// EMassProcessingPhase::EndPhysics
		ETickingGroup::TG_PostPhysics,	// EMassProcessingPhase::PostPhysics
		ETickingGroup::TG_LastDemotable, // EMassProcessingPhase::FrameEnd
	};
} // UE::Mass::Private

//----------------------------------------------------------------------//
//  FMassProcessingPhase
//----------------------------------------------------------------------//
// 构造：默认参数。注意 bStartWithTickEnabled = false —— 在 RegisterTickFunction 后由
// EnableTickFunctions 显式 SetTickFunctionEnable(true) 才开始 tick。
FMassProcessingPhase::FMassProcessingPhase()
{
	bCanEverTick = true;
	bStartWithTickEnabled = false;
	// 默认支持 LEVELTICK_All（普通 tick）和 LEVELTICK_TimeOnly（暂停游戏但还需走时间的场景）
	SupportedTickTypes = (1 << LEVELTICK_All) | (1 << LEVELTICK_TimeOnly);
}

/**
 * ExecuteTick —— phase 的"心跳"，由 UE 调度系统在对应 TickGroup 期间调用
 *
 * 流程：
 *   1) 校验 SupportedTickTypes / PhaseManager；
 *   2) 通知 PhaseManager.OnPhaseStart（内部可能重建依赖图、处理 pending 动态 processor）；
 *   3) 广播 OnPhaseStart 委托；
 *   4) 根据 bRunInParallelMode 选择并行/同步模式：
 *        并行模式：TriggerParallelTasks 派发，把返回的 event 串到 MyCompletionGraphEvent->DontCompleteUntil；
 *                  完成回调 OnParallelExecutionDone 在 GameThread 上跑（广播 OnPhaseEnd 等）；
 *        单线程模式：直接 Executor::Run；同步广播 OnPhaseEnd；通知 PhaseManager.OnPhaseEnd。
 *   5) 暂停状态下跳过 PhaseProcessor.Run，但仍然广播 OnPhaseStart/End —— "phase 节奏不变"。
 *
 * 调用线程：UE Tick 系统决定，常见于 GameThread；并行模式下完成回调也在 GameThread。
 */
void FMassProcessingPhase::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (ShouldTick(TickType) == false)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_Mass_PhaseTick);
	SCOPE_CYCLE_COUNTER(STAT_Mass_Total);

	checkf(PhaseManager, TEXT("Manager is null which is not a supported case. Either this FMassProcessingPhase has not been initialized properly or it's been left dangling after the FMassProcessingPhase owner got destroyed."));

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("FMassProcessingPhase::ExecuteTick %s"), *UEnum::GetValueAsString(Phase)));

	// PhaseManager 钩子（先于用户 OnPhaseStart 委托）
	PhaseManager->OnPhaseStart(*this);
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/PhaseStartDelegate"));
		// 广播给用户 —— 可能在这里挂入额外逻辑（数据准备等）
		OnPhaseStart.Broadcast(DeltaTime);
	}

	check(PhaseProcessor);
	
	FMassEntityManager& EntityManager = PhaseManager->GetEntityManagerRef();

	bIsDuringMassProcessing = true;

	if (bRunInParallelMode && PhaseManager->IsPaused() == false)
	{
		// 并行模式：派发到 task graph
		bool bWorkRequested = false;
		if (PhaseProcessor->IsEmpty() == false)
		{
			// 注意：FMassProcessingContext 是 rvalue 喂给 TriggerParallelTasks —— 内部 ExecutionContext
			// 已经被搬走，本作用域结束时 Context 析构发现 ExecutionContextPtr 为空，不会重复 flush 命令
			// （真正的 flush 由 FMassExecutorDoneTask::DoTask 完成）
			FMassProcessingContext Context(EntityManager, DeltaTime);
			const FGraphEventRef PipelineCompletionEvent = UE::Mass::Executor::TriggerParallelTasks(*PhaseProcessor, MoveTemp(Context), [this, DeltaTime]()
				{
					// 完成回调（在 GameThread 上跑）：广播 OnPhaseEnd、通知 PhaseManager.OnPhaseEnd
					OnParallelExecutionDone(DeltaTime);
				}
				, CurrentThread);

			if (PipelineCompletionEvent.IsValid())
			{
				// 把"phase 完成事件"挂到 UE 的 tick completion event 上 —— UE 不会认为本 phase 已结束直到
				// 我们的并行工作做完。这是 phase 与 UE Tick 系统的关键同步点。
				MyCompletionGraphEvent->DontCompleteUntil(PipelineCompletionEvent);
				bWorkRequested = true;
			}
		}
		if (bWorkRequested == false)
		{
			// pipeline 是空的 —— 直接当作"立刻完成"，跑收尾路径
			OnParallelExecutionDone(DeltaTime);
		}
	}
	else
	{
		// 单线程模式（或暂停）
		if (PhaseManager->IsPaused() == false)
		{
			// note that it's important to create the processing context in this scope
			// so that it wraps up its destruction before we call OnPhaseEnd, which in turn will cause
			// the main EntityManager's command buffer to flush
			// 中文：刻意让 FMassProcessingContext 在此作用域内析构 —— 它会把命令 append 到 EntityManager 主缓冲；
			// 之后 OnPhaseEnd 里会触发主缓冲 flush —— 顺序很重要：先 append 后 flush。
			FMassProcessingContext Context(EntityManager, DeltaTime);
			UE::Mass::Executor::Run(*PhaseProcessor, Context);
		}

		{
			LLM_SCOPE_BYNAME(TEXT("Mass/PhaseEndDelegate"));
			// 广播给用户 OnPhaseEnd
			OnPhaseEnd.Broadcast(DeltaTime);
		}
		// PhaseManager 钩子（后于用户 OnPhaseEnd） —— 可能在这里 flush 主缓冲
		PhaseManager->OnPhaseEnd(*this);
		bIsDuringMassProcessing = false;
	}
}

/**
 * 并行执行收尾回调 —— 由 FMassExecutorDoneTask 在 GameThread 上调
 *   1) 标记处理结束；
 *   2) 广播用户 OnPhaseEnd；
 *   3) 通知 PhaseManager.OnPhaseEnd（其中可能 flush 主缓冲）。
 */
void FMassProcessingPhase::OnParallelExecutionDone(const float DeltaTime)
{
	bIsDuringMassProcessing = false;
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/PhaseEndDelegate"));
		OnPhaseEnd.Broadcast(DeltaTime);
	}
	check(PhaseManager);
	PhaseManager->OnPhaseEnd(*this);
}

// FTickFunction 接口：profiler / 调试用诊断字符串
FString FMassProcessingPhase::DiagnosticMessage()
{
	return (PhaseManager ? PhaseManager->GetName() : TEXT("NULL-MassProcessingPhaseManager")) + TEXT("[ProcessingPhaseTick]");
}

FName FMassProcessingPhase::DiagnosticContext(bool bDetailed)
{
	return TEXT("MassProcessingPhase");
}

// 由 PhaseManager.Initialize 调，绑定 manager / phase 编号 / TickGroup / PhaseProcessor
void FMassProcessingPhase::Initialize(FMassProcessingPhaseManager& InPhaseManager, const EMassProcessingPhase InPhase, const ETickingGroup InTickGroup, UMassCompositeProcessor& InPhaseProcessor)
{
	PhaseManager = &InPhaseManager;
	Phase = InPhase;
	TickGroup = InTickGroup;        // FTickFunction 成员：决定本 tick function 在哪个 UE TickGroup 跑
	PhaseProcessor = &InPhaseProcessor;
}

//----------------------------------------------------------------------//
// FPhaseProcessorConfigurator
//----------------------------------------------------------------------//
/**
 * Configure —— 依赖图构建的核心实现
 *
 * 步骤：
 *   1) 用 PhaseProcessor 现有的 child processors + LastResult.PrunedProcessors 组成 TmpPipeline
 *      （PrunedProcessors 是上轮被裁剪掉的 —— 这次仍要参与求解，因为 archetype 改了它们可能复活）；
 *   2) 用 InOutRemovedDynamicProcessors 把已 Unregister 的动态 processor 从 TmpPipeline 中清除；
 *   3) 把 DynamicProcessors 中匹配本 phase 的塞入 TmpPipeline（去重）；
 *   4) 把 PhaseConfig.ProcessorCDOs 中的 CDO 拷贝出 runtime 实例追加进去；
 *   5) 跑 FMassProcessorDependencySolver 求解依赖顺序（输出 SortedProcessors + InOutOptionalResult）；
 *   6) UpdateProcessorsCollection：让 PhaseProcessor 用新的有序 child 列表；
 *   7) 调试日志：列出本次被裁剪的 processor（无可用 archetype）；
 *   8) 若是并行模式（solver 没强制单线程），构建 flat processing graph（task graph 派发用的拓扑）；
 *   9) 若 bInitializeCreatedProcessors，调 PhaseProcessor.InitializeInternal 把新 processor 真正 Initialize。
 *
 * 调用约束：GameThread 上（OnPhaseStart 内）。
 */
void FMassPhaseProcessorConfigurationHelper::Configure(TArrayView<UMassProcessor* const> DynamicProcessors, TArray<TWeakObjectPtr<UMassProcessor>>& InOutRemovedDynamicProcessors
	, EProcessorExecutionFlags InWorldExecutionFlags, const TSharedRef<FMassEntityManager>& EntityManager
	, FMassProcessorDependencySolver::FResult& InOutOptionalResult)
{
	// 用 PhaseProcessor 当前的 child processors 初始化临时 pipeline（保留这次仍有效的 processor）
	FMassRuntimePipeline TmpPipeline(PhaseProcessor.GetChildProcessorsView(), InWorldExecutionFlags);
	{
		SCOPE_CYCLE_COUNTER(STAT_Mass_PhaseConfigurePipelineCreation);

		// 把上轮被裁剪掉的 processor 也并入 —— 这次它们可能"复活"
		TmpPipeline.AppendProcessors(InOutOptionalResult.PrunedProcessors);

		if (TmpPipeline.Num())
		{
			// some previously added dynamic processors were either in the active processor group,
			// or were among the pruned processors. At this point we have both groups in TmpPipeline now
			// so we need to check if any of these processors have been removed since las processing
			// graph recreation
			// 中文：之前注册过的某些动态 processor 现在被 Unregister 了 —— 它们可能还残留在
			// active 列表或 pruned 列表里。这里统一在 TmpPipeline 中清除。
			// 倒序遍历是为了 RemoveAtSwap 的安全。
			for (int32 Index = InOutRemovedDynamicProcessors.Num() - 1; Index >= 0; --Index)
			{
				if (const UMassProcessor* RemovedProcessor = InOutRemovedDynamicProcessors[Index].Get())
				{
					if (TmpPipeline.RemoveProcessor(*RemovedProcessor) == false)
					{
						// 没在 TmpPipeline 里 —— 留在列表中，下个 phase Configure 还会尝试
						continue;
					}
				}
				// 已经清掉（或 weak ref 已失效），从 RemovedDynamicProcessors 中摘除
				InOutRemovedDynamicProcessors.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			}
		}

		// 加入当前注册中的动态 processor —— 仅保留属于本 phase 的
		for (UMassProcessor* Processor : DynamicProcessors)
		{
			checkf(Processor != nullptr, TEXT("Dynamic processor provided to MASS is null."));
			if (Processor->GetProcessingPhase() == Phase)
			{
				TmpPipeline.AppendUniqueProcessor(*Processor);
			}
		}

		UObject* Owner = EntityManager->GetOwner();
		check(Owner);
		// @todo consider doing this only during initial config.
		// 中文：把 PhaseConfig 中的 CDO 列表拷贝成 runtime 实例追加（去重）。
		// @todo 注释提示：这里每次 Configure 都会做拷贝，理论上只在第一次需要 —— 是个潜在优化点。
		TmpPipeline.AppendUniqueRuntimeProcessorCopies(PhaseConfig.ProcessorCDOs, *Owner, EntityManager);
	}

	// 跑依赖求解
	TArray<FMassProcessorOrderInfo> SortedProcessors;
	FMassProcessorDependencySolver Solver(TmpPipeline.GetMutableProcessors(), bIsGameRuntime);

	Solver.ResolveDependencies(SortedProcessors, EntityManager, &InOutOptionalResult);

	// 把求解结果灌入 PhaseProcessor —— 这是本函数的"产出"
	PhaseProcessor.UpdateProcessorsCollection(SortedProcessors, InWorldExecutionFlags);

#if WITH_MASSENTITY_DEBUG
	// 调试：从 TmpPipeline 中扣除 SortedProcessors，剩下的就是被 prune 掉的（无可用 archetype）
	for (const FMassProcessorOrderInfo& ProcessorOrderInfo : SortedProcessors)
	{
		TmpPipeline.RemoveProcessor(*ProcessorOrderInfo.Processor);
	}
	
	if (TmpPipeline.Num())
	{
		UE_VLOG_UELOG(&PhaseProcessor, LogMass, Verbose, TEXT("Discarding processors due to not having anything to do (no relevant Archetypes):"));
		for (UMassProcessor* Processor : TmpPipeline.GetProcessors())
		{
			UE_VLOG_UELOG(&PhaseProcessor, LogMass, Verbose, TEXT("\t%s"), *Processor->GetProcessorName());
		}
	}
#endif // WITH_MASSENTITY_DEBUG

	// 若不是单线程求解（即并行模式），构建扁平的并行执行图（task graph 派发用）
	if (Solver.IsSolvingForSingleThread() == false)
	{
		PhaseProcessor.BuildFlatProcessingGraph(SortedProcessors);
	}

	// 完成新加入 processor 的 Initialize（第一次 Run 之前必须做的初始化）
	if (bInitializeCreatedProcessors)
	{
		PhaseProcessor.InitializeInternal(ProcessorOuter, EntityManager);
	}
}

//----------------------------------------------------------------------//
// FMassProcessingPhaseManager::FPhaseGraphBuildState
//----------------------------------------------------------------------//
// 重置缓存：清空 LastResult 和 bInitialized；保留 b*Rebuild 标志（它们表达"需要重建"）
// 注意：bNewArchetypes 和 bProcessorsNeedRebuild 不在此重置 —— 调用方按需重置
void FMassProcessingPhaseManager::FPhaseGraphBuildState::Reset()
{
	LastResult.Reset();
	bInitialized = false;
}

//----------------------------------------------------------------------//
// FMassProcessingPhaseManager
//----------------------------------------------------------------------//
// 构造：保存执行 flags；注册调试 hook（仅 WITH_MASSENTITY_DEBUG）
FMassProcessingPhaseManager::FMassProcessingPhaseManager(EProcessorExecutionFlags InProcessorExecutionFlags) 
	: ProcessorExecutionFlags(InProcessorExecutionFlags)
{
#if WITH_MASSENTITY_DEBUG
	// 注：两个 hook 都注册到 OnEntityManagerInitialized —— 这看起来像是 bug，
	// 后一行本应是 OnEntityManagerDeinitialized（见文末"疑似问题"小结）
	OnDebugEntityManagerInitializedHandle = FMassDebugger::OnEntityManagerInitialized.AddRaw(this, &FMassProcessingPhaseManager::OnDebugEntityManagerInitialized);
	OnDebugEntityManagerDeinitializedHandle = FMassDebugger::OnEntityManagerInitialized.AddRaw(this, &FMassProcessingPhaseManager::OnDebugEntityManagerDeinitialized);
#endif // WITH_MASSENTITY_DEBUG
}

/**
 * Initialize —— 用 settings 配置初始化所有 phase
 *
 * 动作：
 *   1) 自动确定 ProcessorExecutionFlags（结合 World 类型 + 已有 flags）；
 *   2) 自动确定 SupportedTickTypes；
 *   3) 为每个 phase 新建一个 UMassCompositeProcessor（PhaseProcessor）并绑定到 FMassProcessingPhase；
 *   4) 配置 PhaseProcessor 的 phase 标签 / 组名（用于调试显示）；
 *   5) 标记 bIsAllowedToTick = true（但 phase tick 还没有 RegisterTickFunction —— 由 Start 完成）。
 *
 * 注：此时 PhaseProcessor 的 child processors 还是空 —— 第一次 OnPhaseStart 时才会真正配置。
 */
void FMassProcessingPhaseManager::Initialize(UObject& InOwner, TConstArrayView<FMassProcessingPhaseConfig> InProcessingPhasesConfig, const FString& DependencyGraphFileName)
{
	UWorld* World = InOwner.GetWorld();

	Owner = &InOwner;
	ProcessingPhasesConfig = InProcessingPhasesConfig;

	ProcessorExecutionFlags = UE::Mass::Utils::DetermineProcessorExecutionFlags(World, ProcessorExecutionFlags);
	const uint8 SupportedTickTypes = UE::Mass::Utils::DetermineProcessorSupportedTickTypes(World);

	for (int PhaseAsInt = 0; PhaseAsInt < int(EMassProcessingPhase::MAX); ++PhaseAsInt)
	{		
		const EMassProcessingPhase Phase = EMassProcessingPhase(PhaseAsInt);
		FMassProcessingPhase& ProcessingPhase = ProcessingPhases[PhaseAsInt];

		// 为该 phase 创建 composite processor 实例 —— 命名规则 "ProcessingPhase_PrePhysics" 等
		UMassCompositeProcessor* PhaseProcessor = NewObject<UMassCompositeProcessor>(&InOwner, UMassCompositeProcessor::StaticClass()
			, *FString::Printf(TEXT("ProcessingPhase_%s"), *UEnum::GetDisplayValueAsText(Phase).ToString()));
	
		check(PhaseProcessor);
		ProcessingPhase.Initialize(*this, Phase, UE::Mass::Private::PhaseToTickingGroup[PhaseAsInt], *PhaseProcessor);
		ProcessingPhase.SupportedTickTypes = SupportedTickTypes;

		// 让 vlog 把 PhaseProcessor 的日志归到 owner 名下
		REDIRECT_OBJECT_TO_VLOG(PhaseProcessor, &InOwner);
		PhaseProcessor->SetProcessingPhase(Phase);
		PhaseProcessor->SetGroupName(FName(FString::Printf(TEXT("%s Group"), *UEnum::GetDisplayValueAsText(Phase).ToString())));

#if WITH_MASSENTITY_DEBUG
		// 调试：把当前 phase 的描述写到 vlog
		FStringOutputDevice Ar;
		PhaseProcessor->DebugOutputDescription(Ar);
		UE_VLOG(&InOwner, LogMass, Log, TEXT("Setting new group processor for phase %s:\n%s"), *UEnum::GetValueAsString(Phase), *Ar);
#endif // WITH_MASSENTITY_DEBUG
	}

	bIsAllowedToTick = true;
}

/**
 * Deinitialize —— 清理。必须在 owner BeginDestroy 之前调（FGCObject 限制）。
 *   1) 清空所有 PhaseProcessor 引用 —— 让 GC 不再保留它们；
 *   2) 清空动态 processor 列表；
 *   3) 重置图缓存；
 *   4) 把 pending 队列里所有元素 dequeue（不处理 —— 仅丢弃，因为已经 deinit）。
 */
void FMassProcessingPhaseManager::Deinitialize()
{
	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.PhaseProcessor = nullptr;
	}

	DynamicProcessors.Reset();

	for (FPhaseGraphBuildState& GraphBuildState : ProcessingGraphBuildStates)
	{
		GraphBuildState.Reset();
	}

	// manually deque all the queues, since there's no guarantee that this
	// FMassProcessingPhaseManager instance is getting destroyed right after this call
	// 中文：手动把每个 phase 的 pending 队列 drain 干净，避免悬挂的 TStrongObjectPtr 阻止 GC。
	// 队列本身的析构会做这件事，但 PhaseManager 不一定立刻析构 —— 现在就 drain 比较安全。
	FDynamicProcessorOperation DummyElement;
	for (TMpscQueue<FDynamicProcessorOperation>& Queue : PendingDynamicProcessors)
	{	
		while (Queue.Dequeue(DummyElement))
		{
			// empty on purpose
		}
	}
}

/**
 * TriggerPhase —— 手动触发某个 phase 的执行
 *
 * 直接调 ExecuteTick；不走 UE Tick 系统。常用于测试 / 自定义 tick 调度。
 * 返回值就是传入的 MyCompletionGraphEvent —— 调用方据此等待 phase 完成。
 */
const FGraphEventRef& FMassProcessingPhaseManager::TriggerPhase(const EMassProcessingPhase Phase, const float DeltaTime
	, const FGraphEventRef& MyCompletionGraphEvent, ENamedThreads::Type CurrentThread)
{
	check(Phase != EMassProcessingPhase::MAX);

	if (bIsAllowedToTick)
	{
		ProcessingPhases[(int)Phase].ExecuteTick(DeltaTime, LEVELTICK_All, CurrentThread, MyCompletionGraphEvent);
	}

	return MyCompletionGraphEvent;
}

// Start(World) —— 从 World 自动取 EntityManager 并启动
void FMassProcessingPhaseManager::Start(UWorld& World)
{
	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);

	if (ensure(EntitySubsystem))
	{
		Start(EntitySubsystem->GetMutableEntityManager().AsShared());
	}
	else
	{
		UE_VLOG_UELOG(Owner.Get(), LogMass, Error, TEXT("Called %s while missing the EntitySubsystem"), ANSI_TO_TCHAR(__FUNCTION__));
	}
}

/**
 * Start(EntityManager) —— 主启动入口
 *
 * 动作：
 *   1) 保存 EntityManager（同时充当"运行中"标志）；
 *   2) 注册调试数据 provider（"Phase-executed processors"、"Pruned processors"），让 FMassDebugger 能查看；
 *   3) 注册 OnNewArchetype 监听 —— 让 archetype 变化能触发图重建；
 *   4) 若 EntityManager 关联 World 则调 EnableTickFunctions 真正启用 phase tick；
 *   5) 标记 bIsAllowedToTick = true。
 */
void FMassProcessingPhaseManager::Start(const TSharedRef<FMassEntityManager>& InEntityManager)
{
	EntityManager = InEntityManager;

#if WITH_MASSENTITY_DEBUG
	// 调试 provider #1：暴露每个 phase 的 PhaseProcessor + 其 child processors 给 FMassDebugger
	FMassDebugger::RegisterProcessorDataProvider(TEXT("Phase-executed processors"), InEntityManager, [WeakThis = AsWeak()](TArray<const UMassProcessor*>& OutProcessors)
	{
		if (TSharedPtr<FMassProcessingPhaseManager> SharedThis = WeakThis.Pin())
		{
			for (const FMassProcessingPhase& Phase : SharedThis->ProcessingPhases)
			{
				OutProcessors.Add(Phase.DebugGetPhaseProcessor());
				OutProcessors.Append(Phase.DebugGetPhaseProcessor()->GetChildProcessorsView());
			}
		}
	});

	// 调试 provider #2：暴露每个 phase 被 prune 掉的 processor（"看得见但跑不到"的那些）
	FMassDebugger::RegisterProcessorDataProvider(TEXT("Pruned processors"), InEntityManager, [WeakThis = AsWeak()](TArray<const UMassProcessor*>& OutProcessors)
	{
		if (TSharedPtr<FMassProcessingPhaseManager> SharedThis = WeakThis.Pin())
		{
			TConstArrayView<FPhaseGraphBuildState> BuildStates = SharedThis->DebugGetProcessingGraphBuildStates();
			for (const FPhaseGraphBuildState& State : BuildStates)
			{
				OutProcessors.Append(ObjectPtrDecay(State.LastResult.PrunedProcessors));
			}
		}
	});
#endif // WITH_MASSENTITY_DEBUG

	// 关键监听：archetype 增量变化 → 标记所有 phase 图脏 → 下次 OnPhaseStart 重建
	OnNewArchetypeHandle = EntityManager->GetOnNewArchetypeEvent().AddRaw(this, &FMassProcessingPhaseManager::OnNewArchetype);

	if (UWorld* World = EntityManager->GetWorld())
	{
		EnableTickFunctions(*World);
	}

	bIsAllowedToTick = true;
}

/**
 * AddReferencedObjects —— FGCObject 接口
 *
 * 必须把所有 UObject 引用注册给 GC，否则会被回收：
 *   1) 每个 phase 的 PhaseProcessor；
 *   2) DynamicProcessors（先清掉 null 元素，确保不会出错）；
 *   3) 每个 phase 的 PrunedProcessors（同样先清 null）。
 *
 * 用 AddStableReferenceArray 而不是逐个 AddReferencedObject —— 后者更高效（一批引用一次性提交）。
 */
void FMassProcessingPhaseManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		if (Phase.PhaseProcessor)
		{
			Collector.AddReferencedObject(Phase.PhaseProcessor);
		}
	}

	auto NullProcessorRemover = [](const TObjectPtr<UMassProcessor>& Processor)
	{
		return !Processor;
	};

	// 不应该有 null —— 这里 check 检查零值，把它当作 invariant 而不是 sanitization
	check(DynamicProcessors.RemoveAllSwap(NullProcessorRemover) == 0);
	Collector.AddStableReferenceArray(&DynamicProcessors);

	// we also need to store our pruned processors
	// 中文：被裁剪掉的 processor 仍然要保 GC —— 它们可能因 archetype 变化而"复活"重新加入图
	for (FPhaseGraphBuildState& GraphBuildState : ProcessingGraphBuildStates)
	{
		check(GraphBuildState.LastResult.PrunedProcessors.RemoveAllSwap(NullProcessorRemover) == 0);
		Collector.AddStableReferenceArray(&GraphBuildState.LastResult.PrunedProcessors);
	}
}

/**
 * EnableTickFunctions —— 把所有 phase 注册到 World 并启用 tick
 *
 * 子类可以 override 改变行为（例如换不同的 level、调整优先级等）。
 *
 * 默认实现：
 *   * 若 cvar 开关 + 是 PrePhysics phase，提升整条 prerequisite 链为高优先级（让 task graph 尽早动起来）；
 *   * RegisterTickFunction(World.PersistentLevel) —— 注册到 World 持久关卡；
 *   * SetTickFunctionEnable(true) —— 启用 tick；
 *   * 调试：把每个 phase 的 PhaseProcessor 描述输出到 vlog（仅 game world，避免编辑器下污染时间戳）。
 */
void FMassProcessingPhaseManager::EnableTickFunctions(const UWorld& World)
{
	check(EntityManager);

	const bool bIsGameWorld = World.IsGameWorld();

	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		if (UE::Mass::Tweakables::bMakePrePhysicsTickFunctionHighPriority && (Phase.Phase == EMassProcessingPhase::PrePhysics))
		{
			constexpr bool bHighPriority = true;
			Phase.SetPriorityIncludingPrerequisites(bHighPriority);
		}

		Phase.RegisterTickFunction(World.PersistentLevel);
		Phase.SetTickFunctionEnable(true);
#if WITH_MASSENTITY_DEBUG
		if (Phase.PhaseProcessor && bIsGameWorld)
		{
			// not logging this in the editor mode since it messes up the game-recorded vislog display (with its progressively larger timestamp)
			FStringOutputDevice Ar;
			Phase.PhaseProcessor->DebugOutputDescription(Ar);
			UE_VLOG_UELOG(Owner.Get(), LogMass, Log, TEXT("Enabling phase %s tick:\n%s")
				, *UEnum::GetValueAsString(Phase.Phase), *Ar);
		}
#endif // WITH_MASSENTITY_DEBUG
	}

	if (bIsGameWorld)
	{
		// not logging this in the editor mode since it messes up the game-recorded vislog display (with its progressively larger timestamp)
		UE_VLOG_UELOG(Owner.Get(), LogMass, Log, TEXT("MassProcessingPhaseManager %s.%s has been started")
			, *GetNameSafe(Owner.Get()), *GetName());
	}
}

/**
 * Stop —— 停掉 phase tick
 *   1) bIsAllowedToTick = false；
 *   2) 移除 OnNewArchetype 监听并 reset EntityManager；
 *   3) 关闭每个 phase 的 tick；
 *   4) 调试日志（仅 game world）。
 */
void FMassProcessingPhaseManager::Stop()
{
	bIsAllowedToTick = false;

	if (EntityManager)
	{
		EntityManager->GetOnNewArchetypeEvent().Remove(OnNewArchetypeHandle);
		EntityManager.Reset();
	}
	
	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.SetTickFunctionEnable(false);
	}

	if (UObject* LocalOwner = Owner.Get())
	{
		UWorld* World = LocalOwner->GetWorld();
		if (World && World->IsGameWorld())
		{
			// not logging this in editor mode since it messes up the game-recorded vislog display (with its progressively larger timestamp) 
			UE_VLOG_UELOG(LocalOwner, LogMass, Log, TEXT("MassProcessingPhaseManager %s.%s has been stopped")
				, *GetNameSafe(LocalOwner), *GetName());
		}
	}
}

// 申请暂停 —— 必须 GameThread。仅设置 bIsPauseTogglePending，真正生效在 OnPhaseEnd(FrameEnd)
void FMassProcessingPhaseManager::Pause()
{
	check(IsInGameThread());

	if (bIsPaused == false)
	{
		bIsPauseTogglePending = true;

		UE_VLOG_UELOG(Owner.Get(), LogMass, Log, TEXT("Scheduling Pause for next FrameEnd phase"));
	}
}

// 申请恢复 —— 必须 GameThread。仅设置 bIsPauseTogglePending，真正生效在 OnPhaseStart(PrePhysics)
void FMassProcessingPhaseManager::Resume()
{
	check(IsInGameThread());

	if (bIsPaused == true)
	{
		bIsPauseTogglePending = true;

		UE_VLOG_UELOG(Owner.Get(), LogMass, Log, TEXT("Scheduling Resume for next PrePhysics phase"));
	}
}

/**
 * OnPhaseStart —— phase 起始钩子（由 FMassProcessingPhase::ExecuteTick 调，在用户委托之前）
 *
 * 干 4 件事：
 *   1) 暂停切换（仅在 PrePhysics 起点恢复）；
 *   2) 同步 cvar mass.FullyParallel → phase 的并行模式（只在 phase 间切换以避免破坏一致性）；
 *   3) 处理动态 processor 队列里的 add/remove；
 *   4) 若图脏（有新 archetype 或 processor 增删，或 dependency solver 检测到结果过期），重建依赖图。
 *
 * Trace 标记：UE_TRACE_MASS_PHASE_BEGIN —— Insights 视图里能看到 phase 时间线。
 */
void FMassProcessingPhaseManager::OnPhaseStart(FMassProcessingPhase& Phase)
{
	ensure(CurrentPhase == EMassProcessingPhase::MAX);
	CurrentPhase = Phase.Phase;

	const int32 PhaseAsInt = int32(Phase.Phase);

	// The VERY FIRST thing we do in the first phase is to change the Pause state if needed.
	// This way any code that depends on knowing the pause state (if any) gets consistent results.
	// 中文：在 PrePhysics 起点（PhaseAsInt == 0）做"恢复"动作，让本帧从一开始就处于 unpaused 状态
	if (bIsPauseTogglePending && bIsPaused == true && PhaseAsInt == 0)
	{
		bIsPaused = false;
		bIsPauseTogglePending = false;

		UE_VLOG_UELOG(Owner.Get(), LogMass, Log, TEXT("Phase Processing is now Resumed"));
	}

	// switch between parallel and single-thread versions only after a given batch of processing has been wrapped up	
	// 中文：并行/单线程模式切换 —— 必须在 phase 起始处做，避免一个 phase 中"半并行半同步"
	if (Phase.IsConfiguredForParallelMode() != UE::Mass::Tweakables::bFullyParallel)
	{
		if (UE::Mass::Tweakables::bFullyParallel)
		{
			Phase.ConfigureForParallelMode();
		}
		else
		{
			Phase.ConfigureForSingleThreadMode();
		}
	}

	// 处理 pending 动态 processor 操作（add/remove）—— 这一步可能让 bProcessorsNeedRebuild 置 true
	if (PendingDynamicProcessors[PhaseAsInt].IsEmpty() == false)
	{
		HandlePendingDynamicProcessorOperations(PhaseAsInt);
	}

	UE_TRACE_MASS_PHASE_BEGIN(PhaseAsInt)

	// 检查图是否需要重建：
	//   * Owner 仍有效；
	//   * Phase 编号合法；
	//   * 标记为脏：bNewArchetypes 或 bProcessorsNeedRebuild
	//   * 配置数组索引合法
	if (Owner.IsValid()
		&& ensure(Phase.Phase != EMassProcessingPhase::MAX)
		&& (ProcessingGraphBuildStates[PhaseAsInt].bNewArchetypes || ProcessingGraphBuildStates[PhaseAsInt].bProcessorsNeedRebuild)
		// if not a valid index then we're not able to recalculate dependencies 
		&& ensure(ProcessingPhasesConfig.IsValidIndex(PhaseAsInt)))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass Rebuild Phase Graph");

		FPhaseGraphBuildState& GraphBuildState = ProcessingGraphBuildStates[PhaseAsInt];
		// 三层判断：
		//   * 从未初始化过 → 必须构建；
		//   * 有动态 processor 增删 → 必须构建；
		//   * dependency solver 检测到 LastResult 已过期（archetype 集合变了，prune 结果可能不同）→ 构建。
		if (GraphBuildState.bInitialized == false 
			|| ProcessingGraphBuildStates[PhaseAsInt].bProcessorsNeedRebuild
			|| FMassProcessorDependencySolver::IsResultUpToDate(GraphBuildState.LastResult, EntityManager) == false)
		{
			UMassCompositeProcessor* PhaseProcessor = ProcessingPhases[PhaseAsInt].PhaseProcessor;
			check(PhaseProcessor);

			// 重置上轮结果 —— 但 PrunedProcessors 还会用：在 Configure 内部当输入参与求解
			GraphBuildState.LastResult.Reset();

			// 真正的重建动作 —— 见 FMassPhaseProcessorConfigurationHelper::Configure
			FMassPhaseProcessorConfigurationHelper Configurator(*PhaseProcessor, ProcessingPhasesConfig[PhaseAsInt], *Owner.Get(), Phase.Phase);
			Configurator.Configure(DynamicProcessors, RemovedDynamicProcessors, ProcessorExecutionFlags, EntityManager.ToSharedRef(), GraphBuildState.LastResult);

			GraphBuildState.bInitialized = true;

#if WITH_MASSENTITY_DEBUG
			UObject* OwnerPtr = Owner.Get();
			// print it all out to vislog
			UE_VLOG_UELOG(OwnerPtr, LogMass, Verbose, TEXT("Phases initialization done. Current composition:"));

			FStringOutputDevice OutDescription;
			PhaseProcessor->DebugOutputDescription(OutDescription);
			UE_VLOG_UELOG(OwnerPtr, LogMass, Verbose, TEXT("--- %s"), *OutDescription);
			OutDescription.Reset();
#endif // WITH_MASSENTITY_DEBUG
		}

		// 重建完成 —— 清掉 dirty 标志
		ProcessingGraphBuildStates[PhaseAsInt].bProcessorsNeedRebuild = false;
		ProcessingGraphBuildStates[PhaseAsInt].bNewArchetypes = false;
	}
}

/**
 * OnPhaseEnd —— phase 结束钩子（由 FMassProcessingPhase::ExecuteTick / OnParallelExecutionDone 调，
 *               在用户委托之后）
 *
 * 干 3 件事：
 *   1) 标记 CurrentPhase 回到 MAX（空闲）；
 *   2) 若是 FrameEnd 末尾 + 有暂停请求挂起，正式生效"暂停"；
 *   3) Flush EntityManager 主缓冲（若有 pending 命令）—— 把本 phase 累积的命令真正应用。
 */
void FMassProcessingPhaseManager::OnPhaseEnd(FMassProcessingPhase& Phase)
{
	ensure(CurrentPhase == Phase.Phase);
	UE_TRACE_MASS_PHASE_END(static_cast<int32>(CurrentPhase))
	CurrentPhase = EMassProcessingPhase::MAX;

	// The VERY LAST thing we do in FrameEnd is change the Pause state if needed.
	// This way any code that depends on knowing the pause state (if any) gets consistent results.
	// 中文：在 FrameEnd 末尾做"暂停"动作 —— 让下一帧从一开始就处于 paused 状态
	if (bIsPauseTogglePending && bIsPaused == false
		&& Phase.Phase == EMassProcessingPhase::FrameEnd)
	{
		bIsPaused = true;
		bIsPauseTogglePending = false;

#if WITH_MASSENTITY_DEBUG
		UE_VLOG_UELOG(Owner.Get(), LogMass, Log, TEXT("Phase Processing is now Paused"));
#endif // WITH_MASSENTITY_DEBUG
	}

	// Phase 期间累积到 EntityManager 主缓冲的命令在这里 flush
	// （单线程模式下 FProcessingContext 析构时 append 命令到主缓冲；并行模式下 FMassExecutorDoneTask
	//  已经 flush 过自己的 buffer，但用户在 OnPhaseEnd 委托内可能又入队新命令 —— 这里统一兜底 flush）
	if (GetEntityManagerRef().Defer().HasPendingCommands())
	{
		GetEntityManagerRef().FlushCommands();
	}
}

// 调试名 —— "<Owner名>_MassProcessingPhaseManager"
FString FMassProcessingPhaseManager::GetName() const
{
	return GetNameSafe(Owner.Get()) + TEXT("_MassProcessingPhaseManager");
}

/**
 * RegisterDynamicProcessor —— 公开入口（线程安全）
 *
 * 走 MPSC 队列异步处理：把 (Processor, Add) 入队，下次该 phase 的 OnPhaseStart 时 dequeue 并刷新图。
 * 这样允许任意线程（甚至 worker）去注册 processor，无需自己同步。
 */
void FMassProcessingPhaseManager::RegisterDynamicProcessor(UMassProcessor& Processor)
{
	if (ensureMsgf(Processor.GetProcessingPhase() != EMassProcessingPhase::MAX
		, TEXT("%hs, Misconfigured processor %s, marked as ProcessingPhase == MAX"), __FUNCTION__, *Processor.GetName()))
	{
		PendingDynamicProcessors[int32(Processor.GetProcessingPhase())].Enqueue(&Processor, EDynamicProcessorOperationType::Add);
	}
}

// 真正添加 —— 在 GameThread 上、phase 起始 dequeue 时调
// 若 processor 还没 Initialize，则现在 Initialize；加入 DynamicProcessors 数组；标记为 dynamic。
void FMassProcessingPhaseManager::RegisterDynamicProcessorInternal(TNotNull<UMassProcessor*> Processor)
{
	if (Processor->IsInitialized() == false)
	{
		check(EntityManager->GetOwner());
		Processor->CallInitialize(EntityManager->GetOwner(), EntityManager.ToSharedRef());
	}
	DynamicProcessors.Add(Processor);
	Processor->MarkAsDynamic();
}

// UnregisterDynamicProcessor —— 公开入口（线程安全），同样走 MPSC 队列
void FMassProcessingPhaseManager::UnregisterDynamicProcessor(UMassProcessor& Processor)
{
	if (ensureMsgf(Processor.GetProcessingPhase() != EMassProcessingPhase::MAX
		, TEXT("%hs, Misconfigured processor %s, marked as ProcessingPhase == MAX"), __FUNCTION__, *Processor.GetName()))
	{
		PendingDynamicProcessors[int32(Processor.GetProcessingPhase())].Enqueue(&Processor, EDynamicProcessorOperationType::Remove);
	}
}

/**
 * 真正移除 —— 在 GameThread 上、phase 起始 dequeue 时调
 *   * 从 DynamicProcessors 数组移除；
 *   * 标记图脏（bProcessorsNeedRebuild）；
 *   * 把它放入 RemovedDynamicProcessors —— 因为它可能正驻留在 PhaseProcessor 内，
 *     下次 Configure 才能彻底清掉。
 */
void FMassProcessingPhaseManager::UnregisterDynamicProcessorInternal(TNotNull<UMassProcessor*> Processor)
{
	int32 Index = INDEX_NONE;
	if (DynamicProcessors.Find(Processor, Index))
	{
		DynamicProcessors.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		ProcessingGraphBuildStates[int32(Processor->GetProcessingPhase())].bProcessorsNeedRebuild = true;

		// it's possible that the given dynamic processor is a part of processing graph at the moment
		// we need to store the information about its removal and use it when rebuilding the graph next time around.
		// 中文：保留弱引用 —— 下次 Configure 时从 PhaseProcessor 内部彻底清掉
		RemovedDynamicProcessors.Add(Processor);
	}
	else
	{
		checkf(false, TEXT("Unable to remove Processor '%s', as it was never added or already removed."), *Processor->GetName());
	}
}

/**
 * HandlePendingDynamicProcessorOperations —— 在 phase 起始处消费 pending 队列
 *
 * 把当前 phase 的所有挂起操作 dequeue 并处理；
 * 若至少做了一次 add/remove，标记 bProcessorsNeedRebuild = true，触发后续图重建。
 *
 * 调用线程：GameThread（OnPhaseStart 内）。
 */
void FMassProcessingPhaseManager::HandlePendingDynamicProcessorOperations(const int32 PhaseIndex)
{
	bool bWorkDone = false;
	FDynamicProcessorOperation Operation;
	while (PendingDynamicProcessors[PhaseIndex].Dequeue(Operation))
	{
		if (Operation.Get<1>() == EDynamicProcessorOperationType::Add)
		{
			RegisterDynamicProcessorInternal(Operation.Get<0>().Get());
		}
		else
		{
			UnregisterDynamicProcessorInternal(Operation.Get<0>().Get());
		}
		bWorkDone = true;
	}

	if (bWorkDone)
	{
		ProcessingGraphBuildStates[PhaseIndex].bProcessorsNeedRebuild = bWorkDone;
	}
}

/**
 * OnNewArchetype —— EntityManager.OnNewArchetypeEvent 的回调
 *
 * 行为：把 6 个 phase 的 bNewArchetypes 标志一律置 true。
 * 后果：下一次任意 phase OnPhaseStart 时会触发依赖图重建（即使 LastResult 仍上次有效，
 * 因为新 archetype 可能让之前被 prune 的 processor "复活"）。
 *
 * 注意：本回调可能在任意线程触发（取决于 archetype 创建的入口），但写 bool 标志是 trivially safe。
 */
void FMassProcessingPhaseManager::OnNewArchetype(const FMassArchetypeHandle& NewArchetype)
{
	for (FPhaseGraphBuildState& GraphBuildState : ProcessingGraphBuildStates)
	{
		GraphBuildState.bNewArchetypes = true;
	}
}

#if WITH_MASSENTITY_DEBUG
// 当前实现为空 —— 预留扩展点（FMassDebugger 通知 EntityManager 初始化/反初始化时回调到此）
void FMassProcessingPhaseManager::OnDebugEntityManagerInitialized(const FMassEntityManager& InEntityManager)
{
	
}

void FMassProcessingPhaseManager::OnDebugEntityManagerDeinitialized(const FMassEntityManager& InEntityManager)
{
	
}
#endif // WITH_MASSENTITY_DEBUG

//-----------------------------------------------------------------------------
// DEPRECATED —— 已废弃 API，仅作向后兼容
//-----------------------------------------------------------------------------
// 5.6 起废弃：旧版 Configure（SharedPtr + Result*）。转发到新版。
UE_DEPRECATED(5.6, "This flavor of Configure is deprecated. Please use the one using a TSharedRef<FMassEntityManager> parameter instead")
void FMassPhaseProcessorConfigurationHelper::Configure(TArrayView<UMassProcessor* const> DynamicProcessors, EProcessorExecutionFlags InWorldExecutionFlags
	, const TSharedPtr<FMassEntityManager>& EntityManager
	, FMassProcessorDependencySolver::FResult* OutOptionalResult)
{
	if (ensureMsgf(EntityManager, TEXT("Configuring processors without a valid EntityManager is no longer supported"))
		&& OutOptionalResult)
	{
		// 注意：用 static 数组占位 RemovedDynamicProcessors —— 因为旧 API 没暴露这个参数
		// 这意味着旧路径无法正确处理"已 Unregister 的动态 processor 仍残留在 PhaseProcessor 内"的情形
		static TArray<TWeakObjectPtr<UMassProcessor>> DummyRemovedDynamicProcessors;
		Configure(DynamicProcessors, DummyRemovedDynamicProcessors, InWorldExecutionFlags, EntityManager.ToSharedRef(), *OutOptionalResult);
	}
}

// 5.6 起废弃：旧版 Start（SharedPtr）
void FMassProcessingPhaseManager::Start(const TSharedPtr<FMassEntityManager>& InEntityManager)
{
	if (InEntityManager)
	{
		Start(InEntityManager.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
