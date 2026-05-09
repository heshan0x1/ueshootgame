// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "CoreMinimal.h"
#include "MassEntityManager.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassProcessingTypes.h"
#include "Async/TaskGraphInterfaces.h"
#include "MassCommandBuffer.h"
#include "MassRequirements.h"
#include "MassProcessor.generated.h"


#define UE_API MASSENTITY_API

struct FMassProcessingPhaseConfig;
class UMassCompositeProcessor;
struct FMassDebugger;
struct FMassEntityQuery;
struct FMassEntityManager;
struct FMassExecutionContext;
struct FMassExecutionRequirements;
struct FMassSubsystemRequirements;
namespace UE::Mass
{
	struct FQueryExecutor;
}

//===========================================================================
// 【中文】EProcessorCompletionStatus —— 单个 processor 任务在 composite 调度中的完成状态。
//---------------------------------------------------------------------------
//   - Invalid   : 初始 / 未启动；
//   - Threaded  : 已派发给 TaskGraph，未完成；
//   - Postponed : 当前阶段被推迟（例如等待依赖）；
//   - Done      : 已完成执行（CompletionEvent 也满足 IsComplete）。
// 主要由 UMassCompositeProcessor::FProcessorCompletion::IsDone() 使用。
//===========================================================================
enum class EProcessorCompletionStatus : uint8
{
	Invalid,
	Threaded,
	Postponed,
	Done
};

//===========================================================================
// 【中文】FMassProcessorExecutionOrder —— 处理器的"顺序约束"声明。
//---------------------------------------------------------------------------
// 模型：
//   ExecuteInGroup    : 我属于哪个 group（None = phase 顶层 group）
//   ExecuteBefore[]   : 我必须在这些 processor / group 之前跑（同一 phase 内）
//   ExecuteAfter[]    : 我必须在这些 processor / group 之后跑（同一 phase 内）
// 这些声明是 FMassProcessorDependencySolver 的输入，被解析后产生 FlatProcessingGraph。
//
// USTRUCT 反射：可在编辑器与 ProjectSettings/MassEntitySettings 中配置；
//   - EditAnywhere : 编辑器中任意层级都能改；
//   - config      : 写入到 .ini （Mass 配置文件）。
//===========================================================================
USTRUCT()
struct FMassProcessorExecutionOrder
{
	GENERATED_BODY()

	/** Determines which processing group this processor will be placed in. Leaving it empty ("None") means "top-most group for my ProcessingPhase" */
	// 中文：所属 group 名（支持点号嵌套，如 "AI.Locomotion"）。空 = phase 顶层组。
	UPROPERTY(EditAnywhere, Category = Processor, config)
	FName ExecuteInGroup = FName();

	// 中文：必须先于这些名称完成。可填 processor 类名或 group 名。
	UPROPERTY(EditAnywhere, Category = Processor, config)
	TArray<FName> ExecuteBefore;

	// 中文：必须在这些名称完成之后才能跑。
	UPROPERTY(EditAnywhere, Category = Processor, config)
	TArray<FName> ExecuteAfter;
};

//===========================================================================
// 【中文】EActivationState —— processor 的运行时启用状态。
//---------------------------------------------------------------------------
//   - Inactive : CallExecute 不会调，但 *仍参与依赖解析*（让其他 processor 的依赖正确传递）。
//   - Active   : 每次到达对应 phase 都会被执行。
//   - OneShot  : 与 Active 一样跑一次，跑完后 CallExecute 自动 MakeInactive() ⇒ 下次 phase 不再跑。
// 一个常见用法：通过蓝图 SetActive 在运行时打开/关闭某个调试 processor。
//===========================================================================
UENUM()
enum class EActivationState : uint8
{
	Inactive,
	Active,
	OneShot,	// one-shot processor will auto-disable itself after the next CallExecute call
};

/**
 * Values determining whether a processor wants to be pruned at runtime. The value is not used when
 * processing graph is generated for project configuration purposes or debug-time graph visualization purposes
 * This behavior can be overridden by UMassProcessor::ShouldAllowQueryBasedPruning child class overrides
 */
//===========================================================================
// 【中文】EMassQueryBasedPruning —— "如果我所有 query 都没匹配的 archetype，就把我从执行图剪掉"。
//---------------------------------------------------------------------------
// 仅在运行时生效（Project Settings / 调试视图绘制时不剪）。
// 子类可重写 ShouldAllowQueryBasedPruning(bRuntimeMode) 提供更精细控制。
//===========================================================================
UENUM()
enum class EMassQueryBasedPruning : uint8
{
	Prune,		// pruning will always be applied at runtime
	Never,		// pruning will never be applied at runtime
	Default = Prune
};

//===========================================================================
// 【中文整体说明】UMassProcessor —— ECS 的"逻辑承载体"
//---------------------------------------------------------------------------
// 【角色】Processor 是 Mass 中"行为代码 + Query"的打包单元。区别于纯数据的 Fragment，
// Processor 是 ECS 中的"系统(System)"，每帧在某个 phase 被调度执行。
//
// 【为什么 Processor 是 UObject 而 Query 不是】
//   - Processor 需要：
//       * 在 Project Settings 的 MassEntitySettings 中可见可配（config）
//       * 支持蓝图扩展、EditInlineNew、CDO 模板/反射
//       * 跨模块发现（GetDerivedClasses）
//     ⇒ 必须是 UObject。
//   - FMassEntityQuery 是临时声明 / 短生命周期的"约束包"：
//       * 通常作为 processor 的 *成员变量*；
//       * 由 Processor::ConfigureQueries 中通过 RegisterQuery 注册指针；
//       * 不需要反射/CDO；
//     ⇒ 普通 USTRUCT 即可。
//
// 【UCLASS 反射宏参数】
//   abstract             : 不能直接 NewObject<UMassProcessor>，必须子类化；
//   EditInlineNew        : 编辑器中可以"新建实例"（用于 composite processor 内嵌列表配置）；
//   CollapseCategories   : 编辑器属性面板里把分类合并展示；
//   config = Mass        : 配置文件名为 Mass*.ini；
//   defaultconfig        : 写默认配置（DefaultMass.ini）；
//   ConfigDoNotCheckDefaults : 不强制 default 检查（避免 CDO 启动时出现 config 不一致警告）；
//   MinimalAPI           : 只导出元数据，不导出全部成员（构建期优化）。
//
// 【两种使用模式】
//   (1) 手动模式 —— 推荐子类大量逻辑使用：
//         class UMyProcessor : public UMassProcessor {
//             FMassEntityQuery MyQuery;            // 成员变量
//             virtual void ConfigureQueries(...) override {
//                 MyQuery.AddRequirement<...>();
//                 ...
//                 RegisterQuery(MyQuery);          // 必须 ⇐
//             }
//             virtual void Execute(...) override {
//                 MyQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx) { ... });
//             }
//         };
//
//   (2) 声明模式 —— 给 AutoExecuteQuery 赋一个 FQueryExecutor，框架的默认 Execute 会自动调它的 CallExecute。
//       适合纯数据驱动 / 简单情况。
//
// 【生命周期 & 钩子函数】
//   - 构造（默认 ExecutionFlags = Server | Standalone）
//   - PostInitProperties → 设置 StatId（CPU profile）
//   - CallInitialize：
//       * 初始化所有 OwnedQueries；
//       * 调 ConfigureQueries(EntityManager)（子类填 query requirement、注册）；
//       * 自动派生 bRequiresGameThreadExecution（任一 query 标记 GT 即整体 GT）；
//       * 调 InitializeInternal（子类自定义初始化）；
//       * 标记 bInitialized = true。
//   - 每帧 phase 触发 → CompositeProcessor::Execute → CallExecute → Execute()
//   - DispatchProcessorTasks: 多线程模式下创建 TaskGraph 任务（GT 或非 GT 子类）。
//
// 【关键字段（见后）】ExecutionOrder / ProcessingPhase / ExecutionFlags / ActivationState / ExecutionPriority。
//===========================================================================
UCLASS(abstract, EditInlineNew, CollapseCategories, config = Mass, defaultconfig, ConfigDoNotCheckDefaults, MinimalAPI)
class UMassProcessor : public UObject
{
	GENERATED_BODY()

public:

	UE_API UMassProcessor();
	UE_API explicit UMassProcessor(const FObjectInitializer& ObjectInitializer);

	// 中文：是否已调过 CallInitialize 完成初始化。
	bool IsInitialized() const;

	/** Calls InitializeInternal and handles initialization bookkeeping. */
	// 中文：初始化入口。框架在 phase manager 注册 processor 实例时调用。
	// 该函数是 *非虚* 的；子类应该重写 InitializeInternal 而非 Initialize。
	UE_API void CallInitialize(const TNotNull<UObject*> Owner, const TSharedRef<FMassEntityManager>& EntityManager);
	
	// 中文：派发该 processor 为 TaskGraph 任务（多线程模式下被 CompositeProcessor 调用）。
	// 默认实现：根据 bRequiresGameThreadExecution 选 FMassProcessorTask 还是 FMassProcessorsTask_GameThread。
	// 返回 FGraphEventRef 给依赖此 processor 完成的下游使用。
	UE_API virtual FGraphEventRef DispatchProcessorTasks(const TSharedPtr<FMassEntityManager>& EntityManager, FMassExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites = FGraphEventArray());

	// 中文：返回 NetMode 过滤位掩码（Server/Client/Standalone/Editor），ExecutionFlags 字段的 enum 包装。
	EProcessorExecutionFlags GetExecutionFlags() const;

	/** Whether this processor should execute according the CurrentExecutionFlags parameters */
	// 中文：当前 World 的 NetMode 与本 processor 的 ExecutionFlags 是否有交集。无 = 跳过。
	bool ShouldExecute(const EProcessorExecutionFlags CurrentExecutionFlags) const;
	// 中文：执行入口。流程：检查 IsActive → 设置 debug 描述 → CacheSubsystemRequirements
	//       → Execute(EntityManager, Context) → 若 OneShot 自动 MakeInactive。
	UE_API void CallExecute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

	/** 
	 * Controls whether there can be multiple instances of a given class in a single FMassRuntimePipeline and during 
	 * dependency solving. 
	 */
	// 中文：默认 false ⇒ 每个 phase 只允许一个该类实例（若用户配多份会被 dedup）。
	// 设为 true 可允许多实例（"Dynamic processor"也典型走这条路）。
	bool ShouldAllowMultipleInstances() const;

	void DebugOutputDescription(FOutputDevice& Ar) const;
	UE_API virtual void DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const;
	UE_API virtual FString GetProcessorName() const;
	
	//----------------------------------------------------------------------//
	// Ordering functions 
	//----------------------------------------------------------------------//
	/**
	 * Indicates whether this processor can ever be pruned while considered for a phase processing graph. A processor
	 * can get pruned if none of its registered queries interact with archetypes instantiated at the moment of graph
	 * building. This can also happen for special processors that don't register any queries - if that's the case override 
	 * this function to return an appropriate value or use QueryBasedPruning to configure the expected behavior.
	 * By default, the processor will be the subject of pruning when bRuntimeMode == true.
	 * @param bRuntimeMode indicates whether the pruning is being done for game runtime (true) or editor-time presentation (false)
	 */
	// 中文：query-based pruning 是性能优化 —— 如果一个 processor 的所有 query 当前在世界中无任何 archetype 匹配，
	// 那它跑也是空跑，不如把它从执行图中剪掉。
	// 默认实现：仅运行时（bRuntimeMode=true）且 QueryBasedPruning==Prune 时返回 true。
	// 子类如果有"不靠 query 的副作用"（例如周期性发命令），应重写返回 false。
	UE_API virtual bool ShouldAllowQueryBasedPruning(const bool bRuntimeMode = true) const;

	UE_API virtual EMassProcessingPhase GetProcessingPhase() const;
	UE_API virtual void SetProcessingPhase(EMassProcessingPhase Phase);
	// 中文：依赖求解器据此把 processor 钉在 GameThread 任务上。详见 InitializeInternal 的自动派生逻辑。
	bool DoesRequireGameThreadExecution() const;
	
	const FMassProcessorExecutionOrder& GetExecutionOrder() const;

	/** By default,  fetches requirements declared entity queries registered via RegisterQuery. Processors can override 
	 *	this function to supply additional requirements */
	// 中文：把 OwnedQueries 的所有 read/write requirement 收集到 OutRequirements。
	// 依赖求解器据此判断"两个 processor 是否可并发"（write-write 或 write-read 冲突 ⇒ 必须串行）。
	UE_API virtual void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

	const FMassSubsystemRequirements& GetProcessorRequirements() const;

	/**
	 * @return the current value of ExecutionPriority
	 * @see SetExecutionPriority
	 * @see ExecutionPriority
	 */
	int16 GetExecutionPriority() const;

	/**
	 * Sets new ExecutionPriority for this processor. The change will take effect the next time the processing graph is built
	 * Note that at this point this operation does not cause processing graph rebuilding, so this function should be used
	 * before processor's initialization or as part of code that will cause processing graph rebuilding anyway.
	 * @see ExecutionPriority
	 */
	// 中文：设新 ExecutionPriority。注意：当前实现 *不会* 触发图重建，需要在 init 前调或者随同其他重建调。
	void SetExecutionPriority(const int16 NewExecutionPriority);

	/** Adds Query to RegisteredQueries list. Query is required to be a member variable of this processor. Not meeting
	 *  this requirement will cause check failure and the query won't be registered. */
	// 中文：注册一个 query 给本 processor 拥有。强制要求：query 必须是 *本 processor 实例的成员变量*。
	// 通过指针地址范围检查（[ThisStart, ThisEnd] 包含 [QueryStart, QueryEnd]）来验证 —— 这就是为什么 OwnedQueries 用裸指针安全：
	// query 与 processor 同生命周期。
	UE_API void RegisterQuery(FMassEntityQuery& Query);

	// 中文：动态 processor 标记。一旦设置永不清除（私有字段保护）。
	// 动态 processor 特性：bAutoRegisterWithProcessingPhases==false，且允许同类型多实例（绕过 ShouldAllowMultipleInstances）。
	void MarkAsDynamic();
	bool IsDynamic() const;

	/**
	 * Marks processor as "Active" (@see ActivationState for details). If called during Mass processing the
	 * call will take effect next phase.
	 */
	void MakeActive();
	/**
	 * Marks processor as "One Shot" (@see ActivationState for details). If called during Mass processing
	 * the call will take effect next phase. The processor will auto-disable after execution.
	 */
	void MakeOneShot();
	/**
	 * Deactivate the processor, it will no longer execute its `Execute` function.
	 */
	void MakeInactive();

	bool IsActive() const;

	// 中文：是否应自动加入全局 processor 列表（即 phase manager 自动收集的列表）。
	bool ShouldAutoAddToGlobalList() const;
#if WITH_EDITOR
	// 中文：仅编辑器：在 MassEntitySettings UI 中是否显示。
	// 即使关闭 AutoAdd，也可能因 bCanShowUpInSettings 而显示（用户可手动添加）。
	bool ShouldShowUpInSettings() const;
#endif // WITH_EDITOR

	/** Sets bAutoRegisterWithProcessingPhases. Setting it to true will result in this processor class being always 
	 * instantiated to be automatically evaluated every frame. @see FMassProcessingPhaseManager
	 * Note that calling this function is only valid on CDOs. Calling it on a regular instance will fail an ensure and 
	 * have no other effect, i.e. CDO's value won't change */
	// 中文：仅可对 CDO（ClassDefaultObject）调用 —— 因为这个属性写到 default config 文件，影响所有该类的实例。
	// 编辑器下还会通过 UpdateSinglePropertyInConfigFile 持久化到 .ini。
	UE_API void SetShouldAutoRegisterWithGlobalList(const bool bAutoRegister);

	// 中文：取出 OwnedQueries 中至少有一个 archetype 匹配的所有 archetype。常用于 pruning 决策与 debug。
	UE_API void GetArchetypesMatchingOwnedQueries(const FMassEntityManager& EntityManager, TArray<FMassArchetypeHandle>& OutArchetype);
	UE_API bool DoesAnyArchetypeMatchOwnedQueries(const FMassEntityManager& EntityManager);
	int32 GetOwnedQueriesNum() const;
	
#if CPUPROFILERTRACE_ENABLED
	// 中文：CPU profiler 用的字符串 —— PostInitProperties 时设为 GetProcessorName()。
	FString StatId;
#endif
	
protected:
	/** Called to initialize the processor's internal state. Override to perform custom steps. */
	// 中文：子类自定义初始化（如订阅事件、读 settings）。在 ConfigureQueries 之后调用。
	UE_API virtual void InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager);

	/**
	 * Called internally during processor's initialization so that child classes configure their owned queries
	 * with requirements. This function is called before the processor gets considered by Mass dependency
	 * solver, and the requirement information stored in queries is crucial for that process. 
	 */
	// 中文：子类必须在此处用 query.AddRequirement<>() 等声明 query 需要的 fragment，并 RegisterQuery(query)。
	// 默认实现：若 AutoExecuteQuery 有值，调它的 ConfigureQuery；否则若有 OwnedQueries 但未配 requirement，报警告。
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager);

	UE_API virtual void PostInitProperties() override;

	/**
	 * Called during the processing phase to which this processor is registered.
	 * Default implementation requires that AutoExecuteQuery is populated with a QueryExecutor.
	 */
	// 中文：每帧执行入口（被 CallExecute 包了一层 active/requirements 检查）。
	// 子类要么 override 此函数，要么在子类构造里给 AutoExecuteQuery 赋一个有效的 FQueryExecutor。
	// 否则默认实现会 checkf 失败（"忘了配置任何执行内容"）。
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

protected:
	/** Configures when this given processor can be executed in relation to other processors and processing groups, within its processing phase. */
	// 中文：这是依赖求解器最重要的输入之一。EditDefaultsOnly 仅 CDO 可改 —— 因为是"类级别"配置。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	FMassProcessorExecutionOrder ExecutionOrder;

	/** Processing phase this processor will be automatically run as part of. Needs to be set before the processor gets
	 *  registered with MassProcessingPhaseManager, otherwise it will have no effect. This property is usually read via
	 *  a given class's CDO, so it's recommended to set it in the constructor. */
	// 中文：所属 phase（PrePhysics/StartPhysics/...DuringPhysics/EndPhysics/PostPhysics/FrameEnd）。
	// 必须在注册到 phase manager 之前设定（典型在构造函数内）。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	EMassProcessingPhase ProcessingPhase = EMassProcessingPhase::PrePhysics;

	/** Whether this processor should be executed on StandAlone or Server or Client */
	// 中文：NetMode 过滤。bitmask 形式（EProcessorExecutionFlags：Server|Client|Standalone|Editor）。
	// 默认在构造里设为 Server|Standalone（不在 Client/Editor 跑）。
	// 在编辑器中以 bitmask 编辑器展示。
	UPROPERTY(EditAnywhere, Category = "Pipeline", meta = (Bitmask, BitmaskEnum = "/Script/MassEntity.EProcessorExecutionFlags"), config)
	uint8 ExecutionFlags;

	/** Configures whether this processor should be automatically included in the global list of processors executed every tick (see ProcessingPhase and ExecutionOrder). */
	// 中文：true（默认）⇒ 每帧自动跑。false ⇒ 必须显式注册（典型为 "动态 processor"）。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	uint8 bAutoRegisterWithProcessingPhases : 1 = true;

	/** Meant as a class property, make sure to set it in subclass' constructor. Controls whether there can be multiple
	 *  instances of a given class in a single FMassRuntimePipeline and during dependency solving. */
	// 中文：默认 false（每类一份）。在子类构造函数中显式设为 true 才允许同类多实例。
	uint8 bAllowMultipleInstances : 1 = false;

	// 中文：true ⇒ 必须在 GameThread 跑（DispatchProcessorTasks 派发 GT task）。
	// 在 CallInitialize 中会被 OwnedQueries / ProcessorRequirements 自动 OR 上去（保证安全）。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	uint8 bRequiresGameThreadExecution : 1 = false;

#if WITH_EDITORONLY_DATA
	/** Used to permanently remove a given processor class from PipeSetting's listing. Used primarily for test-time 
	 *  processor classes, but can also be used by project-specific code to prune the processor list. */
	// 中文：仅编辑器数据 —— 控制是否在 MassEntitySettings UI 中显示该类。test-only / 内部 processor 通常关掉。
	UPROPERTY(config)
	uint8 bCanShowUpInSettings : 1 = true;
#endif // WITH_EDITORONLY_DATA

private:
	/**
	 * Gets set to true when an instance of the processor gets added to the phase processing as a "dynamic processor".
	 * Once set it's never expected to be cleared out to `false` thus the private visibility of the member variable.
	 * A "dynamic" processor is a one that has bAutoRegisterWithProcessingPhases == false, meaning it's not automatically
	 * added to the processing graph. Additionally, making processors dynamic allows one to have multiple instances
	 * of processors of the same class. 
	 * @see MarkAsDynamic()
	 * @see IsDynamic()
	 */
	// 中文：动态 processor 标志。一旦置 true 永不复原 —— 所以私有，避免外部误改。
	uint8 bIsDynamic : 1 = false;

	/** Used to track whether Initialized has been called. */
	// 中文：CallInitialize 完成后才 = true。InitializeInternal 内重复 init 没有保护，依赖此标记由 phase manager 保证。
	uint8 bInitialized : 1 = false;

	/**
	 * Processors can be activated/deactivated at runtime. Deactivating a running processor will not disrupt the processing
	 * graph since the disabled processor's dependencies will get passed down to the subsequent processors depending on this one.
	 * Deactivating processor's CDO will result in every instance starting off as disabled. Those will still be considered
	 * while building the processor dependency graph and one included in the processing graph will function just as the
	 * processor instances disabled at runtime (i.e. won't run, but pass down their dependencies).
	 * A special type of activation is "One Shot" mode - it works just like "Active" state, but it will auto-disable itself
	 * upon completion of the next CallExecute call.
	 */
	// 中文：见 EActivationState 注释。注意：禁用一个运行中的 processor 不会破坏 graph ——
	// 它的依赖会"穿透"传到下游（见 UMassCompositeProcessor::DispatchProcessorTasks 中的 AdditionalEvents 逻辑）。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	EActivationState ActivationState = EActivationState::Active;

protected:
	/**
	 * Denoted how important it is for this processor to be executed as soon as possible within a processing graph.
	 * The larger the number the higher the priority. It's used in two ways:
	 * - used when sorting nodes that otherwise seem similar in terms of "which processor to pick for execution next"
	 * - affects the priority of the dependencies - if this super-important processor is waiting for processor A and B,
	 *		then A and B become super important as well.
	 */
	// 中文：执行优先级，越大越优先。两种作用：
	//   1) 当多个 processor 在 topological sort 里"等价"时，按 priority 排序；
	//   2) 通过依赖反向传播 —— 若高优先级 processor 等 A、B 完成，则 A、B 也提升到同等优先级。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	int16 ExecutionPriority = 0;

	/**
	 * Determines whether given processor wants to be pruned from the execution graph when there are
	 * no archetypes matching its requirements.
	 * Defaults to EMassQueryBasedPruning::Prune, @see EMassQueryBasedPruning
	 */
	// 中文：见 EMassQueryBasedPruning 枚举注释。默认 Prune。
	EMassQueryBasedPruning QueryBasedPruning = EMassQueryBasedPruning::Default;

	friend UMassCompositeProcessor;
	friend FMassDebugger;

	/** A query representing elements this processor is accessing in Execute function outside of query execution */
	// 中文：本 processor 在 Execute 中"额外"访问的子系统/世界级别需求（不通过 query 走的部分）。
	// 例如：直接调某 WorldSubsystem。CallExecute 中会 CacheSubsystemRequirements 校验，不满足即跳过 Execute。
	FMassSubsystemRequirements ProcessorRequirements;

	/** A QueryExecutor that can optionally be run in lieu of overriding the Execute function. */
	// 中文：声明模式入口。子类构造时 `AutoExecuteQuery = MakeShared<MyExecutor>();` 即可享受默认 Execute。
	// FQueryExecutor 是数据驱动 query 执行的封装。
	TSharedPtr<UE::Mass::FQueryExecutor> AutoExecuteQuery;

private:
	/** Stores processor's queries registered via RegisterQuery. 
	 *  @note that it's safe to store pointers here since RegisterQuery does verify that a given registered query is 
	 *  a member variable of a given processor */
	// 中文：拥有的 query 指针列表。RegisterQuery 用地址范围检查保证这些指针指向 *本 processor 的成员*，
	// 因此与 processor 同生命周期，裸指针安全。
	TArray<FMassEntityQuery*> OwnedQueries;

#if WITH_MASSENTITY_DEBUG
	// 中文：CallInitialize 时拼装 "ProcessorName (NetMode)" 调试串，用于 trace / log。
	FString DebugDescription;
#endif

	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
public:
	// 中文：已弃用 —— 5.6 起请用 InitializeInternal(UObject&, const TSharedRef<FMassEntityManager>&)。
	// 通过 UE::Mass::Utils::GetEntityManager(World) 拿 manager 后转调新接口。
	UE_DEPRECATED(5.6, "Initialize is deprecated. Override InitializeInternal(UObject&, const TSharedRef<FMassEntityManager>&) instead. If you want to call the function, use CallInitialize.")
	UE_API virtual void Initialize(UObject& Owner) final;
	// 中文：已弃用 —— 5.6 起请用 ConfigureQueries(EntityManager)。`final` 阻止子类继续 override 老签名。
	UE_DEPRECATED(5.6, "This flavor of ConfigureQueries is deprecated. Override ConfigureQueries(const TSharedRef<FMassEntityManager>&) instead.")
	virtual void ConfigureQueries() final {};
};


//===========================================================================
// 【中文整体说明】UMassCompositeProcessor —— "容器型" processor
//---------------------------------------------------------------------------
// 角色：把多个 processor 组织成一个"组(group)"，并按依赖顺序协调执行。
// 它本身也是 UMassProcessor，可以嵌套（multi-level group: "AI.Locomotion.Steering"）。
//
// 关键容器：
//   - ChildPipeline (FMassRuntimePipeline) : 持有 child processor 实例。VisibleAnywhere 只读地反射在编辑器中。
//   - FlatProcessingGraph                  : *扁平化* 的依赖排序结果（拓扑序 + 每个节点的 dependency 索引）。
//                                            注意它是 TArray<FDependencyNode> 而非"图"——已展平为线性可执行序列。
//   - CompletionStatus[]                   : 与 FlatProcessingGraph 同大小，记录每个节点的 task 完成事件/状态。
//
// 执行模型：
//   单线程 (MASS_DO_PARALLEL=0): 直接 for-each child Execute()。
//   多线程: BuildFlatProcessingGraph 把 sorted processors 转成依赖索引数组；
//           DispatchProcessorTasks 按节点顺序派发 TaskGraph 任务，每个任务 Prerequisites = Dependencies 对应的 events。
//           最后用一个 CompletionEvent（空函数 task）汇聚所有任务完成。
//
// "禁用 processor"穿透依赖：DispatchProcessorTasks 对 inactive processor 不派发任务，
// 但把它原本的 Prerequisites 累积到 AdditionalEvents[NodeIndex] —— 下游若依赖该 inactive 节点，
// 会自动改依赖那个累积事件集合，确保隐式依赖不丢。
//===========================================================================
UCLASS(MinimalAPI)
class UMassCompositeProcessor : public UMassProcessor
{
	GENERATED_BODY()

	friend FMassDebugger;
public:
	// 中文：扁平依赖图节点。
	//   Name        : processor 名（用作 NameToDependencyIndex 索引键）；
	//   Processor   : 指向 child processor（不应为空，group 节点已被展开）；
	//   Dependencies: 本节点依赖的其他节点的"在 FlatProcessingGraph 中的下标"列表。下标必须 < 当前节点下标（拓扑序）。
	//   SequenceIndex: 仅 debug 用 —— 表示在 group 嵌套树中的层级，用于美化 log 缩进。
	struct FDependencyNode
	{
		FName Name;
		UMassProcessor* Processor = nullptr;
		TArray<int32> Dependencies;
#if WITH_MASSENTITY_DEBUG
		int32 SequenceIndex = INDEX_NONE;
#endif // WITH_MASSENTITY_DEBUG
	};

public:
	UE_API UMassCompositeProcessor();

	// 中文：直接设置 child processor 列表（覆盖式）。两个重载分别用 view 与 move array。
	UE_API void SetChildProcessors(TArrayView<UMassProcessor*> InProcessors);
	UE_API void SetChildProcessors(TArray<TObjectPtr<UMassProcessor>>&& InProcessors);

	UE_API virtual void InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& InEntityManager) override;
	UE_API virtual void DebugOutputDescription(FOutputDevice& Ar, int32 Indent = 0) const override;
	// 中文：override —— 还会递归把所有 child 的 ProcessingPhase 一起改。
	UE_API virtual void SetProcessingPhase(EMassProcessingPhase Phase) override;

	UE_API void SetGroupName(FName NewName);
	FName GetGroupName() const;

	/** 
	 * @brief 设置子 processor 集合，并完成依赖求解 + 扁平化图构建。
	 * 流程：
	 *   1) FMassProcessorDependencySolver(InProcessorInstances).ResolveDependencies(...) → 拓扑排序 SortedProcessors；
	 *   2) UpdateProcessorsCollection 把 sorted 复用进 ChildPipeline（保留状态）；
	 *   3) 若非单线程，BuildFlatProcessingGraph 把 sorted 转成 FlatProcessingGraph（多线程派发用）。
	 */
	UE_API virtual void SetProcessors(TArrayView<UMassProcessor*> InProcessorInstances, const TSharedPtr<FMassEntityManager>& EntityManager = nullptr);

	/** 
	 * Builds flat processing graph that's being used for multithreading execution of hosted processors.
	 */
	UE_API virtual void BuildFlatProcessingGraph(TConstArrayView<FMassProcessorOrderInfo> SortedProcessors);

	/**
	 * Adds processors in InOutOrderedProcessors to ChildPipeline. 
	 * Note that this operation is non-destructive for the existing processors - the ones of classes found in InOutOrderedProcessors 
	 * will be retained and used instead of the instances provided via InOutOrderedProcessors. Respective entries in InOutOrderedProcessors
	 * will be updated to reflect the reuse.
	 * The described behavior however is available only for processors with bAllowMultipleInstances == false.
	 */
	// 中文：把排序结果"灌入" ChildPipeline，但尽量复用现有实例（保留状态，如 signaling processor 内部累积状态）。
	// 仅对 bAllowMultipleInstances==false 的类型做复用查找。
	// 还会按 NetMode (WorldExecutionFlags) 过滤掉不该跑的 processor。
	UE_API void UpdateProcessorsCollection(TArrayView<FMassProcessorOrderInfo> InOutOrderedProcessors, EProcessorExecutionFlags InWorldExecutionFlags = EProcessorExecutionFlags::None);

	/** adds SubProcessor to an appropriately named group. If RequestedGroupName == None then SubProcessor
	 *  will be added directly to ChildPipeline. If not then the indicated group will be searched for in ChildPipeline 
	 *  and if it's missing it will be created and AddGroupedProcessor will be called recursively */
	// 中文：递归地把 SubProcessor 添加到 RequestedGroupName 对应的（嵌套）group。
	// 若 group 不存在则 NewObject<UMassCompositeProcessor> 创建并加入 ChildPipeline。
	UE_API void AddGroupedProcessor(FName RequestedGroupName, UMassProcessor& SubProcessor);

	UE_API virtual FGraphEventRef DispatchProcessorTasks(const TSharedPtr<FMassEntityManager>& EntityManager, FMassExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites = FGraphEventArray()) override;

	bool IsEmpty() const;

	UE_API virtual FString GetProcessorName() const override;

	TConstArrayView<UMassProcessor*> GetChildProcessorsView() const;

protected:
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	/** RequestedGroupName can indicate a multi-level group name, like so: A.B.C
	 *  We need to extract the highest-level group name ('A' in the example), and see if it already exists. 
	 *  If not, create it. 
	 *  @param RequestedGroupName name of the group for which we want to find or create the processor.
	 *  @param OutRemainingGroupName contains the group name after cutting the high-level group. In the used example it
	 *    will contain "B.C". This value is then used to recursively create subgroups */
	// 中文：处理点号分层 group 名（"A.B.C" → 顶层 "A"，剩余 "B.C" 由 caller 递归继续解析）。
	UE_API UMassCompositeProcessor* FindOrAddGroupProcessor(FName RequestedGroupName, FString* OutRemainingGroupName = nullptr);

protected:
	// 中文：child processor 容器。VisibleAnywhere 让编辑器属性面板看到层级（只读，不能在面板里直接增删）。
	UPROPERTY(VisibleAnywhere, Category=Mass)
	FMassRuntimePipeline ChildPipeline;

	/** Group name that will be used when resolving processor dependencies and grouping */
	// 中文：本 composite 在依赖图中以这个名字被引用（通过 ExecuteInGroup="..."）。
	UPROPERTY()
	FName GroupName;

#if WITH_MASSENTITY_DEBUG
	// 中文：当前帧是否需要 dump 新生成的 graph。BuildFlatProcessingGraph 末尾根据 cvar 置位，
	// DispatchProcessorTasks 第一次执行后置回 false。
	bool bDebugLogNewProcessingGraph = false;
#endif // WITH_MASSENTITY_DEBUG

	// 中文：见类注释。下标顺序就是执行顺序（Dependencies 中的索引保证 < 当前下标）。
	TArray<FDependencyNode> FlatProcessingGraph;

	// 中文：每个 child processor 的完成事件包装。仅多线程模式下使用。
	struct FProcessorCompletion
	{
		FGraphEventRef CompletionEvent;
		EProcessorCompletionStatus Status = EProcessorCompletionStatus::Invalid;

		bool IsDone() const 
		{
			return Status == EProcessorCompletionStatus::Done || (CompletionEvent.IsValid() && CompletionEvent->IsComplete());
		}

		void Wait()
		{
			if (CompletionEvent.IsValid())
			{
				CompletionEvent->Wait();
			}
		}
	};
	TArray<FProcessorCompletion> CompletionStatus;

	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
public:

	/*UE_DEPRECATED(5.6, "This flavor of Initialize is deprecated. Please use the one requiring a FMassEntityManager parameter")
	virtual void Initialize(UObject& Owner) final;*/

	// 中文：5.6 起的 deprecated 重载，转调 TArrayView 版本。
	UE_DEPRECATED(5.6, "This flavor of SetChildProcessors is deprecated. Please use one of the others.")
	UE_API void SetChildProcessors(TArray<UMassProcessor*>&& InProcessors);
};


//-----------------------------------------------------------------------------
// UMassProcessor inlines
//-----------------------------------------------------------------------------
inline bool UMassProcessor::IsInitialized() const
{
	return bInitialized;
}

// 中文：把 uint8 ExecutionFlags 转回 enum 类型供 ShouldExecute 用。
inline EProcessorExecutionFlags UMassProcessor::GetExecutionFlags() const
{
	return static_cast<EProcessorExecutionFlags>(ExecutionFlags);
}

// 中文：bitmask AND —— 只要 NetMode bit 与本 processor 的允许位有交集就执行。
inline bool UMassProcessor::ShouldExecute(const EProcessorExecutionFlags CurrentExecutionFlags) const
{
	return (GetExecutionFlags() & CurrentExecutionFlags) != EProcessorExecutionFlags::None;
}

inline bool UMassProcessor::ShouldAllowMultipleInstances() const
{
	return bAllowMultipleInstances;
}

inline int32 UMassProcessor::GetOwnedQueriesNum() const
{
	return OwnedQueries.Num();
}

inline void UMassProcessor::DebugOutputDescription(FOutputDevice& Ar) const
{
	DebugOutputDescription(Ar, 0);
}

inline bool UMassProcessor::DoesRequireGameThreadExecution() const
{
	return bRequiresGameThreadExecution;
}
	
inline const FMassProcessorExecutionOrder& UMassProcessor::GetExecutionOrder() const
{
	return ExecutionOrder;
}

inline const FMassSubsystemRequirements& UMassProcessor::GetProcessorRequirements() const
{
	return ProcessorRequirements;
}

inline int16 UMassProcessor::GetExecutionPriority() const
{
	return ExecutionPriority; 
}

inline void UMassProcessor::SetExecutionPriority(const int16 NewExecutionPriority)
{
	ExecutionPriority = NewExecutionPriority; 
}

// 中文：标记该实例为动态。一旦标记永不撤回。
inline void UMassProcessor::MarkAsDynamic()
{
	bIsDynamic = true;
}

inline bool UMassProcessor::IsDynamic() const
{
	return bIsDynamic != 0;
}

// 中文：注意 Active/OneShot/Inactive 是即时切换；CallExecute 中读它判断要不要执行。
// 在正在 processing 中改不会立即生效（依赖 phase manager 下一帧再读）。
inline void UMassProcessor::MakeActive()
{
	ActivationState = EActivationState::Active;
}

inline void UMassProcessor::MakeOneShot()
{
	ActivationState = EActivationState::OneShot;
}

inline void UMassProcessor::MakeInactive()
{
	ActivationState = EActivationState::Inactive;
}

// 中文：IsActive 在 OneShot 状态下也返回 true（因为该状态下还会跑一次）。
inline bool UMassProcessor::IsActive() const
{
	return ActivationState != EActivationState::Inactive;
}

inline bool UMassProcessor::ShouldAutoAddToGlobalList() const
{
	return bAutoRegisterWithProcessingPhases;
}

#if WITH_EDITOR
// 中文：编辑器中的"是否在 settings UI 中显示" —— 自动注册的肯定显示，否则取 bCanShowUpInSettings。
inline bool UMassProcessor::ShouldShowUpInSettings() const
{
	return ShouldAutoAddToGlobalList() || bCanShowUpInSettings;
}
#endif // WITH_EDITOR

//-----------------------------------------------------------------------------
// UMassCompositeProcessor inlines
//-----------------------------------------------------------------------------
inline FName UMassCompositeProcessor::GetGroupName() const
{
	return GroupName;
}

inline bool UMassCompositeProcessor::IsEmpty() const
{
	return ChildPipeline.IsEmpty();
}

inline TConstArrayView<UMassProcessor*> UMassCompositeProcessor::GetChildProcessorsView() const
{
	return ChildPipeline.GetProcessors();
}

#undef UE_API 
