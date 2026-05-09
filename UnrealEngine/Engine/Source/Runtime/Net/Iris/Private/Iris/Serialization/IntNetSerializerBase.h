// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Iris/Serialization/BitPacking.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Core/BitTwiddling.h"
#include <type_traits>

namespace UE::Net::Private
{

// FIntNetSerializerBase is expected to work for both signed and unsigned integers.
/**
 * 整型 NetSerializer 的 CRTP（模板）基座。
 *
 * 设计意图：
 *  - 把 Int8/16/32/64 / Uint8/16/32/64 这 8 个 Serializer 共用的全部逻辑集中在一个模板里，
 *    具体 Serializer（如 FInt32NetSerializer）只需简单继承本模板并指定 DefaultConfig 即可，
 *    从而保证 Serialize/Deserialize/Quantize/Dequantize/IsEqual/Validate 的实现在所有位宽上完全一致。
 *  - 量化策略本质上是"原位存储 + 位数裁剪"：SourceType 与 QuantizedType 采用相同字节数（无符号），
 *    Quantize 只做位截断 + 可选符号扩展，无"浮点量化"这类有损转换。
 *  - Delta 序列化走 BitPacking.h 中的 SerializeIntDelta / SerializeUintDelta（根据有符号性选择），
 *    利用预先准备好的 DeltaBitCountTable 选择合适的差分位宽，小幅变化只需少量比特。
 *
 * 模板参数：
 *  @tparam InSourceType  原始 C++ 整型类型（int8/int16/.../uint64）。
 *  @tparam InConfigType  对应 Config 类型（例如 FInt32NetSerializerConfig），必须包含 uint8 BitCount 成员。
 */
template<typename InSourceType, typename InConfigType>
struct FIntNetSerializerBase
{
	// 网络协议兼容性版本号。当序列化格式发生不兼容变更时需要递增，两端版本不一致会被检测并拒绝回放。
	static const uint32 Version = 0;

	using SourceType = InSourceType;
	// For convenience we'll use an unsigned type as the quantized type
	// 为便于位操作（截断、符号扩展）与 BitStream API 对接，量化状态统一使用无符号同字节孪生类型。
	using QuantizedType = std::make_unsigned_t<SourceType>;
	typedef InConfigType ConfigType;

	/**
	 * 按 Config->BitCount 位数把 Quantized 值写入比特流。
	 *
	 * 写入格式：
	 *  - 若 BitCount >= 16（ZeroValueOptimizationBitCount），先写 1 bit "IsZero" 标志；
	 *    当值为 0 时只花 1 bit 即可完成写入，避免浪费 16/32/64 bit。
	 *  - 64-bit 类型需要拆成低 32 bit + 高 (BitCount-32) bit 分两次写（因为 WriteBits 的位宽限制，
	 *    同时也是为了避免给 32-bit 类型展开出位移警告）。
	 *
	 *  @param Args.Source 指向 SourceType 的量化状态（此 Serializer 量化 == 原位，故也是 QuantizedType）。
	 */
	static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
	{
		const QuantizedType Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

		FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const uint32 BitCount = Config->BitCount;

		// Zero value optimization for larger bit counts
		// 大位宽（>=16）时的零值优化：写入 1 bit 标志，若为 0 即可提前返回，节省整型原本需要的 16/32/64 bit。
		if (BitCount >= ZeroValueOptimizationBitCount)
		{
			if (Writer->WriteBool(Value == QuantizedType(0)))
			{
				return;
			}
		}

		if constexpr (sizeof(SourceType) == 8)
		{
			// 64-bit 类型：先写低 32 位，然后写剩余的高 (BitCount-32) 位；若 BitCount<=32 则只写一次。
			Writer->WriteBits(static_cast<uint32>(Value), FMath::Min(BitCount, 32U));
			if (BitCount > 32U)
			{
				Writer->WriteBits(static_cast<uint32>(Value >> 32U), BitCount - 32U); // sizeof(SourceType)*4 == 32 for 64-bit types. This is to prevent compiler warning for shorter types.
			}
		}
		else
		{
			// 8/16/32-bit 类型：BitCount <= 32，直接一次写完。
			Writer->WriteBits(Value, BitCount);
		}
	}

	/**
	 * 与 Serialize 对称：按 Config->BitCount 位数从比特流读出值，并在必要时做符号扩展。
	 *
	 *  - 若 BitCount >= 16，先读 1 bit "IsZero"；为真则直接写入 0 并返回。
	 *  - 64-bit 类型分两次读（低 32 位 + 高 BitCount-32 位）。
	 *  - 如果 SourceType 是有符号型，读出的无符号值会通过 (Value ^ SignMask) - SignMask
	 *    这个经典无分支技巧做 BitCount 宽度的符号位扩展，恢复原始有符号语义。
	 */
	static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
	{
		QuantizedType* Target = reinterpret_cast<QuantizedType*>(Args.Target);

		FNetBitStreamReader* Reader = Context.GetBitStreamReader();

		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const uint32 BitCount = Config->BitCount;

		// Zero value optimization for larger bit counts
		// 零值优化的对称读取：若标志位为真表示对端写入了 0。
		if (BitCount >= ZeroValueOptimizationBitCount)
		{
			if (Reader->ReadBool())
			{
				*Target = SourceType(0);
				return;
			}
		}

		QuantizedType Value;
		if constexpr (sizeof(SourceType) == 8)
		{
			// 分两段读回 64-bit 值。
			Value = Reader->ReadBits(FMath::Min(BitCount, 32U));
			if (BitCount > 32U)
			{
				Value |= static_cast<QuantizedType>(Reader->ReadBits(BitCount - 32U)) << 32U;
			}
		}
		else
		{
			Value = static_cast<QuantizedType>(Reader->ReadBits(BitCount));
		}

		// Sign-extend the value if needed
		// 有符号类型需要对读出的 BitCount 位宽无符号值做符号扩展。
		// 经典技巧：若顶位（SignMask）为 1，(x ^ SignMask) - SignMask 等价于填满高位 1；否则保持不变。
		if constexpr (std::is_signed_v<SourceType>)
		{
			const QuantizedType SignMask = static_cast<QuantizedType>(QuantizedType(1) << (BitCount - 1U));
			Value = (Value ^ SignMask) - SignMask;
		}

		*Target = Value;
	}

	/**
	 * 基于前一版本值（Prev）的差分序列化，写出时走 BitPacking 的 SerializeIntDelta/SerializeUintDelta。
	 *
	 * 流程：
	 *  1) 若 MaxBitCount==0（极端非法配置）直接跳过；
	 *  2) 根据 MaxBitCount 查 GetDeltaBitCountTableIndex 选到对应的差分位宽表；
	 *  3) 按有符号/无符号性分别调用 SerializeIntDelta / SerializeUintDelta。
	 *     这两个辅助函数会写入 {表索引, 本次差值} 的紧凑变长编码：
	 *      - 小改动（比如两次值几乎相同）只花极少 bit（"same value 优化"即表项 0，差值 0 bit）。
	 */
	static void SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
	{
		// For this case we require the values to be properly signed as the helper function requires it.
		// 这里必须按"带符号语义"取值：SerializeIntDelta 内部的差值计算需要正确的符号扩展。
		const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);
		const SourceType PrevValue = *reinterpret_cast<const SourceType*>(Args.Prev);

		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const uint8 MaxBitCount = Config->BitCount;

		if (MaxBitCount == 0)
		{
			return;
		}

		// 根据最大位宽选择合适的差分位宽查找表。
		const uint32 IndexToBitCountEntries = GetDeltaBitCountTableIndex(MaxBitCount);
		const uint8* BitCountTable = DeltaBitCountTable[IndexToBitCountEntries];
		const uint32 BitCountTableEntryCount = DeltaBitCountTableEntryCount[IndexToBitCountEntries];

		FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
		if constexpr (std::is_signed_v<SourceType>)
		{
			// 有符号走 Int 版本（差值可正可负，需 Zig-Zag 或符号保留编码）。
			SerializeIntDelta(*Writer, Value, PrevValue, BitCountTable, BitCountTableEntryCount, MaxBitCount);
		}
		else
		{
			// 无符号走 Uint 版本。
			SerializeUintDelta(*Writer, Value, PrevValue, BitCountTable, BitCountTableEntryCount, MaxBitCount);
		}
	}

	/**
	 * 差分反序列化，与 SerializeDelta 严格对称。
	 *  - MaxBitCount==0 时直接把 Target 置 0；
	 *  - 否则按同样规则选表并调用 DeserializeIntDelta / DeserializeUintDelta 恢复最新值。
	 */
	static void DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
	{
		SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
		const SourceType PrevValue = *reinterpret_cast<const SourceType*>(Args.Prev);

		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const uint8 MaxBitCount = Config->BitCount;

		if (MaxBitCount == 0)
		{
			Target = SourceType(0);
			return;
		}

		const uint32 IndexToBitCountEntries = GetDeltaBitCountTableIndex(MaxBitCount);
		const uint8* BitCountTable = DeltaBitCountTable[IndexToBitCountEntries];
		const uint32 BitCountTableEntryCount = DeltaBitCountTableEntryCount[IndexToBitCountEntries];

		FNetBitStreamReader* Reader = Context.GetBitStreamReader();
		if constexpr (std::is_signed_v<SourceType>)
		{
			DeserializeIntDelta(*Reader, Target, PrevValue, BitCountTable, BitCountTableEntryCount, MaxBitCount);
		}
		else
		{
			DeserializeUintDelta(*Reader, Target, PrevValue, BitCountTable, BitCountTableEntryCount, MaxBitCount);
		}
	}

	/**
	 * 量化：把 SourceType 值按 BitCount 裁剪（并对有符号做符号扩展）得到 QuantizedType。
	 *
	 * 注意：本家族的"量化"其实是"原位存储 + 高位清零 / 符号扩展"：
	 *  - 无符号：仅保留低 BitCount 位；
	 *  - 有符号：先保留低 BitCount 位，再用 SignMask 对 BitCount-1 位做符号扩展——这样多余的高位
	 *    被扩展成符号位的复制，存回内部 quantized 缓冲时与原值在 BitCount 范围内完全等价。
	 *  由于 SourceType 与 QuantizedType 字节数相等（QuantizedType 是 SourceType 的无符号孪生），
	 *  实质上没有压缩；真正的压缩发生在 Serialize 阶段仅写出 BitCount 位。
	 */
	static void Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
	{
		const SourceType Source = *reinterpret_cast<const SourceType*>(Args.Source);

		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const uint32 BitCount = Config->BitCount;

		QuantizedType Value;
		if constexpr (std::is_signed_v<SourceType>)
		{
			// 取低 BitCount 位后做符号扩展：保留带符号语义。
			const QuantizedType SignMask = static_cast<QuantizedType>(QuantizedType(1) << (BitCount - 1U));
			const QuantizedType ValueMask = SignMask | (SignMask - QuantizedType(1));

			Value = static_cast<QuantizedType>(Source) & ValueMask;
			// Sign-extend
			Value = (Value ^ SignMask) - SignMask;
		}
		else
		{
			// 无符号：直接截断高位。
			constexpr QuantizedType MaxValueForType = ~QuantizedType(0);
			const QuantizedType ValueMask = MaxValueForType >> (sizeof(QuantizedType)*8U - BitCount);
			Value = Source & ValueMask;
		}

		QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
		Target = Value;
	}

	/**
	 * 反量化：由于量化本质是原位存储，直接把 Source 拷回 Target 即可。
	 */
	static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
	{
		const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
		SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

		Target = Source;
	}

	/**
	 * 比较两个值是否相等。
	 *  - bStateIsQuantized=true：两端都是已量化状态，直接按 QuantizedType 整型比较；
	 *  - bStateIsQuantized=false：两端是原始 SourceType，但只有低 BitCount 位在量化后会被保留，
	 *    因此只对这低 BitCount 位做比较——这避免了仅在"被丢弃的高位"上不同的值被误判为不等。
	 */
	static bool IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
	{
		if (Args.bStateIsQuantized)
		{
			const QuantizedType Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
			const QuantizedType Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

			return Value0 == Value1;
		}
		else
		{
			const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
			const uint32 BitCount = Config->BitCount;

			constexpr QuantizedType MaxValueForType = ~QuantizedType(0);
			const QuantizedType ValueMask = MaxValueForType >> (sizeof(SourceType)*8U - BitCount);

			// Only the lower BitCount bits of the values are considered when quantizing.
			// 只考虑量化后会实际参与的低 BitCount 位；多余高位差异不视为不等。
			const QuantizedType MaskedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0) & ValueMask;
			const QuantizedType MaskedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1) & ValueMask;

			return MaskedValue0 == MaskedValue1;
		}
	}

	/**
	 * 合法性校验（不改变任何值）。
	 *  - BitCount 必须处于 [1, sizeof(SourceType)*8] 区间；
	 *  - 值本身所需位数（有符号包含符号位）不得超过 BitCount，否则视为越界错误。
	 *  GetBitsNeeded 对负值返回 `(bits needed to represent including sign bit)`。
	 */
	static bool Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const uint32 BitCount = Config->BitCount;

		// Detect invalid bit count
		if (BitCount < 1 || BitCount > sizeof(SourceType)*8U)
		{
			return false;
		}

		const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);
		const bool bIsValidValue = GetBitsNeeded(Value) <= BitCount;
		return bIsValidValue;
	}

private:
	/**
	 * 根据 MaxBitCount 选择对应的差分位宽表索引。
	 *
	 * 通过一个 16-bit 的 LUT（TableIndexLUT）把 (BitCount-1)/8 ∈ [0..7] 映射到 [0..3] 的表索引，
	 * 相当于：BitCount 段          | <=8 | 9..16 | 17..24 | 25..32 | 33..40 | 41..48 | 49..56 | 57..64+
	 *                       Index |  0  |   1   |   2    |   2    |   3    |   3    |   3    |   3
	 */
	static uint32 GetDeltaBitCountTableIndex(uint8 BitCount)
	{
		// Use a small lookup table.
		// It's indexed via (BitCount-1)/8 which is in the range [0,7].
		// In max BitCount: 65+ 64 56 48 40 32 24 16  8
		//           Index:   0  3  3  3  3  2  2  1  0
		constexpr uint64 TableIndexLUT = 0b1111111110100100U;
		constexpr uint32 MaxTableIndex = 3U;
		const uint32 LUTShiftAmount = (uint8(BitCount - 1) >> 3U) << 1U;
		const uint32 TableIndex = (TableIndexLUT >> LUTShiftAmount) & MaxTableIndex;
		return TableIndex;
	}

	/* These are delta compression bit count tables for bitcounts <= 8, 16, 32 and 64 respectively.
	 * What they have in common is that they all allow for same value optimization,
	 * i.e. the delta to be serialized is zero. The index into the table must always be written
	 * so for bit counts > 8 the minimum number of bits written is 2.
	 * The bit counts are not scientifically nor empirically proven. They're mainly aiming to keep small changes cheap.
	 */
	/*
	 * 差分位宽查找表（按 MaxBitCount 分档）：
	 *  - 每档第一项均为 0，用于"相同值"快速路径（delta=0 时零比特差值）；
	 *  - 对 BitCount > 8 的档位额外提供 1 至 2 个中间档，让小改动用少量 bit 就能编码；
	 *  - 需要先写入 Table 索引（见 DeltaBitCountTableEntryCount 1/3/3/3 分别对应 1/2/2/2 bit 索引），
	 *    因此最小写入开销：<=8 bit 档只需 0 bit 索引；其他档至少 2 bit。
	 *  表中的数值并非理论最优，主要启发是"让小幅变化廉价"。
	 */
	inline static const uint8 DeltaBitCountTable[4][3] = 
	{
		{0, 0 /* unused */, 0 /* unused */},
		{0, 4, 10},
		{0, 4, 14},
		{0, 14, 32},
	};

	// 每档表项实际条目数：索引 0 的档只有 1 项；其余 3 项（需要 2-bit 索引）。
	inline static const uint32 DeltaBitCountTableEntryCount[4] = {1, 3, 3, 3};

	// 启用零值优化的最小 BitCount 阈值——低于 16 时一个 bit 标志反而不划算。
	static constexpr uint32 ZeroValueOptimizationBitCount = 16U;
};

}
