// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// RotatorNetSerializers.h
// ---------------------------------------------------------------------------------------------
// 旋转器（FRotator / FRotator3f / FRotator3d）的 NetSerializer 公开声明，含三种精度档：
//   - FRotatorNetSerializer        ：等价 AsShort（16-bit/分量）
//   - FRotatorAsByteNetSerializer  ：8-bit/分量（粗）
//   - FRotatorAsShortNetSerializer ：16-bit/分量（中等）
//   - FRotator3fNetSerializer      ：FRotator3f → AsShort 16-bit/分量（强 float）
//   - FRotator3dNetSerializer      ：FRotator3d → AsShort 16-bit/分量（强 double）
//
// 注意：UE 的 FRotator::CompressAxisToByte / Short 是按 **角度（degrees）** 缩放：
//   - AsByte ：1 单位 = 360°/256  ≈ 1.40625°/单位     精度约 ±0.7°
//   - AsShort：1 单位 = 360°/65536 ≈ 0.0055°/单位      精度约 ±0.003°
//   FRotator 内部用度数（不是弧度），±180° 回绕由 CompressAxis* 函数自然处理（取模 256/65536）。
//   * 与文档的"2π/256 / 2π/65536"表述存在差异（实际单位是度），见末尾报告。
//
// 共享比特布局（所有 Rotator Serializer 都用此格式）：
//   [XYZIsNotZero : 3]                    ← 三 bit 表示 Pitch/Yaw/Roll 是否为非零
//   if X != 0: [X : N]   N=8 (Byte) / N=16 (Short)
//   if Y != 0: [Y : N]
//   if Z != 0: [Z : N]
// 全零旋转（Identity）→ 仅 3 bit。每分量独立"非零标志"是带宽的关键优化。
//
// 注意 Validate ：所有变体都会拒绝 NaN（FRotator::ContainsNaN）。±180°/超过 360° 不视为非法
// （CompressAxis* 会取模规整）。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "RotatorNetSerializers.generated.h"

/** FRotatorNetSerializer 配置：空（与 AsShort 等价的默认精度）。 */
USTRUCT()
struct FRotatorNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FRotatorAsByteNetSerializer 配置：每分量 8-bit，粗精度。 */
USTRUCT()
struct FRotatorAsByteNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FRotatorAsShortNetSerializer 配置：每分量 16-bit，中等精度。 */
USTRUCT()
struct FRotatorAsShortNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FRotator3fNetSerializer 配置：FRotator3f（强 float）。 */
USTRUCT()
struct FRotator3fNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FRotator3dNetSerializer 配置：FRotator3d（强 double）。 */
USTRUCT()
struct FRotator3dNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// FRotator + 默认精度（实际等价 AsShort）。
UE_NET_DECLARE_SERIALIZER(FRotatorNetSerializer, IRISCORE_API);
// FRotator + 8-bit/分量。
UE_NET_DECLARE_SERIALIZER(FRotatorAsByteNetSerializer, IRISCORE_API);
// FRotator + 16-bit/分量。
UE_NET_DECLARE_SERIALIZER(FRotatorAsShortNetSerializer, IRISCORE_API);
// FRotator3f + 16-bit/分量。
UE_NET_DECLARE_SERIALIZER(FRotator3fNetSerializer, IRISCORE_API);
// FRotator3d + 16-bit/分量。
UE_NET_DECLARE_SERIALIZER(FRotator3dNetSerializer, IRISCORE_API);

}
