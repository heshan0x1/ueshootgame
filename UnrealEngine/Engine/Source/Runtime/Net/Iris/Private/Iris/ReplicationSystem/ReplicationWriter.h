// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationWriter.h —— 发送端核心（每连接一份）
// -----------------------------------------------------------------------------
// 整体架构：
//   • 维护每对象 FReplicationInfo（16 字节紧凑）+ 状态机 + ChangeMask；
//   • 每帧 UpdateScope（与 Filtering 输出一致）+ UpdateDirtyChangeMasks（应用 ChangeMaskCache）；
//   • Write 阶段按 priority 选取对象，并以 batch 形式串行写入；
//   • 单对象太大放不进 MTU → 走 HugeObject 通道（拆分为多个 NetBlob 分片可靠发送）；
//   • FReplicationRecord 记录所有 in-flight 数据，配合 ProcessDeliveryNotification 做 ACK/NACK 回滚。
//
// 状态机（FReplicationInfo::State，5 bit 编码 EReplicatedObjectState）：
//
//   ┌─────────────────┐
//   │  PendingCreate  │  对象进入 scope，尚未发送 creation
//   └────────┬────────┘
//            │ Write 出 creation
//            ▼
//   ┌─────────────────────────┐
//   │ WaitOnCreateConfirmation│  已发送，等 ACK；可继续发状态但需附带 creation 信息
//   └────────────┬────────────┘
//                │ Delivered（ACK）
//                ▼
//   ┌─────────────────┐
//   │     Created     │  正常 replicating
//   └─┬───────────┬───┘
//     │           │
//     │ TearOff   │ ScopeRemoved/Destroyed
//     ▼           ▼
//   ┌──────────────┐  ┌────────────────────────┐
//   │PendingTearOff│  │  PendingDestroy /      │
//   │              │  │  SubObjectPendingDestroy│
//   └──────┬───────┘  └────────┬───────────────┘
//          │                   │ Write destroy
//          ▼                   ▼
//   ┌──────────────────────────────────┐
//   │     WaitOnDestroyConfirmation    │  等 ACK；丢失则重发，确认则进入 Destroyed
//   └────────────────┬─────────────────┘
//                    ▼
//                Destroyed
//
//   特殊状态：
//     • WaitOnFlush          停止前等所有 in-flight 状态被 ACK（Reliable RPC / state）
//     • CancelPendingDestroy 已送 destroy，对象又被加回 scope，需取消销毁
//     • PermanentlyDestroyed DestructionInfo（static actor）已确认
//     • AttachmentToObjectNotInScope  特殊"OOB 附件载体" slot（InternalIndex=0）
//     • HugeObject           HugeObject 通道占用的特殊 slot
//
// PerObject 调度优先级：每帧 SchedulingPriority 累加（来自 Prioritization），
// 写入成功后归零；SchedulingThresholdPriority 控制是否值得写。
// CreatePriority/TearOffPriority/LostStatePriorityBump 是 bump 系数。
//
// HugeObject 通道（FHugeObjectSendQueue）：
//   - 单对象超过 MTU → PrepareAndSendHugeObjectPayload 切分为 N 个 NetBlob 分片；
//   - 分片走 PartialNetObjectAttachmentHandler 序列化后由 NetBlobManager 可靠发送；
//   - 接收端 NetBlobAssembler 重组完成后再触发 batch 解析；
//   - SendQueue 跟踪所有"in transit"的 root 对象，避免同一对象的两次 huge 重叠。
//
// FReplicationRecord 协作：每写一个对象（含 SubObject），生成 FObjectRecord（Record + AttachmentRecord），
// 提交后串入 PerObject FRecordInfoList；ProcessDeliveryNotification 时按 InfoCount 批量出队：
//   • Delivered → HandleDeliveredRecord（推进 LastAckedBaselineIndex / 释放 ChangeMask）；
//   • Lost      → HandleDroppedRecord（把 ChangeMask 重新合并回 ObjectsWithDirtyChanges → 重发）；
//   • Discarded → HandleDiscardedRecord（断开/清理路径，仅释放资源不重发）。
//
// 与外部子系统协作：
//   • FNetExports          每连接的 NetToken/ObjectReference 导出 ACK 跟踪
//   • FObjectReferenceCache 序列化 UObject 引用时翻译 NetRefHandle
//   • FNetBlobManager / Handlers Attachment / RPC / HugeObject 分片
//   • FDeltaCompressionBaselineManager 基线创建/失效
//   • FReplicationFiltering 提供 RelevantObjectsInScope（UpdateScope 输入）
//   • FReplicationConditionals 计算 conditional ChangeMask（按连接屏蔽位）
// =============================================================================

#pragma once

// This class will never be included from public headers
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationTypes.h"
#include "Iris/ReplicationSystem/ReplicationRecord.h"
#include "Iris/DataStream/DataStream.h"
#include "Iris/ReplicationSystem/AttachmentReplication.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/NetExports.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ReplicationDataStreamDebug.h"
#include "Iris/Stats/NetStats.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/List.h"
#include "Containers/Set.h"
#include "Misc/EnumClassFlags.h"

// Forward declaration
class UReplicationSystem;
class UNetObjectBlobHandler;
class UPartialNetObjectAttachmentHandler;
class UObjectReplicationBridge;

namespace UE::Net
{
	class FNetBitStreamReader;
	class FNetBitStreamWriter;
	class FNetObjectAttachment;
	class FNetSerializationContext;
	struct FReplicationProtocol;
	
	namespace Private
	{
		struct FChangeMaskCache;
		class FNetRefHandleManager;
		class FReliableNetBlobQueue;
		class FReplicationConditionals;
		class FReplicationFiltering;
		class FReplicationSystemInternal;
		class FDeltaCompressionBaselineManager;
		class FDeltaCompressionBaseline;
	}
}

namespace UE::Net::Private
{

class FReplicationWriter
{
public:
	// Scheduling constants
	// 调度常量：每帧 priority 累加；写成功归零。下面是 bump 系数：
	static constexpr float CreatePriority = 1.f;            // 新对象首次发送的 priority bump
	static constexpr float TearOffPriority = 1.f;           // TearOff 时 bump
	static constexpr float LostStatePriorityBump = 1.f;     // 丢包重发 bump（让丢包对象优先级提升）
	static constexpr float SchedulingThresholdPriority = 1.f; // 低于此阈值的对象不参与本帧调度

	// When scheduling there is no point in scheduling more objects than we can fit in a packet
	// 部分排序：单包能塞下的对象数大概上限（partial sort 提升性能）
	static const uint32 PartialSortObjectCount = 128u;

public:

	// State
	// 5-bit 紧凑状态枚举（与 FReplicationRecord::FRecordInfo::ReplicatedObjectState 共用编码）。
	enum class EReplicatedObjectState : uint8
	{
		Invalid = 0,

		// Special states
		AttachmentToObjectNotInScope,	// Special state for object index used for sending attachments to remote owned objects   特殊 slot（InternalIndex=0）：承载 OOB 附件
		HugeObject,						// Special state for object index used for sending parts of huge object payloads        特殊 slot：HugeObject 分片通道

		// Normal states
		PendingCreate,					// Not yet created, no data in flight                                          未发送 creation，无 in-flight
		WaitOnCreateConfirmation,		// Waiting for confirmation from remote, we can send state data, but if we do we must also include creation info until the object is Created
		Created,						// Confirmed by remote, this is the normal replicating state                   正常 replicating
		WaitOnFlush,					// Pending flush, we are waiting for all in-flight state data to be acknowledged 等所有 in-flight 状态/RPC 被确认（用于优雅 EndReplication）
		PendingTearOff,					// TearOff should be sent                                                       即将发送 TearOff（"断开 replication 但保留实例"）
		SubObjectPendingDestroy,		// SubObject destroy should be sent                                             子对象销毁待发送
		CancelPendingDestroy,			// Destroy was sent but object wants to start replicating again                 已发送 destroy 但对象又被加回 scope（取消销毁）
		PendingDestroy,					// Object is set to be destroyed                                                 待发送销毁
		WaitOnDestroyConfirmation,		// Destroy has been sent, waiting for response from client                       已发送 destroy，等 ACK
		Destroyed,						// Confirmed as Destroyed,                                                       销毁已确认
		PermanentlyDestroyed,			// DestructionInfo has been confirmed as received                                static actor 的 DestructionInfo 已确认

		Count
	};

	// 必须 ≤32（5 bit）—— 与 ReplicationRecord 的位字段宽度耦合
	static_assert((uint8)(EReplicatedObjectState::Count) <= 32, "EReplicatedObjectState must fit in 5 bits. See FReplicationInfo::State and FReplicationRecord::FRecordInfo::ReplicatedObjectState members.");

	static const TCHAR* LexToString(const EReplicatedObjectState State);

	// 优雅 EndReplication 所需的 flush 选项位（FlushReliable 默认开启）
	enum EFlushFlags : uint32
	{
		FlushFlags_None				= 0U,
		FlushFlags_FlushState		= 1U << 0U,											// Make sure that all current state data is acknowledged before we stop replicating the object
		FlushFlags_FlushReliable	= FlushFlags_FlushState << 1U,						// Make sure that all enqueued Reliable RPCs are delivered before we stop replicating the object
		FlushFlags_FlushTornOffSubObjects	= FlushFlags_FlushReliable << 1U,					// Make sure that we flush TearOff and replicated destroy properly
		FlushFlags_All				= FlushFlags_FlushState | FlushFlags_FlushReliable | FlushFlags_FlushTornOffSubObjects,
		FlushFlags_Default			= FlushFlags_FlushReliable,
	};

	// Bookkeeping info required for a replicated object
	// Keep as small as possible since there is one per replicated object per connection
	// Changemask can and will most likely be replaced by a smaller index into a pool to reduce overhead
	// 严格 16 字节：每对象每连接一份 → 必须紧凑。位段含义见各成员。
	struct FReplicationInfo
	{
		inline FReplicationInfo();

		FChangeMaskStorageOrPointer ChangeMaskOrPtr;			// Changemask storage or pointer to storage	（小 mask 内联 / 大 mask 走指针池）
		union 
		{
			uint64 Value;
			struct 
			{
				uint64 ChangeMaskBitCount : 16;							// This is cached to avoid having to look it up in the protocol
				uint64 State : 5;										// Current state    见 EReplicatedObjectState（5 bit）
				uint64 HasDirtySubObjects : 1;							// Set if this object has dirty subobjects
				uint64 IsSubObject : 1;									// Set if this object is a subobject
				uint64 HasDirtyChangeMask : 1;							// Set if the ChangeMask might be dirty, if not set the changemask should be zero!
				uint64 HasAttachments : 1;								// Set if there are attachments, such as RPCs, waiting to be sent
				uint64 HasChangemaskFilter : 1;							// Do we need to filter our changemask or not    LifetimeCondition / Conditional 过滤
				uint64 IsDestructionInfo : 1;							// If this is a destruction info    static actor 销毁信息载体
				uint64 IsCreationConfirmed : 1;							// We know that this object has been created on the receiving end
				uint64 TearOff : 1;										// This object should be torn off
				uint64 SubObjectPendingDestroy : 1;						// This object is a subobject that should be destroyed when we replicate owner
				uint64 IsDeltaCompressionEnabled : 1;					// Set to 1 if deltacompression is enabled for this object
				uint64 LastAckedBaselineIndex : 2;						// Last acknowledged baseline index which we can use for deltacompresion
				uint64 PendingBaselineIndex : 2;						// Baseline index pending acknowledgment from client
				uint64 FlushFlags : 3;									// Flags indicating what we are waiting for when flushing
				uint64 HasDirtyConditionals : 1;						// If this flag is set, we must update conditionals.
				mutable uint64 HasCannotSendInfo : 1;					// If this flag is set, the object has been prevented from being sent at least once.
			};
		};

		// 内联 ChangeMask 的最大位数；超过则走指针 + 池
		static const uint32 LocalChangeMaskMaxBitCount = 64u;

		EReplicatedObjectState GetState() const { return (EReplicatedObjectState)State; }

		ChangeMaskStorageType* GetChangeMaskStoragePointer() { return ChangeMaskOrPtr.GetPointer(ChangeMaskBitCount); }
		const ChangeMaskStorageType* GetChangeMaskStoragePointer() const { return ChangeMaskOrPtr.GetPointer(ChangeMaskBitCount); }
	};

	static_assert(sizeof(FReplicationInfo) == 16, "Expected sizeof FReplicationInfo to be 16 bytes");

	// 跟踪某对象"被卡住不能发送"的诊断信息（典型场景：某依赖始终未满足）
	struct FCannotSendInfo
	{
		uint64 StartCycles = 0;
		uint32 SuppressWarningCounter = 0U;
	};

public:
	~FReplicationWriter();

	// Init
	// 由 ReplicationDataStream::Init 调用，缓存内部子系统指针 + 初始化容器
	void Init(const FReplicationParameters& InParameters);

	void Deinit();

	// Update new or existing/destroyed
	// 输入：本帧 RelevantObjectsInScope（来自 Filtering）；
	// 处理：新进 scope → StartReplication；离开 scope → 进入 PendingDestroy 等
	void UpdateScope(const FNetBitArrayView& ScopedObjects);

	// Force update DirtyChangeMasks and mark objects for flush and/or tearoff depending on flags
	void ForceUpdateDirtyChangeMasks(const FChangeMaskCache& CachedChangeMasks, EFlushFlags ExtraFlushFlags, bool bMarkForTearOff) { InternalUpdateDirtyChangeMasks(CachedChangeMasks, ExtraFlushFlags, bMarkForTearOff); }

	// Called if an object first being teared-off/flushed and then explicitly destroyed before it has been removed from scope
	void NotifyDestroyedObjectPendingEndReplication(FInternalNetRefIndex ObjectInternalIndex);

	// Called to propagate changes to global lifetime conditionals
	void UpdateDirtyGlobalLifetimeConditionals(TArrayView<FInternalNetRefIndex> ObjectsWithDirtyConditionals);

	// Propagate dirty changemasks
	// 把 ChangeMaskCache 中合并所得的 dirty bit 应用到 PerObject ChangeMaskOrPtr
	void UpdateDirtyChangeMasks(const FChangeMaskCache& CachedChangeMasks) { InternalUpdateDirtyChangeMasks(CachedChangeMasks, EFlushFlags::FlushFlags_None, false); }

	// Returns objects that are in need of a priority update. It could be dirty, new or objects in need of resending.
	const FNetBitArray& GetObjectsRequiringPriorityUpdate() const;

	// UpdatedPriorities contains priorities for all objects. Objects in need of a priority update should use the newly calculated priorities.
	// 把 Prioritization 算出的优先级合并到 SchedulingPriorities[]（注意是 += 累加）
	void UpdatePriorities(const float* UpdatedPriorities);

	UDataStream::EWriteResult BeginWrite(const UDataStream::FBeginWriteParameters& Params);

	// WriteData to Packet, returns true for now if data was written
	// 主入口：尝试在当前 packet 内写入尽可能多的 batch（HugeObject 优先 → OOB → 普通对象）
	UDataStream::EWriteResult Write(FNetSerializationContext& Context);

	void EndWrite();

	// Deal with processing of lost and delivered data.
	// ACK/NACK 入口：从 ReplicationRecord 出队最老一包的 InfoCount，对每条 RecordInfo 调用 HandleXxxRecord
	void ProcessDeliveryNotification(EPacketDeliveryStatus PacketDeliveryStatus);

	void SetReplicationEnabled(bool bInReplicationEnabled);
	bool IsReplicationEnabled() const;

	void SetNetExports(FNetExports& InNetExports);

	// Attachments
	// Queue NetObjectAttachments, returns whether the attachments was enqueued or not.
	// 入队附件（RPC / Subobject Attachments）；返回 false 时上层应回退（队列满或对象不可达）
	bool QueueNetObjectAttachments(FInternalNetRefIndex OwnerInternalIndex, FInternalNetRefIndex SubObjectInternalIndex, TArrayView<const TRefCountPtr<FNetBlob>> Attachments, ENetObjectAttachmentSendPolicyFlags SendFlags);

	// 优雅断开判断条件：Reliable 附件全部送达且 ACK
	bool AreAllReliableAttachmentsSentAndAcked() const;

	void Update(const UDataStream::FUpdateParameters& Params);

	[[nodiscard]] FString PrintObjectInfo(FInternalNetRefIndex ObjectIndex) const;

private:
	// Various types

	enum : uint32
	{
		// 特殊"对象槽"：用于"对不在 scope 的对象发送 OOB 附件"
		ObjectIndexForOOBAttachment = 0U,
	};

	// Propagate dirty changemasks
	void InternalUpdateDirtyChangeMasks(const FChangeMaskCache& CachedChangeMasks, EFlushFlags ExtraFlushFlags, bool bTearOff);

	// 调度槽：对象索引 + 排序键（priority + bumps）
	struct FScheduleObjectInfo
	{
		uint32 Index;
		float SortKey;
	};

	// We persist some information during write so that we can support writing multiple packets with data without re-doing scheduling work.
	// WriteContext 跨多包持续：当一帧内可能写多个包时，避免每包都重做调度排序。
	struct FWriteContext
	{
		FWriteContext() : bIsValid(0) {}

		// Objects we have written in this packet batch to avoid sending same object multiple times
		// 本"包批次"已写过的对象，避免一帧多次写入
		FNetBitArray ObjectsWrittenThisPacket;
		// DependentObjets that we should try to write this packet batch, aka. allow overcommit if we have pending DependentObjects when the packet is full
		// 依赖对象：父对象写入时一并要求发送；包满时允许 over-commit
		TArray<uint32, TInlineAllocator<32>> DependentObjectsPendingSend;
		// Scheduled objects
		FScheduleObjectInfo* ScheduledObjectInfos;
		uint32 ScheduledObjectCount;

		// For performance sake we do partial sorting so we need to track how many object we have sorted.
		// 部分排序：只对优先级最高的 PartialSortObjectCount 做完整排序
		uint32 SortedObjectCount;
		// The index into the scheduled objects array to attempt to replicate next.
		uint32 CurrentIndex;

		// Written batch count, which excludes objects pending destroy and subobjects
		uint32 WrittenBatchCount;

		// How many objects that were attempted to be replicated but which ultimately didn't fit in the packet.
		uint32 FailedToWriteSmallObjectCount;

		// How many root object destroys and destruction infos have been replicated.
		uint32 WrittenDestroyObjectCount;

		// How many packets have we written to?
		uint32 NumWrittenPacketsInThisBatch = 0U;

		// How many packets are we allowed to send in this batch. Based off the netspeed limit. When 0 = unlimited packets
		uint32 MaxPacketsToSend = 1U;

		EDataStreamWriteMode WriteMode = EDataStreamWriteMode::Full;	
	
		// Whether we're running low on ReplicationRecords or not. If starving only OOB and huge objects are sent, if at all possible.
		// "Record 饥饿"：FReplicationRecord 容量接近上限 → 退化模式只发 OOB/HugeObject
		uint32 bIsInReplicationRecordStarvation : 1;
		// Whether there are destroyed objects to send or not
		uint32 bHasDestroyedObjectsToSend : 1;
		// Whether there are dirty objects to send or not
		uint32 bHasUpdatedObjectsToSend : 1;
		// Whether there is at least one huge object to send or not
		uint32 bHasHugeObjectToSend : 1;
		// Whether there are OOB attachments to send or not
		uint32 bHasOOBAttachmentsToSend : 1;
		// Whether this packet is mainly intended for OOB and HugeObject data
		uint32 bIsOOBPacket : 1;
		// Whether this context is valid or not
		uint32 bIsValid : 1;

		FNetSendStats Stats;
	};

	// 一次 batch 中"对象级"写入信息（既包含父对象也包含本 batch 内的所有 sub-object）
	struct FBatchObjectInfo
	{
		FNetRefHandle Handle;
		uint32 InternalIndex;
		FNetObjectAttachmentsWriter::FCommitRecord AttachmentRecord;
		ENetObjectAttachmentType AttachmentType;
		bool bHasUnsentAttachments;
		uint32 NewBaselineIndex : 2;     // 新 baseline 槽位（与 BaselineManager 协商）
		uint32 bIsInitialState : 1;      // 是否首次完整状态（创建后第一次）
		uint32 bSentState : 1;
		uint32 bSentAttachments : 1;
		uint32 bHasDirtySubObjects : 1;
		uint32 bSentTearOff : 1;
		uint32 bSentDestroySubObject : 1;
		uint32 bSentBatchData : 1;
	};

	// 一次 batch 的类型（决定 Write 路径 + Record 处理逻辑）
	enum class EBatchInfoType : uint32
	{
		Object,
		HugeObject,
		OOBAttachment,
		Internal,
		// Currently unused as destruction infos don't create a BatchInfo.
		DestructionInfo,
	};

	// 一次 batch 的临时信息（Write 期间）
	struct FBatchInfo
	{
		TArray<FBatchObjectInfo, TInlineAllocator<16>> ObjectInfos;
		uint32 ParentInternalIndex;
		EBatchInfoType Type;
	};

	// 单对象的 Record（持久化到 FReplicationRecord，用于 ACK 回滚）
	struct FObjectRecord
	{
		FReplicationRecord::FRecordInfo Record;
		FNetObjectAttachmentsWriter::FReliableReplicationRecord AttachmentRecord;
	};

	struct FBatchRecord
	{
		TArray<FObjectRecord, TInlineAllocator<16>> ObjectReplicationRecords;
		uint32 BatchCount = 0U;
	};

	// 写入失败时用以回滚 BitStream 位置
	struct FBitStreamInfo
	{
		uint32 ReplicationStartPos;
		uint32 BatchStartPos;
		uint32 ReplicationCapacity;
	};

	// HugeObject 通道状态
	enum class EHugeObjectSendStatus : uint32
	{
		Idle,
		Sending,
	};

	// 单个 HugeObject 的"传输上下文"：完整 payload 切分后形成 N 个 NetBlob 分片；
	// 当所有分片的 TRefCountPtr 全部归 1（被 Manager 全部 ACK），认为 HugeObject 完整 ACK。
	struct FHugeObjectContext
	{
		FHugeObjectContext();
		~FHugeObjectContext();

		FInternalNetRefIndex RootObjectInternalIndex = 0;
		FBatchRecord BatchRecord;
		FNetExportContext::FBatchExports BatchExports;

		// The entire payload. When refcount reaches one for all blobs the object has been fully acked.
		TArray<TRefCountPtr<FNetBlob>> Blobs;
	};

	// HugeObject 队列：避免同一对象同时多份 in transit；提供 in-queue 检测/ack 推进/回收。
	class FHugeObjectSendQueue
	{
	public:
		FHugeObjectSendQueue();
		~FHugeObjectSendQueue();

		// If the queue is full we can't start another send.
		bool IsFull() const;
		bool IsEmpty() const;
		uint32 NumRootObjectsInTransit() const;

		// Enqueue huge object info and return true if it can be sent.
		bool EnqueueHugeObject(const FHugeObjectContext& Context);

		// Returns true if the object is a huge object root object or part of any huge object's payload. The latter is an expensive operation.
		bool IsObjectInQueue(FInternalNetRefIndex ObjectIndex, bool bFullSearch) const;

		// Best effort implementation of getting a valid index for trace.
		FInternalNetRefIndex GetRootObjectInternalIndexForTrace() const;

		// Call AckHugeObject on all objects determined to have been fully processed.
		// 扫描所有 SendContexts，对全部分片归 1 的 context 调用回调（Writer 据此 commit/release）
		void AckObjects(TFunctionRef<void (const FHugeObjectContext& Context)> AckHugeObject);

		void FreeContexts(TFunctionRef<void (const FHugeObjectContext& Context)> FreeHugeObject);

	public:
		// Public members
		// 发送性能统计（用于诊断 stall）
		struct FStats
		{
			// Cycle counter for when the huge object context went from idle to sending.
			uint64 StartSendingTime = 0;
			// Cycle counter for when the last part of huge object was sent.
			uint64 EndSendingTime = 0;
			// Cycle counter for when it was detected that no more parts of the huge object could be sent until some of the first parts have been acked.
			uint64 StartStallTime = 0;
		};

		FStats Stats;

		FNetTraceCollector* TraceCollector = nullptr;
		const FNetDebugName* DebugName = nullptr;

	private:
		TSet<FInternalNetRefIndex> RootObjectsInTransit;
		TDoubleLinkedList<FHugeObjectContext> SendContexts;
	};

	// 写对象的标志位：状态/附件/HugeObject 路径
	enum EWriteObjectFlag : unsigned
	{
		WriteObjectFlag_State = 1U,
		WriteObjectFlag_Attachments = WriteObjectFlag_State << 1U,
		WriteObjectFlag_HugeObject = WriteObjectFlag_Attachments << 1U,
		WriteObjectFlag_IsWritingHugeObjectBatch = WriteObjectFlag_HugeObject << 1U,
	};

	// WriteObjectBatch 失败时下一步策略
	enum class EWriteObjectRetryMode : unsigned
	{
		// Stop trying to write more object this frame.
		Abort,
		// Continue with something smaller, it might succeed.
		TrySmallObject,
		// The object is probably huge. Enter special mode for huge objects.
		SplitHugeObject,
	};

	// 单对象写入返回码
	enum class EWriteObjectStatus : unsigned
	{
		Success,

		// The object is in an invalid state and won't be written. This is not considered a failure.
		InvalidState,

		// BitStream overflow.
		BitStreamOverflow,

		// A detached instance, which no longer has an instance protocol. This object cannot be replicated.
		NoInstanceProtocol,

		// A subobject with an invalid owner.
		InvalidOwner,

		// Some error occurred while serializing the object.
		Error,

	};

private:

	void SetNetObjectListsSize(FInternalNetRefIndex NewMaxInternalIndex);
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	uint32 GetDefaultFlushFlags() const;
	uint32 GetFlushStatus(uint32 InternalIndex, const FReplicationInfo& Info, uint32 FlushFlagsToTest = EFlushFlags::FlushFlags_Default) const;
	void SetPendingDestroyOrSubObjectPendingDestroyState(uint32 Index, FReplicationInfo& Info);

	bool IsObjectIndexForOOBAttachment(uint32 InternalIndex) const { return InternalIndex == ObjectIndexForOOBAttachment; }

	// 取该协议初始 changemask（首次完整状态发送时全 1 但要尊重 InitOnly 屏蔽等）
	void GetInitialChangeMask(ChangeMaskStorageType* ChangeMaskStorage, const FReplicationProtocol* Protocol);

	// Start replication of new object with the specified InternalIndex
	void StartReplication(uint32 InternalIndex);

	// Stop replication object with the specified InternalIndex
	void StopReplication(uint32 InternalIndex);

	// Get ReplicationInfo for specified InternalIndex, prefer to use this method over direct access.
	FReplicationInfo& GetReplicationInfo(uint32 InternalIndex);
	const FReplicationInfo& GetReplicationInfo(uint32 InternalIndex) const;

	// Set the state of a ReplicatedObject, prefer this method to enable logging
	// 通过此辅助函数切换状态以便统一打日志/Trace
	void SetState(uint32 InternalIndex, EReplicatedObjectState NewState);

	// Write index part of handle
	void WriteNetRefHandleId(FNetSerializationContext& Context, FNetRefHandle RefHandle);
		
	// Create new ObjectRecord
	// Note: be aware that it will allocate a copy of the ChangeMask that needs to be handled if the record is not Committed
	void CreateObjectRecord(const FNetBitArrayView* ChangeMask, const FReplicationInfo& Info, const FBatchObjectInfo& ObjectInfo, FObjectRecord& OutRecord);
	
	// Push and link info for written object to ReplicationRecord
	// 将临时 FObjectRecord 提交到 FReplicationRecord 并链接到 PerObject FRecordInfoList
	void CommitObjectRecord(uint32 InternalObjectIndex, const FObjectRecord& Record);
	// Push and link info for destroyed root object
	void CommitObjectDestroyRecord(uint32 InternalObjectIndex, const FObjectRecord& ObjectRecord, const FReplicationRecord::FSubObjectRecord& SubObjectRecord);

	void CommitBatchRecord(const FBatchRecord& BatchRecord);

	// 依赖对象调度：以父对象 priority 为基准，把 dependents 加入调度池
	void ScheduleDependentObjects(uint32 Index, float ParentPriority, TArray<float>& LocalPriorities, FScheduleObjectInfo* ScheduledObjectIndices, uint32& OutScheduledObjectCount);

	uint32 ScheduleObjects(FScheduleObjectInfo* ScheduledObjectIndices);
	
	// Partial sort of OutScheduledObjectIndices, will sort at most PartialSortObjectCount objects
	uint32 SortScheduledObjects(FScheduleObjectInfo* ScheduledObjectIndices, uint32 ScheduledObjectCount, uint32 StartIndex);

	// Update the active set of stream debug features based on cvars, build configuration etc. Enabling certain debug features can help track down bitstream errors. 
	void UpdateStreamDebugFeatures();
	// Write stream debug features.
	void WriteStreamDebugFeatures(FNetSerializationContext& Context);
	// Write all objects pending destroy (or as many as we fit in the current packet). Will call one of WriteObjectsAndSubObjectsPendingDestroy and WriteRootObjectsPendingDestroy depending on cvar net.Iris.DestroyRootAndSubObjectsIndividually
	uint32 WriteObjectsPendingDestroy(FNetSerializationContext& Context);
	// Used when net.Iris.DestroyRootAndSubObjectsIndividually is true. Will send destroy for every root and subobject. Leagcy Iris behavior.
	uint32 WriteObjectsAndSubObjectsPendingDestroy(FNetSerializationContext& Context);
	// Used when net.Iris.DestroyRootAndSubObjectsIndividually is false. Will only send destroy for root objects and subobjects will be gathered on the client side and destroyed. This guarantees atomic object hierarchy destroys.
	uint32 WriteRootObjectsPendingDestroy(FNetSerializationContext& Context);

	// Write object and SubObjects
	// 入口：写一个对象 + 它的所有 dirty 子对象（一个 batch）
	EWriteObjectStatus WriteObjectAndSubObjects(FNetSerializationContext& Context, uint32 InternalIndex, uint32 WriteObjectFlags, FBatchInfo& OutBatchInfo);

	// Write objects recursive
	EWriteObjectStatus WriteObjectInBatch(FNetSerializationContext& Context, uint32 InternalIndex, uint32 WriteObjectFlags, FBatchInfo& OutBatchInfo);

	// Write 阶段的统一返回码
	enum class EWriteStatus : int32
	{
		// Stream is full and we should stop serializing more objects
		Abort = -1,
		// Object was skipped and won't be retried again. Ex: failed dependency, waiting for creation confirmation or buffer won't fit the object and its the last packet to send.
		Skipped = 0,
		// Object was successfully written
		Written = 1,
	};

	// Write destruction info for an object that should be destroyed
	// returns > 1 if Objects was written, 0 if the objects was skipped (failed dependency or waiting for creation confirmation) -1 if the stream is full and we should stop
	EWriteStatus WriteDestructionInfo(FNetSerializationContext& Context, uint32 InternalIndex);

	bool WriteNetRefHandleDestructionInfo(FNetSerializationContext& Context, FNetRefHandle Handle);

	struct FWriteBatchResult
	{
		EWriteStatus Status = EWriteStatus::Skipped;
		uint32 NumWritten = 0;
	};

	// Write Object and any subobject(s) to stream as an atomic batch
	// returns > 1 if Objects was written, 0 if the objects was skipped (failed dependency or waiting for creation confirmation) -1 if the stream is full and we should stop
	FWriteBatchResult WriteObjectBatch(FNetSerializationContext& Context, FInternalNetRefIndex InternalIndex, uint32 WriteObjectFlags);

	// 把一个超大对象切分为多个 NetBlob 分片，提交到 NetBlobManager；接收端 NetBlobAssembler 重组。
	EWriteStatus PrepareAndSendHugeObjectPayload(FNetSerializationContext& Context, FInternalNetRefIndex InternalIndex);

	// Write OOBAttachments
	uint32 WriteOOBAttachments(FNetSerializationContext& Context);

	// Write as many scheduled objects to stream as we can fit.
	uint32 WriteObjects(FNetSerializationContext& Context);

	// Updates ReplicationInfos, pushes ReplicationRecords etc after a successful call to WriteObjectInBatch() on a top level object
	uint32 HandleObjectBatchSuccess(const FBatchInfo& BatchInfo, FBatchRecord& OutRecord);

	// Determines the best course of action after a WriteObjectBatch() call failed.
	EWriteObjectRetryMode HandleObjectBatchFailure(EWriteObjectStatus WriteObjectStatus, const FBatchInfo& BatchInfo, const FBitStreamInfo& BatchBitStreamInfo);

	// Update logic for dropped RecordInfo
	// NACK 路径：把丢失的 ChangeMask 重新 OR 回 ObjectsWithDirtyChanges → 触发重发
	void HandleDroppedRecord(const FReplicationRecord::FRecordInfo& RecordInfo, FReplicationInfo& Info, const FNetObjectAttachmentsWriter::FReliableReplicationRecord& AttachmentRecord);
	// 状态级特化：根据丢失时对象当前状态决定回滚动作（例如丢失 PendingCreate 需要重发 creation）
	template<EReplicatedObjectState LostState> void HandleDroppedRecord(EReplicatedObjectState CurrentState, const FReplicationRecord::FRecordInfo& RecordInfo, FReplicationInfo& Info, const FNetObjectAttachmentsWriter::FReliableReplicationRecord& AttachmentRecord);

	// Update logic for delivered RecordInfo
	// ACK 路径：推进 LastAckedBaselineIndex / 释放 ChangeMask
	void HandleDeliveredRecord(const FReplicationRecord::FRecordInfo& RecordInfo, FReplicationInfo& Info, const FNetObjectAttachmentsWriter::FReliableReplicationRecord& AttachmentRecord);

	// Update logic for discarded RecordInfo, for preventing memory leaks on disconnect and shutdown.
	// 断开/关闭路径：仅清理资源，不做状态机推进
	void HandleDiscardedRecord(const FReplicationRecord::FRecordInfo& RecordInfo, FReplicationInfo& Info, const FNetObjectAttachmentsWriter::FReliableReplicationRecord& AttachmentRecord);

	// Setup replication info to be able to send attachments to objects not in scope
	// 配置 InternalIndex=0 这个特殊 slot 以承载 OOB 附件（用于"目标对象不在 scope"的 RPC）
	void SetupReplicationInfoForAttachmentsToObjectsNotInScope();

	// 把 LifetimeCondition / Conditional 应用到 ChangeMask（按连接屏蔽位）
	void ApplyFilterToChangeMask(uint32 ParentInternalIndex, uint32 InternalIndex, FReplicationInfo& Info, const FReplicationProtocol* Protocol, const uint8* InternalStateBuffer, bool bIsInitialState);

	// Patchup changemask to include any in-flight changes. Returns true if in-flight changes were added.
	// 把 PerObject 链表中所有 in-flight 的 ChangeMask 合并回当前 mask，确保丢包时不漏更新
	bool PatchupObjectChangeMaskWithInflightChanges(uint32 InternalIndex, FReplicationInfo& Info);

	// Invalidate all inflight baseline information
	// 当对象 baseline 被 BaselineManager 失效时，清除其 in-flight baseline 索引（防止接收端用错基线）
	void InvalidateBaseline(uint32 InternalIndex, FReplicationInfo& Info);

	// Returns true if the record chain starting from the provided RecordInfo contains any records with statedata
	// Note: Does not check if it is part of a huge object
	bool HasInFlightStateChanges(uint32 InternalIndex, const FReplicationInfo& Info) const;

	// Returns true if object has pending state changes in flight
	// Note: Does not check if it is part of a huge object
	bool HasInFlightStateChanges(const FReplicationRecord::FRecordInfo* RecordInfo) const;

	// Returns true object and subobjects can be created on remote
	bool CanSendObject(uint32 InternalIndex) const;

	// 是否处于"未确认创建"的初始两态之一
	inline bool IsInitialState(const EReplicatedObjectState State) const { return State == EReplicatedObjectState::PendingCreate || State == EReplicatedObjectState::WaitOnCreateConfirmation; }

	bool IsActiveHugeObject(uint32 InternalIndex) const;
	bool IsObjectPartOfActiveHugeObject(uint32 InternalIndex) const;

	bool CanQueueHugeObject() const;

	void FreeHugeObjectSendQueue();

	bool HasDataToSend(const FWriteContext& Context) const;

	// 收集对象状态中引用的 export（NetToken/ObjectReference），并附加到 Context 中（接收端按 export 解析）
	void CollectAndAppendExports(FNetSerializationContext& Context, uint8* RESTRICT InternalBuffer, const FReplicationProtocol* Protocol) const;

	bool IsWriteObjectSuccess(EWriteObjectStatus Status) const;

	// 增量编码：写入 baseline 与当前状态的 delta
	void SerializeObjectStateDelta(FNetSerializationContext& Context, uint32 InternalIndex, const FReplicationInfo& Info, const FNetRefHandleManager::FReplicatedObjectData& ObjectData, const uint8* RESTRICT ReplicatedObjectStateBuffer, FDeltaCompressionBaseline& CurrentBaseline, uint32 CreatedBaselineIndex);

	// 关闭/清理路径：把所有 in-flight Record 全部 discard（不重发，仅清资源）
	void DiscardAllRecords();
	void StopAllReplication();

	// 标脏快捷接口：把对象加入 ObjectsWithDirtyChanges 并打点日志
	void MarkObjectDirty(FInternalNetRefIndex InternalIndex, const char* Caller);

	// Writes a sentinel naking it easier for the receiver to detect bitstream errors.
	void WriteSentinel(FNetBitStreamWriter* Writer, const TCHAR* DebugName);

	// Returns a valid FCannotSendInfo if we should start tracking how long we are blocked from sending.
	FCannotSendInfo* ShouldWarnIfCannotSend(const FReplicationInfo& Info, FInternalNetRefIndex InternalIndex) const;

private:
	// Replication parameters
	FReplicationParameters Parameters;

	// Record of all in-flight data
	// 全连接的"流水账"（包级 ACK 时按 InfoCount 批量出队）
	FReplicationRecord ReplicationRecord;

	// Tracking information for the state of all objects
	// 每对象 16B 的状态信息（数组下标 = InternalIndex）
	TArray<FReplicationInfo> ReplicatedObjects;

	// Tracking information linking all in-flight data per object
	// 每对象的 in-flight Record 链表锚点（First/Last）
	TArray<FReplicationRecord::FRecordInfoList> ReplicatedObjectsRecordInfoLists;

	// Each replicated object has a scheduling priority that is bumped every time we have a chance to send and zeroed out every time the object is successfully sent
	// 调度优先级：每帧 += Prioritization 输出 + 各种 bumps；写成功置 0
	TArray<float> SchedulingPriorities;

	// Track Objects Pending Destroy?
	FNetBitArray ObjectsPendingDestroy;

	// Objects in this bitArray with dirty change masks
	// 本帧待发送（脏）对象集合 —— Write 调度的输入候选
	FNetBitArray ObjectsWithDirtyChanges;

	// Track Objects That is in scope for this connection
	FNetBitArray ObjectsInScope;
	
	// Handles logic for all attachments to objects.
	// 附件（RPC / 子对象 attachment）发送队列（含 Reliable/Unreliable/Huge 三种）
	FNetObjectAttachmentsWriter Attachments;

	// Cached internal systems
	// Init 时缓存的子系统指针（避免每次回查 ReplicationSystemInternal）
	FReplicationSystemInternal* ReplicationSystemInternal = nullptr;
	FNetRefHandleManager* NetRefHandleManager = nullptr;
	UObjectReplicationBridge* ReplicationBridge = nullptr;
	FDeltaCompressionBaselineManager* BaselineManager = nullptr;
	FObjectReferenceCache* ObjectReferenceCache = nullptr;
	const FReplicationFiltering* ReplicationFiltering = nullptr;
	FReplicationConditionals* ReplicationConditionals = nullptr;
	const UPartialNetObjectAttachmentHandler* PartialNetObjectAttachmentHandler = nullptr;
	const UNetObjectBlobHandler* NetObjectBlobHandler = nullptr;
	FNetExports* NetExports = nullptr;
	FNetTypeStats* NetTypeStats = nullptr;

	FWriteContext WriteContext;
	FBitStreamInfo WriteBitStreamInfo;
	FHugeObjectSendQueue HugeObjectSendQueue;
	// Features that can aid in tracking down bitstream errors etc.
	EReplicationDataStreamDebugFeatures StreamDebugFeatures = EReplicationDataStreamDebugFeatures::None;

	// Is replication enabled?
	bool bReplicationEnabled = false;
	
	// Should we use high prio create?
	const bool bHighPrioCreate = false;

	mutable TMap<FInternalNetRefIndex, FCannotSendInfo> CannotSendInfos;
};

inline FReplicationWriter::FReplicationInfo::FReplicationInfo()
: Value(0U)
{
}

template<FReplicationWriter::EReplicatedObjectState LostState> void FReplicationWriter::HandleDroppedRecord(FReplicationWriter::EReplicatedObjectState CurrentState, const FReplicationRecord::FRecordInfo& RecordInfo, FReplicationWriter::FReplicationInfo& Info, const FNetObjectAttachmentsWriter::FReliableReplicationRecord& AttachmentRecord)
{
	//static_assert(false, "Expected specialization to exist.");
	// 模板默认体：不应被实例化；必须为每个状态显式特化（在 .cpp 中实现）
}

}