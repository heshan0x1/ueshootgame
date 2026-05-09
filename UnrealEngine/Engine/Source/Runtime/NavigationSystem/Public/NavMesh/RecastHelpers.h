// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Coord system utilities
 *
 * Translates between Unreal and Recast coords.
 * Unreal: x, y, z
 * Recast: -x, z, -y
 */

// =============================================================================
// 文件概览：
//   提供 UE 与 Recast（Epic fork 的 Recast & Detour）坐标系的双向转换函数。
//   - UE 使用左手坐标 (X 向前, Y 向右, Z 向上)；
//   - Recast 使用右手坐标 (X 向左, Y 向上, Z 向前)；
//   因此互转为 (X,Y,Z) ↔ (-X, Z, -Y)。颜色/矩阵也有对应 helper。
//
// 导出函数：Unreal2Recast* / Recast2Unreal*（Point / Box / Matrix / Color）。
// 实现文件：Private/NavMesh/RecastHelpers.cpp。
//
// 与其它文件的关系：
//   - 在整个 NavMesh 生成、Tile 顶点导入/导出、调试绘制都被高频调用。
//   - Recast/Detour 接口（float 指针）与 UE（FVector）之间的胶水层。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "Math/Box.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "Math/UnrealMathSSE.h"
#endif
#include "Math/Vector.h"


// UE 坐标（x, y, z） → Recast 坐标（-x, z, -y）——裸指针版（用于 Detour 接口）。
extern NAVIGATIONSYSTEM_API FVector Unreal2RecastPoint(const FVector::FReal* UnrealPoint);
// UE → Recast 的 FVector 版（等价于指针版）。
extern NAVIGATIONSYSTEM_API FVector Unreal2RecastPoint(const FVector& UnrealPoint);
// 对 AABB 两个端点做坐标变换后重新构造 FBox（注意：变换后 Min/Max 语义不保证）。
extern NAVIGATIONSYSTEM_API FBox Unreal2RecastBox(const FBox& UnrealBox);
// 返回 UE→Recast 的常量变换矩阵（用于批量变换几何）。
extern NAVIGATIONSYSTEM_API FMatrix Unreal2RecastMatrix();

// Recast 坐标（-x, z, -y） → UE 坐标（x, y, z）——裸指针版。
extern NAVIGATIONSYSTEM_API FVector Recast2UnrealPoint(const FVector::FReal* RecastPoint);
// Recast → UE 的 FVector 版。
extern NAVIGATIONSYSTEM_API FVector Recast2UnrealPoint(const FVector& RecastPoint);

// Recast AABB (float*, float*) → UE FBox（Min/Max 会因变换翻转，FBox 构造器会重排）。
extern NAVIGATIONSYSTEM_API FBox Recast2UnrealBox(const FVector::FReal* RecastMin, const FVector::FReal* RecastMax);
extern NAVIGATIONSYSTEM_API FBox Recast2UnrealBox(const FBox& RecastBox);
// Recast → UE 的常量变换矩阵。
extern NAVIGATIONSYSTEM_API FMatrix Recast2UnrealMatrix();
// Recast 的 32 位打包颜色（0xAABBGGRR 布局）→ UE FColor（内部 BGRA）。
extern NAVIGATIONSYSTEM_API FColor Recast2UnrealColor(const unsigned int RecastColor);
