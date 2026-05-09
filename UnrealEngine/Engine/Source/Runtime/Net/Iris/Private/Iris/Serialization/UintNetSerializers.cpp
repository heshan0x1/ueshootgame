// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/UintNetSerializers.h"
#include "Iris/Serialization/IntNetSerializerBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UintNetSerializers)

namespace UE::Net
{

/** FUint8NetSerializer：8-bit 无符号整型 Serializer；默认 BitCount=8，即原生位宽，无压缩。 */
struct FUint8NetSerializer : public Private::FIntNetSerializerBase<uint8, FUint8NetSerializerConfig>
{
	inline static const FUint8NetSerializerConfig DefaultConfig = FUint8NetSerializerConfig(uint8(8));
};
UE_NET_IMPLEMENT_SERIALIZER(FUint8NetSerializer);

/** FUint16NetSerializer：16-bit 无符号整型。默认 BitCount=16。 */
struct FUint16NetSerializer : public Private::FIntNetSerializerBase<uint16, FUint16NetSerializerConfig>
{
	inline static const FUint16NetSerializerConfig DefaultConfig = FUint16NetSerializerConfig(uint8(16));
};
UE_NET_IMPLEMENT_SERIALIZER(FUint16NetSerializer);

/** FUint32NetSerializer：32-bit 无符号整型。默认 BitCount=32。 */
struct FUint32NetSerializer : public Private::FIntNetSerializerBase<uint32, FUint32NetSerializerConfig>
{
	inline static const FUint32NetSerializerConfig DefaultConfig = FUint32NetSerializerConfig(uint8(32));
};
UE_NET_IMPLEMENT_SERIALIZER(FUint32NetSerializer);

/** FUint64NetSerializer：64-bit 无符号整型。默认 BitCount=64。Serialize 会自动拆成两次 32-bit 写入。 */
struct FUint64NetSerializer : public Private::FIntNetSerializerBase<uint64, FUint64NetSerializerConfig>
{
	inline static const FUint64NetSerializerConfig DefaultConfig = FUint64NetSerializerConfig(uint8(64));
};
UE_NET_IMPLEMENT_SERIALIZER(FUint64NetSerializer);

}
