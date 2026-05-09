// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   实现 FRecastInternalDebugData 的 vertex/text/end 三个方法。
//   把 Recast 生成过程里 duDebugDraw 的回调转成 UE 的三角/线段/点数组。
//
// 注意：本文件是"放在 Public 目录下的 cpp"，跟随头文件一起使用，未编入 Private 目录。
// =============================================================================

#include "NavMesh/RecastInternalDebugData.h"

#include "HAL/PlatformCrt.h"
#include "Misc/AssertionMacros.h"
#include "NavMesh/RecastHelpers.h"

#if WITH_RECAST

#include "DebugUtils/DebugDraw.h"

// Recast 递交一个顶点：先把 Recast 坐标转 UE 坐标、打包颜色转 FColor，
// 再根据当前 CurrentPrim 分流到 Point/Line/Triangle 数组。
void FRecastInternalDebugData::vertex(const FVector::FReal x, const FVector::FReal y, const FVector::FReal z, unsigned int color, const FVector::FReal u, const FVector::FReal v)
{
	const FVector::FReal RecastPos[3] = { x,y,z };
	const FVector Pos = Recast2UnrealPoint(RecastPos);
	const FColor Color = Recast2UnrealColor(color);
	// 根据当前图元类型分拣到不同缓冲区；Quad 也先存到三角缓冲，end() 再补索引。
	switch(CurrentPrim)
	{
	case DU_DRAW_POINTS:
		PointVertices.Push(Pos);
		PointColors.Push(Color);
		break;
	case DU_DRAW_LINES:
		LineVertices.Push(Pos);
		LineColors.Push(Color);
		break;
	case DU_DRAW_TRIS:
		// Fallthrough
	case DU_DRAW_QUADS:
		TriangleVertices.Push(Pos);
		TriangleColors.Push(Color);
		break;
	}
}

// 文字标注：坐标转换后，位置与文本存进平行数组。
void FRecastInternalDebugData::text(const FVector::FReal x, const FVector::FReal y, const FVector::FReal z, const char* text)
{
	const FVector::FReal RecastPos[3] = { x,y,z };
	const FVector Pos = Recast2UnrealPoint(RecastPos);
	LabelVertices.Push(Pos);
	Labels.Push(FString(text));
}

// 结束当前图元批次：为三角/四边形补齐索引缓冲。
void FRecastInternalDebugData::end()
{
	// 分支：Quad 要拆成两个三角形（0-1-3, 3-1-2）。
	if (CurrentPrim == DU_DRAW_QUADS)
	{
		// Turns quads to triangles
		// 迭代目标：对本批 FirstVertexIndex 开始的每 4 个顶点生成 6 个三角形索引。
		for (int32 i = FirstVertexIndex; i < TriangleVertices.Num(); i += 4)
		{
			ensure(i + 3 < TriangleVertices.Num());
			TriangleIndices.Push(i + 0);
			TriangleIndices.Push(i + 1);
			TriangleIndices.Push(i + 3);

			TriangleIndices.Push(i + 3);
			TriangleIndices.Push(i + 1);
			TriangleIndices.Push(i + 2);
		}
	}
	// 分支：Tri 已经是逐三角顶点，按顺序追加索引即可。
	else if (CurrentPrim == DU_DRAW_TRIS)
	{
		// Add indices for triangles.
		// 迭代目标：为本批每个顶点追加递增索引。
		for (int32 i = FirstVertexIndex; i < TriangleVertices.Num(); i++)
		{
			TriangleIndices.Push(i);
		}
	}
}
#endif // WITH_RECAST
