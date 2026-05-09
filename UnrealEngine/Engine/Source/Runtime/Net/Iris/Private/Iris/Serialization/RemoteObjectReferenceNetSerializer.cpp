// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RemoteObjectReferenceNetSerializer.cpp —— FRemoteObjectReference 序列化实现
// -----------------------------------------------------------------------------
// 本 Serializer 把 `FRemoteObjectReference` 切成 3 个子部分依次串行化：
//   (1) ObjectId   : 64 位（拆成两个 32 位写入避免 WriteBits 单次>32 位限制）
//   (2) ServerId   : REMOTE_OBJECT_SERVER_ID_BIT_SIZE 位
//   (3) PathName   : 通过 FStructNetSerializer + 运行期注册的 StateDescriptor
//                     转发到 FRemoteObjectPathName 的结构序列化
//
// 量化态 `FQuantizedRemoteObjectReference`（QuantizedRemoteObjectReference.h）：
//   uint64 ObjectId; uint16 ServerId; alignas(8) uint8 QuantizedPathNameStruct[32];
//   • 末尾 32 字节缓冲为 PathName 结构的量化态（FStructNetSerializer 内部状态）。
//   • 通过 `OnPostFreezeNetSerializerRegistry` 钩子在 NetSerializerRegistry 冻结
//     之后用 ReplicationStateDescriptorBuilder 构造 PathName 的描述符，并
//     fatal-check QuantizedPathNameStruct 缓冲足够。
//
// Traits：
//   • bIsForwardingSerializer：本类把工作转发到 ObjectPathNameNetSerializer，
//     必须保留所有钩子，否则触发断言。
//   • bHasDynamicState / bHasCustomNetReference：PathName 字符串持有动态分配；
//     CollectNetReferences 被显式留空，因为远端引用不参与 Iris 引用解析
//     管线（它们独立于 NetRefHandle 体系）。
//
// SerializeDelta / DeserializeDelta 也直接转发：路径用结构默认 delta 实现；
// ObjectId/ServerId 走全量重发（位数极少，无意义压缩）。
//
// 与 `bSerializeObjectReferencesAsRemoteIds` 联动：
//   ObjectNetSerializer 在该标志为 true 时，把普通 UObject* 属性的序列化
//   重路由到本类，从而把任意普通对象引用都按 RemoteObjectReference 形式上线。
// =============================================================================

#include "RemoteObjectReferenceNetSerializer.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/QuantizedRemoteObjectReference.h"
#include "UObject/RemoteObjectTransfer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RemoteObjectReferenceNetSerializer)

namespace UE::Net
{

struct FRemoteObjectReferenceNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Traits
	// 转发型 Serializer，需要全部钩子齐备否则在 IMPLEMENT 时断言失败
	static constexpr bool bIsForwardingSerializer = true;
	// 路径子结构持有动态分配
	static constexpr bool bHasDynamicState = true;
	// 自定义引用收集（实际为空，远端引用不进入 NetReferenceCollector）
	static constexpr bool bHasCustomNetReference = true;

	// Types
	typedef FRemoteObjectReference SourceType;
	typedef FQuantizedRemoteObjectReference QuantizedType;
	typedef FRemoteObjectReferenceNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	// 编译期保证 ServerId 字段宽度足够
	static_assert(sizeof(QuantizedType::ServerId) * 8 >= REMOTE_OBJECT_SERVER_ID_BIT_SIZE, "Quantized ServerId is not large enough to store maximum server ID");

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
	// Registry 委托：在 NetSerializerRegistry 冻结后回调，懒构造 PathName 的
	// ReplicationStateDescriptor 并校验量化缓冲尺寸。
	class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
	{
	private:
		virtual void OnPostFreezeNetSerializerRegistry() override;
	};

	static FRemoteObjectReferenceNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;
	// PathName 子结构序列化器使用的运行期 Config（其 StateDescriptor 在
	// OnPostFreezeNetSerializerRegistry 中填充）
	static FStructNetSerializerConfig RemoteObjectPathNameNetSerializerConfig;
	// 缓存通用 FStructNetSerializer 指针
	static const FNetSerializer* RemoteObjectPathNameNetSerializer;
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FRemoteObjectReferenceNetSerializer);

const FRemoteObjectReferenceNetSerializer::ConfigType FRemoteObjectReferenceNetSerializer::DefaultConfig;

FRemoteObjectReferenceNetSerializer::FNetSerializerRegistryDelegates FRemoteObjectReferenceNetSerializer::NetSerializerRegistryDelegates;
FStructNetSerializerConfig FRemoteObjectReferenceNetSerializer::RemoteObjectPathNameNetSerializerConfig;
const FNetSerializer* FRemoteObjectReferenceNetSerializer::RemoteObjectPathNameNetSerializer = &UE_NET_GET_SERIALIZER(FStructNetSerializer);

// 序列化协议：
//   ObjectId.lo32 : 32
//   ObjectId.hi32 : 32
//   ServerId      : REMOTE_OBJECT_SERVER_ID_BIT_SIZE
//   PathNameStruct: 由 FStructNetSerializer 按 PathName StateDescriptor 序列化
void FRemoteObjectReferenceNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// 64 位 Id 拆 32+32 写入（WriteBits 单次最多 32 位）
	static_assert(sizeof(Value.ObjectId) == 8, "Size of ObjectId expected to be 8 bytes");
	Writer->WriteBits(static_cast<uint32>(Value.ObjectId), 32U);
	Writer->WriteBits(static_cast<uint32>(Value.ObjectId >> 32U), 32U);

	Writer->WriteBits(Value.ServerId, REMOTE_OBJECT_SERVER_ID_BIT_SIZE);

	// 转发到 FStructNetSerializer 序列化 FRemoteObjectPathName
	FNetSerializeArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	InternalArgs.Source = NetSerializerValuePointer(&Value.QuantizedPathNameStruct[0]);
	RemoteObjectPathNameNetSerializer->Serialize(Context, InternalArgs);
}

// 反序列化：与 Serialize 对称，依次读三段。
void FRemoteObjectReferenceNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	Value.ObjectId = Reader->ReadBits(32U);
	Value.ObjectId |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;

	Value.ServerId = static_cast<uint16>(Reader->ReadBits(REMOTE_OBJECT_SERVER_ID_BIT_SIZE));

	FNetDeserializeArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	InternalArgs.Target = NetSerializerValuePointer(&Value.QuantizedPathNameStruct[0]);
	RemoteObjectPathNameNetSerializer->Deserialize(Context, InternalArgs);
}

// SerializeDelta：ObjectId/ServerId 走全量；仅 PathName 子结构尝试增量。
// 因为 ObjectId/ServerId 仅 80 位左右，做位级 delta 收益极小。
void FRemoteObjectReferenceNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	static_assert(sizeof(Value.ObjectId) == 8, "Size of ObjectId expected to be 8 bytes");
	Writer->WriteBits(static_cast<uint32>(Value.ObjectId), 32U);
	Writer->WriteBits(static_cast<uint32>(Value.ObjectId >> 32U), 32U);

	Writer->WriteBits(Value.ServerId, REMOTE_OBJECT_SERVER_ID_BIT_SIZE);

	// 转发到 FStructNetSerializer 增量序列化 FRemoteObjectPathName
	FNetSerializeDeltaArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	InternalArgs.Source = NetSerializerValuePointer(&Value.QuantizedPathNameStruct[0]);
	RemoteObjectPathNameNetSerializer->SerializeDelta(Context, InternalArgs);
}

void FRemoteObjectReferenceNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	Value.ObjectId = Reader->ReadBits(32U);
	Value.ObjectId |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;

	Value.ServerId = static_cast<uint16>(Reader->ReadBits(REMOTE_OBJECT_SERVER_ID_BIT_SIZE));

	FNetDeserializeDeltaArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	InternalArgs.Target = NetSerializerValuePointer(&Value.QuantizedPathNameStruct[0]);
	RemoteObjectPathNameNetSerializer->DeserializeDelta(Context, InternalArgs);
}

// 量化：
//   • 拆出 ObjectId/ServerId（注意 GetIdNumber 等接口可能未做 globalize；本量化态
//     存储的是"全局"形式数值，需结合 SerializeDelta 与 RemoteObjectId/ServerId
//     Serializer 的对比理解）；
//   • 若该对象当前在本进程已存在（FindObjectFastInternal），通过
//     UE::RemoteObject::Transfer::RegisterSharedObject 注册为共享对象，并取其
//     当前 PathName 用于上线；这样接收端在缺失对象时可按 PathName 重建代理。
void FRemoteObjectReferenceNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	TargetValue.ObjectId = SourceValue.GetRemoteId().GetIdNumber();
	TargetValue.ServerId = static_cast<uint16>(SourceValue.GetSharingServerId().GetIdNumber());

	FRemoteObjectPathName PathName;
	if (SourceValue.GetRemoteId().IsValid())
	{
		// 若对象当前在本进程存在，注册为共享对象并提取最新路径名
		if (UObject* ExistingObject = StaticFindObjectFastInternal(SourceValue.GetRemoteId()))
		{
			UE::RemoteObject::Transfer::RegisterSharedObject(ExistingObject);
			PathName = FRemoteObjectPathName(ExistingObject);
		}
	}

	// PathName 子结构走 StructNetSerializer 量化（按 RemoteObjectPathName 描述符）
	FNetQuantizeArgs InternalArgs = Args;
	InternalArgs.Source = NetSerializerValuePointer(&PathName);
	InternalArgs.Target = NetSerializerValuePointer(&TargetValue.QuantizedPathNameStruct[0]);
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	return RemoteObjectPathNameNetSerializer->Quantize(Context, InternalArgs);
}

// 反量化：拼回 FRemoteObjectReference。
//   • 把 ObjectId 与 ServerId 通过 GetLocalized() 还原（恰好等于本机的 ID 变 Local 哨兵）；
//   • PathName 子结构由 StructNetSerializer 反量化；
//   • 最终通过 NetDequantize() 将三者合成 FRemoteObjectReference。
void FRemoteObjectReferenceNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	SourceType& TargetValue = *reinterpret_cast<SourceType*>(Args.Target);
	QuantizedType& QuantizedValue = *reinterpret_cast<QuantizedType*>(Args.Source);

	FRemoteObjectId ObjectId;
	ObjectId.Id = QuantizedValue.ObjectId;

	FRemoteObjectPathName PathName;

	FNetDequantizeArgs InternalArgs = Args;
	InternalArgs.Source = NetSerializerValuePointer(&QuantizedValue.QuantizedPathNameStruct[0]);
	InternalArgs.Target = NetSerializerValuePointer(&PathName);
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;

	RemoteObjectPathNameNetSerializer->Dequantize(Context, InternalArgs);

	// FRemoteObjectReference::NetDequantize 内部决定如何根据三元组回构对象/代理
	TargetValue.NetDequantize(ObjectId.GetLocalized(), FRemoteServerId::FromIdNumber(QuantizedValue.ServerId).GetLocalized(), PathName);
}

// 等值：
//   • 量化态：依赖 FQuantizedRemoteObjectReference::operator==（仅比 ObjectId）。
//     这意味着相同对象但不同 ServerId/PathName 仍判等。设计意图：以 ObjectId
//     为权威标识，避免因路径短暂不同步而误判脏。
//   • 源态：使用 FRemoteObjectReference::operator==
bool FRemoteObjectReferenceNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& QuantizedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& QuantizedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		return QuantizedValue0 == QuantizedValue1;
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		return SourceValue0 == SourceValue1;
	}
}

// 校验：RemoteId 与 SharingServerId 必须同时有效或同时无效（避免半合法状态）。
bool FRemoteObjectReferenceNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);

	// 远端引用的 ObjectId 与 ServerId 必须同时有效或同时无效
	return SourceValue.GetRemoteId().IsValid() == SourceValue.GetSharingServerId().IsValid();
}

// 远端引用不进入 Iris 引用解析管线；NetReferenceCollector 留空。
void FRemoteObjectReferenceNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	// 远端引用无需经过此处的引用收集流程
}

// 克隆动态状态：ObjectId/ServerId 直接拷贝；PathName 子结构走结构 Serializer 的克隆路径
void FRemoteObjectReferenceNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	QuantizedType& SourceValue = *reinterpret_cast<QuantizedType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	TargetValue.ObjectId = SourceValue.ObjectId;
	TargetValue.ServerId = SourceValue.ServerId;

	FNetCloneDynamicStateArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	InternalArgs.Source = NetSerializerValuePointer(&SourceValue.QuantizedPathNameStruct[0]);
	InternalArgs.Target = NetSerializerValuePointer(&TargetValue.QuantizedPathNameStruct[0]);
	return RemoteObjectPathNameNetSerializer->CloneDynamicState(Context, InternalArgs);
}

// 释放动态状态：仅 PathName 子结构持有动态分配
void FRemoteObjectReferenceNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& SourceValue = *reinterpret_cast<QuantizedType*>(Args.Source);

	FNetFreeDynamicStateArgs InternalArgs = Args;
	InternalArgs.NetSerializerConfig = &RemoteObjectPathNameNetSerializerConfig;
	InternalArgs.Source = NetSerializerValuePointer(&SourceValue.QuantizedPathNameStruct[0]);
	return RemoteObjectPathNameNetSerializer->FreeDynamicState(Context, InternalArgs);
}

// 在 NetSerializerRegistry 冻结后构建 FRemoteObjectPathName 的 StateDescriptor，
// 并 fatal-check 量化态预留缓冲足够。
// 此回调由 FNetSerializerRegistryDelegates 框架在所有 Serializer 注册完成后调用，
// 时机晚于 PreFreeze 但早于 ReplicationSystem 业务初始化。
void FRemoteObjectReferenceNetSerializer::FNetSerializerRegistryDelegates::OnPostFreezeNetSerializerRegistry()
{
	UStruct* Struct = TBaseStructure<FRemoteObjectPathName>::Get();
	RemoteObjectPathNameNetSerializerConfig.StateDescriptor = FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Struct);
	const FReplicationStateDescriptor* Descriptor = RemoteObjectPathNameNetSerializerConfig.StateDescriptor.GetReference();
	check(Descriptor != nullptr);

	// 校验量化态尺寸/对齐假设
	static_assert(offsetof(QuantizedType, QuantizedPathNameStruct) == 16U, "Expected buffer for struct to be at offset 16 of QuantizedType.");
	if (sizeof(QuantizedType::QuantizedPathNameStruct) < Descriptor->InternalSize || alignof(QuantizedType) < Descriptor->InternalAlignment)
	{
		// 量化缓冲不足：直接 fatal，提示需调整 QuantizedRemoteObjectReference.h 中的 32 字节预留
		LowLevelFatalError(TEXT("QuantizedType::QuantizedStruct for FRemoteObjectReferenceNetSerializer has size %u and alignment %u but requires size %u and alignment %u."), uint32(sizeof(QuantizedType::QuantizedPathNameStruct)), uint32(alignof(QuantizedType)), uint32(Descriptor->InternalSize), uint32(Descriptor->InternalAlignment));
	}
}

}
