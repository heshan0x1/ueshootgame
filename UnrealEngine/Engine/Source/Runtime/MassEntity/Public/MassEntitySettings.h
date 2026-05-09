// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSettings.h"
#include "MassProcessingPhaseManager.h"
#include "MassProcessor.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "StructUtils/InstancedStruct.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntitySettings.generated.h"

// =============================================================================================
// MassEntitySettings.h —— MassEntity 模块自身的 Project Settings
// ---------------------------------------------------------------------------------------------
// 关系：UMassEntitySettings : UMassModuleSettings : UObject
//   - 派生自 UMassModuleSettings，因此会自动注册到 UMassSettings 的子页面（见 MassSettings.h）。
//   - 在 UI 中显示为 "Project Settings → Plugins → Mass → Mass Entity"。
//
// 它持有 MassEntity 子模块运行所需的关键配置：
//   1) ChunkMemorySize           —— Archetype 内每个 Chunk 占用的字节数（性能关键参数）。
//   2) DumpDependencyGraphFileName—— DependencySolver 将 processor DAG 输出为 .dot 文件用于调试。
//   3) ProcessingPhasesConfig[]  —— 每个 phase（PrePhysics / DuringPhysics / PostPhysics / FrameEnd
//                                    / Output / ...）下挂哪些 processor、用什么 CompositeProcessor 包装。
//   4) ProcessorCDOs             —— 当前模块（含已加载 Plugin）中所有非 abstract 的 UMassProcessor
//                                    派生类的 CDO 列表，按显示名排序。供编辑器可视化与按需启用。
//
// 使用入口：
//   GET_MASS_CONFIG_VALUE(ChunkMemorySize) → 等价于 GetMutableDefault<UMassEntitySettings>()->ChunkMemorySize
//   通常在运行时通过这个宏读取配置。
// =============================================================================================


/**
 * 便捷宏：在运行时拿到 UMassEntitySettings CDO 上的某个字段值。
 * 用法：GET_MASS_CONFIG_VALUE(ChunkMemorySize) — 返回当前 ini 配置里的 ChunkMemorySize。
 *
 * 注意：返回的是 *Mutable* default —— 若调用方写入会真的改变全局配置，请仅用于读取。
 */
#define GET_MASS_CONFIG_VALUE(a) (GetMutableDefault<UMassEntitySettings>()->a)

struct FPropertyChangedEvent;


/**
 * Implements the settings for MassEntity plugin
 * 中文：MassEntity 插件的设置类。
 *
 * UCLASS 元参数：
 *   - MinimalAPI               : 仅导出反射符号
 *   - config = Mass            : 字段持久化到 DefaultMass.ini
 *   - defaultconfig            : 不写入 Saved/Config（per-user 覆盖）
 *   - DisplayName = "Mass Entity": Project Settings 子页面显示名
 */
UCLASS(MinimalAPI, config = Mass, defaultconfig, DisplayName = "Mass Entity")
class UMassEntitySettings : public UMassModuleSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	/**
	 * 编辑器下的"设置变更"广播委托。
	 * 触发时机：用户在 Project Settings UI 中改了任何 UPROPERTY，PostEditChangeProperty 回调中 Broadcast。
	 * 监听者：运行中的 FMassProcessingPhaseManager 收到通知后重建 phase 拓扑（重新跑 DependencySolver）。
	 *
	 * 这是"配置即代码"思想下的关键反馈链：UI 改一个字段 → 整个 processor 图谱在 PIE 中实时重建。
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSettingsChange, const FPropertyChangedEvent& /*PropertyChangedEvent*/);
#endif // WITH_EDITORONLY_DATA

	/**
	 * "初始化完成"委托。
	 * 触发时机：BuildProcessorListAndPhases() 第一次完成扫描（即引擎初始化结束，所有 UClass 已注册之后）。
	 * 监听者：那些必须等 ProcessorCDOs/PhaseConfig 就绪才能工作的子系统（例如 PhaseManager 的初始化）。
	 */
	DECLARE_MULTICAST_DELEGATE(FOnInitialized);

	MASSENTITY_API UMassEntitySettings(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * 扫描整个引擎中所有 UMassProcessor 派生类，填充 ProcessorCDOs 与 ProcessingPhasesConfig，
	 * 然后（在编辑器下）为每个 phase 生成可视化用的 PhaseProcessor。
	 * 多次调用无副作用：bInitialized 守卫保证只跑一次（除非显式重置）。
	 */
	MASSENTITY_API void BuildProcessorListAndPhases();

	/**
	 * 把指定的 Processor 子类强制加入"全局激活列表"。
	 * 用途：用于那些 bAutoRegisterWithProcessingPhases==false 但项目希望临时启用的 processor。
	 * 副作用：会把 ProcessorCDO 的 ShouldAutoRegisterWithGlobalList() 设为 true。
	 */
	MASSENTITY_API void AddToActiveProcessorsList(TSubclassOf<UMassProcessor> ProcessorClass);

	/**
	 * 取得所有 phase 的配置（只读视图）。若尚未初始化会自动触发 BuildProcessorListAndPhases()。
	 * 这是 PhaseManager.Initialize() 拿配置的标准入口。
	 */
	MASSENTITY_API TConstArrayView<FMassProcessingPhaseConfig> GetProcessingPhasesConfig();

	/** 单 phase 配置查询，越界由 check 兜底。inline，性能无损。 */
	const FMassProcessingPhaseConfig& GetProcessingPhaseConfig(const EMassProcessingPhase ProcessingPhase) const { check(ProcessingPhase != EMassProcessingPhase::MAX); return ProcessingPhasesConfig[int(ProcessingPhase)]; }

	/** 拿到全局唯一的"初始化完成"广播委托 —— 注意是 static，从任何地方都能监听。 */
	static FOnInitialized& GetOnInitializedEvent() { return GET_MASS_CONFIG_VALUE(OnInitializedEvent); }
#if WITH_EDITOR
	/** 编辑器专属：拿到"设置变更"广播委托，PhaseManager 等监听者用它来响应 UI 改动。 */
	FOnSettingsChange& GetOnSettingsChange() { return OnSettingsChange; }

	/** 是否已完成首次扫描（仅编辑器查询；运行时无意义，因为运行时一启动就肯定已初始化）。 */
	static bool IsInitialized() { return GET_MASS_CONFIG_VALUE(bInitialized); }

protected:
	/** 编辑器：UPROPERTY 被改时的回调。会按属性名分支处理（重建 ProcessorList / 修正 ChunkMemorySize / 总是重建 Phases 并广播）。 */
	MASSENTITY_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** 编辑器：嵌套属性被改时的回调（例如某 Processor 内部的 bAutoRegisterWithProcessingPhases 翻转），用于触发重新扫描。 */
	MASSENTITY_API virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	/** UObject 标准回调：CDO 反序列化完成后修正 ChunkMemorySize（保证为合法的 2 的幂等约束）。 */
	MASSENTITY_API virtual void PostInitProperties() override;

	/** UObject 标准回调：解除对 OnPostEngineInit / CompiledInUObjectsRemovedDelegate 的订阅。 */
	MASSENTITY_API virtual void BeginDestroy() override;

	/**
	 * Engine 完成初始化时（FCoreDelegates::OnPostEngineInit）触发。
	 * 为什么必须等到这一刻？因为 BuildProcessorList() 用 GetDerivedClasses() 来枚举所有
	 * UMassProcessor 派生类，而这要求所有 UClass 反射类型已经注册完成 —— 在 OnPostEngineInit 之后才成立。
	 */
	MASSENTITY_API void OnPostEngineInit();

	/** 编辑器下：根据 ProcessingPhasesConfig[i].ProcessorCDOs 重新构造每个 phase 的 PhaseProcessor（CompositeProcessor）。 */
	MASSENTITY_API void BuildPhases();

	/** 扫描 UMassProcessor 派生 → 过滤抽象类与 UMassCompositeProcessor → 写入 ProcessorCDOs 与 PhaseConfig。 */
	MASSENTITY_API void BuildProcessorList();

private:
	/**
	 * 当某个模块（例如 GameFeaturePlugin）被卸载时，该模块带来的 UMassProcessor CDO 也应从列表中移除。
	 * 通过监听 FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate 实现热卸载支持。
	 */
	void OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages);

public:
	/**
	 * Archetype 中每个 Chunk 的字节大小，默认 128KB。
	 *
	 * 性能调优要点（值得反复斟酌的关键参数）：
	 *   - 调大：单个 chunk 容纳更多 entity → 减少 chunk 切换 / 跨 chunk 迭代开销 → 利于
	 *           cache prefetch 流水；但若实际 entity 数量少，会浪费内存（chunk 为空也吃整页）。
	 *   - 调小：内存利用率高、稀疏 archetype 不浪费；代价是 chunk 数变多，
	 *           cache miss 率上升、processor 内层循环边界检查更频繁。
	 *   - 经验值：128KB 是 L2 cache 友好的折中（约对应 1024~4096 个 entity，取决于 fragment 大小）。
	 *
	 * UPROPERTY meta：
	 *   - EditDefaultsOnly: 只能在 CDO/Settings 编辑器中改（不能在实例上改）
	 *   - config           : 持久化到 DefaultMass.ini
	 *   - AdvancedDisplay  : 默认折叠在"Advanced"区，避免新手误改
	 *
	 * 修改后会经过 UE::Mass::SanitizeChunkMemorySize() 校验/对齐（详见 MassArchetypeData.cpp:27）。
	 */
	UPROPERTY(EditDefaultsOnly, Category = Mass, config, AdvancedDisplay)
	uint32 ChunkMemorySize = 128 * 1024;

	/**
	 * The name of the file to dump the processor dependency graph. T
	 * The dot file will be put in the project log folder.
	 * To generate a svg out of that file, simply run dot executable with following parameters: -Tsvg -O filename.dot 
	 *
	 * 中文：将 DependencySolver 计算出的 processor 依赖图导出成 GraphViz .dot 文件的文件名。
	 *      留空则不导出。文件落在 Project/Saved/Logs/ 下。生成 svg：
	 *          dot -Tsvg -O <FileName>.dot
	 *
	 * 注意 Transient ：不写入 ini，每次启动重置为空，避免误把"调试导出开关"持久化。
	 */
	UPROPERTY(EditDefaultsOnly, Category = Mass, Transient)
	FString DumpDependencyGraphFileName;

	/**
	 * Lets users configure processing phases including the composite processor class to be used as a container for the phases' processors.
	 *
	 * 中文：每个 ProcessingPhase 的配置数组，长度固定为 EMassProcessingPhase::MAX。
	 *      数组下标 i 对应 EMassProcessingPhase(i) —— 这种"枚举做下标"的设计省去了 Map 查找。
	 *
	 * 单个 FMassProcessingPhaseConfig 的字段（详见 MassProcessingPhaseManager.h:78）：
	 *   - PhaseName        : 显示名（例如 "PrePhysics"）
	 *   - PhaseGroupClass  : 该 phase 用哪个 UMassCompositeProcessor 派生类做容器（高级定制点）
	 *   - ProcessorCDOs    : 该 phase 中要执行的 processor CDO 列表（编辑器中可视化勾选）
	 *   - Description      : 编辑器只读：该 phase 的依赖图描述文本
	 *   - PhaseProcessor   : 编辑器只读：实例化好的 CompositeProcessor，用于 UI 展示
	 *
	 * 'config' 标签 → 持久化到 ini；用户在 UI 上勾选哪个 processor 启用，会回写到 DefaultMass.ini。
	 */
	UPROPERTY(EditDefaultsOnly, Category = Mass, config)
	FMassProcessingPhaseConfig ProcessingPhasesConfig[(uint8)EMassProcessingPhase::MAX];

	/**
	 * This list contains all the processors available in the given binary (including plugins). The contents are sorted by display name.
	 *
	 * 中文：当前二进制（含已加载插件）中所有可用 processor CDO 的总览列表，按名字排序。
	 *      此列表是"全集"，phase 配置里的 ProcessorCDOs 是它的子集。
	 *
	 * UPROPERTY meta：
	 *   - VisibleAnywhere : 只读展示
	 *   - Transient       : 不持久化（启动时由 BuildProcessorList 动态扫描得到）
	 *   - Instanced       : 显示时使用每个 processor 实例自己的属性面板（而非通用 ObjectPicker）
	 *   - EditFixedSize   : 不允许在 UI 上 add/remove（由代码维护）
	 */
	UPROPERTY(VisibleAnywhere, Category = Mass, Transient, Instanced, EditFixedSize)
	TArray<TObjectPtr<UMassProcessor>> ProcessorCDOs;

#if WITH_EDITORONLY_DATA
protected:
	/** 编辑器专属：设置变更广播委托实例（外部通过 GetOnSettingsChange() 监听）。 */
	FOnSettingsChange OnSettingsChange;
#endif // WITH_EDITORONLY_DATA

	/** 是否已完成首次"扫描 + 构建 phase"。BuildProcessorListAndPhases() 用它做幂等保护。 */
	bool bInitialized = false;

	/** Engine 是否已走完 OnPostEngineInit。决定 BuildProcessorListAndPhases() 是否实际执行。 */
	bool bEngineInitialized = false;

	/** "初始化完成"广播委托实例。Broadcast 时机：BuildProcessorListAndPhases 末尾。 */
	FOnInitialized OnInitializedEvent;
};
