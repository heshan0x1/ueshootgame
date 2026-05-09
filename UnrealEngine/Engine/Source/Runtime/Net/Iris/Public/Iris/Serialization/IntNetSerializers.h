// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "IntNetSerializers.generated.h"

// Integer serializers
/**
 * 有符号整型（Int8/16/32/64）系列 Serializer 的 Config。
 *
 * 所有有符号整数类型共用同一个 Config 结构体（通过下方的 typedef 别名），仅差异在于
 * 具体 Serializer 模板实例化时传入的 SourceType 位宽。
 *
 *  - BitCount：实际写入比特流的位数（1..sizeof(SourceType)*8）。小于原生位宽即为"裁剪量化"。
 *    默认值 0 代表未配置；所有具体 Serializer 的 DefaultConfig 会按原生位宽填充（见 .cpp）。
 */
USTRUCT()
struct FIntNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	// 带显式 BitCount 构造，方便内部代码以特定位宽快速生成 Config。
	explicit inline FIntNetSerializerConfig(uint8 InBitCount) : BitCount(InBitCount) {}
	// 默认构造（供 USTRUCT 反射与容器使用），BitCount=0 需后续初始化。
	inline FIntNetSerializerConfig() : BitCount(0) {}

	// 写入比特流的位数；范围 [1, sizeof(SourceType)*8]。
	UPROPERTY()
	uint8 BitCount;
};

// Int64/32/16/8 共用同一个 Config 结构（内部逻辑由模板基类区分位宽）。
typedef FIntNetSerializerConfig FInt64NetSerializerConfig;
typedef FIntNetSerializerConfig FInt32NetSerializerConfig;
typedef FIntNetSerializerConfig FInt16NetSerializerConfig;
typedef FIntNetSerializerConfig FInt8NetSerializerConfig;

namespace UE::Net
{

// 下列宏展开后声明 FIntXXNetSerializerInfo 结构（包含 Serializer 静态实例和量化状态大小/对齐/DefaultConfig 访问器），
// 供 Iris 序列化注册表与 PropertyNetSerializerInfo 引用。具体实现见 IntNetSerializers.cpp。
UE_NET_DECLARE_SERIALIZER(FInt64NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FInt32NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FInt16NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FInt8NetSerializer, IRISCORE_API);

}