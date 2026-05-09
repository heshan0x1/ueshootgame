// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/Serialization/EnumNetSerializers.h"

namespace UE::Net::Private
{

/**
 * 内部工具：遍历 UEnum 自动推导 EnumNetSerializerConfig 的 LowerBound/UpperBound/BitCount。
 *
 * 这些函数被 DefaultPropertyNetSerializerInfos 在注册表构建阶段调用——当遇到 FEnumProperty
 * 或 FByteProperty（带 UEnum 的情况）时，自动根据 UEnum 的底层类型挑选对应的
 * FEnumXXNetSerializerConfig 并调用本函数填充。
 *
 * IRISCORE_API 修饰使得 IrisCore 模块内所有 .cpp 都能访问这些函数
 * （仅向本模块内部暴露，故放在 Private/ 下且命名带 "Internal"）。
 *
 * 返回值：永远返回 true（设计约定）。
 */
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumInt8NetSerializerConfig& OutConfig, const UEnum* Enum);
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumInt16NetSerializerConfig& OutConfig, const UEnum* Enum);
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumInt32NetSerializerConfig& OutConfig, const UEnum* Enum);
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumInt64NetSerializerConfig& OutConfig, const UEnum* Enum);

IRISCORE_API bool InitEnumNetSerializerConfig(FEnumUint8NetSerializerConfig& OutConfig, const UEnum* Enum);
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumUint16NetSerializerConfig& OutConfig, const UEnum* Enum);
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumUint32NetSerializerConfig& OutConfig, const UEnum* Enum);
IRISCORE_API bool InitEnumNetSerializerConfig(FEnumUint64NetSerializerConfig& OutConfig, const UEnum* Enum);

}
