// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Templates/EnableIf.h"
#include "Templates/IsIntegral.h"
#include "Templates/IsSigned.h"

namespace UE::Net
{

/**
 * 整数差分编码（Delta）公共 API —— Iris 序列化最重要的 ChangeMask/Int 压缩手段。
 *
 * 核心思想：
 *  - 已知 PrevValue（上一次 ack 的值），只传输 "Delta = Value - PrevValue"（以 signed 方式）。
 *  - Delta 通常很小，用 SmallBitCountTable 中的几个候选"小位宽"之一编码；Delta 过大则写入 LargeBitCount 位的完整值。
 *  - 前缀"索引"选择用哪个位宽。索引表长 SmallBitCountTableEntryCount（必须为 2^N - 1，典型 3 或 7），再加隐式 "LargeBitCount" 共 2^N 项。
 *
 * 编码布局（单个值）：
 *   [2..N 位 TableIndex]
 *   if TableIndex == 0：  直接写 LargeBitCount 位的完整 Value；
 *   else (1..SmallBitCountTableEntryCount)： 写 SmallBitCountTable[TableIndex-1] 位的 Delta（signed，接收端会 sign-extend）。
 *
 * 错误韧性：
 *   即使错误包产生了不可解的 Delta，接收端通过 LargeBitCount mask 保证 OutValue 不会越界，避免内存污染。
 *
 * 所有重载（int32/uint32/int64/uint64）都要求 Value 与 PrevValue 能用 LargeBitCount 位表示；
 * 对于有符号类型，超出的高位必须是符号位扩展（全 0 或全 1）。
 */
/**
 * All the SerializeIntDelta functions assume that the Value and PrevValue are representable using LargeBitCount. Negative values are expected to be properly represented
 * with set bits from LargeBitCount and up. The SmallBitCountTable should be kept relatively small as the index into it must be replicated. The SmallBitCountTableEntryCount
 * needs to be a power of two minus one, i.e. of the form (2^N) - 1. The LargeBitCount is treated as the last entry in the table and is used when the delta between the 
 * Value and PrevValue cannot be represented with any of the bit counts in the table. 0 is a valid bit count in the first entry but is only used when the Value and PrevValue
 * are equal.
 */
IRISCORE_API void SerializeIntDelta(FNetBitStreamWriter& Writer, const int32 Value, const int32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);
/**
 * All the DeserializeIntDelta functions assume that the PrevValue is representable using LargeBitCount. Negative values are expected to be properly represented with set bits from LargeBitCount and up.
 * The OutValue is guaranteed to also be representable using LargeBitCount. An incorrectly replicated delta will never be able to cause on overflow on the receiving side. 
 * This is ensured via masking and sign-bit propagation.
 */
IRISCORE_API void DeserializeIntDelta(FNetBitStreamReader& Reader, int32& OutValue, const int32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);

/*
 * All the SerializeUintDelta functions assume that the Value and PrevValue are representable using LargeBitCount. For SmallBitCountTable information see SerializeIntDelta.
 * @see SerializeIntDelta
 *
 * Unsigned 版本：Delta 仍按有符号语义编码（"-1" 对应的按位表示），但接收端最终通过 mask 保留 LargeBitCount 位。
 */
IRISCORE_API void SerializeUintDelta(FNetBitStreamWriter& Writer, const uint32 Value, const uint32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);
/**
 * All the DeserializeIntDelta functions assume that the PrevValue is representable using LargeBitCount. The OutValue is guaranteed to also be representable using LargeBitCount. 
 * An incorrectly replicated delta will never be able to cause on overflow on the receiving side. This is ensured via masking.
 */
IRISCORE_API void DeserializeUintDelta(FNetBitStreamReader& Reader, uint32& OutValue, const uint32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);

/** 64 位版：实现分流到 Private::SerializeUintDeltaImpl<uint64>。 */
IRISCORE_API void SerializeIntDelta(FNetBitStreamWriter& Writer, const int64 Value, const int64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);
IRISCORE_API void DeserializeIntDelta(FNetBitStreamReader& Reader, int64& OutValue, const int64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);

IRISCORE_API void SerializeUintDelta(FNetBitStreamWriter& Writer, const uint64 Value, const uint64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);
IRISCORE_API void DeserializeUintDelta(FNetBitStreamReader& Reader, uint64& OutValue, const uint64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount);

/**
 * QuantizeSignedUnitFloat：把 [-1.0f, 1.0f] 范围的单位浮点量化成 BitCount 位整数。
 *  - BitCount ∈ [2, 23]：线性量化。保留 1 位符号，剩余 BitCount-1 位表示 [0, 2^(BitCount-1)-1] 的振幅。
 *    Sign 舍入：正值 +0.5 舍入到最近，负值 -0.5 保证 -1.0f 对应精确的 -(2^(BitCount-1)-1)（即可以精确往返）。
 *  - BitCount ≥ 24：走 25 位浮点编码路径——
 *      利用 FVector 的 IEEE 754 表示，先把 abs(Value) 加 1.0f 映射到 [1.0, 2.0)，共享指数；
 *      保留 23 位有效位 + 1 位 "是否非零（区分 0.0 和 1.0）" + 1 位符号。
 *      0.0f 特殊情况只占 2 位（符号 + 非零标记）。
 * 返回值：低 BitCount（或 25）位为量化结果，其余位清零。
 */
/**
 * Converts a unit float to a quantized representation using a specified bit count.
 * @param Value A float in range [-1.0f, 1.0f]. No clamping is performed.
 * @param BitCount The desired bit count of the quantized value.
 * @return The quantized representation. Unused top bits will be zero.
 * @note For bit counts > 23 the return value will use 25 bits, otherwise exactly as many bits as requested.
 */
IRISCORE_API uint32 QuantizeSignedUnitFloat(float Value, uint32 BitCount);

/**
 * Dequantizes the value returned from QuantizeSignedUnitFloat or DeserializeSignedUnitFloat called with the same BitCount.
 * @param Value The quantized value, as returned by QuantizeSignedUnitFloat.
 * @return The float in range [-1.0f, 1.0f] corresponding to the quantized value.
 * @see QuantizeSignedUnitFloat
 *
 * 反量化：严格对偶实现，保证 BitCount < 24 时 ±1.0f 能精确恢复。
 */
IRISCORE_API float DequantizeSignedUnitFloat(uint32 Value, uint32 BitCount);

/**
 * Serializes a signed unit float quantized with QuantizeSignedUnitFloat with the same BitCount.
 * @see QuantizeSignedUnitFloat
 *
 * 将量化后的 uint32 写入位流：
 *  - BitCount < 24：直接写 BitCount 位；
 *  - 否则写 2 位 (符号 + 非零)，若值非零再写 23 位 significand。
 */
void SerializeSignedUnitFloat(FNetBitStreamWriter& Writer, uint32 Value, uint32 BitCount);

/**
 * Deserializes a signed unit float that was serialized using SerializeSignedUnitFloat with the same BitCount.
 * @see SerializeSignedUnitFloat.
 * @see DequantizeSignedUnitFloat
 *
 * 读回量化值（仍是 uint32，交给 DequantizeSignedUnitFloat 变回 float）。
 * BitCount < 24 路径：读 BitCount 位后做"符号扩展到 32 位有符号数"并装回 uint32（保留原位模式）。
 * BitCount ≥ 24 路径：读 2 位 (符号 + 非零)，若非零再读 23 位，否则 significand 记 0。
 */
uint32 DeserializeSignedUnitFloat(FNetBitStreamReader& Reader, uint32 BitCount);


/**
 * SerializeSameValue：若 Value == OtherValue 写 1 位 = 1；否则写 1 位 = 0。
 * 真正的"值"本身并不写入——上层调用者负责在 bit 为 0 时再单独写 Value。
 *
 * 典型用法：Delta 压缩的最简模式——把 Value 与 Prev 对比，相等就只写 1 位。
 *
 * @return 是否"相等"（也即是否进入了"写 1 位"的快路径）。
 */
/**
 * Serializes a single bit indicating whether the value is equal to another value or not.
 * @return true if the Value was equal to OtherValue, false if not.
 * @note The actual value is never written.
 * @see DeserializeSameValue
 */
template<typename T>
inline bool SerializeSameValue(FNetBitStreamWriter& Writer, const T Value, const T OtherValue)
{
	const uint32 IsSameValue = (Value == OtherValue ? 1U : 0U);
	Writer.WriteBits(IsSameValue, 1U);
	return IsSameValue & 1;
}

/**
 * Deserializes what was serialized using SerializeSameValue.
 * @param Reader The bitstream to read from.
 * @param OutValue Will be set to OtherValue if the read bit was 1, unset otherwise.
 * @param OtherValue The value to set to OutValue if the read bit was 1.
 * @return true if OutValue was set to OtherValue, false if not.
 * @see SerializeSameValue
 *
 * 对偶：读 1 位，若为 1 则 OutValue = OtherValue 并返回 true；为 0 时 OutValue 不动（调用者需自行再读真正的值）。
 */
template<typename T>
inline bool DeserializeSameValue(FNetBitStreamReader& Reader, T& OutValue, const T OtherValue)
{
	const uint32 IsSameValue = Reader.ReadBits(1U);
	if (IsSameValue)
	{
		OutValue = OtherValue;
		return true;
	}

	return false;
}

/**
 * 通用有符号整数 Delta 模板：对任意 ≤ 8 字节的有符号整型，先转为标准 int32/int64 再调底层实现。
 * SFINAE 约束：TIsSigned<T> && sizeof(T) <= 8。注意使用的是 UE 传统的 TEnableIf 而非 std::enable_if。
 * 为什么要先 cast：
 *   直接把小整型（如 int8 = -1）传给底层 int32 版本，C++ 会做符号扩展到 0xFFFFFFFF，符合小负数到小 delta 的要求。
 *   若先用 uint8(-1) = 0xFF 再给底层，就会当作 +255，使 delta 很大，反而丢失压缩效益。
 */
template<typename T, typename TEnableIf<TIsSigned<T>::Value && sizeof(T) <= 8U, int32>::Type U = -1>
inline void SerializeIntDelta(FNetBitStreamWriter& Writer, const T Value, const T PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	// Careful casting to properly represent negative numbers in the larger type as we want the delta between small negative numbers and small positive numbers to be small.
	using SignedType = std::conditional_t<sizeof(T) == 8, int64, int32>;
	return SerializeIntDelta(Writer, SignedType(Value), SignedType(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** 通用有符号整数 Delta 反序列化：先拿到 SignedType 值再 T 收窄。 */
template<typename T, typename TEnableIf<TIsSigned<T>::Value && sizeof(T) <= 8U, int32>::Type U = -1>
inline void DeserializeIntDelta(FNetBitStreamReader& Reader, T& OutValue, const T PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	using SignedType = std::conditional_t<sizeof(T) == 8, int64, int32>;

	SignedType Value;
	DeserializeIntDelta(Reader, Value, SignedType(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
	OutValue = T(Value);
}

/**
 * 通用无符号整数 Delta 模板：SFINAE 约束 !TIsSigned && TIsIntegral && sizeof ≤ 8。
 * 小 unsigned 先扩展成 uint32/uint64 再交给底层，保持 delta 语义。
 */
template<typename T, typename TEnableIf<!TIsSigned<T>::Value && TIsIntegral<T>::Value && sizeof(T) <= 8U, uint32>::Type U = 1U>
inline void SerializeUintDelta(FNetBitStreamWriter& Writer, const T Value, const T PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	// Careful casting to allow the delta between small positive numbers and large positive numbers to be small.
	using UnsignedType = std::conditional_t<sizeof(T) == 8, uint64, uint32>;
	return SerializeUintDelta(Writer, UnsignedType(Value), UnsignedType(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** 通用无符号整数 Delta 反序列化。 */
template<typename T, typename TEnableIf<!TIsSigned<T>::Value && TIsIntegral<T>::Value && sizeof(T) <= 8U, uint32>::Type U = 1U>
inline void DeserializeUintDelta(FNetBitStreamReader& Reader, T& OutValue, const T PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	using UnsignedType = std::conditional_t<sizeof(T) == 8, uint64, uint32>;

	UnsignedType Value;
	DeserializeUintDelta(Reader, Value, UnsignedType(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
	OutValue = T(Value);
}

/**
 * SerializeSignedUnitFloat 的内联实现：
 *  - BitCount < 24：直接 WriteBits（量化结果已是 BitCount 位 signed 表示）。
 *  - BitCount ≥ 24：值被划分为 "符号 + 是否非零"（2 位）+ 23 位 significand。若 0 则 significand 省略。
 */
inline void SerializeSignedUnitFloat(FNetBitStreamWriter& Writer, uint32 Value, uint32 BitCount)
{
	if (BitCount < 24U)
	{
		Writer.WriteBits(Value, BitCount);
	}
	else
	{
		const uint32 SignAndIsNotZero = Value >> 23U;
		Writer.WriteBits(SignAndIsNotZero, 2U);
		if (SignAndIsNotZero & 1U)
		{
			Writer.WriteBits(Value, 23U);
		}
	}
}

/**
 * DeserializeSignedUnitFloat 的内联实现（与 Serialize 对偶）：
 *  - BitCount < 24：读 BitCount 位后用 (X ^ Mask) - Mask 做符号扩展到 32 位；
 *  - BitCount ≥ 24：读 2 位 (SignAndIsNotZero)，若非零再读 23 位 significand，组装回量化表示。
 */
inline uint32 DeserializeSignedUnitFloat(FNetBitStreamReader& Reader, uint32 BitCount)
{
	if (BitCount < 24U)
	{
		uint32 Value = Reader.ReadBits(BitCount);
		// Sign-extend
		const uint32 Mask = 1U << (BitCount - 1U);
		Value = (Value ^ Mask) - Mask;
		return Value;
	}
	else
	{
		const uint32 SignAndIsNotZero = Reader.ReadBits(2U);
		uint32 Value = SignAndIsNotZero << 23U;
		if (SignAndIsNotZero & 1U)
		{
			Value |= Reader.ReadBits(23U);
		}
		return Value;
	}
}

}
