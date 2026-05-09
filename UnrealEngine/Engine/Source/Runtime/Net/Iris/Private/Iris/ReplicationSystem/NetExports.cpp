// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetExports.cpp —— Export ACK 跟踪实现
// -------------------------------------------------------------------------------------------------------------
// 时序示意：
//   InitExportRecordForPacket    -> CurrentExportInfo = 当前 in-flight 长度
//   CommitExportsToRecord(BatchA) -> 把 BatchA 的导出追加到 in-flight 队列尾
//   CommitExportsToRecord(BatchB) -> 同上
//   ...
//   PushExportRecordForPacket    -> ExportRecord.Enqueue(本包新增数量)
//
// 一段时间后：
//   ProcessPacketDeliveryStatus(Delivered) -> 把 in-flight 队列头部 Count 个移到 Acknowledged
//   ProcessPacketDeliveryStatus(Lost)      -> 直接丢弃；下次 ConditionalWrite 重新判断要不要 export
// =============================================================================================================

#include "NetExports.h"
#include "Iris/PacketControl/PacketNotification.h"

namespace UE::Net::Private
{

void FNetExports::InitExportRecordForPacket()
{
	// 包开始：快照 in-flight 队列长度。后续 CommitExportsToRecord 会让队列变长，
	// 在 PushExportRecordForPacket 时计算"新增量 = 当前长度 - 此快照"。
	CurrentExportInfo.NetHandleExportCount = static_cast<uint32>(NetHandleExports.Count());
	CurrentExportInfo.NetTokenExportCount = static_cast<uint32>(NetTokenExports.Count());
}

void FNetExports::CommitExportsToRecord(FExportScope& ExportScope)
{
	// 把"本 batch 已 export"的所有 handle/token 按写入顺序追加到 in-flight 队列。
	// 注意：BatchExports 是 ExportScope 的 ExportContext 内部累加器；scope 析构时会被丢弃，所以必须在结束前 commit。
	const FNetExportContext::FBatchExports& BatchExports = ExportScope.ExportContext.GetBatchExports();
	for (FNetRefHandle Handle : BatchExports.HandlesExportedInCurrentBatch)
	{
		NetHandleExports.Enqueue(Handle);
	}
	for (FNetToken Token : BatchExports.NetTokensExportedInCurrentBatch)
	{
		NetTokenExports.Enqueue(Token);
	}
}

void FNetExports::PushExportRecordForPacket()
{
	// 包结束：本包新增了多少个 handle/token export？通过 (当前队列长度 - 包开始时的快照) 计算。
	FExportInfo Info;
	Info.NetHandleExportCount = static_cast<uint32>(NetHandleExports.Count()) - CurrentExportInfo.NetHandleExportCount;
	Info.NetTokenExportCount = static_cast<uint32>(NetTokenExports.Count()) - CurrentExportInfo.NetTokenExportCount;
	ExportRecord.Enqueue(Info);
}

void FNetExports::AcknowledgeBatchExports(const FNetExportContext::FBatchExports& BatchExports)
{
	// OOB 路径：该 batch 不会经过 packet ACK 流程（已立即可靠送达），直接合并到 Acknowledged 集合。
	AcknowledgedExports.AcknowledgedExportedHandles.Append(BatchExports.HandlesExportedInCurrentBatch);
	AcknowledgedExports.AcknowledgedExportedNetTokens.Append(BatchExports.NetTokensExportedInCurrentBatch);
}

FNetExports::FExportInfo FNetExports::PopExportRecord()
{
	check(ExportRecord.Count());

	// FIFO 取出最早一包的 record（与 PacketControl 的 ACK 顺序保持一致）。
	const FExportInfo ExportInfo = ExportRecord.Peek();
	ExportRecord.Pop();

	return ExportInfo;
}

void FNetExports::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status)
{
	FExportInfo ExportInfo = PopExportRecord();
	if (Status == UE::Net::EPacketDeliveryStatus::Delivered)
	{
		// 已送达：把 in-flight 队列头部对应数量的条目移入 Acknowledged 集合。
		// 之后 ConditionalWriteNetTokenData 看到这些 handle/token 已 ACK，就不会再次 export。
		uint32 NetHandleExportCount = ExportInfo.NetHandleExportCount;
		while (NetHandleExportCount)
		{
			FNetRefHandle Handle = NetHandleExports.PeekNoCheck();
			AcknowledgedExports.AcknowledgedExportedHandles.Add(Handle);
			NetHandleExports.PopNoCheck();
			--NetHandleExportCount;
		}
		uint32 NetTokenExportCount = ExportInfo.NetTokenExportCount;
		while (NetTokenExportCount)
		{
			FNetToken Token = NetTokenExports.PeekNoCheck();
			AcknowledgedExports.AcknowledgedExportedNetTokens.Add(Token);
			NetTokenExports.PopNoCheck();
			--NetTokenExportCount;
		}
	}
	else
	{
		// 丢包：从 in-flight 队列直接弹出（不进 Acknowledged）。
		// 不在这里"立即重发"——下次 ReplicationWriter 写到引用了这些 token 的 batch 时，
		// ConditionalWriteNetTokenData 会发现"未 ACK"自动重新 export。
		NetHandleExports.PopNoCheck(ExportInfo.NetHandleExportCount);
		NetTokenExports.PopNoCheck(ExportInfo.NetTokenExportCount);
	}
}

}
