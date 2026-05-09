// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// FieldPathNetSerializer.cpp —— FFieldPath 序列化
// -----------------------------------------------------------------------------
// FFieldPath 表示"指向某 FProperty 的反射路径"，含两部分：
//   * Owner: TWeakObjectPtr<UStruct>（属性所属的类/结构体）
//   * Path : TArray<FName>（嵌套路径段，逐级 inner）
// 因为 FFieldPath 内部成员 protected 且需要 ResolveCache 等私有逻辑，无法直接
// 让 StructNetSerializer 烘焙。本 serializer 通过 FFieldPathNetSerializerSerializationHelper
// 这个公开 USTRUCT 中转：
//   Quantize : FFieldPath → 拷贝 Owner+Path 到 Helper → StructNetSerializer.Quantize
//   Dequantize: 反向 → SetResolvedOwner / SetPath / ClearCachedField
//
// 序列化策略说明：
//   * 路径每段都是 FName，由 StructNetSerializer 内的 FName serializer 编码（一般 NetToken）；
//   * Owner 是弱引用，没解析的 path 没法用 → Quantize 阶段如 Owner 失效则 Helper 留空，
//     接收端等价于"没有此 field"。
//
// trait：
//   * bIsForwardingSerializer + bHasDynamicState（Helper 内 TArray 含 dynamic）
//   * bHasCustomNetReference（Owner 是 UStruct 引用，需收集）
// =============================================================================

#include "InternalNetSerializers.h"
#include "UObject/CoreNet.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"

namespace UE::Net
{

// 子类暴露 FFieldPath 的 protected 成员给我们用 —— 一种"向上转换 + friend"的便利做法。
struct FFieldPathForNetSerializer : public FFieldPath
{
public:
	TWeakObjectPtr<UStruct> GetResolvedOwner() const { return ResolvedOwner; }
	void SetResolvedOwner(const TWeakObjectPtr<UStruct>& InResolvedOwner) { ResolvedOwner = InResolvedOwner; }

	const TArray<FName>& GetPath() const { return Path; }
	void SetPath(const TArray<FName>& InPath) { Path = InPath; }

	void ClearCachedField() { FFieldPath::ClearCachedField(); }
};

struct FFieldPathNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Traits
	static constexpr bool bIsForwardingSerializer = true; // Triggers asserts if a function is missing
	static constexpr bool bHasDynamicState = true;
	static constexpr bool bHasCustomNetReference = true;

	// Types
	// 量化态：固定 40 字节、对齐 8。容纳 Helper 烘焙后的 ReplicationState（实际尺寸在
	// PostFreeze 阶段由 OnPostFreezeNetSerializerRegistry 验证不超出）。
	struct FQuantizedType
	{
		alignas(8) uint8 QuantizedStruct[40];
	};

	typedef FFieldPathForNetSerializer SourceType;
	typedef FQuantizedType QuantizedType;
	typedef FFieldPathNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	//
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	// 注册委托：在 PostFreeze 阶段烘焙 Helper struct 的 Descriptor 并校验量化态尺寸。
	class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
	{
	private:
		virtual void OnPostFreezeNetSerializerRegistry() override;
	};

	// FFieldPath → Helper 的拷贝 helper（仅在 Owner 有效时填充）。
	static void FieldPathToFieldPathNetSerializerSerializationHelper(const SourceType& FieldPath, FFieldPathNetSerializerSerializationHelper& OutStruct);

	static FFieldPathNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;
	// Helper struct 的 StructNetSerializerConfig，在 OnPostFreeze 中烘焙填充 StateDescriptor。
	static FStructNetSerializerConfig StructNetSerializerConfig;
	static const FNetSerializer* StructNetSerializer;
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FFieldPathNetSerializer);

const FFieldPathNetSerializer::ConfigType FFieldPathNetSerializer::DefaultConfig;

FFieldPathNetSerializer::FNetSerializerRegistryDelegates FFieldPathNetSerializer::NetSerializerRegistryDelegates;
FStructNetSerializerConfig FFieldPathNetSerializer::StructNetSerializerConfig;
const FNetSerializer* FFieldPathNetSerializer::StructNetSerializer = &UE_NET_GET_SERIALIZER(FStructNetSerializer);

// -----------------------------------------------------------------------------
// Serialize / Deserialize / Delta —— 全部直接转发给 StructNetSerializer
// -----------------------------------------------------------------------------
// 因为量化态里其实就是 Helper struct 的标准量化数据，让 StructNetSerializer 处理即可。
void FFieldPathNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	FNetSerializeArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->Serialize(Context, InternalArgs);
}

void FFieldPathNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetDeserializeArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->Deserialize(Context, InternalArgs);
}

void FFieldPathNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	FNetSerializeDeltaArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->SerializeDelta(Context, InternalArgs);
}

void FFieldPathNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	FNetDeserializeDeltaArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->DeserializeDelta(Context, InternalArgs);
}

// -----------------------------------------------------------------------------
// Quantize —— FFieldPath → Helper struct → 调 StructNetSerializer.Quantize
// -----------------------------------------------------------------------------
// 中转步骤：把 FFieldPath 中的 ResolvedOwner / Path 拷贝到 Helper 临时变量，
// 再喂给 StructNetSerializer 走标准量化路径。
void FFieldPathNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);

	FFieldPathNetSerializerSerializationHelper IntermediateValue;
	FieldPathToFieldPathNetSerializerSerializationHelper(SourceValue, IntermediateValue);

	FNetQuantizeArgs InternalArgs = Args;
	InternalArgs.Source = NetSerializerValuePointer(&IntermediateValue);
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->Quantize(Context, InternalArgs);
}

// -----------------------------------------------------------------------------
// Dequantize —— Helper struct ← StructNetSerializer.Dequantize → 写回 FFieldPath
// -----------------------------------------------------------------------------
// SetResolvedOwner / SetPath 后调 ClearCachedField 让 FFieldPath 下次访问时
// 重新解析（避免拿到旧 cache）。
void FFieldPathNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	SourceType& TargetValue = *reinterpret_cast<SourceType*>(Args.Target);

	FFieldPathNetSerializerSerializationHelper IntermediateValue;

	FNetDequantizeArgs InternalArgs = Args;
	InternalArgs.Target = NetSerializerValuePointer(&IntermediateValue);
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;

	StructNetSerializer->Dequantize(Context, InternalArgs);

	TargetValue.SetResolvedOwner(IntermediateValue.Owner);
	TargetValue.SetPath(IntermediateValue.PropertyPath);
	TargetValue.ClearCachedField();
}

bool FFieldPathNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		FNetIsEqualArgs InternalArgs = Args;
		InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
		return StructNetSerializer->IsEqual(Context, InternalArgs);
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		FFieldPathNetSerializerSerializationHelper IntermediateValue0;
		FFieldPathNetSerializerSerializationHelper IntermediateValue1;

		FieldPathToFieldPathNetSerializerSerializationHelper(SourceValue0, IntermediateValue0);
		FieldPathToFieldPathNetSerializerSerializationHelper(SourceValue1, IntermediateValue1);

		FNetIsEqualArgs InternalArgs = Args;
		InternalArgs.Source0 = NetSerializerValuePointer(&IntermediateValue0);
		InternalArgs.Source1 = NetSerializerValuePointer(&IntermediateValue1);
		InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
		return StructNetSerializer->IsEqual(Context, InternalArgs);
	}
}

bool FFieldPathNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);

	FFieldPathNetSerializerSerializationHelper IntermediateValue;
	FieldPathToFieldPathNetSerializerSerializationHelper(SourceValue, IntermediateValue);

	FNetValidateArgs InternalArgs = Args;
	InternalArgs.Source = NetSerializerValuePointer(&IntermediateValue);
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;

	return StructNetSerializer->Validate(Context, InternalArgs);
}

// -----------------------------------------------------------------------------
// CollectNetReferences —— 仅当 Helper 描述符标记 HasObjectReference 时才转发
// -----------------------------------------------------------------------------
// Owner 是 UStruct 弱引用，烘焙后会成为 ObjectReference 类型成员。这里直接
// 委托给 StructNetSerializer.CollectNetReferences。
void FFieldPathNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	using namespace Private;

	const FReplicationStateDescriptor* Descriptor = StructNetSerializerConfig.StateDescriptor;
	if (Descriptor && EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasObjectReference))
	{
		FNetCollectReferencesArgs InternalArgs = Args;
		InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
		StructNetSerializer->CollectNetReferences(Context, InternalArgs);
	}
}

void FFieldPathNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	FNetCloneDynamicStateArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->CloneDynamicState(Context, InternalArgs);
}

void FFieldPathNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	FNetFreeDynamicStateArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &StructNetSerializerConfig;
	return StructNetSerializer->FreeDynamicState(Context, InternalArgs);
}

// 拷贝 helper：Owner 失效（GC 已销毁）时 OutStruct 留空，等价空 path 序列化。
void FFieldPathNetSerializer::FieldPathToFieldPathNetSerializerSerializationHelper(const SourceType& FieldPath, FFieldPathNetSerializerSerializationHelper& OutStruct)
{
	const TWeakObjectPtr<UStruct>& Owner = FieldPath.GetResolvedOwner();
	// Without a valid owner we don't know the full path to the property and will not be able to resolve it.
	if (Owner.IsValid())
	{
		OutStruct.Owner = Owner;
		OutStruct.PropertyPath = FieldPath.GetPath();
	}
}

// PostFreeze 阶段烘焙 Helper struct 的 Descriptor，并校验量化态 buffer 尺寸/对齐
// 是否能容下烘焙结果。容不下时直接 LowLevelFatalError —— 这种情况是结构体改大
// 但 FQuantizedType 没跟上，是开发期 bug。
void FFieldPathNetSerializer::FNetSerializerRegistryDelegates::OnPostFreezeNetSerializerRegistry()
{
	UStruct* Struct = FFieldPathNetSerializerSerializationHelper::StaticStruct();
	StructNetSerializerConfig.StateDescriptor = FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Struct);
	const FReplicationStateDescriptor* Descriptor = StructNetSerializerConfig.StateDescriptor.GetReference();
	check(Descriptor != nullptr);

	// Validate our assumptions regarding quantized state size and alignment.
	static_assert(offsetof(FQuantizedType, QuantizedStruct) == 0U, "Expected buffer for struct to be first member of FQuantizedType.");
	if (sizeof(FQuantizedType::QuantizedStruct) < Descriptor->InternalSize || alignof(FQuantizedType) < Descriptor->InternalAlignment)
	{
		LowLevelFatalError(TEXT("FQuantizedType::QuantizedStruct has size %u and alignment %u but requires size %u and alignment %u."), uint32(sizeof(FQuantizedType::QuantizedStruct)), uint32(alignof(FQuantizedType)), uint32(Descriptor->InternalSize), uint32(Descriptor->InternalAlignment)); 
	}
}

}
