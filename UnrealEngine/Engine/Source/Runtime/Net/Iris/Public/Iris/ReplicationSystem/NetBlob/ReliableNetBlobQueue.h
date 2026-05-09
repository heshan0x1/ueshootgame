// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReliableNetBlobQueue.h —— 可靠 NetBlob 有序队列（in-order delivery）
// -----------------------------------------------------------------------------
// 角色：每个对象/连接的"可靠 attachment 通道"内部数据结构。提供：
//   * 序号窗口：[FirstSeq, LastSeq) 表示发送窗口；MaxUnackedBlobCount=1024。
//   * Sent / Acked 位图：跟踪每条 blob 的"已发送/已确认"状态，环形索引到
//     NetBlobs[1024]。
//   * inflight / pending 区分：UnsentBlobCount = 还未写入任何包的 blob 数。
//   * 重传：包丢失（Lost）→ 清 Sent 位 → 下一帧 SerializeInternal 重发。
//   * 顺序保证：接收侧 Peek 仅返回连续 acked 的 blob；不连续位置上的 blob 即使
//     已收到也要等前面 blob 到齐才能 Pop。
// 序列化记录（FReplicationRecord）：
//   * 一个 packet 最多写入 4 段不连续的序号区间（4 × {Number(10b),Count(6b)}），
//     共 64 位。这是为了同时容纳：丢包重传段 + 新发送段。
//   * Number 是相对窗口的低 10 位（环形索引），Count 是该段长度。
// =============================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/PacketControl/PacketNotification.h"
#include "Templates/RefCounting.h"

namespace UE::Net
{
	class FNetBlob;
	class FNetSerializationContext;
	struct FReplicationStateDescriptor;
	namespace Private
	{
		class FNetBlobHandlerManager;
	}
}

namespace UE::Net::Private
{

/** Helper class to deliver blobs reliably in order. 中文：可靠且按序投递 blob 的辅助队列。*/
class FReliableNetBlobQueue
{
public:
	// A single ReplicationRecord supports up to four disjoint sequences to be serialized in a packet. This allows for resends of dropped data while potentially also writing new blobs for the first time.
	// 中文：FReplicationRecord 描述一次 Serialize 写入的"序号布局"——一个包内最多
	// 4 个不连续区间，覆盖"重传 + 新发送"场景。每个 FSequence = {Number, Count}：
	//   Number(10bit) —— 本段起始序号（相对窗口）
	//   Count(6bit)   —— 本段长度（最多 63 个 blob）
	// 64 位刚好放得下 4 段。
	struct FReplicationRecord
	{
		struct FSequence
		{
			enum : unsigned
			{
				NumberBitCount = 10U,
				CountBitCount = 6U,
			};

			FSequence() = default;
			explicit FSequence(uint16 Value)
			{
				Number = Value & ((1U << NumberBitCount) - 1U);
				Count = Value >> NumberBitCount;
			}

			uint16 ToUint16() const
			{
				return static_cast<uint16>((Count << NumberBitCount) | Number);
			}

			uint16 Number : NumberBitCount = 0;
			uint16 Count : CountBitCount = 0;
		};

		FReplicationRecord() = default;
		// 中文：从 64 位整体编码反序列化（高位是低索引）。
		explicit FReplicationRecord(uint64 Value)
		{
			for (unsigned Index : {3U, 2U, 1U, 0U})
			{
				Sequences[Index] = FSequence(Value & 0xFFFFU);
				Value >>= 16U;
			}
		}

		uint64 ToUint64() const
		{
			uint64 Value = 0;
			for (unsigned Index : {0U, 1U, 2U, 3U})
			{
				Value = (Value << 16U) | Sequences[Index].ToUint16();
			}

			return Value;
		}

		// 中文：四段 Count 全为 0 → 本包没有可靠数据，记录可整体丢弃。
		bool IsValid() const { return (Sequences[0].Count | Sequences[1].Count | Sequences[2].Count | Sequences[3].Count) != 0U; }

		FSequence Sequences[4];
	};

	/** This represents a ReplicationRecord where nothing was serialized. */

	/** How many blobs can be sent before an ACK/NAK is required to continue sending. Changing this might require changing FReplicationRecord too. */
	// 中文：发送窗口大小。Sent/Acked 位图 + NetBlobs 数组都是 1024。改大需同步
	// 修改 FSequence::NumberBitCount。
	static constexpr uint32 MaxUnackedBlobCount = 1024U;

	FReliableNetBlobQueue();
	~FReliableNetBlobQueue();

	/** Returns the number of blobs that have not yet been sent. 中文：尚未写入任何包的 blob 数。*/
	uint32 GetUnsentBlobCount() const { return UnsentBlobCount; }

	/** Returns true if there are unsent blobs. There may still be unacked blobs even if there are no unsent ones. */
	// 中文：有"还没发"的 blob。注意"已发未 ack"的 blob 也算 inflight，不影响此判断。
	bool HasUnsentBlobs() const { return GetUnsentBlobCount() > 0; }

	/** Returns whether all blobs have been sent and acknowledged as received. */
	// 中文：FirstSeq==LastSeq 且无 Unsent → 整个队列空。
	bool IsAllSentAndAcked() const { return FirstSeq == LastSeq && GetUnsentBlobCount() == 0; }

	/** Returns whether the send window is full or not. 中文：窗口长度达到 1024 即满，必须等 ack 才能继续 Enqueue。*/
	bool IsSendWindowFull() const;

	/** Returns true if it's safe to destroy this queue. */
	// 中文：FirstSeq==LastSeq 且 FirstSeq 在窗口起点 → 不会出现"半开半合"的悬挂状态。
	IRISCORE_API bool IsSafeToDestroy() const;

	/** Put a blob to be sent in the queue. Returns true if the blob was successfully queued and false if the queue was full. */
	// 中文：入队（发送侧）。窗口满返回 false——上层需要降级（如丢弃或缓存到外部）。
	IRISCORE_API bool Enqueue(const TRefCountPtr<FNetBlob>& Blob);

	/** On the receiving end this will return a pointer to the next blob that can be processed. */
	// 中文：接收侧 Peek —— 返回当前 FirstSeq 处的 blob（要求已 ack）。如果该位置
	// 不是 acked 直接返回 nullptr（必须等前面到齐保证 in-order）。
	IRISCORE_API const TRefCountPtr<FNetBlob>* Peek();

	/** On the receiving end this will remove the next blob to be processed from the queue. Call after processing the blob returned from Peek(). */
	// 中文：接收侧 Pop —— Peek 处理完后调用，++FirstSeq 推进窗口。
	IRISCORE_API void Pop();

	/** On the receiving end this will move all received unreliable NetBlobs to the array and release them from the queue. This breaks the ordering guarantees provided by using Peek and Pop. Reliable NetBlobs are unaffected by this operation. */
	// 中文：把窗口内所有 unreliable blob 抽出去（不保证顺序，但 unreliable 本身
	// 也不要求保序）。reliable 不受影响。
	IRISCORE_API void DequeueUnreliable(TArray<TRefCountPtr<FNetBlob>>& Unreliable);

	/** Count all unreliable blobs 中文：数窗口内的 unreliable 数量（用于诊断/限流）。*/
	IRISCORE_API SIZE_T GetUnreliableCount() const;

	/** On sending side this will drop all unreliable in the queue to avoid creating a backlog of unsent unreliable blobs */
	// 中文：发送侧主动丢弃所有 unreliable —— 防止 unreliable 在队列里积压。
	IRISCORE_API void DropUnreliable();

	/**
	 * Serializes as many blobs as possible using their respective SerializeWithObject() method. It is assumed the NetRefHandle will be 
	 * reconstructed somehow on the receiving end and passed to DeserializeWithObject().
	 * This provides an opportunity for FNetObjectAttachments, such as FNetRPCs, to avoid serializing the same NetRefHandle redundantly.
	 * @param Context A FNetSerializationContext.
	 * @param RefHandle The handle for the blobs' target object.
	 * @param OutRecord The record to pass to CommitReplicationRecord() if a packet containing the serialized data was sent.
	 * @return The number of blobs that were serialized.
	 *
	 * 中文：尽可能多地写入 blob —— 走 SerializeWithObject 路径（已知 RefHandle，省
	 * 略 source object 引用编码）。返回实际写入数量；OutRecord 记录序号区间，
	 * 由调用方在包发出时通过 CommitReplicationRecord 提交。
	 */
	IRISCORE_API uint32 SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReplicationRecord& OutRecord);

	/**
	 * Deserializes blobs with object using their respective DeserializeWithObject() method.
	 * @return The number of blobs that were deserialized.
	 * @see SerializeWithObject
	 *
	 * 中文：接收侧——读出本包内的所有可靠 blob 并放入接收窗口（按序号索引）。
	 */
	IRISCORE_API uint32 DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle);

	/**
	 * Serializes as many blobs as possible using their respective Serialize() method.
	 * @param Context A FNetSerializationContext.
	 * @param OutRecord The record to pass to CommitReplicationRecord() if a packet containing the serialized data was sent.
	 * @return The number of blobs that were serialized.
	 *
	 * 中文：不带对象上下文版本——blob 内自带 ObjectReference。
	 */
	IRISCORE_API uint32 Serialize(FNetSerializationContext& Context, FReplicationRecord& OutRecord);

	/**
	 * Deserializes blobs with object using their respective Deserialize() method.
	 * @return The number of blobs that were deserialized.
	 * @see Serialize
	 */
	IRISCORE_API uint32 Deserialize(FNetSerializationContext& Context);

	/**
	 * Call after a packet containing serialized data was sent.
	 * @see SerializeWithObject, Serialize
	 *
	 * 中文：包真正进入网络后调用——把刚序列化的 blob 在 Sent 位图里打上"已发送"，
	 * 同时把 unreliable 立即释放（不重发）。Reliable 仍保留以便 Lost 时重发。
	 * 减少 UnsentBlobCount。
	 */
	IRISCORE_API void CommitReplicationRecord(const FReplicationRecord& Record);

	/**
	 * For each packet for which CommitReplicationRecord() was called ProcessPacketDeliveryStatus() needs
	 * to be called in the same order when it's known whether the packet was delivered or not.
	 * @param Status Whether the packet was delivered or not or if the record should simply be discarded due to closing a connection.
	 * @param Record The record that was obtained via a Serialize/SerializeWithObject call and passed to CommitReplicationRecord.
	 * @see CommitReplicationRecord
	 *
	 * 中文：包到达回执（ACK/Lost/Discard）。
	 *   * Delivered → SetIndexIsAcked + Release blob + 推进窗口（PopInOrderAckedBlobs）
	 *   * Lost      → ClearSequenceIsSent + UnsentBlobCount += Count（下次重发）
	 *   * Discard   → 等同 Delivered（关闭连接时清理）
	 */
	IRISCORE_API void ProcessPacketDeliveryStatus(EPacketDeliveryStatus Status, const FReplicationRecord& Record);

private:
	enum Constants : uint32
	{
		// 中文：环形索引位数 = log2(MaxUnackedBlobCount) = 10。
		IndexBitCount = 10U,
		// 中文：一个 record 最多 4 段不连续区间。
		MaxWriteSequenceCount = UE_ARRAY_COUNT(FReplicationRecord::Sequences),
		// 中文：单段最大长度 = 2^6 - 1 = 63（保留 0 表示"无效段"）。
		MaxSequenceLength = (1U << FReplicationRecord::FSequence::CountBitCount) - 1U,
	};

	uint32 SequenceToIndex(uint32 Seq) const;

	bool IsSequenceAcked(uint32 Seq) const;
	bool IsIndexAcked(uint32 Index) const;
	void SetIndexIsAcked(uint32 Index);
	void SetSequenceIsAcked(uint32 Index);
	void ClearIndexIsAcked(uint32 Index);

	bool IsSequenceSent(uint32 Seq) const;
	bool IsIndexSent(uint32 Index) const;
	void SetIndexIsSent(uint32 Index);
	void SetSequenceIsSent(uint32 Index);
	void ClearSequenceIsSent(uint32 Seq);
	void ClearIndexIsSent(uint32 Seq);

	bool IsValidReceiveSequence(uint32 Seq) const;

	uint32 SerializeInternal(FNetSerializationContext& Context, FNetRefHandle RefHandle, FReliableNetBlobQueue::FReplicationRecord& OutRecord, const bool bSerializeWithObject);
	uint32 DeserializeInternal(FNetSerializationContext& Context, FNetRefHandle RefHandle, const bool bSerializeWithObject);

	void OnPacketDelivered(const FReplicationRecord& Record);
	void OnPacketDropped(const FReplicationRecord& Record);

	void PopInOrderAckedBlobs();

	// 中文：环形数据存储。
	TRefCountPtr<FNetBlob> NetBlobs[MaxUnackedBlobCount];
	uint32 Sent[(MaxUnackedBlobCount + 31)/32];   // 中文：已发送位图（32 个 uint32 = 1024 位）
	uint32 Acked[(MaxUnackedBlobCount + 31)/32];  // 中文：已确认位图（接收侧表示"已收到"）
	uint32 FirstSeq;        // 中文：窗口左端（接收侧：下一个待 Pop；发送侧：最早未完全 ack 的 blob）
	uint32 LastSeq;         // 中文：窗口右端（exclusive）
	uint32 UnsentBlobCount; // 中文：发送侧——尚未写入任何包的 blob 数（含从未发的 + Lost 后待重发的）
};

// 中文：Sent/Acked 位图的位操作内联实现（性能关键路径）。

//
inline bool FReliableNetBlobQueue::IsSendWindowFull() const
{
	// 中文：窗口长度 = 1024 时禁止再 Enqueue。
	return (LastSeq - FirstSeq) >= MaxUnackedBlobCount;
}

inline uint32 FReliableNetBlobQueue::SequenceToIndex(uint32 Seq) const
{ 
	// 中文：序号 → 环形索引（NetBlobs 数组下标）。
	return Seq % MaxUnackedBlobCount;
}

inline bool FReliableNetBlobQueue::IsIndexAcked(uint32 Index) const
{
	return (Acked[Index >> 5U] & (1U << (Index & 31U))) != 0U;
}

inline bool FReliableNetBlobQueue::IsSequenceAcked(uint32 Seq) const
{
	return IsIndexAcked(SequenceToIndex(Seq));
}

inline void FReliableNetBlobQueue::SetIndexIsAcked(uint32 Index)
{
	Acked[Index >> 5U] |= (1U << (Index & 31U));
}

inline void FReliableNetBlobQueue::SetSequenceIsAcked(uint32 Seq)
{
	return SetIndexIsAcked(SequenceToIndex(Seq));
}

inline void FReliableNetBlobQueue::ClearIndexIsAcked(uint32 Index)
{
	Acked[Index >> 5U] &= ~(1U << (Index & 31U));
}

inline bool FReliableNetBlobQueue::IsIndexSent(uint32 Index) const
{
	return (Sent[Index >> 5U] & (1U << (Index & 31U))) != 0U;
}

inline bool FReliableNetBlobQueue::IsSequenceSent(uint32 Seq) const
{
	return IsIndexSent(SequenceToIndex(Seq));
}

inline void FReliableNetBlobQueue::SetIndexIsSent(uint32 Index)
{
	Sent[Index >> 5U] |= (1U << (Index & 31U));
}

inline void FReliableNetBlobQueue::SetSequenceIsSent(uint32 Seq)
{
	return SetIndexIsSent(SequenceToIndex(Seq));
}

inline void FReliableNetBlobQueue::ClearIndexIsSent(uint32 Index)
{
	Sent[Index >> 5U] &= ~(1U << (Index & 31U));
}

inline void FReliableNetBlobQueue::ClearSequenceIsSent(uint32 Seq)
{
	return ClearIndexIsSent(SequenceToIndex(Seq));
}

inline bool FReliableNetBlobQueue::IsValidReceiveSequence(uint32 Seq) const
{
	// 中文：接收侧合法性——序号必须在当前窗口内（FirstSeq ≤ Seq < FirstSeq + 1024）。
	// 超出 → 旧重复包 / 协议错位。
	return (Seq >= FirstSeq) & (Seq - FirstSeq < MaxUnackedBlobCount); // Must be less than MaxUnackedBlobCount as LastSeq == Seq + 1
}

}
