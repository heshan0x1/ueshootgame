// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// VectorNetSerializers.cpp
// ---------------------------------------------------------------------------------------------
// FVectorNetSerializer / FVector3fNetSerializer / FVector3dNetSerializer 的实现。
//
// 实现思路：
//   - 抽象出模板 `FFloatTripletNetSerializer<T>`，T = float 或 double。
//   - 三个具体 Serializer 通过继承复用 Serialize/Deserialize；只各自定义 ConfigType。
//   - 单分量都用"1-bit 零标志位 + 32/64-bit 原值"格式，与 FFloatNetSerializer 同款思想。
//
// 比特布局：
//   [Xnz : 1] [X : 32]?  [Ynz : 1] [Y : 32]?  [Znz : 1] [Z : 32]?    （float 版）
//   [Xnz : 1] [X : 64]?  [Ynz : 1] [Y : 64]?  [Znz : 1] [Z : 64]?    （double 版）
//
//   - 全零向量 → 3 bit；
//   - 全非零   → 3 + 3*32 = 99 bit（float） / 3 + 3*64 = 195 bit（double）。
//
// SourceType=FFloatTriplet（uint X/Y/Z）的好处与 Float Serializer 相同：
//   - 避免 +0 == -0、NaN != NaN 等 IEEE 754 比较坑；
//   - IsEqual 直接走 Builder SFINAE 默认（用 `operator==` 做 bit-exact）。
//
// **未提供 Validate**：不拒绝 NaN/Inf。该组 Serializer 是"位精确"语义；要拒绝 NaN，请用
// PackedVector 系列（该系列 Validate 会拒绝 NaN）。
//
// **未提供 SerializeDelta**：走 Builder 默认（每次完整重发）。如需差分，应使用
// PackedVector* 系列。
// =============================================================================================

#include "Iris/Serialization/VectorNetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Math/Vector.h"
#include "Traits/IntType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VectorNetSerializers)

namespace UE::Net::Private
{

/**
 * 浮点三元组 Serializer 模板。
 *
 * @tparam T 浮点元素类型（float 或 double）。SourceType 内部用对应位宽的 uint。
 */
template<typename T>
struct FFloatTripletNetSerializer
{
	// 同 sizeof 的无符号整数类型（uint32 / uint64）。
	using UintType = typename TUnsignedIntType<sizeof(T)>::Type;

	static const uint32 Version = 0;

	/**
	 * We are interested in the bit representation of the floats, not IEEE 754 behavior. This is particularly
	 * relevant for IsEqual where for example -0.0f == +0.0f if the values were treated as floats
	 * rather than the bit representation of the floats. By using integer types we can avoid
	 * implementing some functions and use the default implementations instead.
	 *
	 * 译：用整数表示浮点比特，规避 IEEE 754 的 ±0、NaN 等异常等价问题；同时让 Builder 的
	 *     默认 IsEqual / Quantize 等就够用。
	 */
	struct FFloatTriplet
	{
		UintType X;
		UintType Y;
		UintType Z;

	public:
		// bit-exact 比较，提供给 Builder 默认 IsEqual 用。
		bool operator==(const FFloatTriplet& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
	};

	typedef FFloatTriplet SourceType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);
};

/**
 * 序列化：3 个分量逐个走"1-bit 零标志 + 原位"模式。
 * 32-bit 路径直接 WriteBits(32)；64-bit 路径分两次 WriteBits(32)（位流 API 单次最多 32 bit）。
 */
template<typename T>
void FFloatTripletNetSerializer<T>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const FFloatTriplet& Value = *reinterpret_cast<const FFloatTriplet*>(Args.Source);

	const UintType X = Value.X;
	const UintType Y = Value.Y;
	const UintType Z = Value.Z;

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if constexpr (sizeof(T) == 4)
	{
		// 32-bit (float) 路径：每分量 1 标志位 + 0/32 位原值。
		if (Writer->WriteBool(X != 0))
		{
			Writer->WriteBits(X, 32U);
		}
		if (Writer->WriteBool(Y != 0))
		{
			Writer->WriteBits(Y, 32U);
		}
		if (Writer->WriteBool(Z != 0))
		{
			Writer->WriteBits(Z, 32U);
		}
	}
	else
	{
		// 64-bit (double) 路径：每分量 1 标志位 + 0/(32+32) 位原值（低 32 后高 32）。
		if (Writer->WriteBool(X != 0))
		{
			Writer->WriteBits(static_cast<uint32>(X), 32U);
			Writer->WriteBits(static_cast<uint32>(X >> 32U), 32U);
		}
		if (Writer->WriteBool(Y != 0))
		{
			Writer->WriteBits(static_cast<uint32>(Y), 32U);
			Writer->WriteBits(static_cast<uint32>(Y >> 32U), 32U);
		}
		if (Writer->WriteBool(Z != 0))
		{
			Writer->WriteBits(static_cast<uint32>(Z), 32U);
			Writer->WriteBits(static_cast<uint32>(Z >> 32U), 32U);
		}
	}
}

/**
 * 反序列化：与 Serialize 对称。零标志位为 0 时分量保持 0。
 */
template<typename T>
void FFloatTripletNetSerializer<T>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	UintType X = 0;
	UintType Y = 0;
	UintType Z = 0;

	if constexpr (sizeof(T) == 4U)
	{
		if (Reader->ReadBool())
		{
			X = Reader->ReadBits(32U);
		}

		if (Reader->ReadBool())
		{
			Y = Reader->ReadBits(32U);
		}

		if (Reader->ReadBool())
		{
			Z = Reader->ReadBits(32U);
		}
	}
	else
	{
		// 64-bit 拼接：低 32 → 高 32。
		if (Reader->ReadBool())
		{
			X = Reader->ReadBits(32U);
			X |= static_cast<UintType>(Reader->ReadBits(32U)) << 32U;
		}

		if (Reader->ReadBool())
		{
			Y = Reader->ReadBits(32U);
			Y |= static_cast<UintType>(Reader->ReadBits(32U)) << 32U;
		}

		if (Reader->ReadBool())
		{
			Z = Reader->ReadBits(32U);
			Z |= static_cast<UintType>(Reader->ReadBits(32U)) << 32U;
		}
	}

	FFloatTriplet& Value = *reinterpret_cast<FFloatTriplet*>(Args.Target);
	Value.X = X;
	Value.Y = Y;
	Value.Z = Z;
}

}

namespace UE::Net
{

// 防御编译期断言：FVector 的标量必须是 32 或 64 位浮点。
static_assert(sizeof(decltype(FVector::X)) == 4U || sizeof(decltype(FVector::X)) == 8U, "Unknown floating point type in FVector.");

/**
 * FVector（UE 默认精度）的原位 Serializer。
 * 标量类型由 FVector::X 决定：LWC 启用时为 double，否则为 float。
 */
struct FVectorNetSerializer : public Private::FFloatTripletNetSerializer<decltype(FVector::X)>
{
	static const uint32 Version = 0;

	typedef FVectorNetSerializerConfig ConfigType;
	static const ConfigType DefaultConfig;

};
const FVectorNetSerializer::ConfigType FVectorNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FVectorNetSerializer);

/** FVector3f（强制 float）：模板特化为 float。 */
struct FVector3fNetSerializer : public Private::FFloatTripletNetSerializer<float>
{
	static const uint32 Version = 0;

	typedef FVector3fNetSerializerConfig ConfigType;
	static const ConfigType DefaultConfig;
};

const FVector3fNetSerializer::ConfigType FVector3fNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FVector3fNetSerializer);

/** FVector3d（强制 double）：模板特化为 double。 */
struct FVector3dNetSerializer : public Private::FFloatTripletNetSerializer<double>
{
	static const uint32 Version = 0;

	typedef FVector3dNetSerializerConfig ConfigType;
	static const ConfigType DefaultConfig;
};

const FVector3dNetSerializer::ConfigType FVector3dNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FVector3dNetSerializer);

}
