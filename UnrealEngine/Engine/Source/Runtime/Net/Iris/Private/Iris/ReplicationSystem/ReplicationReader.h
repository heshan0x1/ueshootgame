// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationReader.h —— 接收端核心（每连接一份）
// -----------------------------------------------------------------------------
// 角色：解析 Server 通过 UReplicationDataStream 发来的比特流，包含：
//   1) 调试 features 位图（StreamDebugFeatures）
//   2) 待销毁对象列表（ReadObjectsPendingDestroy → 子对象先于父对象，或父对象级联销毁）
//   3) 对象 batch（ReadObjects）—— 每个 batch 包含：
//        • NetRefHandle（远端 ID → 本地 ID 通过 Bridge::CreateNetRefHandleFromRemote 翻译）
//        • 可选的 CreationHeader（首次出现时由 NetObjectFactory 实例化对象）
//        • 状态数据 / 增量数据（与 baseline 配对的 delta 解码）
//        • 附件 NetBlob（RPC / 子对象状态包等，由 NetBlobHandlerManager 分发）
//   4) HugeObject 通道：超过 MTU 的 batch 会被拆成多个 NetBlob 分片，
//      接收端按序组装（FNetBlobAssembler）后再走一次 batch 解析路径。
//
// PendingBatchData：当 batch 中包含尚未解析的 must-be-mapped 引用（如 async 加载未完成、
// 关卡 streaming）时，先把整段 bytes 暂存到 FPendingBatchHolder，待引用解析完成后再回放。
//
// DequantizeAndApplyHelper：把"内部量化 buffer"反量化到 external（UObject 属性内存）
// 并触发 RepNotify / OnRep。
//
// 对象引用跟踪：每对象维护两份 MultiMap：
//   - UnresolvedObjectReferences  改写但当前不可解析（async/未生成）
//   - ResolvedDynamicObjectReferences  已解析的 dynamic handle（可能后续变为不可解析）
// 配合 HotUnresolvedHandleCache / ColdUnresolvedHandleCache 的轮询解决策略。
//
// 错误处理：
//   - protocol mismatch → 标记对象为 broken（BrokenObjects）+ 上报 ReplicationSystem；
//   - bitstream 错误   → 调用 Context.SetError，断开连接由上层裁定。
// =============================================================================

#pragma once

// This class will never be included from public headers
#include "Iris/ReplicationSystem/AttachmentReplication.h"
#include "Iris/ReplicationSystem/ChangeMaskUtil.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/PendingBatchData.h"
#include "Iris/ReplicationSystem/ReplicationDataStreamDebug.h"
#include "Iris/ReplicationSystem/ReplicationTypes.h"
#include "Containers/Map.h"
#include "Misc/MemStack.h"

// Forward declaration
class UObjectReplicationBridge;

enum class EIrisAsyncLoadingPriority : uint8;

namespace UE::Net
{
	class FNetSerializationContext;
	class FNetTokenStoreState;
	class FReplicationStateStorage;
	class FNetBitStreamReader;
	class FNetBitStreamWriter;

	namespace Private
	{
		class FReplicationSystemInternal;
		class FResolveAndCollectUnresolvedAndResolvedReferenceCollector;
		class FNetBlobHandlerManager;
		class FNetRefHandleManager;
	}
}

namespace UE::Net::Private
{

// 附件分发模式：决定一次 dispatch 处理可靠还是不可靠附件（或两者）
enum class ENetObjectAttachmentDispatchFlags : uint32
{
	None = 0,
	Reliable = 1U << 0U,
	Unreliable = Reliable << 1U,
};
ENUM_CLASS_FLAGS(ENetObjectAttachmentDispatchFlags);

/** Deals with Incoming replication Data and dispatch */
class FReplicationReader
{
public:
	FReplicationReader();
	~FReplicationReader();

	// Init
	// 由 ReplicationDataStream::Init 在 AddConnection 时调用；缓存 ReplicationSystem 内部子系统指针。
	void Init(const FReplicationParameters& Parameters);
	void Deinit();

	// Read incoming replication data
	// 主入口：Read 一整段 ReplicationDataStream payload。失败时 Context.SetError 终止。
	void Read(FNetSerializationContext& Context);
	
	// Mark objects pending destroy as unresolvable.
	// 将待销毁对象从 ResolvedDynamicHandle 表中迁移到 unresolved（避免对外发布"已解析但下一帧消失"）。
	void UpdateUnresolvableReferenceTracking();

	// Process queued batches
	// 推进 PendingBatchHolder：尝试再次解析此前因 unresolved-must-be-mapped 而暂存的 batch。
	void ProcessQueuedBatches();

	[[nodiscard]] FString PrintObjectInfo(FInternalNetRefIndex ObjectIndex, FNetRefHandle NetRefHandle) const;

private:
	// ChangeMaskOffset -> FNetRefHandle
	enum EConstants : uint32
	{
		// "InitState" 引用专用占位 ChangeMaskOffset：Init-only 引用没有 ChangeMask 位，用最大值哨兵。
		FakeInitChangeMaskOffset = 0xFFFFFFFFU,
	};
	typedef TMultiMap<uint32, FNetRefHandle> FObjectReferenceTracker;
	typedef TArray<FNetRefHandle, TInlineAllocator<32>> FResolvedNetRefHandlesArray;

	// 接收端每对象的运行时元数据（与发送端 FReplicationInfo 不同：这里关心 baselines、
	// unresolved 引用、destroy/tear-off 标志等）。
	struct FReplicatedObjectInfo
	{
		FReplicatedObjectInfo();

		// We accumulate unresolved changes in this changemask
		// 当前所有"因引用未解析而被 hold 住"的 changemask 位
		FChangeMaskStorageOrPointer UnresolvedChangeMaskOrPointer;

		/* In order to be able to do partial updates the changemask bit is stored with the reference.
		 * That also means a reference can have many entries, but at most one per changemask bit. */
		// 同一引用可能出现在多个 changemask 位（数组多元素都用同一引用），
		// 故按 ChangeMaskOffset → Handle 多映射。
		FObjectReferenceTracker UnresolvedObjectReferences;
		FObjectReferenceTracker ResolvedDynamicObjectReferences;

		// These maps provide a O(1) lookup for the number of handles referenced in 
		// UnresolvedObjectReferences and ResolvedDynamicObjectReferences.
		// 反查计数：每个 Handle 在上述两份 MultiMap 中出现多少次（用于增删时维护）
		TMap<FNetRefHandle, int16> UnresolvedHandleCount;
		TMap<FNetRefHandle, int16> ResolvedDynamicHandleCount;

		// Baselines
		// 接收端基线缓存：最多保留两份（PrevStored / LastStored），用于 delta 解码
		uint8* StoredBaselines[2];

		uint32 InternalIndex;							// InternalIndex
		union
		{
			uint32 Value;
			struct	 
			{
				uint32 ChangeMaskBitCount : 16;					// This is cached to avoid having to look it up in the protocol		
				uint32 bHasUnresolvedReferences : 1;			// Do we have unresolved references in the changemask?
				uint32 bHasUnresolvedInitialReferences : 1;		// Do we have unresolved initial only references
				uint32 bHasAttachments : 1;
				uint32 bDestroy : 1;
				uint32 bTearOff : 1;
				uint32 bIsDeltaCompressionEnabled : 1;
				uint32 LastStoredBaselineIndex : 2;				// Last stored baseline, as soon as we receive data compressed against the baseline we know that we can release older baselines
				uint32 PrevStoredBaselineIndex : 2;				// Previous stored baselines index
				uint32 Padding : 7;
			};
		};

		bool RemoveUnresolvedHandleCount(FNetRefHandle RefHandle);
		bool RemoveResolvedDynamicHandleCount(FNetRefHandle RefHandle);
	};

	// Temporary Data to dispatch
	// 一次 Read 期间临时收集"待派发"的 dispatch 信息（量化 buffer + 元数据），见 .cpp。
	struct FDispatchObjectInfo;

	enum : uint32
	{
		// 用于 OOB 附件（不属于任何对象）的特殊"对象索引"占位
		ObjectIndexForOOBAttachment = 0U,
		// Try to avoid reallocations for dispatch in the case we need to process a huge object
		ObjectsToDispatchSlackCount = 32U,
	};

	bool IsObjectIndexForOOBAttachment(uint32 InternalIndex) const { return InternalIndex == ObjectIndexForOOBAttachment; }

	// Read index part of handle
	// 从 bitstream 读取 NetRefHandle 的远端 Id（不含 ReplicationSystemId）
	FNetRefHandle ReadNetRefHandleId(FNetSerializationContext& Context, FNetBitStreamReader& Reader) const;

	enum EReadObjectFlag : unsigned
	{
		// 当前正处于 HugeObject 分片重组完成后的二次解析路径（影响 batch size 等校验）
		ReadObjectFlag_IsReadingHugeObjectBatch = 1U,
	};

	// Read a new or updated object
	// 解析单个对象 batch（含 owner + 0..N 个 sub-object）
	uint32 ReadObjectBatch(FNetSerializationContext& Context, uint32 ReadObjectFlags);

	// Read object or subobject
	void ReadObjectInBatch(FNetSerializationContext& Context, FNetRefHandle BatchHandle, bool bIsSubObject);

	// 两种 batch 编码：
	//   WithoutSizes —— 旧/紧凑路径，按对象数读到位置截止；
	//   WithSizes    —— 调试或新路径，每个对象前先写 size 字段（便于跳过出错对象）。
	uint32 ReadObjectsInBatch(FNetSerializationContext& Context, FNetRefHandle InCompleteHandle, bool bHasBatchOwnerData, uint32 BatchEndBitPosition);
	uint32 ReadObjectsInBatchWithoutSizes(FNetSerializationContext& Context, FNetRefHandle InCompleteHandle, bool bHasBatchOwnerData, uint32 BatchEndBitPosition);
	uint32 ReadObjectsInBatchWithSizes(FNetSerializationContext& Context, FNetRefHandle InCompleteHandle, bool bHasBatchOwnerData, uint32 BatchEndBitPosition);


	// Read stream debug features. Use StreamDebugFeatures to see if a feature is enabled.
	void ReadStreamDebugFeatures(FNetSerializationContext& Context);
	// Read objects pending destroy. This will call ReadObjectsAndSubObjectsPendingDestroy or ReadRootObjectsPendingDestroy depending on sending side settings.
	uint32 ReadObjectsPendingDestroy(FNetSerializationContext& Context);
	// Receive both objects and subobject destruction. Legacy Iris behavior.
	// 旧路径：每个 root 和 sub 都单独发送销毁指令
	uint32 ReadObjectsAndSubObjectsPendingDestroy(FNetSerializationContext& Context);
	// Receive only root object destruction and destroy all existing subobjects with them atomically.
	// 新路径（net.Iris.DestroyRootAndSubObjectsIndividually=false）：仅发送 root 销毁，
	// 接收端从 NetDependencyData 找出所有 sub-object 并原子销毁。
	uint32 ReadRootObjectsPendingDestroy(FNetSerializationContext& Context);

	// Read state data for all incoming objects
	void ReadObjects(FNetSerializationContext& Context, uint32 ObjectCountToRead, uint32 ReadObjectFlags);

	// Process a single huge object attachment
	// HugeObject 通道：单个分片到达时由此函数处理（push 到 NetBlobAssembler）
	void ProcessHugeObjectAttachment(FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& Attachment);

	// Assemble and deserialize huge object if present
	// 当 NetBlobAssembler 表示已收齐全部分片时，把组装好的 buffer 走一次"内嵌 batch"解析。
	void ProcessHugeObject(FNetSerializationContext& Context);

	// Resolve and dispatch unresolved references
	// 单对象级别：对该对象的 UnresolvedChangeMask 尝试再次解析
	void ResolveAndDispatchUnresolvedReferencesForObject(FNetSerializationContext& Context, uint32 InternalIndex);

	// Resolve and dispatch unresolved references
	// 全局：每帧扫描 Hot/ColdUnresolvedHandleCache，对其依赖的对象触发 dispatch
	void ResolveAndDispatchUnresolvedReferences();

	// Dispatch all data received for the frame, this includes trying to resolve object references
	// 帧末统一派发：DequantizeAndApplyHelper 从 InternalState → ExternalState → UObject 属性 + RepNotify
	void DispatchStateData(FNetSerializationContext& Context);

	// Dispatch resolved attachments
	// 派发已解析的附件（RPC 等），按 Reliable/Unreliable 分批
	void ResolveAndDispatchAttachments(FNetSerializationContext& Context, FReplicatedObjectInfo* ReplicationInfo, ENetObjectAttachmentDispatchFlags DispatchFlags);

	// End replication for all objects that the server has told us to destroy or tear off
	void DispatchEndReplication(FNetSerializationContext& Context);

	// Create tracking info for the object with the given InternalInfo
	FReplicatedObjectInfo& StartReplication(uint32 InternalIndex);

	// Remove tracking info for the object with InternalIndex
	// bDestroyInstance=true → 通过 Bridge 销毁 UObject；bTearOff=true → 仅断开 replication 但保留实例
	void EndReplication(FInternalNetRefIndex InternalIndex, bool bTearOff, bool bDestroyInstance);

	// Free any data allocated per object
	void CleanupObjectData(FReplicatedObjectInfo& ObjectInfo);

	// Lookup the tracking info for the object with IntnernalIndex
	FReplicatedObjectInfo* GetReplicatedObjectInfo(uint32 InternalIndex);
	const FReplicatedObjectInfo* GetReplicatedObjectInfo(uint32 InternalIndex) const;

	// Update reference tracking maps for the current object
	// 同步 UnresolvedObjectReferences / ResolvedDynamicObjectReferences 与本帧新数据
	void UpdateObjectReferenceTracking(FReplicatedObjectInfo* ReplicationInfo, FNetBitArrayView ChangeMask, bool bIncludeInitState, FResolvedNetRefHandlesArray& OutNewResolvedRefHandles, const FObjectReferenceTracker& NewUnresolvedReferences, const FObjectReferenceTracker& NewMappedDynamicReferences);
	
	// An optimized version of UpdateObjectReferenceTracking().
	void UpdateObjectReferenceTracking_Fast(FReplicatedObjectInfo* ReplicationInfo, FNetBitArrayView ChangeMask, bool bIncludeInitState, FResolvedNetRefHandlesArray& OutNewResolvedRefHandles, const FObjectReferenceTracker& NewUnresolvedReferences, const FObjectReferenceTracker& NewMappedDynamicReferences);

	// Remove all references for object
	void CleanupReferenceTracking(FReplicatedObjectInfo* ObjectInfo);

	// Update ReplicationInfo and OutUnresolvedChangeMask based on data collected by the Collector
	// 由 FResolveAndCollectUnresolvedAndResolvedReferenceCollector 在 dequantize 阶段收集到的引用，
	// 在此处与 ReplicationInfo 上的旧状态 diff，生成新的 UnresolvedChangeMask 与 newly-resolved 列表。
	void BuildUnresolvedChangeMaskAndUpdateObjectReferenceTracking(const FResolveAndCollectUnresolvedAndResolvedReferenceCollector& Collector, FNetBitArrayView CollectorChangeMask, FReplicatedObjectInfo* ReplicationInfo, FNetBitArrayView& OutUnresolvedChangeMask, FResolvedNetRefHandlesArray& OutNewResolvedRefHandles);

	void RemoveUnresolvedObjectReferenceInReplicationInfo(FReplicatedObjectInfo* ReplicationInfo, FNetRefHandle Handle);
	void RemoveResolvedObjectReferenceInReplicationInfo(FReplicatedObjectInfo* ReplicationInfo, FNetRefHandle Handle);

	// A previously resolved dynamic reference is now unresolvable. The ReplicationInfo needs to be updated to reflect this.
	// Returns true if the reference was found.
	// 已解析的 dynamic handle 突然不可解析（如对象被销毁），从 Resolved 移到 Unresolved。
	bool MoveResolvedObjectReferenceToUnresolvedInReplicationInfo(FReplicatedObjectInfo* ReplicationInfo, FNetRefHandle UnresolvableHandle);

	// 增量解码：从 baseline + delta 还原状态；NewBaselineIndex 表示对端给本对象设置的新基线槽位
	void DeserializeObjectStateDelta(FNetSerializationContext& Context, uint32 InternalIndex, FDispatchObjectInfo& Info, FReplicatedObjectInfo& ObjectInfo, const FNetRefHandleManager::FReplicatedObjectData& ObjectData, uint32& OutNewBaselineIndex);

	// If async loading is enabled this function will verify if we can resolve all PendingMustBeMappedReferences
	// 当 batch 携带 must-be-mapped 引用但本地尚未加载完成时，把整段 bytes 转入 PendingBatchData。
	// 返回非空表示已转入 pending（调用方应跳过当帧 dispatch）。
	FPendingBatchData* UpdateUnresolvedMustBeMappedReferences(FNetRefHandle OwnerHandle, TArray<FNetRefHandle>& MustBeMappedReferences, EIrisAsyncLoadingPriority InIrisAsyncLoadingPriority);

	// Returns true if the object assigned to this handle has been instantiated locally
	bool DoesParentExist(FNetRefHandle ObjectHandle) const;

	// If we are queuing data for a batch we must also defer calls to EndReplication
	// This method writes this method in the form of a QueuedChunk
	// 当 batch 已被 PendingBatchData 暂存，但其中又含 EndReplication 指令时，
	// 把销毁动作也以 chunk 形式排队，待 batch 回放完成后再触发。
	bool EnqueueEndReplication(FPendingBatchData* PendingBatchData, bool bShouldDestroyInstance, bool bShouldProcessHierarchy, FNetRefHandle NetRefHandleToEndReplication);

	// Remove a handle from the hot and cold unresolved caches used by ResolveAndDispatchUnresolvedReferences(). If this handle is marked as unresolved
	// again, it will be added to the hot cache.
	void RemoveFromUnresolvedCache(const FNetRefHandle Handle);

	// Reads and verifies the sentinel was read properly. If an error is detected the context will have an error set. The function will return true if the sentinel matches, false otherwise.
	bool ReadSentinel(FNetSerializationContext& Context, const TCHAR* DebugName);
	
private:

	class FObjectsToDispatchArray;

	FReplicationParameters Parameters;

	// 临时分配器：单帧内的小缓冲（DispatchObjectInfo 等）；帧末统一回收
	FMemStackBase TempLinearAllocator;
	FMemStackChangeMaskAllocator TempChangeMaskAllocator;

	// 持久 ChangeMask 分配器（unresolved changemask 跨帧持有）
	FGlobalChangeMaskAllocator PersistentChangeMaskAllocator;

	// 缓存的内部子系统指针（Init 时填充）
	FReplicationSystemInternal* ReplicationSystemInternal;
	FNetRefHandleManager* NetRefHandleManager;
	FReplicationStateStorage* StateStorage;
	UObjectReplicationBridge* ReplicationBridge;

	// A cache holding unresolved handles that should be resolved each time ResolveAndDispatchUnresolvedReferences() is called.
	// 热缓存：最近变 unresolved 的 handle，每帧都尝试解析
	TMap<FNetRefHandle, uint32> HotUnresolvedHandleCache;

	// A cache holding unresolved handles that should be resolved by ResolveAndDispatchUnresolvedReferences() at fixed intervals.
	// 冷缓存：长期 unresolved，按固定间隔（节流）尝试
	TMap<FNetRefHandle, uint32> ColdUnresolvedHandleCache;

	// Temporary buffers used by ResolveAndDispatchUnresolvedReferences().
	TSet<FNetRefHandle> VisitedUnresolvedHandles;
	TSet<FInternalNetRefIndex> InternalObjectsToResolve;

	// We track some data about incoming objects
	// Stored in a map for now
	// 接收侧每对象信息（对应 NetRefHandleManager 中的 InternalIndex）
	TMap<uint32, FReplicatedObjectInfo> ReplicatedObjects;

	// Temporary data valid during receive
	FObjectsToDispatchArray* ObjectsToDispatchArray;

	// We need to keep some data around for objects with pending dependencies
	// For now just use array and brute force the updates
	// 有"附件待解析"的对象索引（典型：附件中含尚未加载的对象引用）
	TArray<uint32> ObjectsWithAttachmentPendingResolve;

	// Track all objects waiting for this handle to be resolvable
	// 反向表：等待某 unresolved Handle 的所有 owner 对象（解析后批量唤醒）
	TMultiMap<FNetRefHandle, FInternalNetRefIndex /*OwnerInternalIndex*/> UnresolvedHandleToDependents;
	
	// Track all objects with a dynamic handle in case it becomes unresolvable
	// 反向表：持有某 dynamic Handle 的对象（被销毁时批量降级为 unresolved）
	TMultiMap<FNetRefHandle, uint32> ResolvedDynamicHandleToDependents;

	// We do not expect to have many objects in this state
	// PendingBatchData 容器：暂存"等待 must-be-mapped 引用解析/async load 完成"的 batch
	FPendingBatchHolder PendingBatchHolder;

	// We do not expect many objects to be broken
	// 协议失配/解码错误的对象（通常对应 ReportProtocolMismatch 上报）
	TArray<FNetRefHandle> BrokenObjects;

	// Keep track of the last time we warned about an object blocked by this must be mapped reference
	// Prevents spamming errors for multiple objects all waiting on the same asset
	// 警告节流：相同的 must-be-mapped 资产不要反复刷日志
	TMap<FNetRefHandle, double> BlockedMustBeMappedLastWarningTime;

	// Used during receive and processing of pending batches
	TArray<FNetRefHandle> TempMustBeMappedReferences;

	// Preallocate the arrays used by BuildUnresolvedChangeMaskAndUpdateObjectReferenceTracking() to 
	// avoid memory allocations during the frame.
	FObjectReferenceTracker UnresolvedReferencesCache;
	FObjectReferenceTracker MappedDynamicReferencesCache;

	FNetBlobHandlerManager* NetBlobHandlerManager;
	FNetBlobType NetObjectBlobType;            // 对象状态 NetBlob 类型 ID（HugeObject 路径会用到）
	FNetObjectAttachmentsReader Attachments;
	FObjectReferenceCache* ObjectReferenceCache;
	FNetObjectResolveContext ResolveContext;

	IConsoleVariable const* DelayAttachmentsWithUnresolvedReferences;
	uint32 NumHandlesPendingResolveLastUpdate = 0U;

	// Features that can aid in tracking down bitstream errors etc.
	EReplicationDataStreamDebugFeatures StreamDebugFeatures = EReplicationDataStreamDebugFeatures::None;
};

}