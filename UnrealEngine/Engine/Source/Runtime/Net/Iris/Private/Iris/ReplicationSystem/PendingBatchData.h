// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// PendingBatchData.h —— Reader 端"等待引用解析 / async load 完成"的 batch 暂存
// -------------------------------------------------------------------------------------------------------------
// 模块定位：FReplicationReader 在收到一个对象 batch 时，可能因为以下原因无法立即处理：
//   ① batch 中含有"必须已映射"的 NetRefHandle（PendingMustBeMappedReferences）尚未解析；
//   ② Owner 自身需要被 async load（异步加载未完成）；
//   ③ Owner 的 CreationDependentParents 还未到位（CreationDependency 链上的 parent 没创建好）。
//
// 解决方案：把原始 bitstream 暂存到 FPendingBatchData，同时记录还差哪些引用 / parent；
//          之后每帧 FReplicationReader::UpdateUnresolvedMustBeMappedReferences 重新尝试处理。
//
// 另外为了避免 GC 把 ResolvedReferences 释放掉，本结构会持有 FNetRefHandle 强引用直到 batch 真正被解码。
//
// 与 FObjectReferenceCache 的协作：
//   - 当一个 PendingMustBeMappedReferences 中的 Handle 终于成功解析（异步加载完成或新对象创建），
//     ObjectReferenceCache 会通知 ReplicationReader 重试本批 batch。
//   - 多次重试如果仍卡住，会按 NextWarningTimeout 输出 warning（避免静默卡死）。
// =============================================================================================================

#pragma once

#include "Containers/Array.h"

#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/ObjectReferenceTypes.h"

#include "Templates/UniquePtr.h"

namespace UE::Net::Private
{

enum EReplicationDataStreamDebugFeatures : uint32;

}

namespace UE::Net::Private
{
// Queued data chunk
// 单个被排队的数据块（一个对象在 batch 中通常只有 1 个 chunk，但 EndReplication 等场景可能拆分多个）。
struct FQueuedDataChunk
{
	FQueuedDataChunk()
	: StorageOffset(0U)
	, NumBits(0U)
	, bHasBatchOwnerData(0U)
	, bIsEndReplicationChunk(0U)
	{
	}

	uint32 StorageOffset;                    // 在 FPendingBatchData::DataChunkStorage 中的起始 word offset。
	uint32 NumBits : 30;                     // 本 chunk 的有效比特数（bit 精度，最多 ~1G bits）。
	uint32 bHasBatchOwnerData : 1;           // 本 chunk 是否包含 Owner 的 creation header / state（与 SubObject chunk 的区分）。
	uint32 bIsEndReplicationChunk : 1;       // 本 chunk 是否是 EndReplication（销毁通知）；不需要 owner 创建即可处理。
	// 调试特性位（按 ini 中 ReplicationDataStreamDebugFeatures 配置启用 sentinel/checksum 等）。
	EReplicationDataStreamDebugFeatures StreamDebugFeatures = static_cast<EReplicationDataStreamDebugFeatures>(0U);
};

// Struct to contain storage and required data for queued batches pending must be mapped references
// 一个对象（Owner）的所有"待解决"信息：原始 bitstream + 阻塞依赖。
struct FPendingBatchData
{
	// We use a single array to store the actual data, it will grow if required.
	// 单一连续存储池：所有 chunks 的 bitstream 字（uint32 word）紧挨存放，避免每个 chunk 独立分配。
	TArray<uint32, TInlineAllocator<32>> DataChunkStorage;		
	// 每个 chunk 的元信息（offset/numBits 等）。
	TArray<FQueuedDataChunk, TInlineAllocator<4>> QueuedDataChunks;

	// The MustBeMapped references that are still not resolved
	// 阻塞引用：必须先解析（一般是异步加载或 ObjectReferenceCache 还没收到注册）才能解码 batch。
	TArray<FNetRefHandle, TInlineAllocator<4>> PendingMustBeMappedReferences;

	// Resolved references for which we have are holding on to references to avoid GC
	// 已解析但仍未 apply 的引用：持有强引用防止 GC（解码后会释放）。
	TArray<FNetRefHandle, TInlineAllocator<4>> ResolvedReferences;

	// Owner of the queued data chunks
	FNetRefHandle Owner; // 本 batch 所属对象的 NetRefHandle。

	// The list of parents that must exist before the owner can be created
	// CreationDependency：Owner 创建前必须就绪的 parent 列表（与 NetDependencyData::CreationDependencies 对应）。
	TArray<FNetRefHandle, TInlineAllocator<4>> CreationDependentParents;

	// Time when we started to accumulate data for this object
	// 累计数据起始时间（cycle）；用于"卡顿超阈值时输出警告"。
	uint64 PendingBatchStartCycles = 0;

	// At what time should we warn about being blocked too long
	// 下次允许输出"卡住太久"warning 的时间（避免每帧刷屏）。
	double NextWarningTimeout = 0.0;
	
	// Incremented every time we try to process queued batches, reset each time we output warning.
	// 重试次数计数器；每次输出 warning 后清零。
	int32 PendingBatchTryProcessCount = 0;

	// The async loading priority of the Owner
	// Owner 的异步加载优先级（来自 ObjectReplicationBridgeConfig）；用于 ObjectReferenceCache 调度异步加载。
	EIrisAsyncLoadingPriority IrisAsyncLoadingPriority = EIrisAsyncLoadingPriority::Invalid;
};

typedef TUniquePtr<FPendingBatchData> FPendingBatchDataPtr;

// 容器：FNetRefHandle(Owner) -> FPendingBatchData 的 Map（FReplicationReader 持有）。
struct FPendingBatchHolder
{
public:

	bool Contains(FNetRefHandle NetRefHandle) const
	{
		return PendingBatches.Contains(NetRefHandle);
	}

	FPendingBatchData* Find(FNetRefHandle NetRefHandle)
	{
		FPendingBatchDataPtr* Ptr = PendingBatches.Find(NetRefHandle);
		return Ptr ? Ptr->Get() : nullptr;
	}

	const FPendingBatchData* Find(FNetRefHandle NetRefHandle) const
	{
		const FPendingBatchDataPtr* Ptr = PendingBatches.Find(NetRefHandle);
		return Ptr ? Ptr->Get() : nullptr;
	}

	// 查或建：第一次某 Owner 被阻塞时通过 CreatePendingBatch 分配新槽。
	FPendingBatchData* FindOrCreate(FNetRefHandle NetRefHandle)
	{
		FPendingBatchDataPtr* Ptr = PendingBatches.Find(NetRefHandle);
		return Ptr ? Ptr->Get() : CreatePendingBatch(NetRefHandle);
	}

	void Remove(FNetRefHandle NetRefHandle)
	{
		PendingBatches.Remove(NetRefHandle);
	}
	
	bool IsEmpty() const
	{
		return PendingBatches.IsEmpty();
	}

	int32 Num() const
	{
		return PendingBatches.Num();
	}

	auto CreateConstIterator() const
	{
		return PendingBatches.CreateConstIterator();
	}

	void Empty()
	{
		PendingBatches.Empty();
	}

private:

	// 在 ReplicationReader.cpp 中实现（处理日志/统计等副作用）。
	FPendingBatchData* CreatePendingBatch(FNetRefHandle Owner);

	TMap<FNetRefHandle, FPendingBatchDataPtr> PendingBatches;
};

} // namespace UE::Net::Private

