// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 ELinkGenerationDebugFlags —— 控制"自动链接生成"（NavLink Jump Auto-
//   Generation）过程中哪些中间数据要以 Debug 形态可视化。
//   作为位标记枚举（Bitflags）使用，可在编辑器里多选。
//
// 使用：RecastNavMesh / RecastNavMeshGenerator 在启用 bGenerateLinks 时，
//      按该位集选择性输出 Walkable Surface / Borders / 采样轨迹 / 链接等调试几何。
// =============================================================================
#pragma once

#include "LinkGenerationDebugFlags.generated.h"

// 链接生成调试开关（按位多选）。
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ELinkGenerationDebugFlags : uint16
{
	WalkableSurface				    = 1 << 0, // 可行走表面（候选源面）。
	WalkableBorders				    = 1 << 1, // 可行走表面的边界（是采样起点）。
	SelectedEdge				    = 1 << 2, // 当前选中的边（UI 选择后）。
	SelectedEdgeTrajectory		    = 1 << 3, // 选中边的跳跃轨迹采样。
	SelectedEdgeLandingSamples	    = 1 << 4, // 选中边的落点候选样本。
	SelectedEdgeCollisions		    = 1 << 5, // 选中边采样期间发生的碰撞。
	SelectedEdgeCollisionsSamples	= 1 << 6, // 选中边的碰撞样本点。
	Links						    = 1 << 7, // 最终生成的链接。
	FilteredLinks				    = 1 << 8, // 被过滤掉的候选链接（失败原因辅助）。
};
ENUM_CLASS_FLAGS(ELinkGenerationDebugFlags);
