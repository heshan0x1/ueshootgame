// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RemoteServerIdNetSerializer.cpp —— FRemoteServerId 序列化实现
// -----------------------------------------------------------------------------
// 协议：仅写出 REMOTE_OBJECT_SERVER_ID_BIT_SIZE 位（远小于 16，按集群规模决定）。
// 量化：把 Local 哨兵 globalize 为真实 ServerId；反量化反之。
// =============================================================================

#include "RemoteServerIdNetSerializer.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "UObject/RemoteObjectTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RemoteServerIdNetSerializer)

namespace UE::Net
{

struct FRemoteServerIdNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Types
	typedef FRemoteServerId SourceType;
	// 量化态选 uint16 即可覆盖 REMOTE_OBJECT_SERVER_ID_BIT_SIZE（最多 16 位）
	typedef uint16 QuantizedType;
	typedef FRemoteServerIdNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	// 编译期保证 uint16 足以容纳最大 ServerId 位宽
	static_assert(sizeof(QuantizedType) * 8U >= REMOTE_OBJECT_SERVER_ID_BIT_SIZE, "Quantized ServerId is not large enough to store maximum server ID");

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FRemoteServerIdNetSerializer);

const FRemoteServerIdNetSerializer::ConfigType FRemoteServerIdNetSerializer::DefaultConfig;

// 写出 REMOTE_OBJECT_SERVER_ID_BIT_SIZE 位
void FRemoteServerIdNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	Writer->WriteBits(Value, REMOTE_OBJECT_SERVER_ID_BIT_SIZE);
}

// 读取 REMOTE_OBJECT_SERVER_ID_BIT_SIZE 位
void FRemoteServerIdNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	Value = static_cast<uint16>(Reader->ReadBits(REMOTE_OBJECT_SERVER_ID_BIT_SIZE));
}

// 量化：Localize → Globalize 转换。永远不允许 Local 哨兵进入 wire（断言）。
void FRemoteServerIdNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	FRemoteServerId GlobalizedId = SourceValue.GetGlobalized();
	TargetValue = static_cast<QuantizedType>(GlobalizedId.Id);

	checkf(TargetValue != static_cast<QuantizedType>(ERemoteServerIdConstants::Local), TEXT("Local server id should never be serialized"));
}

// 反量化：Globalize → Localize。把"恰好等于本机 ServerId 编号"还原为 Local 哨兵。
void FRemoteServerIdNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	SourceType& TargetValue = *reinterpret_cast<SourceType*>(Args.Target);
	QuantizedType& QuantizedValue = *reinterpret_cast<QuantizedType*>(Args.Source);

	checkf(QuantizedValue != static_cast<QuantizedType>(ERemoteServerIdConstants::Local), TEXT("Local server id should never be serialized"));

	FRemoteServerId LocalizedId;
	LocalizedId.Id = QuantizedValue;
	TargetValue = LocalizedId.GetLocalized();
}

// 等值：量化态 uint16 比较；源态走 FRemoteServerId::operator==
bool FRemoteServerIdNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
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

// 校验：ServerId 必须在 [0, MAX_REMOTE_OBJECT_SERVER_ID] 范围内
bool FRemoteServerIdNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);

	// Server ID 必须在合法范围内
	return SourceValue.GetIdNumber() <= MAX_REMOTE_OBJECT_SERVER_ID;
}

}
