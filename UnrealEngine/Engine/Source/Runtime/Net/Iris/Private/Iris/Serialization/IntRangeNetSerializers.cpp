// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/IntRangeNetSerializers.h"
#include "Iris/Serialization/IntRangeNetSerializerUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IntRangeNetSerializers)

namespace UE::Net
{

// 注意：Range 系列不在此提供 DefaultConfig——上界/下界与 BitCount 必须由使用方显式配置，
// 不同业务用到的值域千差万别，使用零默认值会立即触发 Validate 失败，便于排查漏配。

/** FInt8RangeNetSerializer：带值域约束的 int8 Serializer。全部行为由 FIntRangeNetSerializerBase 提供。 */
struct FInt8RangeNetSerializer : public Private::FIntRangeNetSerializerBase<int8, FInt8RangeNetSerializerConfig>
{
};
// 宏展开定义 FInt8RangeNetSerializerInfo::Serializer 等符号。
UE_NET_IMPLEMENT_SERIALIZER(FInt8RangeNetSerializer);

/** FInt16RangeNetSerializer：带值域约束的 int16 Serializer。 */
struct FInt16RangeNetSerializer : public Private::FIntRangeNetSerializerBase<int16, FInt16RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FInt16RangeNetSerializer);

/** FInt32RangeNetSerializer：带值域约束的 int32 Serializer。 */
struct FInt32RangeNetSerializer : public Private::FIntRangeNetSerializerBase<int32, FInt32RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FInt32RangeNetSerializer);

/** FInt64RangeNetSerializer：带值域约束的 int64 Serializer。 */
struct FInt64RangeNetSerializer : public Private::FIntRangeNetSerializerBase<int64, FInt64RangeNetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FInt64RangeNetSerializer);

}
