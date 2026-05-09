// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：DirtyNetObjectTracker.cpp
// 模块：Iris / ReplicationSystem
// 功能：FDirtyNetObjectTracker 实现 + 全局 helper（MarkNetObjectStateDirty / ForceNetUpdate）。
// 协作：
//   * NetCore::FGlobalDirtyNetObjectTracker —— 跨 RS 的脏 NetHandle 集合 + 可选 PerProperty。
//   * LegacyPushModel.cpp —— MARK_PROPERTY_DIRTY 旧宏的桥接转发（间接通过 Global）。
//   * FNetRefHandleManager —— FNetHandle ↔ InternalIndex 翻译；GlobalScope 位图过滤。
//   * FReplicationConditionals —— PerProperty 模式下需要查询条件位图。
// =====================================================================================

#include "DirtyNetObjectTracker.h"
#include "Iris/IrisConstants.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"

#include "ProfilingDebugging/CsvProfiler.h"
#include "Traits/IntType.h"

DEFINE_LOG_CATEGORY(LogIrisDirtyTracker)

namespace UE::Net::Private
{

FDirtyNetObjectTracker::FDirtyNetObjectTracker()
: ReplicationSystemId(InvalidReplicationSystemId)
{
}

FDirtyNetObjectTracker::~FDirtyNetObjectTracker()
{
}

void FDirtyNetObjectTracker::Init(const FDirtyNetObjectTrackerInitParams& Params)
{
	check(Params.NetRefHandleManager != nullptr);

	NetRefHandleManager = Params.NetRefHandleManager;
	ReplicationSystemId = Params.ReplicationSystemId;
	
	NetObjectIdCount = Params.MaxInternalNetRefIndex;

	// 中文：向 Global 注册一个 Poller。Global 在每次 Reset 之前会回调
	// PreResetDelegate（这里传入 ApplyGlobalDirtyObjectList），保证在 Reset
	// 之前先把脏数据吸过来——双重保险，即使外层忘了 UpdateDirtyNetObjects。
	GlobalDirtyTrackerPollHandle = FGlobalDirtyNetObjectTracker::CreatePoller(FGlobalDirtyNetObjectTracker::FPreResetDelegate::CreateRaw(this, &FDirtyNetObjectTracker::ApplyGlobalDirtyObjectList));

	SetNetObjectListsSize(Params.MaxInternalNetRefIndex);

	// 中文：跟 NetRefHandleManager 关联——当其 Internal 索引扩容时同步本表 BitArray。
	NetRefHandleManager->GetOnMaxInternalNetRefIndexIncreasedDelegate().AddRaw(this, &FDirtyNetObjectTracker::OnMaxInternalNetRefIndexIncreased);

	// 中文：默认允许外部 Mark；在帧循环关键阶段会被 Lock 切换。
	AllowExternalAccess();

	UE_LOG(LogIrisDirtyTracker, Log, TEXT("FDirtyNetObjectTracker::Init[%u]: CurrentMaxSize: %u"), ReplicationSystemId, NetObjectIdCount);
}

void FDirtyNetObjectTracker::Deinit()
{
	NetRefHandleManager->GetOnMaxInternalNetRefIndexIncreasedDelegate().RemoveAll(this);
	// 中文：注销 Global Poller 句柄；之后即使外部还在 Mark Global，本 RS 也不会再吸。
	GlobalDirtyTrackerPollHandle.Destroy();
	bShouldResetPolledGlobalDirtyTracker = false;
}

void FDirtyNetObjectTracker::SetNetObjectListsSize(FInternalNetRefIndex NewMaxInternalIndex)
{
	// 中文：三个 BitArray 必须保持一致大小；NetRefHandleManager 是单一来源。
	AccumulatedDirtyNetObjects.SetNumBits(NewMaxInternalIndex);
	ForceNetUpdateObjects.SetNumBits(NewMaxInternalIndex);
	DirtyNetObjects.SetNumBits(NewMaxInternalIndex);
}

void FDirtyNetObjectTracker::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	SetNetObjectListsSize(NewMaxInternalIndex);
	NetObjectIdCount = NewMaxInternalIndex;
}

// ------------------------------------------------------------------------------------
// MarkPushbasedPropertiesDirty
// ------------------------------------------------------------------------------------
// 中文：被 ApplyGlobalDirtyObjectList 调用——把"对象的若干 RepIndex 脏"投射到
// 对应 fragment 的 MemberPollMask（属性级 Push 模式）。Polling 阶段会优先处理这些。
//
// 流程：
//   1. 通过 InternalIndex 取出 Replicated 数据（含 Protocol / InstanceProtocol）。
//   2. 校验 PushModelOwnerCount == 1（目前 Iris 只支持单一 push owner，UE-278338）。
//   3. 对每个被标脏的 RepIndex：
//      a. 查 RepIndexToFragmentIndexTable 找到 Fragment 与 MemberIndex；
//      b. 若该成员有 Lifetime Conditional 且条件位图未启用，则跳过；
//      c. 否则在 fragment 的 MemberPollMask 上把对应 MemberIndex 位置 1。
//   4. 若至少有一个属性命中，返回 true，调用方再把整个对象标到 DirtyNetObjects。
// ------------------------------------------------------------------------------------
bool FDirtyNetObjectTracker::MarkPushbasedPropertiesDirty(FInternalNetRefIndex ObjectIndex, uint16 OwnerIndex, const FNetBitArrayView& DirtyProperties)
{
	bool bShouldMarkObjectDirty = false;

	// For now, we do not support more than a single push based owner per protocol. JIRA: UE-278338
	if (!ensure(OwnerIndex == 0U))
	{
		return false;
	}

	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;
	const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;

	// Nothing to poll to or from.
	// 中文：实例已被解绑或 StateBuffer 不存在 —— 标记无效。
	if (!InstanceProtocol || !NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex))
	{
		return false;
	}
	
	if (Protocol->PushModelOwnerCount == 0U)
	{
		// 中文：协议不含 push-based 状态，无需 PerProperty 处理。
		return false;
	}

	if (Protocol->PushModelOwnerCount > 1U)
	{
		UE_LOG(LogIris, Error, TEXT("Failed to mark dirty due to too many pushmodel state owners ObjectIndex %d"), ObjectIndex);					
		return false;
	}

	const FNetRefHandleManager* LocalNetRefHandleManager = NetRefHandleManager;
	DirtyProperties.ForAllSetBits([ObjectIndex, OwnerIndex, LocalNetRefHandleManager,  &Protocol, &InstanceProtocol, &bShouldMarkObjectDirty](uint32 RepIndex)
		{
			// Dynamic part
			// 中文：RepIndex → FragmentIndex 的查表。RepIndex 是 UProperty 的 RepIndex；
			// FragmentIndexTable 是 Iris 在烘焙 Protocol 时建立的逆向映射。
			const FReplicationProtocol::FRepIndexToFragmentIndexTable& FragmentIndexTable = Protocol->PushModelOwnerRepIndexToFragmentIndexTable[OwnerIndex];
			if (RepIndex >= FragmentIndexTable.NumEntries)
			{
				UE_LOG(LogIris, Verbose, TEXT("Trying to mark invalid property dirty (Could be a disabled property). Invalid memberindex %s : RepIndex [%d][%d]"), *LocalNetRefHandleManager->PrintObjectFromIndex(ObjectIndex), OwnerIndex, RepIndex);
				return;
			}
			const FReplicationProtocol::FRepIndexToFragmentIndex FragmentIndex = FragmentIndexTable.RepIndexToFragmentIndex[RepIndex];

			if (FragmentIndex.FragmentIndex == FReplicationProtocol::FRepIndexToFragmentIndex::InvalidEntry)
			{
				// 中文：该 RepIndex 没有映射到任何 fragment（属性被 Disable 或非 push）。
				return;
			}

			const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[FragmentIndex.FragmentIndex];
			const FReplicationStateDescriptor* StateDescriptor = Protocol->ReplicationStateDescriptors[FragmentIndex.FragmentIndex];

			// Does this state contain this property?
			const FReplicationStateMemberRepIndexToMemberIndexDescriptor& RepIndexToMemberIndexDescriptor = StateDescriptor->MemberRepIndexToMemberIndexDescriptors[RepIndex];
			if (RepIndexToMemberIndexDescriptor.MemberIndex == FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry)
			{
				UE_LOG(LogIris, Verbose, TEXT("Trying to mark not existing property dirty. Invalid memberindex %s : RepIndex [%d][%d]"), *LocalNetRefHandleManager->PrintObjectFromIndex(ObjectIndex), OwnerIndex, RepIndex);
				return;
			}

			// Skip custom conditionals
			// 中文：若成员被 Lifetime/Custom Conditional 控制且当前条件位被关闭，
			// 跳过——避免把"理论上不会复制"的成员加进 Poll 队列。
			{
				const EReplicationStateTraits Traits = StateDescriptor->Traits;
				if (EnumHasAnyFlags(Traits, EReplicationStateTraits::HasLifetimeConditionals))
				{
					const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = StateDescriptor->MemberChangeMaskDescriptors[RepIndexToMemberIndexDescriptor.MemberIndex];

					if (ChangeMaskInfo.BitCount > 0)
					{
						FNetBitArrayView MemberConditionalChangeMask = Private::GetMemberConditionalChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);
						if (!MemberConditionalChangeMask.GetBit(ChangeMaskInfo.BitOffset))
						{
							return;
						}
					}
				}
			}

			// Mark dirty
			// 中文：在 fragment 的 MemberPollMask 上将该 MemberIndex 位置 1 ——
			// Polling 阶段会用它精确选取需要 Quantize 的成员，避免全量扫描。
			FNetBitArrayView DirtyMembersForPolling = GetMemberPollMask(Fragment.ExternalSrcBuffer, StateDescriptor);
			DirtyMembersForPolling.SetBit(RepIndexToMemberIndexDescriptor.MemberIndex);
			bShouldMarkObjectDirty |= true;
		}
	);

	return bShouldMarkObjectDirty;
}

// ------------------------------------------------------------------------------------
// ApplyGlobalDirtyObjectList
// ------------------------------------------------------------------------------------
// 中文：从 Global 拉脏数据 → 翻译到 InternalIndex → 写入 DirtyNetObjects。
// 两条路径：
//   * PerProperty 模式：从 Global 取 (Handle → 脏属性 RepIndex 位图)，再走
//     MarkPushbasedPropertiesDirty 把 RepIndex 投射到 fragment。
//   * 普通模式：只取 Handle 集合，把对象整体标脏。
// ------------------------------------------------------------------------------------
void FDirtyNetObjectTracker::ApplyGlobalDirtyObjectList()
{
	if (FGlobalDirtyNetObjectTracker::IsUsingPerPropertyDirtyTracking())
	{
		if (UReplicationSystem* ReplicationSystem = GetReplicationSystem(ReplicationSystemId))
		{
			FReplicationConditionals& Conditionals = ReplicationSystem->GetReplicationSystemInternal()->GetConditionals();

			const FGlobalDirtyNetObjectTracker::FDirtyHandleAndPropertyMap& GlobalDirtyNetObjects = FGlobalDirtyNetObjectTracker::GetDirtyNetObjectsAndProperties(GlobalDirtyTrackerPollHandle);
			for (const FGlobalDirtyNetObjectTracker::FDirtyHandleAndPropertyMap::ElementType& Element : GlobalDirtyNetObjects)
			{
				const FNetHandle NetHandle = Element.Key;

				const FInternalNetRefIndex NetObjectIndex = NetRefHandleManager->GetInternalIndexFromNetHandle(NetHandle);
				if (NetObjectIndex != FNetRefHandleManager::InvalidInternalIndex)
				{
					// Update changemasks based on rep-indices marked dirty
					const FGlobalDirtyNetObjectTracker::FDirtyPropertyStorage& DirtyPropertyData = Element.Value;
					if (DirtyPropertyData.Num())
					{
						// 中文：DirtyPropertyData 是按 32-bit 字打包的 RepIndex 位图，
						// 这里把字数 × 字宽得到 BitCount，再包成 NetBitArrayView。
						const uint32 BitCount = DirtyPropertyData.Num() * sizeof(FGlobalDirtyNetObjectTracker::FDirtyPropertyStorage::ElementType) * 8U;						
						constexpr uint32 OwnerIndex = 0U; // Always 0 as we currently onyly support a single push based fragment owner

						FNetBitArrayView DirtyRepIndices = MakeNetBitArrayView(DirtyPropertyData.GetData(), BitCount, FNetBitArrayView::NoResetNoValidate);
						if (const bool bShouldMarkObjectDirty = MarkPushbasedPropertiesDirty(NetObjectIndex, OwnerIndex, DirtyRepIndices))
						{
							// MarkObjectDirty
							DirtyNetObjects.SetBit(NetObjectIndex);							
						}
					}
					else
					{
						// If it was an explicit dirty call for the object, mark object dirty
						// 中文：没有 RepIndex 信息（即整对象级 Mark），整体标脏。
						DirtyNetObjects.SetBit(NetObjectIndex);
					}
				}
			}
		}
	}
	else
	{
		// 中文：普通模式——只关心 NetHandle 集合，不处理属性级。
		const TSet<FNetHandle>& GlobalDirtyNetObjects = FGlobalDirtyNetObjectTracker::GetDirtyNetObjects(GlobalDirtyTrackerPollHandle);
		for (FNetHandle NetHandle : GlobalDirtyNetObjects)
		{
			const FInternalNetRefIndex NetObjectIndex = NetRefHandleManager->GetInternalIndexFromNetHandle(NetHandle);
			if (NetObjectIndex != FNetRefHandleManager::InvalidInternalIndex)
			{
				DirtyNetObjects.SetBit(NetObjectIndex);
			}
		}
	}
}

void FDirtyNetObjectTracker::ApplyAndTryResetGlobalDirtyObjectList()
{
	ApplyGlobalDirtyObjectList();

	// 中文：仅当本 RS 是 Global 唯一 Poller 时立即 Reset；多 Poller 则失败，
	// 由 bShouldResetPolledGlobalDirtyTracker 留给 ReconcilePolledList 重试。
	FGlobalDirtyNetObjectTracker::ResetDirtyNetObjectsIfSinglePoller(GlobalDirtyTrackerPollHandle);

	bShouldResetPolledGlobalDirtyTracker = true;
}

// ------------------------------------------------------------------------------------
// UpdateDirtyNetObjects
// ------------------------------------------------------------------------------------
// 中文：StartPreSendUpdate 早期调用——
//   1. ApplyAndTryResetGlobalDirtyObjectList → 拉 Global 脏。
//   2. 用 GlobalScopeList（NetRefHandleManager 维护的"在该 RS 全局 Scope"位图）
//      过滤 DirtyNetObjects，避免已被销毁的对象残留脏标记。
//   3. AccumulatedDirtyNetObjects |= DirtyNetObjects（再 AndScope）。
// ------------------------------------------------------------------------------------
void FDirtyNetObjectTracker::UpdateDirtyNetObjects()
{
	if (!GlobalDirtyTrackerPollHandle.IsValid())
	{
		return;
	}

	IRIS_PROFILER_SCOPE(FDirtyNetObjectTracker_UpdateDirtyNetObjects)

	LockExternalAccess();

	ApplyAndTryResetGlobalDirtyObjectList();

	const uint32 NumWords = AccumulatedDirtyNetObjects.GetNumWords();

	// Note: We use the actual GlobalScopableInternalIndices BitArray as we allow new (sub)objects to be added
	// during the NetUpdate and we do not want them to be removed from the set of dirty objects.
	// 中文：取 GlobalScopableInternalIndices 的实时位图（不是快照）——这样
	// NetUpdate 中途新加的 (sub)object 不会被当成"已销毁"误清。
	const FNetBitArrayView GlobalScopeList = NetRefHandleManager->GetGlobalScopableInternalIndices();
	const uint32* GlobalScopeListData = GlobalScopeList.GetDataChecked(NumWords);
	
	uint32* AccumulatedDirtyNetObjectsData = AccumulatedDirtyNetObjects.GetDataChecked(NumWords);
	uint32* DirtyNetObjectsData = DirtyNetObjects.GetDataChecked(NumWords);
		
	for (uint32 WordIndex = 0; WordIndex < NumWords; ++WordIndex)
	{
		// Due to objects having been marked as dirty and later removed we must make sure that all dirty objects are still in scope.
		// 中文：DirtyNetObjects &= GlobalScope（去掉已被回收对象的脏）。
		uint32 DirtyObjectWord = DirtyNetObjectsData[WordIndex] & GlobalScopeListData[WordIndex];
		DirtyNetObjectsData[WordIndex] = DirtyObjectWord;

		// Add the latest dirty objects to the accumulated list and remove no-longer scoped objects that have never been copied.
		// 中文：Accumulated = (Accumulated | DirtyThisFrame) & GlobalScope
		// —— 累积新脏，但同时把已不在 Scope 中且从未拷贝过的对象一并清掉。
		AccumulatedDirtyNetObjectsData[WordIndex] = (AccumulatedDirtyNetObjectsData[WordIndex] | DirtyNetObjectsData[WordIndex]) & GlobalScopeListData[WordIndex];
	}

	AllowExternalAccess();
}

void FDirtyNetObjectTracker::UpdateAndLockDirtyNetObjects()
{
	if (!GlobalDirtyTrackerPollHandle.IsValid())
	{
		return;
	}
	
	UpdateDirtyNetObjects();

	// 中文：锁住 Global 直到下次 Reset，禁止在本帧后续阶段再改动 Global——
	// 适用于 Filter/Quantize 这些"已经不该再有新 Mark 出现"的阶段。
	FGlobalDirtyNetObjectTracker::LockDirtyListUntilReset(GlobalDirtyTrackerPollHandle);
}

void FDirtyNetObjectTracker::UpdateAccumulatedDirtyList()
{
	IRIS_PROFILER_SCOPE(FDirtyNetObjectTracker_UpdateDirtyNetObjects)
	// 中文：在 PreSendUpdate.Poll 之前再做一次 Accumulated |= DirtyThisFrame。
	// 因为 Bridge.PreSendUpdate 的 Class PreUpdate 回调可能再次 Mark 对象。
	AccumulatedDirtyNetObjects.Combine(DirtyNetObjects, FNetBitArray::OrOp);
}

void FDirtyNetObjectTracker::MarkNetObjectDirty(FInternalNetRefIndex NetObjectIndex)
{
#if UE_NET_THREAD_SAFETY_CHECK
	checkf(bIsExternalAccessAllowed, TEXT("Cannot mark objects dirty while the bitarray is locked for modifications."));
#endif

	if (NetObjectIndex >= NetObjectIdCount || NetObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		UE_LOG(LogIrisDirtyTracker, Warning, TEXT("FDirtyNetObjectTracker::MarkNetObjectDirty received invalid NetObjectIndex: %u | Max: %u"), NetObjectIndex, NetObjectIdCount);
		return;
	}

#if UE_NET_IRIS_CSV_STATS
	// 中文：仅当从 0→1 时才计数（避免重复 Mark 多次膨胀计数器）。
	PushModelDirtyObjectsCount += (DirtyNetObjects.IsBitSet(NetObjectIndex) ? 0 : 1);
#endif

	// 中文：手写位运算路径——避开 FNetBitArray::SetBit 的额外校验，热点路径精简。
	const uint32 BitOffset = NetObjectIndex;
	const StorageType BitMask = StorageType(1) << (BitOffset & (StorageTypeBitCount - 1));

	uint32* DirtyNetObjectsData = DirtyNetObjects.GetData();

	DirtyNetObjectsData[BitOffset/StorageTypeBitCount] |= BitMask;

	UE_LOG(LogIrisDirtyTracker, Verbose, TEXT("FDirtyNetObjectTracker::MarkNetObjectDirty[%u]: %s"), ReplicationSystemId, *NetRefHandleManager->PrintObjectFromIndex(NetObjectIndex));
}

void FDirtyNetObjectTracker::ForceNetUpdate(FInternalNetRefIndex NetObjectIndex)
{
#if UE_NET_IRIS_CSV_STATS
	ForceNetUpdateObjectsCount += (ForceNetUpdateObjects.IsBitSet(NetObjectIndex)?0:1);
#endif

	ForceNetUpdateObjects.SetBit(NetObjectIndex);

	// Flag the object dirty so we update filters etc too
	// 中文：仅设 Force 位还不够——必须同时进 DirtyNetObjects，
	// 让 Filtering/Prioritization 也能看到该对象需要被处理。
	MarkNetObjectDirty(NetObjectIndex);

	UE_LOG(LogIrisDirtyTracker, Verbose, TEXT("FDirtyNetObjectTracker::ForceNetUpdateObjects[%u]: %s"), ReplicationSystemId, *NetRefHandleManager->PrintObjectFromIndex(NetObjectIndex));
}

void FDirtyNetObjectTracker::LockExternalAccess()
{
#if UE_NET_THREAD_SAFETY_CHECK
	bIsExternalAccessAllowed = false;
#endif
}

void FDirtyNetObjectTracker::AllowExternalAccess()
{
#if UE_NET_THREAD_SAFETY_CHECK
	bIsExternalAccessAllowed = true;
#endif
}

FNetBitArrayView FDirtyNetObjectTracker::GetDirtyNetObjectsThisFrame()
{
#if UE_NET_THREAD_SAFETY_CHECK
	// 中文：必须先 LockExternalAccess（外部不能再 Mark）才能读取本帧脏列表，
	// 防止 Filter/Poll 阶段读到不一致快照。
	checkf(!bIsExternalAccessAllowed, TEXT("Cannot access the DirtyNetObjects bitarray unless its locked for multithread access."));
#endif
	return MakeNetBitArrayView(DirtyNetObjects);
}

// ------------------------------------------------------------------------------------
// ReconcilePolledList
// ------------------------------------------------------------------------------------
// 中文：PostSendUpdate 阶段调用——根据 Polling 阶段实际处理过的对象集合，
// 把对应位从三个状态位图中清除：
//   * ForceNetUpdateObjects     &=~ ObjectsPolled
//   * AccumulatedDirtyNetObjects &=~ ObjectsPolled
//   * DirtyNetObjects             清 0（本帧已结束）
//
// 同时若上一阶段未能 Reset Global（多 Poller 场景），现在再尝试一次。
// ------------------------------------------------------------------------------------
void FDirtyNetObjectTracker::ReconcilePolledList(const FNetBitArrayView& ObjectsPolled)
{
	LockExternalAccess();

	if (bShouldResetPolledGlobalDirtyTracker)
	{
		bShouldResetPolledGlobalDirtyTracker = false;
		FGlobalDirtyNetObjectTracker::ResetDirtyNetObjects(GlobalDirtyTrackerPollHandle);
	}

	// Clear ForceNetUpdate from every object that were polled.
	MakeNetBitArrayView(ForceNetUpdateObjects).Combine(ObjectsPolled, FNetBitArrayView::AndNotOp);

	// Clear dirty flags for objects that were polled
	MakeNetBitArrayView(AccumulatedDirtyNetObjects).Combine(ObjectsPolled, FNetBitArrayView::AndNotOp);

	// Clear the current frame dirty objects
	DirtyNetObjects.ClearAllBits();

	AllowExternalAccess();
}

#if UE_NET_IRIS_CSV_STATS
void FDirtyNetObjectTracker::ReportCSVStats()
{
	// 中文：把本帧累计的 PushModel/Force 计数上报到 CSV Profiler，并清零进入下一帧。
	CSV_CUSTOM_STAT(Iris, PushModelDirtyObjects, PushModelDirtyObjectsCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(Iris, ForceNetUpdateObjects, ForceNetUpdateObjectsCount, ECsvCustomStatOp::Set);

	PushModelDirtyObjectsCount = 0;
	ForceNetUpdateObjectsCount = 0;
}
#endif

#pragma region GlobalFunctions
// ------------------------------------------------------------------------------------
// 全局桥接 helper：按 RSId 派发到具体 FDirtyNetObjectTracker。
// 主要被 LegacyPushModel 桥、UReplicationSystem::MarkDirty / ForceNetUpdate 等调用。
// ------------------------------------------------------------------------------------

void MarkNetObjectStateDirty(uint32 ReplicationSystemId, FInternalNetRefIndex NetObjectIndex)
{
	if (UReplicationSystem* ReplicationSystem = GetReplicationSystem(ReplicationSystemId))
	{
		FDirtyNetObjectTracker& DirtyNetObjectTracker = ReplicationSystem->GetReplicationSystemInternal()->GetDirtyNetObjectTracker();
		DirtyNetObjectTracker.MarkNetObjectDirty(NetObjectIndex);
	}
}

void ForceNetUpdate(uint32 ReplicationSystemId, FInternalNetRefIndex NetObjectIndex)
{
	if (UReplicationSystem* ReplicationSystem = GetReplicationSystem(ReplicationSystemId))
	{
		FDirtyNetObjectTracker& DirtyNetObjectTracker = ReplicationSystem->GetReplicationSystemInternal()->GetDirtyNetObjectTracker();
		DirtyNetObjectTracker.ForceNetUpdate(NetObjectIndex);
	}
}

#pragma endregion

} // end namespace UE::Net::Private
