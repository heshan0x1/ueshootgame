// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【中文文件总览】MassProcessor.cpp
// -----------------------------------------------------------------------------
// 实现 UMassProcessor / UMassCompositeProcessor 以及多线程任务派发用的 TaskGraph 任务类。
// 主要分为三块：
//   1) FMassProcessorTask / FMassProcessorsTask_GameThread  ——  把"调一次 processor::CallExecute"包成 TaskGraph 任务；
//   2) UMassProcessor 实现                                  ——  init / configure / execute / dispatch 全流程；
//   3) UMassCompositeProcessor 实现                         ——  group 容器，依赖求解 + 扁平图构建 + 任务派发。
// =============================================================================
#include "MassProcessor.h"
#include "MassEntitySettings.h"
#include "MassProcessorDependencySolver.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "MassQueryExecutor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassProcessor)

DECLARE_CYCLE_STAT(TEXT("MassProcessor Group Completed"), Mass_GroupCompletedTask, STATGROUP_TaskGraphTasks);
DECLARE_CYCLE_STAT(TEXT("Mass Processor Task"), STAT_Mass_DoTask, STATGROUP_Mass);

#if WITH_MASSENTITY_DEBUG
namespace UE::Mass::Debug
{
	// 中文：调试 cvar —— 是否每帧打印 composite processor 的任务派发图。
	bool bLogProcessingGraphEveryFrame = false;
	// 中文：调试 cvar —— 是否在新生成 graph 时打印一次。
	bool bLogNewProcessingGraph = true;

	namespace
	{
		FAutoConsoleVariableRef CVars[] = {
			{ TEXT("mass.LogProcessingGraph"), bLogProcessingGraphEveryFrame
				, TEXT("When enabled every composite processor, every frame, will log task graph tasks created while dispatching processors to other threads, along with their dependencies.")
				, ECVF_Cheat }
			, { TEXT("mass.LogNewProcessingGraph"), bLogNewProcessingGraph
				, TEXT("When enabled every time a new processing graph is created the composite processor hosting it will log it during first execution.")
				, ECVF_Cheat }
		};
	}

}

// change to 1 to enable more detailed processing tasks logging
// 中文：把这里改成 1 可启用任务派发的详细 log（每个 processor task 的开始/结束）。
#if 0
#define PROCESSOR_TASK_LOG(Fmt, ...) UE_VLOG_UELOG(this, LogMass, Verbose, Fmt, ##__VA_ARGS__)
#else
#define PROCESSOR_TASK_LOG(...) 
#endif // 0

#else 
#define PROCESSOR_TASK_LOG(...) 
#endif // WITH_MASSENTITY_DEBUG

// =============================================================================
// 【中文】FMassProcessorTask —— "执行一个 processor"的 TaskGraph 任务。
// -----------------------------------------------------------------------------
// 派发线程：默认 ENamedThreads::AnyHiPriThreadHiPriTask（任意工作线程，高优先级）。
// 一次 DoTask = 进入 processing scope（影响 deferred command 行为）+ 调 processor->CallExecute。
//
// bManageCommandBuffer = true 时，本任务在执行前替换 ExecutionContext 的 deferred command buffer
// 为本任务私有的 buffer，避免多线程往同一个 buffer 推命令。执行完再 MoveAppend 回主 buffer。
// 这是 Mass 多线程命令推送安全的关键。
// =============================================================================
class FMassProcessorTask
{
public:
	FMassProcessorTask(const TSharedPtr<FMassEntityManager>& InEntityManager, const FMassExecutionContext& InExecutionContext, UMassProcessor& InProc, bool bInManageCommandBuffer = true)
		: EntityManager(InEntityManager)
		, ExecutionContext(InExecutionContext)
		, Processor(&InProc)
		, bManageCommandBuffer(bInManageCommandBuffer)
	{}

	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMassProcessorTask, STATGROUP_TaskGraphTasks);
	}

	// 中文：TrackSubsequents —— 让其他任务可以把本任务作为 prerequisite 等。
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	// 中文：派发到任意高优先级工作线程。GT 子类会 override 此返回 GameThread。
	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyHiPriThreadHiPriTask;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		checkf(Processor, TEXT("Expecting a valid processor to execute"));

		PROCESSOR_TASK_LOG(TEXT("+--+ Task %s started on %u"), *Processor->GetProcessorName(), FPlatformTLS::GetCurrentThreadId());
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(MassProcessorTask);
		SCOPE_CYCLE_COUNTER(STAT_Mass_DoTask);
		SCOPE_CYCLE_COUNTER(STAT_Mass_Total);

		check(EntityManager);
		FMassEntityManager& EntityManagerRef = *EntityManager.Get();
		// 中文：进入 processing scope。这个 RAII 对 EntityManager.IsProcessing 计数 +1，scope 出后 -1。
		// 期间所有 EntityManager.* 修改都需要走 deferred command。
		FMassEntityManager::FScopedProcessing ProcessingScope = EntityManagerRef.NewProcessingScope();

		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass Processor Task");
		
		if (bManageCommandBuffer)
		{
			// 中文：保存原始 buffer 共享指针，临时换成本任务私有的。
			TSharedPtr<FMassCommandBuffer> MainSharedPtr = ExecutionContext.GetSharedDeferredCommandBuffer();
			ExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FMassCommandBuffer()));
			Processor->CallExecute(EntityManagerRef, ExecutionContext);
			// 中文：私有 buffer 的命令 move 回主 buffer，等 phase 结束 flush。
			MainSharedPtr->MoveAppend(ExecutionContext.Defer());
		}
		else
		{
			Processor->CallExecute(EntityManagerRef, ExecutionContext);
		}
		PROCESSOR_TASK_LOG(TEXT("+--+ Task %s finished"), *Processor->GetProcessorName());
	}

private:
	TSharedPtr<FMassEntityManager> EntityManager;
	// 中文：注意这里 ExecutionContext 是按值持有 —— 每个任务有自己独立的 context，避免线程间共享。
	FMassExecutionContext ExecutionContext;
	UMassProcessor* Processor = nullptr;
	/** 
	 * indicates whether this task is responsible for creation of a dedicated command buffer and transferring over the 
	 * commands after processor's execution;
	 */
	bool bManageCommandBuffer = true;
};

// =============================================================================
// 【中文】FMassProcessorsTask_GameThread —— GT 强制版 task。
// 仅 override GetDesiredThread() 返回 GameThread + HighTaskPriority。
// 用于 bRequiresGameThreadExecution 的 processor。
// =============================================================================
class FMassProcessorsTask_GameThread : public FMassProcessorTask
{
public:
	FMassProcessorsTask_GameThread(const TSharedPtr<FMassEntityManager>& InEntityManager, const FMassExecutionContext& InExecutionContext, UMassProcessor& InProc)
		: FMassProcessorTask(InEntityManager, InExecutionContext, InProc)
	{}

	static ENamedThreads::Type GetDesiredThread()
	{
		// Use a high priority task so processor chains that touch the game thread will take priority over normal ticks
		return ENamedThreads::SetTaskPriority(ENamedThreads::GameThread, ENamedThreads::HighTaskPriority);
	}
};

//----------------------------------------------------------------------//
// UMassProcessor 
//----------------------------------------------------------------------//
// 中文：FObjectInitializer 重载 —— 转调默认构造。GENERATED_BODY 要求两个构造都存在。
UMassProcessor::UMassProcessor(const FObjectInitializer& ObjectInitializer)
	: UMassProcessor()
{
}

// 中文：默认构造 —— 默认 ExecutionFlags = Server | Standalone（即 Client/Editor 不跑）。
// 子类可以在自己构造里改。
UMassProcessor::UMassProcessor()
	: ExecutionFlags((int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone))
{
}

// =============================================================================
// 【中文】CallInitialize —— processor 初始化的统一入口。
// 调用方：FMassProcessingPhaseManager 在创建 processor 实例后调用。
//
// 关键流程：
//   1) 防御：CDO / abstract 类不参与初始化（CDO 用于反射元数据）；
//   2) 准备 DebugDescription（含 NetMode）；
//   3) 初始化每个 OwnedQuery（绑 EntityManager）；
//   4) 调子类 ConfigureQueries（在此处子类填 query requirement 并 RegisterQuery）；
//   5) 检查 OwnedQueries / ProcessorRequirements 是否要求 GT，自动 OR 进 bRequiresGameThreadExecution；
//   6) 调子类 InitializeInternal（自定义初始化，如订阅事件）；
//   7) 标记 bInitialized = true。
//
// 注意：步骤 4 的 OwnedQuery 是子类在 ConfigureQueries 中通过 RegisterQuery 添加的；
// 但本函数中第 3 步是先 Initialize 已经在 OwnedQueries 中的（这看起来矛盾）—— 
// 实际上 RegisterQuery 是子类在 ConfigureQueries 调的，所以步骤 3 与步骤 4 顺序保证：
// "已注册的（多次初始化场景）先 init，然后 configure 阶段再注册新的 query"。
// 对首次 init，OwnedQueries 在步骤 3 时是空的，全部在步骤 4 中添加。
// =============================================================================
void UMassProcessor::CallInitialize(const TNotNull<UObject*> Owner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	if (ensure(HasAnyFlags(RF_ClassDefaultObject) == false 
		&& GetClass()->HasAnyClassFlags(CLASS_Abstract) == false))
	{

#if WITH_MASSENTITY_DEBUG
		// 中文：DebugDescription 用于 trace / VisualLogger，格式: "ProcessorName (NetMode)"。
		// 当开启 TraceProcessors debug feature 时，预先 Reserve 字符串容量做小优化。
		if (EntityManager.Get().DebugHasAllDebugFeatures(FMassEntityManager::EDebugFeatures::TraceProcessors))
		{
			DebugDescription = *GetProcessorName();
			FString NetMode = EntityManager.Get().GetWorld() ? ToString(EntityManager.Get().GetWorld()->GetNetMode()) : TEXT("None");
			//                       DebugDescription       " ("  NetMode        ")"
			DebugDescription.Reserve(DebugDescription.Len() + 2 + NetMode.Len() + 1);
			DebugDescription.Append(TEXT(" ("));
			DebugDescription.Append(*NetMode);
			DebugDescription.Append(TEXT(")"));
		}
		else
		{
			DebugDescription = FString::Printf(TEXT("%s (%s)"), *GetProcessorName(), EntityManager.Get().GetWorld() ? *ToString(EntityManager.Get().GetWorld()->GetNetMode()) : TEXT("No World"));
		}
#endif

		for (FMassEntityQuery* Query : OwnedQueries)
		{
			// we should never get nulls here since OwnedQueries is private and the only way to
			// add queries to it is to go through RegisterQuery, which in turn ensures the
			// input query is a member variable of the processor.
			checkfSlow(Query, TEXT("We never expect nulls in OwnedQueries - those pointers are supposed to point at member variable."));
			Query->Initialize(EntityManager);
		}

		ConfigureQueries(EntityManager);

		// 中文：自动派生 GT 需求 —— 任一 query 或 ProcessorRequirements 需要 GT，整体就需要 GT。
		// 这是"better safe than sorry"原则，避免子类标错 bRequiresGameThreadExecution 导致并发崩溃。
		bool bNeedsGameThread = ProcessorRequirements.DoesRequireGameThreadExecution();
		for (const FMassEntityQuery* QueryPtr : OwnedQueries)
		{
			CA_ASSUME(QueryPtr);
			bNeedsGameThread = (bNeedsGameThread || QueryPtr->DoesRequireGameThreadExecution());
		}

		// 中文：若用户标记与自动派生不一致，verbose log 提醒（不是错误，可能是临时禁用）。
		UE_CLOG(bRequiresGameThreadExecution != bNeedsGameThread, LogMass, Verbose
			, TEXT("%s is marked bRequiresGameThreadExecution = %s, while the registered queries' or processor requirements indicate the opposite")
			, *GetProcessorName(), bRequiresGameThreadExecution ? TEXT("TRUE") : TEXT("FALSE"));

		// better safe than sorry - if queries or processor requirements indicate the game thread execution is required, then we mark the whole processor as such
		bRequiresGameThreadExecution = bRequiresGameThreadExecution || bNeedsGameThread;

		InitializeInternal(*Owner, EntityManager);

		bInitialized = true;
	}
}

// 中文：基类空实现 —— 子类按需 override 添加初始化逻辑。
void UMassProcessor::InitializeInternal(UObject&, const TSharedRef<FMassEntityManager>&)
{
	// empty in base class
}

// =============================================================================
// 中文：ConfigureQueries 默认实现。两条路：
//   - 若 AutoExecuteQuery 有效：调它的 ConfigureQuery 让其填充 query requirement；
//   - 否则若 OwnedQueries 非空但子类没 override：警告（可能是子类忘了 override）。
// =============================================================================
void UMassProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	if (AutoExecuteQuery.IsValid())
	{
		AutoExecuteQuery->ConfigureQuery(ProcessorRequirements);
	}
	else
	{
		UE_CVLOG_UELOG(OwnedQueries.Num(), this, LogMass, Warning
			, TEXT("%s has entity queries registered. Make sure to override ConfigureQueries to configure the queries, and do not call the Super implementation")
			, *GetProcessorName());
	}
}

// =============================================================================
// 中文：SetShouldAutoRegisterWithGlobalList —— 设置 bAutoRegisterWithProcessingPhases。
// 必须在 CDO 上调（断言保护）。编辑器下还会持久化到 default config 文件。
// 用途：插件代码在启动时把某些 processor 类标为 / 取消自动注册。
// =============================================================================
void UMassProcessor::SetShouldAutoRegisterWithGlobalList(const bool bAutoRegister)
{	
	if (ensureMsgf(HasAnyFlags(RF_ClassDefaultObject), TEXT("Setting bAutoRegisterWithProcessingPhases for non-CDOs has no effect")))
	{
		bAutoRegisterWithProcessingPhases = bAutoRegister;
#if WITH_EDITOR
		if (UClass* Class = GetClass())
		{
			if (FProperty* AutoRegisterProperty = Class->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMassProcessor, bAutoRegisterWithProcessingPhases)))
			{
				UpdateSinglePropertyInConfigFile(AutoRegisterProperty, *GetDefaultConfigFilename());
			}
		}
#endif // WITH_EDITOR
	}
}

// 中文：取出所有 OwnedQuery 当前匹配的 archetype（CacheArchetypes 保证缓存最新）。AddUnique 去重。
void UMassProcessor::GetArchetypesMatchingOwnedQueries(const FMassEntityManager& EntityManager, TArray<FMassArchetypeHandle>& OutArchetype)
{
	UE_CLOG(OwnedQueries.Num() == 0, LogMass, Warning, TEXT("%s has no registered queries while being asked for matching archetypes"), *GetName());

	for (FMassEntityQuery* QueryPtr : OwnedQueries)
	{
		CA_ASSUME(QueryPtr);
		QueryPtr->CacheArchetypes();

		for (const FMassArchetypeHandle& ArchetypeHandle : QueryPtr->GetArchetypes())
		{
			OutArchetype.AddUnique(ArchetypeHandle);
		}
	}
}

// 中文：用于 query-based pruning —— 只要任一 query 匹配 ≥1 archetype 即返回 true。
bool UMassProcessor::DoesAnyArchetypeMatchOwnedQueries(const FMassEntityManager& EntityManager)
{
	for (FMassEntityQuery* QueryPtr : OwnedQueries)
	{
		CA_ASSUME(QueryPtr);
		QueryPtr->CacheArchetypes();

		if (QueryPtr->GetArchetypes().Num() > 0)
		{
			return true;
		}
	}
	return false;
}

// 中文：UObject 标准钩子 —— CDO 加载/构造时调。这里设置 CPU profiler 的 StatId。
void UMassProcessor::PostInitProperties()
{
	Super::PostInitProperties();

#if CPUPROFILERTRACE_ENABLED
	StatId = GetProcessorName();
#endif
}

// =============================================================================
// 中文：CallExecute —— processor 执行的"包装入口"。每帧 phase tick 时被调用。
// 流程：
//   1) IsActive 检查（Inactive 直接早返）。LIKELY 优化提示。
//   2) 设置 trace scope / debug 信息；
//   3) CacheSubsystemRequirements —— 校验 ProcessorRequirements 中声明的 subsystem 是否都可访问；
//      不满足时 *跳过 Execute* 但仍 log（避免静默错误）；
//   4) 调虚函数 Execute；
//   5) 若是 OneShot，自动 MakeInactive 做完转禁。
// =============================================================================
void UMassProcessor::CallExecute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (LIKELY(ensureMsgf(IsActive(), TEXT("Trying to CallExecute for an inactive processor %s"), *GetProcessorName())))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*StatId);
		LLM_SCOPE_BYNAME(TEXT("Mass/ExecuteProcessor"));
		// Not using a more specific scope by default (i.e. LLM_SCOPE_BYNAME(*StatId)) since LLM is more strict regarding the provided string (no spaces or '_')

#if WITH_MASSENTITY_DEBUG
		Context.DebugSetExecutionDesc(DebugDescription);
		Context.DebugSetProcessor(this);
#endif
		// CacheSubsystemRequirements will return true only if all requirements declared with ProcessorRequirements are met
		// meaning if it fails there's no point in calling Execute.
		// Note that we're not testing individual queries in OwnedQueries - processors can function just fine with some 
		// of their queries not having anything to do.
		if (Context.CacheSubsystemRequirements(ProcessorRequirements))
		{
			Execute(EntityManager, Context);
		}
		else
		{
			UE_VLOG_UELOG(this, LogMass, VeryVerbose, TEXT("%s Skipping Execute due to subsystem requirements not being met"), *GetProcessorName());
		}

		if (ActivationState == EActivationState::OneShot)
		{
			MakeInactive();
		}
	}
}

// =============================================================================
// 中文：基类 Execute 默认实现 —— 若有 AutoExecuteQuery 则调它，否则 *硬错*（要求子类必须 override）。
// 这是声明模式的入口点。
// =============================================================================
void UMassProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (AutoExecuteQuery.IsValid())
	{
		AutoExecuteQuery->CallExecute(Context);
	}
	else
	{
		static constexpr TCHAR MessageFormat[] = TEXT("UMassProcessor::Execute should never be called without an AutoExecuteQuery set. Override the function or populate AutoExecuteQuery. Processor name: %s");
		checkf(false, MessageFormat, *GetProcessorName());
	}
}

// 中文：仅运行时（bRuntimeMode==true）且配置为 Prune 时返回 true。Project Settings 视图下不剪。
bool UMassProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return bRuntimeMode && QueryBasedPruning == EMassQueryBasedPruning::Prune;
}

EMassProcessingPhase UMassProcessor::GetProcessingPhase() const
{
	return ProcessingPhase;
}

void UMassProcessor::SetProcessingPhase(EMassProcessingPhase Phase)
{
	ProcessingPhase = Phase;
}

// 中文：把所有 OwnedQuery 的 read/write fragment 需求合并到 OutRequirements。
// 子类如果有"非 query 路径"的需求（比如直接调 EntityManager.SetFragmentValue），要 override 此函数额外 push。
void UMassProcessor::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	for (FMassEntityQuery* Query : OwnedQueries)
	{
		CA_ASSUME(Query);
		Query->ExportRequirements(OutRequirements);
	}
}

// =============================================================================
// 中文：RegisterQuery —— 把 query 加入 OwnedQueries 之前，做"成员变量地址范围检查"。
//
// 算法：
//   ThisStart  = (uintptr_t)this
//   ThisEnd    = ThisStart + sizeof(*this)（用反射拿 GetClass()->GetStructureSize 取真实大小）
//   QueryStart = (uintptr_t)&Query
//   QueryEnd   = QueryStart + sizeof(FMassEntityQuery)
//   要求 [QueryStart, QueryEnd] ⊆ [ThisStart, ThisEnd] 才认为 Query 是本对象的成员变量。
//
// 这是裸指针 OwnedQueries 安全的根本保证 —— Query 与 Processor 同生命周期。
// =============================================================================
void UMassProcessor::RegisterQuery(FMassEntityQuery& Query)
{
	const uintptr_t ThisStart = (uintptr_t)this;
	const uintptr_t ThisEnd = ThisStart + GetClass()->GetStructureSize();
	const uintptr_t QueryStart = (uintptr_t)&Query;
	const uintptr_t QueryEnd = QueryStart + sizeof(FMassEntityQuery);

	if (QueryStart >= ThisStart && QueryEnd <= ThisEnd)
	{
		OwnedQueries.AddUnique(&Query);
	}
	else
	{
		// 中文：检查失败 —— Query 不是本 processor 的成员变量。直接 checkf 而不 ensure，因为这是严重 bug。
		static constexpr TCHAR MessageFormat[] = TEXT("Registering entity query for %s while the query is not given processor's member variable. Skipping.");
		checkf(false, MessageFormat, *GetProcessorName());
		UE_LOG(LogMass, Error, MessageFormat, *GetProcessorName());
	}
}

// =============================================================================
// 中文：DispatchProcessorTasks —— 把本 processor 包装成一个 TaskGraph 任务派发出去。
// 选择 GT vs 非 GT task 由 bRequiresGameThreadExecution 决定。
// 返回的 FGraphEventRef 用于下游依赖。
// =============================================================================
FGraphEventRef UMassProcessor::DispatchProcessorTasks(const TSharedPtr<FMassEntityManager>& EntityManager, FMassExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites)
{
	FGraphEventRef ReturnVal;
	if (LIKELY(ensureMsgf(IsActive(), TEXT("Trying to dispatch processor task for inactive processor %s"), *GetProcessorName())))
	{
		if (bRequiresGameThreadExecution)
		{
			ReturnVal = TGraphTask<FMassProcessorsTask_GameThread>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntityManager, ExecutionContext, *this);
		}
		else
		{
			ReturnVal = TGraphTask<FMassProcessorTask>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntityManager, ExecutionContext, *this);
		}
	}
	return ReturnVal;
}

FString UMassProcessor::GetProcessorName() const
{
	return GetName();
}

void UMassProcessor::DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const
{
#if WITH_MASSENTITY_DEBUG
	Ar.Logf(TEXT("%*s%s"), Indent, TEXT(""), *GetProcessorName());
#endif // WITH_MASSENTITY_DEBUG
}

//----------------------------------------------------------------------//
//  UMassCompositeProcessor
//----------------------------------------------------------------------//
// 中文：默认构造 —— composite processor 不自动注册到全局 phase manager 列表
// （它们是"组容器"而非干活的 processor）。子类可改回 true。
UMassCompositeProcessor::UMassCompositeProcessor()
	: GroupName(TEXT("None"))
{
	// not auto-registering composite processors since the idea of the global processors list is to indicate all 
	// the processors doing the work while composite processors are just containers. Having said that subclasses 
	// can change this behavior if need be.
	bAutoRegisterWithProcessingPhases = false;
}

// 中文：直接覆盖 ChildPipeline 列表（两个重载分别对应 TArrayView 与 move TArray<TObjectPtr>）。
// 注意这不会做依赖求解，仅是赋值；如果要重新建图请用 SetProcessors。
void UMassCompositeProcessor::SetChildProcessors(TArrayView<UMassProcessor*> InProcessors)
{
	ChildPipeline.SetProcessors(InProcessors);
}

void UMassCompositeProcessor::SetChildProcessors(TArray<TObjectPtr<UMassProcessor>>&& InProcessors)
{
	ChildPipeline.SetProcessors(MoveTemp(InProcessors));
}

// 中文：composite 自身没有 query，每个 child 在自己的 ConfigureQueries 中独立配置。所以这里空实现。
void UMassCompositeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// nothing to do here since ConfigureQueries will get independently called for all the processors during their creation
}

// =============================================================================
// 中文：DispatchProcessorTasks —— 多线程调度核心。
// 输入：FlatProcessingGraph（已被 BuildFlatProcessingGraph 填充好的扁平拓扑序）。
// 输出：一个汇总 CompletionEvent —— 等所有任务完成。
//
// 关键：
//   - Events[i] 持有第 i 个节点的 FGraphEventRef；
//   - 派发节点 i 时，Prerequisites = ∪{ Events[d] : d ∈ Dependencies[i] }；
//   - inactive processor 不派发任务，但通过 AdditionalEvents[i] 把它的 prerequisites 传递给下游
//     —— 这是"穿透依赖"的实现：让被禁用的节点不破坏 graph 隐式依赖。
//   - 最后空 lambda 任务作为汇总 sink，等所有子任务结束。
// =============================================================================
FGraphEventRef UMassCompositeProcessor::DispatchProcessorTasks(const TSharedPtr<FMassEntityManager>& EntityManager, FMassExecutionContext& ExecutionContext, const FGraphEventArray& InPrerequisites)
{
	FGraphEventArray Events;
	Events.AddDefaulted(FlatProcessingGraph.Num());

	FGraphEventArray Prerequisites;
	// we'll fill this one with dependencies of disabled processors. We initialize it lazily
	// 中文：每个 inactive 节点会把"自己的 prerequisites"暂存在 AdditionalEvents[NodeIndex] 里。
	// 下游节点若依赖该 inactive 节点，会把 AdditionalEvents[DependencyIndex] 加到自己的 prereq 中。
	TArray<FGraphEventArray> AdditionalEvents;
		
	for (int32 NodeIndex = 0; NodeIndex < FlatProcessingGraph.Num(); ++NodeIndex)
	{
		FDependencyNode& ProcessingNode = FlatProcessingGraph[NodeIndex];

		if (ensureMsgf(ProcessingNode.Processor, TEXT("We don't expect any group nodes at this point. If we get any there's a bug in dependencies solving.")))
		{
			Prerequisites.Reset(ProcessingNode.Dependencies.Num());
			for (const int32 DependencyIndex : ProcessingNode.Dependencies)
			{
				checkSlow(DependencyIndex < NodeIndex);
				Prerequisites.Add(Events[DependencyIndex]);
			}
			// this means there are some inactive processors so we need to consider additional dependencies
			if (AdditionalEvents.Num())
			{
				for (const int32 DependencyIndex : ProcessingNode.Dependencies)
				{
					Prerequisites.Append(AdditionalEvents[DependencyIndex]);
				}
			}

			if (ProcessingNode.Processor->IsActive())
			{
				Events[NodeIndex] = ProcessingNode.Processor->DispatchProcessorTasks(EntityManager, ExecutionContext, Prerequisites);
			}
			else
			{
				if (AdditionalEvents.Num() == 0)
				{
					// lazy initialization
					AdditionalEvents.AddDefaulted(FlatProcessingGraph.Num());
				}
				// if the processor is not going to run at all we store its Prerequisites so that
				// processors waiting for this given processor to finish will keep their place
				// in the overall processing graph
				// NOTE: this is safer than just ignoring the dependencies since even though this
				// processor is not running, the subsequent processors might unknowingly rely on
				// implicit dependencies that the current processor was ensuring. 
				AdditionalEvents[NodeIndex].Append(MoveTemp(Prerequisites));
			}
		}
	}

#if WITH_MASSENTITY_DEBUG
	if (UE::Mass::Debug::bLogProcessingGraphEveryFrame || bDebugLogNewProcessingGraph)
	{
		FScopedCategoryAndVerbosityOverride LogOverride(TEXT("LogMass"), ELogVerbosity::Log);

		for (int i = 0; i < FlatProcessingGraph.Num(); ++i)
		{
			FDependencyNode& ProcessingNode = FlatProcessingGraph[i];
			FString DependenciesDesc;
			for (const int32 DependencyIndex : ProcessingNode.Dependencies)
			{
				DependenciesDesc += FString::Printf(TEXT("%s, "), *FlatProcessingGraph[DependencyIndex].Name.ToString());
			}

			check(ProcessingNode.Processor);
			if (Events[i].IsValid())
			{
				PROCESSOR_TASK_LOG(TEXT("Task %u %s%s%s"), Events[i]->GetTraceId(), *ProcessingNode.Processor->GetProcessorName()
					, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
			}
			else
			{
				ensureMsgf(ProcessingNode.Processor->IsActive() == false, TEXT("This path is expected to trigger only for inactive processors"))
				PROCESSOR_TASK_LOG(TEXT("Task [INACTIVE] %s%s%s"), *ProcessingNode.Processor->GetProcessorName()
					, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
			}
		}

		bDebugLogNewProcessingGraph = false;
	}
#endif // WITH_MASSENTITY_DEBUG

	// 中文：汇总 sink —— 一个空 lambda 任务，把所有 Events 作为 prerequisites。
	// caller 等这个 CompletionEvent 即可知所有 child 都跑完了。
	FGraphEventRef CompletionEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([this](){}
		, GET_STATID(Mass_GroupCompletedTask), &Events, ENamedThreads::AnyHiPriThreadHiPriTask);

	return CompletionEvent;
}

// =============================================================================
// 中文：单线程模式下的 Execute —— 直接顺序遍历 ChildPipeline 调每个 active processor。
// 顺序由 SetProcessors 时构造的 ChildPipeline 顺序决定（已被 dependency solver 排序）。
// =============================================================================
void UMassCompositeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	for (UMassProcessor* Proc : ChildPipeline.GetMutableProcessors())
	{
		if (LIKELY(ensure(Proc) && Proc->IsActive()))
		{
			Proc->CallExecute(EntityManager, Context);
		}
	}
}

// 中文：override —— 先初始化 ChildPipeline 中的所有 child processor，再调 Super 的初始化。
void UMassCompositeProcessor::InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	ChildPipeline.Initialize(Owner, EntityManager);
	Super::InitializeInternal(Owner, EntityManager);
}

// =============================================================================
// 中文：SetProcessors —— 入口流程：依赖求解 + 灌入 ChildPipeline + 多线程图构建。
// 步骤：
//   1) FMassProcessorDependencySolver(InProcessors) 用 ExecutionOrder 与 ExportRequirements 信息排序；
//   2) UpdateProcessorsCollection 把排序结果灌入 ChildPipeline（尽量复用现有实例）；
//   3) 若不是单线程模式，BuildFlatProcessingGraph 生成 FlatProcessingGraph 给多线程派发用。
// =============================================================================
void UMassCompositeProcessor::SetProcessors(TArrayView<UMassProcessor*> InProcessorInstances, const TSharedPtr<FMassEntityManager>& EntityManager)
{
	// figure out dependencies
	FMassProcessorDependencySolver Solver(InProcessorInstances);
	TArray<FMassProcessorOrderInfo> SortedProcessors;
	Solver.ResolveDependencies(SortedProcessors, EntityManager);

	UpdateProcessorsCollection(SortedProcessors);

	if (Solver.IsSolvingForSingleThread() == false)
	{
		BuildFlatProcessingGraph(SortedProcessors);
	}
}

// =============================================================================
// 中文：BuildFlatProcessingGraph —— 把 sorted processors 转成 FlatProcessingGraph。
// 算法（线性扫一遍）：
//   1) 对每个 sorted entry，把它的 Name 映射到当前 FlatProcessingGraph.Num() 作为新 node 的 index；
//   2) 创建 FDependencyNode{Name, Processor}；
//   3) 把 entry.Dependencies (FName 列表) 通过 NameToDependencyIndex 转成 int32 索引数组。
// 这要求 sorted 序列是拓扑序（每个节点的 dependency 都在它之前出现）—— DependencySolver 保证。
//
// 失败模式：若 dep solver 漏漏返回 group 节点（Element.Processor 为空），这里 checkSlow 会报。
// =============================================================================
void UMassCompositeProcessor::BuildFlatProcessingGraph(TConstArrayView<FMassProcessorOrderInfo> SortedProcessors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BuildFlatProcessingGraph);
#if !MASS_DO_PARALLEL
	// 中文：单线程构建提示 —— 此函数构建的图在单线程模式下不会被使用，仅作为 debug 用。
	UE_LOG(LogMass, Warning
		, TEXT("MassCompositeProcessor::BuildFlatProcessingGraph is not expected to run in a single-threaded Mass setup. The flat graph will not be used at runtime."));
#endif // MASS_DO_PARALLEL

	FlatProcessingGraph.Reset();

	// this part is creating an ordered, flat list of processors that can be executed in sequence
	// with subsequent task only depending on the elements prior on the list
	// 中文：NameToDependencyIndex 是构建期临时索引表 —— "name → 在 FlatProcessingGraph 中的下标"。
	TMap<FName, int32> NameToDependencyIndex;
	NameToDependencyIndex.Reserve(SortedProcessors.Num());
	TArray<int32> SuperGroupDependency;
	for (const FMassProcessorOrderInfo& Element : SortedProcessors)
	{
		NameToDependencyIndex.Add(Element.Name, FlatProcessingGraph.Num());

		// we don't expect to get any "group" nodes here. If it happens it indicates a bug in dependency solving
		checkSlow(Element.Processor);
		FDependencyNode& Node = FlatProcessingGraph.Add_GetRef({ Element.Name, Element.Processor });
		Node.Dependencies.Reserve(Element.Dependencies.Num());
		for (FName DependencyName : Element.Dependencies)
		{
			checkSlow(DependencyName.IsNone() == false);
			// 中文：FindChecked —— 若依赖名不在表中，立即崩。这保护了拓扑序的不变量。
			Node.Dependencies.Add(NameToDependencyIndex.FindChecked(DependencyName));
		}
#if WITH_MASSENTITY_DEBUG
		Node.SequenceIndex = Element.SequenceIndex;
#endif // WITH_MASSENTITY_DEBUG
	}

#if WITH_MASSENTITY_DEBUG
	// 中文：调试 dump —— 缩进体现 group 嵌套层级（SequenceIndex * 2 个空格）。
	FScopedCategoryAndVerbosityOverride LogOverride(TEXT("LogMass"), ELogVerbosity::Log);
	UE_LOG(LogMass, Log, TEXT("%s flat processing graph:"), *GroupName.ToString());

	int32 Index = 0;
	for (const FDependencyNode& ProcessingNode : FlatProcessingGraph)
	{
		FString DependenciesDesc;
		for (const int32 DependencyIndex : ProcessingNode.Dependencies)
		{
			DependenciesDesc += FString::Printf(TEXT("%d, "), DependencyIndex);
		}
		if (ProcessingNode.Processor)
		{
			UE_LOG(LogMass, Log, TEXT("[%2d]%*s%s%s%s"), Index, ProcessingNode.SequenceIndex * 2, TEXT(""), *ProcessingNode.Processor->GetProcessorName()
				, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
		}
		++Index;
	}

	bDebugLogNewProcessingGraph = UE::Mass::Debug::bLogNewProcessingGraph;
#endif // WITH_MASSENTITY_DEBUG
}

// =============================================================================
// 中文：UpdateProcessorsCollection —— 把 sorted 后的 processor 集合灌入 ChildPipeline。
//
// 关键设计：尽量复用现有实例（保持状态）。因为 Mass 推荐 stateless processor，但实际工程
// 中常出现 signaling processor 之类有内部计数/历史的 processor。
//
// 复用策略：仅 bAllowMultipleInstances == false 的类型才查找复用。否则每个实例独立。
// 同时按 NetMode (WorldExecutionFlags) 过滤 —— ShouldExecute 不通过的不进 ChildPipeline。
//
// 注意 InOutOrderedProcessors 是 inout：复用时会把 ProcessorInfo.Processor 改写成已存在那个实例，
// caller 拿到的 sorted 列表也会反映复用情况。
// =============================================================================
void UMassCompositeProcessor::UpdateProcessorsCollection(TArrayView<FMassProcessorOrderInfo> InOutOrderedProcessors, EProcessorExecutionFlags InWorldExecutionFlags)
{
	TArray<TObjectPtr<UMassProcessor>> ExistingProcessors(ChildPipeline.GetMutableProcessors());
	ChildPipeline.Reset();

	const UWorld* World = GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = UE::Mass::Utils::DetermineProcessorExecutionFlags(World, InWorldExecutionFlags);
	const FMassProcessingPhaseConfig& PhaseConfig = GET_MASS_CONFIG_VALUE(GetProcessingPhaseConfig(ProcessingPhase));

	for (FMassProcessorOrderInfo& ProcessorInfo : InOutOrderedProcessors)
	{
		if (ensureMsgf(ProcessorInfo.NodeType == FMassProcessorOrderInfo::EDependencyNodeType::Processor, TEXT("Encountered unexpected FMassProcessorOrderInfo::EDependencyNodeType while populating %s"), *GetGroupName().ToString()))
		{
			checkSlow(ProcessorInfo.Processor);
			if (ProcessorInfo.Processor->ShouldExecute(WorldExecutionFlags))
			{
				// we want to reuse existing processors to maintain state. It's recommended to keep processors state-less
				// but we already have processors that do have some state, like signaling processors.
				// the following search only makes sense for "single instance" processors
				if (ProcessorInfo.Processor->ShouldAllowMultipleInstances() == false)
				{
					TObjectPtr<UMassProcessor>* FoundProcessor = ExistingProcessors.FindByPredicate([ProcessorClass = ProcessorInfo.Processor->GetClass()](TObjectPtr<UMassProcessor>& Element)
						{
							return Element && (Element->GetClass() == ProcessorClass);
						});

					if (FoundProcessor)
					{
						// overriding the stored value since the InOutOrderedProcessors can get used after the call and it 
						// needs to reflect the actual work performed
						ProcessorInfo.Processor = FoundProcessor->Get();
					}
				}

				CA_ASSUME(ProcessorInfo.Processor);
				ChildPipeline.AppendProcessor(*ProcessorInfo.Processor);
			}
		}
	}
}

// 中文：composite 自身在 log/profiler 中以 group name 标识，而不是 class name。
FString UMassCompositeProcessor::GetProcessorName() const
{
	return GroupName.ToString();
}

void UMassCompositeProcessor::DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const
{
#if WITH_MASSENTITY_DEBUG
	if (ChildPipeline.Num() == 0)
	{
		Ar.Logf(TEXT("%*sGroup %s: []"), Indent, TEXT(""), *GroupName.ToString());
	}
	else
	{
		Ar.Logf(TEXT("%*sGroup %s:"), Indent, TEXT(""), *GroupName.ToString());
		for (UMassProcessor* Proc : ChildPipeline.GetProcessors())
		{
			check(Proc);
			Ar.Logf(TEXT("\n"));
			Proc->DebugOutputDescription(Ar, Indent + 3);
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

// 中文：override —— 把 phase 改动级联到所有 child（保证 group 内 processor 的 phase 一致）。
void UMassCompositeProcessor::SetProcessingPhase(EMassProcessingPhase Phase)
{
	Super::SetProcessingPhase(Phase);
	for (UMassProcessor* Proc : ChildPipeline.GetMutableProcessors())
	{
		Proc->SetProcessingPhase(Phase);
	}
}

void UMassCompositeProcessor::SetGroupName(FName NewName)
{
	GroupName = NewName;
#if CPUPROFILERTRACE_ENABLED
	StatId = GroupName.ToString();
#endif
}

// =============================================================================
// 中文：AddGroupedProcessor —— 把 SubProcessor 添加到指定 group（支持点号嵌套）。
// 算法：
//   - RequestedGroupName 为 None 或等于本 group ⇒ 直接 append 到 ChildPipeline。
//   - 否则 FindOrAddGroupProcessor 找/建顶层 group，递归到剩余路径。
// 例：在 root composite 上 AddGroupedProcessor("AI.Loco", SubProc)：
//   - 找/建 root.ChildPipeline 中的 "AI" composite；
//   - 在 "AI" composite 上递归 AddGroupedProcessor("Loco", SubProc)；
//   - "Loco" composite 不存在 ⇒ 建 ⇒ 直接 append SubProc。
// =============================================================================
void UMassCompositeProcessor::AddGroupedProcessor(FName RequestedGroupName, UMassProcessor& Processor)
{
	if (RequestedGroupName.IsNone() || RequestedGroupName == GroupName)
	{
		ChildPipeline.AppendProcessor(Processor);
	}
	else
	{
		FString RemainingGroupName;
		UMassCompositeProcessor* GroupProcessor = FindOrAddGroupProcessor(RequestedGroupName, &RemainingGroupName);
		check(GroupProcessor);
		GroupProcessor->AddGroupedProcessor(FName(*RemainingGroupName), Processor);
	}
}

// =============================================================================
// 中文：FindOrAddGroupProcessor —— "AI.Loco.Steering" → 返回 ChildPipeline 中的 "AI" composite，
// 并把剩余 "Loco.Steering" 通过 OutRemainingGroupName 输出（caller 递归用）。
// 若顶层 group 不存在，NewObject<UMassCompositeProcessor>(GetOuter()) 创建后挂入 ChildPipeline。
// =============================================================================
UMassCompositeProcessor* UMassCompositeProcessor::FindOrAddGroupProcessor(FName RequestedGroupName, FString* OutRemainingGroupName)
{
	UMassCompositeProcessor* GroupProcessor = nullptr;
	const FString NameAsString = RequestedGroupName.ToString();
	FString TopGroupName;
	if (NameAsString.Split(TEXT("."), &TopGroupName, OutRemainingGroupName))
	{
		RequestedGroupName = FName(*TopGroupName);
	}
	GroupProcessor = ChildPipeline.FindTopLevelGroupByName(RequestedGroupName);

	if (GroupProcessor == nullptr)
	{
		check(GetOuter());
		GroupProcessor = NewObject<UMassCompositeProcessor>(GetOuter());
		GroupProcessor->SetGroupName(RequestedGroupName);
		ChildPipeline.AppendProcessor(*GroupProcessor);
	}

	return GroupProcessor;
}

//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------

// 中文：5.6 弃用入口 —— 仅做"老式签名 → 新签名"的转发。
// 通过 Owner.GetWorld() 反查 EntityManager。新代码应直接调 CallInitialize。
void UMassProcessor::Initialize(UObject& Owner)
{
	FMassEntityManager* EntityManager = UE::Mass::Utils::GetEntityManager(Owner.GetWorld());
	if (ensureMsgf(EntityManager, TEXT("Unable to determine the current MassEntityManager")))
	{
		InitializeInternal(Owner, EntityManager->AsShared());
	}
}

//void UMassCompositeProcessor::Initialize(UObject& Owner)
//{
//	FMassEntityManager* EntityManager = UE::Mass::Utils::GetEntityManager(Owner.GetWorld());
//	if (ensureMsgf(EntityManager, TEXT("Unable to determine the current MassEntityManager")))
//	{
//		InitializeInternal(Owner, EntityManager->AsShared());
//	}
//}

// 中文：deprecated TArray<UMassProcessor*>&& 重载 —— 转调新 TArrayView 版本。
void UMassCompositeProcessor::SetChildProcessors(TArray<UMassProcessor*>&& InProcessors)
{
	SetChildProcessors(MakeArrayView(InProcessors.GetData(), InProcessors.Num()));
}