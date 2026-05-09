// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：UNetObjectPrioritizer 的轻量基类实现。仅提供可选 hook 的空默认实现：AddConnection / RemoveConnection /
// PrePrioritize / PostPrioritize。所有派生类必须实现的纯虚（Init/Deinit/OnMaxInternalNetRefIndexIncreased/
// AddObject/RemoveObject/UpdateObjects/Prioritize）由派生类负责。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectPrioritizer)

/**
Have spatial prioritizers and non-spatial prioritizers. Kind of like ReplicationGraphNodes if you will.
How many spatial prioritizers can be set per object?
How many non-spatial prioritizers can be set per object?

Could fairly easily support up to 32 (64) prioritizers per object if using a mask. Could be somewhat expensive to traverse all objects per prioritizer,
or complex logic to splat out prioritizer IDs. Another idea is to have a BitArray per prioritizer, but that bit array must be large enough to hold MaxObjectCount.
Ok for thousands of objects (a few KB), but expensive for millions of objects.

If supporting multiple prioritizers each prioritizer needs to be careful to honor the existing priority? Or enforce via logic MAXing between runs, requires temp storage.
*/
// Epic 内部设计 memo（保留原文，作历史性参考）：
//   - 当前实现中，每个对象只能绑定到 1 个 prioritizer（通过 ObjectIndexToPrioritizer[ObjectIndex] = uint8 索引）。
//   - 上述讨论提出了"多 prioritizer 共存"的方案（位掩码 / 多 BitArray），尚未实现。
//   - 本文件的 PrePrioritize/PostPrioritize hook 已为这种"多 prioritizer 共存 + 取最大优先级"的演进路径预留了空间。

UNetObjectPrioritizer::UNetObjectPrioritizer()
{
}

// 默认空实现：派生类如需 per-connection 状态可重写（例如 NetObjectCountLimiter::AddConnection 分配 LastConsiderFrames 数组）。
void UNetObjectPrioritizer::AddConnection(uint32 ConnectionId)
{
}

void UNetObjectPrioritizer::RemoveConnection(uint32 ConnectionId)
{
}

// 默认空实现：派生类可在所有连接打分前做一次性准备工作（例如 NetObjectCountLimiter 的 RoundRobin 推进）。
void UNetObjectPrioritizer::PrePrioritize(FNetObjectPrePrioritizationParams&)
{
}

void UNetObjectPrioritizer::PostPrioritize(FNetObjectPostPrioritizationParams&)
{
}
