// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// GuidNetSerializer.cpp
// ---------------------------------------------------------------------------------------------
// FGuidNetSerializer 实现：FGuid 直接位级序列化，128 位（4×32-bit 原位）。
//
// 比特布局：
//   [IsValid : 1] [A : 32] [B : 32] [C : 32] [D : 32]
//
// IsValid 标志的判定来自 FGuid::IsValid()：当 (A | B | C | D) != 0 时返回 true。
// 全零 GUID 视为无效，仅写 1 bit。
//
// 注意事项：
//   - 反序列化时**不要使用** FGuid 的默认构造（行为可能因版本而异），因此显式 FGuid(0,0,0,0)。
//   - 4 次 32-bit 写入的字节序由 NetBitStreamWriter 内部决定（与读对称），上层无须关心。
//   - 不实现 Quantize/Dequantize/IsEqual/Validate/SerializeDelta；走 Builder 默认。
// =============================================================================================

#include "Iris/Serialization/GuidNetSerializer.h"
#include "Misc/Guid.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuidNetSerializer)

namespace UE::Net
{

/**
 * FGuidNetSerializer ：FGuid 的标准 Serializer。
 * SourceType == FGuid，无独立 QuantizedType（即 SourceType == QuantizedType，
 * 由 Builder SFINAE 默认推断）。
 */
struct FGuidNetSerializer
{
	static const uint32 Version = 0;

	typedef FGuid SourceType;
	typedef FGuidNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);
};
UE_NET_IMPLEMENT_SERIALIZER(FGuidNetSerializer);

const FGuidNetSerializer::ConfigType FGuidNetSerializer::DefaultConfig;

/**
 * 序列化：1-bit IsValid + 0/128-bit。
 *
 * - 无效（全零）→ 1 bit。绝大多数默认初始化场景都走此路径，节省 128-bit。
 * - 有效         → 1 bit + A/B/C/D 顺序各 32-bit。
 */
void FGuidNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const FGuid& Value = *reinterpret_cast<const FGuid*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if (Writer->WriteBool(Value.IsValid()))
	{
		Writer->WriteBits(Value.A, 32U);
		Writer->WriteBits(Value.B, 32U);
		Writer->WriteBits(Value.C, 32U);
		Writer->WriteBits(Value.D, 32U);
	}
}

/**
 * 反序列化：与 Serialize 对称。
 * 显式构造 FGuid(0,0,0,0) 而非依赖默认构造，避免不同版本的 FGuid 默认值差异。
 */
void FGuidNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	// Don't rely on the Guid default constructor.
	// 译：不要依赖 FGuid 默认构造行为，显式给四个分量赋 0。
	FGuid Value(0, 0, 0, 0);
	if (Reader->ReadBool())
	{
		Value.A = Reader->ReadBits(32U);
		Value.B = Reader->ReadBits(32U);
		Value.C = Reader->ReadBits(32U);
		Value.D = Reader->ReadBits(32U);
	}

	FGuid& Target = *reinterpret_cast<FGuid*>(Args.Target);
	Target = Value;
}

}
