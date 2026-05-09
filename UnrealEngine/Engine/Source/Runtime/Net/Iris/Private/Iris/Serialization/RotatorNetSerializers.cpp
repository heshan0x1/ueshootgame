// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// RotatorNetSerializers.cpp
// ---------------------------------------------------------------------------------------------
// 旋转器 Serializer 的实现。
//
// 两个模板基类：
//   - FRotatorAsShortNetSerializerBase<RotatorType>：每分量 16-bit。
//   - FRotatorAsByteNetSerializerBase<RotatorType> ：每分量 8-bit。
//
// 量化算法（依赖 UE FRotator 内置工具）：
//   - CompressAxisToByte (Angle)  → uint8 ，等价于 (uint8)round(NormalizeAxis(Angle) * 256 / 360)
//                                  其中 NormalizeAxis 把角度规整到 (-180, 180]。
//   - CompressAxisToShort(Angle)  → uint16 ，类似但 *65536/360。
//
// 反量化：DecompressAxisFromByte/Short——单位转换回角度（约 1.40625°/byte 单位 或 0.0055°/short 单位）。
//
// **±180° 回绕**：CompressAxis* 通过 NormalizeAxis 把任意角度（包括 ±180°、>360°、<−360°）
//   全都映射到 (-180, 180]，不会出现别的值；客户端 Decompress 后值也保持在 (-180, 180]。
//
// IsEqual ：
//   - 量化态：直接 16-bit/8-bit 字段比较（含 XYZIsNotZero flags）。
//   - 源态  ：直接 Pitch/Yaw/Roll 浮点比较。⚠ 注意未量化前 +0/-0 会被视为相等（IEEE ==），
//     这与 Float/Vector Serializer 的策略不同；但这里不会引发 ChangeMask 抖动，因为在
//     QuantizedType 进入 ChangeMask 前已经走过 Quantize 取整，±0 都映射为 0。
//
// SerializeDelta ：
//   - AsShort ：以 (D = curr - prev) 形式发送 16-bit 差值。注意：该差值如果跨越 ±180°，
//     uint16 自然回绕（符合"圆环"语义）；接收端 (prev + D) 也用 uint16 回绕。
//   - AsByte  ：注释里说"差分意义不大，直接全发"。所以 AsByte 的 SerializeDelta 实际是
//     "ChangedMask + 8-bit 完整重发"模式（不是真差分）。
//
// Validate ：拒绝 NaN（任意角度均合法，包括 ±180/任意大值，CompressAxis 会规整）。
// =============================================================================================

#include "Iris/Serialization/RotatorNetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Math/Rotator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RotatorNetSerializers)

namespace UE::Net
{

/**
 * AsShort 旋转器基类（每分量 16-bit）。
 * @tparam RotatorType FRotator / FRotator3f / FRotator3d。
 *
 * 量化态布局：3-bit 非零掩码 + 三个 16-bit 分量（按需）。
 */
template<typename RotatorType>
struct FRotatorAsShortNetSerializerBase
{
	struct FQuantizedType
	{
		// Using three bits to indicate whether the components are zero or not.
		// 译：3 bit 表示 X/Y/Z 是否为非零（位 0/1/2 对应 X/Y/Z）。
		uint16 XYZIsNotZero;
		uint16 X;     // CompressAxisToShort(Pitch)
		uint16 Y;     // CompressAxisToShort(Yaw)
		uint16 Z;     // CompressAxisToShort(Roll)
	};

	typedef RotatorType SourceType;
	typedef FQuantizedType QuantizedType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

protected:
	using ScalarType = decltype(SourceType::Pitch);

	// XYZIsNotZero 字段的位掩码（在 SerializeDelta 中也复用为 XYZDiffersMask 命名）。
	enum Constants : uint16
	{
		XDiffersMask = 1U,    // bit 0 = X (Pitch) 非零/有差异
		YDiffersMask = 2U,    // bit 1 = Y (Yaw)
		ZDiffersMask = 4U,    // bit 2 = Z (Roll)

		XYZDiffersMask = 7U,  // 全部三 bit
	};
};

/**
 * AsByte 旋转器基类（每分量 8-bit）。
 * @tparam RotatorType FRotator / FRotator3f / FRotator3d（虽然 AsByte 当前只对 FRotator 实例化）。
 */
template<typename RotatorType>
struct FRotatorAsByteNetSerializerBase
{
	struct FQuantizedType
	{
		// Using three bits to indicate whether the components are zero or not.
		// 译：3 bit 表示 X/Y/Z 是否为非零；与 AsShort 相同语义。
		uint8 XYZIsNotZero;
		uint8 X;     // CompressAxisToByte(Pitch)
		uint8 Y;     // CompressAxisToByte(Yaw)
		uint8 Z;     // CompressAxisToByte(Roll)
	};

	typedef RotatorType SourceType;
	typedef FQuantizedType QuantizedType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

protected:
	using ScalarType = decltype(SourceType::Pitch);

	enum Constants : uint16
	{
		XDiffersMask = 1U,
		YDiffersMask = 2U,
		ZDiffersMask = 4U,

		XYZDiffersMask = 7U,
	};
};

// FRotatorAsShortNetSerializerBase implementation
//-----------------------------------------------------------------------------

/**
 * 序列化（AsShort）：3-bit XYZIsNotZero header + 每非零分量 16-bit。
 *   - Identity 旋转   → 3 bit
 *   - 一轴非零        → 3 + 16 = 19 bit
 *   - 全部非零        → 3 + 48 = 51 bit
 */
template<typename T>
void FRotatorAsShortNetSerializerBase<T>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(Value.XYZIsNotZero, 3U);
	if (Value.XYZIsNotZero & XDiffersMask)
	{
		Writer->WriteBits(Value.X, 16U);
	}
	if (Value.XYZIsNotZero & YDiffersMask)
	{
		Writer->WriteBits(Value.Y, 16U);
	}
	if (Value.XYZIsNotZero & ZDiffersMask)
	{
		Writer->WriteBits(Value.Z, 16U);
	}
}

/** 反序列化（AsShort）：与 Serialize 对称。XYZIsNotZero 决定哪些分量需要读 16 bit。 */
template<typename T>
void FRotatorAsShortNetSerializerBase<T>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	QuantizedType Value;
	const uint16 XYZIsNotZero = static_cast<uint16>(Reader->ReadBits(3U));
	Value.XYZIsNotZero = XYZIsNotZero;
	Value.X = static_cast<uint16>(XYZIsNotZero & XDiffersMask ? Reader->ReadBits(16U) : 0U);
	Value.Y = static_cast<uint16>(XYZIsNotZero & YDiffersMask ? Reader->ReadBits(16U) : 0U);
	Value.Z = static_cast<uint16>(XYZIsNotZero & ZDiffersMask ? Reader->ReadBits(16U) : 0U);

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = Value;
}

/**
 * 增量序列化（AsShort）：发送 16-bit 差分（uint16 自然回绕）。
 *
 * Per component equality. With the current scaling a 10 degree change would require 12 bits to replicate
 * due to 11 bits for the value and 1 for the sign. Add a small lookup table index for that and you're
 * up to 14 bits instead of 1+16 bits.
 *
 * 译：按分量做差分。当前 16-bit/分量缩放下，10° 变化需要 12 bit（11 数值 + 1 符号）；
 *     再加上一个小查表索引（即 ChangedMask 3 bit）合计 14 bit，对比直接全发 17(=1+16) bit
 *     更划算。
 *
 * 注意：当前实现仍发完整 16-bit 差分（不是变长查表）；上面注释是优化思路 TODO。
 *
 * 比特布局：[XYZDiffers : 3] [DX : 16]? [DY : 16]? [DZ : 16]?
 */
template<typename T>
void FRotatorAsShortNetSerializerBase<T>::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	// Per component equality. With the current scaling a 10 degree change would require 12 bits to replicate
	// due to 11 bits for the value and 1 for the sign. Add a small lookup table index for that and you're
	// up to 14 bits instead of 1+16 bits.

	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	const QuantizedType& PrevValue = *reinterpret_cast<QuantizedType*>(Args.Prev);

	// uint16 减法自动回绕，保证差值符合"圆环"语义（跨 ±180° 不会爆炸）。
	const uint16 DX = Value.X - PrevValue.X;
	const uint16 DY = Value.Y - PrevValue.Y;
	const uint16 DZ = Value.Z - PrevValue.Z;

	uint32 XYZDiffers = 0;
	XYZDiffers |= (DX != 0) ? uint32(XDiffersMask) : uint32(0);
	XYZDiffers |= (DY != 0) ? uint32(YDiffersMask) : uint32(0);
	XYZDiffers |= (DZ != 0) ? uint32(ZDiffersMask) : uint32(0);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(XYZDiffers, 3U);
	if (XYZDiffers & XDiffersMask)
	{
		Writer->WriteBits(DX, 16U);
	}
	if (XYZDiffers & YDiffersMask)
	{
		Writer->WriteBits(DY, 16U);
	}
	if (XYZDiffers & ZDiffersMask)
	{
		Writer->WriteBits(DZ, 16U);
	}
}

/**
 * 增量反序列化（AsShort）：(Prev + D) uint16 回绕得到当前值。
 * 同时根据当前值重建 XYZIsNotZero flags（因为差分路径不直接传 flags）。
 */
template<typename T>
void FRotatorAsShortNetSerializerBase<T>::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	const QuantizedType& PrevValue = *reinterpret_cast<QuantizedType*>(Args.Prev);
	QuantizedType TempValue = PrevValue;

	const uint32 XYZDiffers = Reader->ReadBits(3U);
	if (XYZDiffers & XDiffersMask)
	{
		const uint16 DX = static_cast<uint16>(Reader->ReadBits(16U));
		TempValue.X += DX;          // uint16 回绕
	}
	if (XYZDiffers & YDiffersMask)
	{
		const uint16 DY = static_cast<uint16>(Reader->ReadBits(16U));
		TempValue.Y += DY;
	}
	if (XYZDiffers & ZDiffersMask)
	{
		const uint16 DZ = static_cast<uint16>(Reader->ReadBits(16U));
		TempValue.Z += DZ;
	}

	// Reconstruct flags
	// 译：当前值的 flags 必须由当前数值重建，否则后续 IsEqual / Serialize 会用到错误的标志。
	uint16 XYZIsNotZero = 0U;
	XYZIsNotZero |= (TempValue.X != 0 ? XDiffersMask : 0U);
	XYZIsNotZero |= (TempValue.Y != 0 ? YDiffersMask : 0U);
	XYZIsNotZero |= (TempValue.Z != 0 ? ZDiffersMask : 0U);
	TempValue.XYZIsNotZero = XYZIsNotZero;

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = TempValue;
}

/**
 * 量化（AsShort）：FRotator → 3×uint16。
 *   - CompressAxisToShort 内部先 NormalizeAxis 把角度规整到 (-180, 180]，
 *     再乘以 65536/360（即 ~182.04）后取整。任何输入角度（含 ±180、>360、<−360）都安全。
 *   - XYZIsNotZero 标志位在量化时计算（≠ 0 即设位）。
 */
template<typename T>
void FRotatorAsShortNetSerializerBase<T>::Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	QuantizedType TempValue = {};

	// FRotator 字段顺序：Pitch (X 轴绕)、Yaw (Z 轴绕)、Roll (X 轴绕)。
	// QuantizedType 字段命名 X/Y/Z 与 Pitch/Yaw/Roll 一一对应（不严格代表笛卡尔轴）。
	TempValue.X = SourceType::CompressAxisToShort(Source.Pitch);
	TempValue.Y = SourceType::CompressAxisToShort(Source.Yaw);
	TempValue.Z = SourceType::CompressAxisToShort(Source.Roll);

	TempValue.XYZIsNotZero |= (TempValue.X != 0) ? XDiffersMask : uint16(0);
	TempValue.XYZIsNotZero |= (TempValue.Y != 0) ? YDiffersMask : uint16(0);
	TempValue.XYZIsNotZero |= (TempValue.Z != 0) ? ZDiffersMask : uint16(0);

	Target = TempValue;
}

/** 反量化（AsShort）：DecompressAxisFromShort 把 16-bit 单位转回度数（值 ∈ (-180, 180]）。 */
template<typename T>
void FRotatorAsShortNetSerializerBase<T>::Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	SourceType TempValue;

	TempValue.Pitch = SourceType::DecompressAxisFromShort(Source.X);
	TempValue.Yaw = SourceType::DecompressAxisFromShort(Source.Y);
	TempValue.Roll = SourceType::DecompressAxisFromShort(Source.Z);

	Target = TempValue;
}

/**
 * IsEqual（AsShort）：
 *   - 量化态：比较 X/Y/Z + flags（含 flags 是为了与序列化输出严格一致）。
 *   - 源态  ：直接 Pitch/Yaw/Roll 浮点比较。⚠ 注意此处 +0/-0 会被 IEEE == 视为相等，
 *     但因为 Quantize 后 ±0 都映射为 0，ChangeMask 不会抖动。
 *     另：未规整的 360° 与 0°（角度等价但浮点 ≠）会被视为不等——属于源态语义。
 */
template<typename T>
bool FRotatorAsShortNetSerializerBase<T>::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& SourceValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& SourceValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		const bool bXIsEqual = (SourceValue0.X == SourceValue1.X);
		const bool bYIsEqual = (SourceValue0.Y == SourceValue1.Y);
		const bool bZIsEqual = (SourceValue0.Z == SourceValue1.Z);
		const bool bFlagsAreEqual = (SourceValue0.XYZIsNotZero == SourceValue1.XYZIsNotZero);
		return bXIsEqual & bYIsEqual & bZIsEqual & bFlagsAreEqual;
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		const bool bPitchIsEqual = (SourceValue0.Pitch == SourceValue1.Pitch);
		const bool bYawIsEqual = (SourceValue0.Yaw == SourceValue1.Yaw);
		const bool bRollIsEqual = (SourceValue0.Roll == SourceValue1.Roll);
		return bPitchIsEqual & bYawIsEqual & bRollIsEqual;
	}
}

/** Validate（AsShort）：拒绝 NaN。任意角度（含 ±180、超大值）合法（量化时会规整）。 */
template<typename T>
bool FRotatorAsShortNetSerializerBase<T>::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<SourceType*>(Args.Source);
	return !Value.ContainsNaN();
}

// FRotatorAsByteNetSerializerBase implementation
//-----------------------------------------------------------------------------

/**
 * 序列化（AsByte）：3-bit flags + 每非零分量 8-bit。
 * Identity → 3 bit；全非零 → 3 + 24 = 27 bit。
 */
template<typename T>
void FRotatorAsByteNetSerializerBase<T>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(Value.XYZIsNotZero, 3U);
	if (Value.XYZIsNotZero & XDiffersMask)
	{
		Writer->WriteBits(Value.X, 8U);
	}
	if (Value.XYZIsNotZero & YDiffersMask)
	{
		Writer->WriteBits(Value.Y, 8U);
	}
	if (Value.XYZIsNotZero & ZDiffersMask)
	{
		Writer->WriteBits(Value.Z, 8U);
	}
}

/** 反序列化（AsByte）：与 Serialize 对称。 */
template<typename T>
void FRotatorAsByteNetSerializerBase<T>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	QuantizedType Value;
	const uint8 XYZIsNotZero = static_cast<uint8>(Reader->ReadBits(3U));
	Value.XYZIsNotZero = XYZIsNotZero;
	Value.X = static_cast<uint8>(XYZIsNotZero & XDiffersMask ? Reader->ReadBits(8U) : 0U);
	Value.Y = static_cast<uint8>(XYZIsNotZero & YDiffersMask ? Reader->ReadBits(8U) : 0U);
	Value.Z = static_cast<uint8>(XYZIsNotZero & ZDiffersMask ? Reader->ReadBits(8U) : 0U);

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = Value;
}

/**
 * 增量序列化（AsByte）：注意——本函数其实**不发差值**，而是发"完整 8-bit 值 + ChangedMask"。
 *
 * 原因：8-bit 已经很小，差分编码的 header 开销（变长位数）会比直接重发更贵。所以这里只用
 * ChangedMask 跳过未变分量即可。
 *
 * 比特布局：[XYZDiffers : 3] [X : 8]? [Y : 8]? [Z : 8]?
 */
template<typename T>
void FRotatorAsByteNetSerializerBase<T>::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	const QuantizedType& PrevValue = *reinterpret_cast<QuantizedType*>(Args.Prev);

	uint32 XYZDiffers = 0;
	XYZDiffers |= (Value.X != PrevValue.X) ? uint32(XDiffersMask) : uint32(0);
	XYZDiffers |= (Value.Y != PrevValue.Y) ? uint32(YDiffersMask) : uint32(0);
	XYZDiffers |= (Value.Z != PrevValue.Z) ? uint32(ZDiffersMask) : uint32(0);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(XYZDiffers, 3U);
	if (XYZDiffers & XDiffersMask)
	{
		Writer->WriteBits(Value.X, 8U);    // 直接发完整值
	}
	if (XYZDiffers & YDiffersMask)
	{
		Writer->WriteBits(Value.Y, 8U);
	}
	if (XYZDiffers & ZDiffersMask)
	{
		Writer->WriteBits(Value.Z, 8U);
	}
}

/** 增量反序列化（AsByte）：未变分量保留 PrevValue；变化分量直接覆盖（非差分）。 */
template<typename T>
void FRotatorAsByteNetSerializerBase<T>::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	const QuantizedType& PrevValue = *reinterpret_cast<QuantizedType*>(Args.Prev);
	QuantizedType TempValue = PrevValue;

	const uint32 XYZDiffers = Reader->ReadBits(3U);
	if (XYZDiffers & XDiffersMask)
	{
		TempValue.X = static_cast<uint8>(Reader->ReadBits(8U));
	}
	if (XYZDiffers & YDiffersMask)
	{
		TempValue.Y = static_cast<uint8>(Reader->ReadBits(8U));
	}
	if (XYZDiffers & ZDiffersMask)
	{
		TempValue.Z = static_cast<uint8>(Reader->ReadBits(8U));
	}

	// Reconstruct flags
	// 译：根据当前数值重建 flags（差分路径不传输 flags）。
	uint8 XYZIsNotZero = 0U;
	XYZIsNotZero |= static_cast<uint8>(TempValue.X != 0 ? XDiffersMask : 0U);
	XYZIsNotZero |= static_cast<uint8>(TempValue.Y != 0 ? YDiffersMask : 0U);
	XYZIsNotZero |= static_cast<uint8>(TempValue.Z != 0 ? ZDiffersMask : 0U);
	TempValue.XYZIsNotZero = XYZIsNotZero;

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = TempValue;
}

/** 量化（AsByte）：CompressAxisToByte。1 单位 ≈ 1.40625°。 */
template<typename T>
void FRotatorAsByteNetSerializerBase<T>::Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	QuantizedType TempValue = {};

	TempValue.X = SourceType::CompressAxisToByte(Source.Pitch);
	TempValue.Y = SourceType::CompressAxisToByte(Source.Yaw);
	TempValue.Z = SourceType::CompressAxisToByte(Source.Roll);

	TempValue.XYZIsNotZero |= (TempValue.X != 0) ? XDiffersMask : uint8(0);
	TempValue.XYZIsNotZero |= (TempValue.Y != 0) ? YDiffersMask : uint8(0);
	TempValue.XYZIsNotZero |= (TempValue.Z != 0) ? ZDiffersMask : uint8(0);

	Target = TempValue;
}

/** 反量化（AsByte）：DecompressAxisFromByte → degree ∈ (-180, 180]。 */
template<typename T>
void FRotatorAsByteNetSerializerBase<T>::Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	SourceType TempValue;
	TempValue.Pitch = SourceType::DecompressAxisFromByte(Source.X);
	TempValue.Yaw = SourceType::DecompressAxisFromByte(Source.Y);
	TempValue.Roll = SourceType::DecompressAxisFromByte(Source.Z);

	Target = TempValue;
}

/** IsEqual（AsByte）：与 AsShort 同款逻辑。 */
template<typename T>
bool FRotatorAsByteNetSerializerBase<T>::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& SourceValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& SourceValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		const bool bXIsEqual = (SourceValue0.X == SourceValue1.X);
		const bool bYIsEqual = (SourceValue0.Y == SourceValue1.Y);
		const bool bZIsEqual = (SourceValue0.Z == SourceValue1.Z);
		const bool bFlagsAreEqual = (SourceValue0.XYZIsNotZero == SourceValue1.XYZIsNotZero);
		return bXIsEqual & bYIsEqual & bZIsEqual & bFlagsAreEqual;
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		const bool bPitchIsEqual = (SourceValue0.Pitch == SourceValue1.Pitch);
		const bool bYawIsEqual = (SourceValue0.Yaw == SourceValue1.Yaw);
		const bool bRollIsEqual = (SourceValue0.Roll == SourceValue1.Roll);
		return bPitchIsEqual & bYawIsEqual & bRollIsEqual;
	}
}

/** Validate（AsByte）：拒绝 NaN。 */
template<typename T>
bool FRotatorAsByteNetSerializerBase<T>::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<SourceType*>(Args.Source);
	return !Value.ContainsNaN();
}

// 五个具体 Serializer 类型
//-----------------------------------------------------------------------------
// 全部基于上面两个模板基类，仅绑定 SourceType 与 ConfigType。
// 注意 FRotatorNetSerializer 默认走 AsShort 精度（16-bit/分量），与 UE 默认 NetSerialize 相符。

struct FRotatorNetSerializer : public FRotatorAsShortNetSerializerBase<FRotator>
{
	static constexpr uint32 Version = 0;

	typedef FRotatorNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;
};
UE_NET_IMPLEMENT_SERIALIZER(FRotatorNetSerializer);

struct FRotatorAsByteNetSerializer : public FRotatorAsByteNetSerializerBase<FRotator>
{
	static constexpr uint32 Version = 0;

	typedef FRotatorAsByteNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;
};
UE_NET_IMPLEMENT_SERIALIZER(FRotatorAsByteNetSerializer);

struct FRotatorAsShortNetSerializer : public FRotatorAsShortNetSerializerBase<FRotator>
{
	static constexpr uint32 Version = 0;

	typedef FRotatorAsShortNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;
};
UE_NET_IMPLEMENT_SERIALIZER(FRotatorAsShortNetSerializer);

struct FRotator3fNetSerializer : public FRotatorAsShortNetSerializerBase<FRotator3f>
{
	static constexpr uint32 Version = 0;

	typedef FRotator3fNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;
};
UE_NET_IMPLEMENT_SERIALIZER(FRotator3fNetSerializer);

struct FRotator3dNetSerializer : public FRotatorAsShortNetSerializerBase<FRotator3d>
{
	static constexpr uint32 Version = 0;

	typedef FRotator3dNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;
};
UE_NET_IMPLEMENT_SERIALIZER(FRotator3dNetSerializer);

}
