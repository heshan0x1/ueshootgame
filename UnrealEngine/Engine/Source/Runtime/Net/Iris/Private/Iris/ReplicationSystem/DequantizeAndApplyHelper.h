// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：DequantizeAndApplyHelper.h
// 模块：Iris / ReplicationSystem（辅助 & 周边）
// 功能：客户端接收侧"内部 buffer → 外部 buffer + Apply"统一流程。
//
// 接收链路（FReplicationReader 内部 batch 解析后调用本 Helper）：
//   ┌─────────────────────────────────────────────────────────────────┐
//   │ batch 已 Deserialize 到 SrcObjectStateBuffer（量化态/内部态）   │
//   │ 同时拿到 ChangeMask 与 UnresolvedReferencesChangeMask           │
//   └─────────────────────────────────────────────────────────────────┘
//                              │
//                              ▼
//   FDequantizeAndApplyHelper::Initialize
//     · 按 ChangeMask 切分 per-state 子位图
//     · 为每个有脏的 ReplicationStateDescriptor：
//         - 分配 ExternalState buffer（除非 Fragment 持久化）
//         - 调 FReplicationStateOperations::Dequantize / DequantizeWithMask
//         - 计算/记录 Unresolved 引用标记
//         - 收集 Owner（用于 PreNetReceive/PostNetReceive/RepNotify）
//                              │
//                              ▼
//   FDequantizeAndApplyHelper::ApplyAndCallLegacyFunctions
//     · PreNetReceive 回调
//     · 对每个状态 → Fragment::ApplyReplicatedState
//     · PostNetReceive 回调
//     · CallRepNotifies（仅含 RepNotifies trait 的 fragment）
//     · PostRepNotifies 回调
//                              │
//                              ▼
//   FDequantizeAndApplyHelper::Deinitialize
//     · 析构（非持久化）ExternalState buffer
//
// 拆分多步的原因：FReplicationReader 在某些路径（PendingBatchData 解引用）
// 需要"先 Apply 但 RepNotify 延迟到引用解析后再发"——这就是
// ApplyAndCallLegacyPreApplyFunction + CallLegacyPostApplyFunctions 的拆分。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Net/Core/NetBitArray.h"

class FMemStackBase;

namespace UE::Net
{
	class FNetSerializationContext;
	struct FReplicationInstanceProtocol;
	struct FReplicationProtocol;

	/**
	 * FDequantizeAndApplyParameters
	 * 中文：调用入口的输入打包。所有指针/数据由 FReplicationReader 在解析单
	 * 个对象 batch 时填好。
	 */
	struct FDequantizeAndApplyParameters
	{
		// 中文：临时分配器（per-batch 用 FMemMark scope，结束统一回收）。
		FMemStackBase* Allocator = nullptr;
		// 中文：本 batch 收到的 ChangeMask 位流原始字数据，BitCount = Protocol->ChangeMaskBitCount。
		const uint32* ChangeMaskData = nullptr;
		// 中文：未解析引用的位图（与 ChangeMask 对齐）；可为空表示全部已解析。
		const uint32* UnresolvedReferencesChangeMaskData = nullptr;
		// 中文：内部态（Quantize 后）buffer 起始地址；按各 Descriptor 的 InternalSize/Alignment 排列。
		const uint8* SrcObjectStateBuffer = nullptr;
		// 中文：实例协议（fragment 列表 + ExternalSrcBuffer 地址）。
		const FReplicationInstanceProtocol* InstanceProtocol = nullptr;
		// 中文：协议（StateDescriptor 列表 + ChangeMaskBitCount 等）。
		const FReplicationProtocol* Protocol = nullptr;
		// 中文：Init 阶段是否包含未解析引用（仅 IsInitState 时有意义）。
		bool bHasUnresolvedInitReferences = false;
	};
}

namespace UE::Net::Private
{	

/**
 * FDequantizeAndApplyHelper
 * 中文：纯静态门面 + 内部 FContext。Initialize 返回的 FContext* 由调用方持有，
 * 在 Apply/Deinit 阶段透传——上下文里缓存了所有"需要 Apply"的 fragment。
 */
struct FDequantizeAndApplyHelper
{
	// 中文：内部上下文（cpp 中定义）；外部只持有指针。
	struct FContext;

	// 中文：Step 1 —— 按 ChangeMask 解出每个 State 的位图，对脏的 state
	// 执行 Dequantize 到 ExternalState buffer，收集 Owner / Unresolved 信息。
	static FContext* Initialize(FNetSerializationContext& NetSerializationContext, const FDequantizeAndApplyParameters& Parameters);
	// 中文：Step 2 —— 一次性 PreNet + Apply + PostNet + RepNotify + PostRepNotify。
	static void ApplyAndCallLegacyFunctions(FContext* Context, FNetSerializationContext& NetSerializationContext);
	// 中文：Step 2.b —— 仅做 PostNetReceive + RepNotify + PostRepNotifies。
	// 用于"先 Apply、后续等引用解析完再发 RepNotify"的延迟路径。
	static void CallLegacyPostApplyFunctions(FContext* Context, FNetSerializationContext& NetSerializationContext);
	// 中文：Step 2.a —— 仅做 PreNetReceive + Apply。配合 CallLegacyPostApplyFunctions
	// 实现两段式 Apply。
	static void ApplyAndCallLegacyPreApplyFunction(FContext* Context, FNetSerializationContext& NetSerializationContext);
	// 中文：Step 3 —— 析构 ExternalState 临时 buffer（持久化 fragment 跳过）。
	// MemStack 内存由调用方在 FMemMark 退出时回收，本函数仅负责 C++ 析构语义。
	static void Deinitialize(FContext* Context);
};

}
