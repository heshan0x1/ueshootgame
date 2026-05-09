// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// FloatNetSerializers.cpp
// ---------------------------------------------------------------------------------------------
// FFloatNetSerializer / FDoubleNetSerializer 的实现。
//
// 关键设计：
//   1. **以整数位形式作为 SourceType**——`uint32`（float）/ `uint64`（double）。
//      原因：IEEE 754 中 `+0.0f == -0.0f`，且 `NaN != NaN`。如果直接用 float/double 做
//      IsEqual / Quantize，会产生：
//         - 对状态去重错误（+0/-0 被视为相同，但位上不同 → ChangeMask 误判 dirty 抖动）；
//         - NaN 永远 dirty 的死循环。
//      改用 uint 后，bit-exact 比较直接复用 NetSerializerBuilder 的 default IsEqual，
//      天然解决这两个坑。
//
//   2. **Serialize 优化**：`WriteBool(Value != 0)`——零值（默认值/未初始化）极常见，
//      用 1-bit 表示零，比固定写 32/64-bit 节省大量带宽。非零再写完整 32/64 位原值。
//      代价：非零值多花 1-bit。
//
//   3. **SerializeDelta 优化**（NetBitStreamUtil + BitPacking）：
//      把当前值 V 与上一次 ack 值 Prev 做"无符号差分"，用 SerializeUintDelta + 多档比特表。
//      对 float（32-bit）：[0, 16, 25] —— 这是 BitPacking 内部 "delta + small/medium/full"
//      三档自适应：
//         - 档 0：bit-exact 相同（diff == 0），编码 0 比特；
//         - 档 16：差分落在 ±2^15 范围；典型场景：值在 ~100 的范围内 0.1 量级浮动。
//         - 档 25：差分落在 ±2^24；允许指数 ±1（值倍/半），覆盖更剧烈变化；
//         - 否则降级为 32 位原值。
//      对 double（64-bit）：[0, 42, 54] 类似——42-bit 覆盖 1000+ 范围内的 0.1 量级浮动，
//      54-bit 覆盖指数 ±1。
//
//   4. **NaN/Inf 处理**：本 Serializer 不在 Validate / Quantize 中拒绝 NaN/Inf。位级
//      转发，行为与发送方语义一致。若上层希望拒绝，应在业务层提前过滤。
// =============================================================================================

#include "Iris/Serialization/FloatNetSerializers.h"
#include "Iris/Serialization/BitPacking.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FloatNetSerializers)

namespace UE::Net
{

/**
 * FFloatNetSerializer ：32-bit 单精度浮点序列化器。
 *
 * 仅实现 Serialize / Deserialize / SerializeDelta / DeserializeDelta；
 * Quantize/Dequantize/IsEqual/Validate/Clone/Free/CollectReferences/Apply 全部走
 * NetSerializerBuilder 的 SFINAE 默认实现（因为 SourceType=uint32 的默认 IsEqual
 * 即为 bit-exact 比较，正是我们想要的语义）。
 */
struct FFloatNetSerializer
{
	static const uint32 Version = 0;

	/**
	 * We are interested in the bit representation of the float, not IEEE 754 behavior. This is particularly
	 * relevant for IsEqual where for example -0.0f == +0.0f if the values were treated as floats
	 * rather than the bit representation of the floats. By using uint32 as SourceType we can avoid
	 * implementing some functions.
	 *
	 * 译：我们关心 float 的位表示而非 IEEE 754 行为。对 IsEqual 尤其关键——若用 float 比较
	 *     +0.0f == -0.0f 但位不同，会导致 ChangeMask 抖动；NaN!=NaN 也会让 NaN 状态永远脏。
	 *     使用 uint32 作为 SourceType 后，比较即位级精确，且能省去若干自定义函数。
	 */
	typedef uint32 SourceType;
	typedef FFloatNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig = ConfigType();

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

private:
	// 增量编码档位表（SerializeUintDelta 自适应选择最经济的档），共 3 + 1 档（最后一档是 32 位回退）。
	inline static const uint8 DeltaBitCountTable[] = 
	{
		// Same value optimization.
		// 档 0：完全相同（diff == 0）。仅写 1 个档位选择位即可，不写值。
		0,
		// Allows for small changes (0.1) at small values (100) and larger changes at larger values.
		// 档 1：16 位有符号差分，覆盖 ±2^15。
		// 解释：对于值在 ~100 量级、变化 0.1 的场景，IEEE 754 二进制差通常 < 2^15（粗略经验值）。
		16,
		// Allows exponent to increment or decrement, i.e. doubling or halfing a value.
		// 档 2：25 位有符号差分，覆盖 ±2^24，允许指数位 ±1（值翻倍/减半）。
		25,
	};
	static constexpr uint32 DeltaBitCountTableEntryCount = sizeof(DeltaBitCountTable)/sizeof(DeltaBitCountTable[0]);
};
UE_NET_IMPLEMENT_SERIALIZER(FFloatNetSerializer);

/**
 * FDoubleNetSerializer ：64-bit 双精度浮点序列化器。
 *
 * 与 FFloatNetSerializer 同款设计，只是位宽和 Delta 档位不同。
 */
struct FDoubleNetSerializer
{
	static const uint32 Version = 0;

	// SourceType=uint64：bit-exact 比较，避免 ±0、NaN 比较坑（同上）。
	typedef uint64 SourceType;
	typedef FDoubleNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig = ConfigType();

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

private:
	// 增量编码档位表（双精度专用，最大档为 64 位回退）。
	inline static const uint8 DeltaBitCountTable[] = 
	{
		// Same value optimization.
		// 档 0：bit-exact 相同。
		0,
		// Allows for small changes (0.1) at small values (1000+) and larger changes at larger values.
		// 档 1：42 位有符号差分。double 的 mantissa 有 52 位，0.1 量级变化在 1000+ 量级值上
		// 通常落在 ±2^41 内。
		42,
		// Allows exponent to increment or decrement, i.e. doubling or halfing a value.
		// 档 2：54 位有符号差分，允许指数 ±1。
		54,
	};
	static constexpr uint32 DeltaBitCountTableEntryCount = sizeof(DeltaBitCountTable)/sizeof(DeltaBitCountTable[0]);
};
UE_NET_IMPLEMENT_SERIALIZER(FDoubleNetSerializer);

// FFloatNetSerializer implementation
//-----------------------------------------------------------------------------

/**
 * 序列化：1-bit 零标志 + 32-bit 原位。
 *   - Value == 0      → 1 位 (0)，共 1 bit。
 *   - Value != 0      → 1 位 (1) + 32 位原 IEEE 754 比特 = 33 bit。
 *
 * 注意：Value == 0 这里指的是 uint32 值为 0，等价于 +0.0f 的位模式。
 *       -0.0f 的位模式是 0x80000000，会走 33-bit 路径（位级精确）。
 */
void FFloatNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const uint32 Value = *reinterpret_cast<const uint32*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if (Writer->WriteBool(Value != 0))
	{
		Writer->WriteBits(Value, 32U);
	}
}

/**
 * 反序列化：与 Serialize 对称。
 * 默认值 0 表示 +0.0f；仅当读取的 zero-flag 为 1 时才读 32-bit 完整位。
 */
void FFloatNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const uint32 Value = Reader->ReadBool() ? Reader->ReadBits(32U) : 0U;

	uint32* Target = reinterpret_cast<uint32*>(Args.Target);
	*Target = Value;
}

/**
 * 增量序列化：以"无符号 delta"形式写入。
 * 调用 BitPacking::SerializeUintDelta 自动从 DeltaBitCountTable 中选择最小可承载的档位
 * （0 / 16 / 25 / 32），并写一个档位选择小整数 + 对应位数的差值。
 *
 * 这里使用 unsigned delta（V - Prev 在 uint32 范围里溢出回绕），SerializeUintDelta
 * 会做符号扩展处理（差值在 [-2^(N-1), 2^(N-1)-1] 内即可）。
 */
void FFloatNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const uint32 Value = *reinterpret_cast<const uint32*>(Args.Source);
	const uint32 PrevValue = *reinterpret_cast<const uint32*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeUintDelta(*Writer, Value, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 32U);
}

/**
 * 增量反序列化：与 SerializeDelta 对称。基线 PrevValue 必须与发送端一致（ack 序列化）。
 */
void FFloatNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	uint32& Target = *reinterpret_cast<uint32*>(Args.Target);
	const uint32 PrevValue = *reinterpret_cast<const uint32*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	DeserializeUintDelta(*Reader, Target, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 32U);
}

// FDoubleNetSerializer implementation
//-----------------------------------------------------------------------------

/**
 * 序列化（double 64-bit 版）：与 float 同设计。
 *   - Value == 0：1 bit。
 *   - Value != 0：1 bit + 32 bit (low) + 32 bit (high) = 65 bit。
 *
 * 拆 2 次写 32-bit 是因为 NetBitStreamWriter::WriteBits 单次最多 32 位。
 */
void FDoubleNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const uint64 Value = *reinterpret_cast<const uint64*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if (Writer->WriteBool(Value != 0))
	{
		Writer->WriteBits(static_cast<uint32>(Value), 32U);          // low 32
		Writer->WriteBits(static_cast<uint32>(Value >> 32U), 32U);   // high 32
	}
}

/**
 * 反序列化：低 32 → 高 32 顺序拼回 64 位。
 */
void FDoubleNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	uint64 Value = 0;
	
	if (Reader->ReadBool())
	{
		Value = Reader->ReadBits(32U);
		Value |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;
	}

	uint64* Target = reinterpret_cast<uint64*>(Args.Target);
	*Target = Value;
}

/**
 * 增量序列化（double）：64-bit 版本的 SerializeUintDelta，BitCountTable=[0, 42, 54]+64。
 */
void FDoubleNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const uint64 Value = *reinterpret_cast<const uint64*>(Args.Source);
	const uint64 PrevValue = *reinterpret_cast<const uint64*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeUintDelta(*Writer, Value, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 64U);
}

/**
 * 增量反序列化（double）。
 */
void FDoubleNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	uint64& Target = *reinterpret_cast<uint64*>(Args.Target);
	const uint64 PrevValue = *reinterpret_cast<const uint64*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	DeserializeUintDelta(*Reader, Target, PrevValue, DeltaBitCountTable, DeltaBitCountTableEntryCount, 64U);
}

}
