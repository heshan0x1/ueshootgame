// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "IntRangeNetSerializers.generated.h"

// Integer range serializers
/**
 * 有符号整数区间（带 [LowerBound, UpperBound] 约束）的 Config 集合。
 *
 * 每个位宽都有独立的 Config 结构（因为 LowerBound/UpperBound 类型不同），公共字段语义：
 *  - LowerBound / UpperBound：值域闭区间；Quantize 会 Clamp 并 rebase 到 [0, UpperBound-LowerBound]。
 *  - BitCount：rebased 值的写入位数，通常由 GetBitsNeededForRange(LowerBound, UpperBound) 得到。
 *    可显著低于原生位宽（例如 int32 值域 [-10, 10] 只需 5 bit 即可编码）。
 */
USTRUCT()
struct FInt8RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int8 LowerBound = 0;      // 值域下界（含）。
	UPROPERTY()
	int8 UpperBound = 0;      // 值域上界（含）。
	UPROPERTY()
	uint8 BitCount = 0;       // 序列化 rebased 值所用的位数。
};

/** Int16 区间 Config，语义同上。 */
USTRUCT()
struct FInt16RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int16 LowerBound = 0;
	UPROPERTY()
	int16 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
};

/** Int32 区间 Config，语义同上。常用于"实际值域较窄的属性"，例如 AI 状态枚举的整数映射。 */
USTRUCT()
struct FInt32RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 LowerBound = 0;
	UPROPERTY()
	int32 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
};

/** Int64 区间 Config，语义同上。一般用于 hash/id 类窄化场景，较少见。 */
USTRUCT()
struct FInt64RangeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int64 LowerBound = 0;
	UPROPERTY()
	int64 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
};

namespace UE::Net
{

// 声明四个有符号区间 Serializer（其 Info 结构提供 Serializer/Size/Alignment/DefaultConfig 访问器）。
UE_NET_DECLARE_SERIALIZER(FInt8RangeNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FInt16RangeNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FInt32RangeNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FInt64RangeNetSerializer, IRISCORE_API);

}
