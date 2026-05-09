// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "UintNetSerializers.generated.h"

// Unsigned integer serializers
/**
 * 无符号整型（Uint8/16/32/64）系列 Serializer 的 Config。
 *
 * 结构与 FIntNetSerializerConfig 完全一致（BitCount 决定写入比特流的位数），
 * 唯一区别是绑定的 SourceType 为无符号类型——Quantize 里只做位截断，无需符号扩展。
 */
USTRUCT()
struct FUintNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	explicit inline FUintNetSerializerConfig(uint8 InBitCount) : BitCount(InBitCount) {}
	inline FUintNetSerializerConfig() : BitCount(0) {}

	// 写入比特流的位数；范围 [1, sizeof(SourceType)*8]。
	UPROPERTY()
	uint8 BitCount;
};

// 4 个无符号位宽共用同一个 Config 结构。
typedef FUintNetSerializerConfig FUint64NetSerializerConfig;
typedef FUintNetSerializerConfig FUint32NetSerializerConfig;
typedef FUintNetSerializerConfig FUint16NetSerializerConfig;
typedef FUintNetSerializerConfig FUint8NetSerializerConfig;

namespace UE::Net
{

// 声明 FUintXXNetSerializerInfo（带 IRISCORE_API 导出），Info 内含 FNetSerializer 静态实例与访问器。
UE_NET_DECLARE_SERIALIZER(FUint64NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FUint32NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FUint16NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FUint8NetSerializer, IRISCORE_API);

}
