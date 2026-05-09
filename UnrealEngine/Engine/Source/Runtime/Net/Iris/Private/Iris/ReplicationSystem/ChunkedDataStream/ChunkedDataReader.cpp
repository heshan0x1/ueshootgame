// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataReader.cpp
// 模块: Iris / ReplicationSystem / ChunkedDataStream（接收侧实现）
//
// 协议解码（与 Writer 对齐，详见 ChunkedDataWriter.cpp 顶部）：
//   while (continuation==1):
//     bIsInSequence : 1 bit
//     if (!bIsInSequence): seq : 11 bit
//     bIsFirstChunk : 1 bit
//     if (bIsFirstChunk):
//        bIsExportChunk : 1 bit
//        PartCount      : packed uint32
//     bIsFullChunk : 1 bit
//     if (!bIsFullChunk):
//        PartByteCount : packed uint16
//     bytes : ChunkSize（首片且 PartCount>1）or PartByteCount
//
// 重组规则：
//   - 用"序号差 SeqDelta = (Seq - ExpectedSeq) & SequenceBitMask"做环形索引；
//   - 同一序号到两次：自然覆盖（重复检测，无需显式去重表）；
//   - AssemblePayloadsPendingAssembly 严格按 ExpectedSeq 顺序消费——
//     遇到未到达的槽位（PartByteCount==0 等表征空槽）立刻停下，等数据到齐。
// =============================================================================

#include "ChunkedDataReader.h"

#include "Iris/PacketControl/PacketNotification.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Misc/ScopeExit.h"

namespace UE::Net::Private
{

// FDataChunk 默认构造：清零 + 序号哨兵 0xFFFF（与 Writer 一致）。
FChunkedDataReader::FDataChunk::FDataChunk()
: PartCount(0U)
, SequenceNumber(uint16(-1))
, PartByteCount(0U)
, bIsFirstChunk(0U)
, bIsExportChunk(0U)
{
}

// 与 Writer 协议保持一致：首片若 PartCount>1，则该首片实际承载 ChunkSize 字节
// （PartByteCount 字段被借用来表达"末片字节数"）；否则 PartByteCount 即本片字节数。
const uint32 FChunkedDataReader::FDataChunk::GetPartPayloadByteCount() const
{
	// Size is encoded by a combination of PartCount and PayloadByteCount
	// The first part contains the size of the entire payload encoded as PartCount * ChunkSize + PayloadByteCount
	return (bIsFirstChunk && (PartCount > 1U)) ? FChunkedDataStreamParameters::ChunkSize : PartByteCount;
}

// 反序列化单个分片（与 FChunkedDataWriter::FDataChunk::Serialize 对偶）。
void FChunkedDataReader::FDataChunk::Deserialize(UE::Net::FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	bIsFirstChunk = Reader->ReadBool() ? 1U : 0U;
	if (bIsFirstChunk)
	{
		// 仅首片携带 export 标志 + 总分片数。
		bIsExportChunk = Reader->ReadBool();
		PartCount = ReadPackedUint32(Reader);
	}
	else
	{
		// 中间/末片：保持上一片决定的 export 状态由 RecvQueueEntry 维护，本结构清零即可。
		bIsExportChunk = 0U;
		PartCount = 0U;
	}
			
	const bool bIsFullChunk = Reader->ReadBool();
	const uint32 ReadPartByteCount = bIsFullChunk ? FChunkedDataStreamParameters::ChunkSize : ReadPackedUint16(Reader);

	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	// 防御性裁剪：若对端发了大于 ChunkSize 的值（恶意/损坏），强制截断。
	PartByteCount = (uint16)FMath::Min(FChunkedDataStreamParameters::ChunkSize, ReadPartByteCount);

	// Read actual payload
	UE_NET_TRACE_SCOPE(Payload, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);

	// Size is encoded as a combination of PartCount and PayloadByteCount
	// The first part contains the size of the entire payload encoded as PartCount * ChunkSize + PayloadByteCount
	// So the actual payload size of the first part depends on if there are multiple parts or not.
	// 对照 Writer：首片若 PartCount>1，本片实际数据是 ChunkSize 字节而非 PartByteCount。
	const uint32 PartPayloadByteCount = (bIsFirstChunk && (PartCount > 1U)) ? FChunkedDataStreamParameters::ChunkSize : PartByteCount;
	PartPayload.SetNum(PartPayloadByteCount);
	ReadBytes(Reader, PartPayload.GetData(), PartPayloadByteCount);			
}

// 构造：缓存 Init 参数；从 ReplicationSystem 取 ObjectReferenceCache + NetTokenStore；
// 设置 ResolveContext / NetTokenResolveContext 为本连接的解析上下文（async load / token 解析）。
FChunkedDataReader::FChunkedDataReader(const UDataStream::FInitParameters& InParams)
: InitParams(InParams)
, ReplicationSystem(UE::Net::GetReplicationSystem(InParams.ReplicationSystemId))
, ObjectReferenceCache(&ReplicationSystem->GetReplicationSystemInternal()->GetObjectReferenceCache())
{
	FNetTokenStore* NetTokenStore = ReplicationSystem->GetNetTokenStore();

	// Setup internal context
	ResolveContext.ConnectionId = InitParams.ConnectionId;
	ResolveContext.RemoteNetTokenStoreState = NetTokenStore->GetRemoteNetTokenStoreState(InitParams.ConnectionId);

	NetTokenResolveContext.NetTokenStore = NetTokenStore;
	NetTokenResolveContext.RemoteNetTokenStoreState = ResolveContext.RemoteNetTokenStoreState;
}

// 释放本 Reader 在 dispatch 期间临时持有的对象引用（避免 GC）。
// 这些引用通过 ObjectReferenceCache::AddTrackedQueuedBatchObjectReference 注入；
// 若不显式 Remove，被引用对象将永远无法被 GC 回收。
void FChunkedDataReader::ResetResolvedReferences()
{
	// Make sure to release all references that we are holding on to
	if (ObjectReferenceCache)
	{
		for (const FNetRefHandle& RefHandle : ResolvedReferences)
		{
			ObjectReferenceCache->RemoveTrackedQueuedBatchObjectReference(RefHandle);
		}
	}
}

// 析构：保证 GC 引用追踪释放。
FChunkedDataReader::~FChunkedDataReader()
{
	ResetResolvedReferences();
}

// 从一个已组装好的"export-chunk payload"中提取 PackageMap exports。
// 先读 32 位 ExportsOffset 头（=引用区位长）：
//   - == 0：无 exports，直接 Deserialize quantize 数据；
//   - != 0：先 Seek 到 exports 索引区，调 ObjectReferenceCache->ReadExports
//     收集 must-be-mapped 引用，再 Seek 回 引用区开始反序列化 quantize 数据。
// 反序列化得到的 QuantizedExports 暂存在 Entry.References 中，
// 等到 dispatch 时再 dequantize 到 Reader.PackageMapExports。
bool FChunkedDataReader::ProcessExportPayload(FNetSerializationContext& Context, FRecvQueueEntry& Entry)
{
	FNetBitStreamReader ExportsReader;
	ExportsReader.InitBits(Entry.Payload.GetData(), Entry.Payload.Num() * 8U);
	FNetSerializationContext SubContext = Context.MakeSubContext(&ExportsReader);

	FNetTraceCollector* ExportsTraceCollector = nullptr;
#if UE_NET_TRACE_ENABLED
	FNetTraceCollector ExportsTraceCollectorOnStack;
	ExportsTraceCollector = &ExportsTraceCollectorOnStack;
#endif

	SubContext.SetTraceCollector(ExportsTraceCollector);

	UE_NET_TRACE_NAMED_SCOPE(ExportsTraceScope, ExportsPayload, ExportsReader, ExportsTraceCollector, ENetTraceVerbosity::Trace);

ON_SCOPE_EXIT
	{
#if UE_NET_TRACE_ENABLED
		UE_NET_TRACE_EXIT_NAMED_SCOPE(ExportsTraceScope);

		// Append huge object state at end of stream.
		// 把本子 stream 的追踪事件折叠到主流，确保 NetTrace UI 中的层级正确。
		// 一个 packet 中若有多个 exports payload，需要累加 MultiExportsPayLoadOffset。
		if (FNetTraceCollector* TraceCollector = Context.GetTraceCollector())
		{
			FNetBitStreamReader& Reader = *Context.GetBitStreamReader();
			// Inject after all other trace events
			const uint32 LevelOffset = 3U;
			FNetTrace::FoldTraceCollector(TraceCollector, ExportsTraceCollector, GetBitStreamPositionForNetTrace(Reader) + MultiExportsPayLoadOffset, LevelOffset);

			MultiExportsPayLoadOffset += ExportsReader.GetPosBits();;
		}
#endif
	};

	// Read and process exports
	const uint32 ExportsOffset = ExportsReader.ReadBits(FChunkedDataStreamParameters::NumBitsForExportOffset);

	if (SubContext.HasErrorOrOverflow())
	{
		return false;
	}

	uint32 ExportsEndPosition = 0U;
	if (ExportsOffset != 0U)
	{
		// 头部告诉我们引用区有多少位 ⇒ Seek 跳过引用区抵达 exports 索引区。
		const uint32 ReturnPos = ExportsReader.GetPosBits();
		ExportsReader.Seek(ReturnPos + ExportsOffset);

		if (!ObjectReferenceCache->ReadExports(FNetRefHandle::GetInvalid(), SubContext, &Entry.References->MustBeMappedReferences, Entry.References->IrisAsyncLoadingPriority))
		{
			return false;
		}

		ExportsEndPosition = ExportsReader.GetPosBits();
		// 回到引用区起点解 quantize 数据。
		ExportsReader.Seek(ReturnPos);
	}
	FIrisPackageMapExportsUtil::Deserialize(SubContext, Entry.References->QuantizedExports);

	// Just to get tracing to report nicely.
	// 把 Reader 移到真正的尾部位置（绕过 quantize 数据后 + exports 索引尾部），
	// 让追踪报告的字节数和实际包大小一致。
#if UE_NET_TRACE_ENABLED
	if (!SubContext.HasErrorOrOverflow() && ExportsOffset != 0U)
	{
		ExportsReader.Seek(ExportsEndPosition);
	}
#endif

	return !SubContext.HasErrorOrOverflow();
}

// 按 ExpectedSeq 顺序消费 DataChunksPendingAssembly：
//   - 仅当环首槽位的 SequenceNumber == ExpectedSeq 才推进，
//     否则停下等待缺失分片到达；
//   - 每消费一个分片：
//        * 首片：在 ReceiveQueue 创建/复用一个 FRecvQueueEntry，预分配缓冲；
//        * 追加 PartPayload 到队尾条目的 Payload；
//        * 若刚组装完一个 export-chunk payload，立即调 ProcessExportPayload，
//          清空 Payload 让该条目进入"已处理 exports，等待真实数据 payload 复用"状态；
//   - 累计未 dispatch 字节超限 ⇒ SetError。
void FChunkedDataReader::AssemblePayloadsPendingAssembly(UE::Net::FNetSerializationContext& Context)
{
	// Reset ExportsPayLoadOffset
	// 每次进入重组流程清零——本字段只服务于本次 ReadData 的 NetTrace 折叠位置。
	MultiExportsPayLoadOffset = 0U;
	while (!DataChunksPendingAssembly.IsEmpty())
	{
		const FDataChunk& CurrentChunk = DataChunksPendingAssembly.First();

		// 关键的"按序号顺序消费"逻辑：环首必须正好是 ExpectedSeq，否则停下。
		// 这是接收端实现"严格 in-order"语义的核心。
		if (CurrentChunk.SequenceNumber != ExpectedSeq)
		{
			break;
		}

		// If we have encountered an error, we will no longer try to assemble received chunks
		// 已错误：吞掉分片但不再尝试拼装（丢弃数据，等待上层关流）。
		if (!HasError())
		{
			bool bLocalHasError = false;
			if (CurrentChunk.bIsFirstChunk)
			{
				FRecvQueueEntry* CurrentEntry = ReceiveQueue.IsEmpty() ? nullptr : &ReceiveQueue.Last();

				// Validate that previous entry is complete
				// 上一条目必须已经组装完成（RemainingByteCount==0），否则协议损坏。
				if (ensure(!CurrentEntry || (CurrentEntry->RemainingByteCount == 0U)))
				{
					// If the last received payload was an export we append to the same entry.
					// 复用规则：上一条目是"已处理完 exports 但没装数据"的占位条目时，
					// 当前 first-chunk（且不是 export-chunk）会复用它——把数据装进同一 Entry，
					// References 自然就和这条数据 payload 关联起来。
					// 任何其他情况（首条目 / 当前是 export-chunk / 上条已塞过数据）都新开 Entry。
					if (!CurrentEntry || CurrentChunk.bIsExportChunk || !(CurrentEntry->bHasProcessedExports && CurrentEntry->Payload.Num() == 0U))
					{
						// Create new chunk to dispatch and start assembling payload
						CurrentEntry = &ReceiveQueue.Emplace_GetRef();							

						if (CurrentChunk.bIsExportChunk)
						{
							// 为 export 条目准备 References 容器（dequantize 后供业务读取引用用）。
							CurrentEntry->References = MakeUnique<FReferencesForImport>();					
						}
					}

					// 接收端从首片解出总长度：(PartCount-1)*ChunkSize + 末片字节数（=PartByteCount）。
					CurrentEntry->RemainingByteCount = ((CurrentChunk.PartCount - 1U) * FChunkedDataStreamParameters::ChunkSize) + CurrentChunk.PartByteCount;
						
					UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("AssemblePayloadsPendingAssembly Size: %u PartCount: %u IsExportPayload: %u"), CurrentEntry->RemainingByteCount, CurrentChunk.PartCount, CurrentChunk.bIsExportChunk);

					// 缓冲上限保护：超过 MaxUndispatchedPayloadBytes 立刻置错——
					// 防止恶意/异常对端持续发大 payload 但本端未 dispatch 导致内存炸裂。
					if (CurrentUndispatchedPayloadBytes + CurrentEntry->RemainingByteCount > MaxUndispatchedPayloadBytes)
					{
						SetError(TEXT("Error: MaxUndispatchedPayloadBytes exceeded."));
					}
					else
					{
						// 一次性预留容量，后续 Append 不会再分配。
						CurrentEntry->Payload.Reserve(CurrentEntry->RemainingByteCount);
						CurrentUndispatchedPayloadBytes += CurrentEntry->RemainingByteCount;
					}
				}
				else
				{
					// 协议错误：上一条 payload 没装完就来了新 first-chunk。
					SetError(TEXT("Error: Encountered new payload when previous one still is not fully received, DataStream will be closed."));
				}
			}
			if (!HasError())
			{
				// 把 PartPayload 追加到当前条目的 Payload 中，并扣减剩余字节数。
				FRecvQueueEntry& CurrentEntry = ReceiveQueue.Last();
				if (ensure((uint32)CurrentChunk.PartPayload.Num() <= CurrentEntry.RemainingByteCount))
				{
					CurrentEntry.Payload.Append(CurrentChunk.PartPayload);
					CurrentEntry.RemainingByteCount -= CurrentChunk.PartPayload.Num();
						
					// 刚组装完且本条目带 References ⇒ 这是一段 export payload，立即处理：
					// dequantize 头不在这里（dispatch 时再 dequantize），这里只 Deserialize 出
					// QuantizedExports + must-be-mapped references；处理完后 reset Payload 等数据 chunk。
					if (CurrentEntry.RemainingByteCount == 0 && !CurrentEntry.bHasProcessedExports && CurrentEntry.References)
					{
						// Read and process exports as soon as export payload is assembled
						if (ProcessExportPayload(Context, CurrentEntry))
						{
							// If the export payload has been processed, we reset the payload so that we can reuse the same entry for the actual data payload
							// 关键：清空 Payload 让"数据 chunk"复用本条目，并扣减字节统计。
							CurrentUndispatchedPayloadBytes -= CurrentEntry.Payload.Num();
							CurrentEntry.Payload.Reset();
							CurrentEntry.bHasProcessedExports = true;
						}
						else
						{
							SetError(TEXT("Error: Failed to ProcessExportPayload, DataStream will be closed."));
							bLocalHasError = true;
						}
					}
				}
				else
				{
					// 收到的字节超过预声明长度 ⇒ 协议错误。
					SetError(TEXT("Error: Received more data than expected when assembling payload, DataStream will be closed."));
				}
			}
		}

		// We are done with this chunk
		// 推进期望序号，弹出环首槽位（11 位回绕由调用侧读时的 SeqDelta 计算保证）。
		++ExpectedSeq;
		DataChunksPendingAssembly.PopFront();
	}

	if (DataChunksPendingAssembly.IsEmpty())
	{
		// 闲时回收存储。
		DataChunksPendingAssembly.Trim();
	}
}

// 在真正 dispatch payload 前，把 must-be-mapped 引用尽可能解析。
// 解析涉及 ObjectReferenceCache::ResolveObjectReference：
//   - 已解析 ⇒ 加入 ResolvedReferences 临时保活；
//   - 仍需 async load 且对端句柄未损坏 ⇒ 累积到 Unresolved，下次再调本函数；
//   - 句柄损坏 ⇒ 视作"不再尝试解析"，丢出未解析队列（避免无限阻塞 dispatch）。
// 返回 true ⇒ 全部解决，可 dispatch；false ⇒ 还有未解决项，业务侧应稍后重试。
bool FChunkedDataReader::TryResolveUnresolvedMustBeMappedReferences(TArray<FNetRefHandle>& MustBeMappedReferences, EIrisAsyncLoadingPriority IrisAsyncLoadingPriority)
{
	// Resolve
	TArray<FNetRefHandle> Unresolved;
	TArray<TPair<FNetRefHandle, UObject*>, TInlineAllocator<4>> QueuedObjectsToTrack;

	Unresolved.Reserve(MustBeMappedReferences.Num());
	QueuedObjectsToTrack.SetNum(MustBeMappedReferences.Num());

	// 临时把 async loading 优先级提升到 payload 携带的级别（解析后 ON_SCOPE_EXIT 还原）。
	ResolveContext.AsyncLoadingPriority = ConvertAsyncLoadingPriority(IrisAsyncLoadingPriority);
	ON_SCOPE_EXIT
	{
		ResolveContext.AsyncLoadingPriority = INDEX_NONE;
	};

	// Try to resolve references
	for (FNetRefHandle Handle : MustBeMappedReferences)
	{
		UObject* ResolvedObject = nullptr;
		ENetObjectReferenceResolveResult ResolveResult = ObjectReferenceCache->ResolveObjectReference(FObjectReferenceCache::MakeNetObjectReference(Handle), ResolveContext, ResolvedObject);
		if (EnumHasAnyFlags(ResolveResult, ENetObjectReferenceResolveResult::HasUnresolvedMustBeMappedReferences) && !ObjectReferenceCache->IsNetRefHandleBroken(Handle, true))
		{
			// 还需要等 async load。
			Unresolved.Add(Handle);
		}
		else if (ResolveResult == ENetObjectReferenceResolveResult::None)
		{
			// 完美解析；待会儿加入临时跟踪以避免 GC。
			QueuedObjectsToTrack.Emplace(Handle, ResolvedObject);
		}
		// 其他状态（包括 broken）：放弃解析、不加入 unresolved，避免阻塞 dispatch。
	}

	// If more references are resolved, add them to tracking list
	for (TPair<FNetRefHandle, UObject*>& NetRefHandleObjectPair : QueuedObjectsToTrack)
	{
		// 同一 handle 可能多次出现于不同 payload，去重避免重复 AddTrackedQueuedBatchObjectReference。
		if (!ResolvedReferences.Contains(NetRefHandleObjectPair.Key))
		{
			ResolvedReferences.Add(NetRefHandleObjectPair.Key);
			ObjectReferenceCache->AddTrackedQueuedBatchObjectReference(NetRefHandleObjectPair.Key, NetRefHandleObjectPair.Value);
		}
	}

	if (Unresolved.Num())
	{
		// Upate status
		// 把"还未解决"的列表写回入参，下次调用直接处理这部分。
		MustBeMappedReferences = MoveTemp(Unresolved);
	
		return false;
	}

	// Nothing more to do.
	MustBeMappedReferences.Reset();

	return true;	
}

// 派发一条已就绪 payload 给业务回调。流程：
//   1) 若队首条目还在组装（RemainingByteCount != 0）⇒ NothingToDispatch；
//   2) 若条目带 References：
//        a. 启动 async load（若需要）+ 解析 must-be-mapped 引用；
//           未完成 ⇒ WaitingForMustBeMappedReferences（保留条目，下次再来）；
//        b. dequantize QuantizedExports → Reader.PackageMapExports
//           （这是 dispatch 内 << 出的 UObject 引用能解析出来的关键）；
//   3) 调业务回调（FChunkedDataStreamExportReadScope 的使用方在此触发）；
//   4) 弹出条目，闲时收缩。
UChunkedDataStream::EDispatchResult FChunkedDataReader::DispatchReceivedPayload(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction)
{
	if (ReceiveQueue.IsEmpty() || HasError())
	{
		return UChunkedDataStream::EDispatchResult::NothingToDispatch;
	}

	FRecvQueueEntry& Entry = ReceiveQueue.First();
	if (Entry.RemainingByteCount != 0U)
	{
		// 队首还在组装中；in-order 语义不允许跳过队首派发后面的。
		return UChunkedDataStream::EDispatchResult::NothingToDispatch;
	}

	const bool bProcessReferences = Entry.References.IsValid();
	if (bProcessReferences)
	{					
		// 若启用 async load 且仍有未解析的 must-be-mapped 引用：返回等待，业务侧稍后重试。
		if (ObjectReferenceCache->ShouldAsyncLoad() && !TryResolveUnresolvedMustBeMappedReferences(Entry.References->MustBeMappedReferences, Entry.References->IrisAsyncLoadingPriority))
		{
			// Wait for async loading to complete if we have any mustbemapped entries
			UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Waiting for %d MustBeMapped references to be resolvable"), Entry.References->MustBeMappedReferences.Num());
			return UChunkedDataStream::EDispatchResult::WaitingForMustBeMappedReferences;
		}

		// Setup context for dispatch
		// 构造一个临时 InternalContext 给 dequantize 使用。
		FInternalNetSerializationContext InternalContext;
		FInternalNetSerializationContext::FInitParameters InternalContextInitParams;
		InternalContextInitParams.ReplicationSystem = ReplicationSystem;
		InternalContextInitParams.ObjectResolveContext = ResolveContext;
		InternalContext.Init(InternalContextInitParams);

		FNetSerializationContext Context;
		Context.SetLocalConnectionId(InitParams.ConnectionId);
		Context.SetInternalContext(&InternalContext);

		// Dequantize exports
		// QuantizedExports → PackageMapExports。dispatch 内 PackageMap << UObject* 时
		// 通过 PackageMapExports 把 NetRefHandle 解到真实 UObject*（已经被 Resolve 过）。
		FIrisPackageMapExportsUtil::Dequantize(Context, Entry.References->QuantizedExports, PackageMapExports);
	}

	if (Entry.Payload.Num())
	{
		UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Dispatching payload with %u bytes and %u potential exports"), Entry.Payload.Num(), PackageMapExports.References.Num());
		DispatchPayloadFunction(MakeConstArrayView(Entry.Payload));
		CurrentUndispatchedPayloadBytes -= Entry.Payload.Num();
	}

	if (!bProcessReferences)
	{
		// 不带 References 的 payload：本批 dispatch 完毕可立刻释放保活引用——
		// 没有 must-be-mapped references 也不会因引用 GC 影响业务。
		ResetResolvedReferences();
	}
	ReceiveQueue.PopFront();

	if (ReceiveQueue.IsEmpty())
	{
		ReceiveQueue.Trim();
	}

	return UChunkedDataStream::EDispatchResult::Ok;
}

// 批量 dispatch：循环到非 Ok 为止。返回最后一个非 Ok 状态：
//   - WaitingForMustBeMappedReferences ⇒ 队首被 async load 卡住，本帧到此为止；
//   - NothingToDispatch ⇒ 队列已空 / 队首未就绪 / 错误态。
UChunkedDataStream::EDispatchResult FChunkedDataReader::DispatchReceivedPayloads(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction)
{		
	UChunkedDataStream::EDispatchResult Result = UChunkedDataStream::EDispatchResult::Ok;
	for (; Result == UChunkedDataStream::EDispatchResult::Ok; Result = DispatchReceivedPayload(DispatchPayloadFunction))
	{
	}

	return Result;
}

// 已完整组装、可立刻 dispatch 的 payload 个数（RemainingByteCount==0 计数）。
// 注意：组装中的条目不计入。
uint32 FChunkedDataReader::GetNumReceivedPayloadsPendingDispatch() const
{
	uint32 ReadyToDispatchCount = 0U;
	for (const FRecvQueueEntry& Entry : ReceiveQueue)
	{
		ReadyToDispatchCount += Entry.RemainingByteCount == 0U ? 1U : 0U;
	}
	return ReadyToDispatchCount;
}

// ReadData：从 packet 的本流字段反复读取 "continuation + chunk"，
// 把每个分片按序号差 SeqDelta 写入 DataChunksPendingAssembly[SeqDelta]，
// 最后调 AssemblePayloadsPendingAssembly 尽量推进重组。
//
// 关键点：
//   - SeqDelta 把 11 位回绕的序号差直接当作环索引；序号回绕本身不打断重组
//     （只要 in-flight 总数不超过 2048，与 Writer 端窗口一致）；
//   - 同序号到达多次（重复包）会自然覆盖槽位 → "重复检测"零成本；
//   - 缺失序号槽位保持默认 FDataChunk（PartByteCount=0），等待真正分片到达后被覆盖。
void FChunkedDataReader::ReadData(UE::Net::FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	// $TODO: set this up in DataStreamManager instead. UE-243627
	// 局部 InternalContext：dispatch / dequantize 期间需要解析对象引用。
	// 注释 TODO 指出未来应把这块上提到 Manager 层统一管理。
	FInternalNetSerializationContext InternalContext;
	FInternalNetSerializationContext::FInitParameters InternalContextInitParams;
	InternalContextInitParams.ReplicationSystem = ReplicationSystem;
	InternalContextInitParams.ObjectResolveContext = ResolveContext;
	InternalContext.Init(InternalContextInitParams);

	Context.SetLocalConnectionId(InitParams.ConnectionId);
	Context.SetInternalContext(&InternalContext);

	uint16 LastReadSeq = uint16(-1);
	while (Reader->ReadBool())
	{
		// 续帧位 true 后任何错误立即停止读取（剩余位流由上层 Manager 处理）。
		if (Reader->IsOverflown())
		{
			break;
		}

		UE_NET_TRACE_SCOPE(DataChunk, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);

		UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(SequenceScope, static_cast<const TCHAR*>(nullptr), *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		// Read sequence number
		// 与 Writer 编码对齐：bIsInSequence=1 ⇒ Seq=LastReadSeq+1；否则读 11 位。
		const bool bIsInSequence = Reader->ReadBool();
		uint16 ReadSeq = 0U;
		if (bIsInSequence)
		{
			ReadSeq = (LastReadSeq + 1U) & FChunkedDataStreamParameters::SequenceBitMask;
		}
		else
		{
			ReadSeq = (uint16)Reader->ReadBits(FChunkedDataStreamParameters::SequenceBitCount);
		}

		if (Reader->IsOverflown())
		{
			break;
		}

		LastReadSeq = ReadSeq;

		const uint16 Seq = LastReadSeq;
		// 序号差：注意是 11 位无符号回绕减法，自动处理 ExpectedSeq 跨越 2048 边界。
		const uint16 SeqDelta = (Seq - ExpectedSeq) & FChunkedDataStreamParameters::SequenceBitMask;

		// Make room to store missing sequence numbers.
		// 扩展环形缓冲到 SeqDelta+1 大小，缺失槽位用默认 FDataChunk 占位。
		// 这种"按差分扩展"是检测丢包/乱序的核心：
		//   - 若 SeqDelta=0：恰是 ExpectedSeq，可立刻参与组装；
		//   - 若 SeqDelta>0：前面有 SeqDelta 个未到达槽位，等它们到齐才能推进。
		{
			DataChunksPendingAssembly.Reserve(FMath::Max(DataChunksPendingAssembly.Num(), (int32)(SeqDelta + 1)));
			while ((uint32)DataChunksPendingAssembly.Num() <= SeqDelta)
			{
				DataChunksPendingAssembly.Add(FDataChunk());
			}
		}

		// 写入对应槽位（同序号重复到达会被自然覆盖，等同"重复检测+丢弃"）。
		FDataChunk& Chunk = DataChunksPendingAssembly[SeqDelta];
		Chunk.SequenceNumber = ExpectedSeq + SeqDelta;
		Chunk.Deserialize(Context);

#if UE_NET_TRACE_ENABLED
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

		UE_LOG_CHUNKEDDATASTREAM_CONN(Verbose, TEXT("Deserialize Seq:%u (local:%u), Expected %u"), Seq, Chunk.SequenceNumber, ExpectedSeq);
	}

	// Assemble data chunks that we have received
	// 一次 ReadData 末尾尝试重组：能消多少消多少（按 ExpectedSeq 顺序）。
	AssemblePayloadsPendingAssembly(Context);

	// Remove dangling reference to InternalContext on the stack
	// 解除外层 Context 对栈上 InternalContext 的引用，避免该指针在栈帧弹出后悬空。
	Context.SetInternalContext(nullptr);
}

// 进入错误态：仅打印一次，避免反复刷屏。bHasError 被 UChunkedDataStream::HasError 暴露给上层。
void FChunkedDataReader::SetError(const FString& InErrorMessage)
{
	UE_LOG_CHUNKEDDATASTREAM_CONN(Error, TEXT("FChunkedDataReader::ErrorEncountered() %s"), *InErrorMessage);
	if (!bHasError)
	{
		bHasError = true;
	}
}

bool FChunkedDataReader::HasError() const
{
	return bHasError;
}

} // End of namespace(s)
