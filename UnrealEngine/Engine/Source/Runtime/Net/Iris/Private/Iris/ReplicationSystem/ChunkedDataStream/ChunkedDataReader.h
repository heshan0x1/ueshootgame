// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataReader.h
// 模块: Iris / ReplicationSystem / ChunkedDataStream（接收侧实现）
//
// 概述：
//   FChunkedDataReader 是 UChunkedDataStream 的"接收端引擎"。每条 stream 内部
//   独占一个 Reader。
//
//   职责：
//     1) 解析对端 packet 中本 stream 的所有分片（FDataChunk::Deserialize）；
//     2) 按"期望序号" ExpectedSeq 把分片归档到 DataChunksPendingAssembly 环形
//        缓冲对应"序号差"槽位；同序号到达再一次的分片被自然覆盖（重复检测）；
//     3) AssemblePayloadsPendingAssembly：从环首按 ExpectedSeq 严格连续地
//        消费已就位的分片，逐步把 PartPayload 拼接进 ReceiveQueue 队尾的
//        FRecvQueueEntry::Payload；
//     4) 若是 export-chunk 头，先把它的"export payload"独立组装并立即
//        ProcessExportPayload（dequantize 对象引用 + NetTokens），后续紧邻
//        的数据 chunk 会复用同一 FRecvQueueEntry，把 References 与 Payload
//        关联起来；
//     5) DispatchReceivedPayload(s)：业务侧轮询取出已组装完整的 payload；
//        若 payload 含 must-be-mapped 引用且尚未 async-load 完成，会返回
//        WaitingForMustBeMappedReferences 并保留队首条目；
//     6) 缓冲上限保护：超过 MaxUndispatchedPayloadBytes 则置错误态。
//
//   传输语义：与 Writer 端严格对齐——单档"可靠 + 严格按序"。
//   ExpectedSeq 单调递增（按 11 位回绕由 SeqDelta 处理），不接受序号倒退。
//
// 重组数据通路：
//   ReadData → 按 SeqDelta 写入 DataChunksPendingAssembly[SeqDelta]
//             → AssemblePayloadsPendingAssembly 按 ExpectedSeq 依次取
//                 ├─ bIsFirstChunk → ReceiveQueue 新建条目（含 RemainingByteCount）
//                 │                  若 bIsExportChunk → 配 References，附属于"下一个"
//                 │                  数据条目（先组装 exports，再 ProcessExportPayload，
//                 │                  最后 Reset Payload 复用此条目装数据）
//                 └─ 追加 PartPayload 到 ReceiveQueue.Last().Payload
// =============================================================================

#pragma once

#include "ChunkedDataStreamCommon.h"
#include "Containers/Array.h"
#include "Containers/RingBuffer.h"

#include "Iris/DataStream/DataStream.h"

#include "Iris/ReplicationSystem/ChunkedDataStream/ChunkedDataStream.h"
#include "Iris/ReplicationSystem/ObjectReferenceCacheFwd.h"
#include "Iris/ReplicationSystem/ObjectReferenceTypes.h"

#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "Iris/Serialization/IrisPackageMapExportUtil.h"
#include "Iris/Serialization/NetExportContext.h"

#include "Net/Core/NetToken/NetTokenExportContext.h"

namespace UE::Net::Private
{

// Used by ChunkedDataStream to reading and dispatching incoming data
class FChunkedDataReader
{
public:

	using EWriteResult = UDataStream::EWriteResult;
	using FBeginWriteParameters = UDataStream::FBeginWriteParameters;
	using FInitParameters = UDataStream::FInitParameters;

	// FReferencesForImport：单条接收 payload 携带的导出附属（与 Writer 侧 FReferencesForExport 对应）。
	// - QuantizedExports：从 export payload 反序列化得到的、尚未 dequantize 的对象引用集合；
	// - MustBeMappedReferences：必须在 dispatch 之前解析完成的引用列表
	//   （ObjectReferenceCache::ReadExports 写入；DispatchReceivedPayload 在调用业务回调
	//   前会循环 TryResolveUnresolvedMustBeMappedReferences 直到全部解析或仍有未解析）；
	// - IrisAsyncLoadingPriority：转发给 ObjectReferenceCache 解析的优先级。
	struct FReferencesForImport
	{
		FIrisPackageMapExportsQuantizedType QuantizedExports;
		TArray<FNetRefHandle> MustBeMappedReferences;
		EIrisAsyncLoadingPriority IrisAsyncLoadingPriority = EIrisAsyncLoadingPriority::Default;

		// 析构：释放 QuantizedExports 中的动态字段（字符串/数组等）。
		~FReferencesForImport()
		{
			FIrisPackageMapExportsUtil::FreeDynamicState(QuantizedExports);
		}
	};

	// RecvQueue entry
	// FRecvQueueEntry：业务侧 dispatch 维度的一条 payload，按业务入队顺序排列。
	// - Payload：已组装好的完整字节缓冲（dispatch 时通过 TConstArrayView64 传给业务回调）；
	// - References：可选的导出（export-chunk 先到达时建立）；
	// - RemainingByteCount：还差多少字节才组装完整（>0 ⇒ 队尾条目仍在重组中，不可 dispatch）；
	// - bHasProcessedExports：true 表示本条目最初是 export-chunk 用以组装 exports，
	//   组装完后已 ProcessExportPayload 并把 Payload 清空准备装载真正的数据 payload；
	//   下一条 first-chunk（同样属于该 export-chunk 之后的数据 payload）会复用本条目。
	struct FRecvQueueEntry
	{
		// Returns true if this is a processed export payload
		bool GetIsProcessedExportPayload() const
		{
			return bHasProcessedExports;
		}

		// 4 字节对齐：方便位流读取器以字对齐方式读出。
		TArray<uint8, TAlignedHeapAllocator<4>> Payload;
		TUniquePtr<FReferencesForImport> References;
		uint32 RemainingByteCount = 0U;
		bool bHasProcessedExports = false;
	};

	// FDataChunk（接收端）：与 Writer 端对应的解码版本——拥有自己的 PartPayload 字节
	// 拷贝（因为从 bitstream 读出的字节需独立保存到达组装时机）。
	struct FDataChunk
	{
		FDataChunk();

		// 返回本分片实际承载的有效字节数（首片若 PartCount>1 则恒为 ChunkSize；
		// 否则按 PartByteCount。这是 Writer/Reader 共同遵守的协议）。
		const uint32 GetPartPayloadByteCount() const;
		// 反序列化：从 bitstream 读出 header + 字节，写入 PartPayload。
		void Deserialize(UE::Net::FNetSerializationContext& Context);

		// 本分片的字节缓冲（独立持有；无 64KB 上限因 ChunkSize=192）。
		TArray<uint8> PartPayload;
		uint32 PartCount;
		uint16 SequenceNumber;
		uint16 PartByteCount : 14;
		uint16 bIsFirstChunk : 1;
		uint16 bIsExportChunk : 1;
	};

public:

	// 构造：缓存 Init 参数，从 ReplicationSystem 中取得 ObjectReferenceCache /
	// NetTokenStore，并设好 ResolveContext（用于解析对象引用、async load）。
	FChunkedDataReader(const UDataStream::FInitParameters& InParams);
	// 析构：释放所有"为避免 GC 而临时持有的对象引用"。
	~FChunkedDataReader();

	// 从 Entry.Payload（即已组装好的 export-chunk 字节）解析出 exports：
	//   - 头部读 NumBitsForExportOffset 位的 ExportsOffset；
	//   - 若 != 0：先 Seek 到 exports 区，调 ObjectReferenceCache::ReadExports
	//     收集 must-be-mapped reference + async load priority；
	//   - 再调 FIrisPackageMapExportsUtil::Deserialize 把对象引用 quantize 数据存到
	//     Entry.References->QuantizedExports（dispatch 时再 dequantize 到 PackageMap）。
	bool ProcessExportPayload(FNetSerializationContext& Context, FRecvQueueEntry& Entry);

	// 从 DataChunksPendingAssembly 环首取连续 ExpectedSeq 的分片，逐分片把
	// PartPayload 追加到 ReceiveQueue 队尾条目；首片创建新 RecvQueueEntry。
	void AssemblePayloadsPendingAssembly(UE::Net::FNetSerializationContext& Context);

	// 在 dispatch 前驱动 must-be-mapped 引用解析；返回 true ⇒ 全部已解析；
	// false ⇒ 还有挂起项需要 async load，调用方应稍后再来。
	// 已解析的对象会通过 AddTrackedQueuedBatchObjectReference 临时保活避免 GC，
	// 离开本 reader 时再统一 Remove。
	bool TryResolveUnresolvedMustBeMappedReferences(TArray<FNetRefHandle>& MustBeMappedReferences, EIrisAsyncLoadingPriority IrisAsyncLoadingPriority);

	// 业务接口（被 UChunkedDataStream::DispatchReceivedPayload 转发）。
	UChunkedDataStream::EDispatchResult DispatchReceivedPayload(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction);
	UChunkedDataStream::EDispatchResult DispatchReceivedPayloads(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction);

	// 已组装完整、等待 dispatch 的 payload 个数。
	uint32 GetNumReceivedPayloadsPendingDispatch() const;

	// UDataStream::ReadData override（被 UChunkedDataStream 转发）：
	// 解析 packet 中的本流字段，归档分片，调 AssemblePayloadsPendingAssembly。
	void ReadData(UE::Net::FNetSerializationContext& Context);

	// 置错误态（可恢复性差，业务层一般直接关闭流）。
	void SetError(const FString& InErrorMessage);
	bool HasError() const;

	// 释放 ResolvedReferences 持有的临时引用计数（避免 GC 锁住的 UObject）。
	void ResetResolvedReferences();

public:

	friend class FChunkedDataStreamExportReadScope;

	// Incoming data
	// 接收端"按序号差索引"的环形分片缓冲：
	// 索引 0 槽位 = ExpectedSeq；索引 i = ExpectedSeq + i。
	// 分片到达即写入对应槽位（重复 → 自然覆盖；乱序 → 先放槽位等待前面到齐）。
	TRingBuffer<FDataChunk> DataChunksPendingAssembly;

	// Received data, ready to dispatch
	// 已组装好的 payload 队列（按业务入队顺序）。队首条目若 RemainingByteCount==0
	// 即可 dispatch；否则等组装完。
	TRingBuffer<FRecvQueueEntry> ReceiveQueue;

	// Next expected sequence number
	// 期望接收的下一个序号（11 位，按 SequenceBitMask 回绕）。
	// AssemblePayloadsPendingAssembly 每消费一个分片就 ++ExpectedSeq。
	uint16 ExpectedSeq = 0;

	// We have encountered and error, and should close the DataStream
	// 错误态标志（一旦置位不再重组分片，UChunkedDataStream::HasError 返回 true）。
	bool bHasError = false;

	// Cached on init
	FInitParameters InitParams;
	UReplicationSystem* ReplicationSystem = nullptr;
	FObjectReferenceCache* ObjectReferenceCache = nullptr;
	// 对象引用解析上下文（含 ConnectionId / RemoteNetTokenStoreState / AsyncLoadingPriority）。
	FNetObjectResolveContext ResolveContext;
	// NetToken 解析上下文（用于 PackageMap 反序列化字符串 token 等）。
	FNetTokenResolveContext NetTokenResolveContext;

	// Resolved references for which we have are holding on to references to avoid GC, must be released on exit
	// dispatch 期间为防 GC 把已解析对象临时纳入"批次引用追踪"——
	// reader 析构 / ResetResolvedReferences 时统一释放。
	TArray<FNetRefHandle, TInlineAllocator<4>> ResolvedReferences;

	// Exports
	// 当前 dispatch 用的 PackageMap exports（被 FChunkedDataStreamExportReadScope 绑定，
	// 业务侧通过 PackageMap 反序列化时就能从此处解析出 UObject*）。
	UE::Net::FIrisPackageMapExports PackageMapExports;

	// Maximum undispatched payload bytes, if this is overflown datastream will be put in error state and closed
	// 接收端"已收到但未 dispatch"字节上限（默认 10 MB）。
	// 业务侧可通过 UChunkedDataStream::SetMaxUndispatchedPayloadBytes 修改。
	uint64 MaxUndispatchedPayloadBytes = 10485760U;

	// Current number of received payload bytes ready to dispatch
	// 当前已收到（含组装中）但未 dispatch 的累计字节数。
	uint64 CurrentUndispatchedPayloadBytes = 0U;

	// Offset used when folding multiple exports payload processed after reading the same pacekt
	// NetTrace 折叠用：同一 packet 中可能解出多个 export payload，
	// 折叠到主流追踪 collector 时位置偏移需累加。
	uint32 MultiExportsPayLoadOffset = 0U;
};

} // End of namespace(s)
