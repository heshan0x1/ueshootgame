// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationDataStream.h —— Iris 复制流的 DataStream 适配器（每连接一个）
// -----------------------------------------------------------------------------
// 角色：作为 UDataStream 的派生类接入 UDataStreamManager 框架，桥接到 per-connection
// 的 FReplicationReader / FReplicationWriter。
//
// 数据流（以发送侧为例）：
//   UDataStreamManager::WriteData
//     → UReplicationDataStream::BeginWrite / WriteData / EndWrite
//        → FReplicationWriter::BeginWrite / Write / EndWrite
//   接收：UDataStreamManager::Read → ReplicationDataStream::ReadData → FReplicationReader::Read
//   ACK/NACK：UDataStreamManager → ProcessPacketDeliveryStatus → ReplicationWriter::ProcessDeliveryNotification
//
// 配置：通过 ini `[/Script/IrisCore.DataStreamDefinitions]` 声明 Name/Class/AutoCreate 等。
// 由 UReplicationSystem 的 InitDataStreamManager 在 AddConnection 时自动创建。
// =============================================================================

#pragma once

#include "Iris/DataStream/DataStream.h"
#include "ReplicationDataStream.generated.h"

namespace UE::Net::Private
{
	class FReplicationReader;
	class FReplicationWriter;
}

UCLASS()
class UReplicationDataStream final : public UDataStream
{
	GENERATED_BODY()

private:
	UReplicationDataStream();
	virtual ~UReplicationDataStream();

	// UDataStream interface —— 将 DataStream 框架的 hook 全部转发到 Reader/Writer。
	virtual void Init(const UDataStream::FInitParameters& Params) override;            // 初始化时从连接信息中拿到 Reader/Writer 并保存
	virtual void Deinit() override;                                                    // 释放（不删除 Reader/Writer，它们由连接持有）
	virtual void Update(const FUpdateParameters& Params) override;                     // 每帧 update（推进 Writer 内部状态）
	virtual EWriteResult BeginWrite(const FBeginWriteParameters& Params) override;     // 包级写入开始（清空 WriteContext，为 Write 做准备）
	virtual EWriteResult WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord) override;
	virtual void EndWrite() override;                                                  // 包级写入结束（提交 Record / 触发 ChangeMaskCache reset）
	virtual void ReadData(UE::Net::FNetSerializationContext& Context) override;        // 接收侧入口（→ ReplicationReader::Read）
	virtual void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record) override;
	virtual bool HasAcknowledgedAllReliableData() const override;                      // 用于优雅断开：判断 Reliable RPC 是否全部确认

private:
	// 这两个指针指向"每连接"持有的 Reader/Writer（在 FReplicationConnection 中分配）。
	// 本类仅做转发，不拥有它们。
	UE::Net::Private::FReplicationReader* ReplicationReader;
	UE::Net::Private::FReplicationWriter* ReplicationWriter;
};
