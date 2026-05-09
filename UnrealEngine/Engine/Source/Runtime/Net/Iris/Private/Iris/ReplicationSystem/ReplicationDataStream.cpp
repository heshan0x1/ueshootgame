// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationDataStream.cpp —— Iris 复制流接入 DataStream 框架
// -----------------------------------------------------------------------------
// 本类是 UDataStream 的派生，仅做"转发"工作：把 UDataStreamManager 的回调路由到
// 每连接的 FReplicationReader / FReplicationWriter。
//
// 注意：Reader / Writer 由 FReplicationConnection 直接持有，本类不拥有它们；
// Init 时通过 ReplicationSystemInternal->Connections 找回连接对应的 Reader/Writer 指针。
// =============================================================================

#include "ReplicationDataStream.h"
#include "Iris/ReplicationSystem/ReplicationReader.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ReplicationDataStream)

UReplicationDataStream::UReplicationDataStream()
: ReplicationReader(nullptr)
, ReplicationWriter(nullptr)
{
}

UReplicationDataStream::~UReplicationDataStream()
{
}

// 从 ReplicationSystemInternal->Connections 找到 ConnectionId 对应的连接，
// 缓存 Reader/Writer 指针；并把 NetExports 注入 Writer（用于跨包导出 ACK 跟踪）。
void UReplicationDataStream::Init(const UDataStream::FInitParameters& Params)
{
	Super::Init(Params);

	using namespace UE::Net::Private;

	UReplicationSystem* ReplicationSystem = UE::Net::GetReplicationSystem(Params.ReplicationSystemId);
	if (ensure(ReplicationSystem))
	{
		// Init ReplicationReader and writer.
		if (FReplicationSystemInternal* ReplicationSystemInternal = ReplicationSystem->GetReplicationSystemInternal())
		{
			FReplicationConnections& Connections = ReplicationSystemInternal->GetConnections();
			FReplicationConnection* Connection = Connections.GetConnection(Params.ConnectionId);
			if (ensure(Connection))
			{
				ReplicationWriter = Connection->ReplicationWriter;
				ReplicationReader = Connection->ReplicationReader;

				if (ensure(Params.NetExports))
				{
					ReplicationWriter->SetNetExports(*Params.NetExports);
				}
			}
		}
	}
}

// 仅清理本类的引用；Reader/Writer 由 Connection 负责销毁。
void UReplicationDataStream::Deinit()
{
	Super::Deinit();

	ReplicationWriter = nullptr;
	ReplicationReader = nullptr;
}

// 每帧 update：转发到 Writer（推进 HugeObject 队列状态、stats 等）。
void UReplicationDataStream::Update(const FUpdateParameters& Params)
{
	if (ReplicationWriter)
	{
		ReplicationWriter->Update(Params);
	}
}

// 包级写入开始：转发到 Writer::BeginWrite。返回 NoData 时 Manager 不会调用 WriteData。
UDataStream::EWriteResult UReplicationDataStream::BeginWrite(const FBeginWriteParameters& Params)
{
	if (ReplicationWriter != nullptr)
	{
		return ReplicationWriter->BeginWrite(Params);
	}

	return EWriteResult::NoData;
}

// 实际写入：转发到 Writer::Write。
// 注意：OutRecord 此处未填充——Iris 的 ReplicationRecord 由 Writer 内部直接管理（见
// FReplicationRecord），不通过 DataStream 框架的 FDataStreamRecord 接口。
UDataStream::EWriteResult UReplicationDataStream::WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord)
{
	if (ReplicationWriter != nullptr)
	{
		return ReplicationWriter->Write(Context);
	}

	return EWriteResult::NoData;
}

// 包级写入结束：转发到 Writer::EndWrite（提交 Record / 更新 Stats）。
void UReplicationDataStream::EndWrite()
{
	if (ReplicationWriter != nullptr)
	{
		return ReplicationWriter->EndWrite();
	}
}

// 接收侧：转发到 Reader::Read。Iris 在解析过程中通过 Context.SetError(...) 报告错误。
void UReplicationDataStream::ReadData(UE::Net::FNetSerializationContext& Context)
{
	if (ReplicationReader != nullptr)
	{
		ReplicationReader->Read(Context);
	}
}

// ACK/NACK 转发到 Writer::ProcessDeliveryNotification。
// Iris 不使用 DataStream 框架的 FDataStreamRecord（Record 参数被忽略）；它的"流水账"
// 完整保存在 Writer 内部的 FReplicationRecord 中，按"包内 InfoCount"批量出队。
void UReplicationDataStream::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record)
{
	if (ReplicationWriter != nullptr)
	{
		ReplicationWriter->ProcessDeliveryNotification(Status);
	}
}

// 用于优雅关闭：仅当所有 Reliable 附件（RPC / 子对象 attachment）都送达并 ACK 后
// 才能干净地 EndReplication / 切到 Closing 状态。
bool UReplicationDataStream::HasAcknowledgedAllReliableData() const
{
	if (ReplicationWriter != nullptr)
	{
		return ReplicationWriter->AreAllReliableAttachmentsSentAndAcked();
	}

	return true;
}
