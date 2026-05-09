// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "PackedIntNetSerializers.generated.h"

/**
 * 打包整型（Packed*）系列 Serializer 的 Config。
 *
 * Packed 系列采用"按字节对齐的变长编码"：序列化时先写 2 bit（32-bit 版本）或 3 bit（64-bit 版本）
 * 的"字节数-1"，再写对应 8/16/24/32(/40/48/56/64) bit 的数值。因此小数值只占 1 字节 + 索引，
 * 大数值最多占到 4 / 8 字节 + 索引，相比固定位宽明显省带宽。
 *
 * Config 本身为空——Packed 序列化不需要任何配置参数（位宽由值本身决定）。
 * 典型使用场景：数组长度、计数器、NetToken ID 等"绝大多数时候很小但偶尔可能很大"的字段。
 */
USTRUCT()
struct FPackedInt64NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FPackedUint64NetSerializer 的 Config（无字段）。 */
USTRUCT()
struct FPackedUint64NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FPackedInt32NetSerializer 的 Config（无字段）。 */
USTRUCT()
struct FPackedInt32NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FPackedUint32NetSerializer 的 Config（无字段）。 */
USTRUCT()
struct FPackedUint32NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// 声明四个 Packed*NetSerializerInfo。实现见 PackedIntNetSerializers.cpp。
UE_NET_DECLARE_SERIALIZER(FPackedInt64NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FPackedUint64NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FPackedInt32NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FPackedUint32NetSerializer, IRISCORE_API);

}
