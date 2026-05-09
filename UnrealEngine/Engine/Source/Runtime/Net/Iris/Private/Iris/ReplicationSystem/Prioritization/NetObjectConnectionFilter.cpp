// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：UNetObjectConnectionFilter 实现。
// 注意：本文件物理位置在 Prioritization/ 目录，但实现的是 **Filtering 子模块**的 UNetObjectConnectionFilter
// （头文件位于 Public/Iris/ReplicationSystem/Filtering/NetObjectConnectionFilter.h）。
// 这是历史遗留——可能因早期模块拆分时漏迁。其功能上与 Prioritization 子模块无关。
//
// 职责：per-connection 显式黑/白名单过滤。常用于：
//   - 多人游戏中"只发给特定玩家"的 actor（队伍内对象）；
//   - 服务端权威开关型对象（仅 owner 看得见的 inventory / debug actor 等）。
// 数据结构：
//   - LocalToNetRefIndex：local index ↔ ObjectIndex 双射。设上限 MaxObjectCount（远小于全局 NetRef 数）；
//   - UsedLocalInfoIndices：哪些 local index 在用；
//   - PerConnectionInfos[ConnId].ReplicationEnabledObjects：该连接对应的 local index 位图（1 = 允许）。
// API：SetReplicateToConnection(handle, connId, Allow/Disallow) 由游戏层调用。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Filtering/NetObjectConnectionFilter.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectConnectionFilter)

// 设置某对象对某连接的复制开关：Allow=1 / Disallow=0。
// 由游戏层（Bridge/GameMode）调用。
void UNetObjectConnectionFilter::SetReplicateToConnection(UE::Net::FNetRefHandle RefHandle, uint32 ConnectionId, UE::Net::ENetFilterStatus FilterStatus)
{
	// 边界检查：ConnectionId 越界则直接返回（带 ensure 提示），避免越界写入。
	if (ConnectionId >= static_cast<uint32>(PerConnectionInfos.Num()))
	{
		ensureMsgf(false, TEXT("UNetObjectConnectionFilter::SetReplicateToConnection received invalid ConnectionId:%u | Max:%u"), ConnectionId, PerConnectionInfos.Num());
		return;
	}

	// 查表：FNetRefHandle → InternalNetRefIndex（中央登记表）。
	const UE::Net::Private::FInternalNetRefIndex ObjectIndex = GetObjectIndex(RefHandle);

	if (ObjectIndex == UE::Net::Private::FNetRefHandleManager::InvalidInternalIndex)
	{
		// 对象未注册（已 EndReplication 或尚未 StartReplication），静默忽略。
		return;
	}

	// 查 FilteringInfo 拿到本 filter 的紧凑 LocalIndex（远小于全局 NetRef 数）。
	if (const FFilteringInfo* FilteringInfo = static_cast<const FFilteringInfo*>(GetFilteringInfo(ObjectIndex)))
	{
		const uint16 LocalIndex = FilteringInfo->GetLocalObjectIndex();

		FPerConnectionInfo& PerConnectionInfo = PerConnectionInfos[ConnectionId];
		ensureMsgf(LocalIndex < PerConnectionInfo.ReplicationEnabledObjects.GetNumBits(), TEXT("UNetObjectConnectionFilter::SetReplicateToConnection Object %u mapped to invalid local index %u"), ObjectIndex, LocalIndex);
		// 直接置位：1=允许该连接复制；0=禁止。
		PerConnectionInfo.ReplicationEnabledObjects.SetBitValue(LocalIndex, FilterStatus == UE::Net::ENetFilterStatus::Allow);
	}
}

void UNetObjectConnectionFilter::OnInit(const FNetObjectFilterInitParams& Params)
{
	MaxInternalIndex = Params.CurrentMaxInternalIndex; 

	Config = TStrongObjectPtr<UNetObjectConnectionFilterConfig>(CastChecked<UNetObjectConnectionFilterConfig>(Params.Config));

	// 受 filter 管理的对象上限：min(全局上限, Config->MaxObjectCount)。
	// 远小于全局，因为本 filter 使用 uint16 LocalIndex 编码，且应用场景只针对少量"可见性敏感"对象。
	const uint16 MaxLocalObjectCount = static_cast<uint16>(FPlatformMath::Min<uint32>(Params.AbsoluteMaxNetObjectCount, Config->MaxObjectCount));
	UsedLocalInfoIndices.Init(MaxLocalObjectCount);
	LocalToNetRefIndex.SetNumZeroed(MaxLocalObjectCount);

	// +1 给 ConnectionId 0。
	PerConnectionInfos.SetNum(Params.MaxConnectionCount + 1);
}

void UNetObjectConnectionFilter::OnDeinit()
{
	Config = nullptr;

	UsedLocalInfoIndices.Empty();
	LocalToNetRefIndex.Empty();
}

void UNetObjectConnectionFilter::AddConnection(uint32 ConnectionId)
{
	// 新连接：分配 ReplicationEnabledObjects 位图。所有位默认 0（拒绝），需通过 SetReplicateToConnection 显式开启。
	FPerConnectionInfo& ConnInfo = PerConnectionInfos[ConnectionId];
	ConnInfo.ReplicationEnabledObjects.Init(UsedLocalInfoIndices.GetNumBits());
}

void UNetObjectConnectionFilter::RemoveConnection(uint32 ConnectionId)
{
	// Free everything that was allocated for the connection.
	FPerConnectionInfo& ConnInfo = PerConnectionInfos[ConnectionId];
	ConnInfo = FPerConnectionInfo();
}

bool UNetObjectConnectionFilter::AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams& Params)
{
	// 分配紧凑 LocalIndex（FindFirstZero 从 UsedLocalInfoIndices 找 0 位）。
	const uint32 LocalIndex = UsedLocalInfoIndices.FindFirstZero();
	if (LocalIndex == UE::Net::FNetBitArrayBase::InvalidIndex)
	{
		// 超过 MaxObjectCount 上限——本对象不会被本 filter 处理（保持默认 Allow 流程）。
		ensureMsgf(false, TEXT("%hs. MaxObjectCount: %u. Config type %s."), "Too many objects added to NetObjectConnectionFilter. Object will not be handled by filter!", (Config.IsValid() ? Config->MaxObjectCount : uint16(0)), ToCStr(GetNameSafe(Config.Get())));
		return false;
	}

	UE_LOG(LogIris, Verbose, TEXT("UNetObjectConnectionFilter::AddObject added %u | Mapped to LocalIndex %u"), ObjectIndex, LocalIndex);

	UsedLocalInfoIndices.SetBit(LocalIndex);
	LocalToNetRefIndex[LocalIndex] = ObjectIndex;

	FFilteringInfo& Info = static_cast<FFilteringInfo&>(Params.OutInfo);
	Info.SetLocalObjectIndex((uint16)(LocalIndex)); //LPS: Assert if overflow

	return true;
}

void UNetObjectConnectionFilter::RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo& InInfo)
{
	// 标记"有对象被移除"——下一帧 PreFilter 会同步所有连接位图，把已删对象的位清掉。
	bObjectRemoved = true;

	const FFilteringInfo& Info = static_cast<const FFilteringInfo&>(InInfo);

	const uint16 LocalIndex = Info.GetLocalObjectIndex();
	UsedLocalInfoIndices.ClearBit(LocalIndex);

	UE_LOG(LogIris, Verbose, TEXT("UNetObjectConnectionFilter::RemoveObject removed %u | Mapped to LocalIndex %u"), ObjectIndex, LocalIndex);

	// For good measure. It's not strictly needed.
	// 哨兵：方便调试；正常流程 LocalIndex 不会再被读到（因为 UsedLocalInfoIndices 该位已被清）。
	LocalToNetRefIndex[LocalIndex] = UE::Net::Private::FNetRefHandleManager::InvalidInternalIndex;
}

void UNetObjectConnectionFilter::PreFilter(FNetObjectPreFilteringParams& Params)
{
	// 仅当本帧有对象被移除时执行——避免每帧都做位图 AND。
	if (!bObjectRemoved)
	{
		return;
	}

	bObjectRemoved = false;

	// Mask out no longer filtered objects to minimize looping during filtering.
	// 把每连接的 ReplicationEnabledObjects 与 UsedLocalInfoIndices 做 AND：
	// 已删除对象的 LocalIndex 在 UsedLocalInfoIndices 里是 0，AND 后会被清掉，避免后续 Filter 时被误判为允许。
	Params.ValidConnections.ForAllSetBits(
		[this](uint32 ConnectionId)
		{
			FPerConnectionInfo& Info = this->PerConnectionInfos[ConnectionId];
			Info.ReplicationEnabledObjects.Combine(this->UsedLocalInfoIndices, UE::Net::FNetBitArrayBase::AndOp);
		}
	);
}

void UNetObjectConnectionFilter::Filter(FNetObjectFilteringParams& Params)
{
	IRIS_PROFILER_SCOPE(UNetObjectConnectionFilter_Filter);

	// 输出：本连接允许的 ObjectIndex 位图。
	UE::Net::FNetBitArrayView& AllowedObjects = Params.OutAllowedObjects;
	AllowedObjects.ClearAllBits();

	// 把 per-conn 的 ReplicationEnabledObjects（按 LocalIndex 索引）映射到 AllowedObjects（按 ObjectIndex 索引）。
	FPerConnectionInfo& ConnectionInfo = PerConnectionInfos[Params.ConnectionId];
	ConnectionInfo.ReplicationEnabledObjects.ForAllSetBits(
		[&AllowedObjects, &ToNetRefIndex = LocalToNetRefIndex](uint32 LocalObjectIndex)
		{
			const uint32 ObjectIndex = ToNetRefIndex[LocalObjectIndex];
			AllowedObjects.SetBit(ObjectIndex);
		}
	);
}
