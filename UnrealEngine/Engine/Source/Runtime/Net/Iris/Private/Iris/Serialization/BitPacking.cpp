// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/BitPacking.h"
#include "Iris/Core/BitTwiddling.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"
#include "Traits/IntType.h"

namespace UE::Net::Private
{

/**
 * float → uint32 的 IEEE 754 按位重解释（无精度损失，只是改变视角）。
 * 使用 union 是历史写法——现代 C++ 更推荐 std::bit_cast，但这里为兼容旧编译器保留。
 */
inline uint32 FloatAsUint32(float Value)
{
	union FFloatAsUint
	{
		float Float;
		uint32 Uint;
	};

	FFloatAsUint FloatAsUint;
	FloatAsUint.Float = Value;
	return FloatAsUint.Uint;
}

/** uint32 → float 的按位重解释（逆操作）。 */
inline float Uint32AsFloat(uint32 Value)
{
	union FFloatAsUint
	{
		float Float;
		uint32 Uint;
	};

	FFloatAsUint FloatAsUint;
	FloatAsUint.Uint = Value;
	return FloatAsUint.Float;
}

/**
 * 整数 Delta 序列化的通用实现（内部模板，模板参数 T ∈ {uint32, uint64}）。
 *
 * 算法流程：
 *  1. 计算 UnsignedDelta = (Value - PrevValue) mod 2^LargeBitCount；
 *  2. 通过 "(U ^ SignMask) - SignMask" 把 UnsignedDelta 视为 LargeBitCount 位的有符号数；
 *  3. 用 GetBitsNeeded(Delta) 求需要位数 BitCountForDelta；
 *  4. 若能塞进 SmallBitCountTable 的某一项：
 *     - 写 TableIndex（1-based）——注意 Delta == 0 时即使 SmallBitCount = 0 也能命中第 0 项；
 *     - 写 SmallBitCount 位的 Delta（≤ 32 直接写；> 32 拆 32 + 剩余）。
 *  5. 否则写 TableIndex = 0，再写 LargeBitCount 位的完整 Value。
 *
 * static_assert：
 *  - !TIsSigned<T>：底层按无符号处理；有符号值需先 cast；
 *  - sizeof(T) ≥ sizeof(int)：排除 uint8/uint16，避免位运算被隐式提升带来语义歧义。
 *
 * if constexpr (TypeBitCount > 32U)：编译期分支，仅在 T == uint64 时编译 64 位拆写代码。
 */
template<typename T>
static inline void SerializeUintDeltaImpl(FNetBitStreamWriter& Writer, const T Value, const T PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	static_assert(!TIsSigned<T>::Value, "This function assumes unsigned integral types");
	static_assert(sizeof(T) >= sizeof(int), "This function assumes types at least as large as ints");

	using SignedType = typename TSignedIntType<sizeof(T)>::Type;
	constexpr unsigned TypeBitCount = sizeof(T)*8U;

	const uint8 MaxDeltaBitCount = SmallBitCountTable[SmallBitCountTableEntryCount - 1U];
	const uint32 BitCountForTableIndex = GetBitsNeeded(SmallBitCountTableEntryCount);

	// The delta must be expressed in terms of maximum bit count (LargeBitCount). 
	// 把 Delta 限定在 LargeBitCount 位宽内（截断超出位），再做符号扩展回到机器字宽。
	const T UnsignedDelta = (Value - PrevValue) & (~T(0) >> (TypeBitCount - LargeBitCount));
	const T DeltaSignMask = T(1) << (LargeBitCount - 1U);
	const SignedType Delta = SignedType((UnsignedDelta ^ DeltaSignMask) - DeltaSignMask);
	const uint32 BitCountForDelta = GetBitsNeeded(Delta);
	if (BitCountForDelta <= MaxDeltaBitCount)
	{
		for (uint32 TableIndex = 0, TableEndIndex = SmallBitCountTableEntryCount; TableIndex != SmallBitCountTableEntryCount; ++TableIndex)
		{
			const uint32 SmallBitCount = SmallBitCountTable[TableIndex];
			// Delta == 0 时允许命中"0 位"项（免费压缩）；其余情况取第一个能容纳 BitCountForDelta 的项。
			if ((BitCountForDelta <= SmallBitCount) | (Delta == 0))
			{
				Writer.WriteBits(TableIndex + 1U, BitCountForTableIndex);
				if constexpr (TypeBitCount > 32U)
				{
					if (SmallBitCount > 32U)
					{
						// 64 位 Delta 需要拆成两个 WriteBits（WriteBits 每次最多 32 位）。
						Writer.WriteBits(static_cast<uint32>(static_cast<T>(Delta)), 32U);
						Writer.WriteBits(static_cast<uint32>(static_cast<T>(Delta) >> 32U), SmallBitCount - 32U);
						return;
					}
				}

				Writer.WriteBits(static_cast<uint32>(static_cast<T>(Delta)), SmallBitCount);
				return;
			}
		}
	}
	else
	{
		// Delta 超过表中最大位宽 → 直接写完整 Value（LargeBitCount 位），TableIndex = 0 作为"逃生口"标记。
		Writer.WriteBits(0U, BitCountForTableIndex);
		if constexpr (TypeBitCount > 32U)
		{
			if (LargeBitCount > 32U)
			{
				Writer.WriteBits(static_cast<uint32>(Value), 32U);
				Writer.WriteBits(static_cast<uint32>(Value >> 32U), LargeBitCount - 32U);
				return;
			}
		}

		Writer.WriteBits(static_cast<uint32>(Value), LargeBitCount);
	}
}

/**
 * 整数 Delta 反序列化的通用实现。
 *
 * 模板参数：
 *  - bMaskOutValue：是否把最终 OutValue 按 LargeBitCount 位 mask（用于无符号，防止越界的高位被保留）；
 *  - bSignExtendValue：是否把 OutValue 做 LargeBitCount 宽的有符号扩展（用于有符号类型的 Deserialize）。
 *
 * 错误韧性：收发端位宽不一致、或包被篡改导致 Delta 超出 LargeBitCount 时，mask/sign-extend 步骤
 * 保证 OutValue 仍落在合法表示范围内，不会溢出越界污染业务内存。
 *
 * if constexpr 分支：
 *  - 仅在 T == uint64 且位宽 > 32 时编译 64 位读取路径；其余走 32 位快路径。
 */
template<bool bMaskOutValue, bool bSignExtendValue, typename T>
static inline void DeserializeUintDeltaImpl(FNetBitStreamReader& Reader, T& OutValue, const T PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	static_assert(!TIsSigned<T>::Value, "This function assumes unsigned integral types");
	static_assert(sizeof(T) >= sizeof(int), "This function assumes types at least as large as ints");

	using SignedType = typename TSignedIntType<sizeof(T)>::Type;
	constexpr unsigned TypeBitCount = sizeof(T)*8U;

	// If the delta isn't small enough to be represented by one of the entries in the SmnallBitCountTable we use LargeBitCount to write down the full value instead.
	// The LargeBitCount value counts as an implicit member of the SmallBitCountTable. For that reason we want the SmallBitCountTableEntryCount to be a number of the
	// form 2^N - 1. If that is true we use all bits needed for the table index and don't need to clamp or handle out of bounds errors in here. As a bonus it will
	// also be as bandwidth efficient as possible.
	// 要求表长是 2^N - 1：加上"大值"入口正好凑 2^N 项，使索引位宽恰好用满，最省带宽且无需越界处理。
	checkfSlow(FMath::IsPowerOfTwo(SmallBitCountTableEntryCount + 1) && SmallBitCountTableEntryCount > 0, TEXT("Table size should be a power of two minus one."));

	const uint32 BitCountForTableIndex = GetBitsNeeded(SmallBitCountTableEntryCount);
	const uint32 TableIndex = Reader.ReadBits(BitCountForTableIndex);
	if (TableIndex)
	{
		// 小 Delta 分支：读 SmallBitCountTable[TableIndex-1] 位的 Delta，再 sign-extend 后与 PrevValue 相加。
		const uint32 BitCountForDelta = SmallBitCountTable[TableIndex - 1U];
		T Delta;
		if ((TypeBitCount > 32U) && (BitCountForDelta > 32U))
		{
			Delta = Reader.ReadBits(32U);
			Delta |= static_cast<T>(Reader.ReadBits(BitCountForDelta - 32U)) << (TypeBitCount/2U);
		}
		else
		{
			Delta = Reader.ReadBits(BitCountForDelta);
		}

		// Mask the shift amount to avoid undefined behavior when BitCountForDelta == 0.
		// 当 BitCountForDelta == 0（Delta 必为 0）时用 mask 避免左移 -1 造成 UB。
		const T DeltaSignMask = T(1) << ((BitCountForDelta - 1U) & (TypeBitCount - 1U));
		Delta = (Delta ^ DeltaSignMask) - DeltaSignMask;
		T Value = PrevValue + Delta;
		if constexpr (bSignExtendValue)
		{
			// We treat the number as LargeBitCount bits wide and handle overflow/underflow first before sign extending.
			// 有符号模式：先按 LargeBitCount mask 把加法产生的溢出位砍掉，再做 sign-extend。
			const T ValueMask = ~T(0) >> (TypeBitCount - LargeBitCount);
			const T ValueSignMask = T(1) << (LargeBitCount - 1U);
			Value = ((Value & ValueMask) ^ ValueSignMask) - ValueSignMask;
		}
		else if constexpr (bMaskOutValue)
		{
			// 无符号模式：只 mask，不 sign-extend。
			const T ValueMask = ~T(0) >> (TypeBitCount - LargeBitCount);
			Value &= ValueMask;
		}
		OutValue = Value;
	}
	else
	{
		// 逃生口分支：直接读 LargeBitCount 位的完整值。
		T Value;
		if ((TypeBitCount > 32U) && (LargeBitCount > 32U))
		{
			Value = Reader.ReadBits(32U);
			Value |= static_cast<T>(Reader.ReadBits(LargeBitCount - 32U)) << (TypeBitCount/2U);
		}
		else
		{
			Value = Reader.ReadBits(LargeBitCount);
		}

		if constexpr (bSignExtendValue)
		{
			const T ValueSignMask = T(1) << (LargeBitCount - 1U);
			Value = (Value ^ ValueSignMask) - ValueSignMask;
		}
		else if constexpr (bMaskOutValue)
		{
			const T ValueMask = ~T(0) >> (TypeBitCount - LargeBitCount);
			Value &= ValueMask;
		}
		OutValue = Value;
	}
}

}

// Implementation of the public API
namespace UE::Net
{

/** int32 版 SerializeIntDelta：把有符号 Value/PrevValue 按位 cast 为 uint32 再走底层模板。 */
void SerializeIntDelta(FNetBitStreamWriter& Writer, const int32 Value, const int32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	return Private::SerializeUintDeltaImpl(Writer, static_cast<uint32>(Value), static_cast<uint32>(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** int32 版 DeserializeIntDelta：底层用 uint32 解码但要求 sign-extend（bMaskOutValue = false, bSignExtendValue = true）。 */
void DeserializeIntDelta(FNetBitStreamReader& Reader, int32& OutValue, const int32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	constexpr bool bMaskOutValue = false;
	constexpr bool bSignExtendValue = true;
	return Private::DeserializeUintDeltaImpl<bMaskOutValue, bSignExtendValue>(Reader, reinterpret_cast<uint32&>(OutValue), static_cast<uint32>(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** uint32 版 SerializeUintDelta：直接进底层。 */
void SerializeUintDelta(FNetBitStreamWriter& Writer, const uint32 Value, const uint32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	return Private::SerializeUintDeltaImpl(Writer, Value, PrevValue, SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** uint32 版 DeserializeUintDelta：bMaskOutValue = true, bSignExtendValue = false。 */
void DeserializeUintDelta(FNetBitStreamReader& Reader, uint32& OutValue, const uint32 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	constexpr bool bMaskOutValue = true;
	constexpr bool bSignExtendValue = false;
	return Private::DeserializeUintDeltaImpl<bMaskOutValue, bSignExtendValue>(Reader, OutValue, PrevValue, SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** int64 版 SerializeIntDelta：cast 到 uint64 走底层。 */
void SerializeIntDelta(FNetBitStreamWriter& Writer, const int64 Value, const int64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	return Private::SerializeUintDeltaImpl(Writer, static_cast<uint64>(Value), static_cast<uint64>(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** int64 版 DeserializeIntDelta。 */
void DeserializeIntDelta(FNetBitStreamReader& Reader, int64& OutValue, const int64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	constexpr bool bMaskOutValue = false;
	constexpr bool bSignExtendValue = true;
	return Private::DeserializeUintDeltaImpl<bMaskOutValue, bSignExtendValue>(Reader, reinterpret_cast<uint64&>(OutValue), static_cast<uint64>(PrevValue), SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** uint64 版 SerializeUintDelta。 */
void SerializeUintDelta(FNetBitStreamWriter& Writer, const uint64 Value, const uint64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	return Private::SerializeUintDeltaImpl(Writer, Value, PrevValue, SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/** uint64 版 DeserializeUintDelta。 */
void DeserializeUintDelta(FNetBitStreamReader& Reader, uint64& OutValue, const uint64 PrevValue, const uint8* SmallBitCountTable, const uint32 SmallBitCountTableEntryCount, uint8 LargeBitCount)
{
	constexpr bool bMaskOutValue = true;
	constexpr bool bSignExtendValue = false;
	return Private::DeserializeUintDeltaImpl<bMaskOutValue, bSignExtendValue>(Reader, OutValue, PrevValue, SmallBitCountTable, SmallBitCountTableEntryCount, LargeBitCount);
}

/**
 * QuantizeSignedUnitFloat 实现。
 *
 * BitCount < 24：线性量化
 *   Scale = 2^(BitCount-1) - 1     （1 位给符号，剩余位表示 0..Scale 的振幅）
 *   IntegerValue = round(Value * Scale)（正值+0.5、负值-0.5 方式向外取整）
 *   保证：Value = ±1.0f 可精确往返，量化步长 1/Scale。
 *
 * BitCount ≥ 24：IEEE 754 位模式编码（固定 25 位）
 *   思路：任意 [0.0, 1.0] 的 float 经 "+1.0f" 变到 [1.0, 2.0)，此区间所有浮点共享同一 biased exponent（0x7F）。
 *   于是 RebasedValueAsUint 的低 23 位 = 该浮点的有效位，可直接作为量化值的 significand。
 *   加上 1 位符号 + 1 位 "是否非零"（用于区分 0.0f 与 1.0f，两者 significand 均为 0），总共 25 位。
 *   0.0f 的特殊压缩：若值为 0，significand 位实际上可以省略，只保留符号 + "是否非零" = 0 的 2 位——
 *   这与后续 SerializeSignedUnitFloat 对齐，从而让 0.0f 可以只用 2 位传输（±0 共 2 种组合）。
 */
uint32 QuantizeSignedUnitFloat(float Value, uint32 BitCount)
{
	if (BitCount < 24U)
	{
		// One bit is used for the sign bit
		const float Scale = float((1 << (BitCount - 1)) - 1);
		const float ScaledValue = Value*Scale;
		// If value is -1.0f we want -((1 << (BitCount - 1)) - 1) to be sent, not -((1 << (BitCount - 1)) - 2).
		// This will ensure -1.0f roundtrips perfectly. So round negative values towards -infinity.
		const int32 IntegerValue = int32(ScaledValue + FMath::Sign(Value)*0.5f);
		return static_cast<uint32>(IntegerValue);
	}
	else
	{
		/*
		 * All values in range [1.0f, 2.0f) share the same exponent. By taking note of
		 * the sign and rebase the absolute value from [0.0f, 1.0f] to [1.0f, 2.0f] we
		 * can replicate the float as sign and significand. We also need a bit to
		 * be able to differentiate between 1.0f and 2.0f which both have all zeros as significand.
		 * We use that bit to special case an original value of 0.0f, since that is a common
		 * value. In that case we do not need to replicate the significand, meaning +/- 0.0f
		 * can be replicated with just two bits and all other values with 25 bits.
		 */
		const uint32 ValueAsUint = Private::FloatAsUint32(Value);
		const float RebasedValue = FMath::Abs(Value) + 1.0f;
		const uint32 RebasedValueAsUint = Private::FloatAsUint32(RebasedValue);
		// RebasedValueAsUint == 0x3F800000 → abs(Value) == 0 → 标记 bit 23 为 0（"是否非零" = 0）。
		const uint32 ValueIsNotZero = uint32(RebasedValueAsUint != 0x3F800000U);
		const uint32 ValueSignBit = ValueAsUint >> 31U;
		const uint32 QuantizedValue = (ValueSignBit << 24U) | (ValueIsNotZero << 23U) | (RebasedValueAsUint & ((1U << 23U) - 1U));
		return QuantizedValue;
	}
}

/**
 * DequantizeSignedUnitFloat 实现（与 Quantize 对偶）。
 *
 * BitCount < 24：InvScale = 1/(2^(BitCount-1) - 1)，直接乘回；先把量化值当 int32 做符号扩展。
 *
 * BitCount ≥ 24：
 *  - 取出 Bit 24（符号）、Bit 23（是否非零）；
 *  - 还原 significand 到 [0, 2^23)；
 *  - 特殊情况：若符号为正 + 非零 + significand 全 0，说明原值恰好是 1.0f（Rebased = 2.0f），
 *    此时把 significand 置位到最高（RebasedFloatWasTwo << 23），使 1.0f 精确恢复。
 *  - 加回 0x3F800000 恢复 biased exponent，位模式重解释为 float，再减 1.0f 得到 abs(Value)。
 *  - 用 ((SignAndIsNotZero & 2) << 30) 把符号位塞回 IEEE bit 31。
 */
float DequantizeSignedUnitFloat(uint32 Value, uint32 BitCount)
{
	if (BitCount < 24U)
	{
		const float InvScale = 1.0f/float((1 << (BitCount - 1U)) - 1);
		const float FloatValue = float(int32(Value))*InvScale;
		return FloatValue;
	}
	else
	{
		const uint32 SignAndIsNotZero = Value >> 23U;
		uint32 RebasedFloatAsUint = Value & ((1U << 23U) - 1U);
		const uint32 RebasedFloatWasTwo = (SignAndIsNotZero & 1U) & (RebasedFloatAsUint == 0U);
		RebasedFloatAsUint += 0x3F800000U + (RebasedFloatWasTwo << 23U);

		float FloatValue = Private::Uint32AsFloat(RebasedFloatAsUint) - 1.0f;
		// Apply sign
		FloatValue = Private::Uint32AsFloat(Private::FloatAsUint32(FloatValue) | ((SignAndIsNotZero & 2U) << 30U));
		return FloatValue;
	}
}

}
