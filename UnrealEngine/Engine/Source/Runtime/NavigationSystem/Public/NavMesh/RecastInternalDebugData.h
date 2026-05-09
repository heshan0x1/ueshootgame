// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 FRecastInternalDebugData —— Recast Tile 生成过程中保留中间产物
//   （体素化、Contour、PolyMesh、DetailMesh、Link 采样等）用于可视化的容器。
//   派生自 Recast 的 duDebugDraw 接口，把 Recast 内部的 begin/vertex/end
//   绘制调用翻译成 UE 的三角/线段/点集，供 NavMeshRenderingComponent 消费。
//
// 编译门：仅 WITH_RECAST 且 RECAST_INTERNAL_DEBUG_DATA 开启时使用。
//
// 与其它文件的关系：
//   - 实现：Public/NavMesh/RecastInternalDebugData.cpp（同目录 cpp，少见放在 Public 下）。
//   - 消费：FRecastTileGenerator 构建 Tile 时写入；Rendering 层读出渲染。
// =============================================================================

#pragma once

#include "Containers/Array.h"
#include "Math/Color.h"
#include "Math/UnrealMathSSE.h"
#include "Math/Vector.h"
#include "NavMesh/RecastHelpers.h"

#if WITH_RECAST

#include "DebugUtils/DebugDraw.h"

// Recast 调试绘制输出容器：实现 duDebugDraw 接口，把绘制指令收集成三角/线/点数组。
struct FRecastInternalDebugData : public duDebugDraw
{
	// 当前正在绘制的图元类型（由 begin() 切换）。
	duDebugDrawPrimitives CurrentPrim = DU_DRAW_POINTS;
	// 本批图元第一个顶点在 TriangleVertices 数组中的起始索引（供 end() 生成 index）。
	int32 FirstVertexIndex = 0;

	// 三角形索引（begin/vertex/end 之后由 end() 补齐）。
	TArray<uint32> TriangleIndices;
	// 三角形顶点与对应顶点颜色（顺序同 Recast 递交顺序）。
	TArray<FVector> TriangleVertices;
	TArray<FColor> TriangleColors;

	// 线段顶点（两两一组）。
	TArray<FVector> LineVertices;
	TArray<FColor>  LineColors;

	// 调试点的位置与颜色。
	TArray<FVector> PointVertices;
	TArray<FColor>  PointColors;

	// 文字标注：位置与对应字符串。
	TArray<FVector> LabelVertices;
	TArray<FString> Labels;

	// 各阶段计时（方便 profiling 每块构建耗时）。
	double BuildTime = 0.;
	double BuildCompressedLayerTime = 0.;
	double BuildNavigationDataTime = 0.;
	double BuildLinkTime = 0;

	// 输入三角数（便于观察 Tile 规模）。
	uint32 TriangleCount = 0;
	// Tile 分辨率等级索引（Low/Default/High）。
	unsigned char Resolution = 0;
	
	FRecastInternalDebugData() {}
	virtual ~FRecastInternalDebugData() override {}

	// Recast duDebugDraw 接口：深度掩码/纹理开关；本实现不需要，留空。
	virtual void depthMask(bool state) override { /*unused*/ };
	virtual void texture(bool state) override { /*unused*/ };

	// 切换图元类型，记录起始索引（给 end() 构建索引缓冲用）。
	virtual void begin(duDebugDrawPrimitives prim, float size = 1.0f) override
	{
		CurrentPrim = prim;
		FirstVertexIndex = TriangleVertices.Num();
	}

	// 多个 vertex() 重载全部委托到最下面的完整版本，避免代码重复。
	virtual void vertex(const FVector::FReal* pos, unsigned int color) override
	{
		vertex(pos[0], pos[1], pos[2], color, 0.0f, 0.0f);
	}

	virtual void vertex(const FVector::FReal x, const FVector::FReal y, const FVector::FReal z, unsigned int color) override
	{
		vertex(x, y, z, color, 0.0f, 0.0f);
	}

	virtual void vertex(const FVector::FReal* pos, unsigned int color, const FVector::FReal* uv) override
	{
		vertex(pos[0], pos[1], pos[2], color, uv[0], uv[1]);
	}

	// 主顶点入口：Recast 坐标 → UE 坐标，再按 CurrentPrim 分流到对应数组。
	NAVIGATIONSYSTEM_API virtual void vertex(const FVector::FReal x, const FVector::FReal y, const FVector::FReal z, unsigned int color, const FVector::FReal u, const FVector::FReal v) override;

	// 文字标注（位置 Recast 坐标）。
	NAVIGATIONSYSTEM_API virtual void text(const FVector::FReal x, const FVector::FReal y, const FVector::FReal z, const char* text) override;

	// 根据当前 CurrentPrim 生成 TriangleIndices（Quad → 两个三角；Tri → 顺序索引）。
	NAVIGATIONSYSTEM_API virtual void end() override;
};
#endif // WITH_RECAST
