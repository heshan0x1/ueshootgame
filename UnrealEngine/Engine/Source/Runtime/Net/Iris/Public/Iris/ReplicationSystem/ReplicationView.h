// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationView.h
// 角色：每条网络连接（含子连接 / 分屏）的"视口"信息容器，供 Filter / Prioritizer 使用。
//
// 用途：
//   * Spatial Filter（NetObjectGridFilter）依据 Pos 决定哪些对象进入连接的 Scope；
//   * Distance / Sphere / FOV Prioritizer 据此对脏对象打分；
//   * 多 sub-view（分屏 / 多人本地）时通过 TArray<FView> 拼接（默认内联 4 个，
//     可由 UE_IRIS_INLINE_VIEWS_PER_CONNECTION 在 Target.cs 调整）。
//
// 由 NetDriver 在每帧 UpdateReplicationViews 后调用 UReplicationSystem::SetReplicationView
// 写入；信息持续到下一次 SetReplicationView 调用为止。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Net/Core/NetHandle/NetHandle.h"

// Define this macro in your game's Target.cs and set it to the maximum clients per connection your game can have (ex: splitscreen).
// 将该宏定义在 Target.cs 中可调整 TInlineAllocator 大小（避免分屏场景额外堆分配）。
#ifndef UE_IRIS_INLINE_VIEWS_PER_CONNECTION
	#define UE_IRIS_INLINE_VIEWS_PER_CONNECTION 4
#endif

namespace UE::Net
{

/**
 * 一条连接（及其本地子连接）的复制视口集合。
 *
 * 字段：
 *   Views : 多视口数组（分屏 / 子连接），通常 1 个；至多 UE_IRIS_INLINE_VIEWS_PER_CONNECTION 个内联存储。
 */
struct FReplicationView
{
	/** 单个视口（一名玩家的"看哪、看向哪、视野角"信息）。 */
	struct FView
	{
		/** The controlling net object associated with this view, typically a player controller. */
		/** 控制该视口的 NetHandle，通常对应 PlayerController（可能为无效）。 */
		FNetHandle Controller;
		/** The actor that is being directly viewed, usually a pawn. */
		/** 视口的"被观察对象"，通常是 Pawn / 摄像机 Actor。 */
		FNetHandle ViewTarget;
		/** Where the viewer is looking from */
		/** 视点世界位置（供距离/球体/网格类 Filter / Prioritizer 使用）。 */
		FVector Pos = FVector::ZeroVector;
		/** Direction the viewer is looking */
		/** 视线方向（单位向量），FOV Prioritizer 据此判断对象是否在视锥内。 */
		FVector Dir = FVector::ForwardVector;
		/** The field of view */
		/** 视野半角（弧度）。默认为 PI/2 = 90°半角 ≈ 180° 全角。 */
		float FoVRadians = UE_HALF_PI;
	};

	// 分屏 / 多本地玩家：多个 FView 拼合形成一条连接的"复合视口"。
	TArray<FView, TInlineAllocator<UE_IRIS_INLINE_VIEWS_PER_CONNECTION>> Views;
};

}
