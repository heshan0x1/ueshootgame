// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationConnections.cpp
// 模块：Iris / ReplicationSystem
// 功能：FReplicationConnections 实现——AddConnection 仅置位图，
//       连接的真正"装配/拆卸"在 Init/DeinitDataStreamManager 与 RemoveConnection 中完成。
//
// 与 ReplicationSystem 的协作时序：
//   1. NetDriver → UReplicationSystem::AddConnection(ConnId) → 本类 AddConnection。
//   2. NetDriver → UReplicationSystem::InitDataStreamManager(ConnId, Manager)
//        → 本类 InitDataStreamManager（创建子 stream）+ ReplicationSystem 单独 new
//          ReplicationWriter / ReplicationReader 并赋值给 FReplicationConnection。
//   3. NetDriver → UReplicationSystem::SetConnectionGracefullyClosing → SetConnectionIsClosing。
//   4. NetDriver → UReplicationSystem::RemoveConnection → 本类 RemoveConnection。
// =====================================================================================

#include "ReplicationConnections.h"
#include "Iris/DataStream/DataStreamManager.h"
#include "Iris/ReplicationSystem/NetTokenDataStream.h"
#include "Iris/ReplicationSystem/ReplicationDataStream.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Iris/ReplicationSystem/ReplicationReader.h"

namespace UE::Net::Private
{

void FReplicationConnections::Deinit()
{
	// 中文：销毁前对所有 Valid 连接逐个 RemoveConnection；ForAllSetBits 是 lambda
	// 形态遍历，遍历期间 RemoveConnection 会清位但不会破坏迭代（按字 snapshot）。
	ValidConnections.ForAllSetBits([this](uint32 ConnectionId) { RemoveConnection(ConnectionId); });
}

void FReplicationConnections::InitDataStreamManager(uint32 ReplicationSystemId, uint32 ConnectionId, UDataStreamManager* DataStreamManager)
{
	FReplicationConnection* Connection = GetConnection(ConnectionId);
	if (Connection == nullptr)
	{
		return;
	}

	// Init data stream manager and create all DataStreams
	// 中文：构建 FInitParameters —— PacketWindowSize=256 决定 ProcessPacketDelivery
	// 的 ACK 历史窗口；Manager 内部会按 ini 注册创建 ReplicationDataStream / NetTokenDataStream
	// / ChunkedDataStream 等所有 AutoCreate 流。
	UDataStream::FInitParameters InitParams;
	InitParams.ReplicationSystemId = ReplicationSystemId;
	InitParams.ConnectionId = ConnectionId;
	InitParams.PacketWindowSize = 256;

	DataStreamManager->Init(InitParams);

	// Store it.
	// 中文：以弱引存储——Manager 由 DataStreamChannel 强引（生命周期与 NetConnection 同步），
	// 这里只是查询入口。
	Connection->DataStreamManager = DataStreamManager;
}

void FReplicationConnections::SetReplicationView(uint32 ConnectionId, const FReplicationView& View)
{
	// 中文：直接覆盖；ReplicationView 是值类型（FVector + 视锥参数等），上层每帧更新。
	ReplicationViews[ConnectionId] = View;
}

void FReplicationConnections::RemoveConnection(uint32 ConnectionId)
{
	check(ValidConnections.GetBit(ConnectionId));
	// 中文：先清 ReplicationView 防止悬空；DeinitDataStreamManager 会同时
	// Deinit + delete Reader/Writer；最后整体重置 FReplicationConnection 字段。
	SetReplicationView(ConnectionId, FReplicationView());

	DeinitDataStreamManager(ConnectionId);

	Connections[ConnectionId] = FReplicationConnection();
	ValidConnections.ClearBit(ConnectionId);
}

FNetBitArray FReplicationConnections::GetOpenConnections() const
{
	// 中文：不依赖位运算捷径——逐 ConnectionId 检查 bIsClosing，构造新位图返回。
	// 调用方常在 Filter/Prioritizer 入口缓存一次本帧用。
	FNetBitArray OpenConnections(ValidConnections.GetNumBits());

	for (int32 ConnectionId = 0; ConnectionId < Connections.Num(); ++ConnectionId)
	{
		if (ValidConnections.IsBitSet(ConnectionId) && !Connections[ConnectionId].bIsClosing)
		{
			OpenConnections.SetBit(ConnectionId);
		}
	}

	return OpenConnections;
}

void FReplicationConnections::DeinitDataStreamManager(uint32 ConnectionId)
{
	if (FReplicationConnection* Connection = GetConnection(ConnectionId))
	{
		// 中文：Manager 可能已被 GC（弱引返回 null）——只在仍有效时 Deinit。
		if (UDataStreamManager* DataStreamManager = Connection->DataStreamManager.Get())
		{
			DataStreamManager->Deinit();
		}

		// This is a bit special as these are owned by ReplicationSystem rather than the ReplicationDataStream
		// 中文：Reader/Writer 的所有权说明——
		//   * 它们由 ReplicationSystem 在 InitDataStreamManager 之外构造（非 UObject）。
		//   * UReplicationDataStream 仅持引用，不负责析构。
		//   * 所以这里必须显式 Deinit + delete，否则会泄漏。
		Connection->ReplicationReader->Deinit();
		Connection->ReplicationWriter->Deinit();

		// Delete replication reader / writer
		delete Connection->ReplicationReader;
		Connection->ReplicationReader = nullptr;
		delete Connection->ReplicationWriter;
		Connection->ReplicationWriter = nullptr;
	}
}

}
