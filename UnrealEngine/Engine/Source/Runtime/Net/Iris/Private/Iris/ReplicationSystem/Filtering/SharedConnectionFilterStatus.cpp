// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// SharedConnectionFilterStatus.cpp —— FSharedConnectionFilterStatus / FSharedConnectionFilterStatusCollection 实现。
// 关键行为见同名 .h 头部说明。
// =====================================================================================================================

#include "Iris/ReplicationSystem/Filtering/SharedConnectionFilterStatus.h"
#include "Iris/Core/IrisLog.h"

namespace UE::Net
{

// FSharedConnectionFilterStatus implementation
// 设置某 ConnectionHandle（父或子）的状态：Allow → 加入 Set；Disallow → 从 Set 移除。
// 同一实例只服务一个父连接：第一次 SetFilterStatus 时锁定 ParentConnectionId，之后必须一致。
bool FSharedConnectionFilterStatus::SetFilterStatus(FConnectionHandle ConnectionHandle, ENetFilterStatus FilterStatus)
{
	if (!ConnectionHandle.IsValid())
	{
		UE_LOG(LogIrisFiltering, Error, TEXT("%s"), TEXT("Trying to set filter status for an invalid FConnectionHandle on an FConnectionHandleFilterGroup"));
		return false;
	}

	if (ParentConnectionId != InvalidConnectionId && ConnectionHandle.GetParentConnectionId() != ParentConnectionId)
	{
		UE_LOG(LogIrisFiltering, Error, TEXT("FConnectionHandleFilterGroup ignoring SetFilterStatus call due to connection ID mismatch. Expected %u got %u."), ParentConnectionId, ConnectionHandle.GetParentConnectionId());
		return false;
	}

	ParentConnectionId = ConnectionHandle.GetParentConnectionId();
	if (FilterStatus == ENetFilterStatus::Allow)
	{
		// 父句柄的 ChildConnectionId 通常是 0（"父本身"），因此父也作为一个"子"放入集合。
		AllowConnections.Add(ConnectionHandle.GetChildConnectionId());
	}
	else
	{
		AllowConnections.Remove(ConnectionHandle.GetChildConnectionId());
	}

	return true;
}

// 移除：父连接 → 整组复位（清空 Set + 解除 Parent 锁定，可被其它父复用）；子连接 → 仅移除该 ChildConnectionId。
void FSharedConnectionFilterStatus::RemoveConnection(FConnectionHandle ConnectionHandle)
{
	if (ConnectionHandle.GetParentConnectionId() == ParentConnectionId)
	{
		if (ConnectionHandle.IsParentConnection())
		{
			AllowConnections.Reset();
			// Allow this instance to be repurposed for a different parent connection.
			ParentConnectionId = InvalidConnectionId;
		}
		else
		{
			AllowConnections.Remove(ConnectionHandle.GetChildConnectionId());
		}
	}
}

// FSharedConnectionFilterStatusCollection implementation
// 字典版 SetFilterStatus：
//   Allow → 必创建条目并写入；
//   Disallow → 仅在已存在时写入；写入后若聚合状态变 Disallow，把整个条目删除（节省内存）。
void FSharedConnectionFilterStatusCollection::SetFilterStatus(FConnectionHandle ConnectionHandle, ENetFilterStatus FilterStatus)
{
	if (!ConnectionHandle.IsValid())
	{
		UE_LOG(LogIrisFiltering, Error, TEXT("%s"), TEXT("Trying to set filter status for an invalid FConnectionHandle on an FConnectionHandleFilterGroupCollection"));
		return;
	}

	const uint32 ParentConnId = ConnectionHandle.GetParentConnectionId();
	if (FilterStatus == ENetFilterStatus::Allow)
	{
		FSharedConnectionFilterStatus& SharedFilterStatus = FindOrAddSharedConnectionFilterStatus(ParentConnId);
		SharedFilterStatus.SetFilterStatus(ConnectionHandle, FilterStatus);
	}
	else
	{
		if (FSharedConnectionFilterStatus* SharedFilterStatus = FindSharedConnectionFilterStatus(ParentConnId))
		{
			SharedFilterStatus->SetFilterStatus(ConnectionHandle, FilterStatus);
			// If after setting the filter status the group disallows replication then we can remove it
			if (SharedFilterStatus->GetFilterStatus() == ENetFilterStatus::Disallow)
			{
				ParentToFilterStatus.Remove(ParentConnId);
			}
		}
	}
}

// 字典版 RemoveConnection：父 → 整组从字典移除；子 → 转交内层组的 RemoveConnection。
void FSharedConnectionFilterStatusCollection::RemoveConnection(FConnectionHandle ConnectionHandle)
{
	if (ConnectionHandle.IsParentConnection())
	{
		ParentToFilterStatus.Remove(ConnectionHandle.GetParentConnectionId());
	}
	else if (ConnectionHandle.IsChildConnection())
	{
		if (FSharedConnectionFilterStatus* SharedFilterStatus = FindSharedConnectionFilterStatus(ConnectionHandle.GetParentConnectionId()))
		{
			SharedFilterStatus->RemoveConnection(ConnectionHandle);
		}
	}
}

// 字典版 GetFilterStatus：找不到条目即默认 Disallow。
ENetFilterStatus FSharedConnectionFilterStatusCollection::GetFilterStatus(uint32 ParentConnectionId) const
{
	if (const FSharedConnectionFilterStatus* SharedFilterStatus = FindSharedConnectionFilterStatus(ParentConnectionId))
	{
		return SharedFilterStatus->GetFilterStatus();
	}

	return ENetFilterStatus::Disallow;
}

FSharedConnectionFilterStatus* FSharedConnectionFilterStatusCollection::FindSharedConnectionFilterStatus(uint32 ParentConnectionId)
{
	return ParentToFilterStatus.Find(ParentConnectionId);
}

const FSharedConnectionFilterStatus* FSharedConnectionFilterStatusCollection::FindSharedConnectionFilterStatus(uint32 ParentConnectionId) const
{
	return ParentToFilterStatus.Find(ParentConnectionId);
}

// 创建新条目：用一次 Disallow 调用先把 ParentConnectionId 锁定（FSharedConnectionFilterStatus 第一次 Set 时锁定）。
// 这一步严格意义上不必，但便于排查"未初始化的组"问题。
FSharedConnectionFilterStatus& FSharedConnectionFilterStatusCollection::FindOrAddSharedConnectionFilterStatus(uint32 ParentConnectionId)
{
	FSharedConnectionFilterStatus& SharedFilterStatus = ParentToFilterStatus.FindOrAdd(ParentConnectionId);
	// Set disallow status to establish the ParentConnectionId of the group. It's not strictly necessary but it could help detect issues with the collection implementation.
	SharedFilterStatus.SetFilterStatus(FConnectionHandle(ParentConnectionId), ENetFilterStatus::Disallow);
	return SharedFilterStatus;
}

}
