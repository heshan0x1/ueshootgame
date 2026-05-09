// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationRecord.h —— 已发送数据的"流水账"（用于 ACK / NACK 回滚）
// -----------------------------------------------------------------------------
// 角色：FReplicationWriter 每写一次对象都会产生一条 FRecordInfo（含 ChangeMask 副本/
// 基线版本/Attachment 索引/SubObject 销毁列表 等），并入两条循环队列：
//   1) RecordInfos —— 全局环形队列，按"写入时间"递增；
//   2) FRecordInfoList —— 按对象（PerObjectInfo）的双向索引链表，把同一对象历次写入串起来；
// 当 NetDriver 回报包 Delivered/Lost 时，按"包内 RecordCount"批量出队，并据此：
//   • Delivered → 推进 LastAckedBaselineIndex、清掉对应 inflight ChangeMask；
//   • Lost      → 把 ChangeMask 重新合并回对象的 dirty mask（"回滚"），并触发重发。
//
// 三层数据：
//   • Record         一个包的"摘要" = 该包包含多少条 RecordInfo（uint16）
//   • RecordInfo     单次写入的细节（16 字节紧凑结构，含 ChangeMaskOrPtr / Index / 状态等）
//   • Attachment/SubObject 副本队列 —— 当 RecordInfo.HasAttachments / HasSubObjectRecord
//                    为 1 时，需要从 AttachmentRecords / SubObjectRecords 同步出队一项。
//
// 容量：MaxReplicationRecordCount = 65535（足以覆盖 256 包窗口 × 平均 256 条/包）。
//
// 数据结构选型注意：FRecordInfo 严格 16 字节对齐（assertion 在文件中），
// ChangeMaskOrPtr 是 union（小 ChangeMask 内联，大 ChangeMask 用指针 → 见 ChangeMaskUtil）。
// =============================================================================

#pragma once

#include "Containers/Array.h"
#include "Iris/ReplicationSystem/ChangeMaskUtil.h"
#include "Net/Core/Misc/ResizableCircularQueue.h"

namespace UE::Net::Private
{

class FReplicationRecord
{
public:
	// We should get away with 64k records in flight given a packet window of 256 and an average of 256 replicated object destroys, objects and subobjects per packet
	// 16-bit 索引足够：256 包 × 256 条 ≈ 65k；超过则需上限保护。
	typedef uint16 ReplicationRecordIndex;
	static constexpr ReplicationRecordIndex InvalidReplicationRecordIndex = 65535U;
	// The MaxReplicationRecordCount should account for at least 256 packets with N records per replicated object or destroyed object. With lots of destroyed objects you could end up with maybe 300-400 records for a single packet.
	static constexpr ReplicationRecordIndex MaxReplicationRecordCount = InvalidReplicationRecordIndex;

	// FRecordInfo 紧凑 bitfield 中的字段宽度
	static constexpr uint32 ObjectIndexBitCount = 20U;        // 支持最多 ~1M 个 NetObject
	static constexpr uint32 ReplicatedObjectStateBitCount = 5U; // 见 FReplicationWriter::EReplicatedObjectState (≤32)

	// Circular queue, indexing into record infos
	// 一个包的"摘要"：该包写入了多少条 RecordInfo（与 RecordInfos 同步出队即可批量回滚）
	struct FRecord
	{
		uint16 RecordCount;
	};

	// SubObjectRecord：当一次写入伴随大量 SubObject 销毁（例如根对象销毁时附带销毁所有子对象），
	// 把这些 SubObject 的 (Index, State) 列表保存下来，以便 ACK/NACK 时能恢复或确认它们的状态。
	struct FSubObjectRecord
	{
		struct FSubObjectInfo
		{
			uint32 Index : ObjectIndexBitCount;
			uint32 ReplicatedObjectState : ReplicatedObjectStateBitCount;
		};

		TArray<FSubObjectInfo> SubObjectInfos;
	};

	// $IRIS: implement some some sort of low overhead chunked fifo array allocator for changemasks used for the replication record
	// They tend to be relatively short lived and are always allocated and freed in the same order.
	// for smaller changemasks we use inlined storage.
	// TODO（来自原作者）：用 chunked FIFO allocator 替换当前 ChangeMask 分配。
	// 因为 ChangeMask 寿命短且严格按时间序申请释放，可以做一个零碎片的环形池。

	// We want to keep this state as small as possible as we will have many of them
	// 严格 16 字节：每个对象每次写入都会产生一条，需要尽量紧凑。
	struct FRecordInfo
	{
		FChangeMaskStorageOrPointer ChangeMaskOrPtr;	// used for ChangeMask storage or a pointer to the changemask (could be repurposed to always be an index to save space)
		uint32 Index : ObjectIndexBitCount;				// Index in the ReplicationInfo array, effectively identifying the object
		uint32 ReplicatedObjectState : ReplicatedObjectStateBitCount; // Encode EReplicatedObjectState using as few bits as we can
		uint32 HasChangeMask : 1;						// Do we have a changeMask? 
		uint32 HasAttachments : 1;						// If this flag is set there's an associated AttachmentRecord（需要从 AttachmentRecords 出队一项）
		uint32 WroteTearOff : 1;						// If this flag is set, we wrote TearOff
		uint32 WroteDestroySubObject : 1;				// If this flag is set, we wrote DestroySubObject
		uint32 NewBaselineIndex : 2;					// This is a new Baseline pending ack（与 DeltaCompressionBaselineManager 协作；ACK 后把 LastAckedBaseline 推进到此值）
		uint32 HasSubObjectRecord : 1;					// If this flag is set there's an associated SubObjectRecord（需要从 SubObjectRecords 出队一项）
		ReplicationRecordIndex NextIndex;				// Points to next older record index   PerObject 链表中下一条更老的记录
		uint16 Padding : 16;
	};

	static_assert(sizeof(FRecordInfo) == 16, "Expected sizeof FRecordInfo to be 16 bytes");
	static_assert(sizeof(ReplicationRecordIndex) == 2, "Need to remove or adjust Padding field in FRecordInfo");

public:

	// Simple index based linked list used to track in flight data
	// Per-Object 的双向锚点：FirstRecordIndex 是该对象最老一条 in-flight 记录，
	// LastRecordIndex 是最新一条；通过 FRecordInfo.NextIndex 串起整条链。
	struct FRecordInfoList
	{
		inline FRecordInfoList();

		ReplicationRecordIndex LastRecordIndex;		// Index of the last written index for this object, used to chain replication infos
		ReplicationRecordIndex FirstRecordIndex;	// First index in flight (oldest), used to quickly be able to iterate over all changes in flight
	};

	// Pop RecordInfo from record and remove it from the provided RecordInfoList
	// 从全局队首出队，并把它从 RecordList 链表上摘下（典型场景：包被 Delivered/Lost）
	inline void PopInfoAndRemoveFromList(FRecordInfoList& RecordList);

	// Push RecordInfo to record and add it to the provided RecordInfoList
	// 写入新 RecordInfo，并追加到 RecordList 末端；若 HasAttachments 则同步入队 AttachmentRecord
	inline void PushInfoAndAddToList(FRecordInfoList& RecordList, const FRecordInfo& RecordInfo, uint64 AttachmentRecord = 0);

	// Push RecordInfo to record and add it to the provided RecordInfoList. Call this when there's an associated SubObjectRecord.
	// 同上但携带 SubObjectRecord（会同步入队 SubObjectRecords）
	inline void PushInfoAndAddToList(FRecordInfoList& RecordList, const FRecordInfo& RecordInfo, const FSubObjectRecord& SubObjectRecord);

	// Reset a RecordInfoList
	inline void ResetList(FRecordInfoList& RecordList);

public:
	inline FReplicationRecord();

	// Get the info for the provided index
	// 通过环形索引访问 RecordInfo（O(1)，相对 FrontIndex 的偏移）
	inline FRecordInfo* GetInfoForIndex(ReplicationRecordIndex Index);
	inline const FRecordInfo* GetInfoForIndex(ReplicationRecordIndex Index) const;

	// PeekInfo. If the info indicates there's an attachment record one must call DequeueAttachmentRecord(). If the info indicates there's a SubObjectRecord one must call DequeueSubObjectRecord().
	// 读取队首；调用方需要根据 HasAttachments / HasSubObjectRecord 判断是否还需 DequeueXxx
	inline const FRecordInfo& PeekInfo() const { return RecordInfos.Peek(); }
	inline const FRecordInfo& PeekInfoAtOffset(uint32 Offset) const { return RecordInfos.PeekAtOffset(Offset); }
	inline const uint32 GetInfoCount() const { return static_cast<uint32>(RecordInfos.Count()); }
	inline const uint32 GetUnusedInfoCount() const { return static_cast<uint32>(MaxReplicationRecordCount) - static_cast<uint32>(RecordInfos.Count()); }

	// If the info from PeekInfo() indicates there's an attachment record one needs to call this function as well.
	// 与 PeekInfo()/PopInfoAndRemoveFromList 配对：HasAttachments=1 时必须额外取一项 AttachmentRecord
	uint64 DequeueAttachmentRecord();

	// If the info from PeekInfo() indicates there's a subobject record one needs to call this function as well.
	// 与 PeekInfo()/PopInfoAndRemoveFromList 配对：HasSubObjectRecord=1 时必须额外取一项 SubObjectRecord
	FSubObjectRecord DequeueSubObjectRecord();

	// FrontIndex
	// 当前队首的全局索引（用作"环形偏移基准"）
	inline ReplicationRecordIndex GetFrontIndex() const { return FrontIndex; }

	// Push a record, currently the record is simply a count of how mane RecordInfos we stored for the record
	// 包结束时调用：把"该包总共写了多少条 RecordInfo"入队
	inline void PushRecord(uint16 InfoCount);

	// Pop a record
	// ACK/NACK 时调用：取出包对应的 InfoCount，外部据此调用 PopInfoAndRemoveFromList N 次
	inline uint16 PopRecord();

	inline const uint32 PeekRecordAtOffset(uint32 Offset) const { return Record.PeekAtOffset(Offset); }

	// Num Records
	// 当前 in-flight 的包数（≤ NetDriver 包窗口）
	inline const uint32 GetRecordCount() const { return static_cast<uint32>(Record.Count()); }
	
private:

	// Push Info to queue, the index of the info will be returned, as long as the info is valid it can be retrieved by the index
	// 入队，返回环形绝对索引（与 FrontIndex 相对运算可定位到队中位置）
	inline ReplicationRecordIndex PushInfo(const FRecordInfo& Info);

	// PopInfo
	// 出队（仅 RecordInfos 主队列），FrontIndex 自增
	inline void PopInfo();

private:
	// Storage for RecordInfos
	// RecordInfo 主环形队列（包级 ACK 时按 InfoCount 批量出队）
	TResizableCircularQueue<FRecordInfo> RecordInfos;

	// Storages for the Record for each packet
	// 每个包对应一个 uint16（=该包内 InfoCount），用于 ACK/NACK 批量出队
	TResizableCircularQueue<uint16> Record;

	// Storage for attachment records
	// 与 RecordInfos 同步消费的"附件记录"队列（仅当 HasAttachments 才会有项）；
	// 附件记录是 64-bit 不透明值（FNetObjectAttachmentsWriter 内部含义）
	TResizableCircularQueue<uint64> AttachmentRecords;

	// Storage for minimalistic subobject records, used by for example object destruction.
	// 同步消费的 SubObject 记录队列（仅当 HasSubObjectRecord 才有项）；
	// 主要场景：根对象销毁时同步销毁子对象，需要记录哪些子对象的状态被一并处理
	TResizableCircularQueue<FSubObjectRecord> SubObjectRecords;

	// Current Index at the oldest Record in the queue, this is used to do relative indexing into the queue when linking pushed records
	// 当前队首的全局索引（每次 PopInfo 自增；环形使用 % MaxReplicationRecordCount 包装）
	ReplicationRecordIndex FrontIndex;
};

// 构造：四条循环队列预分配（容量按经验值给）
FReplicationRecord::FReplicationRecord()
: RecordInfos(1024)
, Record(256)
, AttachmentRecords(64)
, SubObjectRecords(128)
, FrontIndex(0u)
{
}

// 给定环形索引（绝对值），转换为相对队首的偏移并访问。Index 不合法时返回 nullptr。
FReplicationRecord::FRecordInfo* FReplicationRecord::GetInfoForIndex(ReplicationRecordIndex Index)
{
	if (Index != InvalidReplicationRecordIndex)
	{
		// (MaxReplicationRecordCount + Index - FrontIndex) % MaxReplicationRecordCount
		// 加 MaxReplicationRecordCount 是为了避免 Index<FrontIndex 时减法下溢
		const uint32 Offset = uint32(MaxReplicationRecordCount + Index - FrontIndex) % uint32(MaxReplicationRecordCount);
		return &RecordInfos.PokeAtOffsetNoCheck(Offset);
	}

	return nullptr;
}

const FReplicationRecord::FRecordInfo* FReplicationRecord::GetInfoForIndex(ReplicationRecordIndex Index) const
{
	if (Index != InvalidReplicationRecordIndex)
	{
		const uint32 Offset = uint32(MaxReplicationRecordCount + Index - FrontIndex) % uint32(MaxReplicationRecordCount);
		return &RecordInfos.PeekAtOffsetNoCheck(Offset);
	}

	return nullptr;
}

// 取下一项 AttachmentRecord（与 PopInfoAndRemoveFromList 同节奏调用）
inline uint64 FReplicationRecord::DequeueAttachmentRecord()
{
	const uint64 AttachmentRecord = AttachmentRecords.Peek();
	AttachmentRecords.Pop();
	return AttachmentRecord;
}

// 取下一项 SubObjectRecord（typename 指针返回值，因 TArray 较重故使用 MoveTemp）
inline FReplicationRecord::FSubObjectRecord FReplicationRecord::DequeueSubObjectRecord()
{
	FSubObjectRecord SubObjectRecord = MoveTemp(SubObjectRecords.Poke());
	SubObjectRecords.Pop();
	return SubObjectRecord;
}

// 包结束：把"该包共写了多少条 RecordInfo"作为一项入队
void FReplicationRecord::PushRecord(uint16 InfoCount)
{
	Record.Enqueue(InfoCount);
}

// 包反馈：取出最老一包的 InfoCount，调用方据此 PopInfoAndRemoveFromList N 次
uint16 FReplicationRecord::PopRecord()
{
	check(Record.Count());

	const uint16 InfoCount = Record.Peek();
	Record.Pop();

	return InfoCount;
}

// 写入新 RecordInfo（NextIndex 暂置 Invalid，由调用方 PushInfoAndAddToList 串入链表后再补）
FReplicationRecord::ReplicationRecordIndex FReplicationRecord::PushInfo(const FRecordInfo& Info)
{ 
	const SIZE_T CurrentInfoCount = RecordInfos.Count();

	check(CurrentInfoCount < MaxReplicationRecordCount);

	FRecordInfo& NewInfo = RecordInfos.EnqueueDefaulted_GetRef();
	NewInfo = Info;
	NewInfo.NextIndex = InvalidReplicationRecordIndex;

	// 返回环形绝对索引：(FrontIndex + 当前队列长度) % Max
	return (FrontIndex + CurrentInfoCount) % MaxReplicationRecordCount;
}

void FReplicationRecord::PopInfo()
{ 
	FrontIndex = (FrontIndex + 1U) % MaxReplicationRecordCount;
	RecordInfos.Pop();
};

// 出队最老一条，并把它从对应对象的 RecordList 上摘掉。
//   - 若被摘的是 First → First = NextIndex；
//   - 若被摘的还是 Last（即整条链只有它一条）→ Last 重置为 Invalid。
void FReplicationRecord::PopInfoAndRemoveFromList(FRecordInfoList& RecordList)
{
	// This is the record at the front of the Record.
	const ReplicationRecordIndex RecordIndex = GetFrontIndex();
	const FRecordInfo& RecordInfo = PeekInfo();

	// unlink
	RecordList.FirstRecordIndex = RecordInfo.NextIndex;
	RecordList.LastRecordIndex = (RecordList.LastRecordIndex == RecordIndex) ? InvalidReplicationRecordIndex : RecordList.LastRecordIndex;

	PopInfo();
}

// 写入并追加到 RecordList 末端 + 同步入队 AttachmentRecord（若 HasAttachments=1）。
// 链表维护：
//   • 若 RecordList 已有 Last → 把 Last.NextIndex 指向新 Index，并更新 Last；
//   • 否则该对象首次入流：First/Last 同时指向新 Index。
void FReplicationRecord::PushInfoAndAddToList(FRecordInfoList& RecordList, const FRecordInfo& RecordInfo, uint64 AttachmentRecord)
{
	FReplicationRecord::ReplicationRecordIndex NewIndex = PushInfo(RecordInfo);
	if (RecordInfo.HasAttachments)
	{
		AttachmentRecords.Enqueue(AttachmentRecord);
	}

	if (FRecordInfo* LastRecord = GetInfoForIndex(RecordList.LastRecordIndex))
	{
		// Link already in flight record to this newIndex
		LastRecord->NextIndex = NewIndex;
		RecordList.LastRecordIndex = NewIndex;
	}
	else
	{
		// If this is the FirstRecord we update it as well.
		RecordList.FirstRecordIndex = NewIndex;
		RecordList.LastRecordIndex = NewIndex;
	}
}

// 同上，但携带 SubObjectRecord 副本（销毁场景）
void FReplicationRecord::PushInfoAndAddToList(FRecordInfoList& RecordList, const FRecordInfo& RecordInfo, const FSubObjectRecord& SubObjectRecord)
{
	FReplicationRecord::ReplicationRecordIndex NewIndex = PushInfo(RecordInfo);
	if (RecordInfo.HasSubObjectRecord)
	{
		SubObjectRecords.Enqueue(SubObjectRecord);
	}

	if (FRecordInfo* LastRecord = GetInfoForIndex(RecordList.LastRecordIndex))
	{
		// Link already in flight record to this newIndex
		LastRecord->NextIndex = NewIndex;
		RecordList.LastRecordIndex = NewIndex;
	}
	else
	{
		// If this is the FirstRecord we update it as well.
		RecordList.FirstRecordIndex = NewIndex;
		RecordList.LastRecordIndex = NewIndex;
	}
}

// 重置 PerObject 链表（StopReplication / StartReplication 时使用）
void FReplicationRecord::ResetList(FRecordInfoList& RecordList)
{
	RecordList.FirstRecordIndex = InvalidReplicationRecordIndex;
	RecordList.LastRecordIndex = InvalidReplicationRecordIndex;
}

FReplicationRecord::FRecordInfoList::FRecordInfoList()
: LastRecordIndex(InvalidReplicationRecordIndex)
, FirstRecordIndex(InvalidReplicationRecordIndex)
{
}

}
