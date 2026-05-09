// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/IntNetSerializers.h"
#include "Iris/Serialization/IntNetSerializerBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IntNetSerializers)

namespace UE::Net
{

/**
 * FInt8NetSerializer：8-bit 有符号整型 Serializer。
 * 继承 FIntNetSerializerBase<int8, ...> 获得全部 Serialize/Deserialize/Quantize/Dequantize/IsEqual/Validate 实现；
 * 本结构只负责提供默认 Config —— BitCount=8 表示"不裁剪、按原生位宽序列化"。
 */
struct FInt8NetSerializer : public Private::FIntNetSerializerBase<int8, FInt8NetSerializerConfig>
{
	inline static const FInt8NetSerializerConfig DefaultConfig = FInt8NetSerializerConfig(uint8(8));
};
// 实现宏展开后定义 FInt8NetSerializerInfo::Serializer（FNetSerializer 静态函数表实例）、
// GetQuantizedTypeSize/Alignment 及 GetDefaultConfig 等静态成员。
UE_NET_IMPLEMENT_SERIALIZER(FInt8NetSerializer);

/** FInt16NetSerializer：16-bit 有符号整型 Serializer。DefaultConfig.BitCount=16。 */
struct FInt16NetSerializer : public Private::FIntNetSerializerBase<int16, FInt16NetSerializerConfig>
{
	inline static const FInt16NetSerializerConfig DefaultConfig = FInt16NetSerializerConfig(uint8(16));
};
UE_NET_IMPLEMENT_SERIALIZER(FInt16NetSerializer);

/** FInt32NetSerializer：32-bit 有符号整型 Serializer。DefaultConfig.BitCount=32。 */
struct FInt32NetSerializer : public Private::FIntNetSerializerBase<int32, FInt32NetSerializerConfig>
{
	inline static const FInt32NetSerializerConfig DefaultConfig = FInt32NetSerializerConfig(uint8(32));
};
UE_NET_IMPLEMENT_SERIALIZER(FInt32NetSerializer);

/** FInt64NetSerializer：64-bit 有符号整型 Serializer。DefaultConfig.BitCount=64。 */
struct FInt64NetSerializer : public Private::FIntNetSerializerBase<int64, FInt64NetSerializerConfig>
{
	inline static const FInt64NetSerializerConfig DefaultConfig = FInt64NetSerializerConfig(uint8(64));
};
UE_NET_IMPLEMENT_SERIALIZER(FInt64NetSerializer);

}
