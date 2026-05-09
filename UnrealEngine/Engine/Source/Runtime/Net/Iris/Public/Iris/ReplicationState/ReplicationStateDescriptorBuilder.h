// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorBuilder.h —— 反射 → Descriptor 烘焙工厂的对外门面
// -------------------------------------------------------------------------------------
// 【FReplicationStateDescriptorBuilder】是 Iris 把 UClass / UStruct / UFunction 转换为
// FReplicationStateDescriptor 的唯一对外入口。三个工厂函数：
//
//   1) CreateDescriptorsForClass —— 一个 UClass 可能拆出 N 个 Descriptor（按高层条件
//      分块：Init / Owner / Skip-Owner / 无条件 / FastArray / ...），全部 push 进
//      OutCreatedDescriptors。
//   2) CreateDescriptorForStruct —— 一个 UStruct 烘焙成一个 Descriptor。Struct 有自定义
//      NetSerializer 时走特殊路径（IsStructWithCustomSerializer）。
//   3) CreateDescriptorForFunction —— 一个 UFunction 的参数列表烘焙成 Descriptor，
//      用于 RPC 序列化。
//
// 【FParameters】构建参数：
//   - DescriptorRegistry        ：可选缓存。Builder 先 Find，未命中再烘焙并 Register；
//   - ReplicationSystem         ：用于构造默认状态时的 export 上下文（NetTokens 等）；
//   - DefaultStateSource        ：默认状态来源 UObject（默认取 CDO）；
//   - IncludeSuper              ：构建 Class 时是否包含父类属性；
//   - GetLifeTimeProperties     ：是否从 CDO 取 GetLifetimeReplicatedProps（用于 conditionals）；
//   - EnableFastArrayHandling   ：识别 FFastArraySerializer 派生 → 走 FastArray 特化；
//   - AllowFastArrayWithExtraReplicatedProperties: FastArray 默认只支持单个数组属性，
//     此开关允许其他成员存在（绝大多数情况禁用）；
//   - SkipCheckForCustomNetSerializerForStruct: 即使该 struct 有自定义 NetSerializer，
//     仍按底层成员逐个烘焙（用于查看其内部结构）；
//   - SinglePropertyIndex       ：仅烘焙某一个属性，构建"单属性 Descriptor"（特殊场景）。
// =====================================================================================

#pragma once

#include "Containers/Array.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Templates/RefCounting.h"
#include "UObject/CoreNet.h"

class IConsoleVariable;
class UReplicationSystem;
namespace UE::Net::Private
{
	class FReplicationStateDescriptorRegistry;
}

namespace UE::Net
{

class FReplicationStateDescriptorBuilder
{
public:
	// 【FParameters】Builder 工厂参数。
	struct FParameters
	{
		IRISCORE_API FParameters();

		// 共享 Descriptor 缓存。命中时复用，未命中时烘焙完成后注册。
		Private::FReplicationStateDescriptorRegistry* DescriptorRegistry;
		// 当前 ReplicationSystem，用于：
		//   - 构造默认状态时的 NetToken / Export 上下文；
		//   - FastArray 路径取 NetRefHandleManager。
		// 离线烘焙（如 cooker）允许 nullptr。
		UReplicationSystem* ReplicationSystem = nullptr;

		// The template to build the state source from
		// 默认状态来源；nullptr 时 Builder 自动取 Class CDO。
		const UObject* DefaultStateSource = nullptr;

		 // Include super class when building descriptor.
		// 是否合并父类的可复制属性（默认 true，匹配传统 RepLayout 行为）。
		bool IncludeSuper : 1 = true;
		// Get the lifetime properties for conditionals if applicable. This applies only to classes.
		// 是否调用 GetLifetimeReplicatedProps 取条件信息。设为 false 表示忽略所有 ELifetimeCondition，
		// 退化为"所有属性都无条件"。
		bool GetLifeTimeProperties : 1 = true;
		// If EnableFastArrayHandling is true and the struct inherits from FFastArraySerializer then special logic will be applied which allows it to be bound to a FastArrayReplicationFragment.
		// 启用 FastArray 特化路径。设为 false 时即使是 FFastArraySerializer 派生也按普通 Struct 烘焙。
		bool EnableFastArrayHandling : 1 = true;
		// If AllowFastArrayWithExtraReplicatedProperties is set to true, we will allow building descriptors for fastarrays with more than a single property.
		// 允许 FastArray 含额外属性。默认 FastArray 必须只有一个 TArray<...Item> 属性。
		bool AllowFastArrayWithExtraReplicatedProperties : 1 = false;
		// In SkipCheckForCustomNetSerializer is true the descriptor will be built using the underlying representation even if a CustomNetSerializer is registered for the struct
		// 跳过 Struct 自定义 NetSerializer 检查（罕用，调试 / 嵌套结构展开）。
		bool SkipCheckForCustomNetSerializerForStruct : 1 = false;
		 // If SinglePropertyIndex != -1 we will create a state that only includes the specified property.
		// 仅取单个属性烘焙（按 RepIndex）。-1 表示烘焙全部属性。用于 SetPropertyCustomCondition 等单属性场景。
		int32 SinglePropertyIndex = -1;
	};

	// 烘焙结果：一组 TRefCountPtr<const Descriptor>。RefCountPtr 自动 AddRef/Release，
	// 调用方持有结果直至清理。
	typedef TArray<TRefCountPtr<const FReplicationStateDescriptor>> FResult;

	/**
		Create the descriptors from an optional template or the class CDO.

		Done by processing reflection information at runtime for existing replicated properties.

		Since we want to allow ReplicationStates to be shared as much as possible between different connection we do split them up based on
		feature requirements, filtering, conditionals etc. We also keep Init states separate.

		If a descriptor registry is provided, the functions below will search the registry before creating a new ReplicationStateDescriptor.
		Any created ReplicationStateDescriptor will be registered in the provided registry.

		Returns number of states created for the provided class.

		【CreateDescriptorsForClass】Class → 多个 Descriptor。
		拆分原则：把"具有相同高层条件 + 相同 OwnerOnly/SkipOwner/Init 等"的属性合并到
		同一个 Descriptor，最大化跨连接共享。Init State 单独成块（无 ChangeMask）。
		FastArray 属性独立成 Descriptor（用 FastArrayReplicationFragment 处理）。

		返回创建/缓存命中的 Descriptor 数量。
	*/
	IRISCORE_API static SIZE_T CreateDescriptorsForClass(FResult& OutCreatedDescriptors, UClass* InClass, const FParameters& Parameters = FParameters());

	/**
		Create ReplicationStateDescriptor for a struct

		If a descriptor registry is provided, the functions below will search the registry before creating a new ReplicationStateDescriptor.
		Any new created ReplicationStateDescriptor will be registered in the provided registry.

		【CreateDescriptorForStruct】Struct → 单 Descriptor。
		分支：
		  - Struct 有自定义 NetSerializer（IsStructWithCustomSerializer） → 整体走该 NetSerializer，
		    只生成"单成员 wrapper" Descriptor；
		  - Struct 派生自有自定义 NetSerializer 的父类 → IsDerivedFromStructWithCustomSerializer，
		    BaseStruct 指向父；
		  - 否则按普通 Struct 烘焙：递归处理每个 UPROPERTY。
	*/
	IRISCORE_API static TRefCountPtr<const FReplicationStateDescriptor> CreateDescriptorForStruct(const UStruct* InStruct, const FParameters& Parameters = FParameters());

	/**
	 * Create ReplicationStateDescriptor for a function
	 *
	 * If a descriptor registry is provided, the function below will search the registry before creating a new ReplicationStateDescriptor.
	 * Any new created ReplicationStateDescriptor will be registered in the provided registry.
	 *
	 * 【CreateDescriptorForFunction】UFunction 的参数列表 → Descriptor。
	 * 用于 RPC 序列化：每次发送/接收 RPC 时按此 Descriptor 量化/反量化参数。
	 */
	IRISCORE_API static TRefCountPtr<const FReplicationStateDescriptor> CreateDescriptorForFunction(const UFunction* Function, const FParameters& Parameters = FParameters());
};

}
