// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// GuidNetSerializer.h
// ---------------------------------------------------------------------------------------------
// FGuid（128-bit 全球唯一标识，由 4 个 uint32 A/B/C/D 组成）的 NetSerializer 公开声明。
//
// 角色：Iris L2 "具体类型 Serializer"，用于反射类型 `FGuid`。
//
// 设计要点（详见 .cpp）：
//   - **不做量化**——SourceType==FGuid 直接走原位序列化。
//   - 优化：1-bit "IsValid 标志位 + 4×32 bit"，即：
//       * 全零 GUID（FGuid::IsValid() == false）→ 1 bit；
//       * 否则 → 1 + 128 = 129 bit。
//   - 不实现 Quantize/Dequantize/IsEqual：由 NetSerializerBuilder 默认提供（FGuid 的
//     `==` 是 4 个 uint32 比较，bit-exact 等价）。
//   - 不做 Validate（任何 4 元组都视为合法 GUID 位模式）。
//   - 不做 Delta（GUID 一旦初始化基本不变；变化则全 128 bit 重传成本可接受）。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "NetSerializerConfig.h"
#include "UObject/ObjectMacros.h"
#include "GuidNetSerializer.generated.h"

/**
 * FGuidNetSerializer 的配置类型——空配置（无可调参数）。
 */
USTRUCT()
struct FGuidNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// FGuid 的标准 NetSerializer。
UE_NET_DECLARE_SERIALIZER(FGuidNetSerializer, IRISCORE_API);

}
