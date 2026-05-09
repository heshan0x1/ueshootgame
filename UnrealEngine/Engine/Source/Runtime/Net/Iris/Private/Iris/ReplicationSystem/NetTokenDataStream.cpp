// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetTokenDataStream.cpp —— Token Export 独立 DataStream 实现
// -------------------------------------------------------------------------------------------------------------
// 比特流格式（WriteData 内部）：
//   [子流：保留 1 bit Stop 位]
//     while (有 Token 且能塞下) {
//        WriteBool(true);          // 1 bit "有下一个 Token"
//        WriteNetToken(Token);     // PackedUint32(Index) + 1 bit Authority + 5 bit TypeId
//        WriteTokenData(Token);    // 具体值（FName/FString/Struct...）
//     }
//   WriteBool(false);              // 终止标志
//
// 丢包处理：
//   - Lost：把 NetTokenExports（in-flight）末尾 Count 个 Token 反向插回 NetTokensPendingExport 队首，下次重发；
//   - Delivered：从 NetTokenExports 移除（不再追踪）。
// =============================================================================================================

#include "NetTokenDataStream.h"

#include "Iris/IrisConstants.h"
#include "Iris/PacketControl/PacketNotification.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Iris/ReplicationSystem/NetExports.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Core/IrisLog.h"
#include "Net/Core/Trace/NetTrace.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetTokenDataStream)

#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#	define UE_NET_ENABLE_NETTOKEN_LOG 0
#else
#	define UE_NET_ENABLE_NETTOKEN_LOG 0
#endif 

#if UE_NET_ENABLE_NETTOKEN_LOG
#	define UE_LOG_NETTOKEN(Format, ...)  UE_LOG(LogIris, Log, Format, ##__VA_ARGS__)
#else
#	define UE_LOG_NETTOKEN(...)
#endif

#define UE_LOG_NETTOKEN_WARNING(Format, ...)  UE_LOG(LogIris, Warning, Format, ##__VA_ARGS__)

namespace UE::Net::Private
{

// CVar：是否在新连接接入时把"本端已分配的全部 Token"预先排入 NetTokensPendingExport，
// 以便对端尽早拥有完整的 Token 表（牺牲启动带宽换取后续解析稳定）。默认 false。
static bool bIrisPreExportExistingNetTokensOnConnect = false;
static FAutoConsoleVariableRef CVarIrisPreExportExistingNetTokensOnConnect(
	TEXT("net.Iris.IrisPreExportExistingNetTokensOnConnect"),
	bIrisPreExportExistingNetTokensOnConnect,
	TEXT("If true we will enqueue all existing NetTokens for pre-export when a new connection is added."
	));

}

UNetTokenDataStream::UNetTokenDataStream()
: NetTokenStore(nullptr)
, RemoteNetTokenStoreState(nullptr)
, NetExports(nullptr)
, ReplicationSystemId(UE::Net::InvalidReplicationSystemId)
, ConnectionId(~0U)
{
}

UNetTokenDataStream::~UNetTokenDataStream()
{
}

void UNetTokenDataStream::Init(const UDataStream::FInitParameters& Params)
{
	Super::Init(Params);

	using namespace UE::Net;

	ReplicationSystemId = Params.ReplicationSystemId;
	ConnectionId = Params.ConnectionId;

	// 通过 ReplicationSystemId 找到关联的 ReplicationSystem，从而定位 NetTokenStore + 该连接的 RemoteState。
	UReplicationSystem* ReplicationSystem = UE::Net::GetReplicationSystem(ReplicationSystemId);
	NetTokenStore = ReplicationSystem->GetNetTokenStore();
	RemoteNetTokenStoreState = NetTokenStore->GetRemoteNetTokenStoreState(Params.ConnectionId);
	NetExports = Params.NetExports;

	// $IRIS $TODO: if we want to make this into a real feature we need to expose some sort of api to mark tokens for pre-export
	// 实验性"预导出"路径：把本端已有 Token 全部入队，确保对端尽早收到（详见 CVar 注释）。
	if (Private::bIrisPreExportExistingNetTokensOnConnect)
	{
		TArray<FNetToken> Tokens(NetTokenStore->GetAllNetTokens());
		for (const FNetToken& Token : Tokens)
		{
			NetTokensPendingExport.Add(Token);
		}
	}
}

void UNetTokenDataStream::AddNetTokenForExplicitExport(UE::Net::FNetToken NetToken)
{
	NetTokensPendingExport.Add(NetToken);
}

// 数据流框架查询本流是否有数据：仅当队列非空时返回 HasMoreData，让后续 WriteData 真正运行。
UDataStream::EWriteResult UNetTokenDataStream::BeginWrite(const FBeginWriteParameters& Params)
{
	if (!ensure(NetTokenStore) || NetTokensPendingExport.Num() == 0)
	{
		return EWriteResult::NoData;
	}

	return EWriteResult::HasMoreData;
}

UDataStream::EWriteResult UNetTokenDataStream::WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	
	if (!ensure(NetTokenStore) || NetTokensPendingExport.Num() == 0 || Writer->GetBitsLeft() < 1U)
	{
		// If we have no pending data in-flight we can trim down our storage
		// 既无待发送、又无 in-flight，可以收回环形缓冲未使用的内存。
		if (NetTokenExports.Num() == 0)
		{
			NetTokenExports.Trim();
			NetTokensPendingExport.Trim();
		}
		
		return EWriteResult::NoData;
	}

	const FStringTokenStore* StringTokenStore = NetTokenStore->GetDataStore<const FStringTokenStore>();
	FNetExportContext* ExportContext = Context.GetExportContext();

	// Write data until we have no more data to write or it does not fit
	uint32 WrittenCount = 0;

	UE_NET_TRACE_SCOPE(NetTokenDataStream, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	// We use a substream to reserve a stop bit
	// 预留 1 bit 用于流末尾的"无更多 Token"停止标志（见循环结束后的 WriteBool(false)）。
	FNetBitStreamWriter SubStream = Writer->CreateSubstream(Writer->GetBitsLeft() - 1U);
	FNetSerializationContext SubContext = Context.MakeSubContext(&SubStream);

	const bool bIsNetTokenAuthority = NetTokenStore->IsAuthority();
	while (NetTokensPendingExport.Num())
	{
		// 每个 Token 的写入是原子的：失败时通过 RollbackScope 回滚 SubStream + ExportContext。
		FNetBitStreamRollbackScope SequenceRollback(SubStream);
		FNetExportRollbackScope ExportsRollbackScope(SubContext);

		// Peek at front index
		const FNetToken& Token = NetTokensPendingExport.GetAtIndexNoCheck(0);

		// We do not need to export tokens assigned by authority unless we are the authority.
		// 边界条件：
		//   - 如果本端是 Client 且 Token 是 Authority 分配的，对端（Server）一定已经知道，不需要再 export；
		//   - 如果 Token 已在 ExportContext 中标记 IsExported（同帧内已经从其它路径 export 过），跳过。
		if (!(Token.IsAssignedByAuthority() && !bIsNetTokenAuthority) && !ExportContext->IsExported(Token))
		{
			UE_NET_TRACE_NAMED_SCOPE(ExportScope, NetTokenExport, SubStream, SubContext.GetTraceCollector(), ENetTraceVerbosity::Verbose);

			// 1 bit "Continue" + Token + Token Data
			SubStream.WriteBool(true);
			NetTokenStore->WriteNetToken(SubContext, Token);
			NetTokenStore->WriteTokenData(SubContext, Token);

			if (SubStream.IsOverflown())
			{
				// 当前 Token 写不下：RollbackScope 自动回滚此 Token 的写入，跳出循环。
				break;
			}
			else
			{
				UE_NET_TRACE_SET_SCOPE_NAME(ExportScope, StringTokenStore->ResolveToken(Token));

				// Mark Token as exported
				ExportContext->AddExported(Token);

				// Enqueue in our record as well for resending if we drop data
				// 写入成功 → 加入 in-flight 列表，等待 ProcessPacketDeliveryStatus 决定 ACK / 重发。
				NetTokenExports.Add(Token);
				++WrittenCount;
			}
		}

		NetTokensPendingExport.PopFrontNoCheck();
	}

	// Commit substream
	if (WrittenCount)
	{
		// 把成功写入的子流提交到主流，并补一个 0 bit 作为"无更多 Token"标志。
		Writer->CommitSubstream(SubStream);
		Writer->WriteBool(false);

		// Store number of written batches in the external record pointer
		// 把 WrittenCount 直接塞到 OutRecord 指针的位上（不分配对象），ProcessPacketDeliveryStatus 时直接 reinterpret 回 UPTRINT。
		UPTRINT& OutRecordCount = *reinterpret_cast<UPTRINT*>(&OutRecord);
		OutRecordCount = WrittenCount;

		const bool bHasMoreDataToSend = NetTokensPendingExport.Num() > 0;
		return bHasMoreDataToSend ? EWriteResult::HasMoreData : EWriteResult::Ok;
	}
	else
	{
		// 一个都没写进去 → 丢弃子流，本次空走。
		Writer->DiscardSubstream(SubStream);

		const bool bHasMoreDataToSend = NetTokensPendingExport.Num() > 0;

		// $IRIS: $TODO: Fix me when we have addressed issues with cases where we have data to write but we did not fit anything, if over-commit is allowed we should request a new packet
		//return bHasMoreDataToSend ? EWriteResult::HasMoreData : EWriteResult::NoData;

		ensureAlways(!bHasMoreDataToSend);
		return EWriteResult::NoData;
	}
}

// 接收侧：和 WriteData 镜像——读 1 bit 决定是否还有 Token；命中则 ReadNetToken + ReadTokenData，把数据写入 Remote 状态表。
void UNetTokenDataStream::ReadData(UE::Net::FNetSerializationContext& Context)
{
	using namespace UE::Net;

	if (!ensure(NetTokenStore))
	{
		return;
	}

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	UE_NET_TRACE_SCOPE(NetTokenDataStream, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	while (Reader->ReadBool())
	{
		if (Reader->IsOverflown())
		{
			break;
		}

		FNetToken Token = NetTokenStore->ReadNetToken(Context);
		NetTokenStore->ReadTokenData(Context, Token, *RemoteNetTokenStoreState);
	}
}

// 丢包处理：
//   - Lost：把 NetTokenExports 队首 Count 个 Token 反向插回 NetTokensPendingExport 队首（保持顺序），下次 Write 重发。
//     注：Token export 的 ACK 状态本身由 FNetExports 管理；这里只关心"序列化数据"的丢失重发，所以走自己的 Pending 队列。
//   - Delivered/其它：直接从 NetTokenExports 删除（数据已送达，不再追踪）。
void UNetTokenDataStream::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record)
{
	// The Record pointer is used as storage for the number of batches to process
	UPTRINT RecordCount = reinterpret_cast<UPTRINT>(Record);

	// Acknowledgments of exported tokens is handled by the NetExports class, but in this case we want to explicitly resend the export
	if (Status == UE::Net::EPacketDeliveryStatus::Lost)
	{
		while (RecordCount)
		{
			// Could use PopFrontValue but do not want to have the RangeCheck
			NetTokensPendingExport.AddFront(NetTokenExports.GetAtIndexNoCheck(0));
			NetTokenExports.PopFrontNoCheck();
			--RecordCount;
		}
	}
	else
	{
		while (RecordCount)
		{
			NetTokenExports.PopFrontNoCheck();
			--RecordCount;
		}
	}
}

// 当无 in-flight 也无 pending 时，本流 "已可靠送达全部数据"。
bool UNetTokenDataStream::HasAcknowledgedAllReliableData() const
{
	return NetTokensPendingExport.Num() == 0 && NetTokenExports.Num() == 0;
}
