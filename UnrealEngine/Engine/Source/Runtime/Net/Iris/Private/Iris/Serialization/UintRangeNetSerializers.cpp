// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/UintRangeNetSerializers.h"
#include "Iris/Serialization/IntRangeNetSerializerUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UintRangeNetSerializers)

namespace UE::Net
{

// 这 4 个无符号区间 Serializer 完全依赖 FIntRangeNetSerializerBase 模板，
// 本地只提供类型壳——所有 Serialize/Deserialize/Quantize/Dequantize/IsEqual/Validate 都从模板继承。

/** FUint8RangeNetSerializer：带值域约束的 uint8 Serializer。 */
struct FUint8RangeNetSerializer : public Private::FIntRangeNetSerializerBase<uint8, FUint8RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FUint8RangeNetSerializer);

/** FUint16RangeNetSerializer：带值域约束的 uint16 Serializer。 */
struct FUint16RangeNetSerializer : public Private::FIntRangeNetSerializerBase<uint16, FUint16RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FUint16RangeNetSerializer);

/** FUint32RangeNetSerializer：带值域约束的 uint32 Serializer。 */
struct FUint32RangeNetSerializer : public Private::FIntRangeNetSerializerBase<uint32, FUint32RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FUint32RangeNetSerializer);

/** FUint64RangeNetSerializer：带值域约束的 uint64 Serializer。 */
struct FUint64RangeNetSerializer : public Private::FIntRangeNetSerializerBase<uint64, FUint64RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FUint64RangeNetSerializer);

}
