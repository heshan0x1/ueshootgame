// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// PackedVectorNetSerializers.h
// ---------------------------------------------------------------------------------------------
// 向量量化序列化器集合（Iris 数学 Serializer 的"压缩核心"）。
//
// 角色：Iris L2 "具体类型 Serializer"，对应 UE 的四个量化向量包装类型：
//   - FVector_NetQuantize        → FVectorNetQuantizeNetSerializer
//   - FVector_NetQuantize10      → FVectorNetQuantize10NetSerializer
//   - FVector_NetQuantize100     → FVectorNetQuantize100NetSerializer
//   - FVector_NetQuantizeNormal  → FVectorNetQuantizeNormalNetSerializer
//
// 量化方案对比：
// ┌─────────────────────────┬──────────┬────────────────────────────┬───────────────┐
// │ Serializer              │ ScaleBit │ 量化逻辑                   │ 精度          │
// ├─────────────────────────┼──────────┼────────────────────────────┼───────────────┤
// │ NetQuantize             │ 0        │ round(V)                   │ 1 unit        │
// │ NetQuantize10           │ 3        │ round(V * 8)               │ 1/8 unit      │
// │ NetQuantize100          │ 7        │ round(V * 128)             │ 1/128 unit    │
// │ NetQuantizeNormal       │ —        │ X/Y/Z ∈ [-1,1] × 16-bit    │ 1/32768       │
// └─────────────────────────┴──────────┴────────────────────────────┴───────────────┘
//
// 注意：尽管类型名是 "Quantize10/100"，实际缩放因子是 2 的幂（8 / 128），
// 选择 2 的幂可以用整数移位完成乘除，避免浮点误差累积，并对应 ScaleBitCount 的二进制语义。
// 与 1.5 文档"×10/×100"的字面表述存在小幅差异（见末尾报告）。
//
// 自适应位预算（FVectorNetQuantize 系列共享）：
//   - 对每个分量的整数化结果，量化器测量其有符号范围所需的最小比特数（per-vector-of-three）
//     `ComponentBitCount = max(GetBitsNeeded(X), GetBitsNeeded(Y), GetBitsNeeded(Z))`，
//     在 [1, 63] 范围内动态决定，由 7-bit header 在序列化时同步发送。
//   - 当原值过大（超过 2^MaxExponentForScaling，float 是 2^23，double 是 2^52）放弃缩放；
//     当缩放后超过 2^MaxExponentAfterScaling（float 2^30 / double 2^62）则**直接发完整浮点位**
//     （ComponentBitCount=0 + Is64BitScalarType 标志位）。这就是文档所谓 "No clamping- if the
//     values are very large they will be sent as full floats"（题目里的 "WriteConditionallyQuantizedVector
//     的条件选择"对应于此动态分支）。
//
// 误差保证（最大绝对误差 ≤ 0.5 / Scale）：
//   - NetQuantize       ：±0.5 unit
//   - NetQuantize10     ：±0.0625 (≈1/16) unit
//   - NetQuantize100    ：±0.00390625 (≈1/256) unit
//   - NetQuantizeNormal ：±1/65536（每分量 16-bit signed unit-float）
//
// NaN/Inf 处理：
//   - Validate 拒绝 NaN（返回 false → 上层断连）。
//   - Quantize 时若 ContainsNaN，写日志并清零（防御性编程）。
//   - Deserialize 反向也会校验整数全精度路径下的 NaN，错误置位 GNetError_InvalidValue。
//
// SerializeDelta：
//   - 对量化整数路径采用"per-component diff + 自适应 bit count"压缩；
//   - 对全精度路径采用"per-component changed mask + 32/64 位完整重发"。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "PackedVectorNetSerializers.generated.h"

/** FVector_NetQuantize 配置，空。整数化（Scale=1）。 */
USTRUCT()
struct FVectorNetQuantizeNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FVector_NetQuantize10 配置，空。Scale=2^3=8。 */
USTRUCT()
struct FVectorNetQuantize10NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FVector_NetQuantize100 配置，空。Scale=2^7=128。 */
USTRUCT()
struct FVectorNetQuantize100NetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FVector_NetQuantizeNormal 配置，空。每分量 16-bit signed unit-float。 */
USTRUCT()
struct FVectorNetQuantizeNormalNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// For FVector_NetQuantize. Replicates the components as rounded integers. No clamping- if the values are very large they will be sent as full floats.
// 用于 FVector_NetQuantize：分量取整后发送整数；不做截断——超过缩放可表达范围时降级为完整浮点位发送。
UE_NET_DECLARE_SERIALIZER(FVectorNetQuantizeNetSerializer, IRISCORE_API);

// For FVector_NetQuantize10. Replicates components scaled by 8 as rounded integers. No clamping- if the values are very large they will be sent as full floats.
// 用于 FVector_NetQuantize10：分量乘以 8 后取整发送整数。误差 ≤ 1/16。超大值降级完整浮点。
UE_NET_DECLARE_SERIALIZER(FVectorNetQuantize10NetSerializer, IRISCORE_API);

// For FVector_NetQuantize100. Replicates components scaled by 128 as rounded integers. No clamping- if the values are very large they will be sent as full floats.
// 用于 FVector_NetQuantize100：分量乘以 128 后取整。误差 ≤ 1/256。超大值降级完整浮点。
UE_NET_DECLARE_SERIALIZER(FVectorNetQuantize100NetSerializer, IRISCORE_API);

// For FVector_NetQuantizeNormal. Replicates components with 16 bits per component.
// 用于 FVector_NetQuantizeNormal：每分量 16-bit signed unit-float（取值假设在 [-1,1]）。
UE_NET_DECLARE_SERIALIZER(FVectorNetQuantizeNormalNetSerializer, IRISCORE_API);

}
