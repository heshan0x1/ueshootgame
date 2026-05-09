// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =====================================================================================================================
// 文件角色：Mass 处理管线（Processing Pipeline）层最底层的公共类型定义。
// 该头文件放在 MassEntityTypes.h 之前被 include（见 MassEntityTypes.h 第 6 行），是整个 Mass 模块里
// "Processor 概念"第一个登场的头文件。它不直接依赖 UMassProcessor（仅前向声明），以避免循环依赖。
// 本文件定义：
//   1. EProcessorExecutionFlags —— 处理器允许在哪些 netmode / world-mode 下执行（位掩码）
//   2. FMassProcessingContext_DEPRECATED —— 已过时的处理上下文（5.6 版本被 UE::Mass::FProcessingContext 替换）
//   3. FMassRuntimePipeline —— 运行期的 Processor 数组容器
//   4. EMassProcessingPhase —— 帧内 6 大处理阶段（PrePhysics / StartPhysics / ... / FrameEnd）
//   5. FMassProcessorOrderInfo —— DependencySolver 展平后的单个节点（用于处理器/组的调度顺序）
// =====================================================================================================================

#include "StructUtils/StructUtilsTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/SubclassOf.h"
#include "MassEntityMacros.h"
#include "MassProcessingTypes.generated.h"

// 局部宏：在本文件内用 UE_API 代替 MASSENTITY_API。文件末尾 #undef。
// 这种写法可以让函数声明更紧凑，同时避免宏泄漏到包含本文件的其它 TU。
#define UE_API MASSENTITY_API

#ifndef MASS_DO_PARALLEL
// 是否启用并行处理。服务器构建下默认关闭（避免对 dedicated server 产生不确定的线程开销）。
// 若外部已定义此宏则尊重外部定义。
#define MASS_DO_PARALLEL !UE_SERVER
#endif // MASS_DO_PARALLEL

// 前向声明：解耦模块依赖，避免在此处 include 完整定义。
struct FMassEntityManager;       // ECS 管理器（持有所有 archetype / entity）
class UMassProcessor;            // 处理器基类
class UMassCompositeProcessor;   // 组合处理器（承载多个子 processor 并按依赖图调度）
struct FMassCommandBuffer;       // 命令缓冲：延迟变更 entity 组合的队列

// ---------------------------------------------------------------------------------------------------------------------
// EProcessorExecutionFlags
// ---------------------------------------------------------------------------------------------------------------------
// 处理器执行场景的位掩码。每个 UMassProcessor 会声明自己允许在哪些 world/net mode 下运行；
// 运行期由 UE::Mass::Utils::DetermineProcessorExecutionFlags(World, PipelineExecutionFlags) 解析
// 当前 World 的执行掩码，再与 Processor 声明的掩码按位与。若结果为 0，该 processor 被跳过。
// meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true") 表示在编辑器下按"掩码值"方式展示/编辑（非位索引）。
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EProcessorExecutionFlags : uint8
{
	None = 0 UMETA(Hidden),                                                          // 不执行（通常用于 sentinel）
	Standalone = 1 << 0,                                                             // 单机（非网络或 ListenServer 的客户端视角都可能命中）
	Server = 1 << 1,                                                                 // 专用服务器 / ListenServer 主机端
	Client = 1 << 2,                                                                 // 网络客户端
	Editor = 1 << 3,                                                                 // 编辑器工具上下文（非 PIE）
	EditorWorld = 1 << 4,                                                            // 编辑器世界（打开关卡但未 PIE，仍有 UWorld）
	AllNetModes = Standalone | Server | Client UMETA(Hidden),                        // 三大 NetMode 并集
	AllWorldModes = Standalone | Server | Client | EditorWorld UMETA(Hidden),        // 加上 EditorWorld
	All = Standalone | Server | Client | Editor | EditorWorld UMETA(Hidden)          // 全部（最宽松）
};
ENUM_CLASS_FLAGS(EProcessorExecutionFlags);                                          // 生成 |, &, ~ 等运算符重载

// ---------------------------------------------------------------------------------------------------------------------
// FProcessorAuxDataBase
// ---------------------------------------------------------------------------------------------------------------------
// 处理器辅助数据的基类，常被放入 FInstancedStruct 作为"某次处理调用"的临时参数包。
// 此处仅作 USTRUCT 反射标记入口，具体负载由各 processor 自行继承扩展。
USTRUCT()
struct FProcessorAuxDataBase
{
	GENERATED_BODY()
};

// ---------------------------------------------------------------------------------------------------------------------
// FMassProcessingContext_DEPRECATED
// ---------------------------------------------------------------------------------------------------------------------
// 5.6 版本起已废弃，新代码统一用 UE::Mass::FProcessingContext（见下方 using 别名）。
// 保留此类型是为了兼容旧蓝图/数据资产中的反射字段。
USTRUCT(BlueprintType, meta = (Deprecated = "5.6"))
struct FMassProcessingContext_DEPRECATED
{
	GENERATED_BODY()

	UPROPERTY()
	float DeltaSeconds = 0.f;                // 本次 tick 的时间增量

	UPROPERTY()
	FInstancedStruct AuxData;                // 额外上下文数据（类型擦除）

	UPROPERTY()
	bool bFlushCommandBuffer = true;         // 处理结束后是否立刻 flush 命令缓冲
};

namespace UE::Mass
{
	// 新的处理上下文，真正定义放在 MassProcessingContext.h。此处仅前向声明以便下面建立 using 别名。
	struct FProcessingContext;
}
// 类型别名：让老代码里的 `FMassProcessingContext` 直接指向新的 UE::Mass::FProcessingContext。
using FMassProcessingContext = UE::Mass::FProcessingContext;

/** 
 *  Runtime-usable array of MassProcessor copies
 *  运行期可用的 Processor 数组容器。
 *  ---------------------------------------------------------------------------
 *  设计要点：
 *  - 仅承担"容器"职责（增/删/查/排序），不负责调度。真正的依赖图解析 + 顺序计算
 *    在 FMassProcessorDependencySolver，执行（可能并行）在 UMassCompositeProcessor。
 *  - "Copies" 的含义：通常接口接收的是"模板/CDO Processor"，FMassRuntimePipeline 会
 *    用 NewObject<Proc>(Owner, Class, ..., Template=CDO) 创建运行期副本，以避免修改 CDO。
 *  - 持有 TObjectPtr<UMassProcessor>，保证 GC 安全。
 *  - ExecutionFlags 记录"此管线所属 World 的执行掩码"，用于过滤不该执行的 Processor。
 */
USTRUCT()
struct FMassRuntimePipeline
{
	GENERATED_BODY()

private:
	// 实际持有的 Processor 列表。UPROPERTY 保证被 GC 追踪；顺序即"添加顺序"，
	// 最终执行顺序由 SortByExecutionPriority() 或 DependencySolver 决定。
	UPROPERTY()
	TArray<TObjectPtr<UMassProcessor>> Processors;

	// 当前管线关联的 World 执行模式掩码；添加 Processor 时会据此过滤。
	EProcessorExecutionFlags ExecutionFlags = EProcessorExecutionFlags::None;

public:
	// 构造：仅指定 World 执行掩码，内部 Processor 数组为空。
	explicit FMassRuntimePipeline(EProcessorExecutionFlags WorldExecutionFlags = EProcessorExecutionFlags::None);
	// 构造：用一组已就绪的 Processor 直接填充（不做拷贝，只接管指针）。
	UE_API FMassRuntimePipeline(TConstArrayView<TObjectPtr<UMassProcessor>> SeedProcessors, EProcessorExecutionFlags WorldExecutionFlags);
	// 构造：与上面的重载等价，仅参数类型是裸指针数组（内部包装为 TObjectPtr）。
	UE_API FMassRuntimePipeline(TConstArrayView<UMassProcessor*> SeedProcessors, EProcessorExecutionFlags WorldExecutionFlags);

	// 清空 Processors 数组（保留 ExecutionFlags）。
	UE_API void Reset();
	// 对所有尚未初始化的 Processor 调用 CallInitialize(Owner, EntityManager)。
	// 过程中会顺带清除数组里的 nullptr 元素。
	UE_API void Initialize(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager);
	
	/** Copies the given array over to this FMassRuntimePipeline instance. */
	/** 以拷贝方式覆盖当前 Processor 列表（会先 Reset）。入参为裸指针数组。 */
	UE_API void SetProcessors(TArrayView<UMassProcessor*> InProcessors);

	/** Directly moves the contents of given array to the Processors member array. */
	/** 移动语义覆盖当前列表，避免额外拷贝。 */
	UE_API void SetProcessors(TArray<TObjectPtr<UMassProcessor>>&& InProcessors);

	/** Creates runtime copies of UMassProcessors given in InProcessors input parameter, using InOwner as new UMassProcessors' outer. */
	/** 为传入的每个 Processor 创建"运行期副本"（以其为 NewObject 的模板），Outer 为 InOwner。
	 *  内部会先 Reset 再调用 AppendOrOverrideRuntimeProcessorCopies。 */
	UE_API void CreateFromArray(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner);

	/** Calls CreateFromArray and calls Initialize on all processors afterwards. */
	/** CreateFromArray + Initialize 的便捷封装。 */
	UE_API void InitializeFromArray(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager);
	
	/** Creates runtime instances of UMassProcessors for each processor class given via InProcessorClasses. 
	 *  The instances will be created with InOwner as outer. */
	/** 从 Processor 的 UClass 列表创建实例（通过 CDO 的 ShouldExecute 过滤），然后 Initialize。
	 *  与 InitializeFromArray 的区别在于入参是 Class 而非已有实例。 */
	UE_API void InitializeFromClassArray(TConstArrayView<TSubclassOf<UMassProcessor>> InProcessorClasses, UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager);

	/** Creates a runtime instance of every processors in the given InProcessors array. If a processor of that class
	 *  already exists in Processors array it gets overridden. Otherwise it gets added to the end of the collection.*/
	/** "追加或覆盖"：对每个 InProcessor 创建运行期副本；若本管线已存在同类 Processor 则覆盖，
	 *  否则追加到末尾。注意：若 Processor 的 ShouldAllowMultipleInstances() 为 true，则不做覆盖判定，总是追加。 */
	UE_API void AppendOrOverrideRuntimeProcessorCopies(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner);

	/** Creates a runtime instance of every processors in the given array if there's no processor of that class in Processors already.
	 *  Call this function when adding processors to an already configured FMassRuntimePipeline instance. If you're creating 
	 *  one from scratch calling any of the InitializeFrom* methods will be more efficient (and will produce same results)
	 *  or call AppendOrOverrideRuntimeProcessorCopies.
	 *	NOTE: there's a change in functionality since 5.6 - the function will no longer create duplicates for processors returning true from ShouldAllowMultipleInstances
	 */
	/** "去重追加"：若本管线已存在同类 Processor 则跳过，否则创建副本并追加。
	 *  5.6 起的行为变更：即使 ShouldAllowMultipleInstances()==true，也不再创建重复副本。
	 *  新构建场景请优先用 InitializeFrom*；本函数主要用于"已配置好的管线再补 Processor"。 */
	UE_API void AppendUniqueRuntimeProcessorCopies(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager);

	/** Adds InProcessor(s) to Processors without any additional checks */
	/** 直接把指针追加到 Processors 末尾（不做去重、不做 ExecutionFlags 过滤、不初始化）。 */
	UE_API void AppendProcessor(UMassProcessor& InProcessor);
	UE_API void AppendProcessors(TArrayView<TObjectPtr<UMassProcessor>> InProcessors);
	UE_API void AppendProcessors(TArray<TObjectPtr<UMassProcessor>>&& InProcessors);

	/** @return true if the given processor has indeed been added, i.e. will return false if Processor was already part of the pipeline. */
	/** 去重追加单个 Processor；返回 true 表示确实发生了追加。 */
	UE_API bool AppendUniqueProcessor(UMassProcessor& Processor);

	/** Creates an instance of ProcessorClass and adds it to Processors without any additional checks */
	/** 由 Class 创建新实例并直接追加（不去重、不校验 ExecutionFlags）。 */
	UE_API void AppendProcessor(TSubclassOf<UMassProcessor> ProcessorClass, UObject& InOwner);

	/** @return whether the given processor has been removed from hosted processors collection */
	/** 按"同一个指针"移除 Processor；返回是否真的删除过元素。 */
	UE_API bool RemoveProcessor(const UMassProcessor& InProcessor);

	/** goes through Processor looking for a UMassCompositeProcessor instance which GroupName matches the one given as the parameter */
	/** 在顶层 Processor 中查找 GroupName 匹配的 CompositeProcessor（仅查一层，不递归）。 */
	UE_API UMassCompositeProcessor* FindTopLevelGroupByName(const FName GroupName);

	// 是否存在"运行期类恰好相等"（非 IsA）的 Processor。用于去重/避免重复注册。
	UE_API bool HasProcessorOfExactClass(TSubclassOf<UMassProcessor> InClass) const;
	// 是否为空。
	bool IsEmpty() const;

	int32 Num() const;                                                                       // 当前 Processor 数量
	TConstArrayView<TObjectPtr<UMassProcessor>> GetProcessors() const;                       // 只读访问
	TConstArrayView<UMassProcessor*> GetProcessorsView() const { return ObjectPtrDecay(Processors); } // 裸指针视图（弱化 GC 标记）
	TArrayView<TObjectPtr<UMassProcessor>> GetMutableProcessors();                            // 可修改访问

	/** Returns Processors array using move semantics. This operation clears out this FMassRuntimePipeline instance. */
	/** 将内部数组"抢走"（move）；调用后 Pipeline 变空。常用于向 DependencySolver 交接所有权。 */
	TArray<TObjectPtr<UMassProcessor>>&& MoveProcessorsArray();

	// 允许 FMassRuntimePipeline 作为 TMap/TSet 的键：哈希基于"Processor 指针集合"。
	UE_API friend uint32 GetTypeHash(const FMassRuntimePipeline& Instance);

	/**
	 * Sorts processors aggregates in Processors array so that the ones with higher ExecutionPriority are executed first
	 * The function will also remove nullptrs, if any, before sorting.
	 */
	/** 按 Processor->GetExecutionPriority() 降序排序（优先级高者在前）。
	 *  排序前会先 RemoveAllSwap 掉 nullptr。注意：这只是"单一数值优先级"排序，
	 *  不涉及依赖关系；依赖关系由 FMassProcessorDependencySolver 处理。 */
	UE_API void SortByExecutionPriority();

	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
	// 下列重载在 5.6 版被标记过时：它们内部会自行查找 FMassEntityManager，
	// 而新版 API 要求显式传入 TSharedRef<FMassEntityManager>，以避免隐式依赖 World/Subsystem 查找。
	UE_DEPRECATED(5.6, "This flavor of Initialize is deprecated. Please use the one requiring a FMassEntityManager parameter")
	UE_API void Initialize(UObject& Owner);

	UE_DEPRECATED(5.6, "This flavor of InitializeFromArray is deprecated. Please use the one requiring a FMassEntityManager parameter")
	UE_API void InitializeFromArray(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner);
	
	UE_DEPRECATED(5.6, "This flavor of InitializeFromClassArray is deprecated. Please use the one requiring a FMassEntityManager parameter")
	UE_API void InitializeFromClassArray(TConstArrayView<TSubclassOf<UMassProcessor>> InProcessorClasses, UObject& InOwner);

	UE_DEPRECATED(5.6, "This flavor of AppendUniqueRuntimeProcessorCopies is deprecated. Please use the one requiring a FMassEntityManager parameter")
	UE_API void AppendUniqueRuntimeProcessorCopies(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner);

	UE_DEPRECATED(5.6, "This flavor of SetProcessors is deprecated. Please use one of the others.")
	UE_API void SetProcessors(TArray<UMassProcessor*>&& InProcessors);
};

// ---------------------------------------------------------------------------------------------------------------------
// EMassProcessingPhase
// ---------------------------------------------------------------------------------------------------------------------
// Mass 处理阶段——对应 UE TickGroup 中的 6 个节点。FMassProcessingPhaseManager 为每个阶段维护一个独立的
// 顶层 CompositeProcessor，在对应 TG_PrePhysics / TG_StartPhysics / ... / TG_LastDemotable 时刻触发。
// 每个 UMassProcessor 通过 ProcessingPhase 字段声明自己属于哪一个 Phase。
// MAX 是哨兵值，遍历时作为"结束/个数"使用，注意它不是合法阶段。
UENUM()
enum class EMassProcessingPhase : uint8
{
	PrePhysics,     // 物理前
	StartPhysics,   // 物理开始（物理子步骤开启时）
	DuringPhysics,  // 物理并行执行期间（与物理线程并行）
	EndPhysics,     // 物理结束
	PostPhysics,    // 物理后（常规游戏逻辑）
	FrameEnd,       // 帧末（发送 replication / cleanup 之前的最后机会）
	MAX,            // 哨兵：阶段数量上限
};

// ---------------------------------------------------------------------------------------------------------------------
// FMassProcessorOrderInfo
// ---------------------------------------------------------------------------------------------------------------------
// FMassProcessorDependencySolver 的输出单元：它把嵌套的"组/子组/Processor"结构展平为一条线性序列，
// 通过 GroupStart/GroupEnd 两个特殊节点标注原始层级边界（类似括号匹配）。CompositeProcessor 按此序列执行。
struct FMassProcessorOrderInfo
{
	// 依赖图中某一"节点"的种类。
	enum class EDependencyNodeType : uint8
	{
		Invalid,     // 未初始化/非法
		Processor,   // 实际的 Processor 节点
		GroupStart,  // 组开始（与 GroupEnd 成对出现，用于还原嵌套）
		GroupEnd     // 组结束
	};

	FName Name = TEXT("");                                 // 节点名（Processor 名或 Group 名），便于调试/Visualizer
	UMassProcessor* Processor = nullptr;                   // 若 NodeType==Processor 则非空
	EDependencyNodeType NodeType = EDependencyNodeType::Invalid; // 节点类型
	TArray<FName> Dependencies;                            // 本节点显式依赖的其他节点名
	int32 SequenceIndex = INDEX_NONE;                      // 展平后在整个序列中的位置（用于并行 barrier 判断）
};

//-----------------------------------------------------------------------------
// Inlines
//-----------------------------------------------------------------------------
// 纯 ExecutionFlags 的构造：内部 Processor 列表为空，后续再 Append/Initialize。
inline FMassRuntimePipeline::FMassRuntimePipeline(EProcessorExecutionFlags WorldExecutionFlags)
	: ExecutionFlags(WorldExecutionFlags)
{
	
}

inline bool FMassRuntimePipeline::IsEmpty() const
{
	return Processors.IsEmpty();
}

inline int32 FMassRuntimePipeline::Num() const
{
	return Processors.Num();
}

inline TConstArrayView<TObjectPtr<UMassProcessor>> FMassRuntimePipeline::GetProcessors() const
{
	return Processors;
}

inline TArrayView<TObjectPtr<UMassProcessor>> FMassRuntimePipeline::GetMutableProcessors()
{
	return Processors;
}

inline TArray<TObjectPtr<UMassProcessor>>&& FMassRuntimePipeline::MoveProcessorsArray()
{
	return MoveTemp(Processors);
}

// 清理本文件内使用的 UE_API 别名，避免污染其它 TU。
#undef UE_API
