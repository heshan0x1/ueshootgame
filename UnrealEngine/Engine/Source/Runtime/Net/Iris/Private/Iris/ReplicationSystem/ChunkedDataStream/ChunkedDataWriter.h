// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataWriter.h
// 模块: Iris / ReplicationSystem / ChunkedDataStream（发送侧实现）
//
// 概述：
//   FChunkedDataWriter 是 UChunkedDataStream 的"发送端引擎"。每条
//   UChunkedDataStream 实例内部独占一个 Writer。
//
//   职责：
//     1) 接受业务层 EnqueuePayload，把整块大 payload 入队（可附带 PackageMap
//        exports，作为额外的"export-payload"在队列里和数据 payload 共存）；
//     2) 在每次 WriteData 时按需把 SendQueue 队首条目"切分"为固定 ChunkSize
//        字节的 FDataChunk 序列，进入 DataChunksPendingSend 环；
//     3) 按序号把分片串行化进 packet 的 bitstream；
//     4) 跟踪每个分片的"已发送 / 已 ACK"两位状态（Sent[] / Acked[] 位图），
//        以及 DataChunksPendingAck 队列（FIFO 记录每个 packet 中包含的分片
//        序号），实现：
//        - Lost：把分片标记回未发送，下次 WriteData 自然重发；
//        - Delivered：标记已 ACK，PopDeliveredChunks 沿 PendingSend 环头部
//          清理"已确认且前序也都确认"的分片，前进窗口。
//     5) 流量上限保护（SendBufferMaxSize / ExportsBufferMaxSize）。
//
//   传输语义：所有分片都是"可靠 + 严格按序号顺序投递"。
//   Reader 端按 ExpectedSeq 严格递增地驱动重组，因此即便 packet 乱序
//   也能正确还原 payload 顺序。
//
// 数据结构关键链路：
//   SendQueue [FSendQueueEntry] ── 一条业务 payload（+ 可选 exports）
//      └─> SplitPayload ──> DataChunksPendingSend [FDataChunk] (RingBuffer)
//                                 │
//                                 ▼
//                          WriteData 串行化 + DataChunksPendingAck.Add(seq)
//                                 │
//                                 ▼ ProcessPacketDeliveryStatus
//                          Acked 位图 ── PopDeliveredChunks ──> 弹出环首已确认
// =============================================================================

#pragma once

#include "ChunkedDataStreamCommon.h"
#include "Containers/Array.h"
#include "Containers/RingBuffer.h"
#include "Iris/DataStream/DataStream.h"
#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "Iris/Serialization/IrisPackageMapExportUtil.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Net/Core/NetToken/NetTokenExportContext.h"

namespace UE::Net::Private
{

// Used by ChunkedDataStream to capture and send payloads and exports
class FChunkedDataWriter
{
public:
	// 透传基类类型，缩短下文签名。
	using EWriteResult = UDataStream::EWriteResult;
	using FBeginWriteParameters = UDataStream::FBeginWriteParameters;
	using FInitParameters = UDataStream::FInitParameters;
	// 构造：缓存 InitParams，并通过 ReplicationSystemId 取得 ReplicationSystem
	// 与 ObjectReferenceCache（用于在 CreateExportPayload 中处理对象引用导出）。
	FChunkedDataWriter(const FInitParameters& InParams);

	// Tracks references associated with an enqueued payload
	// FReferencesForExport：跟随某条 payload 一起发送的"导出附属信息"。
	// 业务侧在 ExportWriteScope 内序列化 UObject 引用时，PackageMap 会把这些
	// 引用 + 关联的 NetTokens 收集起来。EnqueuePayload 时会调用 CreateExportPayload
	// 把它们 quantize + 序列化为一个独立的字节缓冲区（ExportsPayload），
	// 然后作为"export chunk"先于真正的数据 chunk 发送。
	struct FReferencesForExport
	{
		// 已 quantize 并序列化好的 exports 比特流（按字节对齐到堆，便于直接拷贝进发送 chunk）。
		TArray<uint8, TAlignedHeapAllocator<>> ExportsPayload;
		// 本批 exports 在 FNetExports 中的批次记录（用于 Delivered 后调
		// AcknowledgeBatchExports 通知 NetExports 这些导出已对端确认）。
		FNetExportContext::FBatchExports BatchExports;

		// 析构：释放可能存在的 NetTrace 收集器（仅在追踪开启时分配）。
		~FReferencesForExport()
		{
			if (TraceCollector)
			{
				UE_NET_TRACE_DESTROY_COLLECTOR(TraceCollector);
			}
		}
		// NetTrace 收集器：写 export payload 期间的子事件先打到本地收集器，
		// 实际发送时再 fold 到主 stream 的 collector，便于追踪可视化。
		FNetTraceCollector* TraceCollector = nullptr;
	};
	
	// Enqueued payload to send
	// FSendQueueEntry：发送队列里的一条"逻辑 payload"。
	// - Payload 为业务侧入队的原始字节缓冲（共享，stream 侧只读）；
	// - References 可选，若入队时存在 PackageMap 捕获到的导出，它会先于 Payload
	//   被切分发送；
	// - RefCount 记录"还有多少未确认/未弹出的 FDataChunk 在引用本条目"，
	//   只有 RefCount==0 时才能被 PopDeliveredChunks 真正释放。
	struct FSendQueueEntry
	{
		FSendQueueEntry() {}
		// 移动构造：转交 Payload + References 所有权，并把对方 RefCount 清零
		// 避免双重 release。
		FSendQueueEntry(FSendQueueEntry&& Other)
		: Payload(MoveTemp(Other.Payload))
		, References(MoveTemp(Other.References))
		, RefCount(Other.RefCount)
		{
			Other.RefCount = 0;
		}

		// 由原始 payload 构造，最常用入口（EnqueuePayload）。
		FSendQueueEntry(const TSharedPtr<TArray64<uint8>>& InPayload)
		: Payload(InPayload)
		{
		}

		// 注意：这里实现的 AddRef/Release 仅维护一个简单 mutable int32 计数，
		// 配合 TRefCountPtr<FSendQueueEntry>（FDataChunk::SrcEntry）使用：
		// - FDataChunk 持有一个 RefCount，多个分片同时引用同一 SendQueueEntry；
		// - 不会自动 delete（因为 SendQueueEntry 由 SendQueue 中的 TUniquePtr 拥有），
		//   只用作"还有多少分片未结算"的引用计数指示器。
		void AddRef() const
		{
			++RefCount;
		}

		void Release() const
		{
			--RefCount;
		}
	
		// 业务原始 payload（共享所有权，业务侧释放后由 stream 持有直至发送结束）。
		TSharedPtr<TArray64<uint8>> Payload;
		// 关联的导出附属（可空）。
		TUniquePtr<FReferencesForExport> References;
		// 当前还有多少分片在引用本条目；mutable 允许 const 接口下增减。
		mutable int32 RefCount = 0;
	};

	// Split chunk of data, referencing source
	// FDataChunk：一个具体的待发送/在途分片，引用其源 SendQueueEntry。
	// 不直接拷贝 payload 字节——序列化时通过 SrcEntry->Payload + PayloadByteOffset
	// 计算地址，避免大 payload 反复内存拷贝。
	struct FDataChunk
	{
		FDataChunk();
		// 把本分片串行化到 bitstream（首片附带 PartCount/Exports 标志，其余只携带本片字节）。
		void Serialize(UE::Net::FNetSerializationContext& Context) const;
		
		// Hold a reference to the datachunk as source data is shared
		// 持引用源条目（TRefCountPtr 调 AddRef/Release）；多个分片同时持有一份。
		TRefCountPtr<FSendQueueEntry> SrcEntry;
		// 本分片在源 payload 中的字节偏移。
		uint32 PayloadByteOffset;
		// 该 payload 总分片数（仅首片需要写入对端，便于接收端预分配缓冲）。
		uint32 PartCount;
		// 11 位序号（存于 16 位字段中，发送/接收按 SequenceBitMask 折叠）。
		uint16 SequenceNumber;
		// 14 位字段足够容纳 ChunkSize=192（远小于 16384）。
		uint16 PartByteCount : 14U;
		// 是否是 payload 的首个分片（首片才编码 PartCount/bIsExportChunk）。
		uint16 bIsFirstChunk : 1U;
		// 是否是导出附属的"export chunk"（先于数据 chunk 发送）。
		uint16 bIsExportChunk : 1U;
	};

public:
	// -------- 序号 ↔ 位图槽位下标 互转 + 位图操作 --------
	// MaxUnackedDataChunkCount = 2048，构成位图的循环下标空间。

	// 把 11 位序号映射到 [0, 2048) 的位图下标。
	uint32 SequenceToIndex(uint32 Seq) const
	{
		return Seq % FChunkedDataStreamParameters::MaxUnackedDataChunkCount;
	}

	// Acked 位图：1 表示该序号槽位已收到对端 ACK。
	bool IsIndexAcked(uint32 Index) const
	{
		return (Acked[Index >> 5U] & (1U << (Index & 31U))) != 0U;
	}

	void SetIndexIsAcked(uint32 Index)
	{
		Acked[Index >> 5U] |= (1U << (Index & 31U));
	}

	void SetSequenceIsAcked(uint32 Seq)
	{
		return SetIndexIsAcked(SequenceToIndex(Seq));
	}

	void ClearIndexIsAcked(uint32 Index)
	{
		Acked[Index >> 5U] &= ~(1U << (Index & 31U));
	}

	// Sent 位图：1 表示该序号已发出（在途中）。Lost 时清零以便重发。
	bool IsIndexSent(uint32 Index) const
	{
		return (Sent[Index >> 5U] & (1U << (Index & 31U))) != 0U;
	}

	bool IsSequenceSent(uint32 Seq) const
	{
		return IsIndexSent(SequenceToIndex(Seq));
	}

	void SetIndexIsSent(uint32 Index)
	{
		Sent[Index >> 5U] |= (1U << (Index & 31U));
	}

	void SetSequenceIsSent(uint32 Seq)
	{
		return SetIndexIsSent(SequenceToIndex(Seq));
	}

	void ClearIndexIsSent(uint32 Index)
	{
		Sent[Index >> 5U] &= ~(1U << (Index & 31U));
	}

	void ClearSequenceIsSent(uint32 Seq)
	{
		return ClearIndexIsSent(SequenceToIndex(Seq));
	}

	// -------- 分片/编排核心动作 --------

	// 把一段连续字节切分为 ceil(N / ChunkSize) 个 FDataChunk 追加到 DataChunksPendingSend。
	// bIsExportPayload=true 时表示这是 exports payload 的切分（首片会标记 bIsExportChunk）。
	bool SplitPayload(FSendQueueEntry& SrcEntry, TConstArrayView<uint8> Payload, bool bIsExportPayload = false);

	// 在 EnqueuePayload 时调用：把当前已捕获到的 PackageMapExports + NetTokensPendingExport
	// quantize + serialize 到一个独立字节缓冲，包装成 FReferencesForExport 返回。
	// 返回 nullptr 表示"无需导出"或"导出溢出 ExportsBufferMaxSize"。
	FReferencesForExport* CreateExportPayload();

	// 清空已捕获的 PackageMap exports / NetTokens 池（每次 EnqueuePayload 末尾调用，
	// 让下一次入队的捕获不与本次混淆）。
	void ResetExports();

	// 业务侧入队主入口。见 ChunkedDataStream.h::UChunkedDataStream::EnqueuePayload。
	bool EnqueuePayload(const TSharedPtr<TArray64<uint8>>& Payload);

	// 是否还能继续发送：DataChunksPendingAck.Num() < MaxUnackedDataChunkCount。
	// 即"在途窗口"未满。
	bool CanSend() const;

	// 拉满 DataChunksPendingSend：当队列空且 SendQueue 队首条目还未切分时切之，
	// 返回是否还有数据可发。
	bool UpdateSendQueue();

	// UDataStream::BeginWrite override（被 UChunkedDataStream 转发）。
	EWriteResult BeginWrite(const FBeginWriteParameters& Params);

	// UDataStream::WriteData override：逐分片串行化进 packet 的 bitstream，
	// 直到 packet 满 / SendQueue 空 / 在途窗口满。把"本 packet 写了多少分片"用
	// 整数复用到 OutRecord 指针 bits 中（Manager 会在 ACK/Lost 时回传）。
	EWriteResult WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord);

	// UDataStream::HasAcknowledgedAllReliableData override：所有 SendQueue 条目都
	// 已弹出 ⇒ 全部 payload 都被对端 ACK 完毕，可以安全关闭流。
	bool HasAcknowledgedAllReliableData() const;

	// 沿 DataChunksPendingSend 环首批量回收"已 ACK 且前序也都 ACK"的分片
	// （严格 in-order delivery 语义，类似 TCP 累积 ACK）。
	void PopDeliveredChunks();

	// UDataStream::ProcessPacketDeliveryStatus override：把一个 packet 的最终投递
	// 结果（Lost / Delivered）应用到本 stream，依据 record 中的"分片个数"
	// 从 DataChunksPendingAck 队列头取相应数量的序号。
	void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record);

	// 把指定 SendQueueEntry 从 SendQueue 中移除（顺序查找，O(N)；N 通常很小）。
	void RemoveSendQueueEntry(FSendQueueEntry* SendQueueEntry);

	// This could be more precise by by updating CurrentBytesInSendQueue based on individual datachunks.
	// 当前发送队列字节数（粗粒度——按整条 payload+exports 累计/扣减，
	// 不细到分片粒度，注释说"未来可改进精度"）。
	uint32 GetQueuedBytes() const
	{
		return CurrentBytesInSendQueue;
	}

public:
	// -------- 状态成员 --------

	// Payload data
	// 业务侧入队但尚未完全交付的 payload 列表（顺序即入队序）。
	// 用 TUniquePtr 是为了让 FDataChunk::SrcEntry 的 TRefCountPtr 安全引用稳定地址。
	TArray<TUniquePtr<FSendQueueEntry>> SendQueue;

	// Split data chunks
	// 已切分但尚未发送 / 已发送但尚未确认的分片环（FIFO；首部为最早分片）。
	// PopDeliveredChunks 沿首部弹出累积已 ACK 的部分。
	TRingBuffer<FDataChunk> DataChunksPendingSend;

	// In-flight data chunks pending ack
	// 每个 packet 写入了哪些序号——按写入顺序入队，
	// ProcessPacketDeliveryStatus(Lost/Delivered) 时按 packet 中分片个数从队首取。
	TRingBuffer<uint16> DataChunksPendingAck;

	// Track status of entries in the DataChunksPendingSend
	// Sent[]：序号槽 → "已发送但未确认" 的 1 位状态。Lost 后清零以重发。
	uint32 Sent[(FChunkedDataStreamParameters::MaxUnackedDataChunkCount + 31)/32] = {};
	// Acked[]：序号槽 → "已收到对端 ACK"的 1 位状态。被 PopDeliveredChunks 消费后清零。
	uint32 Acked[(FChunkedDataStreamParameters::MaxUnackedDataChunkCount + 31)/32] = {};
	// 下一个要分配给新分片的序号（自增，按 11 位回绕）。
	uint16 NextSequenceNumber = 0U;
	friend class FChunkedDataStreamExportWriteScope;

	// Cached copy of DataStream init params
	// 缓存 Init 参数（含 ReplicationSystemId / ConnectionId / NetExports 指针）。
	FInitParameters InitParams;
	// 反查指针：日志/导出处理时使用。
	UReplicationSystem* ReplicationSystem = nullptr;
	FObjectReferenceCache* ObjectReferenceCache = nullptr;

	// Total number of bytes in send queue.
	// 当前 SendQueue 中所有 payload+exports 的字节总数（用于 SendBufferMaxSize 限流）。
	uint32 CurrentBytesInSendQueue = 0U;

	// Just for sanity
	// exports payload 的本地 quantize 缓冲上限：512KB。
	// 单批导出超过此值则 CreateExportPayload 返回 nullptr，整条 EnqueuePayload 拒绝入队。
	uint32 ExportsBufferMaxSize = 524288U;

	// We do not allow more paylaod bytes to be enqueued than this.
	// 发送缓冲（业务 payload+exports）总字节上限，默认 10 MB。
	// 业务侧可通过 UChunkedDataStream::SetMaxEnqueuedPayloadBytes 修改。
	uint32 SendBufferMaxSize = 10485760U;

	// Exports
	// 自上次 ResetExports 以来 PackageMap 捕获到的对象引用集合
	// （在 ExportWriteScope 内被填充；EnqueuePayload 时被 CreateExportPayload 消费）。
	UE::Net::FIrisPackageMapExports PackageMapExports;
	// 同上但针对 NetToken（字符串 token / name token / struct token 等）。
	UE::Net::FNetTokenExportContext::FNetTokenExports NetTokensPendingExport;
};

} // End of namespace(s)
