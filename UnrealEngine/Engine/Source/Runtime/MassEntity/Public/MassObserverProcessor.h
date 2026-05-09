// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverProcessor.h】
//
// 定义 observer processor 的基类与运行时上下文：
//
//   - FMassObserverExecutionContext：observer 执行时通过 ExecutionContext.AuxData 访问的上下文，
//     携带"当前是哪种操作 + 涉及哪些类型 + 当前迭代到哪个类型"等信息。
//
//   - UMassObserverProcessor：所有用户自定义 observer 的基类。子类只需声明：
//       1. ObservedType：监听哪个 fragment / tag 类型；
//       2. ObservedOperations：监听哪些操作（位标志，可同时监听 Add | Remove 等）；
//     然后实现父类 UMassProcessor 的 Execute / ConfigureQueries 即可。
//     CDO 在 PostInitProperties 时自动注册到 UMassObserverRegistry（除非禁用 bAutoRegisterWithObserverRegistry）。
//
// 与 ObserverManager 的关系：
//   - UMassObserverProcessor（UCLASS）：定义"什么 observer"
//   - UMassObserverRegistry（CDO）：保存"observer class → 监听的 type/op"映射
//   - FMassObserverManager（USTRUCT）：每个 EntityManager 的运行时层，从 Registry 实例化 processor
// =============================================================================

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.generated.h"

#define UE_API MASSENTITY_API


namespace UE::Mass::ObserverManager
{
	struct FObserverContextIterator;
}

/**
 * Instances of the type will be fed into FMassRuntimePipeline.AuxData and at execution time will
 * be available to observer processors via FMassExecutionContext.GetAuxData() 
 */
// =============================================================================
// 【FMassObserverExecutionContext - observer 执行上下文】
//
// 在 observer processor 被触发执行时，FMassObserverManager::HandleElementsImpl 会把
// 一个 FMassObserverExecutionContext 实例放进 FMassExecutionContext.AuxData。
// observer processor 的 Execute() 中可以通过 GetAuxData<FMassObserverExecutionContext>() 取出此对象，
// 然后查询：
//   - GetOperationType()：当前是哪种操作（Add/Remove/Create/Destroy）；
//   - GetTypesInOperation()：本次操作涉及的所有 fragment/tag 类型列表；
//   - GetCurrentType()：当前迭代到的那个具体 type（observer 可能监听多个 type）。
//
// 为什么需要这些信息？同一个 observer class 可能监听"X 或 Y 任一被加上"，
// 当一次组合变更同时加 X 和 Y 时，processor 会被触发两次，每次 GetCurrentType() 不同。
// =============================================================================
USTRUCT()
struct FMassObserverExecutionContext
{
	GENERATED_BODY()

	FMassObserverExecutionContext() = default;
	// 【中文注释】构造时记录操作类型与本次涉及的所有 type。CurrentTypeIndex 在迭代过程中由 FObserverContextIterator 维护。
	FMassObserverExecutionContext(const EMassObservedOperation InOperation, const TArrayView<const UScriptStruct*> InTypesInOperation)
		: Operation(InOperation), TypesInOperation(InTypesInOperation)
	{	
	}

	// 【中文注释】当前操作类型：Add / Remove / Create / Destroy 等。
	EMassObservedOperation GetOperationType() const
	{
		return Operation;
	}

	// 【中文注释】本次操作涉及的所有 type 列表。例如一次同时加 5 个 fragment，列表里就有 5 个元素。
	TConstArrayView<const UScriptStruct*> GetTypesInOperation() const
	{
		return TypesInOperation;
	}

	// 【中文注释】当前正在被处理的 type（迭代器位置）。
	// observer processor 可据此区分"为什么我被触发"。
	const UScriptStruct* GetCurrentType() const
	{
		return TypesInOperation[CurrentTypeIndex];
	}

	// 【中文注释】上下文是否有效：操作类型在合法范围内 + CurrentTypeIndex 未越界。
	// 用作 FObserverContextIterator 的循环条件。
	bool IsValid() const
	{
		return Operation <  EMassObservedOperation::MAX
			&& TypesInOperation.IsValidIndex(CurrentTypeIndex);
	}

private:
	// 【中文注释】FObserverContextIterator 是 friend：它修改 CurrentTypeIndex 推进迭代。
	// 其他类型不允许直接修改这些字段，只能通过 Get* 访问。
	friend UE::Mass::ObserverManager::FObserverContextIterator;
	// 【中文注释】操作类型，由构造函数设定。MAX 表示未初始化。
	EMassObservedOperation Operation = EMassObservedOperation::MAX;
	// 【中文注释】涉及的 type 列表。注意是 TArrayView —— 数据所有权在 ObserverManager 的栈缓冲里，
	// 此对象生命周期严格短于 HandleElementsImpl 调用期。
	TArrayView<const UScriptStruct*> TypesInOperation;
	// 【中文注释】当前迭代位置。INDEX_NONE 表示未开始；FObserverContextIterator 构造时置 0。
	int32 CurrentTypeIndex = INDEX_NONE;
};

/**
 * Base class for Processors that are used as "observers" of entity operations.
 * An observer declares the type of Mass element it cares about (Fragments and Tags supported at the moment) - via
 * the ObservedType property - and the types of operations it wants to be notified of - via ObservedOperations.
 *
 * When an observed operation takes place the processor's regular execution will take place, with
 * ExecutionContext's "auxiliary data" (obtained by calling GetAuxData) being filled with an instance of FMassObserverExecutionContext,
 * that can be used to get information about the type being handled and the kind of operation. 
 */
// =============================================================================
// 【UMassObserverProcessor - observer processor 基类】
//
// 用户自定义 observer 的基类。继承时必须：
//   1. 在子类构造函数中给 ObservedType 赋值（如 FMyHealthFragment::StaticStruct()）；
//   2. 给 ObservedOperations 赋值（如 EMassObservedOperationFlags::Add）；
//   3. 实现 UMassProcessor 的 ConfigureQueries / Execute 方法。
//
// CDO（class default object）在 PostInitProperties 时会自动调用 Register() —— 把自己注册到
// UMassObserverRegistry 单例。运行时 FMassObserverManager.Initialize() 读取 Registry 实例化 processor。
//
// abstract：本基类不可直接实例化，必须有具体子类。
// =============================================================================
UCLASS(MinimalAPI, abstract)
class UMassObserverProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UE_API UMassObserverProcessor();

	// 【中文注释】当前监听的操作位标志。多个 op 可用 | 组合（如 Add | Remove）。
	EMassObservedOperationFlags GetObservedOperations() const;
	// 【中文注释】当前监听的 fragment / tag 类型。返回 TNotNull 保证非空（断言保护）。
	TNotNull<const UScriptStruct*> GetObservedTypeChecked() const;

protected:
	// 【中文注释】UObject 后初始化钩子：在 CDO 完成属性反序列化后调用。
	// 此处自动触发 Register() 把自己加到 UMassObserverRegistry。
	UE_API virtual void PostInitProperties() override;

	/** 
	 * By default, registers this class as Operation observer of ObservedType. Override to register for multiple 
	 * operations and/or types 
	 */
	// 【中文注释】注册函数：默认实现把自己注册到 UMassObserverRegistry。
	// 子类可 override 以注册到多个 type 或自定义注册逻辑（例如根据配置动态选择 type）。
	UE_API virtual void Register();

protected:
	// 【中文注释】是否自动注册到 ObserverRegistry。默认为 true；
	// 可以通过 ini 配置或在子类构造函数中关闭，配合手动 Register() 用于特殊场景。
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	bool bAutoRegisterWithObserverRegistry = true;

	/** Determines which Fragment or Tag type this given UMassObserverProcessor will be observing */
	// 【中文注释】被观察的 fragment 或 tag 类型。子类构造函数中赋值。
	// 是 UPROPERTY 所以会被序列化（CDO 时已就位）。
	UPROPERTY()
	TObjectPtr<const UScriptStruct> ObservedType = nullptr;

	// 【中文注释】---- 已废弃（5.7）----
	// 旧版只支持单一 op，新版用 ObservedOperations（位标志）支持多 op。
	// PostInitProperties 中会自动迁移：如果旧 Operation 字段非默认值，会转成对应的 flag 写入 ObservedOperations。
	UE_DEPRECATED(5.7, "UMassObserverProcessor::Operation is deprecated. Use ObservedOperations instead")
	EMassObservedOperation Operation = EMassObservedOperation::MAX;

	// 【中文注释】被观察的操作位标志，子类构造函数中赋值。可用 | 组合多个 op。
	// 例如 ObservedOperations = EMassObservedOperationFlags::Add | EMassObservedOperationFlags::Remove
	// 表示同时监听添加与移除事件。
	EMassObservedOperationFlags ObservedOperations = EMassObservedOperationFlags::None;
};


//----------------------------------------------------------------------//
// inlines
//----------------------------------------------------------------------//
inline EMassObservedOperationFlags UMassObserverProcessor::GetObservedOperations() const
{
	return ObservedOperations;
}

inline TNotNull<const UScriptStruct*> UMassObserverProcessor::GetObservedTypeChecked() const
{
	return ObservedType;
}

#undef UE_API
