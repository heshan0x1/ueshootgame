// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// SharedConnectionFilterStatus.h —— "父子/拆屏共享连接"的过滤状态聚合工具。
// ---------------------------------------------------------------------------------------------------------------------
// 应用场景：
//   UE 网络中一个父连接（PlayerController 主连接）可挂多个子连接（拆屏 SplitScreen）。SubObject Filter 这种
//   "对组成对象的可见性按连接控制"的功能，需要兼容父子连接：只要任一子（或父本身）允许，则父连接整体允许。
//   FSharedConnectionFilterStatus 管理"单个父 + 一组子"的语义；FSharedConnectionFilterStatusCollection 管理多
//   父连接的字典。
//
// 语义约定：
//   - 默认状态 = Disallow（聚合规则：至少一个连接 Allow → 整体 Allow）；
//   - SetFilterStatus 必须传入"同一 ParentConnectionId"，否则忽略并记录错误日志；
//   - RemoveConnection 当传入的是父连接（IsParentConnection）时，相当于一次性清空所有子连接并允许实例被复用。
// =====================================================================================================================

#pragma once

#include "HAL/Platform.h"
#include "Iris/IrisConstants.h"
#include "Iris/ReplicationSystem/ReplicationView.h" // for UE_IRIS_INLINE_VIEWS_PER_CONNECTION
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Net/Core/Connection/ConnectionHandle.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

namespace UE::Net
{

/**
 * Keeps track of the filter status for a connection and its child connections. All FConnectionHandles must have the same ParentConnection. As soon as the filter status is set for a FConnectionHandle all subsequent operations must have the same FConnectionHandle.
 * The default state is that the filter status is Disallowed. If there's at least one connection that has set the filter status to Allowed then will the filter status for the group be Allowed.
 */
/**
 * 单个父连接 + 多个子连接的过滤状态聚合器。聚合规则：默认 Disallow；任一子/父 Allow 即整体 Allow。
 * 内部仅记录"Allow 的子连接 ID 集合"——空集合即 Disallow，节省存储。
 */
class FSharedConnectionFilterStatus
{
public:
	/** Returns true if the filter status could be successfully set for the ConnectionHandle. A return value of false indicates either the connection handle is invalid or its ParentConnectionId doesn't match prior calls to SetFilterStatus. */
	/**
	 * 设置 ConnectionHandle（父或子）的过滤状态。
	 * 返回 false 的情形：句柄无效，或与之前 SetFilterStatus 锁定的 ParentConnectionId 不一致。
	 */
	IRISCORE_API bool SetFilterStatus(FConnectionHandle ConnectionHandle, ENetFilterStatus FilterStatus);
	
	/** Returns Disallowed if no connections have set the filter status to Allowed and returns Allowed otherwise. */
	/** 取聚合状态：Allow 集合空 → Disallow；非空 → Allow。 */
	ENetFilterStatus GetFilterStatus() const;
	
	/** Removes the filter status for ConnectionHandle. If it's a parent connection it will act as if all child connections were removed too and allow the instance to be used with a different ParentConnectionId. */
	/**
	 * 移除某 ConnectionHandle。
	 * 父连接：清空整个 Allow 集合并复位 ParentConnectionId（实例可被另一个父连接复用）；
	 * 子连接：仅从 Allow 集合移除其 ChildConnectionId。
	 */
	IRISCORE_API void RemoveConnection(FConnectionHandle ConnectionHandle);

	/** Returns the parent connection ID the group operates on or InvalidConnectionId if no valid connection handles ever set filter status. */
	/** 当前锁定的父连接 ID；从未调用过 SetFilterStatus 则返回 InvalidConnectionId。 */
	uint32 GetParentConnectionId() const;

private:
	// Only keep track of connections which allow replication as the replication status is Disallow by default.
	/** 仅记录 Allow 的子连接 ID 集合（默认 Disallow，留空集合更省）。InlineSetAllocator 大小贴合典型分屏数量。 */
	TSet<uint32, DefaultKeyFuncs<uint32>, TInlineSetAllocator<UE_IRIS_INLINE_VIEWS_PER_CONNECTION>> AllowConnections;
	/** 锁定的父连接 ID。第一次 SetFilterStatus 时确定，RemoveConnection（IsParent）时复位。 */
	uint32 ParentConnectionId = InvalidConnectionId;
};

/** Keeps track of the filter status for multiple connections and their child connections. A FConnectionHandleFilterGroup is stored per unique ParentConnectionId calling SetFilterStatus. */
/**
 * 多父连接的过滤状态字典。每个 ParentConnectionId 对应一份 FSharedConnectionFilterStatus。
 * 在 FReplicationFiltering::FPerSubObjectFilterGroupInfo 中用于"SubObject 过滤组"的连接状态管理。
 */
class FSharedConnectionFilterStatusCollection
{
public:
	IRISCORE_API void SetFilterStatus(FConnectionHandle ConnectionHandle, ENetFilterStatus FilterStatus);

	/** Returns Disallowed if no connections with the supplied ParentConnectionId have set the filter status to Allowed. If at least one connection with the ParentConnectionId allows replication then Allowed is returned. */
	/** 查询 ParentConnectionId 对应聚合状态；找不到则返回 Disallow（与默认语义一致）。 */
	IRISCORE_API ENetFilterStatus GetFilterStatus(uint32 ParentConnectionId) const;

	/** Remove a connection from filter status records. If it's a parent connection the corresponding FConnectionHandleFilterGroup will be removed altogether. */
	/** 移除连接记录：父连接 → 整组从字典中删除；子连接 → 仅在对应组中移除该子。 */
	IRISCORE_API void RemoveConnection(FConnectionHandle ConnectionHandle);

private:
	/** 查找：父连接 → 组（const 与非 const 重载）。 */
	FSharedConnectionFilterStatus* FindSharedConnectionFilterStatus(uint32 ParentConnectionId);
	const FSharedConnectionFilterStatus* FindSharedConnectionFilterStatus(uint32 ParentConnectionId) const;
	/** 查找或创建：内部会用 SetFilterStatus(ParentHandle, Disallow) 为新组锁定 ParentConnectionId。 */
	FSharedConnectionFilterStatus& FindOrAddSharedConnectionFilterStatus(uint32 ParentConnectionId);

	/** ParentConnectionId → 该父连接的状态聚合器。 */
	TMap<uint32, FSharedConnectionFilterStatus> ParentToFilterStatus;
};

inline ENetFilterStatus FSharedConnectionFilterStatus::GetFilterStatus() const
{
	return AllowConnections.IsEmpty() ? ENetFilterStatus::Disallow : ENetFilterStatus::Allow;
}

inline uint32 FSharedConnectionFilterStatus::GetParentConnectionId() const
{
	return ParentConnectionId;
}

}
