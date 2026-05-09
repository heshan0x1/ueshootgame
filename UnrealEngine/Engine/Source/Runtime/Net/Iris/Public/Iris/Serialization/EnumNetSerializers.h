// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "NetSerializerConfig.h"
#include "UObject/ObjectMacros.h"
#include "EnumNetSerializers.generated.h"

/**
 * 枚举 NetSerializer 系列 Config。
 *
 * 每个 Config 在"整数区间"基础上新增了一个 UEnum 指针：
 *  - LowerBound/UpperBound：UEnum 中实际枚举值的最小/最大值；用于 Quantize rebase + Validate 越界检查。
 *  - BitCount：由 GetBitsNeededForRange(LowerBound, UpperBound) 动态确定，比按原生位宽节省大量带宽。
 *  - Enum：源 UEnum 对象，Validate 时调用 `Enum->IsValidEnumValue(Value)` 检查是否是已定义的枚举值
 *    （防止非法整数被当成枚举传输）。注意 Enum 不参与 UPROPERTY 反射（避免循环引用与序列化问题）。
 *
 * 区分 Int8/16/32/64 与 Uint8/16/32/64 的原因：
 *  - UENUM 底层类型由 UEnum 的 CppType 决定（`enum class : uint8` / `: int32` 等）。
 *  - 不同底层类型字节宽度与签名性不同，Config 必须保留完整 LowerBound/UpperBound 类型信息，
 *    否则 rebase 计算会发生溢出或符号错误。
 *  - 默认 PropertyNetSerializer 注册表根据 UEnum 的 CppType 挑选对应 EnumXXNetSerializer。
 */
USTRUCT()
struct FEnumInt8NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int8 LowerBound = 0;   // 枚举值集合中的最小值。
	UPROPERTY()
	int8 UpperBound = 0;   // 枚举值集合中的最大值（不含自动生成的 _MAX）。
	UPROPERTY()
	uint8 BitCount = 0;    // rebased 值的写入位宽。
	const UEnum* Enum = nullptr;  // 关联的 UEnum 对象，用于 Validate 中的严格成员校验。
};

/** int16 枚举 Config，语义同上。 */
USTRUCT()
struct FEnumInt16NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int16 LowerBound = 0;
	UPROPERTY()
	int16 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

/** int32 枚举 Config，语义同上。 */
USTRUCT()
struct FEnumInt32NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 LowerBound = 0;
	UPROPERTY()
	int32 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

/** int64 枚举 Config，语义同上。 */
USTRUCT()
struct FEnumInt64NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int64 LowerBound = 0;
	UPROPERTY()
	int64 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

/** uint8 枚举 Config（最常见——Blueprint enum class 默认为 uint8）。 */
USTRUCT()
struct FEnumUint8NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint8 LowerBound = 0;
	UPROPERTY()
	uint8 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

/** uint16 枚举 Config。 */
USTRUCT()
struct FEnumUint16NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint16 LowerBound = 0;
	UPROPERTY()
	uint16 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

/** uint32 枚举 Config。 */
USTRUCT()
struct FEnumUint32NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint32 LowerBound = 0;
	UPROPERTY()
	uint32 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

/** uint64 枚举 Config。 */
USTRUCT()
struct FEnumUint64NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint64 LowerBound = 0;
	UPROPERTY()
	uint64 UpperBound = 0;
	UPROPERTY()
	uint8 BitCount = 0;
	const UEnum* Enum = nullptr;
};

namespace UE::Net
{

// 8 个枚举 Serializer 的 Info 声明：覆盖 UENUM 可能的全部底层整型。
// 选哪个版本由 DefaultPropertyNetSerializerInfos 根据 UEnum 的 CppType 自动决定；
// 用户一般不需要手动引用这些类型。
UE_NET_DECLARE_SERIALIZER(FEnumInt8NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FEnumInt16NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FEnumInt32NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FEnumInt64NetSerializer, IRISCORE_API);

UE_NET_DECLARE_SERIALIZER(FEnumUint8NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FEnumUint16NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FEnumUint32NetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FEnumUint64NetSerializer, IRISCORE_API);

}
