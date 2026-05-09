// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationConnections.h
// 模块：Iris / ReplicationSystem（辅助 & 周边）
// 功能：FReplicationConnections —— 一个 ReplicationSystem 内所有连接的聚合管理。
//
// 数据布局：
//   * Connections[ConnId]      —— FReplicationConnection 实例（per-conn 主结构）；
//   * ReplicationViews[ConnId] —— 该连接的 FReplicationView（玩家视角，
//                                  供 Filter/Prioritizer 计算可见性 / 优先级）；
//   * ValidConnections         —— FNetBitArray 位图：哪些 ConnId 已 AddConnection。
//
// 每连接（FReplicationConnection）持有：
//   * ReplicationWriter       —— 发送侧状态机（PerObjectInfo / HugeObject / Record）。
//   * ReplicationReader       —— 接收侧批解析器。
//   * DataStreamManager       —— 该连接的 UDataStream 多流复用框架（持弱引）。
//   * UserData                —— 上层 NetDriver 关联的不透明对象指针。
//   * bIsClosing              —— graceful-close 标记，仍允许 flush reliable 但不再
//                                进入"开放连接"列表。
//   * （ReplicationView 单独存于 ReplicationViews 数组）
//
// 容量与 Id：
//   * MaxConnectionCount = ValidConnections.GetNumBits()，构造时设定（默认 128）。
//   * 连接 Id 由 NetDriver 分配并保证稀疏稳定（不复用已删除 Id）。
//
// API 概览：
//   * AddConnection / RemoveConnection / IsValidConnection / IsOpenConnection
//   * GetOpenConnections —— 返回排除 bIsClosing 的位图（用于 Filter/Prioritizer 遍历）。
//   * InitDataStreamManager / DeinitDataStreamManager
//   * SetReplicationView / GetReplicationView
//   * SetConnectionIsClosing
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/ReplicationView.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class UDataStreamManager;
namespace UE::Net::Private
{
	class FReplicationWriter;
	class FReplicationReader;
}

namespace UE::Net::Private
{

/**
 * FReplicationConnection
 * 中文：单连接的运行期结构体。Reader/Writer 是裸指针 + 由 RS 构造销毁；
 * DataStreamManager 是 UObject，用 WeakPtr 防止 GC 失效；UserData 不透明。
 */
struct FReplicationConnection
{
	FReplicationWriter* ReplicationWriter = nullptr;
	FReplicationReader* ReplicationReader = nullptr;
	// 中文：弱引——Manager 由 NetDriver/DataStreamChannel 持有强引；本表只查询。
	TWeakObjectPtr<UDataStreamManager> DataStreamManager;
	// 中文：上层（NetDriver/UNetConnection）传入的不透明指针，便于回溯。
	FObjectPtr UserData = nullptr;
	bool bIsClosing = false; // Should be set when a connection starts the graceful close process to finish flushing reliable data
};

/**
 * FReplicationConnections
 * 中文：所有连接的聚合容器。底层是 fixed-size 的并行数组（按 MaxConnections）+
 * 一个有效位图，避免 TMap 的哈希开销，访问 O(1)。
 */
class FReplicationConnections
{
public:
	// 中文：构造时确定上限——超出 MaxConnections 的 AddConnection 会越界 check。
	explicit FReplicationConnections(uint32 MaxConnections = 128)
	: ValidConnections(MaxConnections)
	{
		Connections.SetNumZeroed(MaxConnections);
		ReplicationViews.SetNum(MaxConnections);
	}

	// 中文：销毁前调用——遍历有效连接逐一 RemoveConnection（Deinit DataStream + 释放 R/W）。
	void Deinit();

	const FReplicationConnection* GetConnection(uint32 ConnectionId) const
	{
		if (ValidConnections.GetBit(ConnectionId))
		{
			return &Connections[ConnectionId];
		}
		
		return nullptr;
	}

	FReplicationConnection* GetConnection(uint32 ConnectionId)
	{
		if (ValidConnections.GetBit(ConnectionId))
		{
			return &Connections[ConnectionId];
		}
		
		return nullptr;
	}

	bool IsValidConnection(uint32 ConnectionId) const
	{
		return ConnectionId < GetMaxConnectionCount() && ValidConnections.GetBit(ConnectionId);
	}

	// 中文：Open = Valid 且未在 graceful-close。Filter/Prioritizer 通常只关心 Open 连接。
	bool IsOpenConnection(uint32 ConnectionId) const
	{
		return ConnectionId < GetMaxConnectionCount() && ValidConnections.GetBit(ConnectionId) && !Connections[ConnectionId].bIsClosing;
	}

	// 中文：注册——只标位图，FReplicationConnection 字段由 RS 后续逐一填（Reader/Writer/DataStream/UserData）。
	void AddConnection(uint32 ConnectionId)
	{
		check(ValidConnections.GetBit(ConnectionId) == false);
		ValidConnections.SetBit(ConnectionId);
	}

	// 中文：销毁顺序：清 ReplicationView → DeinitDataStreamManager → 重置 FReplicationConnection → 清位图。
	IRISCORE_API void RemoveConnection(uint32 ConnectionId);

	uint32 GetMaxConnectionCount() const { return ValidConnections.GetNumBits(); }

	const FNetBitArray& GetValidConnections() const { return ValidConnections; }

	// Returns connections that are not in the closing state
	// 中文：以位图形式返回所有 Open 连接，便于 Filter/Prioritizer 一次性遍历。
	FNetBitArray GetOpenConnections() const;

	// 中文：初始化连接的 UDataStreamManager—— Init 内部会按定义自动创建子 stream
	// （ReplicationDataStream / NetTokenDataStream / 自定义 stream）。
	void InitDataStreamManager(uint32 ReplicationSystemId, uint32 ConnectionId, UDataStreamManager* DataStreamManager);
	void DeinitDataStreamManager(uint32 ConnectionId);

	void SetReplicationView(uint32 ConnectionId, const FReplicationView& ViewInfo);
	const FReplicationView& GetReplicationView(uint32 ConnectionId) const { return ReplicationViews[ConnectionId]; }

	// Flag a connection as being in a graceful-close state meant to flush pending reliable data.
	// 中文：连接进入 graceful-close 后仍保留以便 flush reliable RPC，但不再被
	// 选中进行新对象的复制（IsOpenConnection 返回 false）。
	void SetConnectionIsClosing(uint32 ConnectionId)
	{
		check(ValidConnections.GetBit(ConnectionId) == true);
		Connections[ConnectionId].bIsClosing = true;
	}
private:
	// 中文：fixed-size 并行数组 + 位图。索引即 ConnectionId，O(1) 访问，无哈希。
	TArray<FReplicationConnection> Connections;
	TArray<FReplicationView> ReplicationViews;
	FNetBitArray ValidConnections;
};

}
