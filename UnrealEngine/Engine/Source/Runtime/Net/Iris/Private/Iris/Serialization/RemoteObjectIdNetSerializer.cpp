// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RemoteObjectIdNetSerializer.cpp —— FRemoteObjectId 序列化实现
// -----------------------------------------------------------------------------
// 协议：直接以 64 位定长写入（WriteUint64 / ReadUint64）。
// 量化：FRemoteObjectId 只是 64 位整数封装，因此 QuantizedType = uint64。
//
// 关键不变量：
//   • 序列化前 Quantize 必须 GetGlobalized()：把 ServerId 字段从 "Local 哨兵值"
//     替换回真实 ServerId。Local 永远不应出现在 wire 数据中（断言保证）。
//   • 反序列化后 Dequantize 必须 GetLocalized()：把恰好等于本机 ServerId 的
//     字段重置回 Local 哨兵，便于本地代码通过 IsLocal() 类查询快速判断。
// =============================================================================

#include "RemoteObjectIdNetSerializer.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "UObject/RemoteObjectTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RemoteObjectIdNetSerializer)

namespace UE::Net
{

struct FRemoteObjectIdNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Types
	typedef FRemoteObjectId SourceType;
	// 量化态直接为 uint64：FRemoteObjectId 内部仅含一个 Id 字段
	typedef uint64 QuantizedType;
	typedef FRemoteObjectIdNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	// 编译期保证 QuantizedType 与 FRemoteObjectId::Id 同尺寸
	static_assert(sizeof(QuantizedType) == sizeof(FRemoteObjectId::Id), "Quantized RemoteObjectId type is not the correct size.");

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FRemoteObjectIdNetSerializer);

const FRemoteObjectIdNetSerializer::ConfigType FRemoteObjectIdNetSerializer::DefaultConfig;

// 64 位定长写入。WriteUint64 在 NetBitStreamUtil 中是平台无关的字节序中性写法。
void FRemoteObjectIdNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	WriteUint64(Writer, Value);
}

// 64 位定长读取
void FRemoteObjectIdNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	Value = ReadUint64(Reader);
}

// 量化：先 GetGlobalized 把 Local 哨兵替换为真实 ServerId，再取出 64 位 Id 存入量化态。
// 断言：Globalize 后必不为 Local，否则说明本地数据出错。
void FRemoteObjectIdNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	FRemoteObjectId GlobalizedId = SourceValue.GetGlobalized();
	TargetValue = GlobalizedId.Id;

	FRemoteObjectId LocalizedId;
	LocalizedId.Id = TargetValue;
	checkf(LocalizedId.GetServerId().Id != (uint32)ERemoteServerIdConstants::Local, TEXT("Local server id should never be serialized"));
}

// 反量化：从 64 位 Id 拼回 FRemoteObjectId 后调用 GetLocalized，
// 让"恰好为本机 ServerId"的字段重新表示为 Local 哨兵，便于本地通用代码识别。
void FRemoteObjectIdNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	SourceType& TargetValue = *reinterpret_cast<SourceType*>(Args.Target);
	QuantizedType& QuantizedValue = *reinterpret_cast<QuantizedType*>(Args.Source);

	FRemoteObjectId LocalizedId;
	LocalizedId.Id = QuantizedValue;
	// 对端发来的数据中绝不应含 Local 哨兵
	checkf(LocalizedId.GetServerId().Id != (uint32)ERemoteServerIdConstants::Local, TEXT("Local server id should never be serialized"));
	TargetValue = LocalizedId.GetLocalized();
}

// 等值：量化态直接 64 位整数比较；源态走 FRemoteObjectId::operator==
bool FRemoteObjectIdNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
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

// 校验：ServerId 与 SerialNumber 应在合法范围内（位宽限制由 RemoteObjectTypes 定义）
bool FRemoteObjectIdNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);

	// Server ID 与 serial number 都应在合法范围内
	const bool bServerIdValid = SourceValue.GetServerId().GetIdNumber() <= MAX_REMOTE_OBJECT_SERVER_ID;
	const bool bSerialNumberValid = SourceValue.SerialNumber <= MAX_REMOTE_OBJECT_SERIAL_NUMBER;

	return bServerIdValid && bSerialNumberValid;
}

}
