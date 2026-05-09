// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   实现 RecastHelpers.h 声明的 UE ↔ Recast 坐标系转换函数。
//
// 坐标映射：
//   Unreal(x, y, z)  → Recast(-x, z, -y)
//   Recast(rx,ry,rz) → Unreal(-rx, -rz, ry)
//   颜色 0xAABBGGRR(RecastColor) → FColor(R,G,B,A)。
// =============================================================================

#include "NavMesh/RecastHelpers.h"

// UE → Recast 的 FVector 版：直接按 (-x, z, -y) 重排。
FVector Unreal2RecastPoint(const FVector& UnrealPoint)
{
	return FVector(-UnrealPoint[0], UnrealPoint[2], -UnrealPoint[1]);
}

// UE → Recast 的双精度指针版（供 Detour 接口直接用 double[3]）。
FVector Unreal2RecastPoint(const FVector::FReal* UnrealPoint)
{
	return FVector(-UnrealPoint[0], UnrealPoint[2], -UnrealPoint[1]);
}

// UE → Recast 的 float 指针版（旧接口兼容；LWC 之前 Recast 全部用 float）。
FVector Unreal2RecastPoint(const float* UnrealPoint)
{
	return FVector(-UnrealPoint[0], UnrealPoint[2], -UnrealPoint[1]);
}

// UE AABB → Recast AABB：对两个端点做坐标变换，再用 FBox(Points, 2) 让构造器自动找 Min/Max。
FBox Unreal2RecastBox(const FBox& UnrealBox)
{
	FVector Points[2] = { Unreal2RecastPoint(UnrealBox.Min), Unreal2RecastPoint(UnrealBox.Max) };
	return FBox(Points, 2);
}

// UE → Recast 的常量变换矩阵（行向量对应 -x,-z,+y 轴）。缓存为 static 避免反复构造。
FMatrix Unreal2RecastMatrix()
{
	static FMatrix TM(FVector(-1.f,0,0),FVector(0,0,-1.f),FVector(0,1.f,0),FVector::ZeroVector);
	return TM;
}

// Recast → UE 的双精度指针版：(-rx, -rz, ry)。
FVector Recast2UnrealPoint(const FVector::FReal* RecastPoint)
{
	return FVector(-RecastPoint[0], -RecastPoint[2], RecastPoint[1]);
}

// Recast → UE 的 float 指针版。
FVector Recast2UnrealPoint(const float* RecastPoint)
{
	return FVector(-RecastPoint[0], -RecastPoint[2], RecastPoint[1]);
}

// Recast → UE 的 FVector 版。
FVector Recast2UnrealPoint(const FVector& RecastPoint)
{
	return FVector(-RecastPoint.X, -RecastPoint.Z, RecastPoint.Y);
}

// Recast AABB (double*, double*) → UE FBox：同上，交给 FBox 构造器修正 Min/Max。
FBox Recast2UnrealBox(const FVector::FReal* RecastMin, const FVector::FReal* RecastMax)
{
	FVector Points[2] = { Recast2UnrealPoint(RecastMin), Recast2UnrealPoint(RecastMax) };
	return FBox(Points, 2);
}

// Recast AABB (float*, float*) → UE FBox：float 版兼容，内部已标记 Deprecated。
FBox Recast2UnrealBox(const float* RecastMin, const float* RecastMax)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FVector Points[2] = { Recast2UnrealPoint(RecastMin), Recast2UnrealPoint(RecastMax) };
	return FBox(Points, 2);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// Recast → UE 的 FBox 版（整体 FBox 重算 Min/Max）。
FBox Recast2UnrealBox(const FBox& RecastBox)
{
	FVector Points[2] = { Recast2UnrealPoint(RecastBox.Min), Recast2UnrealPoint(RecastBox.Max) };
	return FBox(Points, 2);
}

// Recast → UE 的常量变换矩阵（等同于 Unreal2Recast 的逆变换，刚好也是对称的自逆矩阵）。
FMatrix Recast2UnrealMatrix()
{
	static FMatrix TM(FVector(-1.f, 0, 0), FVector(0, 0, -1.f), FVector(0, 1.f, 0), FVector::ZeroVector);
	return TM;
}

// Recast 使用小端打包的 0xAABBGGRR；FColor 内部是 BGRA。先按位拆开再组装，避免内存布局差异。
FColor Recast2UnrealColor(const unsigned int RecastColor)
{
	const uint8 R = (RecastColor & 0xFF);
	const uint8 G = (RecastColor & 0xFF00) >> 8;
	const uint8 B = static_cast<uint8>((RecastColor & 0xFF0000) >> 16);
	const uint8 A = static_cast<uint8>((RecastColor & 0xFF000000) >> 24);
	return FColor(R,G,B,A);	// can't be direct assignation since internally FColor is not RGBA
}