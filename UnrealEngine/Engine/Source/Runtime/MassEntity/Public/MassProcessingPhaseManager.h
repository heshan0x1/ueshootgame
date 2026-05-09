// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassProcessingPhaseManager.h —— Mass 与 UE Tick 体系的对接点
// -----------------------------------------------------------------------------
// 角色：运行时调度协调员。把 Mass 的 6 个处理阶段（PrePhysics、StartPhysics、
// DuringPhysics、EndPhysics、PostPhysics、FrameEnd）注册成 UE 的 FTickFunction，
// 让 UE 引擎按 Tick Group 调度时，自动触发 Mass 的 processor 执行。
//
// 关键三件套：
//   FMassProcessingPhaseConfig —— 每个 phase 的"配置"（哪些 processor 该跑在哪个 phase）
//                                  来自 Project Settings (UMassEntitySettings)
//   FMassProcessingPhase       —— 每个 phase 的"运行时" —— 它继承 FTickFunction，
//                                  Tick 时调 Executor::Run/TriggerParallelTasks 来跑 PhaseProcessor
//   FMassProcessingPhaseManager—— 6 个 Phase 的总管：负责生命周期、动态 processor
//                                  注册、依赖图增量构建、archetype 失效通知、暂停/恢复
//
// 数据流：
//   Project Settings (CDO)  ─▶  PhaseConfig.ProcessorCDOs
//                                       │
//                                       ▼
//                  FMassPhaseProcessorConfigurationHelper::Configure
//                                       │  (内部使用 FMassProcessorDependencySolver 求依赖)
//                                       ▼
//             UMassCompositeProcessor (PhaseProcessor) —— 持有有序 child processors
//                                       │
//                                       ▼
//                       UE Tick → FMassProcessingPhase::ExecuteTick
//                                       │
//                                       ▼
//                  Executor::Run (单线程) 或 Executor::TriggerParallelTasks (并行)
//
// 增量构建：
//   * archetype 变化（OnNewArchetype）会标记所有 phase 的 GraphBuildState.bNewArchetypes = true；
//   * 动态 processor 增删（注册队列）会标记 bProcessorsNeedRebuild = true；
//   * OnPhaseStart 时若任意一个标记为脏，则重建该 phase 的 processor 拓扑。
//
// 暂停/恢复：bIsPaused 是"协作式"开关 —— Phase 仍然 Tick，但跳过实际 processor 调用，
// 切换时机锁定到 PrePhysics 起点 / FrameEnd 终点，避免一帧中"半暂停"。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "UObject/Object.h"
#include "UObject/GCObject.h"
#include "Containers/MpscQueue.h"
#include "Engine/EngineBaseTypes.h"
#include "MassProcessingTypes.h"
#include "MassProcessor.h"
#include "MassProcessorDependencySolver.h"
#include "MassProcessingPhaseManager.generated.h"


struct FMassProcessingPhaseManager;
class UMassProcessor;
class UMassCompositeProcessor;
struct FMassEntityManager;
struct FMassCommandBuffer;
struct FMassProcessingPhaseConfig;


/**
 * FMassProcessingPhaseConfig —— 单个 phase 的"配置"（在 Project Settings 编辑）
 *
 * 由 UMassEntitySettings 中的 ProcessingPhasesConfig[] 持有。每个 phase 一份：
 *   * 决定该 phase 用哪个 composite processor 类承载（PhaseGroupClass）；
 *   * 决定哪些 processor 的 CDO 默认会被纳入此 phase（ProcessorCDOs，运行时由 Configure 拷贝出 RuntimeProcessor）；
 *   * 编辑器下还存一个临时 PhaseProcessor 用于 UI 显示"运行时该 phase 实际的 processor 顺序"。
 *
 * 注：ProcessorCDOs 是 Transient —— 不直接序列化到磁盘，由代码在 settings 加载后填充
 * （处理器自身的 phase 归属来自 UMassProcessor 的 ProcessingPhase 属性 + 自动收集机制）。
 */
USTRUCT()
struct FMassProcessingPhaseConfig
{
	GENERATED_BODY()

	// phase 的显示名（PrePhysics / StartPhysics / ... / FrameEnd）
	UPROPERTY(EditAnywhere, Category = Mass, config)
	FName PhaseName;

	// 该 phase 用什么 composite processor 类来组织子 processor。
	// 默认就是 UMassCompositeProcessor，但可以派生扩展（NoClear 表示不能清空成 None）
	UPROPERTY(EditAnywhere, Category = Mass, config, NoClear)
	TSubclassOf<UMassCompositeProcessor> PhaseGroupClass = UMassCompositeProcessor::StaticClass();

	// 属于该 phase 的 processor CDO 列表（运行时被 Configure 拷贝、依赖求解、装入 PhaseProcessor）
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassProcessor>> ProcessorCDOs;

#if WITH_EDITORONLY_DATA
	// this processor is available only in editor since it's used to present the user the order in which processors
	// will be executed when given processing phase gets triggered
	// 中文：仅编辑器使用 —— 把"运行时实际的 processor 编排结果"暴露到 UI 让用户检视
	UPROPERTY(Transient)
	TObjectPtr<UMassCompositeProcessor> PhaseProcessor = nullptr;

	// 用于 UI 显示的描述文本（例如 phase 整体行为说明）
	UPROPERTY(VisibleAnywhere, Category = Mass, Transient)
	FText Description;
#endif //WITH_EDITORONLY_DATA
};


/**
 * FMassProcessingPhase —— 单个处理阶段的运行时实例（继承 FTickFunction）
 *
 * 每个阶段是一个独立的 FTickFunction：注册到 World 的某个 ETickingGroup，UE 调度系统在该 group 阶段
 * 调用 ExecuteTick，于是触发本 phase 的 PhaseProcessor 跑起来。
 *
 * 两种执行模式（由 cvar mass.FullyParallel 切换，仅在每个 phase 起始切换避免破坏一致性）：
 *   * 并行模式：通过 Executor::TriggerParallelTasks 派发，挂到 MyCompletionGraphEvent->DontCompleteUntil；
 *   * 单线程模式：直接 Executor::Run 同步执行。
 *
 * 委托：
 *   OnPhaseStart  —— ExecuteTick 真正干活前广播
 *   OnPhaseEnd    —— ExecuteTick 干完之前广播（并行模式下在 OnParallelExecutionDone 里）
 */
struct FMassProcessingPhase : public FTickFunction
{
	// phase 起始/结束的多播委托，参数为 DeltaSeconds
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPhaseEvent, const float /*DeltaSeconds*/);

	MASSENTITY_API FMassProcessingPhase();
	// 禁用拷贝：FTickFunction 不支持，且 PhaseManager/PhaseProcessor 引用是绑定关系
	FMassProcessingPhase(const FMassProcessingPhase& Other) = delete;
	FMassProcessingPhase& operator=(const FMassProcessingPhase& Other) = delete;

protected:
	// FTickFunction interface
	// UE 调度系统在每帧合适的 TickGroup 里调用本函数 —— phase 的"心跳"
	MASSENTITY_API virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	// 调试诊断接口（profiler / 帧分析显示用）
	MASSENTITY_API virtual FString DiagnosticMessage() override;
	MASSENTITY_API virtual FName DiagnosticContext(bool bDetailed) override;
	// End of FTickFunction interface

	// 并行模式收尾回调：所有 processor task 完成后由 FMassExecutorDoneTask 调用
	// 内部广播 OnPhaseEnd 并通知 PhaseManager
	MASSENTITY_API void OnParallelExecutionDone(const float DeltaTime);

	// 并行/单线程切换接口（由 PhaseManager 在 OnPhaseStart 检查 cvar 后调用）
	bool IsConfiguredForParallelMode() const { return bRunInParallelMode; }
	void ConfigureForParallelMode() { bRunInParallelMode = true; }
	void ConfigureForSingleThreadMode() { bRunInParallelMode = false; }

	// 是否支持当前 ELevelTick（用 bitmask 表达）—— 比如 LEVELTICK_TimeOnly 期间是否参与 tick
	bool ShouldTick(const ELevelTick TickType) const { return SupportedTickTypes & (1 << TickType); }

public:
	// 由 PhaseManager 在 CreatePhases 阶段调用：绑定 manager / phase 枚举 / TickGroup / PhaseProcessor
	MASSENTITY_API void Initialize(FMassProcessingPhaseManager& InPhaseManager, const EMassProcessingPhase InPhase, const ETickingGroup InTickGroup, UMassCompositeProcessor& InPhaseProcessor);
	// 增/删支持的 tick 类型（LEVELTICK_All、LEVELTICK_TimeOnly 等）
	void AddSupportedTickType(const ELevelTick TickType) { SupportedTickTypes |= (1 << TickType); }
	void RemoveSupportedTickType(const ELevelTick TickType) { SupportedTickTypes &= ~(1 << TickType); }

#if WITH_MASSENTITY_DEBUG
	// 调试接口：拿到当前的 PhaseProcessor（仅可读），用于 FMassDebugger 展示
	const UMassCompositeProcessor* DebugGetPhaseProcessor() const;
#endif // WITH_MASSENTITY_DEBUG

protected:
	friend FMassProcessingPhaseManager;

	// composite processor representing work to be performed. GC-referenced via AddReferencedObjects
	// 中文：本 phase 真正要执行的 composite processor（按依赖顺序持有 child processors）。
	// GC 由 FMassProcessingPhaseManager::AddReferencedObjects 维持。
	TObjectPtr<UMassCompositeProcessor> PhaseProcessor = nullptr;

	// 本 phase 的枚举身份（PrePhysics、StartPhysics 等）
	EMassProcessingPhase Phase = EMassProcessingPhase::MAX;
	// phase 起始/结束的广播委托 —— 用户代码可订阅（GameInstance / Subsystem 等）
	FOnPhaseEvent OnPhaseStart;
	FOnPhaseEvent OnPhaseEnd;

private:
	// 反指 manager —— ExecuteTick 内部要回调 OnPhaseStart/OnPhaseEnd
	FMassProcessingPhaseManager* PhaseManager = nullptr;
	// 标记是否正处于 Mass 处理中（atomic：可能在 worker 线程读，主线程改）
	std::atomic<bool> bIsDuringMassProcessing = false;
	// 是否启用并行模式 —— 由 cvar mass.FullyParallel 在 phase 起始处同步
	bool bRunInParallelMode = true;
	// 支持的 ELevelTick 位掩码 —— 见 ShouldTick
	uint8 SupportedTickTypes = 0;
};


/**
 * FMassPhaseProcessorConfigurationHelper —— 临时帮手对象，用于"构建/重建一个 phase 的 PhaseProcessor"
 *
 * 把【现有 PhaseProcessor 子链 + 来自 PhaseConfig 的 CDO 模板 + 动态 processor】合并到一个临时 pipeline，
 * 移除已被 Unregister 的动态 processor，再喂给 FMassProcessorDependencySolver 求依赖顺序，
 * 最后用 SortedProcessors 更新 PhaseProcessor.UpdateProcessorsCollection、构建并行执行图。
 *
 * 调用上下文：仅在 OnPhaseStart 检测到"图脏"时一次性构建。
 */
struct FMassPhaseProcessorConfigurationHelper
{
	FMassPhaseProcessorConfigurationHelper(UMassCompositeProcessor& InOutPhaseProcessor, const FMassProcessingPhaseConfig& InPhaseConfig, UObject& InProcessorOuter, EMassProcessingPhase InPhase)
		: PhaseProcessor(InOutPhaseProcessor), PhaseConfig(InPhaseConfig), ProcessorOuter(InProcessorOuter), Phase(InPhase)
	{
	}

	/** 
	 * @param InWorldExecutionFlags - provide EProcessorExecutionFlags::None to let underlying code decide
	 *
	 * 中文：构建/更新 PhaseProcessor 的内部 pipeline（求解依赖、装入 child processors、构建并行图）
	 *
	 * @param DynamicProcessors             当前注册中的动态 processor（来自 PhaseManager.DynamicProcessors）
	 * @param InOutRemovedDynamicProcessors 已被 Unregister 但可能还残留在 PhaseProcessor 内的动态 processor
	 *                                      —— 函数会从 TmpPipeline 把它们清掉、并在成功后从该列表移除
	 * @param InWorldExecutionFlags         运行环境标志（ServerOnly/ClientOnly/Standalone 等），None=自动判断
	 * @param EntityManager                 当前关联的 EntityManager（dependency solver 需要它去判断 archetype 命中）
	 * @param InOutOptionalResult           dependency solver 的结果缓存（包含 PrunedProcessors 和图签名等，用于增量比对）
	 */
	MASSENTITY_API void Configure(TArrayView<UMassProcessor* const> DynamicProcessors, TArray<TWeakObjectPtr<UMassProcessor>>& InOutRemovedDynamicProcessors
		, EProcessorExecutionFlags InWorldExecutionFlags, const TSharedRef<FMassEntityManager>& EntityManager
		, FMassProcessorDependencySolver::FResult& InOutOptionalResult);

	UMassCompositeProcessor& PhaseProcessor;          // 要被(重)配置的 composite processor
	const FMassProcessingPhaseConfig& PhaseConfig;    // 该 phase 的配置（CDO 列表来源）
	UObject& ProcessorOuter;                          // 新建 processor 实例的 outer（owner）
	EMassProcessingPhase Phase;                       // 当前在配的 phase 编号
	bool bInitializeCreatedProcessors = true;         // Configure 完后是否调用 PhaseProcessor.InitializeInternal（默认是）
	bool bIsGameRuntime = true;                       // 区分编辑器/运行时环境，会影响 dependency solver 行为

	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
	// 5.6 起废弃：旧版 Configure（用 SharedPtr+裸指针的 Result）。请用上面的 SharedRef+引用版本。
	UE_DEPRECATED(5.6, "This flavor of Configure is deprecated. Please use the one using a TSharedRef<FMassEntityManager> parameter instead")
	MASSENTITY_API void Configure(TArrayView<UMassProcessor* const> DynamicProcessors, EProcessorExecutionFlags InWorldExecutionFlags
		, const TSharedPtr<FMassEntityManager>& EntityManager = TSharedPtr<FMassEntityManager>()
		, FMassProcessorDependencySolver::FResult* OutOptionalResult = nullptr);
};

/** 
 * MassProcessingPhaseManager owns separate FMassProcessingPhase instances for every ETickingGroup. When activated
 * via Start function it registers and enables the FMassProcessingPhase instances which themselves are tick functions 
 * that host UMassCompositeProcessor which they trigger as part of their Tick function. 
 * MassProcessingPhaseManager serves as an interface to said FMassProcessingPhase instances and allows initialization
 * with collections of processors (via Initialize function) as well as registering arbitrary functions to be called 
 * when a particular phase starts or ends (via GetOnPhaseStart and GetOnPhaseEnd functions). 
 *
 * 中文：MassProcessingPhaseManager —— Mass 调度协调员（Mass↔UE Tick 的桥梁）
 *
 * 持有 6 份 FMassProcessingPhase（对应每个 EMassProcessingPhase），每份是一个 FTickFunction。
 * Start() 把它们注册到 World 的 PersistentLevel；UE 在每个对应 TickGroup 期间触发其 ExecuteTick，
 * 进而走 Executor::Run / TriggerParallelTasks 跑 PhaseProcessor。
 *
 * 为什么要 FGCObject + TSharedFromThis：
 *   * FGCObject —— 管 PhaseProcessor / DynamicProcessors / PrunedProcessors 这些 UObject 的 GC 引用
 *   * TSharedFromThis —— 调试系统弱引用本对象（FMassDebugger::RegisterProcessorDataProvider 用 AsWeak()）
 *
 * 暂停/恢复语义：
 *   * Pause() —— 在下一个 FrameEnd 结束时正式暂停（让当前帧完整跑完）
 *   * Resume() —— 在下一个 PrePhysics 起点时正式恢复（让下一帧从头开始）
 *   暂停期间 phase 仍然 Tick（OnPhaseStart/End 委托照样广播），但 PhaseProcessor 不会被 Run。
 */
struct FMassProcessingPhaseManager : public FGCObject, public TSharedFromThis<FMassProcessingPhaseManager>
{
public:
	// @param InProcessorExecutionFlags 默认 None；由 Initialize 时用 World 类型自动确定（Server/Client/Standalone）
	MASSENTITY_API explicit FMassProcessingPhaseManager(EProcessorExecutionFlags InProcessorExecutionFlags = EProcessorExecutionFlags::None);
	FMassProcessingPhaseManager(const FMassProcessingPhaseManager& Other) = delete;
	FMassProcessingPhaseManager& operator=(const FMassProcessingPhaseManager& Other) = delete;

	const TSharedPtr<FMassEntityManager>& GetEntityManager() const { return EntityManager; }
	FMassEntityManager& GetEntityManagerRef() { check(EntityManager); return *EntityManager.Get(); }

	/** Retrieves OnPhaseStart multicast delegate's reference for a given Phase */
	// 订阅 phase 起始事件（用户代码典型用法：在某个 phase 开始时刷新数据）
	FMassProcessingPhase::FOnPhaseEvent& GetOnPhaseStart(const EMassProcessingPhase Phase) { return ProcessingPhases[uint8(Phase)].OnPhaseStart; } //-V557
	/** Retrieves OnPhaseEnd multicast delegate's reference for a given Phase */
	// 订阅 phase 结束事件
	FMassProcessingPhase::FOnPhaseEvent& GetOnPhaseEnd(const EMassProcessingPhase Phase) { return ProcessingPhases[uint8(Phase)].OnPhaseEnd; }

	/** 
	 *  Populates hosted FMassProcessingPhase instances with Processors read from MassEntitySettings configuration.
	 *  Calling this function overrides previous configuration of Phases.
	 *
	 *  中文：用 MassEntitySettings 中的配置初始化所有 phase。
	 *  动作：为每个 phase 新建一个 UMassCompositeProcessor 实例（PhaseProcessor），
	 *  绑定到 FMassProcessingPhase，并设定支持的 tick 类型。
	 *  此时 PhaseProcessor 内部还没有 child processors —— 真正的依赖求解发生在第一次 OnPhaseStart。
	 */
	MASSENTITY_API void Initialize(UObject& InOwner, TConstArrayView<FMassProcessingPhaseConfig> ProcessingPhasesConfig, const FString& DependencyGraphFileName = TEXT(""));

	/** Needs to be called before destruction, ideally before owner's BeginDestroy (a FGCObject's limitation) */
	// 中文：清理动态 processor / 图缓存 / pending 队列。必须在 owner BeginDestroy 之前调（FGCObject 限制）。
	MASSENTITY_API void Deinitialize();

	/**
	 * 中文：手动触发某个 phase 执行。Tick 系统调用之外的"手动 phase"入口。
	 * 实际就是直接调 ProcessingPhases[Phase].ExecuteTick。
	 * 返回的 FGraphEventRef 与传入的 MyCompletionGraphEvent 是同一个。
	 *
	 * @param Phase                 要触发的 phase
	 * @param DeltaTime             时间步长
	 * @param MyCompletionGraphEvent 调用方提供的 completion event；并行模式下 phase 会 DontCompleteUntil 到内部 task
	 * @param CurrentThread         调用线程（默认 GameThread）
	 */
	MASSENTITY_API const FGraphEventRef& TriggerPhase(const EMassProcessingPhase Phase, const float DeltaTime, const FGraphEventRef& MyCompletionGraphEvent
		, ENamedThreads::Type CurrentThread = ENamedThreads::GameThread);

	/** 
	 *  Stores EntityManager associated with given world's MassEntitySubsystem and kicks off phase ticking.
	 *
	 *  中文：从 World 自动找到 UMassEntitySubsystem，取其 EntityManager 并启动 phase tick。
	 */
	MASSENTITY_API void Start(UWorld& World);
	
	/**
	 *  Stores InEntityManager as the entity manager. It also kicks off phase ticking if the given InEntityManager is tied to a UWorld.
	 *
	 *  中文：直接用给定的 EntityManager 启动。
	 *    * 注册 OnNewArchetype 监听（让 archetype 变化能让所有 phase 的图缓存失效）；
	 *    * 注册调试 provider（WITH_MASSENTITY_DEBUG）；
	 *    * 若 EntityManager 关联到 UWorld 则调 EnableTickFunctions 让 phase tick 开始跑。
	 */
	MASSENTITY_API void Start(const TSharedRef<FMassEntityManager>& InEntityManager);
	// 停掉 phase tick，移除监听，清空 EntityManager
	MASSENTITY_API void Stop();
	// 是否已经 Start（以 EntityManager 是否有效为判据）
	bool IsRunning() const { return EntityManager.IsValid(); }

	/**
	 * Determine if this Phase Manager is currently paused.
	 * 
	 * While paused, phases will transition as usual, but processors
	 * will not be executed.
	 * 
	 * @return True if this PhaseManager is currently paused; else False
	 *
	 * 中文：暂停状态查询。暂停期间 phase 依然 tick / 广播 OnPhaseStart/End，但 PhaseProcessor 不会被执行。
	 */
	bool IsPaused() const;

	/**
	 * Pause this phase manager at the earliest opportunity (on next FrameEnd phase end).
	 * This allows the current phase cycle to complete before the pause takes effect.
	 *
	 * 中文：申请暂停。生效时机：下一个 FrameEnd 的 OnPhaseEnd —— 保证当前帧完整跑完。
	 * 必须在 GameThread 调用。
	 */
	MASSENTITY_API void Pause();

	/**
	 * Unpause this phase manager at the earliest opportunity (on next PrePhysics phase start).
	 *
	 * 中文：申请恢复。生效时机：下一个 PrePhysics 的 OnPhaseStart —— 让下一帧从头开始。
	 * 必须在 GameThread 调用。
	 */
	MASSENTITY_API void Resume();

	// 调试名（"<Owner>_MassProcessingPhaseManager"）
	MASSENTITY_API FString GetName() const;

	/** Registers a dynamic processor. This needs to be a fully formed processor and will be slotted in during the next tick. */
	// 中文：动态注册一个 processor。
	//   * 必须是完整构造好的 processor（已设好 ProcessingPhase）；
	//   * 实际加入是异步的：操作进入 MPSC 队列，下一次该 phase 的 OnPhaseStart 时统一 dequeue 并刷新图。
	//   * 线程安全：MPSC 队列允许多生产者，单消费者（GameThread 在 OnPhaseStart 中消费）。
	MASSENTITY_API void RegisterDynamicProcessor(UMassProcessor& Processor);
	/** Removes a previously registered dynamic processor of throws an assert if not found. */
	// 中文：动态反注册。同样异步入队，下一次 OnPhaseStart 处理。
	MASSENTITY_API void UnregisterDynamicProcessor(UMassProcessor& Processor);

	/**
	 * FPhaseGraphBuildState —— 单个 phase 的依赖图构建缓存
	 *
	 * 用于增量重建：只有当任一标志为 true 时才重新跑 dependency solver。
	 *
	 * 标志驱动：
	 *   * bNewArchetypes  —— OnNewArchetype 委托触发时被设 true（archetype 集合变了，prune 结果可能变）
	 *   * bProcessorsNeedRebuild —— 动态 processor 增/删时被设 true
	 *   * bInitialized    —— 第一次 Configure 完成后变 true；初始 false 强制首次构建
	 *
	 * LastResult —— 上次求解结果缓存。包含 PrunedProcessors（被删掉、不参与运行但仍持 GC 引用的 processor）
	 * 等信息。dependency solver 用它做"图签名比对"，可避免重复求解。
	 */
	struct FPhaseGraphBuildState
	{
		FMassProcessorDependencySolver::FResult LastResult;  // 上次依赖求解结果（含 PrunedProcessors 和签名）
		bool bNewArchetypes = true;          // 有新 archetype 出现 —— 需要重新检查 prune
		bool bProcessorsNeedRebuild = true;  // 动态 processor 增删 —— 需要重建
		bool bInitialized = false;            // 是否已成功构建过一次

		// 重置缓存（不重置 b*Rebuild 标志 —— 那些表达"需要重建"的语义）
		void Reset();
	};

#if WITH_MASSENTITY_DEBUG
	// 调试用：把 6 个 phase / 6 个图状态以 view 形式暴露给 FMassDebugger
	TConstArrayView<FMassProcessingPhase>  DebugGetProcessingPhases() const;
	TConstArrayView<FPhaseGraphBuildState> DebugGetProcessingGraphBuildStates() const;
#endif // WITH_MASSENTITY_DEBUG

protected:
	// FGCObject interface
	// 中文：注册所有 UObject 引用给 GC（PhaseProcessors / DynamicProcessors / 各 phase 的 PrunedProcessors）
	MASSENTITY_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FMassProcessingPhaseManager");
	}
	// End of FGCObject interface

	// 真正添加/移除动态 processor 的内部实现 —— 在 GameThread 上、phase 起始时被调
	MASSENTITY_API void RegisterDynamicProcessorInternal(TNotNull<UMassProcessor*> Processor);
	MASSENTITY_API void UnregisterDynamicProcessorInternal(TNotNull<UMassProcessor*> Processor);
	// 处理 PendingDynamicProcessors[PhaseIndex] 里所有挂起的 add/remove 操作
	MASSENTITY_API void HandlePendingDynamicProcessorOperations(const int32 PhaseIndex);

	/** Override this function if you want to modify how the phase tick functions get executed. */
	// 中文：把所有 phase 的 tick function 注册到 World 并启用。
	// 子类可以 override 以自定义优先级 / 注册策略。
	MASSENTITY_API virtual void EnableTickFunctions(const UWorld& World);

	/** Creates phase processors instances for each declared phase name, based on MassEntitySettings */
	// 中文：为每个 phase 创建一个 UMassCompositeProcessor 实例（即 PhaseProcessor）
	// 注：当前定义在 Initialize 内部完成；该函数本身在 .cpp 里没有显式实现 —— 是声明遗留？
	MASSENTITY_API void CreatePhases();

	friend FMassProcessingPhase;

	/** 
	 *  Called by the given Phase at the very start of its execution function (the FMassProcessingPhase::ExecuteTick),
	 *  even before the FMassProcessingPhase.OnPhaseStart broadcast delegate
	 *
	 *  中文：phase 起始钩子（在用户的 OnPhaseStart 之前）。负责：
	 *    1) 处理 pending 暂停/恢复（在 phase 0 = PrePhysics 起点恢复）；
	 *    2) 同步 cvar mass.FullyParallel 开关 → phase 的并行模式；
	 *    3) 处理动态 processor 队列中挂起的 add/remove；
	 *    4) 如果图脏，调 FMassPhaseProcessorConfigurationHelper 重建 PhaseProcessor。
	 */
	MASSENTITY_API void OnPhaseStart(FMassProcessingPhase& Phase);

	/**
	 *  Called by the given Phase at the very end of its execution function (the FMassProcessingPhase::ExecuteTick),
	 *  after the FMassProcessingPhase.OnPhaseEnd broadcast delegate
	 *
	 *  中文：phase 结束钩子（在用户的 OnPhaseEnd 之后）。负责：
	 *    1) 在 FrameEnd 结束时正式生效"暂停"；
	 *    2) flush EntityManager 主缓冲（若还有 pending 命令）。
	 */
	MASSENTITY_API void OnPhaseEnd(FMassProcessingPhase& Phase);

	// EntityManager.OnNewArchetypeEvent 的委托回调：把 6 个 phase 的 bNewArchetypes 全部置 true
	// 下一次任意 phase 的 OnPhaseStart 都会触发依赖图重建（因为 archetype 集合变了，prune 结果可能不同）
	MASSENTITY_API void OnNewArchetype(const FMassArchetypeHandle& NewArchetype);

protected:
	// 6 个 phase 的运行时实例（按 EMassProcessingPhase 枚举索引）
	FMassProcessingPhase ProcessingPhases[(uint8)EMassProcessingPhase::MAX];
	// 每个 phase 的依赖图构建缓存
	FPhaseGraphBuildState ProcessingGraphBuildStates[(uint8)EMassProcessingPhase::MAX];
	// 每个 phase 的配置（来自 MassEntitySettings，Initialize 时拷贝进来）
	TArray<FMassProcessingPhaseConfig> ProcessingPhasesConfig;
	// 当前注册中的动态 processor（Strong 引用 —— GC 由 AddReferencedObjects 维护）
	TArray<TObjectPtr<UMassProcessor>> DynamicProcessors;
	// 已 Unregister 但可能还嵌在某个 PhaseProcessor 内的动态 processor —— 下次 Configure 时会被清掉
	TArray<TWeakObjectPtr<UMassProcessor>> RemovedDynamicProcessors;

	// 动态 processor 操作类型（add/remove）
	enum class EDynamicProcessorOperationType : uint8
	{
		Add,
		Remove
	};
	/** using TStrongObjectPtr to not worry about GC while the processor instances are waiting in PendingDynamicProcessors */
	// 中文：(processor强引用, 操作类型) 二元组。用 TStrongObjectPtr 是因为这些 processor 在队列里等候时
	// 暂时不在 DynamicProcessors 数组中，没有别的 UObject 引用 —— 不抓住的话会被 GC。
	using FDynamicProcessorOperation = TPair<TStrongObjectPtr<UMassProcessor>, EDynamicProcessorOperationType>;
	// 每个 phase 一份 MPSC 队列：多生产者（任意线程注册）+ 单消费者（GameThread OnPhaseStart 消费）
	// 选择 MPSC 而非通用 lock 是因为：注册操作可能在任何线程，但消费始终在 GameThread —— 经典 MPSC 场景
	TMpscQueue<FDynamicProcessorOperation> PendingDynamicProcessors[(uint8)EMassProcessingPhase::MAX];

	// 关联的 EntityManager（也是"是否运行中"的标志 —— Stop 时会 reset）
	TSharedPtr<FMassEntityManager> EntityManager;

	// 当前正在执行的 phase（OnPhaseStart 设、OnPhaseEnd 清）。MAX 表示空闲
	EMassProcessingPhase CurrentPhase = EMassProcessingPhase::MAX;

	// owner（创建本 manager 的对象，通常是 UMassEntitySubsystem 或派生 GameInstanceSubsystem）。WeakObjectPtr 避免循环引用。
	TWeakObjectPtr<UObject> Owner;

	// 注册到 EntityManager.OnNewArchetypeEvent 的句柄（Stop 时移除）
	FDelegateHandle OnNewArchetypeHandle;

	// 当前的执行环境标志（Server/Client/Standalone），影响 dependency solver 的 prune 策略
	EProcessorExecutionFlags ProcessorExecutionFlags = EProcessorExecutionFlags::None;
	// 总开关：是否允许 phase tick（Initialize 后变 true，Stop 后 false）
	bool bIsAllowedToTick = false;

	// 是否处于暂停状态
	bool bIsPaused = false;
	// 暂停/恢复请求挂起中（等到合适的 phase 边界才生效）
	bool bIsPauseTogglePending = false;

#if WITH_MASSENTITY_DEBUG
	// 调试事件订阅句柄（FMassDebugger 在 EntityManager 初始化/反初始化时通知本管理器）
	FDelegateHandle OnDebugEntityManagerInitializedHandle;
	FDelegateHandle OnDebugEntityManagerDeinitializedHandle;

	// 调试事件回调（当前实现为空，作为预留扩展点）
	MASSENTITY_API void OnDebugEntityManagerInitialized(const FMassEntityManager&);
	MASSENTITY_API void OnDebugEntityManagerDeinitialized(const FMassEntityManager&);
#endif // WITH_MASSENTITY_DEBUG

public:
	// 5.6 起废弃：旧版 Start（参数为 SharedPtr）。请改用 SharedRef 版本。
	UE_DEPRECATED(5.6, "This flavor of Start is deprecated. Please use the one using a TSharedRef<FMassEntityManager> parameter instead")
	MASSENTITY_API void Start(const TSharedPtr<FMassEntityManager>& InEntityManager);
};

//----------------------------------------------------------------------//
// inlines —— 内联实现
//----------------------------------------------------------------------//
#if WITH_MASSENTITY_DEBUG
// 调试 getter：返回 PhaseProcessor 的 const 指针
inline const UMassCompositeProcessor* FMassProcessingPhase::DebugGetPhaseProcessor() const
{
	return PhaseProcessor;
}

// 调试 view：6 个 phase 的只读视图
inline TConstArrayView<FMassProcessingPhase>  FMassProcessingPhaseManager::DebugGetProcessingPhases() const
{
	return MakeArrayView(ProcessingPhases, static_cast<uint8>(EMassProcessingPhase::MAX));
}

// 调试 view：6 个图状态的只读视图
inline TConstArrayView<FMassProcessingPhaseManager::FPhaseGraphBuildState> FMassProcessingPhaseManager::DebugGetProcessingGraphBuildStates() const
{
	return MakeArrayView(ProcessingGraphBuildStates, static_cast<uint8>(EMassProcessingPhase::MAX));
}
#endif // WITH_MASSENTITY_DEBUG

inline bool FMassProcessingPhaseManager::IsPaused() const 
{
	return bIsPaused; 
}
