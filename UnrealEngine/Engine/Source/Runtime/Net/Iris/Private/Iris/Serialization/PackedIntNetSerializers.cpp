// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/PackedIntNetSerializers.h"
#include "Iris/Core/BitTwiddling.h"
#include "Iris/Serialization/BitPacking.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PackedIntNetSerializers)

namespace UE::Net
{

/**
 * 64-bit Packed 整型 Serializer 的共享基类——提供 delta 编码用的位宽表。
 *
 * DeltaBitCountTable = {0, 14, 32}：
 *  - 索引 0：差值 0 bit（"同值优化"，常用于帧间数据几乎不变的情况）；
 *  - 索引 1：差值 14 bit，足以覆盖一般小范围变化；
 *  - 索引 2：差值 32 bit，应对较大跳变（上层若需要更宽会走 fallback）。
 * 由于只有 3 档，SerializeIntDelta/SerializeUintDelta 需要写 2 bit 索引。
 */
struct FPackedInt64NetSerializerBase
{
	static constexpr SIZE_T DeltaBitCountTableEntryCount = 3;
	// Bit counts aiming to have small value changes use few bits.
	static constexpr uint8 DeltaBitCountTable[] = {0, 14, 32};
};

/**
 * 64-bit 有符号打包整型 Serializer。
 *
 * 格式：3 bit "字节数-1"（即 0..7 表示 1..8 字节）+ 字节数*8 bit 的值。
 *  - 值 0 编码为 0b000 + 8 bit 0 = 11 bit；
 *  - 满 64 bit 编码为 0b111 + 64 bit = 67 bit（比原生 64 bit 多 3 bit，这是最坏情况的固有代价）。
 * 写入时利用 GetBitsNeeded 算出最少字节数；读取时先读 3 bit 得字节数，再按字节数 * 8 读值，
 * 最后用 (x ^ SignMask) - SignMask 做符号扩展。
 */
struct FPackedInt64NetSerializer : public FPackedInt64NetSerializerBase
{
	// 协议版本号：格式变更需递增。
	static const uint32 Version = 0;

	typedef int64 SourceType;
	typedef FPackedInt64NetSerializerConfig ConfigType;

	// Packed 系列无需 Config 字段，DefaultConfig 为空实例。
	inline static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);
};
// 宏展开定义 FPackedInt64NetSerializerInfo::Serializer 等成员。
UE_NET_IMPLEMENT_SERIALIZER(FPackedInt64NetSerializer);

/**
 * 64-bit 无符号打包整型 Serializer。
 *
 * 与有符号版几乎对称，区别是：
 *  - 编码时 `GetBitsNeeded(Value | 1U)` 保证最少 1 bit，避免 ByteCountNeeded-1 下溢（Value==0 的情况）；
 *  - 解码时不做符号扩展。
 */
struct FPackedUint64NetSerializer : public FPackedInt64NetSerializerBase
{
	static const uint32 Version = 0;

	typedef uint64 SourceType;
	typedef FPackedUint64NetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);
};
UE_NET_IMPLEMENT_SERIALIZER(FPackedUint64NetSerializer);

/**
 * 32-bit Packed 整型 Serializer 的共享基类。
 *
 * DeltaBitCountTable = {0, 4, 14}：
 *  - 索引 0：同值优化；
 *  - 索引 1：4 bit 小改动（例如 hp、弹药这类 ±10 级变化）；
 *  - 索引 2：14 bit，覆盖较大变化。
 */
struct FPackedInt32NetSerializerBase
{
	static constexpr SIZE_T DeltaBitCountTableEntryCount = 3;
	// Bit counts aiming to have small value changes use few bits.
	static constexpr uint8 DeltaBitCountTable[] = {0, 4, 14};
};

/**
 * 32-bit 有符号打包整型 Serializer。
 *
 * 格式：2 bit "字节数-1"（0..3 表示 1..4 字节）+ 对应 8/16/24/32 bit 的值。读取侧对无符号值做符号扩展。
 */
struct FPackedInt32NetSerializer : public FPackedInt32NetSerializerBase
{
	static const uint32 Version = 0;

	typedef int32 SourceType;
	typedef FPackedInt32NetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);
};
UE_NET_IMPLEMENT_SERIALIZER(FPackedInt32NetSerializer);

/**
 * 32-bit 无符号打包整型 Serializer。格式与有符号版相同，但无需符号扩展；
 * 编码时同样加 `|1U` 保证 ByteCountNeeded >= 1。
 */
struct FPackedUint32NetSerializer : public FPackedInt32NetSerializerBase
{
	static const uint32 Version = 0;

	typedef uint32 SourceType;
	typedef FPackedUint32NetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);
};
UE_NET_IMPLEMENT_SERIALIZER(FPackedUint32NetSerializer);

// ============================================================================
// FPackedInt64NetSerializer implementation
// ============================================================================

/**
 * 写入格式：3 bit "字节数-1" + 字节数*8 bit 值。
 * GetBitsNeeded(Value) 对 int64 返回含符号位的最少位数（对 0 返回 1）。
 * 大于 32 bit 时分两段写入（受 WriteBits 单次最大 32 bit 限制）。
 */
void FPackedInt64NetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const int64 Value = *reinterpret_cast<const int64*>(Args.Source);

	const uint32 BitCountNeeded = GetBitsNeeded(Value);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U) / 8U;
	const uint32 BitCountToWrite = ByteCountNeeded * 8U;

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(ByteCountNeeded - 1U, 3U);
	if (BitCountToWrite <= 32U)
	{
		Writer->WriteBits(static_cast<uint64>(Value) & 0xFFFFFFFFU, BitCountToWrite);
	}
	else
	{
		// 值需要 >32 bit：先写低 32 bit，再写剩余 (BitCountToWrite-32) bit。
		const uint64 UnsignedValue = static_cast<uint64>(Value);
		Writer->WriteBits(UnsignedValue & 0xFFFFFFFFU, 32U);
		Writer->WriteBits(static_cast<uint32>(UnsignedValue >> 32U), BitCountToWrite - 32U);
	}
}

/**
 * 对称读取：先读 3 bit 字节数-1，再读值，最后做 BitCountToRead 宽度的符号扩展。
 */
void FPackedInt64NetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const uint32 ByteCountToRead = Reader->ReadBits(3U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead * 8U;

	uint64 UnsignedValue;
	if (BitCountToRead <= 32U)
	{
		UnsignedValue = Reader->ReadBits(BitCountToRead);
	}
	else
	{
		UnsignedValue = Reader->ReadBits(32U);
		UnsignedValue |= (static_cast<uint64>(Reader->ReadBits(BitCountToRead - 32U)) << 32U);
	}

	// 符号扩展（经典无分支技巧）：若顶位为 1，填满高位 1；否则保留原值。
	const uint64 Mask = 1ULL << (BitCountToRead - 1U);
	UnsignedValue = (UnsignedValue ^ Mask) - Mask;
	const int64 Value = static_cast<int64>(UnsignedValue);

	int64* Target = reinterpret_cast<int64*>(Args.Target);
	*Target = Value;
}

/** Delta 写出：复用 BitPacking::SerializeIntDelta + 本类的 DeltaBitCountTable。MaxBitCount=64 是值域上限。 */
void FPackedInt64NetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const int64 Value = *reinterpret_cast<const int64*>(Args.Source);
	const int64 PrevValue = *reinterpret_cast<const int64*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeIntDelta(*Writer, Value, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 64U);
}

/** Delta 读取：与 SerializeDelta 对称。 */
void FPackedInt64NetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	int64& Target = *reinterpret_cast<int64*>(Args.Target);
	const int64 PrevValue = *reinterpret_cast<const int64*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	DeserializeIntDelta(*Reader, Target, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 64U);
}

// ============================================================================
// FPackedUint64NetSerializer implementation
// ============================================================================

/**
 * 格式同有符号版（3 bit 字节数-1 + 字节数*8 bit 值），但 GetBitsNeeded(Value | 1U) 保证 ByteCountNeeded >= 1。
 *  - 因为 uint64 GetBitsNeeded(0) = 0 会导致 ByteCountNeeded-1 下溢，|1 避免该边界。
 *  - Value=0 时最终编码为 0b000 + 8 bit 0 = 11 bit。
 */
void FPackedUint64NetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const uint64 Value = *reinterpret_cast<const uint64*>(Args.Source);

	// As we represent the number of bytes to write with two bits we want bits needed to be >= 1
	// 注意：注释原写"two bits"，64-bit 版实际使用 3 bit（见 WriteBits(ByteCountNeeded-1, 3U)）。此处为历史遗留文案。
	const uint32 BitCountNeeded = GetBitsNeeded(Value | 1U);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U) / 8U;
	const uint32 BitCountToWrite = ByteCountNeeded * 8U;

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(ByteCountNeeded - 1U, 3U);
	if (BitCountToWrite <= 32U)
	{
		Writer->WriteBits(Value & 0xFFFFFFFFU, BitCountToWrite);
	}
	else
	{
		Writer->WriteBits(Value & 0xFFFFFFFFU, 32U);
		Writer->WriteBits(static_cast<uint32>(Value >> 32U), BitCountToWrite - 32U);
	}
}

/** 对称读取（无符号版无需符号扩展）。 */
void FPackedUint64NetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const uint32 ByteCountToRead = Reader->ReadBits(3U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead * 8U;

	uint64 Value;
	if (BitCountToRead <= 32)
	{
		Value = Reader->ReadBits(BitCountToRead);
	}
	else
	{
		Value = Reader->ReadBits(32U);
		Value |= (static_cast<uint64>(Reader->ReadBits(BitCountToRead - 32U)) << 32U);
	}

	uint64* Target = reinterpret_cast<uint64*>(Args.Target);
	*Target = Value;
}

/** Delta 写出：使用 SerializeUintDelta（无符号差值编码，避免浪费一位做符号）。 */
void FPackedUint64NetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const uint64 Value = *reinterpret_cast<const uint64*>(Args.Source);
	const uint64 PrevValue = *reinterpret_cast<const uint64*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeUintDelta(*Writer, Value, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 64U);
}

/** Delta 读取：与 SerializeDelta 对称。 */
void FPackedUint64NetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	uint64& Target = *reinterpret_cast<uint64*>(Args.Target);
	const uint64 PrevValue = *reinterpret_cast<const uint64*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	DeserializeUintDelta(*Reader, Target, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 64U);
}

// ============================================================================
// FPackedInt32NetSerializer implementation
// ============================================================================

/**
 * 写入格式：2 bit "字节数-1"（0..3 表示 1..4 字节）+ 8/16/24/32 bit 值。
 * 最大编码：2 + 32 = 34 bit（相比原生 32 bit 多 2 bit 作为最坏情况代价）。
 */
void FPackedInt32NetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const int32 Value = *reinterpret_cast<const int32*>(Args.Source);

	const uint32 BitCountNeeded = GetBitsNeeded(Value);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U)/8U;
	const uint32 BitCountToWrite = ByteCountNeeded*8U;
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(ByteCountNeeded - 1U, 2U);
	// WriteBits 会对 Value 做 uint32 隐式转换并 mask，符号位随 bit 一起写入低 BitCountToWrite 位。
	Writer->WriteBits(Value, BitCountToWrite);
}

/** 对称读取 + 符号扩展。 */
void FPackedInt32NetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const uint32 ByteCountToRead = Reader->ReadBits(2U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead*8U;
	
	uint32 UnsignedValue = Reader->ReadBits(BitCountToRead);
	// 按 BitCountToRead 宽度做符号扩展。
	const uint32 Mask = 1U << (BitCountToRead - 1U);
	UnsignedValue = (UnsignedValue ^ Mask) - Mask;
	const int32 Value = static_cast<int32>(UnsignedValue);
		
	int32* Target = reinterpret_cast<int32*>(Args.Target);
	*Target = Value;
}

/** Delta 写出：参数 32U 为最大值宽度上限。 */
void FPackedInt32NetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const int32 Value = *reinterpret_cast<const int32*>(Args.Source);
	const int32 PrevValue = *reinterpret_cast<const int32*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeIntDelta(*Writer, Value, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 32U);
}

/** Delta 读取。 */
void FPackedInt32NetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	int32& Target = *reinterpret_cast<int32*>(Args.Target);
	const int32 PrevValue = *reinterpret_cast<const int32*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	DeserializeIntDelta(*Reader, Target, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 32U);
}

// ============================================================================
// FPackedUint32NetSerializer implementation
// ============================================================================

/**
 * 与 int32 版本相同的 2 bit "字节数-1" + 字节数*8 bit 值格式；无符号版解码不做符号扩展。
 * `| 1U` 避免 Value=0 时 ByteCountNeeded=0 导致下溢。
 */
void FPackedUint32NetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const uint32 Value = *reinterpret_cast<const uint32*>(Args.Source);

	// As we represent the number of bytes to write with two bits we want bits needed to be >= 1
	const uint32 BitCountNeeded = GetBitsNeeded(Value | 1U);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U)/8U;
	const uint32 BitCountToWrite = ByteCountNeeded*8U;
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(ByteCountNeeded - 1U, 2U);
	Writer->WriteBits(Value, BitCountToWrite);
}

/** 对称读取；无需符号扩展。 */
void FPackedUint32NetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const uint32 ByteCountToRead = Reader->ReadBits(2U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead*8U;
	
	const uint32 Value = Reader->ReadBits(BitCountToRead);
		
	uint32* Target = reinterpret_cast<uint32*>(Args.Target);
	*Target = Value;
}

/** Delta 写出（无符号差值）。 */
void FPackedUint32NetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const uint32 Value = *reinterpret_cast<const uint32*>(Args.Source);
	const uint32 PrevValue = *reinterpret_cast<const uint32*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeUintDelta(*Writer, Value, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 32U);
}

/** Delta 读取。 */
void FPackedUint32NetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	uint32& Target = *reinterpret_cast<uint32*>(Args.Target);
	const uint32 PrevValue = *reinterpret_cast<const uint32*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	DeserializeUintDelta(*Reader, Target, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 32U);
}

}
