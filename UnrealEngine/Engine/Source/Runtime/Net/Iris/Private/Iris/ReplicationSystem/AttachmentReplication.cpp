// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：AttachmentReplication.cpp
// 模块：Iris / ReplicationSystem
// 功能：FNetObjectAttachmentSendQueue / FNetObjectAttachmentsWriter /
//       FNetObjectAttachmentReceiveQueue / FNetObjectAttachmentsReader 实现。
//
// 阅读路线：
//   1. cvar block (容量上限) → 错误名常量。
//   2. SendQueue::FReliableSendQueue —— PreQueue + FReliableNetBlobQueue 的封装。
//   3. SendQueue 的 Enqueue / Serialize / Commit / ACK 流程。
//   4. AttachmentsWriter —— 按 (Type, ObjectIndex) 路由到 SendQueue。
//   5. ReceiveQueue::FDeferredProcessingQueue —— 拼装 PartialNetBlob 的临时队列。
//   6. ReceiveQueue 的 Deserialize 流程（先 Reliable，再 Unreliable）。
//   7. AttachmentsReader —— 按 (Type, ObjectIndex) 路由到 ReceiveQueue。
//
// 关键设计：
//   * Reliable PreQueue：当 FReliableNetBlobQueue 滑窗满时，新 reliable blob 不
//     直接丢，而是先放 PreQueue（容量 ReliableRPCQueueSize）；ACK 推进窗口后
//     再 PopulateQueueFromPreQueue 把 PreQueue 的 blob 灌入滑窗。
//   * Unreliable 满时丢最旧（容量 UnreliableRPCQueueSize）。
//   * Reliable 路径里夹带 unreliable —— Reader 的 DequeueUnreliable 会把它们
//     抽到 Deferred Unreliable 队列，避免被 reliable 顺序阻塞。
//   * BitstreamOverflow 处理 —— 用 FNetBitStreamRollbackScope + ExportRollback
//     回滚整个 attachment + 它附带的 NetExports。
// =====================================================================================

#include "Iris/ReplicationSystem/AttachmentReplication.h"
#include "HAL/IConsoleManager.h"
#include "Iris/Core/IrisLog.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobAssembler.h"
#include "Iris/ReplicationSystem/NetBlob/PartialNetObjectAttachmentHandler.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net::Private
{

namespace AttachmentReplicationCVars
{
	// 中文：每对象 unreliable 队列容量。满时 Pop 最旧。
	static int32 UnreliableRPCQueueSize = 10;
	FAutoConsoleVariableRef CVarUnreliableRPCQueueSize(TEXT("net.UnreliableRPCQueueSize"), UnreliableRPCQueueSize, TEXT("Maximum number of unreliable RPCs queued per object. If more RPCs are queued then older ones will be dropped."));

	// 中文：每对象 reliable PreQueue 容量。这是"溢出滑窗后的备胎"——为
	// "超大 reliable RPC 切片成多 PartialNetBlob 一次性入队"留缓冲。
	static int32 ReliableRPCQueueSize = 4096;
	FAutoConsoleVariableRef CVarReliableRPCQueueSize(TEXT("net.ReliableRPCQueueSize"), ReliableRPCQueueSize, TEXT("Maximum number of reliable RPCs queued per object. This is in addition to the send window size. This is to support very large RPCs that are split into smaller pieces."));

	// 中文：Client→Server 方向 OutOfBand unreliable 队列上限——客户端发往服务器
	// 的连接级 RPC 容量更小，避免恶意客户端挤爆服务器。
	static int32 ClientToServerUnreliableRPCQueueSize = 16;
	FAutoConsoleVariableRef CVarClientToServerUnreliableRPCQueueSize(TEXT("net.ClientToServerUnreliableRPCQueueSize"), ClientToServerUnreliableRPCQueueSize, TEXT( "Maximum number of unreliable RPCs queued for sending from the client to the server. If more RPCs are queued then older ones will be dropped."));

	// 中文：同时拥有未发 RPC 的对象数上限——保护 ObjectToQueue Map 不被
	// "海量短命对象的 RPC"打爆内存。
	static int32 MaxSimultaneousObjectsWithRPCs = 4096;
	FAutoConsoleVariableRef CVarMaxSimultaneousObjectsWithRPCs(TEXT("net.MaxSimultaneousObjectsWithRPCs"), MaxSimultaneousObjectsWithRPCs, TEXT("Maximum number of objects that can have unsent RPCs at the same time. "));
}

// 中文：网络错误名（FName）—— 上层根据它分类处理（断连 / 丢包 / 警告）。
static const FName NetError_UnreliableQueueFull("Unreliable attachment queue full");
static const FName NetError_UnsupportedNetBlob("Unsupported NetBlob type");
static const FName NetError_TooManyObjectsWithFunctionCalls("Too many objects being targeted by RPCs at the same time.");

// SendQueue and Writer
// ------------------------------------------------------------------------------------
// FReliableSendQueue
// 中文：FNetObjectAttachmentSendQueue 私有内部类——把 PreQueue 与
// FReliableNetBlobQueue（真正的滑窗）粘合：
//   * Enqueue：先尝试塞滑窗，剩下放 PreQueue；
//   * Serialize：从滑窗序列化（ReliableNetBlobQueue 内部维护序号 / 滑窗）；
//   * ProcessPacketDeliveryStatus.Delivered：滑窗推进后把 PreQueue 的 blob 灌进。
// ------------------------------------------------------------------------------------
class FNetObjectAttachmentSendQueue::FReliableSendQueue
{
public:
	FReliableSendQueue()
	: MaxPreQueueCount(AttachmentReplicationCVars::ReliableRPCQueueSize)
	{
	}

	bool HasUnsentBlobs() const
	{
		return PreQueue.Num() > 0 || ReliableQueue.HasUnsentBlobs();
	}

	bool CanSendBlobs() const
	{
		// 中文：CanSendBlobs 只看真正的滑窗——PreQueue 里的需要等滑窗推进才能发。
		return ReliableQueue.HasUnsentBlobs();
	}

	bool IsSafeToDestroy() const
	{
		return ReliableQueue.IsSafeToDestroy();
	}

	bool IsAllSentAndAcked() const
	{
		return PreQueue.Num() == 0 && ReliableQueue.IsAllSentAndAcked();
	}

	bool IsSendWindowFull() const
	{
		return ReliableQueue.IsSendWindowFull();
	}

	uint32 GetUnsentBlobCount() const
	{
		return ReliableQueue.GetUnsentBlobCount() + static_cast<uint32>(PreQueue.Num());
	}

	bool Enqueue(TArrayView<const TRefCountPtr<FNetBlob>> Attachments)
	{
		// If we have blobs in the pre queue then continue adding to that.
		// 中文：保序——一旦 PreQueue 有积压，新 blob 必须接着 PreQueue 走，不能跳过。
		const uint32 PreQueueCount = static_cast<uint32>(PreQueue.Num());
		const uint32 AttachmentCount = static_cast<uint32>(Attachments.Num());

		// Assume that we won't fit all the attachments unless they can fit in the pre-queue.
		// 中文：上限检查——按"全塞 PreQueue"最坏情况判断；不够就整组拒绝（保留原子性）。
		if (AttachmentCount + PreQueueCount > MaxPreQueueCount)
		{
			return false;
		}

		uint32 QueuedCount = 0;
		if (PreQueueCount == 0)
		{
			// 中文：没有积压时，先尝试直接进滑窗，直到滑窗满。
			for (const TRefCountPtr<FNetBlob>& Attachment : Attachments)
			{
				if (!ReliableQueue.Enqueue(Attachment))
				{
					break;
				}

				++QueuedCount;
			}
		}

		// 中文：滑窗未消化的部分进入 PreQueue 等待。
		const uint32 AddToPreQueueCount = AttachmentCount - QueuedCount;
		if (AddToPreQueueCount > 0)
		{
			PreQueue.Append(Attachments.GetData() + QueuedCount, AddToPreQueueCount);
		}

		return true;
	}

	uint32 Serialize(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReliableNetBlobQueue::FReplicationRecord& OutRecord)
	{
		// 中文：根据是否绑定具体对象选择 SerializeWithObject / Serialize（影响 NetBlob 内部序列化策略）。
		if (RefHandle.IsValid())
		{
			return ReliableQueue.SerializeWithObject(Context, RefHandle, OutRecord);
		}
		else
		{
			return ReliableQueue.Serialize(Context, OutRecord);
		}
	}

	void ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, const FReliableNetBlobQueue::FReplicationRecord& Record)
	{
		ReliableQueue.ProcessPacketDeliveryStatus(Status, Record);
		// 中文：成功送达且本次 Record 有效——滑窗向前推进，把 PreQueue 余量灌入腾出的位置。
		if (Status == EPacketDeliveryStatus::Delivered && Record.IsValid())
		{
			PopulateQueueFromPreQueue();
		}
	}

	void CommitReplicationRecord(const FReliableNetBlobQueue::FReplicationRecord& Record)
	{
		ReliableQueue.CommitReplicationRecord(Record);
	}

	SIZE_T GetUnreliableCount() const
	{
		SIZE_T UnreliableCount = 0;

		// Count all unreliable attachments in PreQueue and ReliableQueue
		// 中文：FReliableNetBlobQueue 也允许 unreliable blob "搭车" reliable 通道
		// 占住序号；这里把 PreQueue + ReliableQueue 中的不可靠条目都计入。
		for (const TRefCountPtr<FNetBlob>& BlobRef : PreQueue)
		{
			const FNetBlob* Blob = BlobRef.GetReference();
			UnreliableCount += Blob && !Blob->IsReliable() ? 1 : 0;
		}
	
		UnreliableCount += ReliableQueue.GetUnreliableCount();

		return UnreliableCount;
	}

	void DropUnreliable()
	{
		// Drop all unreliable from pre-queue
		// 中文：PreQueue 中扔掉所有不可靠（含 nullptr 防护）。
		PreQueue.RemoveAll([](const TRefCountPtr<FNetBlob>& BlobRef)
		{
			const FNetBlob* Blob = BlobRef.GetReference();
			return Blob == nullptr || !Blob->IsReliable();
		});

		ReliableQueue.DropUnreliable();
	}

private:
	void PopulateQueueFromPreQueue()
	{
		const uint32 AttachmentCount = static_cast<uint32>(PreQueue.Num());
		if (AttachmentCount == 0)
		{
			return;
		}

		// Similar to enqueueing, but without all the checks as we've already passed them.
		// 中文：和 Enqueue 流程相似，但容量校验已在最初 Enqueue 时做过——这里仅尽力灌入。
		TArray<TRefCountPtr<FNetBlob>> PrevPreQueue = MoveTemp(PreQueue);
		TArrayView<const TRefCountPtr<FNetBlob>> Attachments = MakeArrayView(PrevPreQueue);

		uint32 QueuedCount = 0;
		for (const TRefCountPtr<FNetBlob>& Attachment : Attachments)
		{
			if (!ReliableQueue.Enqueue(Attachment))
			{
				break;
			}

			++QueuedCount;
		}

		const uint32 AddToPreQueueCount = AttachmentCount - QueuedCount;
		if (AddToPreQueueCount > 0)
		{
			PreQueue.Append(Attachments.GetData() + QueuedCount, AddToPreQueueCount);
		}
	}

	// 中文：真正的可靠滑窗（按序号排序、有序 ACK / 重传）。
	FReliableNetBlobQueue ReliableQueue;
	// 中文：PreQueue —— 滑窗外的等待区，保证巨型 reliable 序列也能整体入队。
	TArray<TRefCountPtr<FNetBlob>> PreQueue;
	const uint32 MaxPreQueueCount;
};

FNetObjectAttachmentSendQueue::FNetObjectAttachmentSendQueue()
: ReliableQueue(nullptr)
, MaxUnreliableCount(AttachmentReplicationCVars::UnreliableRPCQueueSize)
{
}

FNetObjectAttachmentSendQueue::~FNetObjectAttachmentSendQueue()
{
	delete ReliableQueue;
}

bool FNetObjectAttachmentSendQueue::Enqueue(TArrayView<const TRefCountPtr<FNetBlob>> Attachments)
{
	// 中文：以批组 [0] 的 CreationInfo.Flags 决定整批走 reliable 还是 unreliable。
	// 这里假定上层（Manager）已经按可靠性分组传入。
	const FNetBlobCreationInfo& CreationInfo = Attachments[0]->GetCreationInfo();
	if (EnumHasAnyFlags(CreationInfo.Flags, ENetBlobFlags::Reliable | ENetBlobFlags::Ordered))
	{
		// 中文：懒分配 Reliable 队列——多数对象其实没有 reliable RPC。
		if (ReliableQueue == nullptr)
		{
			ReliableQueue = new FReliableSendQueue();
		}

		return ReliableQueue->Enqueue(Attachments);
	}
	else
	{
		// 中文：unreliable 满则丢最旧（FastArray-like 行为）。
		const SIZE_T TotalCountNeeded = UnreliableQueue.Count() + Attachments.Num();
		if (TotalCountNeeded > MaxUnreliableCount)
		{
			UE_LOG(LogIris, Verbose, TEXT("Dropping old RPCs due to too many unreliable attachments: %" SIZE_T_FMT " max: %u"), UnreliableQueue.Count(), MaxUnreliableCount);
			const SIZE_T PopCount = FPlatformMath::Min(TotalCountNeeded - MaxUnreliableCount, UnreliableQueue.Count());
			UnreliableQueue.Pop(PopCount);
		}

		for (const TRefCountPtr<FNetBlob>& Attachment : Attachments)
		{
			UnreliableQueue.Enqueue(Attachment);
		}
	}

	return true;
}

SIZE_T FNetObjectAttachmentSendQueue::GetUnreliableCount() const
{
	SIZE_T UnreliableCount = 0;

	UnreliableCount += UnreliableQueue.Count();
	if (ReliableQueue)
	{
		UnreliableCount += ReliableQueue->GetUnreliableCount();
	}

	return UnreliableCount;
}

void FNetObjectAttachmentSendQueue::DropUnreliable(bool& bOutHasUnsent)
{
	UnreliableQueue.Empty();
	if (ReliableQueue)
	{
		ReliableQueue->DropUnreliable();
		bOutHasUnsent = ReliableQueue->HasUnsentBlobs();
	}
	else
	{
		bOutHasUnsent = false;
	}
}

bool FNetObjectAttachmentSendQueue::HasUnsent() const
{
	return !UnreliableQueue.IsEmpty() || (ReliableQueue != nullptr && ReliableQueue->HasUnsentBlobs());
}

bool FNetObjectAttachmentSendQueue::HasUnsentUnreliable() const
{
	return !UnreliableQueue.IsEmpty();
}

bool FNetObjectAttachmentSendQueue::IsAllSentAndAcked() const
{
	return UnreliableQueue.IsEmpty() && (ReliableQueue == nullptr || ReliableQueue->IsAllSentAndAcked());
}

bool FNetObjectAttachmentSendQueue::IsAllReliableSentAndAcked() const
{
	return ReliableQueue == nullptr || ReliableQueue->IsAllSentAndAcked();
}

bool FNetObjectAttachmentSendQueue::CanSendMoreReliableAttachments() const
{
	return ReliableQueue == nullptr || !ReliableQueue->IsSendWindowFull();
}

bool FNetObjectAttachmentSendQueue::IsSafeToDestroy() const
{
	return UnreliableQueue.IsEmpty() && (ReliableQueue == nullptr || ReliableQueue->IsSafeToDestroy());
}

void FNetObjectAttachmentSendQueue::SetUnreliableQueueCapacity(uint32 QueueCapacity)
{
	// MaxUnreliableCount is what prevents the queue from growing too large.
	MaxUnreliableCount = QueueCapacity;
	
	const SIZE_T UnreliableCount = UnreliableQueue.Count();
	if (QueueCapacity >= UnreliableCount)
	{
		return;
	}

	const SIZE_T DropCount = UnreliableCount - QueueCapacity;
	UE_LOG(LogIris, Warning, TEXT("Dropping %" SIZE_T_FMT " attachments due to change in unreliable queue capacity to %u"), DropCount, QueueCapacity);
	UnreliableQueue.Pop(DropCount);
}

EAttachmentWriteStatus FNetObjectAttachmentSendQueue::Serialize(FNetSerializationContext& Context, FNetRefHandle RefHandle, FNetObjectAttachmentSendQueue::FCommitRecord& OutRecord, bool& bOutHasUnsentAttachments)
{
	// 中文：本函数把队列里的可发 attachment 写入 BitStream，不立刻消费——
	// 只填 OutRecord，由 ReplicationWriter 决定 Commit 与否（封包成功才 Commit）。
	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();

	FCommitRecord ReplicationRecord;
	// This count is the total number of unsent reliable blobs. If the reliable window is full no blobs can be sentuntil some have been acked.
	// 中文：滑窗满则 UnsentReliableCount > 0 但 CanSendBlobs() 为 false——这是
	// "想发但发不了"的状态，要返回 ReliableWindowFull 通知调用方。
	const uint32 UnsentReliableCount = (ReliableQueue != nullptr ? ReliableQueue->GetUnsentBlobCount() : 0U);
	const bool bCanSendReliableAttachments = UnsentReliableCount > 0 && ReliableQueue->CanSendBlobs();
	const bool bHasUnreliableAttachments = !UnreliableQueue.IsEmpty();
	if (!(bCanSendReliableAttachments || bHasUnreliableAttachments))
	{
		// Ideally we shouldn't get here, but we can handle it. Important to overflow to report a soft error.
		// 中文：理想情况下上层调度器不会让我们进入"无可发"分支；万一进了，
		// 故意 DoOverflow 让外层走"软错误"路径，避免污染 BitStream。
		Writer.DoOverflow();
		OutRecord = ReplicationRecord;
		bOutHasUnsentAttachments = false;

		if (UnsentReliableCount > 0)
		{
			// We want to send reliable blobs but the window is full.
			return EAttachmentWriteStatus::ReliableWindowFull;
		}
		else
		{
			return EAttachmentWriteStatus::NoAttachments;
		}
	}

	UE_NET_TRACE_SCOPE(RPCs, Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	// 中文：写入两个 1-bit 头标记——"是否含可靠"与"是否含不可靠"。
	// 不可靠位写入位置 HasUnreliableAttachmentsWritePos 缓存，以便后续如果
	// 不可靠"想写但写不下"时回填 false（回滚）。
	Writer.WriteBool(bCanSendReliableAttachments);
	const uint32 HasUnreliableAttachmentsWritePos = Writer.GetPosBits();
	Writer.WriteBool(bHasUnreliableAttachments);

	if (Writer.IsOverflown())
	{
		OutRecord = ReplicationRecord;
		bOutHasUnsentAttachments = true;
		return EAttachmentWriteStatus::BitstreamOverflow;
	}

	uint32 SerializedReliableCount = 0;
	if (bCanSendReliableAttachments)
	{
		UE_NET_TRACE_SCOPE(Ordered, Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		SerializedReliableCount = SerializeReliable(Context, RefHandle, ReplicationRecord.ReliableReplicationRecord);
		// If we couldn't fit any reliable attachments then don't even try unreliable
		// 中文：可靠都没写下 → bitstream 过紧；故意 overflow 让外层重排序对象。
		if (SerializedReliableCount == 0)
		{
			Writer.DoOverflow();
			OutRecord = ReplicationRecord;
			bOutHasUnsentAttachments = true;
			return EAttachmentWriteStatus::BitstreamOverflow;
		}
	}

	// Assume success and change when necessary.
	EAttachmentWriteStatus WriteStatus = EAttachmentWriteStatus::Success;

	uint32 SerializedUnreliableCount = 0;
	if (bHasUnreliableAttachments)
	{
		UE_NET_TRACE_SCOPE(Unreliable, Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		SerializedUnreliableCount = SerializeUnreliable(Context, RefHandle, ReplicationRecord.UnreliableCommitRecord);
		if (SerializedUnreliableCount == 0)
		{
			// If we didn't manage to send anything then inform the caller of this via overflowing the bitstream
			if (SerializedReliableCount == 0)
			{
				Writer.DoOverflow();
				// Even if we couldn't send reliable attachments due to the window being full we still probably wouldn't have fit any, so return the overflow status.
				WriteStatus = EAttachmentWriteStatus::BitstreamOverflow;
			}
			// Patch up information previously saying there were unreliable attachments because we couldn't fit any in the bitstream
			// 中文：之前已 WriteBool(true) 表示有 unreliable，但实际一个都没写下，
			// 现在用 NetBitStreamWriteScope 跳到该位置改写为 false，让接收端不要尝试读取。
			else
			{
				FNetBitStreamWriteScope WriteScope(Writer, HasUnreliableAttachmentsWritePos);
				Writer.WriteBool(false);
			}
		}
	}

	if (WriteStatus == EAttachmentWriteStatus::Success)
	{
		// Override WriteStatus with ReliableWindowFull in case we fit one or more unreliable attachments but weren't able to write any reliable ones.
		// 中文：unreliable 顺利发了但 reliable 因窗口满没发——明确返回 ReliableWindowFull
		// 让上层有线索（避免一直等 unreliable 占带宽却没推进 reliable 滑窗）。
		if (UnsentReliableCount > 0 && !bCanSendReliableAttachments)
		{
			WriteStatus = EAttachmentWriteStatus::ReliableWindowFull;
		}
	}

	OutRecord = ReplicationRecord;
	// 中文：仍有未发？取决于"序列化数量 < 队列总数"。
	bOutHasUnsentAttachments = (SerializedReliableCount < UnsentReliableCount) || (SerializedUnreliableCount < UnreliableQueue.Count());
	return WriteStatus;
}

uint32 FNetObjectAttachmentSendQueue::SerializeReliable(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReliableNetBlobQueue::FReplicationRecord& OutRecord)
{
	return ReliableQueue->Serialize(Context, RefHandle, OutRecord);
}

uint32 FNetObjectAttachmentSendQueue::SerializeUnreliable(FNetSerializationContext& Context, FNetRefHandle RefHandle, FUnreliableReplicationRecord& OutRecord)
{
	// 中文：循环序列化 UnreliableQueue 中的 blob，每个 blob 后写一个"还有更多"位。
	// 任何 blob 写到一半溢出 → RollbackScope 回滚整 blob + 已附带的 NetExports，
	// 同时把上一个 blob 的"hasMore"位改为 false（链尾）。
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	const FObjectReferenceCache* ObjectReferenceCache = Context.GetInternalContext()->ObjectReferenceCache;

	uint32 SerializedUnreliableCount = 0;
	uint32 PrevHasMoreAttachmentsWritePos = 0;
	const bool bSerializeWithObject = RefHandle.IsValid();
	for (SIZE_T AttachmentIt = 0, AttachmentEndIt = UnreliableQueue.Count(); AttachmentIt != AttachmentEndIt; ++AttachmentIt)
	{
		// 中文：双重 RAII 回滚——BitStreamRollbackScope 保护位流；
		// NetExportRollbackScope 保护本 blob 触发的 NetExports / NetTokens。
		FNetBitStreamRollbackScope RollbackScope(*Writer);
		FNetExportRollbackScope ExportScope(Context);

		const TRefCountPtr<FNetBlob>& Attachment = UnreliableQueue.PeekAtOffsetNoCheck(AttachmentIt);

		// If we have exports, append them, if attachment is rolled back we will roll back any appended exports as well.
		// 中文：如果 blob 自带 ObjectReference / NetToken 导出，先 Append 到 ExportContext。
		// 一旦后续溢出 → ExportScope.Rollback 把这次 Append 也撤回。
		FNetExportContext* ExportContext = Attachment->HasExports() ? Context.GetExportContext() : nullptr;
		if (ExportContext)
		{
			ObjectReferenceCache->AddPendingExports(Context, Attachment->CallGetNetObjectReferenceExports());
			ExportContext->AddPendingExports(Attachment->CallGetNetTokenExports());
		}

		// 中文：先写 CreationInfo（携带 BlobType + Reliable/Ordered flag），然后写 payload。
		Attachment->SerializeCreationInfo(Context, Attachment->GetCreationInfo());
		if (bSerializeWithObject)
		{
			Attachment->SerializeWithObject(Context, RefHandle);
		}
		else
		{
			Attachment->Serialize(Context);
		}

		// 中文：写"还有更多"位（用于流式停止）。位置缓存到 PrevHasMoreAttachmentsWritePos
		// 以便溢出时回填 false。
		const uint32 HasMoreAttachmentsWritePos = Writer->GetPosBits();
		const bool bHasMoreAttachments = AttachmentIt + 1 != AttachmentEndIt;
		Writer->WriteBool(bHasMoreAttachments);

		if (Writer->IsOverflown())
		{
			// Rollback exports
			ExportScope.Rollback();

			// We need to rollback before we update the continuation bit
			// 中文：必须先回滚位流再改写"上一 blob 的 hasMore"——否则会写到刚被 rollback 释放的位。
			RollbackScope.Rollback();
			if (SerializedUnreliableCount > 0)
			{
				FNetBitStreamWriteScope WriteScope(*Writer, PrevHasMoreAttachmentsWritePos);
				Writer->WriteBool(false);
			}

			break;
		}
		else
		{
			++SerializedUnreliableCount;
			PrevHasMoreAttachmentsWritePos = HasMoreAttachmentsWritePos;
		}
	}
	FUnreliableReplicationRecord ReplicationRecord;
	// 中文：Record 仅记录"成功序列化的数量"——Commit 时按这个数 PopNoCheck。
	ReplicationRecord.Record = SerializedUnreliableCount;
	OutRecord = ReplicationRecord;
	return SerializedUnreliableCount;
}

void FNetObjectAttachmentSendQueue::CommitReplicationRecord(const FNetObjectAttachmentSendQueue::FCommitRecord& Record)
{
	// 中文：封包成功后调用——按 Record 真正消费队列：
	//   * Unreliable：从 UnreliableQueue 头部 Pop N 个；
	//   * Reliable  ：让 ReliableQueue 推进滑窗记账。
	if (Record.UnreliableCommitRecord.IsValid())
	{
		UnreliableQueue.PopNoCheck(Record.UnreliableCommitRecord.Record);
	}
	if (Record.ReliableReplicationRecord.IsValid())
	{
		ReliableQueue->CommitReplicationRecord(Record.ReliableReplicationRecord);
	}
}

void FNetObjectAttachmentSendQueue::ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, const FNetObjectAttachmentSendQueue::FReliableReplicationRecord& Record)
{
	// 中文：包送达/丢失时调用。Unreliable 已在 Commit 阶段 Pop，无需回滚——
	// 这里只把状态透传给 Reliable 滑窗（推进 / 重传 / 丢弃）。
	if (ReliableQueue == nullptr)
	{
		return;
	}

	ReliableQueue->ProcessPacketDeliveryStatus(Status, Record);
}

bool FNetObjectAttachmentsWriter::Enqueue(ENetObjectAttachmentType Type, uint32 ObjectIndex, TArrayView<const TRefCountPtr<FNetBlob>> Attachments)
{
	FNetObjectAttachmentSendQueue* Queue = GetOrCreateQueue(Type, ObjectIndex);
	if (!ensureMsgf(Queue != nullptr, TEXT("Too many objects being targeted by Attachments simultaneously: %d"), ObjectToQueue.Num()))
	{
		return false;
	}

	return Queue->Enqueue(Attachments);
}

bool FNetObjectAttachmentsWriter::HasUnsentAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return false;
	}

	return Queue->HasUnsent();
}

bool FNetObjectAttachmentsWriter::HasUnsentUnreliableAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return false;
	}

	return Queue->HasUnsentUnreliable();
}

bool FNetObjectAttachmentsWriter::IsAllSentAndAcked(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return true;
	}

	return Queue->IsAllSentAndAcked();
}

bool FNetObjectAttachmentsWriter::IsAllReliableSentAndAcked(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return true;
	}

	return Queue->IsAllSentAndAcked();
}

bool FNetObjectAttachmentsWriter::AreAllObjectsReliableSentAndAcked() const
{
	for (const auto& ObjectQueuePair : ObjectToQueue)
	{
		if (!ObjectQueuePair.Value.IsAllReliableSentAndAcked())
		{
			return false;
		}
	}

	for (const TUniquePtr<FNetObjectAttachmentSendQueue>& SpecialQueue : SpecialQueues)
	{
		if (SpecialQueue.IsValid() && !SpecialQueue->IsAllReliableSentAndAcked())
		{
			return false;
		}
	}

	return true;
}

bool FNetObjectAttachmentsWriter::CanSendMoreReliableAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return true;
	}

	return Queue->CanSendMoreReliableAttachments();
}

bool FNetObjectAttachmentsWriter::IsSafeToDestroy(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return true;
	}

	return Queue->IsSafeToDestroy();
}

void FNetObjectAttachmentsWriter::DropAllAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex)
{
	ObjectToQueue.Remove(ObjectIndex);
}

SIZE_T FNetObjectAttachmentsWriter::GetUnreliableAttachmentCount(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return 0;
	}

	return Queue->GetUnreliableCount();
}

void FNetObjectAttachmentsWriter::DropUnreliableAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex, bool &bOutHasUnsentAttachments)
{
	FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		bOutHasUnsentAttachments = false;
		return;
	}

	Queue->DropUnreliable(bOutHasUnsentAttachments);
	if (!bOutHasUnsentAttachments && Queue->IsSafeToDestroy())
	{
		ObjectToQueue.Remove(ObjectIndex);
	}
}

EAttachmentWriteStatus FNetObjectAttachmentsWriter::Serialize(FNetSerializationContext& Context, ENetObjectAttachmentType Type, uint32 ObjectIndex, const FNetRefHandle RefHandle,  FNetObjectAttachmentsWriter::FCommitRecord& OutRecord, bool& bOutHasUnsentAttachments)
{
	FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	// If this ensure fires we have bad logic for keeping track of whether there are attachments or not
	if (ensure(Queue != nullptr))
	{
		return Queue->Serialize(Context, RefHandle, OutRecord, bOutHasUnsentAttachments);
	}
	else
	{
		OutRecord = FCommitRecord();
		bOutHasUnsentAttachments = false;
		return EAttachmentWriteStatus::NoAttachments;
	}
}

void FNetObjectAttachmentsWriter::CommitReplicationRecord(ENetObjectAttachmentType Type, uint32 ObjectIndex, const FNetObjectAttachmentsWriter::FCommitRecord& Record)
{
	FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	Queue->CommitReplicationRecord(Record);
}

void FNetObjectAttachmentsWriter::ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, ENetObjectAttachmentType Type, uint32 ObjectIndex, const FReliableReplicationRecord& Record)
{
	FNetObjectAttachmentSendQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return;
	}

	Queue->ProcessPacketDeliveryStatus(Status, Record);
}

FNetObjectAttachmentSendQueue* FNetObjectAttachmentsWriter::GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex)
{
	return Type == ENetObjectAttachmentType::Normal ? ObjectToQueue.Find(ObjectIndex) : SpecialQueues[uint32(Type)].Get();
}

const FNetObjectAttachmentSendQueue* FNetObjectAttachmentsWriter::GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	return Type == ENetObjectAttachmentType::Normal ? ObjectToQueue.Find(ObjectIndex) : SpecialQueues[uint32(Type)].Get();
}

FNetObjectAttachmentSendQueue* FNetObjectAttachmentsWriter::GetOrCreateQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex)
{
	if (Type != ENetObjectAttachmentType::Normal)
	{
		// 中文：OOB / Huge —— 单例 SpecialQueues[Type]；ObjectIndex 必为 0。
		checkSlow(ObjectIndex == 0U);
		TUniquePtr<FNetObjectAttachmentSendQueue>& QueuePtr = SpecialQueues[uint32(Type)];
		if (FNetObjectAttachmentSendQueue* Queue = QueuePtr.Get())
		{
			return Queue;
		}

		QueuePtr = MakeUnique<FNetObjectAttachmentSendQueue>();

		// 中文：OOB 队列对 Client→Server 方向使用更小的 unreliable 容量上限。
		if (Type == ENetObjectAttachmentType::OutOfBand)
		{
			QueuePtr->SetUnreliableQueueCapacity(AttachmentReplicationCVars::ClientToServerUnreliableRPCQueueSize);
		}

		return QueuePtr.Get();
	}
	else
	{
		// 中文：Normal —— 按 ObjectIndex 映射。Map 已满则返回 null（拒绝入队），
		// Enqueue 上层会以 NetError_TooManyObjectsWithFunctionCalls 或 false 反馈。
		FNetObjectAttachmentSendQueue* Queue = ObjectToQueue.Find(ObjectIndex);
		if (Queue != nullptr)
		{
			return Queue;
		}

		if (ObjectToQueue.Num() >= AttachmentReplicationCVars::MaxSimultaneousObjectsWithRPCs)
		{
			return nullptr;
		}

		Queue = &ObjectToQueue.Add(ObjectIndex);

		return Queue;
	}
}

// ReceiveQueue and Reader
// ------------------------------------------------------------------------------------
// FDeferredProcessingQueue（FNetObjectAttachmentReceiveQueue 私有内部类）
// 中文：接收侧把"反序列化完毕（含 PartialNetBlob 已组装完整）"的 blob
// 暂存于此，等待业务侧 Peek/Pop 处理。两个特性：
//   * 内部各自持有 Reliable / Unreliable 两个 NetBlobAssembler；
//   * bOnlyProcessUnreliable=true 时，收到 reliable blob → SetError 拒绝（防止
//     不可靠通道伪造可靠消息）。
// ------------------------------------------------------------------------------------
class FNetObjectAttachmentReceiveQueue::FDeferredProcessingQueue
{
public:
	struct FInitParams
	{
		const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
		bool bOnlyProcessUnreliable = false;
	};

	void Init(const FInitParams& InitParams)
	{
		PartialNetObjectAttachmentHandler = InitParams.PartialNetObjectAttachmentHandler;
		bOnlyProcessUnreliable = InitParams.bOnlyProcessUnreliable;
	}

	bool IsEmpty() const { return Queue.IsEmpty(); }
	bool HasUnprocessed() const { return !Queue.IsEmpty(); }
	bool IsSafeToDestroy() const
	{ 
		return Queue.IsEmpty() && !ReliableNetBlobAssembler.IsValid() && !UnreliableNetBlobAssembler.IsValid();
	}

	void Enqueue(FNetSerializationContext& Context, FNetRefHandle RefHandle, const TRefCountPtr<FNetBlob>& NetBlob, bool bIsPartialNetBlob)
	{
		const bool bIsBlobReliable = NetBlob->IsReliable();
		// 中文：unreliable 通道不允许收到 reliable blob——是协议错误，断开。
		if (bIsBlobReliable && bOnlyProcessUnreliable)
		{
			UE_LOG(LogIris, Error, TEXT("Received reliable blob when only unreliable is supported."));
			Context.SetError(GNetError_UnsupportedNetBlob);
			return;
		}

		if (bIsPartialNetBlob)
		{
			// 中文：PartialNetBlob —— 走 NetBlobAssembler 拼装。Reliable / Unreliable
			// 各自一份 Assembler（不可混用，因为序号空间不同）。
			TUniquePtr<FNetBlobAssembler>& NetBlobAssembler = bIsBlobReliable ? ReliableNetBlobAssembler : UnreliableNetBlobAssembler;
			if (!NetBlobAssembler.IsValid())
			{
				FNetBlobAssemblerInitParams InitParams;
				InitParams.PartialNetBlobHandlerConfig = PartialNetObjectAttachmentHandler ? PartialNetObjectAttachmentHandler->GetConfig() : nullptr;
				NetBlobAssembler = MakeUnique<FNetBlobAssembler>();
				NetBlobAssembler->Init(InitParams);
			}

			NetBlobAssembler->AddPartialNetBlob(Context, RefHandle, reinterpret_cast<const TRefCountPtr<FPartialNetBlob>&>(NetBlob));
			// 中文：组装完成 → Assemble 得到完整 blob 并入队；
			// 序列断裂（丢中间 part）→ Reset Assembler，等下一序列。
			if (NetBlobAssembler->IsReadyToAssemble() || NetBlobAssembler->IsSequenceBroken())
			{
				if (NetBlobAssembler->IsReadyToAssemble())
				{
					const TRefCountPtr<FNetBlob>& AssembledBlob = NetBlobAssembler->Assemble(Context);
					if (AssembledBlob.IsValid())
					{
						Queue.Enqueue(AssembledBlob);
					}
				}
				NetBlobAssembler.Reset();
			}
		}
		else
		{
			// If we're set to only process unreliable blobs and we're not done assembling a full set of unreliable partial blobs at this point we know we will not succeed in doing so. We want to allow the queue to be destroyed as soon as possible so let's reset the assembler.
			// 中文：unreliable 通道里出现"非 partial"的完整 blob，意味着前一组 partial
			// 已不可能再补齐——丢 Assembler 释放内存。
			if (bOnlyProcessUnreliable)
			{
				UnreliableNetBlobAssembler.Reset();
			}

			Queue.Enqueue(NetBlob);
		}
	}

	const TRefCountPtr<FNetBlob>* Peek()
	{
		for (SIZE_T It = 0, EndIt = Queue.Count(); It < EndIt; ++It)
		{
			// Peek at head. We will return or pop the entry.
			TRefCountPtr<FNetBlob>& Blob = Queue.Poke();
			if (Blob.GetRefCount() > 0)
			{
				return &Blob;
			}
			else
			{
				Queue.Pop();
			}
		}

		return nullptr;
	}

	void Pop()
	{
		return Queue.Pop();
	}

	SIZE_T GetQueueCount() const
	{
		return Queue.Count();
	}

	void SetQueueCapacity(uint32 QueueCapacity)
	{
		const SIZE_T QueueCount = Queue.Count();
		const SIZE_T DropCount = QueueCount - QueueCapacity;
		UE_LOG(LogIris, Warning, TEXT("Dropping %" SIZE_T_FMT " attachments to due to change in unreliable queue capacity to %u"), DropCount, QueueCapacity);
		Queue.Pop(DropCount);
	}

private:
	TUniquePtr<FNetBlobAssembler> ReliableNetBlobAssembler;
	TUniquePtr<FNetBlobAssembler> UnreliableNetBlobAssembler;
	TResizableCircularQueue<TRefCountPtr<FNetBlob>> Queue;
	const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
	/* Whether this queue is only expecting unrealiable NetBlobs or not. */
	bool bOnlyProcessUnreliable = false;
};

FNetObjectAttachmentReceiveQueue::FNetObjectAttachmentReceiveQueue()
: MaxUnreliableCount(AttachmentReplicationCVars::UnreliableRPCQueueSize)
{
}

FNetObjectAttachmentReceiveQueue::~FNetObjectAttachmentReceiveQueue()
{
	delete ReliableQueue;
	for (FDeferredProcessingQueue* Queue : MakeArrayView(DeferredProcessingQueues))
	{
		delete Queue;
	}
}

void FNetObjectAttachmentReceiveQueue::Init(const FNetObjectAttachmentReceiveQueueInitParams& InitParams)
{
	PartialNetObjectAttachmentHandler = InitParams.PartialNetObjectAttachmentHandler;
	PartialNetBlobType = (InitParams.PartialNetObjectAttachmentHandler ? InitParams.PartialNetObjectAttachmentHandler->GetNetBlobType() : InvalidNetBlobType);
}

bool FNetObjectAttachmentReceiveQueue::IsSafeToDestroy() const
{
	return IsDeferredProcessingQueueSafeToDestroy(EDeferredProcessingQueue::Unreliable) && IsDeferredProcessingQueueSafeToDestroy(EDeferredProcessingQueue::Reliable) && (ReliableQueue == nullptr || ReliableQueue->IsSafeToDestroy());
}

bool FNetObjectAttachmentReceiveQueue::HasUnprocessed() const
{
	return HasDeferredProcessingQueueUnprocessed(EDeferredProcessingQueue::Unreliable) || HasDeferredProcessingQueueUnprocessed(EDeferredProcessingQueue::Reliable);
}

const TRefCountPtr<FNetBlob>* FNetObjectAttachmentReceiveQueue::PeekReliable()
{
	if (IsDeferredProcessingQueueEmpty(EDeferredProcessingQueue::Reliable))
	{
		return nullptr;
	}
	else
	{
		return DeferredProcessingQueues[EDeferredProcessingQueue::Reliable]->Peek();
	}
}

void FNetObjectAttachmentReceiveQueue::PopReliable()
{
	DeferredProcessingQueues[EDeferredProcessingQueue::Reliable]->Pop();
}

const TRefCountPtr<FNetBlob>* FNetObjectAttachmentReceiveQueue::PeekUnreliable() const
{
	if (IsDeferredProcessingQueueEmpty(EDeferredProcessingQueue::Unreliable))
	{
		return nullptr;
	}
	else
	{
		return DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable]->Peek();
	}
}

void FNetObjectAttachmentReceiveQueue::PopUnreliable()
{
	return DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable]->Pop();
}

void FNetObjectAttachmentReceiveQueue::SetUnreliableQueueCapacity(uint32 QueueCapacity)
{
	MaxUnreliableCount = QueueCapacity;
	
	if (FDeferredProcessingQueue* UnreliableProcessingQueue = DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable])
	{
		UnreliableProcessingQueue->SetQueueCapacity(QueueCapacity);
	}
}

bool FNetObjectAttachmentReceiveQueue::IsDeferredProcessingQueueEmpty(EDeferredProcessingQueue Queue) const
{
	return DeferredProcessingQueues[Queue] == nullptr || DeferredProcessingQueues[Queue]->IsEmpty();
}

bool FNetObjectAttachmentReceiveQueue::IsDeferredProcessingQueueSafeToDestroy(EDeferredProcessingQueue Queue) const
{
	return DeferredProcessingQueues[Queue] == nullptr || DeferredProcessingQueues[Queue]->IsSafeToDestroy();
}

bool FNetObjectAttachmentReceiveQueue::HasDeferredProcessingQueueUnprocessed(EDeferredProcessingQueue Queue) const
{
	return DeferredProcessingQueues[Queue] != nullptr && DeferredProcessingQueues[Queue]->HasUnprocessed();
}

bool FNetObjectAttachmentReceiveQueue::IsPartialNetBlob(const TRefCountPtr<FNetBlob>& Blob) const
{
	return Blob.IsValid() && Blob->GetCreationInfo().Type == PartialNetBlobType;
}

void FNetObjectAttachmentReceiveQueue::Deserialize(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	FNetBitStreamReader& Reader = *Context.GetBitStreamReader();

	UE_NET_TRACE_SCOPE(RPCs, Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	const bool bHasReliableAttachments = Reader.ReadBool();
	const bool bHasUnreliableAttachments = Reader.ReadBool();

	if (Reader.IsOverflown())
	{
		return;
	}

	if (bHasReliableAttachments)
	{
		UE_NET_TRACE_SCOPE(Ordered, Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		DeserializeReliable(Context, RefHandle);
		if (Context.HasErrorOrOverflow())
		{
			return;
		}
	}

	if (bHasUnreliableAttachments)
	{
		UE_NET_TRACE_SCOPE(Unreliable, Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		DeserializeUnreliable(Context, RefHandle);
		if (Context.HasErrorOrOverflow())
		{
			return;
		}
	}
}

uint32 FNetObjectAttachmentReceiveQueue::DeserializeReliable(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	// 中文：懒分配 ReliableQueue + 对应 Deferred Queue（首次收到 reliable blob 时）。
	if (ReliableQueue == nullptr)
	{
		ReliableQueue = new FReliableNetBlobQueue();

		checkSlow(DeferredProcessingQueues[EDeferredProcessingQueue::Reliable] == nullptr);
		DeferredProcessingQueues[EDeferredProcessingQueue::Reliable] = new FDeferredProcessingQueue();
		FDeferredProcessingQueue::FInitParams InitParams = { .PartialNetObjectAttachmentHandler = PartialNetObjectAttachmentHandler, .bOnlyProcessUnreliable = false };
		DeferredProcessingQueues[EDeferredProcessingQueue::Reliable]->Init(InitParams);
	}

	uint32 DeserializedReliableCount = 0;
	if (RefHandle.IsValid())
	{
		DeserializedReliableCount = ReliableQueue->DeserializeWithObject(Context, RefHandle);
	}
	else
	{
		DeserializedReliableCount = ReliableQueue->Deserialize(Context);
	}

	if (Context.HasErrorOrOverflow())
	{
		return DeserializedReliableCount;
	}

	// 中文：把 ReliableQueue 中"按序到达可处理"的 blob 全部 Pop 到 Deferred Queue。
	// 注意是 ReliableQueue 的内部按序 Peek/Pop——保证业务侧拿到的 blob 是有序的。
	FDeferredProcessingQueue* DeferredProcessingQueue = DeferredProcessingQueues[EDeferredProcessingQueue::Reliable];
	while (const TRefCountPtr<FNetBlob>* Attachment = ReliableQueue->Peek())
	{
		DeferredProcessingQueue->Enqueue(Context, RefHandle, *Attachment, IsPartialNetBlob(*Attachment));
		ReliableQueue->Pop();

		if (Context.HasErrorOrOverflow())
		{
			return DeserializedReliableCount;
		}
	}

	// Add all received unreliable attachments to prevent their processing from being blocked by reliable ones.
	// 中文：把"夹带在 reliable 通道里的 unreliable blob"抽出来放到 Deferred 不可靠队列——
	// 防止"重要不可靠"被卡在 reliable 序号坑上等不到。
	{
		TArray<TRefCountPtr<FNetBlob>> UnreliableAttachments;
		UnreliableAttachments.Reserve(16);
		ReliableQueue->DequeueUnreliable(UnreliableAttachments);
		for (TRefCountPtr<FNetBlob>& UnreliableAttachment : UnreliableAttachments)
		{
			DeferredProcessingQueue->Enqueue(Context, RefHandle, UnreliableAttachment, IsPartialNetBlob(UnreliableAttachment));
			if (Context.HasErrorOrOverflow())
			{
				return DeserializedReliableCount;
			}
		}
	}

	return DeserializedReliableCount;
}

uint32 FNetObjectAttachmentReceiveQueue::DeserializeUnreliable(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	// 中文：懒分配 Unreliable Deferred Queue（bOnlyProcessUnreliable=true 防御）。
	if (DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable] == nullptr)
	{
		DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable] = new FDeferredProcessingQueue();
		FDeferredProcessingQueue::FInitParams InitParams = { .PartialNetObjectAttachmentHandler = PartialNetObjectAttachmentHandler, .bOnlyProcessUnreliable = true };
		DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable]->Init(InitParams);
	}

	FDeferredProcessingQueue* DeferredProcessingQueue = DeferredProcessingQueues[EDeferredProcessingQueue::Unreliable];

	INetBlobReceiver* BlobReceiver = Context.GetNetBlobReceiver();
	checkSlow(BlobReceiver != nullptr);

	FNetBitStreamReader& Reader = *Context.GetBitStreamReader();

	uint32 DeserializedUnreliableCount = 0;
	const bool bDeserializeWithObject = RefHandle.IsValid();
	bool bHasMoreAttachments = false;
	do
	{
		// If the unreliable queue overflows this could be a carefully crafted malicious packet.
		// 中文：unreliable 队列爆了——可能是恶意包（不停发不可靠想撑爆内存）。
		// 直接 SetError，由上层断连。
		if (DeferredProcessingQueue->GetQueueCount() == MaxUnreliableCount)
		{
			UE_LOG(LogIris, Error, TEXT("Unreliable queue is full for %s"), *RefHandle.ToString());
			Context.SetError(NetError_UnreliableQueueFull);
			break;
		}

		// 中文：先读 CreationInfo 拿 BlobType → 由 NetBlobReceiver 工厂创建对应 blob 实例。
		FNetBlobCreationInfo CreationInfo;
		FNetBlob::DeserializeCreationInfo(Context, CreationInfo);
		const TRefCountPtr<FNetBlob>& Attachment = BlobReceiver->CreateNetBlob(CreationInfo);
		if (!Attachment.IsValid())
		{
			Context.SetError(GNetError_UnsupportedNetBlob);
			break;
		}

		if (bDeserializeWithObject)
		{
			Attachment->DeserializeWithObject(Context, RefHandle);
		}
		else
		{
			Attachment->Deserialize(Context);
		}
		
		bHasMoreAttachments = Reader.ReadBool();

		// Break if something went wrong with deserialization.
		if (Context.HasErrorOrOverflow())
		{
			UE_LOG(LogIris, Error, TEXT("Failed to deserialize unreliable attachments for %s"), *RefHandle.ToString());
			break;
		}

		DeferredProcessingQueue->Enqueue(Context, RefHandle, Attachment, IsPartialNetBlob(Attachment));
		if (Context.HasErrorOrOverflow())
		{
			UE_LOG(LogIris, Error, TEXT("Failed to deserialize unreliable attachments for %s"), *RefHandle.ToString());
			break;
		}

		++DeserializedUnreliableCount;
	} while (bHasMoreAttachments);

	return DeserializedUnreliableCount;
}

FNetObjectAttachmentsReader::FNetObjectAttachmentsReader()
{
}

FNetObjectAttachmentsReader::~FNetObjectAttachmentsReader()
{
}

void FNetObjectAttachmentsReader::Init(const FNetObjectAttachmentsReaderInitParams& InitParams)
{
	PartialNetObjectAttachmentHandler = InitParams.PartialNetObjectAttachmentHandler;
}

bool FNetObjectAttachmentsReader::HasUnprocessedAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	const FNetObjectAttachmentReceiveQueue* Queue = GetQueue(Type, ObjectIndex);
	if (Queue == nullptr)
	{
		return false;
	}

	return Queue->HasUnprocessed();
}

void FNetObjectAttachmentsReader::DropAllAttachments(ENetObjectAttachmentType Type, uint32 ObjectIndex)
{
	ObjectToQueue.Remove(ObjectIndex);
}

void FNetObjectAttachmentsReader::Deserialize(FNetSerializationContext& Context, ENetObjectAttachmentType Type, uint32 ObjectIndex, FNetRefHandle RefHandle)
{
	FNetObjectAttachmentReceiveQueue* Queue = GetOrCreateQueue(Type, ObjectIndex);
	if (!ensure(Queue != nullptr))
	{
		Context.SetError(NetError_TooManyObjectsWithFunctionCalls);
		return;
	}

	Queue->Deserialize(Context, RefHandle);
}

FNetObjectAttachmentReceiveQueue* FNetObjectAttachmentsReader::GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex)
{
	return Type == ENetObjectAttachmentType::Normal ? ObjectToQueue.Find(ObjectIndex) : SpecialQueues[uint32(Type)].Get();
}

const FNetObjectAttachmentReceiveQueue* FNetObjectAttachmentsReader::GetQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex) const
{
	return Type == ENetObjectAttachmentType::Normal ? ObjectToQueue.Find(ObjectIndex) : SpecialQueues[uint32(Type)].Get();
}

FNetObjectAttachmentReceiveQueue* FNetObjectAttachmentsReader::GetOrCreateQueue(ENetObjectAttachmentType Type, uint32 ObjectIndex)
{
	if (Type != ENetObjectAttachmentType::Normal)
	{
		checkSlow(ObjectIndex == 0U);
		TUniquePtr<FNetObjectAttachmentReceiveQueue>& QueuePtr = SpecialQueues[uint32(Type)];
		FNetObjectAttachmentReceiveQueue* Queue = QueuePtr.Get();
		if (Queue != nullptr)
		{
			return Queue;
		}

		QueuePtr = MakeUnique<FNetObjectAttachmentReceiveQueue>();
		Queue = QueuePtr.Get();
		{
			FNetObjectAttachmentReceiveQueueInitParams InitParams;
			InitParams.PartialNetObjectAttachmentHandler = PartialNetObjectAttachmentHandler;
			Queue->Init(InitParams);
		}

		if (Type == ENetObjectAttachmentType::OutOfBand)
		{
			Queue->SetUnreliableQueueCapacity(AttachmentReplicationCVars::ClientToServerUnreliableRPCQueueSize);
		}

		return Queue;
	}
	else
	{
		FNetObjectAttachmentReceiveQueue* Queue = ObjectToQueue.Find(ObjectIndex);
		if (Queue != nullptr)
		{
			return Queue;
		}

		if (ObjectToQueue.Num() >= AttachmentReplicationCVars::MaxSimultaneousObjectsWithRPCs)
		{
			return nullptr;
		}

		Queue = &ObjectToQueue.Add(ObjectIndex);
		{
			FNetObjectAttachmentReceiveQueueInitParams InitParams;
			InitParams.PartialNetObjectAttachmentHandler = PartialNetObjectAttachmentHandler;
			Queue->Init(InitParams);
		}

		return Queue;
	}
}

}
