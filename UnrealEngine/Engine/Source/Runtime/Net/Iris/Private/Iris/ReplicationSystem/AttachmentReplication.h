// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：AttachmentReplication.h
// 模块：Iris / ReplicationSystem（辅助 & 周边）
// 功能：附件（Attachment）—— RPC / 子对象消息 / 大对象切片 等"非状态数据"的
//       发送 / 接收 队列基础设施。
//
// 三类附件（ENetObjectAttachmentType）：
//   * Normal      —— 关联到具体 NetObject（ObjectIndex 必填）。如普通对象 RPC、
//                   普通可靠子对象消息等。每个对象一条 SendQueue / ReceiveQueue。
//   * OutOfBand   —— 不关联具体对象（ObjectIndex 必为 0）。比如 Client→Server 的
//                   连接级 RPC、心跳等；多个 OOB 共用一条 SpecialQueue。
//   * HugeObject  —— 大对象的状态切片（HugeObject 通道）。同样 ObjectIndex=0；
//                   与 SequentialPartialNetBlobHandler 配合做按序组装。
//
// 双底层队列：
//   * UnreliableQueue (TResizableCircularQueue<FNetBlob>) —— 容量满则丢最旧；
//   * ReliableSendQueue —— 内嵌 FReliableNetBlobQueue + PreQueue（缓冲超出
//                          滑动窗口大小的待入队 blob）。
//
// 收发对称：FNetObjectAttachmentSendQueue ↔ FNetObjectAttachmentReceiveQueue，
// 二者各自对应 *Writer / *Reader 持有 (ObjectIndex → Queue) 映射。
//
// ACK / Lost 回滚：
//   * SendQueue.Serialize 返回 FCommitRecord；调用方决定是否 Commit（封包入栈）。
//   * 包送达 / 丢失时，ReplicationWriter 调用 ProcessPacketDeliveryStatus 把
//     状态透传给 ReliableQueue 完成滑窗回滚或推进；Unreliable 不需要回滚（已 Pop）。
//
// 与 NetBlobManager / PartialNetObjectAttachmentHandler 协作：
//   * Manager 入队（QueueNetObjectAttachment / SendRPC）→ Writer.Enqueue。
//   * 大于 MTU 的 blob 在 Writer 端由 Partial handler 切片为多个 PartialNetBlob。
//   * Reader 端 PartialNetObjectAttachmentHandler + FNetBlobAssembler 组装回完整 blob。
// =====================================================================================

#pragma once

#include "Net/Core/Misc/ResizableCircularQueue.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Iris/ReplicationSystem/NetBlob/ReliableNetBlobQueue.h"
#include "Containers/Map.h"

class UPartialNetObjectAttachmentHandler;
namespace UE::Net::Private
{
	class FNetBlobHandlerManager;
	class FNetObjectAttachmentsReader;
	class FNetObjectAttachmentsWriter;
}

namespace UE::Net::Private
{

// 中文：附件类别——决定使用对象级 Map 还是 SpecialQueues 索引。
enum class ENetObjectAttachmentType : uint32
{
	// For normal attachments a valid ObjectIndex is required.
	// 中文：必须有 ObjectIndex；存储于 ObjectToQueue Map。
	Normal,

	// For special attachments like OutOfBand and HugeObject the ObjectIndex is assumed to be zero.
	// 中文：与具体对象无关；ObjectIndex 必为 0；存储于 SpecialQueues[Type]。
	OutOfBand,
	HugeObject,

	// For internal use only
	// 中文：哨兵值——SpecialQueues 数组大小用。
	InternalCount
};

// 中文：Serialize 返回值——告诉调用方"为什么写到这里"。
enum class EAttachmentWriteStatus : unsigned
{
	/** At least one attachment could be sent. */
	Success,
	/** There were no attachments to write. */
	NoAttachments,
	/** Due to the reliable window size being full no reliable attachments could be sent. */
	// 中文：可靠滑动窗口已满（接收端尚未 ACK），等待 ACK 后再发。
	ReliableWindowFull,
	/** Writing caused bitstream overflow. */
	// 中文：写到一半 bitstream 溢出（封包剩余空间不够）；调用方应回滚并重排序。
	BitstreamOverflow,
};

/**
 * FNetObjectAttachmentSendQueue
 * 中文：单"对象/SpecialQueue 槽"的发送侧队列；持有：
 *   * UnreliableQueue —— 圆形队列，容量满 Pop 最旧；
 *   * ReliableQueue (lazy)  —— 含 PreQueue + FReliableNetBlobQueue 的滑窗。
 *
 * 双层 Record：
 *   * FUnreliableReplicationRecord —— 已序列化但未 Commit 的 unreliable 数量。
 *   * FReliableReplicationRecord   —— 来自 FReliableNetBlobQueue 的滑窗 record。
 * 二者合并为 FCommitRecord，封包成功后调用 CommitReplicationRecord 一次性应用。
 */
class FNetObjectAttachmentSendQueue
{
public:
	typedef FReliableNetBlobQueue::FReplicationRecord FReliableReplicationRecord;
	
	struct FUnreliableReplicationRecord
	{
		bool IsValid() const { return Record != InvalidReplicationRecord; }

		// 中文：0 表示没有需要 Pop 的 unreliable；非 0 表示要 PopNoCheck 这么多个。
		static constexpr uint32 InvalidReplicationRecord = 0;
		uint32 Record = InvalidReplicationRecord;
	};

	// Commit record contains all data to be committed after serialization is committed to, i.e. will be part of a packet intended to be sent. The ReliableReplicationRecord needs to be part of the packet replication record so that we can act on packet notifications.
	// 中文：FCommitRecord 综合两侧——可靠记录还要随包传给 ReplicationRecord，
	// 后续 ACK/Lost 时回放给 ReliableQueue。
	struct FCommitRecord
	{
		bool IsValid() const { return UnreliableCommitRecord.IsValid() || ReliableReplicationRecord.IsValid(); }

		FReliableReplicationRecord ReliableReplicationRecord;
		FUnreliableReplicationRecord UnreliableCommitRecord;
	};

public:
	FNetObjectAttachmentSendQueue();
	~FNetObjectAttachmentSendQueue();

	// 中文：入队一组 Attachment（可靠/不可靠在 Attachments[0] 的 CreationInfo.Flags 中标定）。
	// 失败原因主要是 Reliable PreQueue 容量不足（false 表示拒绝，外部需重试或丢弃）。
	bool Enqueue(TArrayView<const TRefCountPtr<FNetBlob>> Attachments);

	// 中文：包含 Reliable 队列里已可靠传输但仍未 ACK 的 unreliable 计数（FastArray-like）。
	SIZE_T GetUnreliableCount() const;

	// 中文：丢弃所有 unreliable（PreQueue + ReliableQueue 中的 unreliable + UnreliableQueue）。
	// 输出参数告知是否还有"Reliable 未发"残留——调用方据此决定是否保留队列。
	void DropUnreliable(bool &bOutHasUnsentAttachments);

	bool HasUnsent() const;

	bool HasUnsentUnreliable() const;

	// 中文：可以析构本队列吗？要求所有 unreliable 已 Pop 且 Reliable 队列处于
	// "无未确认 record"状态——否则销毁会丢掉重要 ACK 信息。
	bool IsSafeToDestroy() const;

	bool IsAllSentAndAcked() const;

	bool IsAllReliableSentAndAcked() const;

	// 中文：可靠滑窗是否还能容纳新 reliable blob——窗口满则 Enqueue 进 PreQueue 排队。
	bool CanSendMoreReliableAttachments() const;

	void SetUnreliableQueueCapacity(uint32 QueueCapacity);

private:
	friend FNetObjectAttachmentsWriter;
	class FReliableSendQueue;

	// 中文：Writer 调度的真正序列化入口；写入位流的同时填充 OutRecord。
	EAttachmentWriteStatus Serialize(FNetSerializationContext& Context, FNetRefHandle RefHandle, FCommitRecord& OutRecord, bool& bOutHasUnprocessedAttachments);
	uint32 SerializeReliable(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReliableReplicationRecord& OutRecord);
	uint32 SerializeUnreliable(FNetSerializationContext& Context, FNetRefHandle RefHandle, FUnreliableReplicationRecord& OutRecord);

	// 中文：封包成功后调用——把 record 应用到底层队列（Pop unreliable / 推进 reliable 滑窗）。
	void CommitReplicationRecord(const FCommitRecord& Record);

	// 中文：包送达/丢失/丢弃通知——只对 Reliable record 有意义。
	void ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, const FReliableReplicationRecord& Record);

	// 中文：可靠队列懒分配（仅在第一个 reliable blob 入队时构造）。
	FReliableSendQueue* ReliableQueue;
	TResizableCircularQueue<TRefCountPtr<FNetBlob>> UnreliableQueue;
	uint32 MaxUnreliableCount;
};

/**
 * FNetObjectAttachmentsWriter
 * 中文：所有 Send 队列的"路由聚合"。
 *   * Normal  → ObjectToQueue（TMap<ObjectIndex, SendQueue>）；
 *   * Special → SpecialQueues[OutOfBand|HugeObject]（TUniquePtr）。
 * 由 FNetBlobManager / FReplicationWriter 持有。
 */
class FNetObjectAttachmentsWriter
{
public:
	typedef FNetObjectAttachmentSendQueue::FCommitRecord FCommitRecord;
	typedef FNetObjectAttachmentSendQueue::FReliableReplicationRecord FReliableReplicationRecord;

public:
	bool Enqueue(ENetObjectAttachmentType Type, uint32 ObjectIndex, TArrayView<const TRefCountPtr<FNetBlob>> Attachments);

	bool HasUnsentAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	bool HasUnsentUnreliableAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	// Whether all queued attachments have been sent and that all reliable ones have been acked.
	bool IsAllSentAndAcked(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	// Whether all queued reliable attachments have been sent and acked
	bool IsAllReliableSentAndAcked(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	// Whether all queued reliable attachments have been sent and acked for all types and objects
	// 中文：用于 ReplicationWriter "graceful close" 路径——确保所有 reliable
	// 都已落地后才允许关闭连接。
	bool AreAllObjectsReliableSentAndAcked() const;

	// Whether more reliable attachments can be sent now. It's possible to queue up as many attachments as you see fit, but if the queue is full it can take a while before more attachments will be replicated.
	bool CanSendMoreReliableAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	// Whether the queue can be destroyed without causing issues if more attachments are queued to this instance.
	bool IsSafeToDestroy(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	void DropAllAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex);

	// Counts all unreliable attachments for the given type
	SIZE_T GetUnreliableAttachmentCount(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	void DropUnreliableAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex, bool& bOutHasUnsentAttachments);

	EAttachmentWriteStatus Serialize(FNetSerializationContext& Context, ENetObjectAttachmentType Type, uint32 ObjectIndex, FNetRefHandle RefHandle, FCommitRecord& OutRecord, bool& bOutHasUnsentAttachments);

	void CommitReplicationRecord(ENetObjectAttachmentType Type, uint32 ObjectIndex, const FCommitRecord& Record);

	void ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, ENetObjectAttachmentType Type, uint32 ObjectIndex, const FReliableReplicationRecord& Record);

private:
	bool NetBlobMightNeedSplitting(const TRefCountPtr<FNetObjectAttachment>& Attachment) const;

	// 中文：按 Type 路由到 ObjectToQueue 或 SpecialQueues。
	FNetObjectAttachmentSendQueue* GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex);
	const FNetObjectAttachmentSendQueue* GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	// 中文：取或创建队列；Normal 类型会校验"同时活跃对象数"上限（CVar
	// net.MaxSimultaneousObjectsWithRPCs）。
	FNetObjectAttachmentSendQueue* GetOrCreateQueue(ENetObjectAttachmentType Reason, uint32 ObjectIndex);

	TMap<uint32, FNetObjectAttachmentSendQueue> ObjectToQueue;
	TUniquePtr<FNetObjectAttachmentSendQueue> SpecialQueues[uint32(ENetObjectAttachmentType::InternalCount)];
};

// 中文：ReceiveQueue Init 入参——接收侧需要 PartialNetObjectAttachmentHandler 来识别
// PartialNetBlob 类型并组装。
struct FNetObjectAttachmentReceiveQueueInitParams
{
	const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
};

/**
 * FNetObjectAttachmentReceiveQueue
 * 中文：单"对象/SpecialQueue 槽"的接收侧队列。内部三层：
 *   1. ReliableQueue (FReliableNetBlobQueue) —— 按序号入队、按序号 Pop（保序）；
 *   2. DeferredProcessingQueues[Reliable|Unreliable] (FDeferredProcessingQueue)
 *      —— 把组装完成的 blob 推到此队列等 Reader 业务侧 Peek/Pop；
 *      —— PartialNetBlob 在此层用 FNetBlobAssembler 拼回完整 blob 才入队。
 *   3. (ReliableQueue→DeferredQueue) 之间还会把可靠路径里**夹带的 unreliable**
 *      拽出来放到 deferred 不可靠队列，避免 reliable 阻塞 unreliable。
 *
 * Pop 顺序由业务决定（一般先 Reliable 后 Unreliable）。
 */
class FNetObjectAttachmentReceiveQueue
{
public:
	FNetObjectAttachmentReceiveQueue();
	~FNetObjectAttachmentReceiveQueue();

	void Init(const FNetObjectAttachmentReceiveQueueInitParams& InitParams);

	bool IsSafeToDestroy() const;
	bool HasUnprocessed() const;

	// 中文：以 Peek/Pop 配对暴露给业务侧（例如 NetBlobManager 派发到 RPC handler）。
	const TRefCountPtr<FNetBlob>* PeekReliable();
	void PopReliable();

	const TRefCountPtr<FNetBlob>* PeekUnreliable() const;
	void PopUnreliable();

	void SetUnreliableQueueCapacity(uint32 QueueCapacity);

private:
	friend FNetObjectAttachmentsReader;
	class FDeferredProcessingQueue;

	// 中文：内部双轨——Reliable / Unreliable 各自一条 Deferred 队列。
	enum EDeferredProcessingQueue : unsigned
	{
		Unreliable,
		Reliable,
	};

	bool IsDeferredProcessingQueueEmpty(EDeferredProcessingQueue Queue) const;
	bool IsDeferredProcessingQueueSafeToDestroy(EDeferredProcessingQueue Queue) const;
	bool HasDeferredProcessingQueueUnprocessed(EDeferredProcessingQueue Queue) const;

	// 中文：判定 blob 是否是 PartialNetBlob 类型（通过 PartialNetBlobType 比对）。
	bool IsPartialNetBlob(const TRefCountPtr<FNetBlob>& Blob) const;

	// 中文：Reader 反序列化主入口；先读两个 bool 标头，再分别走 Reliable / Unreliable。
	void Deserialize(FNetSerializationContext& Context, FNetRefHandle RefHandle);
	uint32 DeserializeReliable(FNetSerializationContext& Context, FNetRefHandle RefHandle);
	uint32 DeserializeUnreliable(FNetSerializationContext& Context, FNetRefHandle RefHandle);

	FReliableNetBlobQueue* ReliableQueue = nullptr;
	FDeferredProcessingQueue* DeferredProcessingQueues[2] = {};
	const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
	uint32 MaxUnreliableCount = 0;
	// 中文：从 Handler 取到的 Partial blob type id，加速 IsPartialNetBlob 判断。
	FNetBlobType PartialNetBlobType = InvalidNetBlobType;
};

struct FNetObjectAttachmentsReaderInitParams
{
	const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
};

/**
 * FNetObjectAttachmentsReader
 * 中文：所有 Receive 队列的"路由聚合"——与 Writer 镜像。Reader 的 Deserialize
 * 由 FReplicationReader 在解析每个 batch 时调用。
 */
class FNetObjectAttachmentsReader
{
public:
	FNetObjectAttachmentsReader();
	~FNetObjectAttachmentsReader();

	void Init(const FNetObjectAttachmentsReaderInitParams& InitParams);

	bool HasUnprocessedAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	void DropAllAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex);

	void Deserialize(FNetSerializationContext& Context, ENetObjectAttachmentType Type, uint32 ObjectIndex, FNetRefHandle RefHandle);

	FNetObjectAttachmentReceiveQueue* GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex);

private:
	const FNetObjectAttachmentReceiveQueue* GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex) const;

	FNetObjectAttachmentReceiveQueue* GetOrCreateQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex);

	TMap<uint32, FNetObjectAttachmentReceiveQueue> ObjectToQueue;
	TUniquePtr<FNetObjectAttachmentReceiveQueue> SpecialQueues[uint32(ENetObjectAttachmentType::InternalCount)];
	const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
};

}
