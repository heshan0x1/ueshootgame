// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// IrisConstants.h —— Iris 框架对外公开的「公共数值常量」集合
// -----------------------------------------------------------------------------
// 本文件极小，仅承载 Iris 子系统中跨模块共用的「哨兵值（sentinel）」常量：
//   * InvalidReplicationSystemId —— 表示「无效 / 未指定」的 ReplicationSystem
//     索引；ReplicationSystem 在创建时由 FReplicationSystemFactory 分配 32-bit
//     ID，0~N 为合法值，0xFFFFFFFF 用于「初始化时尚未分配」的占位。
//   * InvalidConnectionId        —— 表示「无效连接」的 ConnectionId；Iris 中
//     合法连接 ID 从 1 开始，0 始终代表「不指向任何连接」（既可用于 server-only
//     上下文，也可用作集合查找未命中的返回值）。
//
// 之所以把哨兵值集中于此并放在 Public/，是因为它们会出现在大量公共 API 的
// 参数 / 返回值中（例如 GetConnectionId() / GetReplicationSystemId() 等），
// 必须随头文件一起对外暴露。
// =============================================================================

#pragma once

#include "CoreTypes.h"

namespace UE::Net
{

// 「无效 ReplicationSystem ID」哨兵值。
// FReplicationSystemFactory 分配的合法 ID 范围为 [0, MaxReplicationSystems)，
// 0xFFFFFFFF 作为「无效 / 未分配」占位（构造期、错误回退路径使用）。
constexpr uint32 InvalidReplicationSystemId = 0xFFFFFFFFU;

// 「无效 Connection ID」哨兵值。
// Iris 内部对连接编号采用 1-based：合法连接编号从 1 起算，0 永远表示「无连接」，
// 既可作为 server-only / 本地路径的占位，也用于集合查询未命中的默认返回。
constexpr uint32 InvalidConnectionId = 0;

}
