// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// VectorNetSerializers.h
// ---------------------------------------------------------------------------------------------
// 三组分浮点向量（FVector / FVector3f / FVector3d）的"原位"NetSerializer 公开声明。
//
// 角色：Iris L2 "具体类型 Serializer"，对应 FProperty 反射类型 FVector/3f/3d。
//
// 与 PackedVectorNetSerializers 的区别：
//   - 本组 Serializer **不做量化**，按"位精确（bit-exact）"原位发送 X/Y/Z 三个浮点。
//   - 单分量带 1-bit 零标志位优化（典型零向量 / 部分轴为 0 时省带宽）。
//   - 适用于"必须保留浮点位级精度"的场景（如确定性同步、Hash/Replay）；
//   - 大多数游戏世界坐标场景应优先使用 PackedVector 系列以节省带宽。
//
// 三个 Serializer：
//   - FVectorNetSerializer   ：FVector（UE 默认精度，5.0+ 默认是 double）；模板特化。
//   - FVector3fNetSerializer ：强制 32-bit 单精度。
//   - FVector3dNetSerializer ：强制 64-bit 双精度。
//
// 实现要点（详见 .cpp）：
//   - 内部用模板 FFloatTripletNetSerializer<T>，T = float/double。
//   - SourceType 为 `FFloatTriplet`（uint32 或 uint64 三元组），同 Float Serializer 设计：
//     避免 IEEE 754 的 ±0/NaN 比较坑，IsEqual 直接走默认实现。
// =============================================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "VectorNetSerializers.generated.h"

/** FVector（UE 默认精度，5.0+ 通常为 double）原位 Serializer 配置。空。 */
USTRUCT()
struct FVectorNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FVector3f（强制单精度 float）原位 Serializer 配置。空。 */
USTRUCT()
struct FVector3fNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FVector3d（强制双精度 double）原位 Serializer 配置。空。 */
USTRUCT()
struct FVector3dNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// FVector：跟随 UE 配置（LWC：double；非 LWC：float）。
UE_NET_DECLARE_SERIALIZER(FVectorNetSerializer, IRISCORE_API);

// FVector3f：固定 32-bit float。
UE_NET_DECLARE_SERIALIZER(FVector3fNetSerializer, IRISCORE_API);

// FVector3d：固定 64-bit double。
UE_NET_DECLARE_SERIALIZER(FVector3dNetSerializer, IRISCORE_API);

}
