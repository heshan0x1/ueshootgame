// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// ReplicationConditionals.cpp
// ---------------------------------------------------------------------------------------------
// 实现：把"协议级 lifetime 条件 + 动态条件 + Owner / Autonomous / Physics + NetGroup"
//      汇总成 (Conn, Object) 的最终 ChangeMask 裁剪，并在条件变化时正确作废 DC 基线。
//
// 参考：Iris_Architecture.md §3.8、ReplicationSystem.md §6.3。
// =============================================================================================

#include "Iris/ReplicationSystem/Conditionals/ReplicationConditionals.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/ReplicationSystem/Conditionals/ReplicationCondition.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineInvalidationTracker.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectGroups.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFiltering.h"
#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Containers/ArrayView.h"
#include "UObject/CoreNetTypes.h"
#include "Net/Core/NetHandle/NetHandleManager.h"
#include "Net/Core/PropertyConditions/RepChangedPropertyTracker.h"
#include "Net/Core/PropertyConditions/PropertyConditions.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "HAL/IConsoleManager.h"

// 本子系统的日志通道（区别于 LogIris 总通道，便于过滤）。
DEFINE_LOG_CATEGORY_STATIC(LogIrisConditionals, Log, All);

namespace UE::Net::Private
{

// CVar：是否在每帧 Update 中把 ObjectsWithDirtyLifetimeConditionals 推到所有连接的 Writer。
// 调试或对比用，默认开启。
static bool bEnableUpdateObjectsWithDirtyConditionals = true;
static FAutoConsoleVariableRef CVarEnableUpdateObjectsWithDirtyConditionals(
	TEXT("net.Iris.EnableUpdateObjectsWithDirtyConditionals"),
	bEnableUpdateObjectsWithDirtyConditionals,
	TEXT("Enable the updating subobjects with conditionals."));

FReplicationConditionals::FReplicationConditionals()
{
}

// 初始化：保存依赖指针、按 Max{InternalNetRefIndex, ConnectionCount} 预分配数组。
// 注意 ConnectionInfos 大小为 MaxConnectionCount + 1（保留 0 号槽避免下标越界）。
void FReplicationConditionals::Init(FReplicationConditionalsInitParams& Params)
{
#if DO_CHECK
	// Verify we can handle max connection count
	// 中文：验证 AutonomousConnectionId 用 15 位足以存下 MaxConnectionCount。
	{
		const FPerObjectInfo ObjectInfo{static_cast<decltype(FPerObjectInfo::AutonomousConnectionId)>(Params.MaxConnectionCount)};
		check(ObjectInfo.AutonomousConnectionId == Params.MaxConnectionCount);
	}
#endif

	NetRefHandleManager = Params.NetRefHandleManager;
	ReplicationFiltering = Params.ReplicationFiltering;
	ReplicationConnections = Params.ReplicationConnections;
	BaselineInvalidationTracker = Params.BaselineInvalidationTracker;
	NetObjectGroups = Params.NetObjectGroups;
	MaxInternalNetRefIndex = Params.MaxInternalNetRefIndex;
	MaxConnectionCount = Params.MaxConnectionCount;

	PerObjectInfos.SetNumZeroed(MaxInternalNetRefIndex);
	ConnectionInfos.SetNum(MaxConnectionCount + 1U);
	ObjectsWithDirtyLifetimeConditionals.Init(Params.MaxInternalNetRefIndex);
}

void FReplicationConditionals::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	// NetRefHandle 池上限提高时同步扩容三块：
	//   * PerObjectInfos（每对象级条件信息）；
	//   * ObjectsWithDirtyLifetimeConditionals（脏对象位图）；
	//   * 已分配过的连接的 ObjectConditionals（per-conn 缓存）。
	MaxInternalNetRefIndex = NewMaxInternalIndex;

	PerObjectInfos.SetNumZeroed(NewMaxInternalIndex);

	ObjectsWithDirtyLifetimeConditionals.SetNumBits(NewMaxInternalIndex);

	for (FPerConnectionInfo& ConnectionInfo : ConnectionInfos)
	{
		// Resize the netobject list for valid connections
		if (!ConnectionInfo.ObjectConditionals.IsEmpty())
		{
			ConnectionInfo.ObjectConditionals.SetNumZeroed(NewMaxInternalIndex);
		}
	}
}

void FReplicationConditionals::OnInternalNetRefIndicesFreed(const TConstArrayView<FInternalNetRefIndex>& FreedIndices)
{
	IRIS_PROFILER_SCOPE(FReplicationConditionals_OnInternalNetRefIndicesFreed);
	// 1. 清掉每个被回收对象的 PerObjectInfo + DynamicConditions 条目；
	for (const FInternalNetRefIndex ObjectIndex : FreedIndices)
	{
		ClearPerObjectInfo(ObjectIndex);
	}

	// 2. 同步清所有 ValidConnection 上对该对象的 PerConn 缓存（避免下次同 index 复用残留状态）。
	const FNetBitArray& ValidConnections = ReplicationConnections->GetValidConnections();
	if (ValidConnections.FindLastOne() != FNetBitArrayBase::InvalidIndex)
	{
		for (const FInternalNetRefIndex ObjectIndex : FreedIndices)
		{
			ClearConnectionInfosForObject(ValidConnections, ObjectIndex);
		}
	}
}

void FReplicationConditionals::MarkLifeTimeConditionalsDirtyForObjectsInGroup(FNetObjectGroupHandle GroupHandle)
{
	IRIS_PROFILER_SCOPE(MarkLifeTimeConditionalsDirtyForObjectsInGroup)

	// Reserved group（系统保留）不允许整批标脏，避免对全体对象的误操作。
	const FNetObjectGroupHandle::FGroupIndexType GroupIndex = GroupHandle.GetGroupIndex();
	if (GroupHandle.IsReservedNetObjectGroup())
	{
		UE_LOG(LogIris, Warning, TEXT("FReplicationConditionals::MarkLifeTimeConditionalsDirtyForObjectsInGroup - Marking reserved group dirty is not allowed. GroupIndex: %u which is not allowed."), GroupIndex);
		return;
	}

	// 把 group 里每个成员都设进 ObjectsWithDirtyLifetimeConditionals，
	// 下一次 Update() 时会通知到所有连接的 ReplicationWriter。
	if (const FNetObjectGroup* Group = NetObjectGroups->GetGroup(GroupHandle))
	{
		for (FInternalNetRefIndex InternalObjectIndex : Group->Members)
		{
			ObjectsWithDirtyLifetimeConditionals.SetBit(InternalObjectIndex);
		}
	}
}

bool FReplicationConditionals::SetConditionConnectionFilter(FInternalNetRefIndex ObjectIndex, EReplicationCondition Condition, uint32 ConnectionId, bool bEnable)
{
	// 仅 RoleAutonomous 支持按连接过滤。其余动态条件请走 SetCondition。
	if (ConnectionId >= MaxConnectionCount)
	{
		return false;
	}

	if (!ensure(Condition == EReplicationCondition::RoleAutonomous))
	{
		UE_LOG(LogIris, Error, TEXT("Only EReplicationCondition::RoleAutonomous supports connection filtering, got '%u'."), uint32(Condition));
		return false;
	}

	// 关闭或 ConnectionId==0 都视为"无 Autonomous 连接"。
	const uint32 AutonomousConnectionId = (ConnectionId == 0U || !bEnable) ? 0U : ConnectionId;
	FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectIndex);
	if (ObjectInfo->AutonomousConnectionId != AutonomousConnectionId)
	{
		UE_LOG(LogIrisConditionals, Verbose, TEXT("SetConditionConnectionFilter %s. AutonomousConnectionId: %u"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), AutonomousConnectionId);

		// 作废基线时只针对"刚被加上"或"刚被取消"的那条连接（节省比对）。
		const uint32 ConnIdForBaselineInvalidation = (bEnable ? ConnectionId : ObjectInfo->AutonomousConnectionId);
		ObjectInfo->AutonomousConnectionId = uint16(AutonomousConnectionId);

		// 副作用 1：把 RemoteRole 标脏，让对端正确切换 ENetRole。
		MarkRemoteRoleDirty(ObjectIndex);
		// Mark object as having dirty global conditional that should be evaluated before next send
		// 副作用 2：下一次 Update 时通知发送侧重新评估该对象的 lifetime 条件。
		ObjectsWithDirtyLifetimeConditionals.SetBit(ObjectIndex);

		// 副作用 3：作废变化连接上的基线（旧基线对应的 SimulatedOnly 等条件结果已不再正确）。
		InvalidateBaselinesForObjectHierarchy(ObjectIndex, TConstArrayView<uint32>(&ConnIdForBaselineInvalidation, 1));
	}

	return true;
}

void FReplicationConditionals::SetOwningConnection(FInternalNetRefIndex ObjectIndex, uint32 OwningConnectionId)
{
	// 比较旧/新 OwningConnection（实际值由 FReplicationFiltering 维护，这里只读）。
	const uint32 OldOwningConnectionId = ReplicationFiltering->GetOwningConnection(ObjectIndex);
	if (OldOwningConnectionId != OwningConnectionId && (OwningConnectionId == InvalidConnectionId || ReplicationConnections->IsValidConnection(OwningConnectionId)))
	{
		UE_LOG(LogIrisConditionals, Verbose, TEXT("SetOwningConnection on object %u. Connection: %u"), ObjectIndex, OwningConnectionId);

		// Mark object as having dirty global conditional that should be evaluated before next send
		// owner 变化会影响 COND_OwnerOnly / COND_SkipOwner / COND_InitialOrOwner / COND_ReplayOrOwner。
		ObjectsWithDirtyLifetimeConditionals.SetBit(ObjectIndex);

		// Invalidate baselines for connections affected by the owner change.
		// 旧 owner 与新 owner 的基线都要作废（条件可能从 disabled 翻到 enabled，引入新字段）。
		{
			const uint32 ConnectionIdToInvalidateCandidates[] = {OldOwningConnectionId, OwningConnectionId};
			uint32 ConnectionIdsToInvalidate[UE_ARRAY_COUNT(ConnectionIdToInvalidateCandidates)];
			uint32 ConnectionIdCount = 0;
			for (uint32 ConnectionId : ConnectionIdToInvalidateCandidates)
			{
				if (ConnectionId != InvalidConnectionId)
				{
					ConnectionIdsToInvalidate[ConnectionIdCount++] = ConnectionId;
				}
			}
			InvalidateBaselinesForObjectHierarchy(ObjectIndex, TConstArrayView<uint32>(ConnectionIdsToInvalidate, ConnectionIdCount));
		}
	}
}

void FReplicationConditionals::AddConnection(uint32 ConnectionId)
{
	// Init connection info
	// 中文：为新连接的 ObjectConditionals 数组按当前 MaxInternalNetRefIndex 大小开辟空间，初始全 0。
	FPerConnectionInfo& ConnectionInfo = ConnectionInfos[ConnectionId];
	ConnectionInfo.ObjectConditionals.SetNumZeroed(MaxInternalNetRefIndex);
}

void FReplicationConditionals::RemoveConnection(uint32 ConnectionId)
{
	// Reset connection info
	// 中文：连接断开时立即释放数组（其他连接不受影响）。
	FPerConnectionInfo& ConnectionInfo = ConnectionInfos[ConnectionId];
	ConnectionInfo.ObjectConditionals.Empty();
}

bool FReplicationConditionals::SetCondition(FInternalNetRefIndex ObjectIndex, EReplicationCondition Condition, bool bEnable)
{
	// RoleAutonomous 必须走带 ConnectionId 的版本。
	if (!ensure(Condition != EReplicationCondition::RoleAutonomous))
	{
		UE_LOG(LogIris, Error, TEXT("%s"), TEXT("EReplicationCondition::RoleAutonomous requires a connection."));
		return false;
	}

	if (Condition == EReplicationCondition::ReplicatePhysics)
	{
		FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ObjectIndex);
		// 仅在"由关→开"这一转变时作废基线（开→关只是收紧条件，不会引入未知字段）。
		if (bEnable && !ObjectInfo->bRepPhysics)
		{
			UE_LOG(LogIrisConditionals, Verbose, TEXT("SetCondition object %s. EReplicationCondition::ReplicatePhysics: %u"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), bEnable ? 1U : 0U);

			// We only care to track this change if the condition is enabled.
			// 因为对象级 Physics 是全局开关，所有连接的基线都需作废。
			const uint32 ConnIdForBaselineInvalidation = BaselineInvalidationTracker->InvalidateBaselineForAllConnections;
			InvalidateBaselinesForObjectHierarchy(ObjectIndex, TConstArrayView<uint32>(&ConnIdForBaselineInvalidation, 1));

			// Mark object as having dirty global conditional that should be evaluated before next send
			ObjectsWithDirtyLifetimeConditionals.SetBit(ObjectIndex);
		}
		ObjectInfo->bRepPhysics = bEnable ? 1U : 0U;
		return true;
	}

	ensureMsgf(false, TEXT("Unhandled EReplicationCondition '%u'"), uint32(Condition));
	return false;
}

void FReplicationConditionals::InitPropertyCustomConditions(FInternalNetRefIndex ObjectIndex)
{
	IRIS_PROFILER_SCOPE(FReplicationConditionals_InitPropertyCustomConditions);

	// 流程：
	//   1) 跳过没有 lifetime conditionals trait 的对象；
	//   2) 找出所有带 HasLifetimeConditionals 的 state，并取其 Fragment 的 Owner（UObject*）；
	//   3) 在该 Owner 上拿 FRepChangedPropertyTracker（兼容旧条件系统的"激活态/动态条件"载体）；
	//   4) 对每个成员：若 Tracker 不激活，则在外部 state buffer 的 ConditionalChangeMask 上清位；
	//      若该成员声明 COND_Dynamic，把 Tracker 中的具体 ELifetimeCondition 同步到本类。
	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;

	if (NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex) == nullptr || !EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasLifetimeConditionals))
	{
		return;
	}

	// Set up fragment owner collector once, currently we only support a single owner
	constexpr uint32 MaxFragmentOwnerCount = 1U;
	UObject* FragmentOwners[MaxFragmentOwnerCount] = {};
	FReplicationStateOwnerCollector FragmentOwnerCollector(FragmentOwners, MaxFragmentOwnerCount);

	FReplicationFragment* const * Fragments = ReplicatedObjectData.InstanceProtocol->Fragments;
	const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;
	const uint32 FragmentCount = InstanceProtocol->FragmentCount;
	const uint32 FirstRelevantStateIndex = Protocol->FirstLifetimeConditionalsStateIndex;

	const UObject* LastOwner = nullptr;
	for (const FReplicationStateDescriptor*& StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors + FirstRelevantStateIndex, static_cast<int32>(Protocol->ReplicationStateCount - FirstRelevantStateIndex)))
	{
		if (EnumHasAnyFlags(StateDescriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
		{
			const SIZE_T StateIndex = &StateDescriptor - Protocol->ReplicationStateDescriptors;

			// Get Owner
			const FReplicationFragment* ReplicationFragment = InstanceProtocol->Fragments[StateIndex];
			FragmentOwnerCollector.Reset();
			ReplicationFragment->CollectOwner(&FragmentOwnerCollector);
			if (FragmentOwnerCollector.GetOwnerCount() == 0U)
			{
				// We have no owner
				continue;
			}

			const UObject* CurrentOwner = FragmentOwnerCollector.GetOwners()[0];
			TSharedPtr<FRepChangedPropertyTracker> ChangedPropertyTracker;
			if (CurrentOwner != LastOwner)
			{				
				ChangedPropertyTracker = FNetPropertyConditionManager::Get().FindOrCreatePropertyTracker(CurrentOwner);
			}

			const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[StateIndex];
			FNetBitArrayView ConditionalChangeMask = GetMemberConditionalChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);

			// Initialize conditionals based on the state of the ChangedPropertyTracker
			const FRepChangedPropertyTracker* Tracker = ChangedPropertyTracker.Get();
			for (const FReplicationStateMemberLifetimeConditionDescriptor& MemberLifeTimeConditionDescriptor : MakeArrayView(StateDescriptor->MemberLifetimeConditionDescriptors, StateDescriptor->MemberCount))
			{
				const SIZE_T MemberIndex = &MemberLifeTimeConditionDescriptor - StateDescriptor->MemberLifetimeConditionDescriptors;
				const uint16 RepIndex = StateDescriptor->MemberProperties[MemberIndex]->RepIndex;
				if (!Tracker->IsParentActive(RepIndex))
				{
					const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = StateDescriptor->MemberChangeMaskDescriptors[MemberIndex];
					ConditionalChangeMask.ClearBits(MemberChangeMaskDescriptor.BitOffset, MemberChangeMaskDescriptor.BitCount);
				}

				if (MemberLifeTimeConditionDescriptor.Condition == COND_Dynamic)
				{
					ELifetimeCondition Condition = Tracker->GetDynamicCondition(RepIndex);
					if (Condition != COND_Dynamic)
					{
						SetDynamicCondition(ObjectIndex, RepIndex, Condition);
					}
				}
			}
		}
	}
}

// N.B. Calls can come for properties that have been disabled. We must handle such cases gracefully.
// 中文：调用可能针对早已被禁用的属性，找不到对应 RepIndex 时仅警告、不视为错误。
//
// 实现分两条路径：
//   * Protocol->LifetimeConditionalsStateCount == 1：单 state 的快路径（绝大多数对象）；
//   * 否则：遍历所有带 lifetime 条件的 state，按 Owner 匹配。
// 共同动作：
//   bIsActive=true  -> ConditionalChangeMask 置位，且把对应 ChangeMask 同步置脏；作废所有连接基线；
//   bIsActive=false -> 仅 ConditionalChangeMask 清位（关闭已发字段不会引入未知值，无需作废）。
bool FReplicationConditionals::SetPropertyCustomCondition(FInternalNetRefIndex ObjectIndex, const void* Owner, uint16 RepIndex, bool bIsActive)
{
	IRIS_PROFILER_SCOPE(FReplicationConditionals_SetPropertyCustomCondition);

	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;

	if (NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex) == nullptr || !EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasLifetimeConditionals))
	{
		return false;
	}

	if (Protocol->LifetimeConditionalsStateCount == 1U)
	{
		const SIZE_T StateIndex = Protocol->FirstLifetimeConditionalsStateIndex;
		const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;
		const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[StateIndex];
		const FReplicationStateDescriptor* StateDescriptor = Protocol->ReplicationStateDescriptors[StateIndex];

		// Note: In this optimized code path we assume the passed Owner is the fragment owner. No checks.
		if (RepIndex >= StateDescriptor->RepIndexCount)
		{
			UE_LOG(LogIris, Warning, TEXT("Trying to change non-existing custom conditional for RepIndex %u in protocol %s"), RepIndex, ToCStr(Protocol->DebugName));
			return false;
		}

		// Modify the external state changemasks accordingly.
		const FReplicationStateMemberRepIndexToMemberIndexDescriptor& RepIndexToMemberIndexDescriptor = StateDescriptor->MemberRepIndexToMemberIndexDescriptors[RepIndex];
		if (RepIndexToMemberIndexDescriptor.MemberIndex == FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry)
		{
			UE_LOG(LogIris, Warning, TEXT("Trying to change non-existing custom conditional for RepIndex %u in protocol %s"), RepIndex, ToCStr(Protocol->DebugName));
			return false;
		}

		const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = StateDescriptor->MemberChangeMaskDescriptors[RepIndexToMemberIndexDescriptor.MemberIndex];
		FNetBitArrayView ConditionalChangeMask = UE::Net::Private::GetMemberConditionalChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);

		if (bIsActive)
		{
			ConditionalChangeMask.SetBits(ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount);

			// If a condition is enabled we also mark the corresponding regular changemask as dirty.
			FNetBitArrayView MemberChangeMask = UE::Net::Private::GetMemberChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);
			FReplicationStateHeader& ReplicationStateHeader = UE::Net::Private::GetReplicationStateHeader(Fragment.ExternalSrcBuffer, StateDescriptor);
			MarkDirty(ReplicationStateHeader, MemberChangeMask, ChangeMaskDescriptor);

			// Enabled conditions causes new properties to be replicated which most likely have incorrect values at the receiving end.
			BaselineInvalidationTracker->InvalidateBaselines(ObjectIndex, BaselineInvalidationTracker->InvalidateBaselineForAllConnections);
		}
		else
		{
			ConditionalChangeMask.ClearBits(ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount);
		}
	}
	else
	{
		// Set up fragment owner collector once.
		constexpr uint32 MaxFragmentOwnerCount = 1U;
		UObject* FragmentOwners[MaxFragmentOwnerCount] = {};
		FReplicationStateOwnerCollector FragmentOwnerCollector(FragmentOwners, MaxFragmentOwnerCount);

		const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;

		const uint32 FirstRelevantStateIndex = Protocol->FirstLifetimeConditionalsStateIndex;
		for (const FReplicationStateDescriptor*& StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors + FirstRelevantStateIndex, static_cast<int32>(Protocol->ReplicationStateCount - FirstRelevantStateIndex)))
		{
			if (EnumHasAnyFlags(StateDescriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
			{
				const SIZE_T StateIndex = &StateDescriptor - Protocol->ReplicationStateDescriptors;

				// Is the passed Owner the owner of the fragment?
				{
					const FReplicationFragment* ReplicationFragment = InstanceProtocol->Fragments[StateIndex];

					FragmentOwnerCollector.Reset();
					ReplicationFragment->CollectOwner(&FragmentOwnerCollector);
					if (FragmentOwnerCollector.GetOwnerCount() == 0U || FragmentOwnerCollector.GetOwners()[0] != Owner)
					{
						// Not the right owner.
						continue;
					}
				}

				// Can this state contain this property?
				if (RepIndex >= StateDescriptor->RepIndexCount)
				{
					continue;
				}

				// Does this state contain this property?
				const FReplicationStateMemberRepIndexToMemberIndexDescriptor& RepIndexToMemberIndexDescriptor = StateDescriptor->MemberRepIndexToMemberIndexDescriptors[RepIndex];
				if (RepIndexToMemberIndexDescriptor.MemberIndex == FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry)
				{
					continue;
				}

				// We found the relevant state. Modify the external state changemasks.
				const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[StateIndex];

				const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = StateDescriptor->MemberChangeMaskDescriptors[RepIndexToMemberIndexDescriptor.MemberIndex];
				FNetBitArrayView ConditionalChangeMask = GetMemberConditionalChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);

				if (bIsActive)
				{
					ConditionalChangeMask.SetBits(ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount);

					// If a condition is enabled we also mark the corresponding regular changemask as dirty.
					FNetBitArrayView MemberChangeMask = GetMemberChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);
					FReplicationStateHeader& Header = GetReplicationStateHeader(Fragment.ExternalSrcBuffer, StateDescriptor);

					MarkDirty(Header, MemberChangeMask, ChangeMaskDescriptor);

					// Enabled conditions causes new properties to be replicated which most likely have incorrect values at the receiving end.
					BaselineInvalidationTracker->InvalidateBaselines(ObjectIndex, BaselineInvalidationTracker->InvalidateBaselineForAllConnections);
				}
				else
				{
					ConditionalChangeMask.ClearBits(ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount);
				}

				return true;
			}
		}

		UE_LOG(LogIris, Warning, TEXT("Trying to change non-existing custom conditional for RepIndex %u in protocol %s"), RepIndex, ToCStr(Protocol->DebugName));
	}

	return false;
}

bool FReplicationConditionals::SetPropertyDynamicCondition(FInternalNetRefIndex ObjectIndex, const void* Owner, uint16 RepIndex, ELifetimeCondition Condition)
{
	IRIS_PROFILER_SCOPE(FReplicationConditionals_SetPropertyDynamicCondition);

	// 与 SetPropertyCustomCondition 同形：单 state 快路径 + 多 state 通路。
	// 关键检查：成员的 lifetime 条件必须是 COND_Dynamic（否则属于声明错误）。
	// 在 DynamicConditionChangeRequiresBaselineInvalidation 返回 true 时
	// （即"以前可能没发→现在可能要发"）才作废所有连接基线 + 标脏 ChangeMask。
	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;

	if (NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex) == nullptr || !EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasLifetimeConditionals))
	{
		return false;
	}

	if (Protocol->LifetimeConditionalsStateCount == 1U)
	{
		const SIZE_T StateIndex = Protocol->FirstLifetimeConditionalsStateIndex;
		const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;
		const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[StateIndex];
		const FReplicationStateDescriptor* StateDescriptor = Protocol->ReplicationStateDescriptors[StateIndex];

		if (RepIndex >= StateDescriptor->RepIndexCount)
		{
			UE_LOG(LogIris, Warning, TEXT("Trying to change non-existing dynamic conditional for RepIndex %u in protocol %s"), RepIndex, ToCStr(Protocol->DebugName));
			return false;
		}

		const FReplicationStateMemberRepIndexToMemberIndexDescriptor& RepIndexToMemberIndexDescriptor = StateDescriptor->MemberRepIndexToMemberIndexDescriptors[RepIndex];
		const uint16 MemberIndex = RepIndexToMemberIndexDescriptor.MemberIndex;
		if ((MemberIndex == FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry))
		{
			UE_LOG(LogIris, Warning, TEXT("Trying to change non-existing dynamic conditional for RepIndex %u in protocol %s"), RepIndex, ToCStr(Protocol->DebugName));
			return false;
		}

		if (StateDescriptor->MemberLifetimeConditionDescriptors[MemberIndex].Condition != COND_Dynamic)
		{
			UE_LOG(LogIris, Warning, TEXT("Trying to change condition for member %s with wrong condition in protocol %s"), ToCStr(StateDescriptor->MemberDebugDescriptors[MemberIndex].DebugName), ToCStr(Protocol->DebugName));
			return false;
		}

		const ELifetimeCondition OldCondition = GetDynamicCondition(ObjectIndex, RepIndex);
		SetDynamicCondition(ObjectIndex, RepIndex, Condition);

		// If a condition may cause something to go from not replicated to replicated we mark the changemask as dirty and invalidate baselines.
		if (DynamicConditionChangeRequiresBaselineInvalidation(OldCondition, Condition))
		{
			const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = StateDescriptor->MemberChangeMaskDescriptors[MemberIndex];

			FNetBitArrayView MemberChangeMask = UE::Net::Private::GetMemberChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);
			FReplicationStateHeader& ReplicationStateHeader = UE::Net::Private::GetReplicationStateHeader(Fragment.ExternalSrcBuffer, StateDescriptor);
			MarkDirty(ReplicationStateHeader, MemberChangeMask, ChangeMaskDescriptor);

			// $TODO Consider more extensive checking to see if only a single connection requires baseline invalidation.
			BaselineInvalidationTracker->InvalidateBaselines(ObjectIndex, BaselineInvalidationTracker->InvalidateBaselineForAllConnections);
		}

		return true;
	}
	else
	{
		constexpr uint32 MaxFragmentOwnerCount = 1U;
		UObject* FragmentOwners[MaxFragmentOwnerCount] = {};
		FReplicationStateOwnerCollector FragmentOwnerCollector(FragmentOwners, MaxFragmentOwnerCount);

		const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;

		const uint32 FirstRelevantStateIndex = Protocol->FirstLifetimeConditionalsStateIndex;
		for (const FReplicationStateDescriptor*& StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors + FirstRelevantStateIndex, static_cast<int32>(Protocol->ReplicationStateCount - FirstRelevantStateIndex)))
		{
			if (EnumHasAnyFlags(StateDescriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
			{
				const SIZE_T StateIndex = &StateDescriptor - Protocol->ReplicationStateDescriptors;

				// Is the passed Owner the owner of the fragment?
				{
					const FReplicationFragment* ReplicationFragment = InstanceProtocol->Fragments[StateIndex];

					FragmentOwnerCollector.Reset();
					ReplicationFragment->CollectOwner(&FragmentOwnerCollector);
					if (FragmentOwnerCollector.GetOwnerCount() == 0U || FragmentOwnerCollector.GetOwners()[0] != Owner)
					{
						// Not the right owner.
						continue;
					}
				}

				// Can this state contain this property?
				if (RepIndex >= StateDescriptor->RepIndexCount)
				{
					continue;
				}

				// Does this state contain this property?
				const FReplicationStateMemberRepIndexToMemberIndexDescriptor& RepIndexToMemberIndexDescriptor = StateDescriptor->MemberRepIndexToMemberIndexDescriptors[RepIndex];
				const uint16 MemberIndex = RepIndexToMemberIndexDescriptor.MemberIndex;
				if (MemberIndex == FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry)
				{
					continue;
				}

				// We've found the relevant state. Verify it's a dynamic condition member.
				if (StateDescriptor->MemberLifetimeConditionDescriptors[MemberIndex].Condition != COND_Dynamic)
				{
					UE_LOG(LogIris, Warning, TEXT("Trying to change condition for member %s with wrong condition in protocol %s"), ToCStr(StateDescriptor->MemberDebugDescriptors[MemberIndex].DebugName), ToCStr(Protocol->DebugName));
					return false;
				}

				const ELifetimeCondition OldCondition = GetDynamicCondition(ObjectIndex, RepIndex);
				SetDynamicCondition(ObjectIndex, RepIndex, Condition);

				// If a condition may cause something to go from not replicated to replicated we mark the changemask as dirty and invalidate baselines.
				if (DynamicConditionChangeRequiresBaselineInvalidation(OldCondition, Condition))
				{
					const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[StateIndex];
					const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = StateDescriptor->MemberChangeMaskDescriptors[MemberIndex];

					FNetBitArrayView MemberChangeMask = UE::Net::Private::GetMemberChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);
					FReplicationStateHeader& ReplicationStateHeader = UE::Net::Private::GetReplicationStateHeader(Fragment.ExternalSrcBuffer, StateDescriptor);
					MarkDirty(ReplicationStateHeader, MemberChangeMask, ChangeMaskDescriptor);

					BaselineInvalidationTracker->InvalidateBaselines(ObjectIndex, BaselineInvalidationTracker->InvalidateBaselineForAllConnections);
				}

				return true;
			}
		}
	}

	return false;
}

void FReplicationConditionals::Update()
{
	// 帧主循环。受 CVar 控制，便于排查兼容性问题。
	// 当前唯一动作：把脏对象通知给所有 ValidConnection 的 ReplicationWriter，
	// 让它们 invalidate 已生成的 PerObjectInfo 状态机，下次 Write 时重新评估。
	if (bEnableUpdateObjectsWithDirtyConditionals)
	{
		UpdateAndResetObjectsWithDirtyConditionals();
	}
}

void FReplicationConditionals::GetChildSubObjectsToReplicate(uint32 ReplicatingConnectionId, const FConditionalsMask& LifetimeConditionals,  const FInternalNetRefIndex ParentObjectIndex, FSubObjectsToReplicateArray& OutSubObjectsToReplicate)
{
	// To mimic old system we use a weird replication order based on hierarchy, SubSubObjects are replicated before the parent
	// 中文：保持与旧系统一致的"后序"递归——先递归到孙节点，再 Add 子节点，确保父对象之前其后代已就绪。
	FChildSubObjectsInfo SubObjectsInfo;
	if (NetRefHandleManager->GetChildSubObjects(ParentObjectIndex, SubObjectsInfo))
	{
		// 没有 lifetime 条件描述：所有 child 都无条件复制。
		if (SubObjectsInfo.SubObjectLifeTimeConditions == nullptr)
		{
			for (uint32 ArrayIndex = 0; ArrayIndex < SubObjectsInfo.NumSubObjects; ++ArrayIndex)
			{
				FInternalNetRefIndex SubObjectIndex = SubObjectsInfo.ChildSubObjects[ArrayIndex];
				GetChildSubObjectsToReplicate(ReplicatingConnectionId, LifetimeConditionals, SubObjectIndex, OutSubObjectsToReplicate);
				OutSubObjectsToReplicate.Add(SubObjectIndex);
			}
		}
		else
		{
			// Append child subobjects that fulfill the condition
			for (uint32 ArrayIndex = 0; ArrayIndex < SubObjectsInfo.NumSubObjects; ++ArrayIndex)
			{
				const FInternalNetRefIndex SubObjectIndex = SubObjectsInfo.ChildSubObjects[ArrayIndex];
				const ELifetimeCondition LifeTimeCondition = (ELifetimeCondition)SubObjectsInfo.SubObjectLifeTimeConditions[ArrayIndex];
				if (LifeTimeCondition == COND_NetGroup)
				{
					// COND_NetGroup：通过该 SubObject 加入的 NetGroup 决定是否复制。
					// 三类特判：
					//   * NetGroupOwner  -> 走 COND_OwnerOnly；
					//   * NetGroupReplay -> 走 COND_ReplayOnly；
					//   * 其它           -> 走 SubObjectFilter（白名单/黑名单连接组）。
					bool bShouldReplicateSubObject = false;
					//TArray<FNetObjectGroupHandle> GroupsMemberOf;
					//NetObjectGroups->GetGroupHandlesOfNetObject(SubObjectIndex, GroupsMemberOf);
					// 
					 const TArrayView<const FNetObjectGroupHandle::FGroupIndexType> GroupIndexes = NetObjectGroups->GetGroupIndexesOfNetObject(SubObjectIndex);
					for (const FNetObjectGroupHandle::FGroupIndexType GroupIndex : GroupIndexes)
					{
						const FNetObjectGroupHandle NetGroup = NetObjectGroups->GetHandleFromIndex(GroupIndex);

						if (NetGroup.IsNetGroupOwnerNetObjectGroup())
						{
							bShouldReplicateSubObject = LifetimeConditionals.IsConditionEnabled(COND_OwnerOnly);
						}
						else if (NetGroup.IsNetGroupReplayNetObjectGroup())
						{
							bShouldReplicateSubObject = LifetimeConditionals.IsConditionEnabled(COND_ReplayOnly);
						}
						else
						{
							ENetFilterStatus ReplicationStatus = ENetFilterStatus::Disallow;
							ensureMsgf(ReplicationFiltering->GetSubObjectFilterStatus(NetGroup, ReplicatingConnectionId, ReplicationStatus), TEXT("FReplicationConditionals::GetChildSubObjectsToReplicat Trying to filter with group %u that is not a SubObjectFilterGroup"), NetGroup.GetGroupIndex());
							bShouldReplicateSubObject = ReplicationStatus != ENetFilterStatus::Disallow;
						}
						
						if (bShouldReplicateSubObject)
						{
							// 任一组允许即可复制（OR 语义）。
							GetChildSubObjectsToReplicate(ReplicatingConnectionId, LifetimeConditionals, SubObjectIndex, OutSubObjectsToReplicate);
							OutSubObjectsToReplicate.Add(SubObjectIndex);
							break;
						}
					}

					UE_CLOG(!bShouldReplicateSubObject, LogIrisConditionals, VeryVerbose, TEXT("%s Filtered out by COND_NetGroup"), *NetRefHandleManager->PrintObjectFromIndex(SubObjectIndex));
				}
				else if (LifetimeConditionals.IsConditionEnabled(LifeTimeCondition))
				{
					// 非 NetGroup：直接看 LifetimeConditionals 中对应 bit。
					GetChildSubObjectsToReplicate(ReplicatingConnectionId, LifetimeConditionals, SubObjectIndex, OutSubObjectsToReplicate);
					OutSubObjectsToReplicate.Add(SubObjectIndex);
				}
				else
				{
					UE_LOG(LogIrisConditionals, VeryVerbose, TEXT("%s Filtered out by %s"), *NetRefHandleManager->PrintObjectFromIndex(SubObjectIndex), *UEnum::GetValueAsString(LifeTimeCondition));
				}
			}
		}
	}
}

void FReplicationConditionals::GetSubObjectsToReplicate(uint32 ReplicationConnectionId, FInternalNetRefIndex RootObjectIndex, FSubObjectsToReplicateArray& OutSubObjectsToReplicate)
{
	//IRIS_PROFILER_SCOPE_VERBOSE(FReplicationConditionals_GetSubObjectsToReplicate);

	// For now, we do nothing to detect if a conditional has changed on the RootParent, we simply defer this until the next
	// time the subobjects are marked as dirty. We might want to consider to explicitly mark object and subobjects as dirty when 
	// the owning connections or conditionals such as bRepPhysics or Role is changed.
	// 中文：注意我们这里 bInitialState 总是 false——这是被 ReplicationWriter 在常规帧中调用的入口，
	//       initial 路径在 ApplyConditionalsToChangeMask 中按 bIsInitialState 单独处理。
	constexpr bool bInitialState = false;
	const FConditionalsMask LifetimeConditionals = GetLifetimeConditionals(ReplicationConnectionId, RootObjectIndex, bInitialState);
	GetChildSubObjectsToReplicate(ReplicationConnectionId, LifetimeConditionals, RootObjectIndex, OutSubObjectsToReplicate);
}

bool FReplicationConditionals::ApplyConditionalsToChangeMask(uint32 ReplicatingConnectionId, bool bIsInitialState, FInternalNetRefIndex ParentObjectIndex, FInternalNetRefIndex ObjectIndex, uint32* ChangeMaskData, const uint32* ConditionalChangeMaskData, const FReplicationProtocol* Protocol)
{
	//IRIS_PROFILER_SCOPE_VERBOSE(FReplicationConditionals_ApplyConditionalsToChangeMask);

	bool bMaskWasModified = false;

	// Assume we need all information regarding connection filtering and replication conditionals.
	FNetBitArrayView ChangeMask = MakeNetBitArrayView(ChangeMaskData, Protocol->ChangeMaskBitCount);

	// Legacy lifetime conditionals support.
	// ----------- Pass 1：legacy lifetime conditional 路径（按成员 / 按 ELifetimeCondition）-----------
	if (EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasLifetimeConditionals))
	{
		// 当前帧 (Conn, Root) 的 LifetimeConditionals。
		const FConditionalsMask LifetimeConditionals = GetLifetimeConditionals(ReplicatingConnectionId, ParentObjectIndex, bIsInitialState);

		// 取上一次缓存值用于差分（首次为 0 → 视同与本次一致，避免无谓置脏）。
		FConditionalsMask PrevLifeTimeConditions = ConnectionInfos[ReplicatingConnectionId].ObjectConditionals[ObjectIndex];
		if (PrevLifeTimeConditions.IsUninitialized())
		{
			PrevLifeTimeConditions = LifetimeConditionals;
		}
		ConnectionInfos[ReplicatingConnectionId].ObjectConditionals[ObjectIndex] = LifetimeConditionals;

		// Optimized path for single lifetime conditional state
		if (Protocol->LifetimeConditionalsStateCount == 1U)
		{
			const FReplicationStateDescriptor* StateDescriptor = Protocol->ReplicationStateDescriptors[Protocol->FirstLifetimeConditionalsStateIndex];
			const uint32 ChangeMaskBitOffset = Protocol->FirstLifetimeConditionalsChangeMaskOffset;

			const FReplicationStateMemberChangeMaskDescriptor* ChangeMaskDescriptors = StateDescriptor->MemberChangeMaskDescriptors;
			const FReplicationStateMemberLifetimeConditionDescriptor* LifetimeConditionDescriptors = StateDescriptor->MemberLifetimeConditionDescriptors;
			for (uint32 MemberIt = 0U, MemberEndIt = StateDescriptor->MemberCount; MemberIt != MemberEndIt; ++MemberIt)
			{
				const FReplicationStateMemberLifetimeConditionDescriptor& LifetimeConditionDescriptor = LifetimeConditionDescriptors[MemberIt];
				ELifetimeCondition Condition = static_cast<ELifetimeCondition>(LifetimeConditionDescriptor.Condition);
				// COND_Dynamic 的成员需要通过本类的 DynamicConditions 表查到运行期实际条件。
				if (Condition == COND_Dynamic)
				{
					const FProperty* Property = StateDescriptor->MemberProperties[MemberIt];
					if (ensure(Property != nullptr))
					{
						Condition = GetDynamicCondition(ObjectIndex, Property->RepIndex);
					}
				}
				
				// If condition was enabled we need to dirty changemask of relevant members. If it was disabled we clear the changemask of relevant members.
				if (LifetimeConditionals.IsConditionEnabled(Condition))
				{
					// 条件由 disable→enable：本帧需"补发"该成员，即使 ChangeMask 上未置位也强制置位。
					if (!PrevLifeTimeConditions.IsConditionEnabled(Condition))
					{
						UE_LOG(LogIrisConditionals, Verbose, TEXT("Dirtying member %s %s:%s due to condition %s"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), ToCStr(StateDescriptor->DebugName), ToCStr(StateDescriptor->MemberDebugDescriptors[MemberIt].DebugName), *UEnum::GetValueAsString(Condition));

						const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = ChangeMaskDescriptors[MemberIt];
						for (uint32 BitIt = ChangeMaskBitOffset + ChangeMaskDescriptor.BitOffset, BitEndIt = BitIt + ChangeMaskDescriptor.BitCount; BitIt != BitEndIt; ++BitIt)
						{
							bMaskWasModified |= !ChangeMask.GetBit(BitIt);
							ChangeMask.SetBit(BitIt);
						}
					}
				}
				else
				{
					// 条件未启用：把对应 ChangeMask 段清零（不发该成员）。
					const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = ChangeMaskDescriptors[MemberIt];
					if (ChangeMask.IsAnyBitSet(ChangeMaskBitOffset + ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount))
					{
						UE_LOG(LogIrisConditionals, VeryVerbose, TEXT("Filtering out member %s %s:%s due to condition %s"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), ToCStr(StateDescriptor->DebugName), ToCStr(StateDescriptor->MemberDebugDescriptors[MemberIt].DebugName), *UEnum::GetValueAsString(Condition));
						ChangeMask.ClearBits(ChangeMaskBitOffset + ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount);
						bMaskWasModified = true;
					}
				}
			}
		}
		else
		{
			// 通用路径：跨 state 累计 ChangeMaskBitOffset，遇到带 lifetime 条件的 state 才处理。
			uint32 CurrentChangeMaskBitOffset = 0U;
			uint32 LifetimeConditionalsStateIt = 0U;
			const uint32 LifetimeConditionalsStateEndIt = Protocol->LifetimeConditionalsStateCount;
			for (const FReplicationStateDescriptor* StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
			{
				if (EnumHasAnyFlags(StateDescriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
				{
					const FReplicationStateMemberChangeMaskDescriptor* ChangeMaskDescriptors = StateDescriptor->MemberChangeMaskDescriptors;
					const FReplicationStateMemberLifetimeConditionDescriptor* LifetimeConditionDescriptors = StateDescriptor->MemberLifetimeConditionDescriptors;
					for (uint32 MemberIt = 0U, MemberEndIt = StateDescriptor->MemberCount; MemberIt != MemberEndIt; ++MemberIt)
					{
						const FReplicationStateMemberLifetimeConditionDescriptor& LifetimeConditionDescriptor = LifetimeConditionDescriptors[MemberIt];
						ELifetimeCondition Condition = static_cast<ELifetimeCondition>(LifetimeConditionDescriptor.Condition);
						if (Condition == COND_Dynamic)
						{
							const FProperty* Property = StateDescriptor->MemberProperties[MemberIt];
							if (ensure(Property != nullptr))
							{
								Condition = GetDynamicCondition(ObjectIndex, Property->RepIndex);
							}
						}

						// If the condition is fulfilled the changemask will remain intact so we can continue to the next member.
						if (LifetimeConditionals.IsConditionEnabled(Condition))
						{
							if (!PrevLifeTimeConditions.IsConditionEnabled(Condition))
							{
								UE_LOG(LogIrisConditionals, Verbose, TEXT("Dirtying member %s %s:%s due to condition %s"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), ToCStr(StateDescriptor->DebugName), ToCStr(StateDescriptor->MemberDebugDescriptors[MemberIt].DebugName), *UEnum::GetValueAsString(Condition));
								const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = ChangeMaskDescriptors[MemberIt];
								for (uint32 BitIt = CurrentChangeMaskBitOffset + ChangeMaskDescriptor.BitOffset, BitEndIt = BitIt + ChangeMaskDescriptor.BitCount; BitIt != BitEndIt; ++BitIt)
								{
									bMaskWasModified |= !ChangeMask.GetBit(BitIt);
									ChangeMask.SetBit(BitIt);
								}
							}
						}
						else
						{
							const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = ChangeMaskDescriptors[MemberIt];
							if (ChangeMask.IsAnyBitSet(CurrentChangeMaskBitOffset + ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount))
							{
								UE_LOG(LogIrisConditionals, VeryVerbose, TEXT("Filtering out member %s %s:%s due to condition %s"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), ToCStr(StateDescriptor->DebugName), ToCStr(StateDescriptor->MemberDebugDescriptors[MemberIt].DebugName), *UEnum::GetValueAsString(Condition));

								ChangeMask.ClearBits(CurrentChangeMaskBitOffset + ChangeMaskDescriptor.BitOffset, ChangeMaskDescriptor.BitCount);
								bMaskWasModified = true;
							}
						}
					}

					// Done processing all states with lifetime conditionals?
					++LifetimeConditionalsStateIt;
					if (LifetimeConditionalsStateIt == LifetimeConditionalsStateEndIt)
					{
						break;
					}
				}

				CurrentChangeMaskBitOffset += StateDescriptor->ChangeMaskBitCount;
			}
		}
	}

	// Apply custom conditionals by word operations.
	// ----------- Pass 2：custom condition mask（按字与运算，最快路径）-----------
	// 公式：ChangeMaskData[w] = OldMask[w] & ConditionalChangeMask[w]，被清掉的位记入 ChangedBits。
	if (ConditionalChangeMaskData != nullptr)
	{
		const uint32 WordCount = ChangeMask.GetNumWords();
		uint32 ChangedBits = 0;
		for (uint32 WordIt = 0; WordIt != WordCount; ++WordIt)
		{
			const uint32 OldMask = ChangeMaskData[WordIt];
			const uint32 ConditionalMask = ConditionalChangeMaskData[WordIt];
			const uint32 NewMask = OldMask & ConditionalMask;
			ChangeMaskData[WordIt] = NewMask;

			ChangedBits |= OldMask & ~ConditionalMask;
		}

		bMaskWasModified = bMaskWasModified | (ChangedBits != 0U);
	}

	return bMaskWasModified;
}

void FReplicationConditionals::UpdateAndResetObjectsWithDirtyConditionals()
{
	IRIS_PROFILER_SCOPE(FReplicationConditionals_UpdateAndResetObjectsWithDirtyConditionals);

	const FNetBitArray& ValidConnections = ReplicationConnections->GetValidConnections();

	// We do not expect many objects with dirty global lifetime conditionals each frame
	// 中文：批量大小 128 是经验值——每帧脏对象通常很少（被 Set 进位图的对象一般是十几个量级）。
	const uint32 MaxBatchObjectCount = 128U;
	FInternalNetRefIndex ObjectIndices[MaxBatchObjectCount];

	// 滚动收集脏对象 → 把同一批 ID 推给所有 ValidConnection 的 Writer。
	const uint32 BitCount = ~0U;
	for (uint32 ObjectCount, StartIndex = 0; (ObjectCount = ObjectsWithDirtyLifetimeConditionals.GetSetBitIndices(StartIndex, BitCount, ObjectIndices, MaxBatchObjectCount)) > 0; )
	{
		for (uint32 ConnectionId : ValidConnections)
		{
			UE::Net::Private::FReplicationConnection* Connection = ReplicationConnections->GetConnection(ConnectionId);
			Connection->ReplicationWriter->UpdateDirtyGlobalLifetimeConditionals(MakeArrayView(ObjectIndices, ObjectCount));
		}

		// 一批不足 MaxBatchObjectCount 时说明已扫到尾部，提前退出。
		StartIndex = ObjectIndices[ObjectCount - 1] + 1U;
		if ((StartIndex == ObjectsWithDirtyLifetimeConditionals.GetNumBits()) | (ObjectCount < MaxBatchObjectCount))
		{
			break;
		}
	}	

	// 处理完一律清零，下一帧重新收集。
	ObjectsWithDirtyLifetimeConditionals.ClearAllBits();
}

FReplicationConditionals::FConditionalsMask FReplicationConditionals::GetLifetimeConditionals(uint32 ReplicatingConnectionId, FInternalNetRefIndex ParentObjectIndex, bool bIsInitialState) const
{
	// 见头文件中的真值表，这里就是把 ConditionalsMask 各位填好返回。
	FConditionalsMask ConditionalsMask{0};

	const uint32 ObjectOwnerConnectionId = ReplicationFiltering->GetOwningConnection(ParentObjectIndex);
	const bool bIsReplicatingToOwner = (ReplicatingConnectionId == ObjectOwnerConnectionId);

	const FReplicationConditionals::FPerObjectInfo* ObjectInfo = GetPerObjectInfo(ParentObjectIndex);
	const bool bRoleSimulated = ReplicatingConnectionId != ObjectInfo->AutonomousConnectionId;
	const bool bRoleAutonomous = ReplicatingConnectionId == ObjectInfo->AutonomousConnectionId;
	const bool bRepPhysics = ObjectInfo->bRepPhysics;

	// 恒为真的几位：
	ConditionalsMask.SetConditionEnabled(COND_None, true);
	ConditionalsMask.SetConditionEnabled(COND_Custom, true);     // 自定义条件由 ConditionalChangeMask 单独处理，这里始终 true
	ConditionalsMask.SetConditionEnabled(COND_Dynamic, true);    // COND_Dynamic 在调用方查表替换为具体条件

	// Owner / Skip / Initial / ReplayOrOwner 互斥逻辑：
	ConditionalsMask.SetConditionEnabled(COND_OwnerOnly, bIsReplicatingToOwner);
	ConditionalsMask.SetConditionEnabled(COND_SkipOwner, !bIsReplicatingToOwner);

	// Autonomous / Simulated 二分：
	ConditionalsMask.SetConditionEnabled(COND_SimulatedOnly, bRoleSimulated);
	ConditionalsMask.SetConditionEnabled(COND_AutonomousOnly, bRoleAutonomous);
	ConditionalsMask.SetConditionEnabled(COND_SimulatedOrPhysics, bRoleSimulated | bRepPhysics);

	// Initial 状态（仅首次发该对象时）：
	ConditionalsMask.SetConditionEnabled(COND_InitialOnly, bIsInitialState);
	ConditionalsMask.SetConditionEnabled(COND_InitialOrOwner, bIsReplicatingToOwner | bIsInitialState);

	// Replay 相关：Iris 当前不再单独区分 replay 录制端，统一按 OwnerOnly 处理；NoReplay 视同普通 Simulated。
	ConditionalsMask.SetConditionEnabled(COND_ReplayOrOwner, bIsReplicatingToOwner);
	ConditionalsMask.SetConditionEnabled(COND_SimulatedOnlyNoReplay, bRoleSimulated);
	ConditionalsMask.SetConditionEnabled(COND_SimulatedOrPhysicsNoReplay, bRoleSimulated | bRepPhysics);
	ConditionalsMask.SetConditionEnabled(COND_SkipReplay, true);
	
	return ConditionalsMask;
}

void FReplicationConditionals::ClearPerObjectInfo(FInternalNetRefIndex ObjectIndex)
{
	// 重置 PerObjectInfo（清 AutonomousConnectionId 和 bRepPhysics）。
	FPerObjectInfo& PerObjectInfo = PerObjectInfos.GetData()[ObjectIndex];
	PerObjectInfo = {};

	// Remove any dynamic conditions information stored.
	// 同时移除该对象在动态条件 map 中的条目（占内存才创建，没什么开销）。
	DynamicConditions.Remove(ObjectIndex);
}

void FReplicationConditionals::ClearConnectionInfosForObject(const FNetBitArray& ValidConnections, FInternalNetRefIndex ObjectIndex)
{
	IRIS_PROFILER_SCOPE(FReplicationConditionals_ClearConnectionInfosForObject);

	// 清掉每个连接里对该对象的 ConditionalsMask 缓存（防止 InternalNetRefIndex 复用时残留旧位）。
	for (uint32 ConnectionId : ValidConnections)
	{
		FPerConnectionInfo& ConnectionInfo = ConnectionInfos[ConnectionId];
		ConnectionInfo.ObjectConditionals[ObjectIndex] = FConditionalsMask{};
	}
}

ELifetimeCondition FReplicationConditionals::GetDynamicCondition(FInternalNetRefIndex ObjectIndex, uint16 RepIndex) const
{
	// 找不到记录视为 COND_Dynamic（占位，调用方会再查一次属性的声明态）。
	const FObjectDynamicConditions* ObjectConditions = DynamicConditions.Find(ObjectIndex);
	if (ObjectConditions == nullptr)
	{
		return COND_Dynamic;
	}

	const int16* Condition = ObjectConditions->DynamicConditions.Find(RepIndex);
	if (Condition == nullptr)
	{
		return COND_Dynamic;
	}

	return static_cast<const ELifetimeCondition>(*Condition);
}

void FReplicationConditionals::SetDynamicCondition(FInternalNetRefIndex ObjectIndex, uint16 RepIndex, ELifetimeCondition Condition)
{
	// 存到稀疏 map：只有真有 dynamic 条件的对象才占内存。
	FObjectDynamicConditions& ObjectConditions = DynamicConditions.FindOrAdd(ObjectIndex);
	ObjectConditions.DynamicConditions.Emplace(RepIndex, static_cast<int16>(Condition));
}

bool FReplicationConditionals::DynamicConditionChangeRequiresBaselineInvalidation(ELifetimeCondition OldCondition, ELifetimeCondition NewCondition) const
{
	// If the old condition didn't cause the member to always be replicated it could have been not replicated to one or more connections.
	// 中文：旧条件不是"恒发"（COND_None / COND_Dynamic 视为"不会丢"）时，过去可能某些连接没收到该字段。
	const bool OldConditionMayHaveBeenDisabled = !(OldCondition == COND_None || OldCondition == COND_Dynamic);

	// If the new condition is something other than never replicating then it may be replicated.
	// 中文：新条件不是 COND_Never，则在某些连接上将会发送。
	const bool NewConditionMayBeEnabled = (NewCondition != COND_Never);

	// 二者同时成立 → 可能从"未发"切换到"将发"，必须让客户端从新基线开始重新对齐。
	return OldConditionMayHaveBeenDisabled && NewConditionMayBeEnabled;
}

void FReplicationConditionals::MarkRemoteRoleDirty(FInternalNetRefIndex ObjectIndex)
{
	// 角色切换需要让 RemoteRole 字段在下次发送中带出。
	// 流程：通过 Protocol 找到 RemoteRole 的 RepIndex（首次扫描后缓存），再调用 MarkPropertyDirty。
	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;

	if (NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex) == nullptr)
	{
		return;
	}

	if (!ReplicatedObjectData.NetHandle.IsValid())
	{
		return;
	}

	const uint16 RepIndex = GetRemoteRoleRepIndex(Protocol);
	if (RepIndex == InvalidRepIndex)
	{
		return;
	}

	MarkPropertyDirty(ObjectIndex, RepIndex);
}

uint16 FReplicationConditionals::GetRemoteRoleRepIndex(const FReplicationProtocol* Protocol)
{
	// 缓存：相同 Protocol 反复来时只扫描一次。
	if (CachedRemoteRoleRepIndex != InvalidRepIndex)
	{
		return CachedRemoteRoleRepIndex;
	}
	
	// 通过 Serializer 指针匹配 FNetRoleNetSerializer，再过滤名字为 NAME_RemoteRole 的属性。
	const FNetSerializer* NetRoleNetSerializer = &UE_NET_GET_SERIALIZER(FNetRoleNetSerializer);

	// Loop through all state descriptors end their properties to find the RemoteRole
	for (const FReplicationStateDescriptor* StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, static_cast<int32>(Protocol->ReplicationStateCount)))
	{
		for (const FReplicationStateMemberSerializerDescriptor& SerializerDescriptor : MakeArrayView(StateDescriptor->MemberSerializerDescriptors, StateDescriptor->MemberCount))
		{
			if (SerializerDescriptor.Serializer != NetRoleNetSerializer)
			{
				continue;
			}

			const SIZE_T MemberIndex = &SerializerDescriptor - StateDescriptor->MemberSerializerDescriptors;
			const FProperty* Property = StateDescriptor->MemberProperties[MemberIndex];
			if (Property && Property->GetFName() == NAME_RemoteRole)
			{
				CachedRemoteRoleRepIndex = Property->RepIndex;
				return Property->RepIndex;
			}
		}
	}

	return InvalidRepIndex;
}

void FReplicationConditionals::MarkPropertyDirty(FInternalNetRefIndex ObjectIndex, uint16 RepIndex)
{
	// 目标：在外部 state buffer 上为该 RepIndex 对应的成员置脏（与 push-model 路径殊途同归）。
	// 步骤：
	//   1) 校验对象 / handle / state buffer 有效；
	//   2) 遍历所有 state，按 Fragment Owner 的 NetHandle 匹配（多 Fragment 时只匹配真正的 owner）；
	//   3) 通过 RepIndex→MemberIndex 映射定位到具体成员；
	//   4) MarkDirty(Header, MemberChangeMask, ChangeMaskDescriptor)。
	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);

	const FNetHandle OwnerHandle = ReplicatedObjectData.NetHandle;
	if (!OwnerHandle.IsValid())
	{
		return;
	}

	if (NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex) == nullptr)
	{
		return;
	}

	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;
	const FReplicationInstanceProtocol* InstanceProtocol = ReplicatedObjectData.InstanceProtocol;

	constexpr uint32 MaxFragmentOwnerCount = 1U;
	UObject* FragmentOwners[MaxFragmentOwnerCount] = {};
	FReplicationStateOwnerCollector FragmentOwnerCollector(FragmentOwners, MaxFragmentOwnerCount);

	for (const FReplicationStateDescriptor*& StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, static_cast<int32>(Protocol->ReplicationStateCount)))
	{
		const SIZE_T StateIndex = &StateDescriptor - Protocol->ReplicationStateDescriptors;

		// Is the passed Owner the owner of the fragment?
		{
			const FReplicationFragment* ReplicationFragment = InstanceProtocol->Fragments[StateIndex];

			FragmentOwnerCollector.Reset();
			ReplicationFragment->CollectOwner(&FragmentOwnerCollector);
			if (FragmentOwnerCollector.GetOwnerCount() == 0U || FNetHandleManager::GetNetHandle(FragmentOwnerCollector.GetOwners()[0]) != OwnerHandle)
			{
				// Not the right owner.
				continue;
			}
		}

		// Can this state contain this property?
		if (RepIndex >= StateDescriptor->RepIndexCount)
		{
			continue;
		}

		// Does this state contain this property?
		const FReplicationStateMemberRepIndexToMemberIndexDescriptor& RepIndexToMemberIndexDescriptor = StateDescriptor->MemberRepIndexToMemberIndexDescriptors[RepIndex];
		if (RepIndexToMemberIndexDescriptor.MemberIndex == FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry)
		{
			continue;
		}

		// We found the relevant state. Modify the external state changemask.
		const FReplicationInstanceProtocol::FFragmentData& Fragment = InstanceProtocol->FragmentData[StateIndex];
		const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskDescriptor = StateDescriptor->MemberChangeMaskDescriptors[RepIndexToMemberIndexDescriptor.MemberIndex];
		FNetBitArrayView MemberChangeMask = GetMemberChangeMask(Fragment.ExternalSrcBuffer, StateDescriptor);
		FReplicationStateHeader& Header = GetReplicationStateHeader(Fragment.ExternalSrcBuffer, StateDescriptor);
		MarkDirty(Header, MemberChangeMask, ChangeMaskDescriptor);

		return;
	}

	UE_LOG(LogIris, Warning, TEXT("Trying to mark non-existing property with RepIndex %u in protocol %s as dirty"), RepIndex, ToCStr(Protocol->DebugName));
}

void FReplicationConditionals::InvalidateBaselinesForObjectHierarchy(uint32 ObjectIndex, const TConstArrayView<uint32>& ConnectionsToInvalidate)
{
	// 一次性把 root + 所有"我拥有"的 SubObject（带 lifetime 条件）的基线作废。
	// 注意：仅对协议中带 HasLifetimeConditionals trait 的对象作废 —— 没有 lifetime 条件的对象其
	// 基线不会因为条件变化而失效。

	// Invalidate baselines for root object
	{
		const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
		if (EnumHasAnyFlags(ObjectData.Protocol->ProtocolTraits, EReplicationProtocolTraits::HasLifetimeConditionals))
		{
			for (const uint32 ConnId : ConnectionsToInvalidate)
			{
				BaselineInvalidationTracker->InvalidateBaselines(ObjectIndex, ConnId);
			}
		}
	}

	// Invalidate baselines for subobjects
	for (const FInternalNetRefIndex SubObjectIndex : NetRefHandleManager->GetSubObjects(ObjectIndex))
	{
		const FNetRefHandleManager::FReplicatedObjectData& SubObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(SubObjectIndex);
		if (SubObjectData.IsOwnedSubObject())
		{
			if (EnumHasAnyFlags(SubObjectData.Protocol->ProtocolTraits, EReplicationProtocolTraits::HasLifetimeConditionals))
			{
				for (const uint32 ConnId : ConnectionsToInvalidate)
				{
					BaselineInvalidationTracker->InvalidateBaselines(SubObjectIndex, ConnId);
				}
			}
		}
	}
}

}
