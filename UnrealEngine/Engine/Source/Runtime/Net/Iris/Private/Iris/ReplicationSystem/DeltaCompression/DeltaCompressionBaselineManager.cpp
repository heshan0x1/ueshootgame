// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaselineManager.cpp
// ---------------------------------------------------------------------------------------------
// 实现 FDeltaCompressionBaselineManager。完整生命周期与状态机请参考 .h 文件顶部说明。
// 关键流程：PreSendUpdate → (Writer 调 CreateBaseline/Get/Destroy) → PostSendUpdate。
// =============================================================================================

#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineManager.h"
#include "Iris/IrisConfigInternal.h"
#include "Iris/Core/BitTwiddling.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisMemoryTracker.h"
#include "Iris/ReplicationSystem/ChangeMaskCache.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineInvalidationTracker.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "HAL/IConsoleManager.h"
#include "Net/Core/Trace/NetTrace.h"
#include <limits>

#if UE_NET_VALIDATE_DC_BASELINES
#define UE_NET_DC_BASELINE_CHECK(...) check(__VA_ARGS__)
#else
#define UE_NET_DC_BASELINE_CHECK(...) 
#endif

namespace UE::Net::Private
{

// Delta compression kill switch
// 中文：DC 全局开关。关闭后 SetDeltaCompressionStatus(Allow) 也会被强制为 Disallow。
static bool bIsDeltaCompressionEnabled = true;
static FAutoConsoleVariableRef CVarEnableDeltaCompression(
	TEXT("net.Iris.EnableDeltaCompression"),
	bIsDeltaCompressionEnabled,
	TEXT("Enable delta compression for replicated objects. Default is true.")
);

// Delta compression baseline throttling.
// 中文：基线创建节流——同对象两次基线创建至少相隔 N 帧。0 表示每帧可建；负值表示永不再建（仅用首基线）。
static int32 MinimumNumberOfFramesBetweenBaselines = 60;
static FAutoConsoleVariableRef CVarMinimumNumberOfFramesBetweenBaselines(
	TEXT("net.Iris.MinimumNumberOfFramesBetweenBaselines"),
	MinimumNumberOfFramesBetweenBaselines,
	TEXT("Minimum number of frames between creation of new delta compression baselines for an object. Default is 60.")
);

bool IsDeltaCompressionEnabled()
{
	return bIsDeltaCompressionEnabled;
}

}

namespace UE::Net::Private
{

FDeltaCompressionBaselineManager::FDeltaCompressionBaselineManager()
{
	// 静态合约：必须有 InvalidObjectInfoIndex==0，并且与 NetRefHandleManager::InvalidInternalIndex 一致。
	// If alignment of storage type for FPerObjectInfo is too small then it's totally unsafe to cast such pointers to FPerObjectInfo.
	static_assert(InvalidObjectInfoIndex == 0, "InvalidObjectInfoIndex needs to be zero in order for current code to function properly.");
	static_assert(InvalidInternalIndex == FNetRefHandleManager::InvalidInternalIndex, "InvalidInternalIndex differs from FNetRefHandleManager::InvalidInternalIndex");
}

FDeltaCompressionBaselineManager::~FDeltaCompressionBaselineManager()
{
}

void FDeltaCompressionBaselineManager::Init(FDeltaCompressionBaselineManagerInitParams& InitParams)
{
	Connections = InitParams.Connections;
	NetRefHandleManager = InitParams.NetRefHandleManager;
	BaselineInvalidationTracker = InitParams.BaselineInvalidationTracker;
	ReplicationSystem = InitParams.ReplicationSystem;
	MaxConnectionCount = InitParams.Connections->GetMaxConnectionCount();
	// MaxDeltaCompressedObjectCount 取 min(全局对象上限, ini/外部传入上限)。
	MaxDeltaCompressedObjectCount = FPlatformMath::Min(InitParams.MaxNetObjectCount, InitParams.MaxDeltaCompressedObjectCount);

	DeltaCompressionEnabledObjects.Init(InitParams.MaxInternalNetRefIndex);

	// PerObjectInfo initialization
	{
		// ObjectInfoIndexType 是 uint16，所以 MaxDeltaCompressedObjectCount 必须 < 65535。
		check(MaxDeltaCompressedObjectCount < std::numeric_limits<ObjectInfoIndexType>::max());
		UsedPerObjectInfos.Init(MaxDeltaCompressedObjectCount + 1U);
		UsedPerObjectInfos.SetBit(InvalidObjectInfoIndex);
		ObjectIndexToObjectInfoIndex.SetNumZeroed(InitParams.MaxInternalNetRefIndex);

		// 每个 PerObjectInfo 实际占用字节数：header（其中 BaselinesForConnections[1] 是个尾部 1 元素）+
		// 额外 (MaxConnectionCount - 1) 份 FObjectBaselineInfo，再对齐到 alignof(FPerObjectInfo)。
		const uint32 BytesPerConnection = sizeof(FObjectBaselineInfo);
		// The PerObjectInfo already contains the connection specific data for one connection,
		// but we need to accommodate for MaxConnectionCount.
		BytesPerObjectInfo = static_cast<uint32>(Align(sizeof(FPerObjectInfo) + BytesPerConnection*(FPlatformMath::Max(MaxConnectionCount, 1U) - 1U), alignof(FPerObjectInfo)));
	}

	BaselineCounts.SetNumZeroed(MaxDeltaCompressedObjectCount + 1U);

	// Init baseline state manager
	{
		FDeltaCompressionBaselineStorageInitParams BaselineStorageInitParams;
		BaselineStorageInitParams.ReplicationStateStorage = InitParams.ReplicationStateStorage;
		// 池上限：MaxDeltaCompressedObjectCount × MaxConnectionCount × 2（双基线）。
		BaselineStorageInitParams.MaxBaselineCount = InitParams.MaxDeltaCompressedObjectCount*MaxConnectionCount*2U;
		BaselineStorage.Init(BaselineStorageInitParams);
	}
}

void FDeltaCompressionBaselineManager::Deinit()
{
	// The BaselineStateManager will release all baselines in its destructor.
	// 中文：先释放所有 PerObjectInfo（持有的 InternalBaseline 会逐个 Release 到 Storage），
	//       Storage 的 Deinit 仅做最后兜底 + 检查泄漏。
	FreeAllPerObjectInfos();
	BaselineStorage.Deinit();
}

void FDeltaCompressionBaselineManager::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	// NetRefHandle 池扩容：仅扩两个按 InternalIndex 寻址的容器。PerObjectInfo 槽位池不受影响。
	DeltaCompressionEnabledObjects.SetNumBits(NewMaxInternalIndex);
	ObjectIndexToObjectInfoIndex.SetNumZeroed(NewMaxInternalIndex);
}

void FDeltaCompressionBaselineManager::PreSendUpdate(FDeltaCompressionBaselineManagerPreSendUpdateParams& UpdateParams)
{
	IRIS_PROFILER_SCOPE(DeltaCompressionBaselineManager_PreSendUpdate);

	// 每帧节流时间基。
	++FrameCounter;

	// 1. 让 DC 启用集合反映本帧的 scope 变化（删去出 scope 的，加进入 scope 的）。
	UpdateScope();

	// 2. 把本帧脏 ChangeMask 同步进所有连接的 per-conn 累计 mask。
	UpdateDirtyStateMasks(UpdateParams.ChangeMaskCache);

	// 3. 应用 InvalidationTracker 中本帧累积的失效事件，逐个释放对应 InternalBaseline。
	InvalidateBaselinesDueToModifiedConditionals();

	// 4. 准备同帧基线共享上下文（避免同对象重复 Reserve）。
	ConstructBaselineSharingContext(BaselineSharingContext);
}

void FDeltaCompressionBaselineManager::PostSendUpdate(FDeltaCompressionBaselineManagerPostSendUpdateParams& UpdateParams)
{
	IRIS_PROFILER_SCOPE(DeltaCompressionBaselineManager_PostSendUpdate);

	// 把本帧 Reserve 出来的所有基线槽 Commit 或 Cancel（详见 BaselineStorage 的 Reserve/Commit 二段提交语义）。
	DestructBaselineSharingContext(BaselineSharingContext);
}

void FDeltaCompressionBaselineManager::AddConnection(uint32 ConnectionId)
{
	// 当前为空——所有 per-conn 状态都已经按 MaxConnectionCount 一次性预分配在 PerObjectInfo 里。
}

void FDeltaCompressionBaselineManager::RemoveConnection(uint32 ConnectionId)
{
	// 释放该连接所有 InternalBaseline，并按计数释放可被回收的 PerObjectInfo。
	ReleaseBaselinesForConnection(ConnectionId);
}

ENetObjectDeltaCompressionStatus FDeltaCompressionBaselineManager::GetDeltaCompressionStatus(FInternalNetRefIndex Index) const
{
	return DeltaCompressionEnabledObjects.GetBit(Index) ? ENetObjectDeltaCompressionStatus::Allow : ENetObjectDeltaCompressionStatus::Disallow;
}

void FDeltaCompressionBaselineManager::SetDeltaCompressionStatus(FInternalNetRefIndex ObjectIndex, ENetObjectDeltaCompressionStatus Status)
{
	// If DC is disabled or the object doesn't support it then mark the object as not DC enabled, regardless of the user's wishes.
	// 中文：三重过滤——用户意愿 ∧ 全局开关 ∧ Protocol trait。任一为 false 则强制视作 Disallow。
	bool bEnableDCForObject = (Status == ENetObjectDeltaCompressionStatus::Allow ? true : false);
	bEnableDCForObject = bEnableDCForObject && bIsDeltaCompressionEnabled;
	bEnableDCForObject = bEnableDCForObject && DoesObjectSupportDeltaCompression(ObjectIndex);

	DeltaCompressionEnabledObjects.SetBitValue(ObjectIndex, bEnableDCForObject);
}

void FDeltaCompressionBaselineManager::UpdateScope()
{
	IRIS_PROFILER_SCOPE(DeltaCompressionBaselineManager_UpdateScope);

	// 高效位运算：以 word 为单位扫描 (current ^ prev) & DC-enabled，识别本帧 add/remove 的 DC 对象。
	using WordType = FNetBitArray::StorageWordType;
	using SignedWordType = TSignedIntType<sizeof(WordType)>::Type;
	constexpr SIZE_T BitsPerWord = sizeof(WordType)*8U;

	const FNetBitArrayView ObjectsInScope = NetRefHandleManager->GetCurrentFrameScopableInternalIndices();
	const FNetBitArrayView PrevObjectsInScope = NetRefHandleManager->GetPrevFrameScopableInternalIndices();

	const WordType* ObjectsInScopeStorage = ObjectsInScope.GetData();
	const WordType* PrevObjectsInScopeStorage = PrevObjectsInScope.GetData();
	WordType* DeltaCompressionEnabledObjectsStorage = DeltaCompressionEnabledObjects.GetData();
	for (uint32 WordEndIt = ObjectsInScope.GetNumWords(), WordIt = 0; WordIt != WordEndIt; ++WordIt)
	{
		const WordType ObjectsInScopeWord = ObjectsInScopeStorage[WordIt];
		const WordType PrevObjectsInScopeWord = PrevObjectsInScopeStorage[WordIt];
		const WordType DeltaCompressionEnabledObjectsWord = DeltaCompressionEnabledObjectsStorage[WordIt];

		// We're only interested in processing delta compressed objects.
		// 中文：把 scope 变化与"DC 启用"求交，仅处理 DC 启用对象的进/出 scope。
		const WordType ModifiedObjectsWord = (ObjectsInScopeWord ^ PrevObjectsInScopeWord) & DeltaCompressionEnabledObjectsWord;

		// If we have DC enabled objects a same frame add+remove could cause bits to be set for non-existing objects.
		// So we enter the update loop if there are either modified objects or DC enabled objects.
		if ((DeltaCompressionEnabledObjectsWord | ModifiedObjectsWord) == 0)
		{
			continue;
		}

		// Clear delta compression status for objects no longer in scope
		// 中文：出 scope 的 DC 对象自动清掉 enabled 位，避免后续误处理。
		DeltaCompressionEnabledObjectsStorage[WordIt] = DeltaCompressionEnabledObjectsWord & ObjectsInScopeWord;

		const WordType BitOffset = WordIt*BitsPerWord;

		// Process removed objects
		// 中文：出 scope 的对象——若已无活跃基线则立即回收 PerObjectInfo；否则保留（待 baseline 全部释放）。
		for (WordType RemovedObjects = PrevObjectsInScopeWord & ModifiedObjectsWord; RemovedObjects; )
		{
			const WordType LeastSignificantBit = GetLeastSignificantBit(RemovedObjects);
			RemovedObjects ^= LeastSignificantBit;

			const WordType ObjectIndex = BitOffset + FPlatformMath::CountTrailingZeros(LeastSignificantBit);

			// Free object info if there are no baselines requiring it to persist.
			{
				ObjectInfoIndexType& ObjectInfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
				if (ObjectInfoIndex != InvalidObjectInfoIndex && BaselineCounts[ObjectInfoIndex] == 0)
				{
					FreePerObjectInfo(ObjectInfoIndex);
					ObjectInfoIndex = InvalidObjectInfoIndex;
				}
			}
		}

		// Process added objects
		// 中文：进 scope 的对象——分配一个 PerObjectInfo + 计算 ChangeMaskStride + 分配 ChangeMask 段。
		for (WordType AddedObjects = ObjectsInScopeWord & ModifiedObjectsWord; AddedObjects; )
		{
			const WordType LeastSignificantBit = GetLeastSignificantBit(AddedObjects);
			AddedObjects ^= LeastSignificantBit;

			const WordType ObjectIndex = BitOffset + FPlatformMath::CountTrailingZeros(LeastSignificantBit);
			AllocPerObjectInfoForObject(ObjectIndex);
		}
	}

	UE_NET_TRACE_FRAME_STATSCOUNTER(ReplicationSystem->GetId(), ReplicationSystem.ObjectWithDC, DeltaCompressionEnabledObjects.CountSetBits(), ENetTraceVerbosity::Verbose);
}

void FDeltaCompressionBaselineManager::UpdateDirtyStateMasks(const FChangeMaskCache* ChangeMaskCache)
{
	IRIS_PROFILER_SCOPE(DeltaCompressionBaselineManager_UpdateDirtyStateMasks);

	// If there aren't any dirty objects there's nothing for us to do.
	if (ChangeMaskCache->Indices.Num() <= 0)
	{
		return;
	}

	const FNetBitArray& ValidConnections = Connections->GetValidConnections();
	const uint32 MaxConnectionId = ValidConnections.FindLastOne();
	// If there are no connections there are no objects that need baseline updates.
	if (MaxConnectionId == ~0U)
	{
		return;
	}

	// 把每个本帧脏对象的 ChangeMask 字段 OR 进所有连接的 per-conn 累计 mask。
	// 这样 CreateBaseline 时 InternalBaseline.ChangeMask 直接拷贝累计 mask + 清零累计 mask 即可。
	const ChangeMaskStorageType* DirtyChangeMaskStoragePtr = ChangeMaskCache->Storage.GetData();
	for (const auto& Entry : ChangeMaskCache->Indices)
	{
		if (!Entry.bHasDirtyChangeMask)
		{
			continue;
		}

		// 仅 DC 启用对象需要累计；其它对象走 full state 路径，无需 baseline。
		if (!DeltaCompressionEnabledObjects.GetBit(Entry.InternalIndex))
		{
			continue;
		}

		FPerObjectInfo* ObjectInfo = GetPerObjectInfoForObject(Entry.InternalIndex);
		if (ObjectInfo == nullptr)
		{
			continue;
		}

		ChangeMaskStorageType*const ConnectionChangeMaskStoragePtr = ObjectInfo->ChangeMasksForConnections;
		ChangeMaskStorageType const*const UpdatedChanges = DirtyChangeMaskStoragePtr + Entry.StorageOffset;
		const uint32 ChangeMaskStridePerConnection = ObjectInfo->ChangeMaskStride;
		for (uint32 ConnectionId : ValidConnections)
		{
			const SIZE_T ChangeMaskOffset = ChangeMaskStridePerConnection*ConnectionId;
			ChangeMaskStorageType* ConnectionChangeMask = ConnectionChangeMaskStoragePtr + ChangeMaskOffset;
			for (uint32 WordIt = 0, WordEndIt = ChangeMaskStridePerConnection; WordIt != WordEndIt; ++WordIt)
			{
				ConnectionChangeMask[WordIt] |= UpdatedChanges[WordIt];
			}
		}
	}
}

FDeltaCompressionBaselineManager::FPerObjectInfo* FDeltaCompressionBaselineManager::AllocPerObjectInfoForObject(uint32 ObjectIndex)
{
	// 1. 申请一个新的 PerObjectInfo 槽位。
	const ObjectInfoIndexType ObjectInfoIndex = AllocPerObjectInfo();
	ObjectIndexToObjectInfoIndex.GetData()[ObjectIndex] = ObjectInfoIndex;

	if (ObjectInfoIndex != InvalidObjectInfoIndex)
	{
		FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectInfoIndex);
		ObjectInfo->ObjectIndex = ObjectIndex;
		
		// Allocate storage for changemasks for all connections
		// 2. 按 Protocol 的 ChangeMaskBitCount 计算每 ChangeMask 占多少 word（向上对齐到 word 边界）。
		const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
		const FReplicationProtocol* Protocol = ObjectData.Protocol;
		const uint32 ChangeMaskStride = Align(Protocol->ChangeMaskBitCount, sizeof(ChangeMaskStorageType)*8U)/(sizeof(ChangeMaskStorageType)*8U);
		check(ChangeMaskStride < 65536U);
		if (ChangeMaskStride > 0)
		{
			// 3. 单次分配三类 ChangeMask 段：
			//    [Conn × MaxConnectionCount]                    ← per-conn 累计
			//    [Baseline × 2 × MaxConnectionCount]            ← 双基线
			//    总共 3 × MaxConnectionCount × stride 个 word。
			const uint32 AllocSize = ChangeMaskStride*3*MaxConnectionCount*sizeof(ChangeMaskStorageType);
			ChangeMaskStorageType* ChangeMasksForConnections = static_cast<ChangeMaskStorageType*>(ChangeMaskAllocator.Alloc(AllocSize, alignof(ChangeMaskStorageType)));
			FPlatformMemory::Memzero(ChangeMasksForConnections, AllocSize);

			ObjectInfo->ChangeMaskStride = static_cast<uint16>(ChangeMaskStride);
			ObjectInfo->ChangeMasksForConnections = ChangeMasksForConnections;
		}

		return ObjectInfo;
	}
	else
	{
		// PerObjectInfo 池满（达 MaxDeltaCompressedObjectCount）：返回 null，调用方继续以 non-DC 路径工作。
		return nullptr;
	}
}

void FDeltaCompressionBaselineManager::FreePerObjectInfoForObject(uint32 ObjectIndex)
{
	ObjectInfoIndexType& ObjectInfoIndex = ObjectIndexToObjectInfoIndex.GetData()[ObjectIndex];
	if (ObjectInfoIndex != InvalidObjectInfoIndex)
	{
		FreePerObjectInfo(ObjectInfoIndex);
		ObjectInfoIndex = InvalidObjectInfoIndex;
	}
}

void FDeltaCompressionBaselineManager::ConstructPerObjectInfo(FPerObjectInfo* ObjectInfo) const
{
	// 显式构造（FPerObjectInfo 的构造函数被 = delete，必须手动初始化）。
	ObjectInfo->ChangeMasksForConnections = nullptr;
	ObjectInfo->ObjectIndex = 0;
	ObjectInfo->PrevBaselineCreationFrame = 0;
	ObjectInfo->ChangeMaskStride = 0;
	for (SIZE_T ConnIt = 0, ConnEndIt = MaxConnectionCount; ConnIt != ConnEndIt; ++ConnIt)
	{
		new (&ObjectInfo->BaselinesForConnections[ConnIt]) FObjectBaselineInfo();
	}
}

void FDeltaCompressionBaselineManager::DestructPerObjectInfo(FPerObjectInfo* ObjectInfo)
{
	if (ObjectInfo->ChangeMasksForConnections != nullptr)
	{
		ChangeMaskAllocator.Free(ObjectInfo->ChangeMasksForConnections);
	}

	for (SIZE_T ConnIt = 0, ConnEndIt = MaxConnectionCount; ConnIt != ConnEndIt; ++ConnIt)
	{
		ObjectInfo->BaselinesForConnections[ConnIt].~FObjectBaselineInfo();
	}
}

FDeltaCompressionBaselineManager::ObjectInfoIndexType FDeltaCompressionBaselineManager::AllocPerObjectInfo()
{
	const uint32 ObjectInfoIndex = UsedPerObjectInfos.FindFirstZero();
	if (ObjectInfoIndex != FNetBitArray::InvalidIndex)
	{
		UsedPerObjectInfos.SetBit(ObjectInfoIndex);

		// 必要时按 ObjectInfoGrowCount 段扩容 ObjectInfoStorage。
		const SIZE_T ObjectInfoStorageIndex = ObjectInfoIndex*BytesPerObjectInfo;
		if (ObjectInfoStorageIndex >= ObjectInfoStorage.Num())
		{
			LLM_SCOPE_BYTAG(Iris);
			ObjectInfoStorage.AddUninitialized(ObjectInfoGrowCount*BytesPerObjectInfo);
		}

		FPerObjectInfo* ObjectInfo = reinterpret_cast<FPerObjectInfo*>(ObjectInfoStorage.GetData() + ObjectInfoStorageIndex);
		ConstructPerObjectInfo(ObjectInfo);

		return static_cast<ObjectInfoIndexType>(ObjectInfoIndex);
	}

	return InvalidObjectInfoIndex;
}

void FDeltaCompressionBaselineManager::FreePerObjectInfo(ObjectInfoIndexType Index)
{
	UsedPerObjectInfos.ClearBit(Index);

	FPerObjectInfo* ObjectInfo = GetPerObjectInfo(Index);
	DestructPerObjectInfo(ObjectInfo);
}

void FDeltaCompressionBaselineManager::FreeAllPerObjectInfos()
{
	auto DestructObjectInfo = [this](uint32 ObjectInfoIndex)
	{
		FPerObjectInfo* ObjectInfo = GetPerObjectInfo(static_cast<ObjectInfoIndexType>(ObjectInfoIndex));
		DestructPerObjectInfo(ObjectInfo);
	};

	UsedPerObjectInfos.ClearBit(InvalidObjectInfoIndex);
	UsedPerObjectInfos.ForAllSetBits(DestructObjectInfo);
}

FDeltaCompressionBaseline FDeltaCompressionBaselineManager::CreateBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex)
{
	UE_NET_DC_BASELINE_CHECK(BaselineSharingContext.ObjectInfoIndicesWithNewBaseline.GetNumBits() > 0);
	
	FDeltaCompressionBaseline Baseline;
	// 早退：DC 关闭/对象未启用 → 直接返回 invalid baseline。Writer 应回退到 full 序列化。
	if (!DeltaCompressionEnabledObjects.GetBit(ObjectIndex))
	{
		return Baseline;
	}

	const ObjectInfoIndexType ObjectInfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (ObjectInfoIndex == InvalidObjectInfoIndex)
	{
		return Baseline;
	}

	FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectInfoIndex);
	FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[ConnId];
	FInternalBaseline& InternalBaseline = BaselineInfo.Baselines[BaselineIndex];

	// 如果该 (Conn, Obj, Idx) 槽已有旧基线，先释放（计数 -1）；新基线建好后再 +1，整体增量记到 BaselineCountAdjustment。
	int16 BaselineCountAdjustment = 0;
	if (InternalBaseline.IsValid())
	{
		--BaselineCountAdjustment;
		ReleaseInternalBaseline(InternalBaseline);
	}

	FDeltaCompressionBaselineStateInfo BaselineStateInfo;
	if (BaselineSharingContext.ObjectInfoIndicesWithNewBaseline.GetBit(ObjectInfoIndex))
	{
		// 同帧内已有连接为该对象 Reserve 过基线 → 直接复用槽位（共享 StateBuffer）。
		const DeltaCompressionBaselineStateInfoIndexType BaselineStateInfoIndex = BaselineSharingContext.ObjectInfoIndexToBaselineInfoIndex[ObjectInfoIndex];
		BaselineStateInfo = BaselineStorage.GetBaselineReservationForCurrentState(BaselineStateInfoIndex);
	}
	else if (IsAllowedToCreateBaselineForObject(ConnId, ObjectIndex, ObjectInfo, ObjectInfoIndex))
	{
		// 节流策略允许 → 真正向 Storage Reserve 一份新基线。
		BaselineStateInfo = BaselineStorage.ReserveBaselineForCurrentState(ObjectIndex);
		if (BaselineStateInfo.IsValid())
		{
			++BaselineSharingContext.CreatedBaselineCount;
			BaselineSharingContext.ObjectInfoIndicesWithNewBaseline.SetBit(ObjectInfoIndex);
			BaselineSharingContext.ObjectInfoIndexToBaselineInfoIndex[ObjectInfoIndex] = BaselineStateInfo.StateInfoIndex;
		}
	}

	if (BaselineStateInfo.IsValid())
	{
		// 节流计数：记录"上次创建基线的帧"。
		ObjectInfo->PrevBaselineCreationFrame = FrameCounter;

		// 即使是同帧共享路径也需要 AddRef（每多一个连接持有就加一次引用）。
		BaselineStorage.AddRefBaseline(BaselineStateInfo.StateInfoIndex);

		++BaselineCountAdjustment;

		// 安装 InternalBaseline。
		InternalBaseline.BaselineStateInfoIndex = BaselineStateInfo.StateInfoIndex;
		InternalBaseline.ChangeMask = GetChangeMaskPointerForBaseline(ObjectInfo, ConnId, BaselineIndex);
		
		// Copy connection specific changemask since last baseline and clear it.
		// 中文：把 per-conn 累计的脏位"快照"进 InternalBaseline.ChangeMask，并清零累计 mask 让下一轮重新累积。
		{
			const uint32 ChangeMaskStride = ObjectInfo->ChangeMaskStride;
			const SIZE_T ChangeMaskOffset = ChangeMaskStride*ConnId;
			ChangeMaskStorageType* ConnectionChangeMask = ObjectInfo->ChangeMasksForConnections + ChangeMaskOffset;
			for (uint32 WordIt = 0, WordEndIt = ChangeMaskStride; WordIt != WordEndIt; ++WordIt)
			{
				InternalBaseline.ChangeMask[WordIt] = ConnectionChangeMask[WordIt];
				ConnectionChangeMask[WordIt] = 0;
			}
		}

		// Fill in return value
		Baseline.ChangeMask = InternalBaseline.ChangeMask;
		Baseline.StateBuffer = BaselineStateInfo.StateBuffer;
	}

	// Check for baseline count overflow.
	UE_NET_DC_BASELINE_CHECK(BaselineCountAdjustment <= 0 || (uint16(BaselineCounts[ObjectInfoIndex] + BaselineCountAdjustment) > 0));

	BaselineCounts[ObjectInfoIndex] += BaselineCountAdjustment;
	return Baseline;
}

void FDeltaCompressionBaselineManager::DestroyBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex)
{
	// Discard 语义：直接丢弃 baseline ChangeMask（典型用于 ACK 后清理上一个基线）。
	DestroyBaseline(ConnId, ObjectIndex, BaselineIndex, EChangeMaskBehavior::Discard);
}

void FDeltaCompressionBaselineManager::LostBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex)
{
	// Merge 语义：包丢失/NACK 时把 baseline 累计的脏位 OR 回 per-conn，下次能补发完整 diff。
	DestroyBaseline(ConnId, ObjectIndex, BaselineIndex, EChangeMaskBehavior::Merge);
}

FDeltaCompressionBaseline FDeltaCompressionBaselineManager::GetBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex) const
{
	FDeltaCompressionBaseline Baseline;

	// InvalidBaselineIndex 是 Writer 用来标记"没有可用基线"的哨兵。
	if (BaselineIndex == InvalidBaselineIndex)
	{
		return Baseline;
	}

	const FPerObjectInfo* ObjectInfo = GetPerObjectInfoForObject(ObjectIndex);
	if (ObjectInfo == nullptr)
	{
		return Baseline;
	}

	UE_NET_DC_BASELINE_CHECK(BaselineIndex < MaxBaselineCount);

	const FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[ConnId];
	const FInternalBaseline& InternalBaseline = BaselineInfo.Baselines[BaselineIndex];

	if (InternalBaseline.IsValid())
	{
		const FDeltaCompressionBaselineStateInfo& BaselineStateInfo = BaselineStorage.GetBaseline(InternalBaseline.BaselineStateInfoIndex);
		Baseline.ChangeMask = InternalBaseline.ChangeMask;
		Baseline.StateBuffer = BaselineStateInfo.StateBuffer;
	}

	// 注意：失效的基线返回 Baseline.IsValid()==false，调用方必须按此分支处理。
	return Baseline;
}

void FDeltaCompressionBaselineManager::ReleaseInternalBaseline(FInternalBaseline& InternalBaseline)
{
	BaselineStorage.ReleaseBaseline(InternalBaseline.BaselineStateInfoIndex);
	InternalBaseline.BaselineStateInfoIndex = InvalidBaselineStateInfoIndex;
}

void FDeltaCompressionBaselineManager::AdjustBaselineCount(const FPerObjectInfo* ObjectInfo, ObjectInfoIndexType ObjectInfoIndex, int16 Adjustment)
{
	if (!Adjustment)
	{
		return;
	}

	// Adjustment 通常为负（Destroy 路径）；这里 check 不会"反向溢出"。
	const uint16 NewBaselineCount = static_cast<uint16>(BaselineCounts[ObjectInfoIndex] + Adjustment);
	UE_NET_DC_BASELINE_CHECK(NewBaselineCount <= BaselineCounts[ObjectInfoIndex]);
	BaselineCounts[ObjectInfoIndex] = NewBaselineCount;

	if (NewBaselineCount == 0)
	{
		// If object is no longer in scope or no longer has DC enabled we can free the associated PerObjectInfo.
		// 中文：基线数归零 + 对象已不在 DC 启用集合 → 真正回收 PerObjectInfo。
		const uint32 ObjectIndex = ObjectInfo->ObjectIndex;
		if (!DeltaCompressionEnabledObjects.GetBit(ObjectIndex))
		{
			FreePerObjectInfo(ObjectInfoIndex);
			ObjectIndexToObjectInfoIndex[ObjectIndex] = InvalidObjectInfoIndex;
		}
	}
}

void FDeltaCompressionBaselineManager::ReleaseBaselinesForConnection(uint32 ConnId)
{
	auto ReleaseObjectBaselines = [this, ConnId](uint32 InObjectInfoIndex)
	{
		const uint16 ObjectInfoIndex = static_cast<uint16>(InObjectInfoIndex);
		FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectInfoIndex);
		FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[ConnId];

		int16 BaselineCountAdjustment = 0;
		for (const uint32 BaselineIndex : {0, 1})
		{
			FInternalBaseline& InternalBaseline = BaselineInfo.Baselines[BaselineIndex];
			if (InternalBaseline.IsValid())
			{
				ReleaseInternalBaseline(InternalBaseline);
				--BaselineCountAdjustment;
			}
		}

		AdjustBaselineCount(ObjectInfo, ObjectInfoIndex, BaselineCountAdjustment);
	};

	// The releasing of baselines can cause PerObjectInfos to be released.
	// 中文：拷贝一份 used 位图遍历——避免 ReleaseObjectBaselines 内的 FreePerObjectInfo 修改被遍历位图。
	FNetBitArray ConnUsedObjectInfos(UsedPerObjectInfos);
	ConnUsedObjectInfos.ClearBit(InvalidObjectInfoIndex);
	ConnUsedObjectInfos.ForAllSetBits(ReleaseObjectBaselines);

}

void FDeltaCompressionBaselineManager::ConstructBaselineSharingContext(FBaselineSharingContext& SharingContext)
{
	// 按当前 PerObjectInfo 槽位数初始化 sharing 位图与 StateInfoIndex 表（每帧重新设大小，避免上下文残留）。
	SharingContext.CreatedBaselineCount = 0;
	SharingContext.ObjectInfoIndicesWithNewBaseline.Init(UsedPerObjectInfos.GetNumBits());
	SharingContext.ObjectInfoIndexToBaselineInfoIndex.SetNumZeroed(UsedPerObjectInfos.GetNumBits());
}

void FDeltaCompressionBaselineManager::DestructBaselineSharingContext(FBaselineSharingContext& SharingContext)
{
	auto ReleaseBaseline = [this, SharingContext](uint32 ObjectInfoIndex)
	{
		// 二段提交收尾：本对象本帧 Reserve 出来的槽位 → Commit 或 Cancel。
		// 注意 lambda 按值捕获 SharingContext 仅为读 ObjectInfoIndexToBaselineInfoIndex；不会双计数。
		const DeltaCompressionBaselineStateInfoIndexType BaselineStateInfoIndex = SharingContext.ObjectInfoIndexToBaselineInfoIndex[ObjectInfoIndex];
		BaselineStorage.OptionallyCommitAndDoReleaseBaseline(BaselineStateInfoIndex);
	};

	if (SharingContext.CreatedBaselineCount > 0)
	{
		SharingContext.ObjectInfoIndicesWithNewBaseline.ForAllSetBits(ReleaseBaseline);
	}

	// Reset context
	SharingContext = FBaselineSharingContext();
}

bool FDeltaCompressionBaselineManager::DoesObjectSupportDeltaCompression(uint32 ObjectIndex) const
{
	// 由 Protocol 编译期 trait 决定（通常需 fragment 全部 SupportsDeltaCompression）。
	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;
	return EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::SupportsDeltaCompression);
}

bool FDeltaCompressionBaselineManager::IsAllowedToCreateBaselineForObject(uint32 ConnId, uint32 ObjectIndex, const FPerObjectInfo* ObjectInfo, ObjectInfoIndexType InfoIndex) const
{
	// If no baselines are created we allow one to be created.
	// 中文：首基线必须建（无视节流）。这是 Writer 启动 SerializeDelta 路径的前提。
	if (BaselineCounts[InfoIndex] == 0)
	{
		return true;
	}

	// 节流：仅当距离上次创建 ≥ Min 帧数时允许。
	const uint32 FramesSincePrevBaselineCreation = FrameCounter - ObjectInfo->PrevBaselineCreationFrame;
	if ((MinimumNumberOfFramesBetweenBaselines >= 0) && (FramesSincePrevBaselineCreation >= static_cast<uint32>(MinimumNumberOfFramesBetweenBaselines)))
	{
		return true;
	}

	// MinimumNumberOfFramesBetweenBaselines<0 时永远走这个分支：仅用首基线，比对到底（极端策略）。
	return false;
}

void FDeltaCompressionBaselineManager::InvalidateBaselinesDueToModifiedConditionals()
{
	IRIS_PROFILER_SCOPE(DeltaCompressionBaselineManager_InvalidateBaselinesDueToModifiedConditionals);

	const FNetBitArray& ValidConnections = Connections->GetValidConnections();
	const uint32 MaxConnectionId = ValidConnections.FindLastOne();
	// If there are no connections there are no baselines to invalidate.
	if (MaxConnectionId == ~0U)
	{
		return;
	}

	// 逐条消费 InvalidationTracker 中的事件。
	// 每条 InvalidationInfo 形如 {ConnId, ObjectIndex}：
	//   ConnId == InvalidateBaselineForAllConnections → 对所有 ValidConnection 释放该对象的两个基线槽；
	//   ConnId == 具体连接                            → 只释放该连接的两个基线槽。
	for (const FDeltaCompressionBaselineInvalidationTracker::FInvalidationInfo& InvalidationInfo : BaselineInvalidationTracker->GetBaselineInvalidationInfos())
	{
		ObjectInfoIndexType ObjectInfoIndex = ObjectIndexToObjectInfoIndex[InvalidationInfo.ObjectIndex];
		if (ObjectInfoIndex == InvalidObjectInfoIndex)
		{
			continue;
		}

		uint16 BaselineCount = BaselineCounts[ObjectInfoIndex];
		if (BaselineCount == 0)
		{
			continue;
		}

		int16 BaselineCountAdjustment = 0;
		FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectInfoIndex);
		if (InvalidationInfo.ConnId == BaselineInvalidationTracker->InvalidateBaselineForAllConnections)
		{
			// 全连接作废：双层循环（每连接 × 双基线槽）。
			for (uint32 ConnectionId : ValidConnections)
			{
				FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[ConnectionId];
				for (const uint32 BaselineIndex : {0, 1})
				{
					FInternalBaseline& InternalBaseline = BaselineInfo.Baselines[BaselineIndex];
					if (InternalBaseline.IsValid())
					{
						--BaselineCountAdjustment;
						ReleaseInternalBaseline(InternalBaseline);
					}
				}
			}
		}
		else
		{
			// 单连接作废：跳过已断开的连接。
			if (!ValidConnections.GetBit(InvalidationInfo.ConnId))
			{
				continue;
			}

			FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[InvalidationInfo.ConnId];
			for (const uint32 BaselineIndex : {0, 1})
			{
				FInternalBaseline& InternalBaseline = BaselineInfo.Baselines[BaselineIndex];
				if (InternalBaseline.IsValid())
				{
					--BaselineCountAdjustment;
					ReleaseInternalBaseline(InternalBaseline);
				}
			}
		}

		AdjustBaselineCount(ObjectInfo, ObjectInfoIndex, BaselineCountAdjustment);
	}
}

/* Expect DestroyBaseline to be called even for baselines that no longer exist. As we invalidate baselines behind the scenes
 * one is to expect that it no longer exists. As such not even the ObjectInfo may exist anymore. */
// 中文：注意——本函数允许"对已被偷偷作废的基线"做调用，因此每一步都做 nullptr / Invalid 检查并安静返回。
void FDeltaCompressionBaselineManager::DestroyBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex, EChangeMaskBehavior ChangeMaskBehavior)
{
	if (BaselineIndex == InvalidBaselineIndex)
	{
		return;
	}

	const ObjectInfoIndexType ObjectInfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (ObjectInfoIndex == InvalidObjectInfoIndex)
	{
		return;
	}

	FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectInfoIndex);
	if (ObjectInfo == nullptr)
	{
		return;
	}

	UE_NET_DC_BASELINE_CHECK(BaselineIndex < MaxBaselineCount);

	FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[ConnId];
	FInternalBaseline& InternalBaseline = BaselineInfo.Baselines[BaselineIndex];
	if (!InternalBaseline.IsValid())
	{
		return;
	}

	// Merge baseline changemask into connection changemask if so desired.
	// 中文：丢包路径——把基线累计的脏位 OR 回 per-conn 当前累积，等下次基线建立时再"打包"出去。
	if (ChangeMaskBehavior == EChangeMaskBehavior::Merge)
	{
		ChangeMaskStorageType* ConnectionChangeMask = GetChangeMaskPointerForConnection(ObjectInfo, ConnId);
		const ChangeMaskStorageType* BaselineChangeMask = InternalBaseline.ChangeMask;
		for (uint32 WordIt = 0, WordEndIt = ObjectInfo->ChangeMaskStride; WordIt != WordEndIt; ++WordIt)
		{
			ConnectionChangeMask[WordIt] |= BaselineChangeMask[WordIt];
		}
	}
	
	// Release the baseline since the connection will have to ask for a new one to be created.
	ReleaseInternalBaseline(InternalBaseline);

	constexpr int16 BaselineCountAdjustment = -1;
	AdjustBaselineCount(ObjectInfo, ObjectInfoIndex, BaselineCountAdjustment);
}

FString FDeltaCompressionBaselineManager::PrintDeltaCompressionStatus(uint32 ConnectionId, FInternalNetRefIndex ObjectIndex) const
{
	// 调试转储；输出节流计数与 ChangeMask 字数。可在控制台命令链路中使用。
	if (const FPerObjectInfo* PerObjectInfo = GetPerObjectInfoForObject(ObjectIndex))
	{
		//TODO: Print per-connection info ?
		//FObjectBaselineInfo& BaselineInfo = ObjectInfo->BaselinesForConnections[ConnId];
		return FString::Printf(TEXT("PrevBaselineCreationFrame: %u | ChangeMaskStride: %u"), PerObjectInfo->PrevBaselineCreationFrame, PerObjectInfo->ChangeMaskStride);
	}
	else
	{
		return FString::Printf(TEXT("No delta compression info for %u"), ObjectIndex);
	}
}

}

#undef UE_NET_DC_BASELINE_CHECK
