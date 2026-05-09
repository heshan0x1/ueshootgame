// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// QuatNetSerializers.h
// ---------------------------------------------------------------------------------------------
// 单位四元数（FQuat / FQuat4f / FQuat4d）的 NetSerializer 公开声明。
//
// 角色：Iris L2 "具体类型 Serializer"，对应 FProperty 反射类型。
// 命名 "FUnitQuat" ：表示对**单位四元数（|q|=1）**做特殊量化压缩；非单位输入会被 Validate 拒绝
// 或在 Quantize 时归一化（防御）。
//
// **量化策略（与传统 "smallest three"不同！）**：
//   传统方案：找出 X/Y/Z/W 中绝对值最大者，省略最大分量、用 2-bit 索引 + 符号 + 3 个 N-bit 分量。
//   Iris 方案：**永远省略 W 分量**，发送 X / Y / Z 的"绝对值的 mantissa"，符号位另存。
//
// 比特布局：
//   [Flags : 7]
//     bit 0..2 : XIsNotZero / YIsNotZero / ZIsNotZero  ← 非零的分量才发 mantissa
//     bit 3..5 : XIsNegative / YIsNegative / ZIsNegative  ← 三轴的符号位
//     bit 6    : WIsNegative  ← W 的符号（用于客户端 sqrt 后恢复）
//   if XIsNotZero: [X : SignificandBitCount]    （float: 23, double: 52）
//   if YIsNotZero: [Y : SignificandBitCount]
//   if ZIsNotZero: [Z : SignificandBitCount]
//
// **精度**：每分量 mantissa 23 / 52 位（即 IEEE 754 mantissa 全部位），技巧是：
//   - Quantize 时把 |X| ∈ [0, 1] 映射到 [1, 2]（加 1.0），这样所有分量的指数都是固定的（即 0），
//     于是只需发送 mantissa 的 23/52 位，无须发指数；这对 [0,1] 区间是**最优精度**。
//   - Dequantize 时反向：值 - 1 还原回 [0,1]，再恢复符号。
//
// **W 的恢复**：W = sqrt(max(0, 1 - X² - Y² - Z²)) ，再按 WIsNegative 设置符号。
//   - 这是单位四元数约束：X² + Y² + Z² + W² = 1。
//   - 因此 W 的精度依赖于 X/Y/Z 的精度——当 |W|≈1（X/Y/Z 都很小）时，W 精度低。
//
// 边界 / 容错：
//   - SizeSquared() ≤ SMALL_NUMBER（接近零向量）→ 量化为 Identity（0,0,0,1）。
//   - 非单位 → IsNormalized 检查后调用 Normalize；Validate 严格要求 IsNormalized。
//   - 反序列化时，若发送端发 X==0 但 IsNotZero=1（不可能由正常 Quantize 产生，但防 corrupted），
//     用 IncreaseExponent 替换以避开下溢（详见 .cpp）。
//
// 注意：题目中"省略最大分量 + 2-bit 索引"的描述与本实现**不符**。Iris 实际是"固定省略 W"。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "QuatNetSerializers.generated.h"

/** FUnitQuatNetSerializer 配置：空。SourceType=FQuat（默认精度，UE LWC 下为 double）。 */
USTRUCT()
struct FUnitQuatNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FUnitQuat4fNetSerializer 配置：空。SourceType=FQuat4f（强 float）。 */
USTRUCT()
struct FUnitQuat4fNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FUnitQuat4dNetSerializer 配置：空。SourceType=FQuat4d（强 double）。 */
USTRUCT()
struct FUnitQuat4dNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// FQuat 单位四元数 Serializer（默认精度）。
UE_NET_DECLARE_SERIALIZER(FUnitQuatNetSerializer, IRISCORE_API);
// FQuat4f 单位四元数（强 float，每分量 23-bit mantissa）。
UE_NET_DECLARE_SERIALIZER(FUnitQuat4fNetSerializer, IRISCORE_API);
// FQuat4d 单位四元数（强 double，每分量 52-bit mantissa）。
UE_NET_DECLARE_SERIALIZER(FUnitQuat4dNetSerializer, IRISCORE_API);

}
