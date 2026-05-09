// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "UintRangeNetSerializers.generated.h"

/**
 * 无符号整数区间系列 Config。
 *
 * 逻辑与有符号版本一致：LowerBound/UpperBound 限定合法值域，BitCount 为 rebased 值的位宽。
 * 由于是无符号类型，Quantize 的 rebase 与 Dequantize 的反 rebase 不需要处理符号扩展。
 */
USTRUCT()
struct FUint8RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint8 LowerBound = 0;     // 值域下界（含）。
	UPROPERTY()
	uint8 UpperBound = 0;     // 值域上界（含）。
	UPROPERTY()
	uint8 BitCount = 0;       // rebased 值的写入位宽。
};

/** Uint16 区间 Config。 */
USTRUCT()
struct FUint16RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint16 LowerBound = 0;
	UPROPERTY()
	uint16 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
};

/** Uint32 区间 Config。 */
USTRUCT()
struct FUint32RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint32 LowerBound = 0;
	UPROPERTY()
	uint32 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
};

/** Uint64 区间 Config。 */
USTRUCT()
struct FUint64RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint64 LowerBound = 0;
	UPROPERTY()
	uint64 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
};

namespace UE::Net
{

// 声明四个无符号区间 Serializer。
UE_NET_DECLARE_SERIALIZER(FUint8RangeNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FUint16RangeNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FUint32RangeNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FUint64RangeNetSerializer, IRISCORE_API);

}
