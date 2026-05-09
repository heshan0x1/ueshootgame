// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetExports.h —— 每连接的 Export ACK 跟踪
// -------------------------------------------------------------------------------------------------------------
// 模块定位：跟踪两类"已发送但未确认"的 export，并按 packet 粒度处理 ACK / Lost：
//   ① FNetRefHandle exports：对象引用的导出（确保对端在解析引用之前已经知道 NetRefHandle 对应的对象）；
//   ② FNetToken exports    ：NetToken 的导出（与上面 NetTokenDataStream 协作）。
//   （另一类 FNetObjectReference 是上述两类的组合，无需单独跟踪）
//
// 状态机（简化版）：
//
//                                ┌─────────────────────────────────┐
//   InitExportRecordForPacket()  │  快照 in-flight 队列当前长度     │
//                                └─────────┬───────────────────────┘
//                                          │
//                                          ▼  写入 batch（多次）
//                          CommitExportsToRecord(BatchExports)
//                                          │
//                                          ▼
//   PushExportRecordForPacket()  ──>  ExportRecord.Enqueue(本包新增的 handle/token 数)
//                                          │
//                                          ▼
//                   等待 PacketControl 通知 ProcessPacketDeliveryStatus(Status)
//                          ┌────────────────────────┐
//                          │ Delivered:             │
//                          │   in-flight 移到       │
//                          │   AcknowledgedExports  │
//                          │ Lost:                  │
//                          │   in-flight 直接丢弃    │
//                          │   （之后 ConditionalWrite│
//                          │    再判断要不要重发）   │
//                          └────────────────────────┘
//
// 与 NetExportContext 的关系：
//   - FNetExportContext 携带"本批次已 export"集合（防止同一帧重复 export）+ "已 ACK"集合（防止重复发送）；
//   - FExportScope 在 RAII 期间把 FNetExportContext 注入 FNetSerializationContext，结束自动还原。
// =============================================================================================================

#pragma once

#include "Net/Core/Misc/ResizableCircularQueue.h"
#include "Iris/Serialization/NetExportContext.h"

namespace UE::Net
{
	enum class EPacketDeliveryStatus : uint8;
}

namespace UE::Net::Private
{

class FNetExports
{
public:

	// Simple scope to make sure we set the correct ExportContext and restore the old one when we exit the scope
	// 用 RAII 切换 FNetSerializationContext 的 ExportContext，离开作用域自动还原。
	class FExportScope
	{
	public:
		~FExportScope();

	private:
		friend class FNetExports;

		FExportScope(FNetSerializationContext& InContext, const FNetExportContext::FAcknowledgedExports& InAcknowledgedExports, FNetExportContext::FBatchExports& BatchExports);

		FNetExportContext ExportContext;       // 本 scope 的 ExportContext（持有 AcknowledgedExports + BatchExports 引用）。
		FNetSerializationContext& Context;
		FNetExportContext* OldExportContext;   // 进入前的 ExportContext，析构时还原。
	};

public:

	// Call at the beginning of the packet to store the current state of exports
	// 包开始：记录 in-flight 队列当前长度，作为本包前的"基线"。
	void InitExportRecordForPacket();

	// Commit exports from exported during current batch
	// 单个 batch 写入完成时调用：把"本 batch 已 export"集合追加到 in-flight 队列尾。
	void CommitExportsToRecord(FExportScope& ExportScope);

	// Call at the end of the packet to store record of any new exports committed during the frame
	// 包结束：把"本包新增的 handle/token 数量"快照 push 到 ExportRecord 队列，等待 packet ACK。
	void PushExportRecordForPacket();

	// 由 PacketControl 通知：根据 ACK 结果把 in-flight 队列头部的 N 个条目移入 AcknowledgedExports（或丢弃）。
	void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status);

	// Explicitly acknowledge exports, this is used for exports originating from out of band batches 
	// OOB（Send Immediate）路径不会进入 packet 跟踪，所以由调用方"显式确认"——直接合并到 Acknowledged 集合。
	void AcknowledgeBatchExports(const FNetExportContext::FBatchExports& BatchExports);

	// Construct a new export scope
	// 构造一个新的 ExportScope（通常在 ReplicationWriter 写每个 batch 之前）。
	FExportScope MakeExportScope(UE::Net::FNetSerializationContext& Context, FNetExportContext::FBatchExports& BatchExports) { return FExportScope(Context, AcknowledgedExports, BatchExports); }

private:

	// 单条 ExportRecord 信息：本包新增了多少个 NetRefHandle 和 NetToken export。
	struct FExportInfo
	{
		uint32 NetHandleExportCount;
		uint32 NetTokenExportCount;
	};

	FExportInfo PopExportRecord();

private:

	// "包级 record" 队列：与 packet 一一对应，按发送顺序排队，等待 ACK。
	TResizableCircularQueue<FExportInfo> ExportRecord;
	// in-flight 的 NetRefHandle 队列（顺序入队 / 按 record 头部出队）。
	TResizableCircularQueue<FNetRefHandle> NetHandleExports;
	// in-flight 的 NetToken 队列（同上）。
	TResizableCircularQueue<FNetToken> NetTokenExports;

	// 当前包开始时的 in-flight 队列长度快照；用于在 PushExportRecordForPacket 计算"本包新增的数量"。
	FExportInfo CurrentExportInfo;

	// Export state
	// 已 ACK 的 export 集合：写入时通过 FNetExportContext::IsExported 查询，避免重复 export。
	FNetExportContext::FAcknowledgedExports AcknowledgedExports;
};

inline FNetExports::FExportScope::FExportScope(FNetSerializationContext& InContext, const FNetExportContext::FAcknowledgedExports& AcknowledgedExports, FNetExportContext::FBatchExports& BatchExports)
	: ExportContext(AcknowledgedExports, BatchExports)
	, Context(InContext)
	, OldExportContext(InContext.GetExportContext())
{
	// 把新 ExportContext 安装到 SerializationContext，使后续 NetSerializer/Token 读写都能查询到 Acknowledged 集合。
	Context.SetExportContext(&ExportContext);
}

inline FNetExports::FExportScope::~FExportScope()
{
	// 恢复旧 ExportContext，避免污染外层调用栈。
	Context.SetExportContext(OldExportContext);
}


}
