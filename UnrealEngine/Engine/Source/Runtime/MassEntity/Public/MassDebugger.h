// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// 【MassDebugger.h —— Mass 框架的调试与诊断横切层（Cross-cutting Debug Facade）】
//
// 一、定位：
//   FMassDebugger 是 Mass 框架的"上帝视角"——一个**纯静态**的接口面（facade），不持有实例，
//   通过与 FMassEntityManager / FMassEntityQuery / UMassProcessor / FMassArchetypeData 等的 friend
//   关系访问几乎所有内部状态。它的存在让 IDE 调试工具（Mass Debugger 编辑器面板、Gameplay Debugger 类目、
//   Insights Trace 等）可以在不污染发布接口的前提下"看穿"整个 ECS 内部。
//
// 二、为什么需要这一层：
//   传统 OOP 调试只需在某个对象上加断点即可；而 ECS 中：
//     1) 实体本身没有"对象"——它只是 Index+SerialNumber 二元组；
//     2) 数据散落在多个 archetype chunk 内，且会随 fragment 增删而**迁移 chunk**；
//     3) Processor 是按 query 批量遍历的，没有"调用某个 entity 方法"的概念；
//   因此必须建立一种"实体级断点"机制：当被选中的实体出现在 batch 中时触发断点，
//   或者在某个 Processor 准备写入某 fragment 时触发——这便是 FBreakpoint 的核心使命。
//
// 三、构建约束：
//   - 所有 debug 设施都被 `#if WITH_MASSENTITY_DEBUG` 包裹；该宏仅在 Development/Editor/Test 构建中开启，
//     **Shipping 构建里整个模块塌缩为空壳**：`FMassDebugger` 仅保留少量返回 "[no debug information]"
//     的存根函数；所有宏（MASS_BREAK_IF_ENTITY_DEBUGGED 等）退化为 no-op，零运行时开销。
//
// 四、关键概念：
//   - FEnvironment：每个 EntityManager 一份调试环境（multi-world 时可能有多个），保存
//                   selected/highlighted entity、breakpoints 列表、processor data providers。
//   - SelectedEntity：通过 GameplayDebugger 拾取或控制台命令 `mass.debug.DebugEntity` 选中的"焦点实体"，
//                    所有 MASS_BREAK_IF_ENTITY_DEBUGGED 宏会在该实体路过时触发断点。
//   - HighlightedEntity：UI 视图层的"高亮"，仅供 debugger UI 跨视图同步显示，不触发 break。
//   - 断点（FBreakpoint）：可在"processor 执行""fragment 写入""entity 创建/销毁"等事件上设置，
//                          命中时调用 `UE_DEBUG_BREAK()` 把 IDE 停在调用栈处。
//
// 五、委托机制：
//   FMassDebugger 暴露一组静态多播委托（OnEntityManagerInitialized / OnEntitySelectedDelegate /
//   OnBreakpointsChangedDelegate / OnDebugEvent 等），Mass Debugger 编辑器 UI 监听这些委托
//   动态刷新视图。
// =====================================================================================================================

#pragma once

#include "MassProcessingTypes.h"
#include "MassEntityTrace.h"
// 【关键预处理屏障】WITH_MASSENTITY_DEBUG：仅在非 Shipping 构建中定义为 1。
// 下面这一大段头文件依赖、类型定义、宏全部依赖于该开关：
//   - 启用：完整的 debugger API、断点、内省接口；
//   - 关闭（Shipping）：仅保留文件末尾的极小存根 struct，其余全部消失，编译期零成本。
#if WITH_MASSENTITY_DEBUG
#include "Containers/ContainersFwd.h"
#include "MassDebuggerBreakpoints.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "Async/TransactionallySafeMutex.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/ObjectKey.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "StructUtils/InstancedStruct.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6

class FOutputDevice;
class FStructOnScope;
class UMassProcessor;
struct FMassEntityQuery;
struct FMassEntityManager;
struct FMassArchetypeHandle;
struct FMassArchetypeChunk;
struct FMassFragmentRequirements;
struct FMassFragmentRequirementDescription;
enum class EMassFragmentAccess : uint8;
enum class EMassFragmentPresence : uint8;
#endif // WITH_MASSENTITY_DEBUG
#include "MassDebugger.generated.h"

namespace UE::Mass::Debug
{
	/**
	 * 【FArchetypeStats —— archetype 内存与占用统计快照】
	 * 由 FMassDebugger::GetArchetypeEntityStats 填充，给 Mass Debugger UI 显示
	 * "某 archetype 占多少内存、有多少实体、chunk 利用率多少"。
	 *
	 * 关键派生指标：
	 *   - 利用率 = EntitiesCount / (ChunksCount * EntitiesCountPerChunk)
	 *   - 浪费率 = WastedEntityMemory / AllocatedSize
	 * 利用率低意味着 chunk 内存碎片化（许多 chunk 只装了几个实体），可能是
	 * archetype 设计粒度过细的信号。
	 */
	struct FArchetypeStats
	{
		/** Number of active entities of the archetype. */
		/** 该 archetype 当前驻留的有效实体数。 */
		int32 EntitiesCount = 0;
		/** Number of entities that fit per chunk. */
		/** 每个 chunk 可容纳的实体上限（由 fragment 总尺寸与固定 chunk 内存预算反推）。 */
		int32 EntitiesCountPerChunk = 0;
		/** Number of allocated chunks. */
		/** 已分配的 chunk 数量（包括未填满的）。 */
		int32 ChunksCount = 0;
		/** Total amount of memory taken by this archetype */
		/** 整个 archetype 占用的总字节数（含未使用的 chunk 尾部）。 */
		SIZE_T AllocatedSize = 0;
		/** How much memory allocated for entities is being unused */
		/** 已分配但未被实体占用的内存（chunk 尾部空槽位之和）；衡量 chunk 利用率的核心指标。 */
		SIZE_T WastedEntityMemory = 0;
		/** Total amount of memory needed by a single entity */
		/** 单个实体（在该 archetype 下）的复合 fragment 总尺寸。 */
		SIZE_T BytesPerEntity = 0;
	};

	/**
	 * Processor 数据提供者函数原型——某些子系统（如 MassGameplay 模块）拥有
	 * 临时/动态创建的 processor，并不直接归属任何 EntityManager；通过
	 * FMassDebugger::RegisterProcessorDataProvider 注册回调，让 Debugger UI
	 * 在拉取 processor 列表时也能枚举到这些"游离" processor。
	 */
	using FProcessorProviderFunction = TFunction<void(TArray<const UMassProcessor*>&)>;
} // namespace UE::Mass::Debug

/**
 * 【FMassGenericDebugEvent —— 通用调试事件载荷的占位 USTRUCT】
 *
 * 通过 FMassDebugger::DebugEvent<TMessage>(...) 发出的调试事件以 FConstStructView 形式承载，
 * 各业务模块定义自己派生自此结构（或直接构造）的 USTRUCT 来描述具体事件类型。
 * 这种"开放式 struct 总线"设计允许任意模块定义新事件类型而不用修改 Debugger 核心。
 *
 * 注意 Context 字段仅在 WITH_EDITORONLY_DATA 下存在，且不是 UPROPERTY——事件被设计为
 * 即时广播即时消费，不应当被序列化或存储。
 */
USTRUCT()
struct FMassGenericDebugEvent
{
	GENERATED_BODY()
	explicit FMassGenericDebugEvent(const UObject* InContext = nullptr)
#if WITH_EDITORONLY_DATA
		: Context(InContext)
#endif // WITH_EDITORONLY_DATA
	{
	}

#if WITH_EDITORONLY_DATA
	// note that it's not a uproperty since these events are only intended to be used instantly, never stored
	const UObject* Context = nullptr;
#endif // WITH_EDITORONLY_DATA
};

#if WITH_MASSENTITY_DEBUG

namespace UE::Mass::Debug
{
	// ----- 三个全局可调控制台变量（CVar），通过 mass.debug.* 控制台命令开关 -----
	/** 是否允许 MASS_SET_ENTITY_DEBUGGED 宏在代码运行路径上自动选中实体作为调试目标。
	 *  默认关闭——开启后，被该宏埋点的代码路径每经过一个实体就会"抢占"焦点，
	 *  适合用来追踪"某行代码到底处理过哪些实体"，但会与手动选择互相冲突。 */
	extern MASSENTITY_API bool bAllowProceduralDebuggedEntitySelection;
	/** 是否允许 MASS_BREAK_IF_ENTITY_DEBUGGED 宏触发 PLATFORM_BREAK()。
	 *  即使选中了实体，若该开关关闭，宏不会真正断到 IDE，避免误触。 */
	extern MASSENTITY_API bool bAllowBreakOnDebuggedEntity;
	/** 是否在 SelectEntity 时把所有 processor query 拿来跟该实体对照，
	 *  生成"哪些 query 不匹配 / 为何不匹配"的诊断信息——用于 Mass Debugger UI 的
	 *  "Why isn't my entity being processed?" 视图。 */
	extern MASSENTITY_API bool bTestSelectedEntityAgainstProcessorQueries;

	/** 遍历 archetype 的回调函数原型；FMassDebugger::ForEachArchetype 使用。 */
	using FArchetypeFunction = TFunction<void(FMassArchetypeHandle)>;
} // namespace UE::Mass::Debug

// ===== 实体调试宏 ====================================================================
// 这四个宏是 Mass 代码中"实体级断点"的埋点接口，被 EntityManager / Processor 等
// 关键代码路径调用。Shipping 构建中（见文件末尾的 #else 分支）全部退化为 no-op。
//
// 使用模式（典型）：
//   void FMassEntityManager::AddFragmentToEntity(...)
//   {
//       MASS_BREAK_IF_ENTITY_DEBUGGED(*this, EntityHandle);   // 若该实体被选中则停在这里
//       ...实际逻辑...
//   }
// =====================================================================================

/** 表达式宏：当前 EntityHandle 是否就是用户选中的调试实体。 */
#define MASS_IF_ENTITY_DEBUGGED(Manager, EntityHandle) (FMassDebugger::GetSelectedEntity(Manager) == EntityHandle)
/** 若该实体是被选中的调试实体且开关 bAllowBreakOnDebuggedEntity=true，则触发平台断点（IDE 停下）。 */
#define MASS_BREAK_IF_ENTITY_DEBUGGED(Manager, EntityHandle) { if (UE::Mass::Debug::bAllowBreakOnDebuggedEntity && MASS_IF_ENTITY_DEBUGGED(Manager, EntityHandle)) { PLATFORM_BREAK();} }
/** 按裸 Index 触发断点的版本——用于一些尚未拥有完整 EntityManager 上下文、
 *  但只想"撞到第 N 号实体就停"的低层路径。 */
#define MASS_BREAK_IF_ENTITY_INDEX(EntityHandle, InIndex) { if (UE::Mass::Debug::bAllowBreakOnDebuggedEntity && EntityHandle.Index == InIndex) { PLATFORM_BREAK();} }
/** 程序化选中：把当前路径触及的实体设为"被调试实体"。当 bAllowProceduralDebuggedEntitySelection 启用时生效。 */
#define MASS_SET_ENTITY_DEBUGGED(Manager, EntityHandle) { if (UE::Mass::Debug::bAllowProceduralDebuggedEntitySelection) {FMassDebugger::SelectEntity(Manager, EntityHandle); }}

/**
 * 【EMassDebugMessageSeverity —— 调试消息严重级】
 * 与 EMessageSeverity 的语义对齐，但额外引入 Default：表示"沿用调用现场的默认严重级"，
 * 让 DebugEvent 调用者既能强制覆盖严重级、也能跟随原始日志语义。
 * MAX 设计为 alias 至 Default 是个小技巧，使得 ConversionMap[] 数组只需覆盖前三项。
 */
enum class EMassDebugMessageSeverity : uint8
{
	Error,
	Warning,
	Info,
	// the following two need to remain last
	Default,
	MAX = Default
};

namespace UE::Mass::Debug
{
	/**
	 * 【FQueryRequirementsView —— FMassEntityQuery 内部状态的"只读快照视图"】
	 *
	 * Query 内部的 fragment/tag/subsystem 需求字段都是 private/protected，无法直接被
	 * Debugger UI 读取；FMassDebugger 通过 friend 访问后封装成本结构返回。
	 *
	 * 注意所有数组成员都是 TConstArrayView——零拷贝，仅在 Query 仍存活时安全。
	 * 这里的"View"语义意味着调用方不能保留视图越过 Query 生命周期。
	 */
	struct FQueryRequirementsView
	{
		TConstArrayView<FMassFragmentRequirementDescription> FragmentRequirements;       //< 普通 fragment 需求
		TConstArrayView<FMassFragmentRequirementDescription> ChunkRequirements;          //< chunk fragment 需求
		TConstArrayView<FMassFragmentRequirementDescription> ConstSharedRequirements;    //< 只读 shared fragment 需求
		TConstArrayView<FMassFragmentRequirementDescription> SharedRequirements;         //< 可写 shared fragment 需求
		const FMassTagBitSet& RequiredAllTags;                                           //< AllOf 标签集合
		const FMassTagBitSet& RequiredAnyTags;                                           //< AnyOf 标签集合
		const FMassTagBitSet& RequiredNoneTags;                                          //< NoneOf 标签集合
		const FMassTagBitSet& RequiredOptionalTags;                                      //< 可选标签集合
		const FMassExternalSubsystemBitSet& RequiredConstSubsystems;                     //< 只读 subsystem 依赖
		const FMassExternalSubsystemBitSet& RequiredMutableSubsystems;                   //< 可写 subsystem 依赖
	};

	/** 把 EMassFragmentAccess 转成日志友好的简短字符串（"--"/"RO"/"RW"）。 */
	FString DebugGetFragmentAccessString(EMassFragmentAccess Access);
	/** 把一组 processor 的描述（query、依赖等）批量打印到 OutputDevice。 */
	MASSENTITY_API extern void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar);

	/** 是否当前配置了任何调试实体范围（DebugEntityBegin/End != INDEX_NONE）。 */
	MASSENTITY_API extern bool HasDebugEntities();
	/** 是否调试范围只覆盖单个实体（Begin == End）。 */
	MASSENTITY_API extern bool IsDebuggingSingleEntity();

	/**
	 * Populates OutBegin and OutEnd with entity index ranges as set by mass.debug.SetDebugEntityRange or
	 * mass.debug.DebugEntity console commands.
	 * @return whether any range has been configured.
	 *
	 * 取出"被关注"的实体 Index 范围。该范围由控制台命令 `mass.debug.SetDebugEntityRange` 或
	 * `mass.debug.DebugEntity` 设置，用于过滤 GameplayDebugger 显示、可视化等。
	 */
	MASSENTITY_API extern bool GetDebugEntitiesRange(int32& OutBegin, int32& OutEnd);
	/** 判断指定实体是否落在调试范围内；可同时返回该实体的稳定调色板色值（按 Index 取色）。 */
	MASSENTITY_API extern bool IsDebuggingEntity(FMassEntityHandle Entity, FColor* OutEntityColor = nullptr);
	/** 获取实体的稳定调试色——同一 Index 永远是同一颜色，便于在轨迹图上区分实体。 */
	MASSENTITY_API extern FColor GetEntityDebugColor(FMassEntityHandle Entity);

	/**
	 * 把 Mass 调试严重级映射回 UE 通用消息严重级；当传入 Default 时返回原始严重级（透传）。
	 */
	inline EMessageSeverity::Type MassSeverityToMessageSeverity(EMessageSeverity::Type OriginalSeverity, EMassDebugMessageSeverity MassSeverity)
	{
		static constexpr EMessageSeverity::Type ConversionMap[int(EMassDebugMessageSeverity::MAX)] =
		{
			/*EMassDebugMessageSeverity::Error=*/EMessageSeverity::Error,
			/*EMassDebugMessageSeverity::Warning=*/EMessageSeverity::Warning,
			/*EMassDebugMessageSeverity::Info=*/EMessageSeverity::Info
		};
		return MassSeverity == EMassDebugMessageSeverity::Default 
			? OriginalSeverity
			: ConversionMap[int(MassSeverity)];
	}
} // namespace UE::Mass::Debug

/**
 * 【FMassDebugger —— 静态调试 facade，是整个 debug 子系统的核心枢纽】
 *
 * 设计要点：
 *   1) 全部接口都是 static——没有实例。原因是要在 EntityManager 之外、跨进程范围
 *      内统一管理多个 EntityManager 的调试状态（编辑器中可能同时运行多个 PIE world）。
 *   2) 通过 friend 关系（在各 Mass 内部类型中声明 `friend struct FMassDebugger;`）
 *      访问 OwnedQueries、FragmentHashToArchetypeMap、Chunks 等私有成员，
 *      避免把这些"仅调试可见"的细节泄漏到公开的 getter 中污染发布 API。
 *   3) 每个 EntityManager 对应一个 FEnvironment 条目，由 RegisterEntityManager
 *      在 EntityManager 初始化时创建、UnregisterEntityManager 在销毁时移除。
 *   4) 静态成员（ActiveEnvironments / 委托们）的生命周期是模块级——靠
 *      ShutdownDebugger 显式清理；TODO 注释明确表示未来希望迁移到 instanced struct
 *      并跟随模块 startup/shutdown 一起管理。
 */
struct FMassDebugger
{
	/**
	 * 【FEnvironment —— 每个 EntityManager 对应一份调试上下文】
	 *
	 * 包含三类状态：
	 *   A. 焦点状态：SelectedEntity（被调试实体）、HighlightedEntity（被高亮实体）
	 *   B. 断点状态：Breakpoints 数组 + 两个加速 Set（ProcessorsWithBreakpoints / FragmentsWithBreakpoints）
	 *      用作"是否需要进入断点检查慢路径"的快速 short-circuit；
	 *   C. 数据提供者：ProcessorProviders 让外部模块注入额外 processor 列表。
	 *
	 * EntityManager 用 weak ptr 持有：debugger 必须容许 EntityManager 先于
	 * Environment 销毁（如热重载、PIE 退出），不能强引用。
	 */
	struct FEnvironment
	{
		TWeakPtr<const FMassEntityManager> EntityManager;          //< const 视图（读取用）
		TWeakPtr<FMassEntityManager> MutableEntityManager;         //< 可写视图（命令注入等少数操作用）
		TMap<FName, UE::Mass::Debug::FProcessorProviderFunction> ProcessorProviders;  //< 外部 processor 提供器（按名字键）
		FMassEntityHandle SelectedEntity;                          //< 被选中的实体（debug 焦点）
		FMassEntityHandle HighlightedEntity;                       //< UI 层高亮实体（不触发断点）

		/** 该 Environment 是否注册了任何断点；break 检查慢路径的第一道门。
		 *  只要本 Env 没断点，所有断点检测都直接 return false。 */
		bool bHasBreakpoint = false;

		/** 该 EntityManager 拥有的所有断点对象（按添加顺序）。 */
		TArray<UE::Mass::Debug::FBreakpoint> Breakpoints;

		/** 按 BreakpointHandle 反查具体 FBreakpoint 对象（线性查找）。 */
		MASSENTITY_API UE::Mass::Debug::FBreakpoint* FindBreakpointByHandle(UE::Mass::Debug::FBreakpointHandle Handle);

		/** quick lookup to skip processors and fragments with no breakpoints set */
		/** 加速集合：缓存"有断点关联的 processor / fragment"，让 ShouldProcessorBreak
		 *  等热路径只需 O(1) 集合查询即可短路掉绝大多数无断点情况。
		 *  这两个集合由 RefreshBreakpoints 重建。 */
		TSet<TObjectKey<const UMassProcessor>> ProcessorsWithBreakpoints;
		TSet<TObjectKey<const UScriptStruct>> FragmentsWithBreakpoints;

#if UE_MASS_TRACE_ENABLED
		/** Trace 系统启动时的回调句柄——当 Insights 开始 trace 时，
		 *  自动把现有 archetype 全部 emit 一次"创建事件"，让 trace 时间轴上有完整初始状态。 */
		FDelegateHandle TraceStartedDelegateHandle;
#endif

		explicit FEnvironment(FMassEntityManager& InEntityManager);
		~FEnvironment();

		/** EntityManager weak 引用是否仍然有效。 */
		bool IsValid() const { return EntityManager.IsValid(); }

		/** 清空所有断点（不广播 OnBreakpointsChanged，由调用方决定）。 */
		void ClearBreakpoints();
	};

	// ===== 全局多播委托 ===============================================================
	// DECLARE_TS_MULTICAST_DELEGATE = ThreadSafe MulticastDelegate；可跨线程订阅。
	// 这些委托是 Mass Debugger UI 与运行时之间的事件总线。
	// =================================================================================

	/** 断点列表发生变更（增/删/启用状态变更等）。Debugger UI 据此刷新断点视图。 */
	DECLARE_TS_MULTICAST_DELEGATE(FOnBreakpointsChanged);
	/** 用户选中了某个实体（手动选择 / 控制台命令 / 程序化选择）。参数：所属 EntityManager、实体句柄。 */
	DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FOnEntitySelected, const FMassEntityManager&, const FMassEntityHandle);
	/** EntityManager 初始化/反初始化通用事件。 */
	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnMassEntityManagerEvent, const FMassEntityManager&);
	/** Environment 级事件（如 ProcessorProvider 注册）。 */
	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnEnvironmentEvent, const FEnvironment&);
	/** 通用调试事件（一种轻量"日志总线"），参数：EventName、负载 struct、严重级覆盖。 */
	DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FOnDebugEvent, const FName /*EventName*/, FConstStructView /*Payload*/, const EMassDebugMessageSeverity /*SeverityOverride*/);

	// ===== Processor / Query 内省 API =================================================

	/** 返回 Processor 内部维护的 query 列表（OwnedQueries 是 protected，靠 friend 访问）。 */
	static MASSENTITY_API TConstArrayView<FMassEntityQuery*> GetProcessorQueries(const UMassProcessor& Processor);
	/** fetches all queries registered for given Processor. Note that in order to get up to date information
	 *  FMassEntityQuery::CacheArchetypes will be called on each query
	 *
	 *  类似 GetProcessorQueries，但在返回前对每个 query 调用 CacheArchetypes() 强制刷新——
	 *  代价是会触发昂贵的 archetype 匹配重算。Debugger UI 中"显示该 query 当前匹配的 archetype 列表"
	 *  视图调用此版本以保证数据是最新的。 */
	static MASSENTITY_API TConstArrayView<FMassEntityQuery*> GetUpToDateProcessorQueries(const FMassEntityManager& EntityManager, UMassProcessor& Processor);

	/** 把 Query 内部所有需求字段打包成 FQueryRequirementsView 返回（零拷贝视图）。 */
	static MASSENTITY_API UE::Mass::Debug::FQueryRequirementsView GetQueryRequirements(const FMassEntityQuery& Query);
	/** 把 Query 的执行需求（subsystem 读写、threading 模型等）导出到 FMassExecutionRequirements。 */
	static MASSENTITY_API void GetQueryExecutionRequirements(const FMassEntityQuery& Query, FMassExecutionRequirements& OutExecutionRequirements);
	/** 列举当前能匹配该 Query 的所有实体——遍历所有 archetype，把命中的 archetype 内的实体全部收集。
	 *  注意：返回的是快照，调用后实体集合可能变化。Debugger UI 用作"该 Query 现在能处理多少实体"。 */
	static MASSENTITY_API TArray<FMassEntityHandle> GetEntitiesMatchingQuery(const FMassEntityManager& EntityManager, const FMassEntityQuery& Query);

	// ===== Archetype 内省 API =========================================================

	/** 对该 EntityManager 下每个 archetype 调用回调。 */
	static MASSENTITY_API void ForEachArchetype(const FMassEntityManager& EntityManager, const UE::Mass::Debug::FArchetypeFunction& Function);
	/** 返回该 EntityManager 下所有 archetype 句柄的快照数组。 */
	static MASSENTITY_API TArray<FMassArchetypeHandle> GetAllArchetypes(const FMassEntityManager& EntityManager);
	/** 返回 archetype 的组合描述（fragment + tag + chunk fragment + shared fragment 全集）。 */
	static MASSENTITY_API const FMassArchetypeCompositionDescriptor& GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle);

	/** 给 Insights/Trace 子系统使用：把 archetype 的内存地址作为稳定 ID（trace 期间不会重定位）。 */
	static MASSENTITY_API uint64 GetArchetypeTraceID(const FMassArchetypeData& ArchetypeData);
	static MASSENTITY_API uint64 GetArchetypeTraceID(const FMassArchetypeHandle& ArchetypeHandle);
	
	/** 返回某 chunk 中所有实体句柄的视图——给"显示某 chunk 内有哪些实体"视图用。 */
	static MASSENTITY_API TConstArrayView<FMassEntityHandle> GetEntitiesViewOfArchetype(
		const FMassArchetypeData& ArchetypeData,
		const FMassArchetypeChunk& Chunk);

	/** 句柄→数据指针的解引用（不存在则返回 nullptr）。 */
	static MASSENTITY_API const FMassArchetypeData* GetArchetypeData(const FMassArchetypeHandle& ArchetypeHandle);
	/** 遍历某 archetype 的所有 chunk，给 chunk 级可视化视图使用。 */
	static MASSENTITY_API void EnumerateChunks(const FMassArchetypeData& Archetype, TFunctionRef<void(const FMassArchetypeChunk&)> Fn);

	/** 计算 archetype 内存与占用统计——参见 FArchetypeStats 注释。 */
	static MASSENTITY_API void GetArchetypeEntityStats(const FMassArchetypeHandle & ArchetypeHandle, UE::Mass::Debug::FArchetypeStats & OutStats);
	/** 返回 archetype 上注册的调试名（多个，因为同一 archetype 可能由多个不同语义"特征"产出）。 */
	static MASSENTITY_API const TConstArrayView<FName> GetArchetypeDebugNames(const FMassArchetypeHandle& ArchetypeHandle);
	/** 收集 archetype 内的所有实体（遍历每个 chunk 的实体数组拼接）。 */
	static MASSENTITY_API TArray<FMassEntityHandle> GetEntitiesOfArchetype(const FMassArchetypeHandle& ArchetypeHandle);

	// ===== Composite Processor 依赖图内省 ============================================

	/** 拿到 composite processor 内部的扁平依赖图（已拓扑排序）。 */
	static MASSENTITY_API TConstArrayView<UMassCompositeProcessor::FDependencyNode> GetProcessingGraph(const UMassCompositeProcessor& GraphOwner);
	/** 拿到 composite processor 实际持有的子 processor 数组。 */
	static MASSENTITY_API TConstArrayView<TObjectPtr<UMassProcessor>> GetHostedProcessors(const UMassCompositeProcessor& GraphOwner);
	
	// ===== Requirement 字符串化 ======================================================
	// 这些函数把 Mass 的需求体系（fragment requirement、tag bitset 等）转成人类可读字符串，
	// 给日志、报错信息、Debugger UI 显示用。

	/** 单条 requirement 的简短描述（如 "+FFooFragment[RW]"）。
	 *  前缀字符语义：+ = AllOf, - = NoneOf, ? = Optional. */
	static MASSENTITY_API FString GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement);
	/** 整个 requirement 集合的"<...>"列表化描述。 */
	static MASSENTITY_API FString GetRequirementsDescription(const FMassFragmentRequirements& Requirements);
	/** 解释为什么 archetype 不满足 requirement——返回多行字符串列出缺失的 fragment / tag。
	 *  这是"为什么我的实体没有被这个 query 处理"问题的核心诊断 API。 */
	static MASSENTITY_API FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle);
	static MASSENTITY_API FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeCompositionDescriptor& ArchetypeComposition);

	// ===== 描述输出（控制台命令的实现） =============================================

	/** 把 archetype 描述打印到 OutputDevice（用于 mass.LogArchetypes 等命令）。 */
	static MASSENTITY_API void OutputArchetypeDescription(FOutputDevice& Ar, const FMassArchetypeHandle& Archetype);
	/** 按裸索引或完整 handle 把实体的所有 fragment 值打印出来（mass.PrintEntityFragments 命令的实现）。 */
	static MASSENTITY_API void OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const int32 EntityIndex, const TCHAR* InPrefix = TEXT(""));
	static MASSENTITY_API void OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const FMassEntityHandle Entity, const TCHAR* InPrefix = TEXT(""));

	// ===== 实体选择/高亮 =============================================================

	/** 选择实体作为调试焦点。会同步设置 DebugEntityRange = [Index,Index]，并广播 OnEntitySelectedDelegate。
	 *  所有 MASS_BREAK_IF_ENTITY_DEBUGGED 宏会在该实体路过时停下。 */
	static MASSENTITY_API void SelectEntity(const FMassEntityManager& EntityManager, const FMassEntityHandle EntityHandle);
	static MASSENTITY_API FMassEntityHandle GetSelectedEntity(const FMassEntityManager& EntityManager);

	/** 高亮实体——纯 UI 概念，不会触发断点；让多个 Debugger 视图同步高亮同一实体。
	 *  注意：与 SelectEntity 不同，HighlightEntity 在 Environment 缺失时静默失败而不是 check。 */
	static MASSENTITY_API void HighlightEntity(const FMassEntityManager& EntityManager, const FMassEntityHandle EntityHandle);
	static MASSENTITY_API FMassEntityHandle GetHighlightedEntity(const FMassEntityManager& EntityManager);

	// ===== 静态委托实例 ==============================================================

	static MASSENTITY_API FOnBreakpointsChanged OnBreakpointsChangedDelegate;     //< 断点变动总线
	static MASSENTITY_API FOnEntitySelected OnEntitySelectedDelegate;             //< 实体选中总线

	static MASSENTITY_API FOnMassEntityManagerEvent OnEntityManagerInitialized;   //< 新 EntityManager 上线
	static MASSENTITY_API FOnMassEntityManagerEvent OnEntityManagerDeinitialized; //< EntityManager 下线
	static MASSENTITY_API FOnEnvironmentEvent OnProcessorProviderRegistered;      //< processor provider 注册
	static MASSENTITY_API FOnDebugEvent OnDebugEvent;                             //< 通用调试事件总线（log 类）
	
	/** 广播一个调试事件：低层语义的 DebugEvent。 */
	static void DebugEvent(const FName EventName, FConstStructView Payload, const EMassDebugMessageSeverity SeverityOverride = EMassDebugMessageSeverity::Default)
	{
		OnDebugEvent.Broadcast(EventName, Payload, SeverityOverride);
	}

	/**
	 * 模板版本 DebugEvent：构造一个 TMessage 类型 USTRUCT，自动取其 FName，
	 * 包装成 FConstStructView 后广播。调用方只需写：
	 *   FMassDebugger::DebugEvent<FMyEvent>(arg1, arg2);
	 * 即可发布事件——非常类似日志框架的"事件类型即标签"模式。
	 */
	template<typename TMessage, typename... TArgs>
	static void DebugEvent(TArgs&&... InArgs)
	{
		DebugEvent(TMessage::StaticStruct()->GetFName()
			, FConstStructView::Make(TMessage(Forward<TArgs>(InArgs)...)));
	}

	/**
	 * Registers given EntityManager with the debugger, creating a new entry in ActiveEnvironments.
	 * @return the index of newly created environment
	 *
	 * 由 FMassEntityManager 在初始化末尾调用——把自己登记进 ActiveEnvironments，
	 * 同时广播 OnEntityManagerInitialized 委托。返回新创建的 environment 在数组中的索引（仅供内部使用）。
	 */
	static MASSENTITY_API int32 RegisterEntityManager(FMassEntityManager& EntityManager);
	/** 由 FMassEntityManager 在反初始化时调用。会移除对应 environment 并广播 OnEntityManagerDeinitialized。
	 *  对于已经过期的 weak ptr 也会顺便清理。 */
	static MASSENTITY_API void UnregisterEntityManager(FMassEntityManager& EntityManager);
	
	/**
	 * Confirms that the initialization state of the EntityManager is Initialized.
	 * @return true if EntityManager is initialized, false otherwise
	 *
	 * 通过 friend 访问 InitializationState 私有字段的便捷判定函数。
	 */
	static MASSENTITY_API bool IsEntityManagerInitialized(const FMassEntityManager& EntityManager);

	/**
	 * Registers the given ProviderFunction with the existing FEnvironment associated with the provided EntityManager.
	 * If one doesn't exist yet, it will be created (i.e. will automatically call RegisterEntityManager() with the provided EntityManager).
	 * The function will be called during data collection for the given FEnvironment.
	 * NOTE: there's no UnregisterProcessorDataProvider, the registered providers will automatically get removed along with
	 * the rest of the data associated with the relevant EntityManager as part of UnregisterEntityManager call.
	 *
	 * 让外部模块（典型如 MassGameplay 的 phase processor 系统）告诉 debugger："我这里也有一些 processor，
	 * 列举它们时请通过这个 lambda 拿"。注册后无法单独反注册，但 EntityManager 反初始化时会一起清理。
	 */
	static MASSENTITY_API void RegisterProcessorDataProvider(FName ProviderName, const TSharedRef<FMassEntityManager>& EntityManager, const UE::Mass::Debug::FProcessorProviderFunction& ProviderFunction);

	/** 返回当前所有活动 environment 的只读视图——Debugger UI 用作 EntityManager 列表数据源。 */
	static TConstArrayView<FEnvironment> GetEnvironments() { return ActiveEnvironments; }
	/** 按 EntityManager 反查 environment（不存在返回 nullptr，与 GetActiveEnvironmentChecked 不同）。 */
	static MASSENTITY_API FEnvironment* FindEnvironmentForEntityManager(const FMassEntityManager& EntityManager);

	/**
	 * Determines whether given Archetype matches given Requirements. In case of a mismatch description of failed conditions will be added to OutputDevice.
	 *
	 * 测试 archetype 是否匹配 requirement，并把不匹配的细节追加到 OutputDevice。是
	 * GetArchetypeRequirementCompatibilityDescription 的"输出版"，能让调用方累积多组结果到同一个 device。
	 */
	static MASSENTITY_API bool DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle, const FMassFragmentRequirements& Requirements, FOutputDevice& OutputDevice);

	// ===== 断点查询/设置 API ==========================================================
	// 下面这一大组 API 是断点子系统对外的接口面。命名规则：
	//   - Should*Break：热路径上调用，问"现在该断吗？"——返回断点 handle 或 Invalid。
	//   - Has*Breakpoint(s)：查询是否存在某类断点，用作短路。
	//   - Set*Breakpoint：注册新断点。
	//   - Clear*Breakpoint：移除断点。
	// 所有 Should* / Has* 都要做到 LIKELY 路径只查一两个 bool/Set，避免对热路径性能造成影响。
	// =================================================================================

	/**
	 * Checks if a processor should break on execute for a given entity. Returns the Id for the breakpoint if one is found, or 0.
	 *
	 * 每次 processor 准备处理某实体前调用——若该 (Processor, Entity) 命中断点，
	 * 返回断点 handle（同时累加 HitCount）。调用方拿到 handle 后调用 FBreakpoint::DebugBreak() 真正停下。
	 */
	static MASSENTITY_API UE::Mass::Debug::FBreakpointHandle ShouldProcessorBreak(const FMassEntityManager& EntityManager, const UMassProcessor* Processor, FMassEntityHandle Entity);

	/**
	 * Checks if a processor has any breakpoints set for any entity.
	 *
	 * 不带 Entity 参数的快速判断：该 processor 上是否注册了任何断点。
	 * processor 在 dispatch 入口处调用此函数决定是否进入"逐实体检查断点"的慢路径。
	 */
	static MASSENTITY_API bool HasAnyProcessorBreakpoints(const FMassEntityManager& EntityManager, const UMassProcessor* Processor);

	/**
	 * Checks if a break should be triggered for a processor that's about to write a given fragment on an entity. Returns the Id for the breakpoint if one is found (Invalid otherwise).
	 *
	 * 给"fragment 写入"事件用的断点查询。在 Fragment 即将被修改的代码路径埋点，可以
	 * 实现"当 X 实体的 FFooFragment 被写入时停下"——非常类似 IDE 的 data breakpoint。
	 */
	static MASSENTITY_API UE::Mass::Debug::FBreakpointHandle ShouldBreakOnFragmentWrite(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity);

	/** 按 handle 反查 FBreakpoint（先找 environment、再找 breakpoint）。 */
	static MASSENTITY_API UE::Mass::Debug::FBreakpoint* FindBreakpoint(const FMassEntityManager& EntityManager, UE::Mass::Debug::FBreakpointHandle Handle);

	/** Get all the breakpoints for a given environment.
	 *  返回某 environment 的断点数组的可写引用——给 Debugger UI 编辑断点参数用。 */
	static MASSENTITY_API TArray<UE::Mass::Debug::FBreakpoint>& GetBreakpoints(const FMassEntityManager& EntityManager);

	/**
	 * Refresh breakpoint flags after changes to breakpoint instances.
	 *
	 * 重建 ProcessorsWithBreakpoints / FragmentsWithBreakpoints 加速集合，
	 * 并刷新各 environment 的 bHasBreakpoint 全局标志。
	 * 任何对 Breakpoints 数组的"外部"修改之后都需要调用一次。
	 */
	static MASSENTITY_API void RefreshBreakpoints();

	/**
	 * Checks if there are any breakpoints set for writing a fragment for any entity
	 * Use FragmentType = nullptr (default) to check for ANY fragment types.
	 *
	 * 查询"任何实体上写入 X fragment 时是否会触发断点"。FragmentType=nullptr 时
	 * 退化为"是否存在任何 fragment 写入断点"。
	 */
	static MASSENTITY_API bool HasAnyFragmentWriteBreakpoints(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType = nullptr);

	/** Create a default-constructed breakpoint
	 *  在 environment 中追加一个空白断点对象，调用方负责后续填充字段。 */
	static MASSENTITY_API UE::Mass::Debug::FBreakpoint& CreateBreakpoint(const FMassEntityManager& EntityManager);

	/** Sets a break to be triggered on processor execute for an entity.
	 *  注册"processor 执行 + 指定实体"复合断点。Entity=Invalid 表示"任意实体"。 */
	static MASSENTITY_API void SetProcessorBreakpoint(const FMassEntityManager& EntityManager, TNotNull<const UMassProcessor*>, FMassEntityHandle Entity);

	/** Sets a break to be triggered for a processor that's about to write a given fragment on an entity.
	 *  注册"fragment 写入 + 指定实体"断点。 */
	static MASSENTITY_API void SetFragmentWriteBreakpoint(const FMassEntityManager& EntityManager, TNotNull<const UScriptStruct*> FragmentType, FMassEntityHandle Entity);

	/** Sets or removes a break before a write to a given fragment on whichever entity is selected at the time.
	 *  注册"fragment 写入 + 当前 selected entity"断点；filter 类型为 SelectedEntity，
	 *  与 SpecificEntity 的差异是触发时实时查询当前选中实体（焦点切换会动态影响断点目标）。 */
	static MASSENTITY_API void SetFragmentWriteBreakForSelectedEntity(const FMassEntityManager& EntityManager, TNotNull<const UScriptStruct*> FragmentType);

	/** Enable or Disable a breakpoint with a given Id.
	 *  按 handle 启用/禁用断点。禁用后断点仍占位（HitCount 仍递增）但不会真正 break。 */
	static MASSENTITY_API void SetBreakpointEnabled(UE::Mass::Debug::FBreakpointHandle Handle, bool bEnable);

	/** Clears a breakpoint triggered on processor execute for an entity. */
	static MASSENTITY_API void ClearProcessorBreakpoint(const FMassEntityManager& EntityManager, const UMassProcessor* Processor, FMassEntityHandle Entity);

	/** Clears all breakpoints set for a given processor. */
	static MASSENTITY_API void ClearAllProcessorBreakpoints(const FMassEntityManager& EntityManager, const UMassProcessor* Processor);

	/** Clears a fragment write breakpoint for a given fragment type and entity. */
	static MASSENTITY_API void ClearFragmentWriteBreak(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity);
	
	/** Clears all write breakpoints set for a given fragment type. */
	static MASSENTITY_API void ClearAllFragmentWriteBreak(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType);

	/** Clears all breakpoints set for a given entity. */
	static MASSENTITY_API void ClearAllEntityBreakpoints(const FMassEntityManager& EntityManager, FMassEntityHandle Entity);

	/**
	 * Sets a write breakpoint for the specified fragment on the selcected entity
	 * @see SelectEntity
	 *
	 * 控制台命令 mass.debug.SetFragmentBreakpoint 的内部实现入口——
	 * 在所有 environment 上都注册一个"selected entity 写入 X fragment 时触发"的断点。
	 */
	static MASSENTITY_API void BreakOnFragmentWriteForSelectedEntity(FName FragmentName);

	/**
	 * Gets the UScriptStruct type for fragment of the specified name.
	 *
	 * 按名字查 fragment UScriptStruct——内部缓存于 FragmentsByName，
	 * 缓存未命中时会扫描所有 archetype 收集所有 fragment 类型来填充缓存。
	 */
	static MASSENTITY_API const UScriptStruct* GetFragmentTypeFromName(FName FragmentName);

	// ===== Fragment 数据读取 ========================================================
	// 这组 API 让 Debugger UI 能"在外部窥探"实体的 fragment 当前值。
	// 返回的是 FStructOnScope（带类型信息的 struct 副本），UI 可用 PropertyEditor 反射展示。

	/** Finds the fragment data of the specified type in the entity data. Returns nullptr if not found.
	 *  查找并复制实体的指定 fragment 值。注意是**复制**——返回的 FStructOnScope 与
	 *  原始内存解耦，UI 修改不会影响实体。 */
	static MASSENTITY_API TSharedPtr<FStructOnScope> GetFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity);

	/** Finds the fragment data of the specified type in the entity data. Returns false if not found.
	 *  上面方法的"into"形式：复用已有 FStructOnScope 减少分配。 */
	static MASSENTITY_API bool GetFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity, TSharedPtr<FStructOnScope>& OutStructData);

	/** Get the shared fragment value container for this entity
	 *  返回该实体的 shared fragment 容器（数据是真正共享的，多个实体的容器内容相同）。 */
	static MASSENTITY_API const FMassArchetypeSharedFragmentValues& GetSharedFragmentValues(const FMassEntityManager& EntityManager, FMassEntityHandle Entity);
	
	/** Finds the shared fragment data of the specified type in the entity data. Returns nullptr if not found. */
	static MASSENTITY_API TSharedPtr<FStructOnScope> GetSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity);

	/** Finds the shared fragment data of the specified type in the entity data. Returns nullptr if not found. */
	static MASSENTITY_API bool GetSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity, TSharedPtr<FStructOnScope>& OutStructData);
	
	/** Finds the const shared fragment data of the specified type in the entity data. Returns nullptr if not found. */
	static MASSENTITY_API TSharedPtr<FStructOnScope> GetConstSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity);

	/** Finds the const shared fragment data of the specified type in the entity data. Returns nullptr if not found. */
	static MASSENTITY_API bool GetConstSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity, TSharedPtr<FStructOnScope>& OutStructData);

	/** Clears all breakpoints in all environments.
	 *  清空所有 environment 的所有断点（会广播 OnBreakpointsChanged）。 */
	static MASSENTITY_API void ClearAllBreakpoints();

	/** Remove a single breakpoint by Id
	 *  按 handle 删除单个断点，自动调用 RefreshBreakpoints 更新加速集合。 */
	static MASSENTITY_API void RemoveBreakpoint(UE::Mass::Debug::FBreakpointHandle Handle);

	/**
	 * Destroy all debug environments and shutdown cleanly.
	 *
	 * 模块卸载时调用——区别于 ClearAllBreakpoints：不广播任何委托（避免在销毁过程中
	 * 仍有订阅者持有失效的指针），并清空 ActiveEnvironments 和 FragmentsByName。
	 */
	static void ShutdownDebugger();
	
private:
	// @todo: contain static data in an instanced struct and setup/teardown in startup/shutdown module
	// （TODO 注释指出未来希望把这些静态数据封装进 instanced struct 并跟随模块启停管理；
	//   目前是 globals，存在初始化/销毁顺序的潜在风险。）
	static MASSENTITY_API TArray<FEnvironment> ActiveEnvironments;             //< 全部活动 environment（每 EntityManager 一个）
	static MASSENTITY_API UE::FTransactionallySafeMutex EntityManagerRegistrationLock;  //< 保护 ActiveEnvironments 的注册锁
	static MASSENTITY_API TMap<FName, const UScriptStruct*> FragmentsByName;   //< fragment 名→类型的缓存（控制台命令解析用）
	static MASSENTITY_API TArray<FName> CommandNames;                          //< 已注册的调试命令名（目前似乎未在本头文件中被使用）

	/** 内部用：必定能拿到 environment（拿不到就 check 失败）。 */
	static MASSENTITY_API FEnvironment& GetActiveEnvironmentChecked(const FMassEntityManager& EntityManager);
	/** 内部用：拿不到返回 nullptr（HighlightEntity 等容许失败的路径用）。 */
	static MASSENTITY_API FEnvironment* GetActiveEnvironment(const FMassEntityManager& EntityManager);
};

#else

// =====================================================================================================================
// 【Shipping 构建分支：debugger 完全塌缩成空壳】
//
// 在 Shipping/无 debug 构建中，FMassDebugger 仅保留几个返回常量字符串的存根函数，
// 让那些"在 logging 路径调用 GetSingleRequirementDescription"的代码能继续编译。
//
// 四个调试宏全部退化为 no-op，达到**完全零运行时开销**——不会有任何 if、任何函数调用，
// 编译器会把它们直接消除。
// =====================================================================================================================

struct FMassArchetypeHandle;
struct FMassFragmentRequirements;
struct FMassFragmentRequirementDescription;
struct FMassArchetypeCompositionDescriptor;

struct FMassDebugger
{
	static FString GetSingleRequirementDescription(const FMassFragmentRequirementDescription&) { return TEXT("[no debug information]"); }
	static FString GetRequirementsDescription(const FMassFragmentRequirements&) { return TEXT("[no debug information]"); }
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements&, const FMassArchetypeHandle&) { return TEXT("[no debug information]"); }
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements&, const FMassArchetypeCompositionDescriptor&) { return TEXT("[no debug information]"); }
};

// 调试宏的空操作版——expansion 后留下完全空白，无任何分支或副作用。
#define MASS_IF_ENTITY_DEBUGGED(a, b) false
#define MASS_BREAK_IF_ENTITY_DEBUGGED(a, b)
#define MASS_BREAK_IF_ENTITY_INDEX(a, b)
#define MASS_SET_ENTITY_DEBUGGED(a, b)

#endif // WITH_MASSENTITY_DEBUG
