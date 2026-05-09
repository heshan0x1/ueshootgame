// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverRegistry.h】
//
// 定义 UMassObserverRegistry —— observer 系统的"配置层"。
//
// ## 单例模式
// UMassObserverRegistry 通过 UE 的 CDO（Class Default Object）机制实现单例：
//   UMassObserverRegistry::Get() 返回 GetDefault<UMassObserverRegistry>()
// 因此整个进程只有一个 Registry 实例，所有 observer 注册都汇集到这里。
//
// ## 角色
//   - 只保存"哪些 observer class 监听哪些类型 + 哪些 op"的配置（class 元数据，不实例化 processor）；
//   - FMassObserverManager.Initialize 时会读取此 Registry 并把每个 observer class 实例化为 processor。
//   - 一个 EntityManager 一个 ObserverManager；但所有 EntityManager 共享同一个 Registry。
//
// ## 数据结构
//   FObserverClassesMap = TMap<TObjectKey<const UScriptStruct>, TArray<FSoftClassPath>>
//     - key：被观察的 fragment / tag 类型（用 TObjectKey 而非 TObjectPtr 避免 GC 引用）；
//     - value：监听该类型的 observer class path 列表（FSoftClassPath，延迟解析）。
//
//   FragmentObserverMaps[op] / TagObserverMaps[op]：每个 op 一个 map。
//
// ## 旧 / 新 API 区别
// 旧版（5.7 之前）使用 FMassEntityObserverClassesMap（持有 TSubclassOf<UMassProcessor>），
// 新版改用 FSoftClassPath 以支持模块卸载场景下的优雅降级。
// =============================================================================

#pragma once

#include "MassEntityTypes.h"
#include "MassProcessor.h"
#include "UObject/ObjectKey.h"
#include "MassObserverRegistry.generated.h"

#define UE_API MASSENTITY_API


struct FMassObserverManager;

/**
 * A wrapper type for a TArray to support having map-of-arrays UPROPERTY members in FMassEntityObserverClassesMap
 */
// 【中文注释】---- 已废弃（5.7）：FMassProcessorClassCollection ----
// 旧版用此结构包装 TSubclassOf<UMassProcessor> 数组以满足 UPROPERTY 嵌套要求。
// 新版直接用 TArray<FSoftClassPath>，无需 USTRUCT 包装。
USTRUCT(meta = (Deprecated = "5.7", DepracationMessage = "FMassProcessorClassCollection is no longer being used and will be removed in the upcoming engine released"))
struct FMassProcessorClassCollection
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TSubclassOf<UMassProcessor>> ClassCollection;
};

/**
 * A wrapper type for a TMap to support having array-of-maps UPROPERTY members in UMassObserverRegistry
 */
// 【中文注释】---- 已废弃（5.7）：FMassEntityObserverClassesMap ----
// 旧版的 type → processor classes 映射结构。新版改用 FObserverClassesMap（非 USTRUCT）。
USTRUCT(meta = (Deprecated = "5.7", DepracationMessage = "FMassEntityObserverClassesMap is no longer being used and will be removed in the upcoming engine released"))
struct FMassEntityObserverClassesMap
{
	GENERATED_BODY()

	/** a helper accessor simplifying access while still keeping Container private */
	const TMap<TObjectPtr<const UScriptStruct>, FMassProcessorClassCollection>& operator*() const
	{
		return Container;
	}

	TMap<TObjectPtr<const UScriptStruct>, FMassProcessorClassCollection>& operator*()
	{
		return Container;
	}

private:
	UPROPERTY()
	TMap<TObjectPtr<const UScriptStruct>, FMassProcessorClassCollection> Container;
};

// =============================================================================
// 【UMassObserverRegistry - 全局 observer 注册表】
//
// CDO 单例：UMassObserverRegistry::Get() 总是返回 GetDefault<UMassObserverRegistry>()。
// 通过 UCLASS 机制保证：
//   - 只有一个实例（构造函数 check(HasAnyFlags(RF_ClassDefaultObject))）；
//   - 引擎启动时由 UObject 系统自动创建；
//   - 关闭时随 UObject 系统销毁。
//
// 注册流程：
//   1. UMassObserverProcessor 子类的 CDO 在 PostInitProperties 时调用 Register；
//   2. Register 内部调用 UMassObserverRegistry::GetMutable().RegisterObserver；
//   3. RegisterObserver 把 (ObservedType, op, ObserverClass) 三元组存入对应的 map。
//
// 使用流程：
//   1. EntityManager 创建时 ObserverManager.Initialize 被调用；
//   2. Initialize 读取 Registry.FragmentObserverMaps[op] / TagObserverMaps[op]；
//   3. 对每个 (type, [classes]) 条目，把 class 解析并实例化成 processor，组装到 pipeline。
// =============================================================================
UCLASS(MinimalAPI)
class UMassObserverRegistry : public UObject
{
	GENERATED_BODY()

public:
	UE_API UMassObserverRegistry();

	// 【中文注释】可写访问 Registry 单例。仅在注册阶段使用（往里加 observer 配置）。
	static UMassObserverRegistry& GetMutable() { return *GetMutableDefault<UMassObserverRegistry>(); }
	// 【中文注释】只读访问 Registry 单例。FMassObserverManager.Initialize 用此版本读配置。
	static const UMassObserverRegistry& Get() { return *GetDefault<UMassObserverRegistry>(); }

	// 【中文注释】注册一个 observer class 到 Registry。
	// 参数：
	//   - ObservedType：fragment 或 tag 的 UScriptStruct 指针；
	//   - OperationFlags：要监听的 op 位标志（多个 op 可合并）；
	//   - ObserverClass：observer processor 的 class（用 SoftClassPath 存储以便延迟解析）。
	// 内部：把 (type, classpath) 加入 FragmentObserverMaps 或 TagObserverMaps 的对应 op 槽。
	UE_API void RegisterObserver(TNotNull<const UScriptStruct*> ObservedType, uint8 OperationFlags, TSubclassOf<UMassProcessor> ObserverClass);

	// 【中文注释】上面 RegisterObserver 的便捷重载：参数 OperationFlags 用枚举类型而非裸 uint8。
	void RegisterObserver(TNotNull<const UScriptStruct*> ObservedType, EMassObservedOperationFlags OperationFlags, TSubclassOf<UMassProcessor> ObserverClass)
	{
		RegisterObserver(ObservedType, static_cast<uint8>(OperationFlags), ObserverClass);
	}

	// 【中文注释】单一 op 的便捷重载：把 EMassObservedOperation 转成对应的位标志。
	UE_API void RegisterObserver(const UScriptStruct& ObservedType, EMassObservedOperation Operation, TSubclassOf<UMassProcessor> ObserverClass);

	// 【中文注释】内部数据结构定义：type → [observer class paths]。
	// TObjectKey 用作 key：弱引用语义，不阻止 GC，模块卸载时可安全清理。
	// FSoftClassPath 用作 value：延迟解析的 class 引用，模块卸载后只是失效但不悬空。
	using FObserverClassesMap = TMap<TObjectKey<const UScriptStruct>, TArray<FSoftClassPath>>;
protected:

	// 【中文注释】模块卸载回调：当 game module hot-reload 时，
	// 移除归属于卸载包的 observer class 条目。防止悬空 SoftClassPath 引用。
	void OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages);

	FDelegateHandle ModulesUnloadedHandle;

	friend FMassObserverManager;
	// 【中文注释】fragment 维度的注册表数组：每个 op 一个 map。
	// 例如 FragmentObserverMaps[AddElement] 包含所有"监听 fragment 添加"的 observer class。
	FObserverClassesMap FragmentObserverMaps[static_cast<uint8>(EMassObservedOperation::MAX)];
	// 【中文注释】tag 维度的注册表数组，结构同 FragmentObserverMaps。
	FObserverClassesMap TagObserverMaps[static_cast<uint8>(EMassObservedOperation::MAX)];

	// 【中文注释】---- 已废弃（5.7）字段，保留是为了 UProperty 反序列化兼容 ----
	// 旧版本保存的项目配置可能引用这些字段；新代码只用 FragmentObserverMaps/TagObserverMaps。
	UE_DEPRECATED(5.7, "Use FragmentObserverMaps instead")
	UPROPERTY()
	FMassEntityObserverClassesMap FragmentObservers[(uint8)EMassObservedOperation::MAX];

	UE_DEPRECATED(5.7, "Use TagObserverMaps instead")
	UPROPERTY()
	FMassEntityObserverClassesMap TagObservers[(uint8)EMassObservedOperation::MAX];
};

#undef UE_API
