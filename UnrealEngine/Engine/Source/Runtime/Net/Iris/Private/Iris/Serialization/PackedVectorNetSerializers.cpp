// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// PackedVectorNetSerializers.cpp
// ---------------------------------------------------------------------------------------------
// 向量量化 Serializer 的实现。包含三种核心算法：
//   1. FPackedVectorNetSerializerBase（共享基类）——
//      "整数缩放 + 自适应 bit-count + 大值降级全精度" 的 NetQuantize/10/100 实现。
//   2. FVectorNetQuantizeNetSerializerBase<Cfg, ScaleBitCount>——薄壳模板，把 ScaleBitCount
//      作为编译期常量绑定到基类的 Serialize/Deserialize/Delta/Quantize/Dequantize 等。
//   3. FVectorNetQuantizeNormalNetSerializer ——独立实现：每分量 16-bit signed unit-float
//      （SerializeSignedUnitFloat / QuantizeSignedUnitFloat 来自 BitPacking）。
//
// 文件分布速读：
//   - L13-L83  ：基类 FPackedVectorNetSerializerBase（Quantized struct / Constants / 接口签名）
//   - L85-L107 ：模板薄壳 FVectorNetQuantizeNetSerializerBase
//   - L111-L128：三个具体 Serializer（NetQuantize / 10 / 100）实现：仅是绑定 ScaleBitCount
//   - L130-L165：FVectorNetQuantizeNormalNetSerializer（独立量化）
//   - L170-L757：基类的 Serialize/Deserialize/Delta/Quantize/Dequantize/IsEqual/Validate 实现
//   - L760-L800：模板薄壳的成员函数转发到基类
//   - L807-L905：FVectorNetQuantizeNormal 的 Serialize/Deserialize/Quantize/Dequantize/IsEqual/Validate
// =============================================================================================

#include "Iris/Serialization/PackedVectorNetSerializers.h"
#include "Iris/Serialization/BitPacking.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Core/BitTwiddling.h"
#include "Math/Vector.h"
#include "Logging/LogMacros.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PackedVectorNetSerializers)

namespace UE::Net::Private
{

// Supports both 64- and 32-bit floating point types.
// 译：本基类同时支持 32 位 / 64 位浮点类型（FVector 的标量是 float 还是 double 在编译期决定）。
/**
 * FPackedVectorNetSerializerBase ：NetQuantize / 10 / 100 的共享基础。
 *
 * 量化态 FQuantizedType 的字段语义：
 *   - X / Y / Z ：64-bit 槽位。当 ComponentBitCount > 0 时存放有符号整数（按 ComponentBitCount
 *                 进行符号扩展）；当 ComponentBitCount == 0 时存放浮点的位表示（uint32 装在低
 *                 32 位，或 uint64 完整装载）。
 *   - ComponentBitCountAndExtraInfo ：低 6 位 = ComponentBitCount（0..63），第 6 位 = ExtraInfo：
 *       * 当 ComponentBitCount > 0：ExtraInfo 表示是否使用了 Scale（IsScaledValueMask）；
 *       * 当 ComponentBitCount == 0：ExtraInfo 表示原标量是 64-bit（Is64BitScalarType），
 *         否则为 32-bit。
 *     线传时只发送低 7 位即可表达完整 ComponentBitCount + ExtraInfo。
 *   - Unused ：填充位（QuantizedType 大小对齐）。
 */
struct FPackedVectorNetSerializerBase
{
	struct FQuantizedType
	{
		uint64 X;
		uint64 Y;
		uint64 Z;
		/*
		 * Extra info stores whether the value is scaled when ComponentBitCount > 0,
		 * or if the source component type is 64 or 32 bits when ComponentBitCount == 0.
		 * Info: [ExtraInfo][ComponentBitCount]
		 * Bit:          [6][           543210]
		 *
		 * 译：当 ComponentBitCount>0，ExtraInfo 表示该值是否使用了缩放；
		 *     当 ComponentBitCount==0，ExtraInfo 表示原始分量是 64-bit 还是 32-bit。
		 */
		uint32 ComponentBitCountAndExtraInfo;
		uint32 Unused;
	};

	typedef FVector SourceType;       // SourceType 始终是 FVector（按 UE LWC 决定 float/double）
	typedef FQuantizedType QuantizedType;

	/** Validate：拒绝包含 NaN 的源向量。（运行时不阻断 ±Inf——但 ContainsNaN 不识别 Inf）。 */
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

protected:
	enum Constants : unsigned
	{
		ComponentBitCountMask = 63U,    // 6 bits
		ExtraInfoMask = 64U,            // 第 6 位（ComponentBitCount 之外的最高位）
		// Depending on ComponentBitCount the ExtraInfo has different meanings.
		// For ComponentBitCount > 0 the ExtraInfo indicates whether the value is scaled or not.
		IsScaledValueMask = ExtraInfoMask,
		// For ComponentBitCount == 0 the ExtraInfo indicates whether the value is 64 bits per component or not.
		Is64BitScalarType = IsScaledValueMask,
	};

	enum DeltaConstants : uint32
	{
		// Per-component "this component differs from prev" 位掩码。
		XDiffersMask = 1U,
		YDiffersMask = 2U,
		ZDiffersMask = 4U,

		XYZDiffersMask = 7U,
		
	};

	// 共享实现入口。模板薄壳把 ScaleBitCount 作为编译期常量传入。
	static void Serialize(uint32 ScaleBitCount, FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(uint32 ScaleBitCount, FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(uint32 ScaleBitCount, FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(uint32 ScaleBitCount, FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(uint32 ScaleBitCount, FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(uint32 ScaleBitCount, FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(uint32 ScaleBitCount, FNetSerializationContext&, const FNetIsEqualArgs& Args);

	// Round negative values towards -infinity and positive values towards +infinity.
	// 译：负值向 -∞ 舍入，正值向 +∞ 舍入（即"远离零"舍入），而非银行家舍入。
	// 公式：int(Value + sign(Value)*0.5) 等价于 round-half-away-from-zero。
	static int32 RoundFloatToInt(float Value)
	{
		return int32(Value + FPlatformMath::Sign(Value)*0.5f);
	}

	static int64 RoundFloatToInt(double Value)
	{
		return int64(Value + FPlatformMath::Sign(Value)*0.5);
	}

};

/**
 * 模板薄壳：把 ScaleBitCount 编译期常量绑定到基类 Serializer 接口。
 *   - InScaleBitCount = 0 → NetQuantize       （Scale = 1）
 *   - InScaleBitCount = 3 → NetQuantize10     （Scale = 8）
 *   - InScaleBitCount = 7 → NetQuantize100    （Scale = 128）
 */
template<typename InConfigType, uint32 InScaleBitCount>
struct FVectorNetQuantizeNetSerializerBase : public FPackedVectorNetSerializerBase
{
	static constexpr uint32 Version = 0U;

	typedef InConfigType ConfigType;

	static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
};

template<typename InConfigType, uint32 ScaleBitCount>
const typename FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::ConfigType FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::DefaultConfig;

}

namespace UE::Net
{

// FVector_NetQuantize ：Scale = 2^0 = 1（即整数化）。误差 ≤ 0.5 unit。
struct FVectorNetQuantizeNetSerializer : public Private::FVectorNetQuantizeNetSerializerBase<FVectorNetQuantizeNetSerializerConfig, 0U>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FVectorNetQuantizeNetSerializer);

// 
// FVector_NetQuantize10 ：Scale = 2^3 = 8（注意：类型名是 "10"，但实际乘 8）。误差 ≤ 1/16 unit。
struct FVectorNetQuantize10NetSerializer : public Private::FVectorNetQuantizeNetSerializerBase<FVectorNetQuantize10NetSerializerConfig, 3U>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FVectorNetQuantize10NetSerializer);

// FVector_NetQuantize100 ：Scale = 2^7 = 128（类型名 "100"，实际乘 128）。误差 ≤ 1/256 unit。
struct FVectorNetQuantize100NetSerializer : public Private::FVectorNetQuantizeNetSerializerBase<FVectorNetQuantize100NetSerializerConfig, 7U>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FVectorNetQuantize100NetSerializer);

/**
 * FVectorNetQuantizeNormalNetSerializer ：每分量 16-bit 有符号 unit-float。
 *
 * - 假设源向量分量 ∈ [-1, 1]（典型用例：单位向量、归一化方向）。
 * - 量化：QuantizeSignedUnitFloat(V, 16) → 16-bit 有符号整数。
 * - Validate：要求 X/Y/Z ∈ [-1, 1]，超出 → 拒绝。
 * - 误差：~ 1/65536（对单位向量足够）。
 * - 注意 SourceType=FVector，但内部会把分量 cast 到 float 再量化（双精度多余位被截断）。
 */
struct FVectorNetQuantizeNormalNetSerializer
{
	static constexpr uint32 Version = 0U;

	struct FQuantizedType
	{
		uint32 X;
		uint32 Y;
		uint32 Z;
		uint32 Unused;   // 填充位，让 IsEqual 能整段比较（包含 Unused）以便编译器矢量化。
	};

	typedef FVector SourceType;
	typedef FQuantizedType QuantizedType;
	typedef FVectorNetQuantizeNormalNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

private:
	// 16 bits per component（根据头文件文档，固定 16 比特）。
	static constexpr uint32 PrecisionBitCount = 16U;
};

const FVectorNetQuantizeNormalNetSerializer::ConfigType FVectorNetQuantizeNormalNetSerializer::DefaultConfig;

UE_NET_IMPLEMENT_SERIALIZER(FVectorNetQuantizeNormalNetSerializer);

}

namespace UE::Net::Private
{

/**
 * 序列化：写入量化态。
 *
 * 比特布局：
 *   [ComponentBitCountAndExtraInfo : 7]
 *   if ComponentBitCount > 0:
 *       [X : ComponentBitCount] [Y : ComponentBitCount] [Z : ComponentBitCount]
 *       （ComponentBitCount > 32 时，每分量分两次 WriteBits：低 32 + 高 (BitCount-32) 位）
 *   if ComponentBitCount == 0:（全精度路径，由 Deserialize 单独解析，见下方）
 *       Serialize 同样把 X/Y/Z 视为整数 64-bit 写出。
 *
 * 注意 ScaleBitCount 在此函数中**不直接使用**，因为 IsScaledValueMask 已记录在
 * ComponentBitCountAndExtraInfo 的 ExtraInfo 位里；ScaleBitCount 只在 Quantize/Dequantize
 * 阶段需要进行乘 / 除还原。
 */
void FPackedVectorNetSerializerBase::Serialize(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	// 写 6-bit ComponentBitCount + 1-bit ExtraInfo。
	Writer->WriteBits(Value.ComponentBitCountAndExtraInfo, 7U);

	uint32 ComponentBitCount = Value.ComponentBitCountAndExtraInfo & ComponentBitCountMask;
	if (ComponentBitCount == 0U)
	{
		// 全精度路径：根据 ExtraInfo 决定每分量写 32 还是 64 位。
		ComponentBitCount = Value.ComponentBitCountAndExtraInfo & Is64BitScalarType ? 64U : 32U;
	}

	if (ComponentBitCount <= 32U)
	{
		// ≤32 位：每分量一次 WriteBits 即可。
		Writer->WriteBits(static_cast<uint32>(Value.X), ComponentBitCount);
		Writer->WriteBits(static_cast<uint32>(Value.Y), ComponentBitCount);
		Writer->WriteBits(static_cast<uint32>(Value.Z), ComponentBitCount);
	}
	else
	{
		// >32 位：每分量分两次 WriteBits（低 32 + 剩余高位）。
		const uint32 BitCountForHighBits = ComponentBitCount - 32U;
		Writer->WriteBits(static_cast<uint32>(Value.X), 32U);
		Writer->WriteBits(static_cast<uint32>(Value.X >> 32U), BitCountForHighBits);
		Writer->WriteBits(static_cast<uint32>(Value.Y), 32U);
		Writer->WriteBits(static_cast<uint32>(Value.Y >> 32U), BitCountForHighBits);
		Writer->WriteBits(static_cast<uint32>(Value.Z), 32U);
		Writer->WriteBits(static_cast<uint32>(Value.Z >> 32U), BitCountForHighBits);
	}
}

/**
 * 反序列化：与 Serialize 对称。
 * 量化整数路径会做"符号扩展"——线上发的是无符号 ComponentBitCount 比特，需还原为 int64。
 * 全精度路径会**校验 NaN**：若网传数据 ContainsNaN，置错断连。
 */
void FPackedVectorNetSerializerBase::Deserialize(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType TempValue = {};

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	TempValue.ComponentBitCountAndExtraInfo = Reader->ReadBits(7U);
	const uint32 ComponentBitCount = TempValue.ComponentBitCountAndExtraInfo & ComponentBitCountMask;
	if (ComponentBitCount > 0)
	{
		// 量化整数路径。
		if (ComponentBitCount <= 32U)
		{
			TempValue.X = Reader->ReadBits(ComponentBitCount);
			TempValue.Y = Reader->ReadBits(ComponentBitCount);
			TempValue.Z = Reader->ReadBits(ComponentBitCount);
		}
		else
		{
			// 拼接 64 位（低 32 + 高 (BitCount-32)）。
			const uint32 BitCountForHighBits = ComponentBitCount - 32U;
			TempValue.X  = Reader->ReadBits(32U);
			TempValue.X |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
			TempValue.Y  = Reader->ReadBits(32U);
			TempValue.Y |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
			TempValue.Z  = Reader->ReadBits(32U);
			TempValue.Z |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
		}

		// Sign-extend
		// 译：把 ComponentBitCount 比特的"无符号"值解释为有符号；
		// XOR Mask 然后减 Mask，相当于做 N-bit signed 扩展为 64-bit signed。
		const uint64 Mask = (1ULL << (ComponentBitCount - 1U));
		TempValue.X = (TempValue.X ^ Mask) - Mask;
		TempValue.Y = (TempValue.Y ^ Mask) - Mask;
		TempValue.Z = (TempValue.Z ^ Mask) - Mask;
	}
	else
	{
		// 全精度路径：原始浮点位级。
		if (TempValue.ComponentBitCountAndExtraInfo & Is64BitScalarType)
		{
			// double 三分量：每分量 64 位（拼接低 32 + 高 32）。
			TempValue.X = Reader->ReadBits(32U);
			TempValue.X |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;
			TempValue.Y = Reader->ReadBits(32U);
			TempValue.Y |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;
			TempValue.Z = Reader->ReadBits(32U);
			TempValue.Z |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;

			// 把内存当作 FVector3d 解释，做 NaN 校验。
			FVector3d Vector;
			memcpy(&Vector, &TempValue.X, 3U*sizeof(double)); //-V512
			if (Vector.ContainsNaN())
			{
				// While we detect and zero out nan data at quantize time if we get bad data here it
				// indicates serialization issues and client should disconnect.
				// 译：Quantize 阶段已对 NaN 清零，理论上线上不该再出现 NaN。一旦出现，说明
				// 线传被篡改或两端协议不一致，置错让上层断连。
				Context.SetError(GNetError_InvalidValue);
				return;
			}
		}
		else
		{
			// float 三分量：每分量 32 位。
			uint32 Components[3];
			Components[0] = Reader->ReadBits(32U);
			Components[1] = Reader->ReadBits(32U);
			Components[2] = Reader->ReadBits(32U);

			FVector3f Vector;
			memcpy(&Vector, &Components, sizeof(Components));
			if (Vector.ContainsNaN())
			{
				// While we detect and zero out nan data at quantize time if we get bad data here it
				// indicates serialization issues and client should disconnect.
				Context.SetError(GNetError_InvalidValue);
				return;
			}

			TempValue.X = Components[0];
			TempValue.Y = Components[1];
			TempValue.Z = Components[2];
		}
	}

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = TempValue;
}

/**
 * 增量序列化：尝试用"per-component delta + 自适应 bit count"压缩；不划算则降级为标准 Serialize。
 *
 * 比特布局（差分路径）：
 *   [IsUsingDeltaCompression : 1]   ← 始终为 1 才进入差分路径
 *   [ChangedComponentsMask : 3]     ← X/Y/Z 各 1 bit，1 表示该分量与上次不同
 *   if ChangedComponentsMask != 0:
 *       [DeltaComponentBitCount : 6]  ← 三分量差分中最大值所需比特数
 *       for each changed component:
 *           [delta : DeltaComponentBitCount]   ← 有符号差分
 *
 * 决策准则（仅在 ExtraInfo 一致时尝试差分；ExtraInfo 不一致表示量化语义切换，差分意义不大）：
 *   1. 双方都为量化整数路径（ComponentBitCount > 0）：
 *      若估算的 "差分总位数 < 标准发送总位数"，启用差分。
 *   2. 双方都为同精度全精度路径（ComponentBitCount == 0 同种）：
 *      仍可用 ChangedMask 优化"有些分量未改变"——总能至少省 3 bit。
 *   3. 否则 / 无法差分 → 降级。
 *
 * 比特预算估算：
 *   EstimatedStandardBitCount  = 6 (BitCount) + 1 (ExtraInfo) + 3*ComponentBitCount
 *   EstimatedCompressedBitCount = (变化分量数 ? 6 : 0) + 3 (mask) + 变化分量数 * DeltaComponentBitCount
 */
void FPackedVectorNetSerializerBase::SerializeDelta(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	const QuantizedType& PrevValue = *reinterpret_cast<QuantizedType*>(Args.Prev);
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// Try to figure out whether it's worthwhile to delta compress or not.
	// 译：先判断是否值得做差分压缩。
	const uint32 ExtraInfo = Value.ComponentBitCountAndExtraInfo & ExtraInfoMask;
	const uint32 PrevExtraInfo = PrevValue.ComponentBitCountAndExtraInfo & ExtraInfoMask;

	if (ExtraInfo == PrevExtraInfo)
	{
		const uint32 ComponentBitCount = Value.ComponentBitCountAndExtraInfo & ComponentBitCountMask;
		const uint32 PrevComponentBitCount = PrevValue.ComponentBitCountAndExtraInfo & ComponentBitCountMask;
		// If both values are packed we can see if the integer delta is small.
		// 译：如果两者都是量化整数路径，看看整数差分能否落在小范围内。
		if (ComponentBitCount > 0U && PrevComponentBitCount > 0U)
		{
			const int64 DeltaValues[3] = 
			{
				static_cast<int64>(Value.X - PrevValue.X),
				static_cast<int64>(Value.Y - PrevValue.Y),
				static_cast<int64>(Value.Z - PrevValue.Z)
			};

			// 哪些分量发生了变化（per-bit）。
			const uint32 XDiffers = (DeltaValues[0] != 0 ? 1U : 0U);
			const uint32 YDiffers = (DeltaValues[1] != 0 ? 1U : 0U);
			const uint32 ZDiffers = (DeltaValues[2] != 0 ? 1U : 0U);
			const uint32 ChangedComponentCount = XDiffers + YDiffers + ZDiffers;

			// 三分量差分中"最大绝对值所需位数"。
			const uint32 DeltaComponentBitCount = FPlatformMath::Max(GetBitsNeeded(DeltaValues[0]), FPlatformMath::Max(GetBitsNeeded(DeltaValues[1]), GetBitsNeeded(DeltaValues[2])));

			constexpr uint32 BitCountForComponent = 6U;
			constexpr uint32 BitCountForExtraInfo = 1U;
			constexpr uint32 BitCountForChangedMask = 3U;
			const uint32 EstimatedStandardBitCount = BitCountForComponent +  BitCountForExtraInfo + 3U*ComponentBitCount;
			const uint32 EstimatedCompressedBitCount = (ChangedComponentCount != 0 ? BitCountForComponent : 0U) + BitCountForChangedMask + ChangedComponentCount*DeltaComponentBitCount;
			if (EstimatedCompressedBitCount < EstimatedStandardBitCount)
			{
				constexpr uint32 IsUsingDeltaCompression = 1U;
				const uint32 ChangedComponentsMask = ((XDiffers*XDiffersMask) | (YDiffers*YDiffersMask) | (ZDiffers*ZDiffersMask));

				Writer->WriteBits(IsUsingDeltaCompression, 1U);
				Writer->WriteBits(ChangedComponentsMask, 3U);
				if (ChangedComponentsMask != 0U)
				{
					Writer->WriteBits(DeltaComponentBitCount, 6U);
					if (DeltaComponentBitCount <= 32U)
					{
						// 单次写入即可：把 int64 截成 int32，再做 N-bit 写入（接收端会做符号扩展）。
						for (SIZE_T ValueIndex = 0, ValueEndIndex = 3, ComponentMask = XDiffersMask; ValueIndex != ValueEndIndex; ++ValueIndex, ComponentMask += ComponentMask)
						{
							if (ChangedComponentsMask & ComponentMask)
							{
								Writer->WriteBits(static_cast<uint32>(static_cast<int32>(DeltaValues[ValueIndex])), DeltaComponentBitCount);
							}
						}
					}
					else
					{
						// 差分需要 >32 位：低 32 + 高 (DeltaComponentBitCount-32)。
						const uint32 BitCountForHighBits = DeltaComponentBitCount - 32U;
						for (SIZE_T ValueIndex = 0, ValueEndIndex = 3, ComponentMask = XDiffersMask; ValueIndex != ValueEndIndex; ++ValueIndex, ComponentMask += ComponentMask)
						{
							if (ChangedComponentsMask & ComponentMask)
							{
								const uint64 UnsignedValue = static_cast<uint64>(DeltaValues[ValueIndex]);
								Writer->WriteBits(static_cast<uint32>(UnsignedValue), 32U);
								Writer->WriteBits(static_cast<uint32>(UnsignedValue >> 32U), BitCountForHighBits);
							}
						}
					}
				}

				// We have serialized the delta compressed data.
				return;
			}
		}
		// Both values are full precision of the same type.
		// 译：两者都是同种精度的全精度浮点路径。
		else if (ComponentBitCount == PrevComponentBitCount)
		{
			// Always say we're using delta compression. This will save at least 3 bits.
			// Check whether any of the components are equal for massive savings.
			// 译：总是声称使用差分（至少省 3 bit 的 BitCount header）；进一步检查每个分量是否相同。
			const uint32 XDiffers = (Value.X != PrevValue.X ? XDiffersMask : 0U);
			const uint32 YDiffers = (Value.Y != PrevValue.Y ? YDiffersMask : 0U);
			const uint32 ZDiffers = (Value.Z != PrevValue.Z ? ZDiffersMask : 0U);
			const uint32 ChangedComponentsMask = (XDiffers | YDiffers | ZDiffers);

			constexpr uint32 IsUsingDeltaCompression = 1U;
			Writer->WriteBits(IsUsingDeltaCompression, 1U);
			Writer->WriteBits(ChangedComponentsMask, 3U);
			if (ChangedComponentsMask != 0U)
			{
				// 64-bit values
				if (ExtraInfo)
				{
					if (XDiffers)
					{
						Writer->WriteBits(static_cast<uint32>(Value.X), 32U);
						Writer->WriteBits(static_cast<uint32>(Value.X >> 32U), 32U);
					}
					if (YDiffers)
					{
						Writer->WriteBits(static_cast<uint32>(Value.Y), 32U);
						Writer->WriteBits(static_cast<uint32>(Value.Y >> 32U), 32U);
					}
					if (ZDiffers)
					{
						Writer->WriteBits(static_cast<uint32>(Value.Z), 32U);
						Writer->WriteBits(static_cast<uint32>(Value.Z >> 32U), 32U);
					}
				}
				// 32-bit values
				else
				{
					if (XDiffers)
					{
						Writer->WriteBits(static_cast<uint32>(Value.X), 32U);
					}
					if (YDiffers)
					{
						Writer->WriteBits(static_cast<uint32>(Value.Y), 32U);
					}
					if (ZDiffers)
					{
						Writer->WriteBits(static_cast<uint32>(Value.Z), 32U);
					}
				}
			}

			// We have serialized the delta compressed data.
			return;
		}
	}

	// If we end up here we couldn't delta compress.
	// 译：执行到这里说明无法差分（ExtraInfo 不同 / 精度切换），写一个 0 标志位然后走标准 Serialize。
	{
		Writer->WriteBits(0U, 1U);
		Serialize(ScaleBitCount, Context, Args);
	}
}

/**
 * 增量反序列化：先读 1-bit 路由——0 = 标准路径，1 = 差分路径。
 * 差分路径根据 PrevValue 的 ComponentBitCountAndExtraInfo 区分量化整数还是全精度。
 */
void FPackedVectorNetSerializerBase::DeserializeDelta(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	if (Reader->ReadBits(1U) == 0)
	{
		// 标准路径：把 Args 转为 NetDeserializeArgs 后转发。
		return Deserialize(ScaleBitCount, Context, Args);
	}

	const QuantizedType& PrevValue = *reinterpret_cast<QuantizedType*>(Args.Prev);
	QuantizedType TempValue = PrevValue;
	// Handle packed values
	if (PrevValue.ComponentBitCountAndExtraInfo & ComponentBitCountMask)
	{
		// 量化整数差分路径。
		const uint32 ChangedComponentsMask = Reader->ReadBits(3U);
		if (ChangedComponentsMask != 0U)
		{
			const uint32 DeltaComponentBitCount = Reader->ReadBits(6U);
			const uint64 Mask = (1ULL << (DeltaComponentBitCount - 1U));
			if (DeltaComponentBitCount <= 32U)
			{
				if (ChangedComponentsMask & XDiffersMask)
				{
					uint64 DX = Reader->ReadBits(DeltaComponentBitCount);
					DX = (DX ^ Mask) - Mask;     // N-bit signed → 64-bit signed
					TempValue.X += DX;
				}

				if (ChangedComponentsMask & YDiffersMask)
				{
					uint64 DY = Reader->ReadBits(DeltaComponentBitCount);
					DY = (DY ^ Mask) - Mask;
					TempValue.Y += DY;
				}

				if (ChangedComponentsMask & ZDiffersMask)
				{
					uint64 DZ = Reader->ReadBits(DeltaComponentBitCount);
					DZ = (DZ ^ Mask) - Mask;
					TempValue.Z += DZ;
				}
			}
			else
			{
				// >32 位拼接 + 符号扩展。
				const uint32 BitCountForHighBits = DeltaComponentBitCount - 32U;
				if (ChangedComponentsMask & XDiffersMask)
				{
					uint64 DX;
					DX = Reader->ReadBits(32U);
					DX |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
					DX = (DX ^ Mask) - Mask;
					TempValue.X += DX;
				}

				if (ChangedComponentsMask & YDiffersMask)
				{
					uint64 DY;
					DY = Reader->ReadBits(32U);
					DY |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
					DY = (DY ^ Mask) - Mask;
					TempValue.Y += DY;
				}

				if (ChangedComponentsMask & ZDiffersMask)
				{
					uint64 DZ;
					DZ = Reader->ReadBits(32U);
					DZ |= static_cast<uint64>(Reader->ReadBits(BitCountForHighBits)) << 32U;
					DZ = (DZ ^ Mask) - Mask;
					TempValue.Z += DZ;
				}
			}

			// We must re-calculate the bit count needed for the new values of the components
			// in case this data will be serialized later.
			// 译：差分还原后值变了，下一次再做 Serialize/Delta 需要正确的 ComponentBitCount，
			// 因此这里重新测量并更新 header（保留 ExtraInfo）。
			const uint32 NewComponentBitCount = FPlatformMath::Max(GetBitsNeeded(static_cast<int64>(TempValue.X)), FPlatformMath::Max(GetBitsNeeded(static_cast<int64>(TempValue.Y)), GetBitsNeeded(static_cast<int64>(TempValue.Z))));
			TempValue.ComponentBitCountAndExtraInfo = (TempValue.ComponentBitCountAndExtraInfo & ExtraInfoMask) | NewComponentBitCount;
		}
	}
	// Handle full precision values
	else
	{
		// 全精度差分路径。
		const uint32 ChangedComponentsMask = Reader->ReadBits(3U);
		if (PrevValue.ComponentBitCountAndExtraInfo & Is64BitScalarType)
		{
			// double 三分量。
			if (ChangedComponentsMask & XDiffersMask)
			{
				TempValue.X = Reader->ReadBits(32U);
				TempValue.X |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;
			}
			if (ChangedComponentsMask & YDiffersMask)
			{
				TempValue.Y = Reader->ReadBits(32U);
				TempValue.Y |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;
			}
			if (ChangedComponentsMask & ZDiffersMask)
			{
				TempValue.Z = Reader->ReadBits(32U);
				TempValue.Z |= static_cast<uint64>(Reader->ReadBits(32U)) << 32U;
			}

			FVector3d Vector;
			memcpy(&Vector, &TempValue.X, 3U*sizeof(double)); //-V512
			if (Vector.ContainsNaN())
			{
				// While we detect and zero out nan data at quantize time if we get bad data here it
				// indicates serialization issues and client should disconnect.
				Context.SetError(GNetError_InvalidValue);
				return;
			}
		}
		else
		{
			// float 三分量：未变化的分量从 PrevValue 继承（注意 TempValue 是从 PrevValue 拷贝的，
			// 所以未变化分支保留 PrevValue 的低 32 位即可）。
			uint32 Components[3];
			if (ChangedComponentsMask & XDiffersMask)
			{
				Components[0] = Reader->ReadBits(32U);
			}
			else
			{
				Components[0] = static_cast<uint32>(TempValue.X);
			}

			if (ChangedComponentsMask & YDiffersMask)
			{
				Components[1] = Reader->ReadBits(32U);
			}
			else
			{
				Components[1] = static_cast<uint32>(TempValue.Y);
			}

			if (ChangedComponentsMask & ZDiffersMask)
			{
				Components[2] = Reader->ReadBits(32U);
			}
			else
			{
				Components[2] = static_cast<uint32>(TempValue.Z);
			}

			FVector3f Vector;
			memcpy(&Vector, &Components, sizeof(Components));
			if (Vector.ContainsNaN())
			{
				// While we detect and zero out nan data at quantize time if we get bad data here it
				// indicates serialization issues and client should disconnect.
				Context.SetError(GNetError_InvalidValue);
				return;
			}

			TempValue.X = Components[0];
			TempValue.Y = Components[1];
			TempValue.Z = Components[2];
		}
	}

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = TempValue;
}

/**
 * 量化（核心）：浮点 → 量化态。
 *
 * 算法步骤：
 *   1. 若源向量 ContainsNaN：写日志并返回零向量量化态（防御）。
 *   2. 计算 ScaledValue = Source * 2^ScaleBitCount（NetQuantize Scale=1, 10→8, 100→128）。
 *   3. 若 ScaledValue.AbsMax < 2^MaxExponentAfterScaling（float 2^30, double 2^62）：
 *      - 检查 Source.AbsMin < 2^MaxExponentForScaling（float 2^23, double 2^52）。该条件为
 *        "缩放对精度有意义"——超过 mantissa 位数后，浮点本身就只能表达整数邻接值，
 *        缩放无法增加精度。
 *      - 满足 → 用 ScaledValue 取整；否则用 Source 直接取整（即等价于 Scale=1）。
 *      - 计算三分量整数所需位数（取最大值）作为 ComponentBitCount。
 *      - ExtraInfo 记录是否使用了 Scale。
 *   4. 否则（值过大）：直接把 X/Y/Z 浮点位原样塞进 QuantizedType（ComponentBitCount=0），
 *      ExtraInfo 标志 32-bit 还是 64-bit。
 *
 * 关键常量：
 *   - MaxExponentForScaling：float=23（mantissa 位数），double=52；
 *   - MaxExponentAfterScaling：float=30（再多就接近 int32 极限），double=62；
 *
 * 这就是题目所说的 "WriteConditionallyQuantizedVector 的条件选择"：
 *   - ScaledValue.AbsMax >= 2^MaxExponentAfterScaling → 走全精度分支；
 *   - 否则量化整数 + 选择是否真的应用 Scale。
 */
void FPackedVectorNetSerializerBase::Quantize(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	using ScalarType = decltype(SourceType::X);
	constexpr SIZE_T ScalarTypeSize = sizeof(ScalarType);
	using IntType = typename TSignedIntType<ScalarTypeSize>::Type;
	using UintType = typename TUnsignedIntType<ScalarTypeSize>::Type;

	// Beyond 2^MaxExponentForScaling scaling cannot improve the precision as the next floating point value is at least 1.0 more. 
	// 译：超过 2^MaxExponentForScaling，浮点已经无法表示小数（mantissa 用尽），缩放无法提升精度。
	constexpr uint32 MaxExponentForScaling = ScalarTypeSize == 4 ? 23U : 52U;
	constexpr ScalarType MaxValueToScale = ScalarType(IntType(1) << MaxExponentForScaling);

	// Rounding of large values can introduce additional precision errors and the extra bandwidth cost to serialize with full precision is small.
	// 译：大值的舍入会引入额外精度误差，而切换到全精度的额外带宽开销不大，干脆直发完整浮点位。
	constexpr uint32 MaxExponentAfterScaling = ScalarTypeSize == 4 ? 30U : 62U;
	constexpr ScalarType MaxScaledValue = ScalarType(IntType(1) << MaxExponentAfterScaling);

	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	if (Source.ContainsNaN())
	{
		// 防御性：源向量含 NaN 直接清零（同时记日志/ensure），避免 NaN 顺着 ChangeMask 持续脏。
		logOrEnsureNanError(TEXT("%s"), TEXT("PackedVectorNetSerializeBase::Quantize Value isn't finite. Clearing for safety."));		

		constexpr uint32 ComponentBitCountForZeroValue = 1U;

		QuantizedType TempValue = {};
		// 设为"已缩放、ComponentBitCount=1"——三个 0 各占 1 bit；接收端会得到零向量。
		TempValue.ComponentBitCountAndExtraInfo = IsScaledValueMask | ComponentBitCountForZeroValue;
		Target = TempValue;

		return;
	}

	const ScalarType Scale = ScalarType(IntType(1) << ScaleBitCount);
	
	SourceType ScaledValue;
	// Avoid NaN checks called by FVector operators.
	// 译：直接逐分量乘，避开 FVector*Scalar 内部可能的 NaN 检查/断言（提速 + 避免误报）。
	ScaledValue.X = Source.X*Scale;
	ScaledValue.Y = Source.Y*Scale;
	ScaledValue.Z = Source.Z*Scale;

	if (ScaledValue.GetAbsMax() < MaxScaledValue)
	{
		// 值在量化范围内：决定是否真用 Scale。
		const bool bUseScaledValue = Source.GetAbsMin() < MaxValueToScale;

		IntType X;
		IntType Y;
		IntType Z;
		if (bUseScaledValue)
		{
			X = RoundFloatToInt(ScaledValue.X);
			Y = RoundFloatToInt(ScaledValue.Y);
			Z = RoundFloatToInt(ScaledValue.Z);
		}
		else
		{
			X = RoundFloatToInt(Source.X);
			Y = RoundFloatToInt(Source.Y);
			Z = RoundFloatToInt(Source.Z);
		}

		// 选择最经济的 per-component bit count（带符号位）。
		const uint32 ComponentBitCount = FPlatformMath::Max(GetBitsNeeded(X), FPlatformMath::Max(GetBitsNeeded(Y), GetBitsNeeded(Z)));

		QuantizedType TempValue = {};
		TempValue.X = static_cast<uint64>(static_cast<int64>(X));
		TempValue.Y = static_cast<uint64>(static_cast<int64>(Y));
		TempValue.Z = static_cast<uint64>(static_cast<int64>(Z));
		TempValue.ComponentBitCountAndExtraInfo = (bUseScaledValue ? IsScaledValueMask : 0U) | ComponentBitCount;

		Target = TempValue;
	}
	else
	{
		// Value needs full precision
		// 译：值过大，量化无法承载，发完整浮点位。ComponentBitCount=0 标记此路径。
		QuantizedType TempValue = {};
		TempValue.X = *reinterpret_cast<const UintType*>(&Source.X);
		TempValue.Y = *reinterpret_cast<const UintType*>(&Source.Y);
		TempValue.Z = *reinterpret_cast<const UintType*>(&Source.Z);
		TempValue.ComponentBitCountAndExtraInfo = (ScalarTypeSize == 8U ? Is64BitScalarType : 0U);

		Target = TempValue;
	}
}

/**
 * 反量化：量化态 → 浮点向量。
 *
 * - 量化整数路径：(int64)X 转 ScalarType；若 IsScaledValueMask 设置，乘以 1/Scale。
 * - 全精度路径：把 uint 位原样 memcpy 回 float/double；如果 SourceType 与存储精度不同
 *   （如 SourceType=FVector3f 但存的是 64-bit），会先重建临时 FVector3d/3f 再转换。
 */
void FPackedVectorNetSerializerBase::Dequantize(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	using ScalarType = decltype(SourceType::X);
	using IntType = typename TSignedIntType<sizeof(ScalarType)>::Type;

	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	const uint32 ComponentBitCount = Source.ComponentBitCountAndExtraInfo & ComponentBitCountMask;
	if (ComponentBitCount > 0)
	{
		// 量化整数路径。
		SourceType TempValue;
		TempValue.X = ScalarType(int64(Source.X));
		TempValue.Y = ScalarType(int64(Source.Y));
		TempValue.Z = ScalarType(int64(Source.Z));

		if (Source.ComponentBitCountAndExtraInfo & IsScaledValueMask)
		{
			// 反缩放：除以 2^ScaleBitCount = 乘以 1/Scale。
			const ScalarType InvScale = ScalarType(1)/ScalarType(IntType(1) << ScaleBitCount);
			Target = TempValue*InvScale;
		}
		else
		{
			Target = TempValue;
		}
	}
	else
	{
		// 全精度路径。
		if (Source.ComponentBitCountAndExtraInfo & Is64BitScalarType)
		{
			// 存储是 double。
			if constexpr (std::is_same<SourceType, FVector3d>::value)
			{
				// SourceType 也是 double —— 直接 memcpy。
				memcpy(&Target.X, &Source.X, 3U*sizeof(double)); //-V512
			}
			else
			{
				// SourceType 是 FVector3f —— 临时构造 FVector3d 再 cast。
				FVector3d Vector;
				memcpy(&Vector.X, &Source.X, 3U*sizeof(double)); //-V512
				Target = SourceType(Vector);
			}
		}
		else
		{
			// 存储是 float。
			uint32 Components[3];
			Components[0] = static_cast<uint32>(Source.X);
			Components[1] = static_cast<uint32>(Source.Y);
			Components[2] = static_cast<uint32>(Source.Z);

			if constexpr (std::is_same<SourceType, FVector3f>::value)
			{
				memcpy(&Target.X, &Components[0], 3U*sizeof(float)); //-V512
			}
			else
			{
				FVector3f Vector;
				memcpy(&Vector.X, &Components[0], 3U*sizeof(float)); //-V512
				Target = SourceType(Vector);
			}
		}
	}
}

/**
 * IsEqual ：
 *   - bStateIsQuantized=true：直接比较量化态字段（包括 ComponentBitCountAndExtraInfo）。
 *     注意：即便两源向量经反量化后数值相等，但若 ExtraInfo / BitCount 不同，仍视为不等
 *     —— 这是为了让 ChangeMask 能严格匹配序列化后的形状。
 *   - 否则：先把两个源向量都 Quantize 到临时 QuantizedType，再比较。这避免了浮点 ±0/NaN 坑。
 */
bool FPackedVectorNetSerializerBase::IsEqual(uint32 ScaleBitCount, FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	QuantizedType QuantizedValue0;
	QuantizedType QuantizedValue1;

	if (Args.bStateIsQuantized)
	{
		QuantizedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		QuantizedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
	}
	else
	{
		FNetQuantizeArgs QuantizeArgs = {};
		QuantizeArgs.NetSerializerConfig = Args.NetSerializerConfig;

		QuantizeArgs.Source = NetSerializerValuePointer(Args.Source0);
		QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedValue0);
		Quantize(ScaleBitCount, Context, QuantizeArgs);

		QuantizeArgs.Source = NetSerializerValuePointer(Args.Source1);
		QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedValue1);
		Quantize(ScaleBitCount, Context, QuantizeArgs);
	}

	return ((QuantizedValue0.X == QuantizedValue1.X) & (QuantizedValue0.Y == QuantizedValue1.Y) & (QuantizedValue0.Z == QuantizedValue1.Z) & (QuantizedValue0.ComponentBitCountAndExtraInfo == QuantizedValue1.ComponentBitCountAndExtraInfo));
}

/**
 * Validate ：
 *   - 拒绝 NaN（用于"严格输入"，发现 NaN → 业务异常）。
 *   - 不拒绝 ±Inf（FVector::ContainsNaN 不识别 Inf）。
 *   - 任意范围、任意精度均合法（因为大值会自动降级为全精度）。
 */
bool FPackedVectorNetSerializerBase::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	// While we can properly send any value we want to be able to inform the user of bad values such as NaN and infinite.
	// 译：虽然技术上能发任意值，但我们仍希望对 NaN/Inf 报警。当前实现仅拒绝 NaN（Inf 未检测）。
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	return !Source.ContainsNaN();
}

// FVectorNetQuantizeNetSerializerBase implementation
//-----------------------------------------------------------------------------
// 模板薄壳：把 ScaleBitCount 从模板参数转发到基类。

template<typename InConfigType, uint32 ScaleBitCount>
void FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	return FPackedVectorNetSerializerBase::Serialize(ScaleBitCount, Context, Args);
}

template<typename InConfigType, uint32 ScaleBitCount>
void FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	return FPackedVectorNetSerializerBase::Deserialize(ScaleBitCount, Context, Args);
}

template<typename InConfigType, uint32 ScaleBitCount>
void FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	return FPackedVectorNetSerializerBase::SerializeDelta(ScaleBitCount, Context, Args);
}

template<typename InConfigType, uint32 ScaleBitCount>
void FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	return FPackedVectorNetSerializerBase::DeserializeDelta(ScaleBitCount, Context, Args);
}

template<typename InConfigType, uint32 ScaleBitCount>
void FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	return FPackedVectorNetSerializerBase::Quantize(ScaleBitCount, Context, Args);
}

template<typename InConfigType, uint32 ScaleBitCount>
void FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	return FPackedVectorNetSerializerBase::Dequantize(ScaleBitCount, Context, Args);
}

template<typename InConfigType, uint32 ScaleBitCount>
bool FVectorNetQuantizeNetSerializerBase<InConfigType, ScaleBitCount>::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	return FPackedVectorNetSerializerBase::IsEqual(ScaleBitCount, Context, Args);
}

}

namespace UE::Net
{

// FVectorNetQuantizeNormalNetSerializer implementation
//-----------------------------------------------------------------------------
// 单位向量量化：每分量 16-bit 有符号 unit-float，假设输入 ∈ [-1, 1]。

/**
 * 序列化：调用 BitPacking::SerializeSignedUnitFloat 三次（每次 16 bit）。
 * 总共 48 bit。无 ExtraInfo / per-component 优化（单位向量很少全为 0）。
 */
void FVectorNetQuantizeNormalNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	SerializeSignedUnitFloat(*Writer, Value.X, PrecisionBitCount);
	SerializeSignedUnitFloat(*Writer, Value.Y, PrecisionBitCount);
	SerializeSignedUnitFloat(*Writer, Value.Z, PrecisionBitCount);
}

/** 反序列化：与 Serialize 对称，每次 DeserializeSignedUnitFloat 16 bit。 */
void FVectorNetQuantizeNormalNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	QuantizedType TempValue = {};
	TempValue.X = DeserializeSignedUnitFloat(*Reader, PrecisionBitCount);
	TempValue.Y = DeserializeSignedUnitFloat(*Reader, PrecisionBitCount);
	TempValue.Z = DeserializeSignedUnitFloat(*Reader, PrecisionBitCount);

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = TempValue;
}

/**
 * 量化：FVector → 16-bit signed unit-float ×3。
 *
 * 注意：这里强制 cast 到 float（即使 SourceType=FVector 是 double）。原因：
 *   1. unit-float 假设 ∈ [-1,1]，单精度足够；
 *   2. 与 BitPacking::QuantizeSignedUnitFloat 的 float 签名一致；
 *   3. 节省 CPU。
 *
 * 越界值（|V|>1）：QuantizeSignedUnitFloat 内部一般会饱和到 ±32767（线传"假"单位向量）。
 */
void FVectorNetQuantizeNormalNetSerializer::Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	QuantizedType TempValue = {};
	TempValue.X = QuantizeSignedUnitFloat(static_cast<float>(Source.X), PrecisionBitCount);
	TempValue.Y = QuantizeSignedUnitFloat(static_cast<float>(Source.Y), PrecisionBitCount);
	TempValue.Z = QuantizeSignedUnitFloat(static_cast<float>(Source.Z), PrecisionBitCount);

	Target = TempValue;
}

/** 反量化：16-bit unit-int → float ∈ [-1,1] → 装回 FVector。 */
void FVectorNetQuantizeNormalNetSerializer::Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	SourceType TempValue;
	TempValue.X = DequantizeSignedUnitFloat(Source.X, PrecisionBitCount);
	TempValue.Y = DequantizeSignedUnitFloat(Source.Y, PrecisionBitCount);
	TempValue.Z = DequantizeSignedUnitFloat(Source.Z, PrecisionBitCount);

	Target = TempValue;
}

/**
 * IsEqual ：
 *   - 量化态：直接比对 X/Y/Z + Unused。包含 Unused 是为了让 SIMD/编译器能整段比较，
 *     更高效（Unused 值已被构造时清零，不影响正确性）。
 *   - 源态：分别量化两源向量再比较——避免 ±0/NaN 浮点坑，且与序列化语义一致。
 */
bool FVectorNetQuantizeNormalNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		// By comparing even the unused bits we allow the compiler to emit more optimized code.
		// 译：连同 Unused 一起比较，编译器可以做更优的 SIMD/movdqu 比较。
		return ((Value0.X == Value1.X) & (Value0.Y == Value1.Y) & (Value0.Z == Value1.Z) & (Value0.Unused == Value1.Unused));
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		// 逐分量对比量化结果（比直接 float ==更稳健，避免 ±0、NaN）。
		if (QuantizeSignedUnitFloat(static_cast<float>(SourceValue0.X), PrecisionBitCount) != QuantizeSignedUnitFloat(static_cast<float>(SourceValue1.X), PrecisionBitCount))
		{
			return false;
		}

		if (QuantizeSignedUnitFloat(static_cast<float>(SourceValue0.Y), PrecisionBitCount) != QuantizeSignedUnitFloat(static_cast<float>(SourceValue1.Y), PrecisionBitCount))
		{
			return false;
		}

		if (QuantizeSignedUnitFloat(static_cast<float>(SourceValue0.Z), PrecisionBitCount) != QuantizeSignedUnitFloat(static_cast<float>(SourceValue1.Z), PrecisionBitCount))
		{
			return false;
		}

		return true;
	}
}

/**
 * Validate ：
 *   - 拒绝 NaN。
 *   - 拒绝 |分量|>1 的输入（这是"Normal" Serializer 的语义假设）。
 *
 * 实现技巧：把 Source 的每个分量 Clamp 到 [-1,1] 后与原值比较；若与原值相等说明在范围内。
 */
bool FVectorNetQuantizeNormalNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<SourceType*>(Args.Source);
	if (Value.ContainsNaN())
	{
		return false;
	}

	SourceType TempValue;
	TempValue.X = FMath::Clamp(Value.X, -1.0f, 1.0f);
	TempValue.Y = FMath::Clamp(Value.Y, -1.0f, 1.0f);
	TempValue.Z = FMath::Clamp(Value.Z, -1.0f, 1.0f);

	return TempValue == Value;
}

}
