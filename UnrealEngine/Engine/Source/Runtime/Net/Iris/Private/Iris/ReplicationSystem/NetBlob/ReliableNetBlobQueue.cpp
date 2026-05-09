// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReliableNetBlobQueue.cpp —— 可靠 NetBlob 有序队列实现
// -----------------------------------------------------------------------------
// 三条主路径：
//   * 发送 SerializeInternal —— 遍历 [FirstSeq, LastSeq)，跳过 IsIndexSent；同
//     一包内最多写 4 段不连续序号区间，每段最多 63 个 blob。每次写一个 blob
//     后用 FNetBitStreamWriteScope 在前一个 "HasMoreBlobs" 位上标记 true，
//     形成"链表式"位流。第一个 disjoint 段的开头会写完整 IndexBitCount=10 的
//     起始索引，后续连续序号只写 1 bit "false" 表示自增。
//   * 提交 CommitReplicationRecord —— 包真正落网络后调用：把 Sent 位打上、
//     unreliable 立即 release（不重发）、UnsentBlobCount 减去本批 count。
//   * 包到达回执 ProcessPacketDeliveryStatus —— Delivered → Acked + 推进窗口；
//     Lost → 清 Sent + UnsentBlobCount 增加（下一帧重发）；Discard → 等同 Delivered。
//
// 接收 DeserializeInternal —— 与发送对偶：读 IndexBitCount (或自增) → 校验
// IsValidReceiveSequence + !IsIndexAcked → 读 bHasData → 反序列化 blob → 写入
// NetBlobs[Index]、SetIndexIsAcked、推进 LastSeq → 直到 bHasMoreBlobs=false。
//
// Peek/Pop —— 接收侧消费：仅当 FirstSeq 处已 acked 才返回该 blob；空 blob 跳过。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/ReliableNetBlobQueue.h"
#include "Iris/IrisConfigInternal.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandlerManager.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Net/Core/Trace/NetTrace.h"

namespace UE::Net::Private
{

// 中文：错误名（NetTrace / 上层关闭连接时使用）。
static const FName NetError_ReliableQueueFull("Reliable attachment queue full");
static const FName NetError_InvalidSequence("Invalid sequence number");

// 中文：trace 中显示的"reliable / unreliable"名字（运行时初始化）。
static FNetDebugName const* ReliabilityNetDebugNames[2];

// ReliableNetBlobQueue
FReliableNetBlobQueue::FReliableNetBlobQueue()
: Sent{}
, Acked{}
, FirstSeq(0)
, LastSeq(0)
, UnsentBlobCount(0)
{
#if UE_NET_TRACE_ENABLED
	Private::ReliabilityNetDebugNames[0] = CreatePersistentNetDebugName(TEXT("Unreliable"));
	Private::ReliabilityNetDebugNames[1] = CreatePersistentNetDebugName(TEXT("Reliable"));
#endif
}

FReliableNetBlobQueue::~FReliableNetBlobQueue()
{
}

// If everything is sent and acked and the first sequence index is zero then it's safe to destroy this instance.
// 中文：销毁安全条件——全部送达且 FirstSeq 在窗口零点。FirstSeq != 0 时，序号
// 仍残留环形位置上的位图状态，强行销毁可能丢失正在飞行中的回执处理。
bool FReliableNetBlobQueue::IsSafeToDestroy() const
{
	if (FirstSeq != LastSeq)
	{
		return false;
	}

	if (SequenceToIndex(FirstSeq) != 0)
	{
		return false;
	}

	return true;
}

uint32 FReliableNetBlobQueue::Serialize(FNetSerializationContext& Context, FReliableNetBlobQueue::FReplicationRecord& OutRecord)
{
	FNetRefHandle InvalidNetHandle;
	constexpr bool bSerializeWithObject = false;
	return SerializeInternal(Context, InvalidNetHandle, OutRecord, bSerializeWithObject);
}

uint32 FReliableNetBlobQueue::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReliableNetBlobQueue::FReplicationRecord& OutRecord)
{
	constexpr bool bSerializeWithObject = true;
	return SerializeInternal(Context, RefHandle, OutRecord, bSerializeWithObject);
}

// 中文：核心序列化函数。算法要点：
//   1) 遍历窗口 [FirstSeq, LastSeq)，跳过已 Sent 的；
//   2) 当遇到 disjoint（与上一个写入序号不连续）时启用新的 WrittenIndex 段；
//      最多 4 段——超过则停止本包的写入；
//   3) 每写完一个 blob 立即试 RollbackScope —— 溢出则回滚整个 blob 写入并退出；
//   4) 链表式 bHasMoreBlobs 位：每条 blob 末尾占 1 bit，下一条 blob 写入成功后
//      回填上一条的 bHasMoreBlobs=true。
//   5) 最终的 OutRecord 描述本包写入的 4 段（Number 是相对窗口的 10 位序号）。
uint32 FReliableNetBlobQueue::SerializeInternal(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReliableNetBlobQueue::FReplicationRecord& OutRecord, const bool bSerializeWithObject)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	const FObjectReferenceCache* ObjectReferenceCache = Context.GetInternalContext()->ObjectReferenceCache;

	uint32 PrevHasMoreBlobsWritePos = 0;
	uint32 SerializedCount = 0;
	uint32 PrevWrittenSeq = ~0U;
	uint32 WrittenIndex = 0;
	uint32 WrittenCount[MaxWriteSequenceCount] = {};
	uint32 WrittenSeq[MaxWriteSequenceCount];
	for (uint32& Sequence : WrittenSeq)
	{
		Sequence = ~0U;
	}

	for (uint32 Seq = FirstSeq, EndSeq = LastSeq; Seq < EndSeq; ++Seq)
	{
		const uint32 Index = SequenceToIndex(Seq);
		if (IsIndexSent(Index))
		{
			// 中文：已发送过的（且仍未确认丢失）跳过。
			continue;
		}

		// Disjoint sequence handling
		// First in sequence?
		// 中文：处理不连续段。第一次遇到的就开新段；与上一次写入序号 +1 不相等
		// 也开新段；超过 4 段则跳出（剩余留待下个包）。
		if (WrittenSeq[WrittenIndex] == ~0U)
		{
			WrittenSeq[WrittenIndex] = Seq;
		}
		// If broken sequence start the next one.
		else if (PrevWrittenSeq + 1U != Seq)
		{
			++WrittenIndex;
			// There's limited support for disjoint sequences in the replication record.
			if (WrittenIndex >= UE_ARRAY_COUNT(WrittenCount))
			{
				break;
			}
			WrittenSeq[WrittenIndex] = Seq;
		}

		// 中文：原子写入域——溢出则回滚 blob 整段 + export rollback。
		FNetBitStreamRollbackScope RollbackScope(*Writer);
		FNetExportRollbackScope ExportRollbackScope(Context);

		// GetRefCount() is not const.
		TRefCountPtr<FNetBlob>& Attachment = NetBlobs[Index];

		// If this sequence is disjoint from the previous sequence we need to serialize the full index.
		// It's important that the sequence number is sent first so we can validate it before receiving exports and payload.
		// 中文：disjoint 段开头写完整 10 位 Index；连续段只写 1 bit "false" 隐含自增。
		// 序号必须先于 exports/payload 写出 —— 接收侧才能立即校验序号合法性。
		if (Writer->WriteBool(Seq != PrevWrittenSeq + 1U))
		{
			Writer->WriteBits(Index, IndexBitCount);
		}

		// Unreliable blobs may have been released.
		// 中文：unreliable blob 在 CommitReplicationRecord 时已 release，只剩占位
		// 序号——bHasData=false 跳过 payload。Reliable 永远 bHasData=true。
		const bool bHasData = Attachment.GetRefCount() > 0;
		if (Writer->WriteBool(bHasData))
		{
			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(ReliabilityScope, Private::ReliabilityNetDebugNames[Attachment->IsReliable()], *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);

			// If we have exports, append them, if attachment is rolled back we will roll back any appended exports as well.
			// 中文：blob 自带 exports 时把 ref/token export 加入 pending；rollback
			// 时也会回滚（FNetExportRollbackScope 配对）。
			FNetExportContext* ExportContext = Attachment->HasExports() ? Context.GetExportContext() : nullptr;
			if (ExportContext)
			{
				ObjectReferenceCache->AddPendingExports(Context, Attachment->CallGetNetObjectReferenceExports());
				ExportContext->AddPendingExports(Attachment->CallGetNetTokenExports());
			}

			// 中文：写 CreationInfo（包含 typeId / Reliable / HasExports 等），让接收
			// 侧用 INetBlobReceiver::CreateNetBlob 构造正确派生类型；再写 payload。
			Attachment->SerializeCreationInfo(Context, Attachment->GetCreationInfo());
			if (bSerializeWithObject)
			{
				Attachment->SerializeWithObject(Context, RefHandle);
			}
			else
			{
				Attachment->Serialize(Context);
			}
		}

		const uint32 HasMoreBlobsWritePos = Writer->GetPosBits();
		Writer->WriteBool(false); // Don't know yet if there's more to come

		if (Writer->IsOverflown())
		{
			// 中文：溢出 → 回滚 export 与 bit 流写入，结束本包写入。
			ExportRollbackScope.Rollback();
			break;
		}
		// We did manage to serialize yet another blob
		else
		{
			// 中文：写入成功——回填上一条 blob 的 bHasMoreBlobs=true 形成链表。
			if (SerializedCount > 0)
			{
				FNetBitStreamWriteScope WriteScope(*Writer, PrevHasMoreBlobsWritePos);
				Writer->WriteBool(true);
			}

			++SerializedCount;

			++WrittenCount[WrittenIndex];
			if (WrittenCount[WrittenIndex] == MaxSequenceLength)
			{
				// 中文：单段达到 63 上限 → 强制开新段。
				++WrittenIndex;
				if (WrittenIndex >= UE_ARRAY_COUNT(WrittenCount))
				{
					break;
				}
			}

			PrevWrittenSeq = Seq;
			PrevHasMoreBlobsWritePos = HasMoreBlobsWritePos;
		}
	}

	// Assemble replication record
	// 中文：组装 OutRecord —— 4 段 (Number, Count) 编码。Number 取低 10 位（环形）。
	FReplicationRecord Record;
	for (uint32 Index = 0; Index < MaxWriteSequenceCount; ++Index)
	{
		Record.Sequences[Index].Number = WrittenSeq[Index] & (MaxUnackedBlobCount - 1U);
		Record.Sequences[Index].Count = WrittenCount[Index] & MaxSequenceLength;
	}
	OutRecord = Record;

	return SerializedCount;
}

void FReliableNetBlobQueue::CommitReplicationRecord(const FReliableNetBlobQueue::FReplicationRecord& Record)
{
	// 中文：包真正落网络后调用——遍历 4 段把每条 blob 标 Sent，unreliable 立即
	// release（不重发），UnsentBlobCount 减去本批 count。
	for (uint32 Index = 0, EndIndex = MaxWriteSequenceCount; Index != EndIndex; ++Index)
	{
		const uint32 Count = Record.Sequences[Index].Count;
		UnsentBlobCount -= Count;
		for (uint32 Seq = Record.Sequences[Index].Number, EndSeq = Seq + Count; Seq != EndSeq; ++Seq)
		{
			const uint32 BlobIndex = SequenceToIndex(Seq);
			SetIndexIsSent(BlobIndex);

			// Release unreliable blobs. They should not be resent.
			// 中文：unreliable 一旦发出就不再保留——丢包就丢了，没有重发语义。
			TRefCountPtr<FNetBlob>& RefCountBlob = NetBlobs[BlobIndex];
			if (const FNetBlob* Blob = RefCountBlob.GetReference())
			{
				if (!Blob->IsReliable())
				{
					RefCountBlob.SafeRelease();
				}
			}
		}
	}
}

SIZE_T FReliableNetBlobQueue::GetUnreliableCount() const
{
	// 中文：诊断/限流用——统计窗口内 unreliable 数量。
	SIZE_T UnreliableCount = 0U;

	for (uint32 Seq = FirstSeq; Seq < LastSeq; ++Seq)
	{
		const uint32 Index = SequenceToIndex(Seq);
		const TRefCountPtr<FNetBlob>& RefCntBlob = NetBlobs[Index];
		if (const FNetBlob* Blob = RefCntBlob.GetReference())
		{
			UnreliableCount += Blob && !Blob->IsReliable() ? 1 : 0;
		}
	}

	return UnreliableCount;
}

void FReliableNetBlobQueue::DropUnreliable()
{
	// 中文：发送侧主动丢弃所有 unreliable —— 防止积压（如低带宽场景）。
	for (uint32 Seq = FirstSeq; Seq < LastSeq; ++Seq)
	{
		const uint32 Index = SequenceToIndex(Seq);

		TRefCountPtr<FNetBlob>& RefCntBlob = NetBlobs[Index];
		const FNetBlob* Blob = RefCntBlob.GetReference();
		if (Blob && !Blob->IsReliable())
		{
			RefCntBlob.SafeRelease();
		}
	}
}

uint32 FReliableNetBlobQueue::Deserialize(FNetSerializationContext& Context)
{
	FNetRefHandle InvalidNetHandle;
	return DeserializeInternal(Context, InvalidNetHandle, false);
}

uint32 FReliableNetBlobQueue::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	return DeserializeInternal(Context, RefHandle, true);
}

// 中文：接收侧反序列化 —— 与 SerializeInternal 严格对偶：
//   * 读 1 bit "isNewSequence"：true → 读 10 位完整 Index；false → Index = (上次+1) % 1024。
//   * 由 Index 反推 ReceivedSeq —— 用 FirstSeq 作为相位锚（环形序号还原）。
//   * 校验：必须落在窗口内 + 还未 Acked（重复包丢弃）；否则 SetError + return。
//   * 读 1 bit "bHasData"：false 表示 unreliable 占位；true → 读 CreationInfo +
//     payload，由 INetBlobReceiver::CreateNetBlob 构造对应派生类型。
//   * 写入 NetBlobs[Index]，SetIndexIsAcked，推进 LastSeq。
//   * 链表式 bHasMoreBlobs 决定循环结束。
uint32 FReliableNetBlobQueue::DeserializeInternal(FNetSerializationContext& Context, FNetRefHandle RefHandle, const bool bSerializeWithObject)
{
	INetBlobReceiver* BlobReceiver = Context.GetNetBlobReceiver();
	checkSlow(BlobReceiver != nullptr);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	FObjectReferenceCache* ObjectReferenceCache = Context.GetInternalContext()->ObjectReferenceCache;

	uint32 Index = ~0U;
	uint32 DeserializedCount = 0;
	bool bHasMoreBlobs = false;
	do
	{
		// 中文：读完整 Index（disjoint 段开头）或自增（连续段）。
		if (const bool bIsNewSequence = Reader->ReadBool())
		{
			Index = Reader->ReadBits(IndexBitCount);
		}
		else
		{
			Index = (Index + 1) % MaxUnackedBlobCount;
		}

		// 中文：把环形 Index 解压回绝对序号 ReceivedSeq。
		// 公式：以 FirstSeq 所在的"窗口起点"对齐到 1024 的整数倍，再加 Index。
		// 如果反推结果 < FirstSeq，说明落在下一圈，加一个 1024 的周期。
		uint32 ReceivedSeq = FirstSeq - (FirstSeq % MaxUnackedBlobCount) + Index;
		if (ReceivedSeq < FirstSeq)
		{
			ReceivedSeq += MaxUnackedBlobCount;
		}

		if (!IsValidReceiveSequence(ReceivedSeq) || IsIndexAcked(Index))
		{
			// 中文：序号越界或重复 → 协议错误/旧包，立即报错并返回已成功反序列化数量。
			UE_LOG(LogIris, Warning, TEXT("Invalid reliable sequence number. ReceivedSeq: %u FirstSeq: %u LastSeq: %u IsAcked: %u"), ReceivedSeq, FirstSeq, LastSeq, unsigned(IsIndexAcked(Index)));
			Context.SetError(NetError_InvalidSequence);

			return DeserializedCount;
		}

		TRefCountPtr<FNetBlob> Blob;
		if (const bool bHasData = Reader->ReadBool())
		{
			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(ReliabilityScope, Private::ReliabilityNetDebugNames[0], *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);

			// 中文：先读 CreationInfo（typeId + flags），让 receiver 用对应 handler
			// 构造正确派生类型（NetRPC / 自定义 attachment / partial 等）。
			FNetBlobCreationInfo CreationInfo;
			FNetBlob::DeserializeCreationInfo(Context, CreationInfo);
			Blob = BlobReceiver->CreateNetBlob(CreationInfo);
			if (!Blob.IsValid())
			{
				// 中文：typeId 找不到对应 handler —— 协议错误/双方版本不一致。
				UE_LOG(LogIris, Warning, TEXT("%hs"), "Unable to create blob.");
				Context.SetError(GNetError_UnsupportedNetBlob);

				return DeserializedCount;
			}

			if (bSerializeWithObject)
			{
				Blob->DeserializeWithObject(Context, RefHandle);
			}
			else
			{
				Blob->Deserialize(Context);
			}

			UE_NET_TRACE_SET_SCOPE_NAME(ReliabilityScope, Private::ReliabilityNetDebugNames[Blob->IsReliable()]);
		}
		
		bHasMoreBlobs = Reader->ReadBool();

		// Break if something went wrong with deserialization.
		// 中文：反序列化错误（payload 不匹配）→ 上层关闭连接，但本批已部分成功。
		if (Context.HasErrorOrOverflow())
		{
			UE_LOG(LogIris, Warning, TEXT("Failed to deserialize reliable attachments for %s"), *RefHandle.ToString());
			break;
		}

		// 中文：写入接收槽位 + 标 Acked + 推进 LastSeq（窗口右端）。
		NetBlobs[Index] = MoveTemp(Blob);
		LastSeq = FMath::Max(LastSeq, ReceivedSeq + 1U);
		SetIndexIsAcked(Index);

		++DeserializedCount;
	} while (bHasMoreBlobs);

	return DeserializedCount;
}

bool FReliableNetBlobQueue::Enqueue(const TRefCountPtr<FNetBlob>& Blob)
{
	// 中文：发送侧入队。窗口满 → 失败，调用方需自己处理（缓存/丢弃/降级）。
	if (IsSendWindowFull())
	{
		return false;
	}

	++UnsentBlobCount;

	const uint32 Index = SequenceToIndex(LastSeq);
	++LastSeq;

	NetBlobs[Index] = Blob;
	return true;
}

const TRefCountPtr<FNetBlob>* FReliableNetBlobQueue::Peek()
{
	// 中文：接收侧 Peek —— 严格按序投递。从 FirstSeq 开始扫描：
	//   * 该位置未 acked → 立即返回 nullptr（必须等前面到齐）；
	//   * 该位置 acked 但 blob 为空（unreliable 占位）→ 跳过并清 Acked，推进 FirstSeq；
	//   * 该位置 acked 且 blob 非空 → 返回该 blob。
	for (; FirstSeq < LastSeq; ++FirstSeq)
	{
		const uint32 Index = SequenceToIndex(FirstSeq);
		if (!IsIndexAcked(Index))
		{
			return nullptr;
		}

		if (NetBlobs[Index].GetRefCount() > 0)
		{
			return &NetBlobs[Index];
		}

		// We skip over empty blobs and ack them.
		// 中文：跳过空槽位（unreliable 占位）。
		ClearIndexIsAcked(Index);
	}

	return nullptr;
}

void FReliableNetBlobQueue::Pop()
{
	// 中文：Peek 处理完调用 → 清槽位并推进 FirstSeq。前置条件：必须 acked 且窗口非空。
	const uint32 Index = SequenceToIndex(FirstSeq);
	checkSlow(IsIndexAcked(Index) && (FirstSeq < LastSeq));
	NetBlobs[Index].SafeRelease();
	ClearIndexIsAcked(Index);
	++FirstSeq;

	// $TODO. For a memory optimization one can change the storage implementation and free the memory here if everything is acked.
}


void FReliableNetBlobQueue::DequeueUnreliable(TArray<TRefCountPtr<FNetBlob>>& Unreliable)
{
	// 中文：接收侧——把已 acked 的 unreliable blob 抽出（unreliable 不要求顺序，
	// 上层可以乱序 dispatch）。Reliable 不动，仍按 Peek/Pop 顺序处理。
	// 若被抽走的 blob 正好在 FirstSeq，顺便推进窗口。
	for (uint32 Seq = FirstSeq; Seq < LastSeq; ++Seq)
	{
		const uint32 Index = SequenceToIndex(Seq);
		if (!IsIndexAcked(Index))
		{
			continue;
		}

		TRefCountPtr<FNetBlob>& RefCntBlob = NetBlobs[Index];
		if (RefCntBlob.GetRefCount() > 0)
		{
			if (const bool bIsUnReliable = !RefCntBlob->IsReliable())
			{
				Unreliable.Emplace(MoveTemp(RefCntBlob));

				// Advance the ack window if possible.
				if (Seq == FirstSeq)
				{
					ClearIndexIsAcked(Index);
					++FirstSeq;
				}
			}
		}
	}
}

void FReliableNetBlobQueue::ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, const FReliableNetBlobQueue::FReplicationRecord& Record)
{
	// 中文：包到达回执的派发器。
	switch (Status)
	{
	case EPacketDeliveryStatus::Delivered:
	{
		OnPacketDelivered(Record);
		break;
	}
	case EPacketDeliveryStatus::Lost:
	{
		OnPacketDropped(Record);
		break;
	}
	case EPacketDeliveryStatus::Discard:
	{
		// Pretend that it was delivered.
		// 中文：连接关闭等场景——视为 delivered，避免无谓的重传。
		OnPacketDelivered(Record);
		break;
	}
	default:
	{
		break;
	}
	}
}

void FReliableNetBlobQueue::OnPacketDelivered(const FReliableNetBlobQueue::FReplicationRecord& Record)
{
	// Mark blobs as acked
	// 中文：把 4 段中所有序号在 Acked 位图上置位，并立即 release blob 以快速回收内存。
	for (uint32 SeqIt = 0, EndSeqIt = MaxWriteSequenceCount; SeqIt != EndSeqIt; ++SeqIt)
	{
		const uint32 Count = Record.Sequences[SeqIt].Count;
		for (uint32 Seq = Record.Sequences[SeqIt].Number, EndSeq = Seq + Count; Seq != EndSeq; ++Seq)
		{
			const uint32 Index = SequenceToIndex(Seq);

			SetIndexIsAcked(Index);

			// Release blob as quickly as possible.
			NetBlobs[Index].SafeRelease();
		}
	}

	// Remove acked blobs, allowing for more blobs to be added to the queue.
	// 中文：从 FirstSeq 开始连续 acked 的 blob 一起 Pop，腾出窗口。
	PopInOrderAckedBlobs();
}

void FReliableNetBlobQueue::OnPacketDropped(const FReliableNetBlobQueue::FReplicationRecord& Record)
{
	// Mark blobs as unsent
	// 中文：丢包路径——清 Sent 位让它们在下一次 SerializeInternal 时被重发；
	// 同时 UnsentBlobCount 加回去（CommitReplicationRecord 时减过）。
	for (uint32 SeqIt = 0, EndSeqIt = MaxWriteSequenceCount; SeqIt != EndSeqIt; ++SeqIt)
	{
		const uint32 Count = Record.Sequences[SeqIt].Count;
		UnsentBlobCount += Count;
		for (uint32 Seq = Record.Sequences[SeqIt].Number, EndSeq = Seq + Count; Seq != EndSeq; ++Seq)
		{
			ClearSequenceIsSent(Seq);
		}
	}
}

void FReliableNetBlobQueue::PopInOrderAckedBlobs()
{
	// 中文：从 FirstSeq 开始扫描连续 acked 的位置，全部出队（清 Acked + Sent + 推
	// 进 FirstSeq）。一旦遇到未 acked 立即停止——保证窗口语义。
	for (; FirstSeq != LastSeq; ++FirstSeq)
	{
		const uint32 Index = SequenceToIndex(FirstSeq);
		if (!IsIndexAcked(Index))
		{
			return;
		}

		ClearIndexIsAcked(Index);
		ClearIndexIsSent(Index);
	}

	// $TODO. For a memory optimization one can change the storage implementation and free the memory here if everything is acked.
}

}
