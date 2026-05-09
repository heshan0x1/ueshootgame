// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaseline.h（Private）
// ---------------------------------------------------------------------------------------------
// 模块定位：Iris / ReplicationSystem / DeltaCompression 子模块的"基线视图"——
//          调用方（ReplicationWriter）从 BaselineManager 取出基线后看到的轻量视图结构。
//
// 概念：
//   * "基线（Baseline）" = 某一时刻该对象量化后的内部状态快照（StateBuffer）+
//     自上次基线以来累计的 ChangeMask。后续 SerializeDelta 会用它做差分压缩。
//   * 视图本身不持有所有权；真正的存储由 FDeltaCompressionBaselineStorage 用引用计数管理，
//     被多连接共享同一份 StateBuffer（节省内存）。
//
// 生命周期（与 FDeltaCompressionBaselineManager / Storage 协作）：
//
//          [PreSendUpdate]                                           [PostSendUpdate]
//                │                                                          │
//   Reserve ─────┼───────► Conn1.AddRef ─────► Conn2.AddRef ────────────────┤
//   (本帧第一次  │         (Manager.CreateBaseline)                          │
//    建议建基线) │                                                          │
//                ▼                                                          ▼
//      BaselineStorage                                          OptionallyCommit&Release
//      .ReserveBaselineForCurrentState                          (Storage.OptionallyCommitAndDoReleaseBaseline)
//                                                                  ├─ refcount > 0 → 真正 Commit 到 ReplicationStateStorage 中
//                                                                  └─ refcount = 0 → Cancel reservation 释放空间
//
//   失效路径（中途）：Conditionals 变化 / Filter 变化 / Conn 退出 / 显式 Invalidate
//                    └→ FDeltaCompressionBaselineInvalidationTracker 记录
//                    └→ Manager.PreSendUpdate.InvalidateBaselinesDueToModifiedConditionals
//                       逐 (Object, Conn) 释放对应 InternalBaseline。
// =============================================================================================

#pragma once

#include "Iris/ReplicationSystem/ChangeMaskUtil.h"

namespace UE::Net::Private
{

/**
 * 基线视图（轻量，非所有者）。
 *
 * 由 FDeltaCompressionBaselineManager::CreateBaseline / GetBaseline 返回给 ReplicationWriter。
 * Writer 的 SerializeDelta 路径会读取：
 *   * StateBuffer：上一次量化态，用作差分基准；
 *   * ChangeMask ：自上次基线以来累计的脏位（在创建该基线时被打包进来）。
 *
 * 注意：本结构不释放任何资源；释放走 BaselineStorage 的引用计数（AddRef/Release）。
 */
class FDeltaCompressionBaseline
{
public:
	/** StateBuffer 是否非空。返回 false 表示当前没有可用基线（例如刚断流或刚被作废）。 */
	bool IsValid() const { return StateBuffer != nullptr; }

	/** 自上次基线以来累计的脏 ChangeMask（按 ChangeMaskStorageType 分字存储）。 */
	const ChangeMaskStorageType* ChangeMask = nullptr;
	/** 量化后内部状态缓冲（即上一次发送时刻的状态）。 */
	const uint8* StateBuffer = nullptr;
};

}
