// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetTokenDataStream.h —— "Token Export"独立 DataStream
// -------------------------------------------------------------------------------------------------------------
// 模块定位：把 NetToken 对应的"具体值数据"（FName/FString/Struct...）作为独立的 DataStream 发送/接收，
//           与对象状态复制（ReplicationDataStream）解耦。
//
// 协作链路：
//   ① ReplicationWriter.Write 把对象状态序列化到比特流时，遇到 NetToken 字段会通过 NetTokenStore::AppendExport
//      把 Token 加入"本批次待 export"集合（FNetExportContext::PendingExports）。
//   ② FNetBlobManager / ReplicationDataStream 把这些 PendingExports 转移到 NetTokenDataStream 的 NetTokensPendingExport。
//   ③ NetTokenDataStream::WriteData 在 DataStream 框架的 BeginWrite 阶段被调用，把 Token+Data 写到自己的子流中。
//   ④ Reader 端：UDataStreamManager 根据 ini 中的注册顺序保证 NetTokenDataStream 比 ReplicationDataStream 先 Read，
//      因此后续读 ReplicationData 时 Token 已经可以 Resolve。
//
// 与 NetExports 的协作：
//   - NetExports 跟踪 "对象 ↔ NetToken" 三类导出的 ACK 状态；NetTokenDataStream 只负责 Token Data 的发送+丢包重发。
//   - Lost 时把 NetTokenExports 末尾的 Token 重新塞回 NetTokensPendingExport，下次再 export。
// =============================================================================================================

#pragma once

#include "Iris/DataStream/DataStream.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Containers/RingBuffer.h"

#include "NetTokenDataStream.generated.h"

namespace UE::Net
{
	class FNetToken;
	class FNetTokenStore;
	class FNetTokenStoreState;
	class FStringTokenStore;

	namespace Private
	{
		class FNetExports;
	}
}

UCLASS()
class UNetTokenDataStream final : public UDataStream
{
	GENERATED_BODY()

public:

	// 接收侧：Reader 在解析 NetToken 字段时通过此方法获取本连接的远端状态表。
	const UE::Net::FNetTokenStoreState* GetRemoteNetTokenStoreState() const { return RemoteNetTokenStoreState; }
	// 显式追加一个待导出的 Token（用于不走常规 ExportContext 的特殊场景）。
	void AddNetTokenForExplicitExport(UE::Net::FNetToken NetToken);

private:
	UNetTokenDataStream();
	virtual ~UNetTokenDataStream();

	// UDataStream interface
	virtual void Init(const UDataStream::FInitParameters& Params) override;
	virtual EWriteResult BeginWrite(const FBeginWriteParameters& Params) override;
	virtual EWriteResult WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord) override;
	virtual void ReadData(UE::Net::FNetSerializationContext& Context) override;
	virtual void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record) override;
	virtual bool HasAcknowledgedAllReliableData() const override;

private:

	// Record of in-flight NetTokens
	// 已写入比特流但未 ACK 的 Token 列表（按发送顺序）；丢包时重新入队 NetTokensPendingExport。
	TRingBuffer<UE::Net::FNetToken> NetTokenExports;

	// All NetTokens enqueued for explicit export
	// 待发送的 Token 队列；每帧 WriteData 从队首消费，写入子流；丢包重发会插回队首（保持顺序）。
	TRingBuffer<UE::Net::FNetToken> NetTokensPendingExport;

	// External record, simply track how many records we have in the internal record
	// DataStream 框架要求每次 WriteData 返回一个 record，以便后续 ProcessPacketDeliveryStatus 处理 ACK；
	// 我们直接复用指针的 bit 位存放 "本次写入了多少个 Token"（Count），不真正分配对象。
	struct FExternalRecord : public FDataStreamRecord
	{
		uint32 Count = 0U;
	};

	UE::Net::FNetTokenStore* NetTokenStore;                  // 系统级 Token 仓库（共享）。
	UE::Net::FNetTokenStoreState* RemoteNetTokenStoreState;  // 本连接的远端 Token 状态表（接收时用）。
	UE::Net::Private::FNetExports* NetExports;               // 本连接的 export ACK 跟踪器。

	uint32 ReplicationSystemId;
	uint32 ConnectionId;
};
