// Copyright Epic Games, Inc. All Rights Reserved.

#include "InternalNetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net
{

/**
 * FBitfieldNetSerializer：位域（bitfield）成员的 Serializer。
 *
 * 与 FBoolNetSerializer 的关键差异：
 *  - bitfield 在结构体中共享一个存储字节（例如 `uint8 bA:1; uint8 bB:1;`），
 *    不能直接按整字节读写——必须只修改/读取 Config->BitMask 标识的那个位，保留邻位不被破坏；
 *  - 量化：从 Source 字节中抽出那 1 bit（Value & BitMask 是否为非零）并存到 Quantized（uint8 0/1）；
 *  - 反量化：先读 Target 原字节，清掉 BitMask 对应位，再把 Quantized 位写回去——这也是为什么
 *    Dequantize 必须 read-modify-write，不能像普通整数那样直接覆盖 Target。
 *
 *  - Config (FBitfieldNetSerializerConfig)：持有 BitMask（必须是 2 的幂，对应 bitfield 的那一位）。
 *  - bUseDefaultDelta=false：一位信息做差分无意义。
 *  - 通过 UE_NET_IMPLEMENT_SERIALIZER_INTERNAL 注册：属于 Iris 内部使用的 Serializer，
 *    不导出为 IRISCORE_API，而是通过默认 PropertyNetSerializerInfo 挂到 FBoolProperty 上。
 */
struct FBitfieldNetSerializer
{
	static const uint32 Version = 0;
	// 注意：此处使用 `static const bool` 而非其他 Serializer 常用的 `static constexpr bool`——
	// 在 Builder SFINAE 探测里两者等价，但风格略有不一致（见最终报告）。
	static const bool bUseDefaultDelta = false;

	typedef uint8 SourceType;     // bitfield 所在的整字节存储。
	typedef uint8 QuantizedType;  // 只保留目标位本身（0 或 1）。
	typedef FBitfieldNetSerializerConfig ConfigType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
// Internal 版本宏展开：只在 IrisCore 内部注册，不导出 IRISCORE_API。
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FBitfieldNetSerializer);

/** 把量化好的 1 bit（0 或 1）写入比特流。 */
void FBitfieldNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(Value, 1U);
}

/** 读 1 bit，直接存入 Quantized 状态。 */
void FBitfieldNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const QuantizedType Value = static_cast<QuantizedType>(Reader->ReadBits(1U));

	QuantizedType* Target = reinterpret_cast<QuantizedType*>(Args.Target);
	*Target = Value;
}

/**
 * 量化：把 SourceByte 中 BitMask 对应位提取为 0/1 存入 QuantizedType。
 *  `!!(Value & BitMask)` 保证结果严格为 0 或 1（避免 Quantize 存了任意非零）。
 */
void FBitfieldNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);
	const QuantizedType Bit = !!(Value & Config->BitMask);

	*reinterpret_cast<QuantizedType*>(Args.Target) = Bit;
}

/**
 * 反量化：read-modify-write：
 *  1) 读出 Target 当前字节（可能已经包含邻位的值）；
 *  2) 清掉 BitMask 对应位（& ~BitMask）；
 *  3) 若 Quantized Bit=1，把 BitMask 回填；否则保持清零；
 *  4) 写回 Target。
 * 这样保证本 Serializer 只修改自己那一位，不破坏同字节中共享存储的其他 bitfield。
 */
void FBitfieldNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const SourceType BitMask = Config->BitMask;
	const QuantizedType Bit = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const SourceType Value = (Bit ? BitMask : SourceType(0));

	const SourceType TargetValue = *reinterpret_cast<SourceType*>(Args.Target);
	const SourceType NewTargetValue = (TargetValue & ~BitMask) | Value;

	*reinterpret_cast<SourceType*>(Args.Target) = NewTargetValue;
}

/**
 * 相等比较。
 *  - bStateIsQuantized=true：直接比较（两端都是 0/1）。
 *  - bStateIsQuantized=false：两端是整字节，只比较 BitMask 所覆盖的那一位差异——
 *    邻位不同不视为本 Serializer 意义上的不等。
 */
bool FBitfieldNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	static_assert(std::is_same_v<SourceType, QuantizedType>, "FBitfieldNetSerializer::IsEqual needs to be re-implemented");

	const SourceType Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
	const SourceType Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);
	if (Args.bStateIsQuantized)
	{
		return Value0 == Value1;
	}
	else
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const SourceType MaskDiff = (Value0 ^ Value1) & Config->BitMask;
		return MaskDiff == SourceType(0);
	}
}

/** 校验：BitMask 必须是 2 的幂（正好一位）。多位的 mask 属于配置错误。 */
bool FBitfieldNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	return FMath::IsPowerOfTwo(Config->BitMask);
}

/**
 * 从 FBoolProperty（bitfield 版本）反推出 BitMask。
 *
 * UE 的 FBoolProperty 实际不暴露位偏移 API；这里利用 `SetPropertyValue(Buffer, true)` 往 1 字节
 * 缓冲区里写入 true，观察被修改的位——那就是该 bitfield 的 BitMask。
 *
 *  - 断言 BitMask != 0 且是 2 的幂；任一不满足说明 UE 内部 bitfield 实现发生了变化，
 *    Iris 需要重写这个适配逻辑，否则复制会错乱。
 *  - 对 UHT 历史实现不兼容的版本，ensureMsgf 会给出明确的诊断日志。
 */
bool InitBitfieldNetSerializerConfigFromProperty(FBitfieldNetSerializerConfig& OutConfig, const FBoolProperty* Bitfield)
{
	// UBoolProperty states the field size can be up to 8 bytes, but it seems to not be true. We rely on it being exactly one byte.
	// UHT 声称 FBoolProperty 可以跨 1~8 字节，但实测仅使用单字节存储；本函数基于此前提。
	alignas(16) uint8 BitfieldStorage[8];
	BitfieldStorage[0] = 0;
	Bitfield->SetPropertyValue(BitfieldStorage, true);
	OutConfig.BitMask = BitfieldStorage[0];

	if (BitfieldStorage[0] == 0)
	{
		ensureMsgf(false, TEXT("Someone has changed how bitfield properties work under the hood. Unable to properly replicate bitfield %s::%s."), ToCStr(Bitfield->GetOwnerVariant().GetName()), ToCStr(Bitfield->GetName()));
		return false;
	}


	if (!FMath::IsPowerOfTwo(BitfieldStorage[0]))
	{
		ensureMsgf(false, TEXT("Someone has changed how bitfield properties work under the hood. Unable to properly replicate bitfield %s::%s."), ToCStr(Bitfield->GetOwnerVariant().GetName()), ToCStr(Bitfield->GetName()));
		return false;
	}

	return true;
}

}
