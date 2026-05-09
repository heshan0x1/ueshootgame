// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// QuatNetSerializers.cpp
// ---------------------------------------------------------------------------------------------
// 单位四元数 Serializer 实现。
//
// 算法核心 ── "[1, 2] 区间 mantissa 直发 + W 由单位约束恢复"：
//
//   1. Quantize：
//      a) 防御：若 |q|² ≤ SMALL_NUMBER（接近零向量），用 Identity (0,0,0,1) 替代；
//      b) 若非单位 → Normalize；
//      c) Clamp X/Y/Z 到 [-1, 1]（Normalize 不能完全保证浮点精度，再做一道保险）；
//      d) 取符号 → Flags 高 4 bit（XIsNeg/YIsNeg/ZIsNeg/WIsNeg）；
//      e) 把 |X|/|Y|/|Z| 加 1.0 → ∈ [1.0, 2.0]：
//          - IEEE 754 中，[1,2] 区间内的浮点指数固定为 0（127 / 1023 偏移后），
//            所有 23/52 位 mantissa 都用于表达 [1,2] 区间内的精度，**无须发送指数位**。
//          - 这相当于均匀利用 mantissa 精度表达 [0,1]，比直接发 |X| 的 IEEE 754 位
//            （[0,1] 区间指数变化大、低值精度过剩）更经济。
//      f) 若 |X|+1 == 1.0（即 X == 0）→ XIsNotZero=0，整 mantissa 不发；
//      g) 否则把 |X|+1 的 mantissa 23/52 位塞到 QuantizedType.X。
//
//   2. Serialize：
//      - 写 7-bit Flags；
//      - 仅对 IsNotZero 的分量写 SignificandBitCount 位 mantissa（float=23, double=52）；
//      - 注意：32-bit WriteBits 单次最多 32 位，double 路径需拆 32+(N-32) 两次。
//
//   3. Deserialize：
//      - 读 Flags + 各 mantissa 位；
//      - **特殊处理**：若 IsNotZero=1 但 mantissa 读到 0，强制设为 IncreaseExponent（即 2^N）。
//        这避免了线传被篡改产生"声称非零但 mantissa==0 → 实际表示 1.0（指数=0,mantissa=0）→
//        反量化得到 0"的歧义；强制后会反量化为 2.0 - 1.0 = 1.0（饱和上界），保证一致性。
//      - 把 mantissa + FloatOneAsUint（即 1.0 的位） → 得到 [1, 2] 区间浮点 → 等待 Dequantize。
//
//   4. Dequantize：
//      - X = float(QuantizedX) - 1.0  → [0, 1]
//      - W = sqrt(max(0, 1 - X² - Y² - Z²))   ← 单位约束求解
//      - 按 Flags 中的 IsNegative 还原符号（异或 SignBit）。
//
//   5. Validate ：要求严格 IsNormalized。非单位 → 拒绝（业务层应预先 Normalize）。
//
//   6. IsEqual ：
//      - 量化态：直接比较 X/Y/Z + Flags。
//      - 源态  ：归一化后比较 X/Y/Z + W 的符号位（包含 +0/-0 的位级区分），
//        因为 0 与 -0 在四元数中表示同一个旋转，但本 Serializer 保留了 W 符号位的差异。
//
// 精度概要：
//   - X/Y/Z：等价于 IEEE 754 在 [1,2] 区间的精度 ── float ≈ 2^-23 ≈ 1.2e-7；double ≈ 2.2e-16。
//   - W ：依赖 sqrt(1 - X² - Y² - Z²)，当 |W|≈1 时精度损失最大（相对误差 O(eps/|W|)）。
//   - 总位预算（最坏情况）：
//       float：7 + 3*23 = 76 bit
//       double：7 + 3*52 = 163 bit
// =============================================================================================

#include "Iris/Serialization/QuatNetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Math/UnrealMathUtility.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(QuatNetSerializers)

namespace UE::Net::Private
{

/**
 * 单位四元数模板基类。
 * @tparam T 四元数类型 FQuat / FQuat4f / FQuat4d。FloatType 由 T::X 推断。
 */
template<typename T>
struct FUnitQuatNetSerializerBase
{
	using FloatType = decltype(T::X);                                             // float 或 double
	using UintType = typename TUnsignedIntType<sizeof(FloatType)>::Type;          // uint32 或 uint64

	/** Flags 字段位定义：低 3 bit IsNotZero + 高 4 bit IsNegative。 */
	enum EQuantizedFlags : uint32
	{
		// 是否为非零（0 表示 mantissa 不发送，反量化后该分量为 0）。
		XIsNotZero = 1U,
		YIsNotZero = XIsNotZero << 1U,
		ZIsNotZero = YIsNotZero << 1U,

		// 各分量的符号位（包括 W）。注意：bit 索引 3/4/5/6 与 Dequantize 的左移量耦合，
		// 见 Dequantize 中 static_assert 校验。
		XIsNegative = ZIsNotZero << 1U,
		YIsNegative = XIsNegative << 1U,
		ZIsNegative = YIsNegative << 1U,
		WIsNegative = ZIsNegative << 1U,
	};

	/** 量化态：X/Y/Z 的 [1, 2] 区间浮点位 + Flags。注意 X/Y/Z 字段并非整数而是 float 的位。 */
	struct FQuantizedData
	{
		UintType X;          // 浮点位（[1, 2] 区间），未发送时为 0
		UintType Y;
		UintType Z;
		uint32 Flags;        // 7-bit 有效（见 EQuantizedFlags）
	};

	typedef T SourceType;
	typedef FQuantizedData QuantizedType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

private:
	// 浮点 ↔ 整数的 type-pun（用 union，避免严格别名规则）。
	static UintType FloatAsUint(FloatType Value);
	static FloatType UintAsFloat(UintType Value);

	// 把 SourceType（FQuat*）规范为单位四元数（用于 IsEqual 源态对比）。
	static T GetUnitQuat(const T& Value);

	// IEEE 754 关键常量。
	// SignificandBitCount：mantissa 位数（float=23, double=52）。
	static constexpr uint32 SignificandBitCount = (sizeof(FloatType) == 4U ? 23U : 52U);
	// FloatOneAsUint：浮点 1.0 的位模式（float=0x3F800000, double=0x3FF0000000000000）。
	static constexpr UintType FloatOneAsUint = (sizeof(FloatType) == 4U ? 0x3F800000U : 0x3FF0000000000000ULL);
	// 符号位偏移：float=31，double=63。
	static constexpr uint32 SignBitShiftAmount = (sizeof(FloatType) == 4U ? 31U : 63U);
	// 符号位掩码（最高位 1）。
	static constexpr UintType SignBit = UintType(1) << SignBitShiftAmount;
};

/** type-pun：float → uint。union 写法避免 strict aliasing UB。 */
template<typename T>
inline typename FUnitQuatNetSerializerBase<T>::UintType FUnitQuatNetSerializerBase<T>::FloatAsUint(FloatType Value)
{
	union FFloatAsUint
	{
		FloatType Float;
		UintType Uint;
	};
	
	FFloatAsUint FloatAsUint;
	FloatAsUint.Float = Value;
	return FloatAsUint.Uint;
}

/** type-pun：uint → float。 */
template<typename T>
inline typename FUnitQuatNetSerializerBase<T>::FloatType FUnitQuatNetSerializerBase<T>::UintAsFloat(UintType Value)
{
	union FFloatAsUint
	{
		FloatType Float;
		UintType Uint;
	};

	FFloatAsUint FloatAsUint;
	FloatAsUint.Uint = Value;
	return FloatAsUint.Float;
}

/**
 * 把任意四元数规整为"单位四元数 + clamp"：
 *   - 接近零向量 → Identity；
 *   - 否则 Normalize 并把 X/Y/Z clamp 到 [-1, 1]。
 *
 * 此函数仅用于 IsEqual 源态比较，不调用 Quantize 路径。
 */
template<typename T>
T FUnitQuatNetSerializerBase<T>::GetUnitQuat(const T& Value)
{
	constexpr FloatType SmallNumber = FloatType(UE_SMALL_NUMBER);
	T UnitQuat = Value;
	if (UnitQuat.SizeSquared() <= SmallNumber)
	{
		UnitQuat = T::Identity;
	}
	else
	{
		// All transmitted quaternions must be unit quaternions, in which case we can deduce the value of W.
		// 译：所有传输的四元数必须是单位四元数；这样接收端能由约束 X²+Y²+Z²+W²=1 推出 W。
		if (!UnitQuat.IsNormalized())
		{
			UnitQuat.Normalize();
		}

		UnitQuat.X = FMath::Clamp(UnitQuat.X, -FloatType(1), FloatType(1));
		UnitQuat.Y = FMath::Clamp(UnitQuat.Y, -FloatType(1), FloatType(1));
		UnitQuat.Z = FMath::Clamp(UnitQuat.Z, -FloatType(1), FloatType(1));
	}

	return UnitQuat;
}

/**
 * 序列化：写 7-bit Flags + 仅非零分量的 mantissa。
 *
 * 32-bit float 路径：每分量直接 WriteBits(SignificandBitCount=23)。
 * 64-bit double 路径：每分量分两次 WriteBits（低 32 + 高 (52-32)=20 位）。
 */
template<typename T>
void FUnitQuatNetSerializerBase<T>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(Value.Flags, 7U);
	if constexpr (sizeof(FloatType) == 4U)
	{
		// float：每分量 23-bit mantissa（≤32-bit，单次 WriteBits 即可）。
		if (Value.Flags & EQuantizedFlags::XIsNotZero)
		{
			Writer->WriteBits(Value.X, SignificandBitCount);
		}
		if (Value.Flags & EQuantizedFlags::YIsNotZero)
		{
			Writer->WriteBits(Value.Y, SignificandBitCount);
		}
		if (Value.Flags & EQuantizedFlags::ZIsNotZero)
		{
			Writer->WriteBits(Value.Z, SignificandBitCount);
		}
	}
	else
	{
		// double：每分量 52-bit mantissa（>32-bit，分两次）。
		constexpr uint32 BitCountForHighBits = SignificandBitCount - 32U;

		if (Value.Flags & EQuantizedFlags::XIsNotZero)
		{
			Writer->WriteBits(static_cast<uint32>(Value.X), 32U);
			Writer->WriteBits(static_cast<uint32>(Value.X >> 32U), BitCountForHighBits);
		}
		if (Value.Flags & EQuantizedFlags::YIsNotZero)
		{
			Writer->WriteBits(static_cast<uint32>(Value.Y), 32U);
			Writer->WriteBits(static_cast<uint32>(Value.Y >> 32U), BitCountForHighBits);
		}
		if (Value.Flags & EQuantizedFlags::ZIsNotZero)
		{
			Writer->WriteBits(static_cast<uint32>(Value.Z), 32U);
			Writer->WriteBits(static_cast<uint32>(Value.Z >> 32U), BitCountForHighBits);
		}
	}
}

/**
 * 反序列化：读 Flags + mantissa；加 FloatOneAsUint 把 mantissa 还原为 [1, 2] 区间浮点位。
 *
 * **IncreaseExponent 防御**：
 *   - 在正常 Quantize 流程中，"mantissa==0 但 IsNotZero=1" 不可能产生（IsNotZero 的判定是
 *     "|val|+1 != 1.0"，恰好排除 mantissa==0 的情形）；
 *   - 但若线传被篡改 / 协议错配出现这种状态，加 1.0 后等于 1.0（指数=0、mantissa=0），
 *     反量化得 0 —— 与 IsNotZero=1 矛盾。
 *   - 这里把 mantissa==0 替换成 IncreaseExponent（即 1<<23 / 1<<52，恰为指数+1 的位模式），
 *     加 1.0 后 = 2.0，反量化得 1.0（饱和到上界），保证一致性。
 */
template<typename T>
void FUnitQuatNetSerializerBase<T>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	QuantizedType TempValue = {};
	TempValue.Flags = Reader->ReadBits(7U);
	if constexpr (sizeof(FloatType) == 4U)
	{
		// 1<<23 = 0x00800000，加上 1.0 (0x3F800000) = 0x40000000 即 float 2.0f。
		constexpr UintType IncreaseExponent = UintType(1) << SignificandBitCount;

		if (TempValue.Flags & EQuantizedFlags::XIsNotZero)
		{
			TempValue.X = Reader->ReadBits(SignificandBitCount);
			if (TempValue.X == 0)
			{
				// 修复非法状态（详见函数注释）。
				TempValue.X = IncreaseExponent;
			}
		}

		if (TempValue.Flags & EQuantizedFlags::YIsNotZero)
		{
			TempValue.Y = Reader->ReadBits(SignificandBitCount);
			if (TempValue.Y == 0)
			{
				TempValue.Y = IncreaseExponent;
			}
		}

		if (TempValue.Flags & EQuantizedFlags::ZIsNotZero)
		{
			TempValue.Z = Reader->ReadBits(SignificandBitCount);
			if (TempValue.Z == 0)
			{
				TempValue.Z = IncreaseExponent;
			}
		}
	}
	else
	{
		// double 路径，分两次读取拼接 52-bit mantissa。
		constexpr UintType IncreaseExponent = UintType(1) << SignificandBitCount;
		constexpr uint32 BitCountForHighBits = SignificandBitCount - 32U;
		if (TempValue.Flags & EQuantizedFlags::XIsNotZero)
		{
			TempValue.X = Reader->ReadBits(32U);
			TempValue.X |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
			if (TempValue.X == 0)
			{
				TempValue.X = IncreaseExponent;
			}
		}

		if (TempValue.Flags & EQuantizedFlags::YIsNotZero)
		{
			TempValue.Y = Reader->ReadBits(32U);
			TempValue.Y |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
			if (TempValue.Y == 0)
			{
				TempValue.Y = IncreaseExponent;
			}
		}

		if (TempValue.Flags & EQuantizedFlags::ZIsNotZero)
		{
			TempValue.Z = Reader->ReadBits(32U);
			TempValue.Z |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
			if (TempValue.Z == 0)
			{
				TempValue.Z = IncreaseExponent;
			}
		}
	}

	// After adding the integer representation of 1.0f we will have all values in the [1.0f, 2.0f] range when interpreted as floats.
	// 译：加上 1.0 的位表示后，X/Y/Z 三个字段就是 [1.0, 2.0] 区间内的合法浮点位（待 Dequantize）。
	TempValue.X += FloatOneAsUint;
	TempValue.Y += FloatOneAsUint;
	TempValue.Z += FloatOneAsUint;

	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);
	Value = TempValue;
}

/**
 * 量化（核心）：FQuat → FQuantizedData。
 *
 * 关键步骤：
 *   1. 防御零向量：|q|² ≤ SMALL_NUMBER 时用 Identity 替代。
 *   2. 强制 Normalize（防止非单位输入导致 sqrt 失败）。
 *   3. Clamp 到 [-1, 1]。
 *   4. 取符号位 → Flags 高 4 bit（异或 SignBit 提取，左移到 Flags 对应位置）。
 *   5. 把 |X|/|Y|/|Z| + 1.0 → 浮点位 → 提取 mantissa。
 *   6. 若 mantissa==0（即 |val|==0），IsNotZero=0，省略发送。
 */
template<typename T>
void FUnitQuatNetSerializerBase<T>::Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	SourceType TempSource = Source;
	QuantizedType TempValue = {};

	if (TempSource.SizeSquared() <= SMALL_NUMBER)
	{
		// 接近零向量 → Identity。
		TempSource = SourceType(FloatType(0), FloatType(0), FloatType(0), FloatType(1));
	}
	else
	{
		// All transmitted quaternions must be unit quaternions, in which case we can deduce the value of W.
		// 译：传输的必须是单位四元数；非单位则强制 Normalize。
		if (!TempSource.IsNormalized())
		{
			TempSource.Normalize();
		}

		TempSource.X = FMath::Clamp(TempSource.X, -FloatType(1), FloatType(1));
		TempSource.Y = FMath::Clamp(TempSource.Y, -FloatType(1), FloatType(1));
		TempSource.Z = FMath::Clamp(TempSource.Z, -FloatType(1), FloatType(1));
	}
	
	// 提取符号位：FloatAsUint(V) >> SignBitShiftAmount 得 0 或 1，乘以对应掩码即可放进 Flags。
	TempValue.Flags |= EQuantizedFlags::XIsNegative*(FloatAsUint(TempSource.X) >> SignBitShiftAmount);
	TempValue.Flags |= EQuantizedFlags::YIsNegative*(FloatAsUint(TempSource.Y) >> SignBitShiftAmount);
	TempValue.Flags |= EQuantizedFlags::ZIsNegative*(FloatAsUint(TempSource.Z) >> SignBitShiftAmount);
	TempValue.Flags |= EQuantizedFlags::WIsNegative*(FloatAsUint(TempSource.W) >> SignBitShiftAmount);

	// Rebase the X, Y and Z components to end up in the range [1.0, 2.0], which allows us to not replicate the exponent
	// except for a bit to differentiate between 1.0 and 2.0.
	// 译：把 |X|/|Y|/|Z| 加 1 重映射到 [1, 2]，从而省去指数位的传输（[1,2] 的指数固定为 0/127/1023）。
	// 这里利用了 IEEE 754 的"次正规规避"——[1,2] 区间所有浮点都共享同一指数。
	TempSource.X = FGenericPlatformMath::Abs(TempSource.X) + FloatType(1);
	TempSource.Y = FGenericPlatformMath::Abs(TempSource.Y) + FloatType(1);
	TempSource.Z = FGenericPlatformMath::Abs(TempSource.Z) + FloatType(1);

	// If the value is 1.0 after rebasing then we denote that to avoid replicating the significand.
	// 译：重映射后若仍为 1.0（说明原值为 0），mantissa 为 0，无需传输——置 IsNotZero=0。
	TempValue.Flags |= EQuantizedFlags::XIsNotZero*(FloatAsUint(TempSource.X) != FloatOneAsUint);
	TempValue.Flags |= EQuantizedFlags::YIsNotZero*(FloatAsUint(TempSource.Y) != FloatOneAsUint);
	TempValue.Flags |= EQuantizedFlags::ZIsNotZero*(FloatAsUint(TempSource.Z) != FloatOneAsUint);

	// 注意：这里直接存浮点位（含指数），Serialize 时只取 mantissa 部分（低 SignificandBitCount 位）。
	// 因为 [1, 2] 的指数位都是固定的（fp32: 0x3F800000 高位），低位即是 mantissa，
	// WriteBits(SignificandBitCount) 只写低位，刚好是 mantissa。
	TempValue.X = FloatAsUint(TempSource.X);
	TempValue.Y = FloatAsUint(TempSource.Y);
	TempValue.Z = FloatAsUint(TempSource.Z);

	Target = TempValue;
}

/**
 * 反量化：FQuantizedData → FQuat。
 *
 * 步骤：
 *   1. 把 [1, 2] 区间浮点位（在 Deserialize 已加好 1.0）转回 float，再减 1.0 → ∈ [0, 1]。
 *   2. 由单位约束求 W：W = sqrt(max(0, 1 - X² - Y² - Z²))（max 防止数值误差导致负数）。
 *   3. 用 Flags 中的符号位还原 X/Y/Z/W 的符号——直接 OR 到浮点位的最高位。
 */
template<typename T>
void FUnitQuatNetSerializerBase<T>::Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	// Rebase to [0.0, 1.0]
	// 译：先把存储的 [1,2] 浮点减 1.0，得 [0,1]（始终是非负绝对值）。
	SourceType TempValue;
	TempValue.X = UintAsFloat(Source.X) - FloatType(1);
	TempValue.Y = UintAsFloat(Source.Y) - FloatType(1);
	TempValue.Z = UintAsFloat(Source.Z) - FloatType(1);
	TempValue.W = FloatType(0);
	// Deduce W
	// 译：用单位约束推 W 的绝对值。max(0, ...) 避免 1 - sumSq 因浮点误差略小于 0 时的 NaN。
	TempValue.W = FPlatformMath::Sqrt(FMath::Max(FloatType(0), FloatType(1) - TempValue.SizeSquared()));

	// Apply signs.
	// 译：把 Flags 里的符号位"挪回"对应浮点的最高位。
	// 数学：(Source.Flags & XIsNegative) >> 3 == 0 或 1（提取出 1-bit 符号），
	// 再左移 (SignBitShiftAmount - 3) 把它移到浮点最高位。
	// 同理 Y/Z/W 的符号位 bit 位置 4/5/6，所以左移量为 SignBitShiftAmount - {4,5,6}。
	static_assert((EQuantizedFlags::XIsNegative >> 3U) == 1U, "EQuantizedFlags are broken.");
	static_assert((EQuantizedFlags::YIsNegative >> 4U) == 1U, "EQuantizedFlags are broken.");
	static_assert((EQuantizedFlags::ZIsNegative >> 5U) == 1U, "EQuantizedFlags are broken.");
	static_assert((EQuantizedFlags::WIsNegative >> 6U) == 1U, "EQuantizedFlags are broken.");

	TempValue.X = UintAsFloat(FloatAsUint(TempValue.X) | (UintType(Source.Flags & EQuantizedFlags::XIsNegative) << (SignBitShiftAmount - 3U)));
	TempValue.Y = UintAsFloat(FloatAsUint(TempValue.Y) | (UintType(Source.Flags & EQuantizedFlags::YIsNegative) << (SignBitShiftAmount - 4U)));
	TempValue.Z = UintAsFloat(FloatAsUint(TempValue.Z) | (UintType(Source.Flags & EQuantizedFlags::ZIsNegative) << (SignBitShiftAmount - 5U)));
	TempValue.W = UintAsFloat(FloatAsUint(TempValue.W) | (UintType(Source.Flags & EQuantizedFlags::WIsNegative) << (SignBitShiftAmount - 6U)));

	Target = TempValue;
}

/**
 * IsEqual ：
 *   - 量化态：直接比较 X/Y/Z + Flags 位精确。
 *   - 源态  ：先把两个源四元数都规整为 UnitQuat，再比较 X/Y/Z + W 的符号位。
 *     这里只比较 W 的符号是因为：W 的绝对值由 X²+Y²+Z² 唯一确定，所以符号是唯一独立信息。
 *     这与量化语义保持一致。
 */
template<typename T>
bool FUnitQuatNetSerializerBase<T>::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		return ((Value0.X == Value1.X) & (Value0.Y == Value1.Y) & (Value0.Z == Value1.Z) & (Value0.Flags == Value1.Flags));
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		// Lightweight version of the quantization. We do not rebase the values in this case.
		// 译：轻量版量化——只 Normalize/Clamp，不做 [1,2] 重映射。
		SourceType UnitQuat0 = GetUnitQuat(SourceValue0);
		SourceType UnitQuat1 = GetUnitQuat(SourceValue1);

		return ((UnitQuat0.X == UnitQuat1.X) & (UnitQuat0.Y == UnitQuat1.Y) & (UnitQuat0.Z == UnitQuat1.Z)
			& ((FloatAsUint(UnitQuat0.W) & SignBit) == (FloatAsUint(UnitQuat1.W) & SignBit)));
	}
}

/**
 * Validate ：严格要求 IsNormalized()。非单位四元数 → 拒绝。
 *
 * 注意：本 Validate 比 Quantize 更严格——Quantize 会容忍非单位（强制 Normalize），
 * Validate 则把责任推回业务层："如果你声称传单位四元数，就必须真的是单位"。
 *
 * NaN 不需要单独检查：IsNormalized 对 NaN 返回 false（NaN != NaN）。
 */
template<typename T>
bool FUnitQuatNetSerializerBase<T>::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<SourceType*>(Args.Source);
	// Require unit quats.
	if (!Value.IsNormalized())
	{
		return false;
	}

	return true; 
}

}

namespace UE::Net
{

// 编译期断言：FQuat / FQuat4f / FQuat4d 的标量类型符合预期。
static_assert(std::is_same<decltype(FQuat::X), float>::value || std::is_same<decltype(FQuat::X), double>::value, "Unknown floating point type in FQuat.");
static_assert(std::is_same<decltype(FQuat4f::X), float>::value, "Unknown floating point type in FQuat4f.");
static_assert(std::is_same<decltype(FQuat4d::X), double>::value, "Unknown floating point type in FQuat4d.");

/** FUnitQuatNetSerializer：FQuat（默认精度，UE LWC 下为 double）。 */
struct FUnitQuatNetSerializer : public Private::FUnitQuatNetSerializerBase<FQuat>
{
	static const uint32 Version = 0;

	typedef FUnitQuatNetSerializerConfig ConfigType;
	static const ConfigType DefaultConfig;

};
const FUnitQuatNetSerializer::ConfigType FUnitQuatNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FUnitQuatNetSerializer);

/** FUnitQuat4fNetSerializer：FQuat4f（强 float，每分量 23-bit）。 */
struct FUnitQuat4fNetSerializer : public Private::FUnitQuatNetSerializerBase<FQuat4f>
{
	static const uint32 Version = 0;

	typedef FUnitQuat4fNetSerializerConfig ConfigType;
	static const ConfigType DefaultConfig;

};
const FUnitQuat4fNetSerializer::ConfigType FUnitQuat4fNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FUnitQuat4fNetSerializer);

/** FUnitQuat4dNetSerializer：FQuat4d（强 double，每分量 52-bit）。 */
struct FUnitQuat4dNetSerializer : public Private::FUnitQuatNetSerializerBase<FQuat4d>
{
	static const uint32 Version = 0;

	typedef FUnitQuat4dNetSerializerConfig ConfigType;
	static const ConfigType DefaultConfig;

};
const FUnitQuat4dNetSerializer::ConfigType FUnitQuat4dNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FUnitQuat4dNetSerializer);

}
