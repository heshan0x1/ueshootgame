// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// FloatNetSerializers.h
// ---------------------------------------------------------------------------------------------
// 浮点（float / double）NetSerializer 公开声明。
//
// 角色定位（参考 Iris_Architecture.md §3.5、Serialization.md §1.5 数学）：
//   - 属于 Iris L2 "底层数据层" 的"具体类型 Serializer"。
//   - 提供两个标量 Serializer：
//       * FFloatNetSerializer   —— 32-bit 单精度（IEEE 754 binary32）。
//       * FDoubleNetSerializer  —— 64-bit 双精度（IEEE 754 binary64）。
//   - 用于 FProperty 反射烘焙时把 `float` / `double` 属性映射为 NetSerializer。
//
// 实现要点（详见对应 .cpp）：
//   - 不对浮点做任何量化压缩；按"位级精度（bit-exact）"原位序列化。
//   - 内部以 `uint32` / `uint64` 作为 SourceType，避免 IEEE 754 的 `==` 怪癖
//     （+0.0f == -0.0f、NaN != NaN）；这样 IsEqual 直接走 builder 默认实现即可。
//   - Serialize 走 "1-bit 零标志位 + 32/64-bit 原位" 优化；零值非常常见。
//   - SerializeDelta 用 `BitPacking::SerializeUintDelta` 三档表（0 / 16 / 25 / 32 位
//     for float; 0 / 42 / 54 / 64 位 for double），精度档位见 .cpp 的 DeltaBitCountTable。
//   - **不做 NaN/Inf Validate**：Iris 不主动拒绝 NaN/Inf，由调用方业务自证；位级原样转发。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "FloatNetSerializers.generated.h"

/**
 * FFloatNetSerializer 的配置类型——空配置（无可调参数）。
 * 仅作为类型 tag 满足 FNetSerializer 的 ConfigType 约束。
 */
USTRUCT()
struct FFloatNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/**
 * FDoubleNetSerializer 的配置类型——空配置（无可调参数）。
 */
USTRUCT()
struct FDoubleNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// 32-bit 单精度浮点 Serializer（FProperty: float -> 这里）。
UE_NET_DECLARE_SERIALIZER(FFloatNetSerializer, IRISCORE_API);

// 64-bit 双精度浮点 Serializer（FProperty: double -> 这里）。
UE_NET_DECLARE_SERIALIZER(FDoubleNetSerializer, IRISCORE_API);

}
