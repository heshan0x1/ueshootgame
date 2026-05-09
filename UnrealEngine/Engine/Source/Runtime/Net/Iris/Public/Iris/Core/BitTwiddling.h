// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PlatformMath.h"
#include "Templates/EnableIf.h"
#include "Templates/IsSigned.h"
#include "Traits/IntType.h"

// ============================================================================
// BitTwiddling —— Iris 序列化层常用的位宽/位工具集合
// ----------------------------------------------------------------------------
// 设计要点：
//   - 所有函数都是模板 + SFINAE（`TEnableIf`），按"有/无符号 × 4 字节/8 字节"
//     拆分成 4 套实现，编译期选择最紧凑的指令（CountLeadingZeros / 64）；
//   - 返回值类型统一 uint32，便于直接作为 FNetBitStream 的写入/读取位宽；
//   - 不使用 `constexpr` 修饰（因为底层 FPlatformMath::CountLeadingZeros 并
//     不保证 constexpr），但所有调用点均为纯运算，编译器通常能在 -O2 下
//     内联到单条 `BSR/CLZ` 指令；
//   - 被 `FIntNetSerializer` / `FUintNetSerializer` / Range/Packed 系列 Serializer
//     以及 BitPacking（Delta 整数）大量使用。
// ============================================================================

namespace UE::Net
{

/**
 * GetBitsNeeded returns the number of bits needed to serialize the value and be able to deserialize it to the
 * original value. For signed integers this means there always need to be room for the sign bit. On the
 * receiving side the most significant bit received will be propagated to all higher bits. For unsigned values
 * no consideration needs to be taken for sign.
 *
 * Examples: 
 * int(-1) will return 1
 * int(0) will return 1
 * int(1) will return 2
 * unsigned(0) will return 0
 * unsigned(1) will return 1
 *
 * @param Value An integer value.
 * @return The number of bits needed to be able to properly reconstruct the value.
 *         For signed integers a sign bit is assumed to be propagated to all higher bits.
 *
 * ----------------------------------------------------------------------------
 * 中文说明：
 *   返回"序列化 Value 并且能够无损还原"所需要的最少比特数。
 *   - 对于**有符号整数**：结果包含符号位（接收侧通过符号位扩展得到原值）。
 *     例：-1 → 1 位（只需一个 1 即可扩展为全 1）；0 → 1 位；1 → 2 位（一个 0 符号位 + 一个 1）。
 *   - 对于**无符号整数**：结果就是最高 set bit 的位置 + 1。
 *     例：0 → 0 位（无需任何位）；1 → 1 位。
 *
 *   SFINAE 分支：以下四个重载按 `TIsSigned × sizeof(T)<=4/==8` 四种组合区分，
 *   `TEnableIf` 给每个重载绑定一个"只有该分支才合法"的默认模板参数，保证编译器
 *   只会选中其中一个，避免歧义。
 * ----------------------------------------------------------------------------
 */
// [有符号 ≤4 字节] 分支
template<typename T, typename TEnableIf<TIsSigned<T>::Value && sizeof(T) <= 4U, int32>::Type X = -1>
uint32 GetBitsNeeded(const T Value)
{
	// SmallUnsignedType：取 sizeof(T) 对应的无符号类型，用于把混算升级后的
	// 结果截回原宽度，避免 uint32 强转时引入"高位 0"污染最高位。
	typedef typename TUnsignedIntType<sizeof(T)>::Type SmallUnsignedType;

	/* The algorithm for signed integers works as described below.
	 *
	 * 1. Replicate the sign bit to the right, by assuming right - shift on signed integers propagates the sign - bit.
	 *    This will create a bit pattern consisting of all ones for negative numbersand all zeros for positive numbers.
	 * 2. Exclusive - or the bit - pattern with the original value.This will do nothing for positive numbers, but for
	 *    negative this will clear out all the top bits that were set to 1 and then set the most significant zero bit to 1.
	 * 3. The resulting value from step 2 makes it possible to find the most significant bit set apart from sign - bits that
	 *    can easily be derived from the bit above.The number of bits needed can now be calculated as 1 for the sign
	 *    plus the number of bits in the type minus how many zero bits in the value after the most significant set bit.
	 *
	 * ------------------------------------------------------------------
	 * 中文复述：
	 *   1) `Value >> (N-1)` 把"最高位"（符号位）算术扩展到整个宽度：
	 *        正数 → 全 0；负数 → 全 1。
	 *   2) 与原值按位异或：正数不变；负数把全部前导 1 清零、并把"第一个
	 *      本来是 0 的位"翻成 1（也就是"最高有意义位"的位置）。
	 *   3) 对这个 massaged 值做 CountLeadingZeros，即可得到"最高有意义位"
	 *      的位置；再 +1 即补上符号位。
	 *   最终公式：33 - CLZ(massaged_as_uint32)。
	 *   这里采用 33 而不是 32，是因为"有符号还要 +1 位符号"，等价于
	 *   `(32 - CLZ) + 1`。
	 * ------------------------------------------------------------------
	 */
	const uint32 MassagedValue = uint32(SmallUnsignedType(T(Value ^ (Value >> (sizeof(T)*8 - 1U)))));
	return 33U - static_cast<uint32>(FPlatformMath::CountLeadingZeros(MassagedValue));
}

// [无符号 ≤4 字节] 分支：直接 CLZ，不需要符号位修正。
template<typename T, typename TEnableIf<!TIsSigned<T>::Value && sizeof(T) <= 4U, uint32>::Type X = 1U>
uint32 GetBitsNeeded(const T Value)
{
	// 32 - CLZ 即为最高 set bit 位置 + 1；Value==0 时 CLZ==32，结果为 0（无需任何位）。
	return 32U - static_cast<uint32>(FPlatformMath::CountLeadingZeros(uint32(Value)));
}

// [有符号 8 字节] 分支：与 32 位同理，右移 63 位得到符号扩展。
template<typename T, typename TEnableIf<TIsSigned<T>::Value && sizeof(T) == 8U, int64>::Type X = -1LL>
uint32 GetBitsNeeded(const T Value)
{
	// 64 位算法，最终 +1 位符号 → 65 - CLZ64。
	const uint64 MassagedValue = uint64(Value ^ (Value >> 63U));
	return 65U - static_cast<uint32>(FPlatformMath::CountLeadingZeros64(MassagedValue));
}

// [无符号 8 字节] 分支：直接 CLZ64。
template<typename T, typename TEnableIf<!TIsSigned<T>::Value && sizeof(T) == 8U, uint64>::Type X = 1ULL>
uint32 GetBitsNeeded(const T Value)
{
	return 64U - static_cast<uint32>(FPlatformMath::CountLeadingZeros64(Value));
}

/**
 * GetBitsNeededForRange returns the number of bits needed to represent any value in the 
 * range [LowerBound, UpperBound]. This number is calculated as the number of bits needed
 * to express Value - LowerBound treated as an unsigned value.
 * @param LowerBound The lowest integer value in the range.
 * @param UpperBound The highest value in the range.
 * @return The number of bits needed to represent any value in the range, assuming user always knows 
 * @note It is assumed UpperBound >= LowerBound.
 *
 * ----------------------------------------------------------------------------
 * 中文说明：
 *   计算编码"区间 [LowerBound, UpperBound] 内任意值"所需的位宽。
 *   算法：把 `UpperBound - LowerBound` 当作无符号整数取位宽——因为任何值
 *   减去下界后落入 [0, Range]，属于无符号范畴，无需符号位。
 *   典型用法：RangeNetSerializer / PackedInt / ReplicationIndex 等"范围
 *   有限"的整数字段可以显著降低位宽。
 *   前置条件：调用方必须保证 UpperBound >= LowerBound，内部不检查。
 * ----------------------------------------------------------------------------
 */
// [有符号 ≤4 字节] 分支
template<typename T, typename TEnableIf<TIsSigned<T>::Value && sizeof(T) <= 4U, int32>::Type X = -1>
uint32 GetBitsNeededForRange(const T LowerBound, const T UpperBound)
{
	using UnsignedType = typename TUnsignedIntType<sizeof(T)>::Type;
	// 先按目标类型的无符号形式相减（利用补码溢出回绕属性得到正确 Range），
	// 再扩展到 uint32 做 CLZ，避免符号扩展造成的错误位宽。
	const uint32 Range = uint32(UnsignedType(UnsignedType(UpperBound) - UnsignedType(LowerBound)));
	return 32U - static_cast<uint32>(FPlatformMath::CountLeadingZeros(Range));
}

// [无符号 ≤4 字节] 分支：无需先转无符号，直接相减即可。
template<typename T, typename TEnableIf<!TIsSigned<T>::Value && sizeof(T) <= 4U, uint32>::Type X = 1U>
uint32 GetBitsNeededForRange(const T LowerBound, const T UpperBound)
{
	const uint32 Range = uint32(UpperBound) - uint32(LowerBound);
	return 32U - static_cast<uint32>(FPlatformMath::CountLeadingZeros(Range));
}

// [8 字节，签名与无符号共用] 分支：
//   对于 64 位，有/无符号的相减结果在 uint64 中"位型相同"，所以共用一版即可。
template<typename T, typename TEnableIf<sizeof(T) == 8U, uint64>::Type X = 1ULL>
uint32 GetBitsNeededForRange(const T LowerBound, const T UpperBound)
{
	const uint64 Range = uint64(UpperBound) - uint64(LowerBound);
	return 64U - static_cast<uint32>(FPlatformMath::CountLeadingZeros64(Range));
}

/**
 * GetLeastSignificantBit returns the least significant bit set in Value, or 0 if none is set.
 *
 * ----------------------------------------------------------------------------
 * 中文说明：
 *   取 Value 中"最低位为 1 的那一位"对应的数值（不是位下标，而是形如 1/2/4/8 的 2 的幂）。
 *   经典位运算技巧：`Value & -Value`（补码下，-Value 等价于 ~Value+1）。
 *   - 对于无符号类型，直接取负会警告/未定义，因此先转成有符号再取负，再转回无符号。
 *   - Value==0 时返回 0；否则返回最低 set bit 的数值。
 *
 *   在 Iris 中常被 ChangeMask 遍历、Condition 位图、Group 集合等用于
 *   "快速取出最低置位并清零之"的循环（`while(mask) { b=LSB(mask); mask^=b; }`）。
 * ----------------------------------------------------------------------------
 */
template<typename T, typename TEnableIf<!TIsSigned<T>::Value, int>::Type X = 1>
T GetLeastSignificantBit(const T Value)
{
	using SignedT = typename TSignedIntType<sizeof(T)>::Type;
	const T LeastSignificantBit = Value & T(-SignedT(Value));
	return LeastSignificantBit;
}

}

