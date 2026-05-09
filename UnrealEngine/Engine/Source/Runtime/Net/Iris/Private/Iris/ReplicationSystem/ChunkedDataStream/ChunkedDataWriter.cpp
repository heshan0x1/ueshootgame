// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataWriter.cpp
// 模块: Iris / ReplicationSystem / ChunkedDataStream（发送侧实现）
//
// 协议布局参考（与 ReplicationSystem.md §6.7 一致）：
//   每条 packet 的本流字段是一段 substream，反复编码"continuation bit + chunk"：
//     while (continuation==1):
//        bIsInSequence : 1 bit
//        if (!bIsInSequence): seq : 11 bit            // 显式序号
//        chunk:
//            bIsFirstChunk : 1 bit
//            if (bIsFirstChunk):
//                bIsExportChunk : 1 bit
//                PartCount      : packed uint32
//            bIsFullChunk : 1 bit
//            if (!bIsFullChunk):
//                PartByteCount : packed uint16
//            payload bytes : ChunkSize（首片且 PartCount>1）or PartByteCount
//   末尾 continuation==0 表示本 packet 中本流的分片读取结束。
//
//   首片用 PartCount + 末片 PartByteCount 共同编码总字节数：
//     TotalBytes = (PartCount - 1) * ChunkSize + LastPartByteCount
//   其中 LastPartByteCount 仅在首片携带，作为"最后一片到底有多少字节"。
// =============================================================================

#include "ChunkedDataWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"

#include "Iris/PacketControl/PacketNotification.h"
#include "Iris/ReplicationSystem/NetExports.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Containers/ContainersFwd.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Misc/ScopeExit.h"

namespace UE::Net::Private
{

// FDataChunk 默认构造：所有字段清零，序号置 0xFFFF（哨兵值）。
FChunkedDataWriter::FDataChunk::FDataChunk()
: PayloadByteOffset(0U)
, PartCount(0U)
, SequenceNumber(uint16(-1))
, PartByteCount(0U)
, bIsFirstChunk(0U)
, bIsExportChunk(0U)
{
}

// 把单个分片串行化进 bitstream（注意：序号 + 续帧位由调用者 WriteData 写入，
// 本函数只写"分片自身的有效载荷头 + 字节"）。
void FChunkedDataWriter::FDataChunk::Serialize(UE::Net::FNetSerializationContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// Size is encoded as a combination of PartCount and PayloadByteCount
	// The first part contains the size of the entire payload encoded as PartCount * ChunkSize + PayloadByteCount
	// 首片携带 PartCount（总分片数）+ 末片实际字节数；中间/末片只携带本片字节数。
	uint32 PartPayLoadBytesToWrite = PartByteCount;
	if (Writer->WriteBool(bIsFirstChunk))
	{
		// 首片：写 bIsExportChunk + PartCount。
		Writer->WriteBool(bIsExportChunk);
		WritePackedUint32(Writer, PartCount);
		// 首片若 PartCount>1，本片必为满 ChunkSize；
		// 若 PartCount==1，唯一一片就是末片，PartByteCount 即整个 payload 大小。
		PartPayLoadBytesToWrite = PartCount > 1U ? FChunkedDataStreamParameters::ChunkSize : PartByteCount;
	}
	// 1 bit "本片是否满 ChunkSize"，避免大多数情况下额外写 packed uint16。
	const bool bIsFullChunk = PartByteCount == FChunkedDataStreamParameters::ChunkSize;
	if (!Writer->WriteBool(bIsFullChunk))
	{
		// 非满（典型场景：末片）才写实际字节数。
		WritePackedUint16(Writer, PartByteCount);
	}

	// Write actual payload
	UE_NET_TRACE_SCOPE(Payload, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
#if UE_NET_TRACE_ENABLED
		// 首片且为 export-chunk：把 exports payload 录制时收集的子事件折叠到主 collector。
		if (bIsFirstChunk && bIsExportChunk)
		{
			if (FNetTraceCollector* Collector = SrcEntry->References->TraceCollector)
			{
				FNetTrace::FoldTraceCollector(Context.GetTraceCollector(), Collector, GetBitStreamPositionForNetTrace(*Writer));
			}
		}
#endif // UE_NET_TRACE_ENABLED

	// 实际字节来源：export-chunk 取自 References->ExportsPayload；普通数据 chunk 取自 SrcEntry->Payload。
	// 这里直接从源缓冲按 PayloadByteOffset 拷贝，避免分片重复持有大块字节。
	const uint8* PayloadData = bIsExportChunk ? SrcEntry->References->ExportsPayload.GetData() : SrcEntry->Payload->GetData();
	WriteBytes(Writer, PayloadData + PayloadByteOffset, PartPayLoadBytesToWrite);
}

// 构造：把 Init 参数缓存下来；从 ReplicationSystemId 还原出 ReplicationSystem 指针，
// 进而拿到 ObjectReferenceCache（用于 CreateExportPayload 中的对象引用导出处理）。
FChunkedDataWriter::FChunkedDataWriter(const FInitParameters& InParams)
: InitParams(InParams)
, ReplicationSystem(UE::Net::GetReplicationSystem(InParams.ReplicationSystemId))
, ObjectReferenceCache(&ReplicationSystem->GetReplicationSystemInternal()->GetObjectReferenceCache())
{
}

// 把一段连续 payload 切分为 ceil(N / ChunkSize) 片，全部 push 到 DataChunksPendingSend。
// 入参 SrcEntry 提供"源条目引用计数 + 源字节缓冲"；调用方控制 bIsExportPayload 区分导出/数据。
//
// 关键点：
//   1) 每个分片自增 NextSequenceNumber（11 位回绕）；
//   2) 首片的 PartByteCount 编码"末片"实际字节数（接收端组合解码总长度）；
//   3) 中间片 PartByteCount = ChunkSize（满）；末片 PartByteCount = 余数。
bool FChunkedDataWriter::SplitPayload(FSendQueueEntry& SrcEntry, TConstArrayView<uint8> Payload, bool bIsExportPayload)
{
	const uint8* SrcPayload = Payload.GetData();
	const uint32 SrcPayloadBytes = Payload.Num();

	// ceil 除法计算分片总数。
	const uint32 ChunkCount = (SrcPayloadBytes + FChunkedDataStreamParameters::ChunkSize - 1U) / FChunkedDataStreamParameters::ChunkSize;

	DataChunksPendingSend.Reserve(DataChunksPendingSend.Num() + int32(ChunkCount));

	UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Split Payload %u Bytes into %u chunks"), SrcPayloadBytes, ChunkCount);
	
	uint32 CurrentOffset = 0U;
	for (uint32 ChunkIt = 0, ChunkEndIt = ChunkCount; ChunkIt != ChunkEndIt; ++ChunkIt)
	{
		const bool bIsFirstChunk = ChunkIt == 0U;

		FDataChunk Chunk;
			
		// SrcEntry 用 TRefCountPtr 持有，AddRef 保证 SendQueueEntry 在所有分片
		// 还未结算完之前不会被 PopDeliveredChunks 释放。
		Chunk.SrcEntry = &SrcEntry;
		Chunk.PartCount = ChunkCount;
		Chunk.bIsFirstChunk = bIsFirstChunk ? 1U : 0U;
		Chunk.bIsExportChunk = bIsExportPayload ? 1U : 0U;
		// 11 位序号（高位字段不重要——发送时按 SequenceBitMask 折叠）。
		Chunk.SequenceNumber = NextSequenceNumber++;

		// Copy relevant data from our temporary buffer.
		// 注意：实际不"拷贝"——分片只记录 PayloadByteOffset，串行化时再从源缓冲按偏移读取。
		Chunk.PayloadByteOffset = CurrentOffset;
		Chunk.PartByteCount = (uint16)FPlatformMath::Min(SrcPayloadBytes - CurrentOffset, FChunkedDataStreamParameters::ChunkSize);
		CurrentOffset += Chunk.PartByteCount;

		// We encode full payload size in first part as (SrcPayloadBytes - ((PartCount - 1) * FChunkedDataStreamParameters::ChunkSize))
		// 首片例外：把 PartByteCount 改写为"末片"字节数，配合 PartCount 即可让接收端
		// 一开始就预分配整段 Payload 缓冲（CurrentEntry->RemainingByteCount = (PartCount-1)*ChunkSize + LastPartByteCount）。
		if (bIsFirstChunk && Chunk.PartCount > 1U)
		{
			const uint16 LastPartByteCount = SrcPayloadBytes % FChunkedDataStreamParameters::ChunkSize;
			// 整除（余数为 0）的极端情况：末片正好满 ChunkSize。
			Chunk.PartByteCount = LastPartByteCount != 0 ? LastPartByteCount : FChunkedDataStreamParameters::ChunkSize;
		}
			
		DataChunksPendingSend.Add(MoveTemp(Chunk));
	}

	return ChunkCount > 0;
}

// 当业务侧在 ExportWriteScope 内 << 序列化了 UObject 引用时，PackageMap 把
// 这些引用收集到 PackageMapExports（以及 NetTokensPendingExport）。本函数：
//   1) 申请一个 ExportsBufferMaxSize 大的临时缓冲 ExportsPayload；
//   2) 在该缓冲的 bitstream 中先写一个 32 位 ExportsOffset 占位头部；
//   3) 调 FIrisPackageMapExportsUtil::Quantize 把对象引用 quantize 到 QuantizedExports；
//   4) 通过 FNetExports::FExportScope 让 ObjectReferenceCache 收集"待导出"列表；
//   5) Serialize quantize 后的引用集合（不含真正的 export 索引数据）；
//   6) 调 ObjectReferenceCache->WritePendingExports 在末尾追加 export 索引数据；
//   7) 回填 32 位头部，记录"中间引用区"的位长度，让对端按 offset Seek 跳过；
// 返回的 FReferencesForExport 会作为 FSendQueueEntry 的"前置导出附属"
// 在 UpdateSendQueue 中先于真实数据 payload 被 SplitPayload。
FChunkedDataWriter::FReferencesForExport* FChunkedDataWriter::CreateExportPayload()
{
	// 注意条件：仅当 PackageMapExports 非空、且 NetTokens 非待导出时才生成 export payload。
	// （NetTokens 走另一条路径，由 NetTokenDataStream 处理；这里不混用。）
	if (!PackageMapExports.IsEmpty() && NetTokensPendingExport.IsEmpty())
	{
		TUniquePtr<FReferencesForExport> Result = MakeUnique<FReferencesForExport>();
		Result->ExportsPayload.AddUninitialized(ExportsBufferMaxSize);

		uint32 ExportsPayloadBytes = 0;
		{
			FNetBitStreamWriter ExportsWriter;
			const uint32 MaxExportsPayloadBytes = Result->ExportsPayload.Num();
			ExportsWriter.InitBytes(Result->ExportsPayload.GetData(), MaxExportsPayloadBytes);

			// Create context
			FNetSerializationContext Context(&ExportsWriter);
			FInternalNetSerializationContext InternalContext(ReplicationSystem);
			Context.SetInternalContext(&InternalContext);
			Context.SetLocalConnectionId(InitParams.ConnectionId);

			// Temporary quantized state
			FIrisPackageMapExportsQuantizedType QuantizedExports = {};

			// We need to release dynamic data on exit
			// quantize 后的状态可能有动态字段（FNetRefHandle 数组等），无论成功失败都要释放。
			ON_SCOPE_EXIT
			{
				FIrisPackageMapExportsUtil::FreeDynamicState(QuantizedExports);
			};

			FIrisPackageMapExportsUtil::Quantize(Context, PackageMapExports, NetTokensPendingExport, QuantizedExports);

			// Setup export scope
			// FExportScope：让 ObjectReferenceCache 在写期间把"必须导出"的引用收集到 BatchExports，
			// 之后 PopDeliveredChunks 时调 AcknowledgeBatchExports 通知"对端已收到这些导出"。
			FNetExports::FExportScope ExportScope = InitParams.NetExports->MakeExportScope(Context, Result->BatchExports);

#if UE_NET_TRACE_ENABLED
			FNetTraceCollector* LocalTraceCollector = UE_NET_TRACE_CREATE_COLLECTOR(ENetTraceVerbosity::Trace);
			Context.SetTraceCollector(LocalTraceCollector);
#endif

			const uint32 ExportHeaderPos = ExportsWriter.GetPosBits();

			UE_NET_TRACE_SCOPE(ExportPayload, ExportsWriter, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);

			// Header
			// 占位：32 位 ExportsOffset；后面 WroteExports 时回填实际偏移。
			ExportsWriter.WriteBits(0, FChunkedDataStreamParameters::NumBitsForExportOffset);

			// Append potential exports
			ObjectReferenceCache->AddPendingExports(Context, MakeArrayView(QuantizedExports.ObjectReferenceStorage.GetData(), QuantizedExports.ObjectReferenceStorage.Num()));

			// Serialize the reference data
			// 引用本身：序列化 quantized 数据（NetRefHandle、对象类信息等）。
			FIrisPackageMapExportsUtil::Serialize(Context, QuantizedExports);

			// 当前 ExportsWriter 写入的引用区位长（去除 32 位头）。
			const uint32 WrittenBitsInBatch = (ExportsWriter.GetPosBits() - ExportHeaderPos) - FChunkedDataStreamParameters::NumBitsForExportOffset;

			// Serialize exports if there are any
			// 真正的 exports 索引数据：NetRefHandle ↔ NetGUID 或 export marker，写在引用区之后。
			const FObjectReferenceCache::EWriteExportsResult WriteExportResult = ObjectReferenceCache->WritePendingExports(Context, 0);
			if (WriteExportResult == FObjectReferenceCache::EWriteExportsResult::BitStreamOverflow)
			{
				// 缓冲溢出：本批 exports 太大（超过 ExportsBufferMaxSize）。
				// 返回 nullptr 让 EnqueuePayload 拒绝整条入队。
				return nullptr;
			}
			else if (WriteExportResult == FObjectReferenceCache::EWriteExportsResult::WroteExports)
			{
				// Go back and update header that we have exports to read.
				// 真正写了 exports：回填头部，告诉 Reader "Skip 这么多位即可跳到 exports 区"。
				FNetBitStreamWriteScope SizeScope(ExportsWriter, ExportHeaderPos);
				ExportsWriter.WriteBits(WrittenBitsInBatch, FChunkedDataStreamParameters::NumBitsForExportOffset);
			}
			ExportsWriter.CommitWrites();

			ExportsPayloadBytes = ExportsWriter.GetPosBytes();

#if UE_NET_TRACE_ENABLED
			Result->TraceCollector = LocalTraceCollector;
			LocalTraceCollector = nullptr;
#endif
		}

		// Trim down the payload.
		// 从 ExportsBufferMaxSize 收缩到实际写入字节数；EAllowShrinking::Yes 强制释放多余内存。
		Result->ExportsPayload.SetNum(ExportsPayloadBytes, EAllowShrinking::Yes);

		return Result.Release();
	}

	return nullptr;
}

// 清空当前累积的 PackageMap exports / NetTokens 待导出列表。
// 调用时机：每次 EnqueuePayload 末尾——下次入队是独立批次，不应共享上一批的引用集。
void FChunkedDataWriter::ResetExports()
{
	PackageMapExports.Reset();
	NetTokensPendingExport.Reset();
}

// 业务入队主入口。流程：
//   1) 计算"将要新增"的总字节数（payload + 可选 exports payload）；
//   2) 与 SendBufferMaxSize 比较，超限直接拒绝；
//   3) 创建 FSendQueueEntry，挂上 References；
//   4) ResetExports 准备下一批。
bool FChunkedDataWriter::EnqueuePayload(const TSharedPtr<TArray64<uint8>>& Payload)
{
	uint32 TotalPayloadByteCount = (uint32)Payload->Num();

	// Nothing to send
	// 空 payload：直接成功（无意义但不算错）。
	if (TotalPayloadByteCount == 0U)
	{
		return true;
	}

	bool bCanEnqueueData = (TotalPayloadByteCount + CurrentBytesInSendQueue) <= SendBufferMaxSize;

	// Do we have exports
	// 仅当数据本身可入队时才尝试 quantize exports（避免无谓的开销）。
	TUniquePtr<FReferencesForExport> Exports(bCanEnqueueData ? CreateExportPayload() : nullptr);
	if (Exports)
	{
		// 二次校验：含 exports 后是否仍未超上限。
		TotalPayloadByteCount += Exports->ExportsPayload.Num();
		bCanEnqueueData = (TotalPayloadByteCount + CurrentBytesInSendQueue) <= SendBufferMaxSize;
	}

	if (!bCanEnqueueData)
	{
		UE_LOG_CHUNKEDDATASTREAM_CONN(Warning, TEXT("EnqueuePayload SendBufferFull: Cannot enqueue payload with %u Bytes, CurrentBytesInSendQueue %u"), TotalPayloadByteCount, CurrentBytesInSendQueue);
		return false;
	}

	// 入队（TUniquePtr 保证地址稳定，便于 FDataChunk::SrcEntry 安全 AddRef）。
	TUniquePtr<FSendQueueEntry>& NewEntry = SendQueue.Emplace_GetRef(MakeUnique<FSendQueueEntry>(Payload));
	NewEntry->References = MoveTemp(Exports);
	CurrentBytesInSendQueue += TotalPayloadByteCount;

	UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("EnqueuePayload NewEntry %" INT64_FMT " Bytes, %u exports CurrentBytesInSendQueue %u"), Payload->Num(), PackageMapExports.References.Num(), CurrentBytesInSendQueue);

	// 不论本次有没有 exports，都重置——下次入队是独立批次。
	ResetExports();

	return true;
}

// 是否允许继续发送：未确认窗口（DataChunksPendingAck）未达 2048 上限。
// 等于 TCP 风格的滑动窗口（但本协议每次 ProcessPacketDeliveryStatus 才前移）。
bool FChunkedDataWriter::CanSend() const
{
	return (uint32)DataChunksPendingAck.Num() < FChunkedDataStreamParameters::MaxUnackedDataChunkCount;
}

// BeginWrite：被 UDataStreamManager 在每个 packet 写入开始时调用——
// 仅声明"是否还有数据"，真正写入由 WriteData 完成。
UDataStream::EWriteResult FChunkedDataWriter::BeginWrite(const FBeginWriteParameters& Params)
{
	if (SendQueue.Num() && CanSend())
	{
		return EWriteResult::HasMoreData;
	}

	return EWriteResult::NoData;
}

// 所有 reliable data 都已 ACK ⇒ SendQueue 必为空（因为 SendQueue 中条目只在
// PopDeliveredChunks 中"全部分片均 ACK 且 RefCount==0"时才 RemoveSendQueueEntry）。
bool FChunkedDataWriter::HasAcknowledgedAllReliableData() const
{
	return SendQueue.IsEmpty();
}

// 在 WriteData 内被反复调用：保证 DataChunksPendingSend 中始终有可发送内容
// （需要时切分 SendQueue 中下一条 RefCount==0 的 entry）。
//
// 注：RefCount==0 表示"该 entry 还未被 SplitPayload 切过分片"，
// 因为 SplitPayload 内部会对 SrcEntry 调 AddRef（通过 TRefCountPtr）。
bool FChunkedDataWriter::UpdateSendQueue()
{
	for (TUniquePtr<FSendQueueEntry>& Entry : SendQueue)
	{
		if (Entry->RefCount == 0U)
		{
			// Should we enqueue exports as separate entry?
			// 若条目带有 exports：先切分 exports payload（先发送），再切分数据 payload。
			// Reader 端依赖此顺序——"export-chunk first chunk → 数据 first chunk" 序号连续到达。
			if (Entry->References)
			{
				const bool bIsExportPayload = true;
				SplitPayload(*Entry, Entry->References->ExportsPayload, bIsExportPayload);
			}
			SplitPayload(*Entry, *Entry->Payload.Get());
			return DataChunksPendingSend.Num() > 0;
		}
	}

	// 没有需要切分的条目，但环里还有可发送分片（重发场景）。
	if (!DataChunksPendingSend.IsEmpty())
	{
		return true;
	}	
		
	return false;
}

// WriteData：在每个 packet 写入阶段，把分片串行化进 substream，
// 直到 packet 用尽 / SendQueue 空 / 在途窗口满。
//
// 关键约定：
//   - 用 substream 隔离写入失败回滚；
//   - 末尾 WriteBool(false) 作为"本流分片结束"哨兵；
//   - 序号采用"按序差分编码"——相邻分片若序号连续仅写 1 bit，否则写 1 bit + 11 bit；
//   - "本 packet 写了多少分片"被巧妙地 reinterpret 进 OutRecord 指针 bits，
//     省去 record 对象分配。Manager 在 ACK/Lost 时直接把这个数值还原回来。
UDataStream::EWriteResult FChunkedDataWriter::WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	
	// Write chunks, we need at least 1 bit free...
	// 没数据 / 没空间 ⇒ NoData。
	if (SendQueue.Num() == 0 || Writer->GetBitsLeft() < 1U)
	{
		// If we have no pending data in-flight we can trim down our storage
		// 完全静止时收缩两个 RingBuffer，避免长期峰值后内存不释放。
		if (SendQueue.Num() == 0)
		{
			DataChunksPendingSend.Trim();
			DataChunksPendingAck.Trim();
		}
		
		return EWriteResult::NoData;
	}

	// Write data until we have no more data to write or it does not fit
	uint32 WrittenCount = 0;

	// Setup a substream and context for writing data
	// 留 1 bit 给后面的"end-of-stream"标记（false）。
	FNetBitStreamWriter SubStream = Writer->CreateSubstream(Writer->GetBitsLeft() - 1U);
	FNetSerializationContext SubContext = Context.MakeSubContext(&SubStream);

	uint16 PrevWrittenSeq = uint16(-1);
	int32 CurrentChunkIndex = 0;
	bool bHasMoreDataToSend = false;
	for (;;)
	{
		// 每次循环都尝试拉一批新分片（必要时切分新 entry）。
		bHasMoreDataToSend = CanSend() && UpdateSendQueue();
		if (!bHasMoreDataToSend || CurrentChunkIndex >= DataChunksPendingSend.Num())
		{
			break;
		}

		const FDataChunk& Chunk = DataChunksPendingSend.GetAtIndexNoCheck(CurrentChunkIndex);
		const uint16 CurrentSeq = Chunk.SequenceNumber;
				
		// 该序号已发送且尚未 ACK ⇒ 跳过（本 packet 不重发，避免重复消耗带宽）。
		// 真正"重发"由 ProcessPacketDeliveryStatus(Lost) 清 Sent 位后下次 WriteData 触发。
		if (IsSequenceSent(CurrentSeq))
		{
			++CurrentChunkIndex;
			continue;
		}

		{
			UE_NET_TRACE_SCOPE(DataChunk, SubStream, SubContext.GetTraceCollector(), ENetTraceVerbosity::Verbose);

			// 写入失败回滚：写到一半 substream 溢出时，恢复到本分片开头位置。
			FNetBitStreamRollbackScope SequenceRollback(SubStream);

			// Continuation marker
			// 续帧标记：true=后面还有分片；末尾会写一个 false 作为终止。
			SubStream.WriteBool(true);

			// Write sequence number, only if it differs from previous written one.
			// 序号差分编码：与上一片 +1（mod 2048）相同 ⇒ 1 bit；否则 1+11 bit。
			const uint16 Seq = Chunk.SequenceNumber & FChunkedDataStreamParameters::SequenceBitMask;
			const bool bIsInSequence = Seq == ((PrevWrittenSeq + 1U) & FChunkedDataStreamParameters::SequenceBitMask);
			PrevWrittenSeq = Seq;

			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(SequenceScope, static_cast<const TCHAR*>(nullptr), SubStream, SubContext.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

			if (!SubStream.WriteBool(bIsInSequence))
			{
				SubStream.WriteBits(Seq, FChunkedDataStreamParameters::SequenceBitCount);
			}

			// Write chunk
			// 分片头 + payload 字节。
			Chunk.Serialize(SubContext);

			if (SubStream.IsOverflown())
			{
				// 溢出：rollback scope 自动回退到本分片 begin，结束循环。
				break;
			}

#if UE_NET_TRACE_ENABLED
			// 追踪命名：便于在 NetTrace UI 中观察分片序号 / 首片 / 总片数。
			if (FNetTrace::GetNetTraceVerbosityEnabled(ENetTraceVerbosity::VeryVerbose))
			{
				TStringBuilder<64> Builder;
				if (Chunk.bIsFirstChunk)
				{
					Builder.Appendf(TEXT("Seq %u First part of %u"), Chunk.SequenceNumber, Chunk.PartCount);
				}
				else
				{
					Builder.Appendf(TEXT("Seq %u"), Chunk.SequenceNumber);
				}
				UE_NET_TRACE_SET_SCOPE_NAME(SequenceScope, Builder.ToString());
			}
#endif // UE_NET_TRACE_ENABLED

			UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Serialized Seq:%u (local:%u)"), Seq, CurrentSeq);

		}

		// Enqueue in our record as well for resending if we drop data
		// 把本分片序号入 Pending ACK 队列；序号也置 Sent 位。
		// 注意：我们记录的是"序号"，不是 PendingSend 下标——后者会随 PopDeliveredChunks 偏移，不稳定。
		DataChunksPendingAck.Add((uint16)CurrentSeq);
		SetSequenceIsSent(CurrentSeq);
		++WrittenCount;
		++CurrentChunkIndex;
	}

	// Commit substream
	if (WrittenCount)
	{
		Writer->CommitSubstream(SubStream);
		// 末尾续帧 false：告知 Reader 本流分片结束。
		Writer->WriteBool(false);

		// Store number of written batches in the external record pointer
		// 关键技巧：把"分片数"塞进 OutRecord 指针 bits（UPTRINT 与 ptr 同长）。
		// Manager 把这个 record 原样回传到 ProcessPacketDeliveryStatus，那时 reinterpret 回数值。
		// 省去 record 对象分配/释放的开销。
		UPTRINT& OutRecordCount = *reinterpret_cast<UPTRINT*>(&OutRecord);
		OutRecordCount = WrittenCount;

		return bHasMoreDataToSend ? EWriteResult::HasMoreData : EWriteResult::Ok;
	}
	else
	{
		// 一分片都没写入：丢弃 substream，保持外层 writer 位置不变。
		Writer->DiscardSubstream(SubStream);

		return bHasMoreDataToSend ? EWriteResult::HasMoreData : EWriteResult::NoData;
	}
}

// 顺序查找移除指定 SendQueueEntry。
// 通常 SendQueue 长度很小（业务侧 EnqueuePayload 不会无限制堆积——SendBufferMaxSize 限流），
// O(N) 查找可接受。
void FChunkedDataWriter::RemoveSendQueueEntry(FSendQueueEntry* SendQueueEntry)
{
	const int32 Index = SendQueue.IndexOfByPredicate([SendQueueEntry](const TUniquePtr<FSendQueueEntry>& Entry) { return Entry.Get() == SendQueueEntry; });
	if (Index != INDEX_NONE)
	{
		SendQueue.RemoveAt(Index);
	}
}

// PopDeliveredChunks：沿 DataChunksPendingSend 环首"贪婪累计弹出" — 
// 严格 in-order delivery 语义：只要环首分片被 ACK，就向前推进；
// 一旦遇到未 ACK 的分片就停下（等其后到达）。
//
// 当某个 SendQueueEntry 的所有分片都被弹出（RefCount 归零，
// 因为 TRefCountPtr<FSendQueueEntry> 在每个 FDataChunk 析构时 Release）：
//   - 把该 entry 携带的 BatchExports 通知给 NetExports：这些导出已被对端确认，
//     ObjectReferenceCache 后续可在其他 stream 复用这些导出而无需重发；
//   - 从 SendQueue 中移除该 entry，扣减 CurrentBytesInSendQueue。
void FChunkedDataWriter::PopDeliveredChunks()
{
	while (!DataChunksPendingSend.IsEmpty())
	{
		const FDataChunk& CurrentChunk = DataChunksPendingSend.First();

		FSendQueueEntry* SendQueueEntry = CurrentChunk.SrcEntry;

		const uint32 Index = SequenceToIndex(CurrentChunk.SequenceNumber);
		// 环首未 ACK：保持 in-order——后续即使有 ACK 也不能跳过它，等其到达。
		if (!IsIndexAcked(Index))
		{
			break;
		}

		// 清位图，准备被新序号复用（11 位序号空间会回绕）。
		ClearIndexIsAcked(Index);
		ClearIndexIsSent(Index);

		// 弹出环首；此时 FDataChunk 析构 → SrcEntry Release → 可能让 RefCount 降到 0。
		DataChunksPendingSend.PopFrontNoCheck();

		// NOTE: it is intentional that we wait with removing the SendQueue entry until we know that all data before it has
		// been delivered to the client to ensure that potential exports has been processed before we acknowledge them
		// If refcount is zero, we have delivered our payload and can treat exports as exported
		// 仅当本 entry 所有分片都已弹出时才结算——保证 exports 在被
		// AcknowledgeBatchExports（让 ObjectReferenceCache 把它们视作"对端已知"）
		// 之前确实已被对端读取处理。
		if (SendQueueEntry->RefCount == 0U)
		{
			UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Completed SendQueueEntry 0x%p"), SendQueueEntry);
			// We need to explicitly acknowledge exports made through the huge object batch
			if (SendQueueEntry->References)
			{
				InitParams.NetExports->AcknowledgeBatchExports(SendQueueEntry->References->BatchExports);
				CurrentBytesInSendQueue -= SendQueueEntry->References->ExportsPayload.Num();
			}
			CurrentBytesInSendQueue -= (uint32)SendQueueEntry->Payload->Num();
			RemoveSendQueueEntry(SendQueueEntry);
		}
	}
}

// 从 Manager 接收每个已发出 packet 的最终投递结果，把对应分片标记 ACK 或 Lost。
//
// 重要：
//   - WriteData 中把"本 packet 写了多少分片"塞到 record 指针 bits 里，
//     这里 reinterpret 还原；
//   - DataChunksPendingAck 是按 packet 写入顺序 push 的"序号 FIFO"，
//     根据数量从队首批量 pop，保证哪些序号属于本 packet。
//   - Lost：仅清 Sent 位 → 下次 WriteData 重发；序号、SendQueue、PendingSend 不变。
//   - Delivered：置 Acked 位 + 调 PopDeliveredChunks 推进窗口。
void FChunkedDataWriter::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record)
{
	// The Record pointer is used as storage for the number of batches to process
	uint32 RecordCount = (uint32)reinterpret_cast<UPTRINT>(Record);

	if (Status == UE::Net::EPacketDeliveryStatus::Lost)
	{
		while (RecordCount)
		{
			// Mark entries as not sent
			// 清 Sent 标志，下个 WriteData 该序号可重新发送（自动重传）。
			const uint32 LostSeq = DataChunksPendingAck.PopFrontValue();
			ClearSequenceIsSent(LostSeq);
			--RecordCount;
			UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Notified Dropped Seq %u"), LostSeq);
		}
	}
	else
	{
		// Delivered：标记 ACK，再尝试把窗口前移。
		while (RecordCount)
		{
			const uint32 DeliveredSeq = DataChunksPendingAck.PopFrontValue();
			// Mark entries as not sent
			// 注释笔误："not sent" 实际是 "acked"。
			SetSequenceIsAcked(DeliveredSeq);
			--RecordCount;
			UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Notified Delivered Seq %u"), DeliveredSeq);
		}
		PopDeliveredChunks();
	}
}

} // End of namespace(s)
