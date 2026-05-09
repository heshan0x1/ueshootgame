// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DateTimeNetSerializer.h
// ---------------------------------------------------------------------------------------------
// FDateTime（UE 内置日期时间，本质 1 个 int64 ticks）的 NetSerializer 公开声明。
//
// 角色：Iris L2 "具体类型 Serializer"，对应 FProperty 反射类型 `FDateTime`。
//
// 设计要点（详见 .cpp）：
//   - SourceType = FDateTime，QuantizedType = int64（FDateTime::GetTicks 的返回类型）。
//   - Quantize / Dequantize 仅是 `Ticks <-> FDateTime` 的等价转换，无精度损失。
//   - Serialize / Deserialize 调用 `WriteInt64 / ReadInt64`（NetBitStreamUtil），即按
//     有符号 64-bit 原位写出。FDateTime::Ticks 的语义是 100ns 单位、起点 0001-01-01。
//   - **不做范围 / 合法性 Validate**：任何 int64 都视为合法 ticks。
//   - **未实现 SerializeDelta**：走 Builder 默认（即每帧整 64-bit）。日期时间通常变化幅度不
//     固定，且占用本就只有 64 bit，不值得做差分。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "NetSerializerConfig.h"
#include "UObject/ObjectMacros.h"
#include "DateTimeNetSerializer.generated.h"

/**
 * FDateTimeNetSerializer 的配置类型——空配置（无可调参数）。
 */
USTRUCT()
struct FDateTimeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// FDateTime 的标准 NetSerializer，对外暴露给 PropertyNetSerializerInfoRegistry 自动注册。
UE_NET_DECLARE_SERIALIZER(FDateTimeNetSerializer, IRISCORE_API);

}
