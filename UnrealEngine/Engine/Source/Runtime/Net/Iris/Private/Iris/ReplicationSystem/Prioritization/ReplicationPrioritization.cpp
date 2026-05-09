// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：FReplicationPrioritization 实现 —— Iris Prioritization 子模块的核心调度器。
// 在 ReplicationSystem 帧循环（见 Iris_Architecture.md §4 / ReplicationSystem.md §6.2）中：
//   阶段 15：UpdatePrioritization（在 Filtering 与 DeltaCompression.PreSend 之间）。
//
// 设计取舍（详见本文件中段的大段英文注释）：
//   方案 A（当前）：每帧一次性遍历"标脏 + 需重发"对象，按 prioritizer 分桶 + 1024/批切片调用 Prioritize。
//                  优点：单次遍历，多 prioritizer 共享同一批；缺点：连接多 + 对象多时仍是 O(C * O)。
//   方案 B（未实现）：每连接独立请求 prioritize 列表（适合 spatial filter 显著差异化场景）。
//   方案 C（节流）：仅 200/帧，剩余对象沿用旧值——对世界刚加载/late join 友好。
//
// 数据结构关键点：
//   - PrioritizerInfos：按 ini 顺序排列的所有 prioritizer 实例；句柄 = 数组下标（uint8）。
//   - ObjectIndexToPrioritizer[ObjectIndex]：值 = PrioritizerInfos 下标；0xFF 表示走"静态默认优先级"。
//   - DefaultPriorities[ObjectIndex]：每对象的静态默认优先级（SetStaticPriority 写入；新增对象广播时使用）。
//   - ConnectionInfos[ConnId].Priorities[ObjectIndex]：实际累加的 per-(conn, obj) 优先级（被 ReplicationWriter 读后清零）。
//
// 内部 Helper：
//   - FPrioritizerBatchHelper：按 prioritizer 分桶 + 自动切片（每批 ≤ 1024）；
//   - FUpdateDirtyObjectsBatchHelper：把脏对象按 prioritizer 分桶传给 UpdateObjects（每批 ≤ 512）。
// =============================================================================================================================

#include "ReplicationPrioritization.h"
#include "Iris/IrisConstants.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"
#include "Net/Core/Misc/NetCVars.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizerDefinitions.h"
#include "Containers/ChunkedArray.h"
#include "Templates/Sorting.h"
#include "UObject/WeakObjectPtr.h"

namespace UE::Net::Private
{

// 编译期一致性保障：本文件用 0xFF 作为"静态默认优先级"的哨兵值，因此 InvalidNetObjectPrioritizerHandle 必须是全 1。
static_assert(InvalidNetObjectPrioritizerHandle == ~FNetObjectPrioritizerHandle(0), "ObjectIndexToPrioritizer code needs attention. Contact the UE Networking team.");
static constexpr uint8 FReplicationPrioritization_InvalidNetObjectPrioritizerIndex = 0xFF;

// 用户调用 GetPrioritizerHandle("DefaultPrioritizer") 时映射为 DefaultSpatialNetObjectPrioritizerHandle(=0)。
static const FName NAME_DefaultPrioritizer(TEXT("DefaultPrioritizer"));

/**
 * Most logic in here revolves around batches. As such we need access to the Chunks.
 */
// TChunkedArrayWithChunkManagement：扩展 TChunkedArray，增加按 chunk 头部弹出 / 单元素 push 等批处理 API。
// 用于 FPrioritizerBatchHelper 的 ObjectIndices 队列：
//   - Emplace_GetRef：单条 push（自动分配新 chunk）；
//   - PopChunkSafe：处理完一整 chunk（即一批 ≤1024）后整体释放 chunk；
//   - GetFirstChunkData/Num：取第一个 chunk 的指针与元素数（提供给 FNetObjectPrioritizationParams.ObjectIndices）。
template<class InElementType, uint32 BytesPerChunk>
class TChunkedArrayWithChunkManagement : public ::TChunkedArray<InElementType, BytesPerChunk>
{
private:
	using Super = ::TChunkedArray<InElementType, BytesPerChunk>;

public:
	/** Removes the first chunk and all elements in it, if it exists. */
	// 弹出第一个 chunk（FIFO 模式）。一批处理完后调用。
	void PopChunkSafe()
	{
		if (Super::NumElements > 0)
		{
			Super::NumElements -= FPlatformMath::Min(Super::NumElements, static_cast<int32>(Super::NumElementsPerChunk));

			constexpr int32 Index = 0;
			constexpr int32 Count = 1;
			Super::Chunks.RemoveAt(Index, Count, EAllowShrinking::No);
		}
	}

	/** Constructs a new element at the end of the array. Returns a reference to it. */
	InElementType& Emplace_GetRef()
	{
		// 当当前 chunk 已满（NumElements 是 NumElementsPerChunk 的倍数）时，分配新 chunk。
		if ((static_cast<uint32>(Super::NumElements) % static_cast<uint32>(Super::NumElementsPerChunk)) == 0U)
		{
			++Super::NumElements;
			typename Super::FChunk* Chunk = new typename Super::FChunk;
			Super::Chunks.Add(Chunk);
			return Chunk->Elements[0];
		}
		else
		{
			return this->operator[](Super::NumElements++);
		}
	}

	/** Returns the number of elements in first chunk. */
	int32 GetFirstChunkNum() const
	{
		return FPlatformMath::Min(Super::NumElements, static_cast<int32>(Super::NumElementsPerChunk));
	}

	/** Returns a pointer to the first element in the first chunk. */
	const InElementType* GetFirstChunkData() const
	{
		if (typename Super::FChunk const** ChunkPtr = Super::Chunks.GetData())
		{
			return &(*ChunkPtr)->Elements[0];
		}

		return nullptr;
	}

	/** Sets num elements to zero but keeps allocations. */
	// Reset：清空但保留分配（实际上 Reset(0) 是会释放所有 Chunks 的 — 名字略有歧义；下游每帧重新 Emplace 即可）。
	void Reset()
	{
		Super::Chunks.Reset(0);
		Super::NumElements = 0;
	}
};

// =============================================================================================================================
// FPrioritizerBatchHelper：把候选对象按 prioritizer 分桶并切片（每批 ≤ MaxObjectCountPerBatch=1024）。
// 工作模式（PrepareBatch 算法）：
//   - 每次调用一口气最多 sort 1024 个候选对象（来自 ObjectsRequiringPriorityUpdate）；
//   - 把每个对象 push 到对应 prioritizer 的 ObjectIndices chunk 队列；
//   - 同时把该对象的优先级 reset 为 DefaultPriority；
//   - 返回三种状态：
//     * ProcessAllBatchesAndStop：所有候选都处理完，调用方处理所有 prioritizer 全部 chunks 后退出循环；
//     * ProcessFullBatchesAndContinue：某 prioritizer 已积满 ≥1024 → 调用方先把已满的 prioritizer 处理一批再继续；
//     * NothingToProcess：异常（CurrentObjectIndex 越界）。
// 这种"积攒到满批再 dispatch"的模式让每个 Prioritize 调用都接近 1024 个对象，最大化 SIMD 收益。
// =============================================================================================================================
class FReplicationPrioritization::FPrioritizerBatchHelper
{
public:
	enum EConstants : unsigned
	{
		MaxObjectCountPerBatch = 1024U,
	};

	enum EBatchProcessStatus : unsigned
	{
		ProcessFullBatchesAndContinue,
		ProcessAllBatchesAndStop,
		NothingToProcess,
	};

	// 单 prioritizer 的"积攒桶"（FIFO chunk 队列；一个 chunk 恰好 1024 个 ObjectIndex = 4KB）。
	struct FPerPrioritizerInfo
	{
		TChunkedArrayWithChunkManagement<uint32, MaxObjectCountPerBatch*sizeof(uint32)> ObjectIndices;
	};

	explicit FPrioritizerBatchHelper(uint32 PrioritizerCount)
	{
		PerPrioritizerInfos.SetNum(PrioritizerCount);
	}

	// 每条连接处理开始前调用：清空 BatchInfo + 所有 prioritizer 桶（保留 chunks 内存）。
	void InitForConnection()
	{
		BatchInfo = FBatchInfo();

		for (FPerPrioritizerInfo& PerPrioritizerInfo : PerPrioritizerInfos)
		{
			PerPrioritizerInfo.ObjectIndices.Reset();
		}
	}

	EBatchProcessStatus PrepareBatch(FPerConnectionInfo& ConnInfo, const FNetBitArrayView Objects, const uint8* PrioritizerIndices, const float* InDefaultPriorities)
	{
		IRIS_PROFILER_SCOPE(FReplicationPrioritization_PrioritizeForConnection_PrepareBatch);

		// CurrentObjectIndex 是迭代游标，越界说明被错误重入。
		if (!ensure(BatchInfo.CurrentObjectIndex < Objects.GetNumBits()))
		{
			return EBatchProcessStatus::NothingToProcess;
		}

		float* ConnPriorities = ConnInfo.Priorities.GetData();
		FPerPrioritizerInfo* PerPrioritizerInfosData = PerPrioritizerInfos.GetData();

		// 临时缓冲：一次最多取 1024 个 ObjectIndex 出来，避免逐个 GetSetBit 的开销。
		uint32 ObjectIndices[MaxObjectCountPerBatch];
		/**
		 * Algorithm will get MaxBatchObjectCount dirty objects and sort them into the correct prioritizer.
		 * Return on the following conditions:
		 * - If a prioritizer has reached at least MaxBatchObjectCount
		 * - If all dirty objects have been sorted.
		 */
		{
			constexpr uint32 BitCount = ~0U;
			// 循环：每次最多取 1024 个，按 prioritizer 分桶，直到所有候选取完或某 prioritizer 满了。
			for (uint32 ObjectCount = 0; (ObjectCount = Objects.GetSetBitIndices(BatchInfo.CurrentObjectIndex, BitCount, ObjectIndices, MaxObjectCountPerBatch)) > 0; )
			{
				if (ObjectCount < MaxObjectCountPerBatch)
				{
					// This is so we can trigger an ensure if PrepareBatch() is called again.
					// 标记为已扫完（GetNumBits）—— 下次再调 PrepareBatch 会触发上方 ensure。
					BatchInfo.CurrentObjectIndex = Objects.GetNumBits();
				}
				else
				{
					// 还有更多——记录下一个起点。
					BatchInfo.CurrentObjectIndex =  ObjectIndices[ObjectCount - 1] + 1U;
				}

				for (const uint32 ObjectIndex : MakeArrayView(ObjectIndices, ObjectCount))
				{
					const uint8 PrioritizerIndex = PrioritizerIndices[ObjectIndex];
					if (PrioritizerIndex == FReplicationPrioritization_InvalidNetObjectPrioritizerIndex)
					{
						// 静态默认优先级路径：跳过（DefaultPriorities 已是当前值，无需重算）。
						continue;
					}

					FPerPrioritizerInfo& PerPrioritizerInfo = PerPrioritizerInfosData[PrioritizerIndex];
					PerPrioritizerInfo.ObjectIndices.Emplace_GetRef() = ObjectIndex;

					// Reset priority to default.
					// 关键：把候选对象的优先级先重置为 DefaultPriority。
					// 之后 prioritizer 用 max(默认, 计算值) 写回——保证多次调用之间不会无限累加。
					ConnPriorities[ObjectIndex] = InDefaultPriorities[ObjectIndex];
				}

				/**
				 * If we've processed all objects return ProcessAll.
				 * If any prioritizer has a full batch then return ProcessFull. This limits the memory footprint to a maximum of two chunks per prioritizer.
				 * Else continue.
				 */
				if ((ObjectCount < MaxObjectCountPerBatch) || (BatchInfo.CurrentObjectIndex == Objects.GetNumBits()))
				{
					// 全部候选都已分桶 → 调用方处理所有桶 → 退出循环。
					return EBatchProcessStatus::ProcessAllBatchesAndStop;
				}
				/**
				 * Is there a full batch for any prioritizer?
				 * We expect checking it after the fact to be a lot faster in typical scenarios versus checking after each object addition to a prioritizer.
				 */
				else
				{
					// 检查是否有 prioritizer 已积满 1024 → 立刻 dispatch，避免内存膨胀（单 prioritizer 最多 2 个 chunks）。
					for (const FPerPrioritizerInfo& PerPrioritizerInfo : PerPrioritizerInfos)
					{
						if (PerPrioritizerInfo.ObjectIndices.Num() >= MaxObjectCountPerBatch)
						{
							return EBatchProcessStatus::ProcessFullBatchesAndContinue;
						}
					}

					// Continue sorting objects by prioritizer.
				}
			}
		}

		// We've sorted all objects.
		return EBatchProcessStatus::ProcessAllBatchesAndStop;
	}

	TArray<FPerPrioritizerInfo, TInlineAllocator<16>> PerPrioritizerInfos;

private:
	struct FBatchInfo
	{
		uint32 CurrentObjectIndex = 0;     // GetSetBitIndices 的迭代游标。
	};

	FBatchInfo BatchInfo;
};

// =============================================================================================================================
// FUpdateDirtyObjectsBatchHelper：把每帧脏对象按 prioritizer 分桶（每批 ≤ 512）调用 UpdateObjects。
// 与 FPrioritizerBatchHelper 不同：
//   - 这里数据结构是固定大小数组（PrioritizerCount * 512 个 uint32），不用 ChunkedArray；
//   - 一次只处理 512 个脏对象，处理完即清空 ObjectCount=0；
// 用途：让 prioritizer 在打分前刷新缓存（如 Location prioritizer 重新读 WorldLocation）。
// =============================================================================================================================
class FReplicationPrioritization::FUpdateDirtyObjectsBatchHelper
{
public:
	enum Constants : uint32
	{
		MaxObjectCountPerBatch = 512U,
	};

	struct FPerPrioritizerInfo
	{
		uint32* ObjectIndices;                                         // 指向本 prioritizer 在 ObjectIndicesStorage 中的子区。
		uint32 ObjectCount;                                            // 当前已积攒数量（≤ 512）。

		FReplicationInstanceProtocol const** InstanceProtocols;        // 与 ObjectIndices 平行的 InstanceProtocol* 数组。
	};

	FUpdateDirtyObjectsBatchHelper(const FNetRefHandleManager* InNetRefHandleManager, uint32 PrioritizerCount)
	: NetRefHandleManager(InNetRefHandleManager)
	{
		// 一次性预分配 PrioritizerCount * 512 容量 —— 内存代价最多约 PrioritizerCount * (512*4 + 512*8) = 6KB/prioritizer。
		PerPrioritizerInfos.SetNumUninitialized(PrioritizerCount);
		ObjectIndicesStorage.SetNumUninitialized(PrioritizerCount*MaxObjectCountPerBatch);
		InstanceProtocolsStorage.SetNumUninitialized(PrioritizerCount*MaxObjectCountPerBatch);

		// 每个 PerPrioritizerInfo 指向 storage 的一段连续区域。
		uint32 PrioritizerIndex = 0;
		for (FPerPrioritizerInfo& PerPrioritizerInfo : PerPrioritizerInfos)
		{
			PerPrioritizerInfo.ObjectIndices = ObjectIndicesStorage.GetData() + PrioritizerIndex*MaxObjectCountPerBatch;
			PerPrioritizerInfo.InstanceProtocols = InstanceProtocolsStorage.GetData() + PrioritizerIndex*MaxObjectCountPerBatch;
			++PrioritizerIndex;
		}
	}

	void PrepareBatch(const uint32* ObjectIndices, uint32 ObjectCount, const uint8* PrioritizerIndices)
	{
		ResetBatch();

		FPerPrioritizerInfo* PerPrioritizerInfosData = PerPrioritizerInfos.GetData();
		for (const uint32 ObjectIndex : MakeArrayView(ObjectIndices, ObjectCount))
		{
			const uint8 PrioritizerIndex = PrioritizerIndices[ObjectIndex];
			if (PrioritizerIndex == FReplicationPrioritization_InvalidNetObjectPrioritizerIndex)
			{
				continue;
			}

			// 同时拉取 InstanceProtocol（只填非 null 的）—— prioritizer 的 UpdateObjects 必需这两组数据。
			if (const FReplicationInstanceProtocol* InstanceProtocol = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex).InstanceProtocol)
			{
				FPerPrioritizerInfo& PerPrioritizerInfo = PerPrioritizerInfosData[PrioritizerIndex];
				PerPrioritizerInfo.ObjectIndices[PerPrioritizerInfo.ObjectCount] = ObjectIndex;
				PerPrioritizerInfo.InstanceProtocols[PerPrioritizerInfo.ObjectCount] = InstanceProtocol;
				++PerPrioritizerInfo.ObjectCount;
			}
		}
	}

	TArray<FPerPrioritizerInfo, TInlineAllocator<16>> PerPrioritizerInfos;

private:
	void ResetBatch()
	{
		for (FPerPrioritizerInfo& PerPrioritizerInfo : PerPrioritizerInfos)
		{
			PerPrioritizerInfo.ObjectCount = 0U;
		}
	}

	TArray<uint32> ObjectIndicesStorage;
	TArray<const FReplicationInstanceProtocol*> InstanceProtocolsStorage;
	const FNetRefHandleManager* NetRefHandleManager;
};

FReplicationPrioritization::FReplicationPrioritization()
: HasNewObjectsWithStaticPriority(0)
{
}

void FReplicationPrioritization::Init(FReplicationPrioritizationInitParams& Params)
{
	check(Params.Connections != nullptr);
	check(Params.NetRefHandleManager != nullptr);

	ReplicationSystem = Params.ReplicationSystem;

	Connections = Params.Connections;
	NetRefHandleManager = Params.NetRefHandleManager;

	MaxInternalNetRefIndex = Params.MaxInternalNetRefIndex;

	// 预分配所有 per-object 数组（NetObjectPrioritizationInfos / ObjectIndexToPrioritizer / DefaultPriorities / ObjectsWithNewStaticPriority）。
	SetNetObjectListsSize(MaxInternalNetRefIndex);

	ConnectionInfos.Reserve(Params.Connections->GetMaxConnectionCount());

	// 读 ini → 创建所有 prioritizer 实例 → 调 Init。这是 prioritizer 的唯一构造点。
	InitPrioritizers();
}

void FReplicationPrioritization::Deinit()
{
	for (FPrioritizerInfo& Info : PrioritizerInfos)
	{
		Info.Prioritizer->Deinit();
		Info.Prioritizer = nullptr;
	}

	PrioritizerDefinitions = nullptr;

	ReplicationSystem = nullptr;
	Connections = nullptr;
	NetRefHandleManager = nullptr;

	NetObjectPrioritizationInfos.Empty();
	ObjectIndexToPrioritizer.Empty();
	DefaultPriorities.Empty();
	ObjectsWithNewStaticPriority.Empty();
}

void FReplicationPrioritization::SetNetObjectListsSize(FInternalNetRefIndex MaxInternalIndex)
{
	constexpr EAllowShrinking NoShrinking = EAllowShrinking::No;

	// $IRIS TODO: This can be quite wasteful in terms of memory assuming many objects will use a static priority. Need object pool!
	// 内存代价：MaxInternalIndex * 16B（PrioritizationInfos）。当大量对象用静态优先级时浪费——记录的优化方向。
	NetObjectPrioritizationInfos.SetNumUninitialized(MaxInternalIndex, NoShrinking);

	// Properly Initialize ObjectIndexToPrioritizer
	// 新增的 slot 必须填 0xFF（"未绑定 prioritizer"哨兵），否则会被错误当成索引 0 的 prioritizer。
	{
		const int32 PreviousSize = ObjectIndexToPrioritizer.Num();
		ObjectIndexToPrioritizer.SetNumUninitialized(MaxInternalIndex, NoShrinking);

		uint8* BufferData = ObjectIndexToPrioritizer.GetData();
		BufferData += PreviousSize;

		check((int32)MaxInternalIndex >= PreviousSize);
		const int32 UninitNum = (int32)MaxInternalIndex - PreviousSize;

		FMemory::Memset(BufferData, FReplicationPrioritization_InvalidNetObjectPrioritizerIndex, UninitNum * sizeof(decltype(ObjectIndexToPrioritizer)::ElementType));
	}

	// Property initialize DefaultPriorities
	// 新增 slot 填 DefaultPriority(=1.0)。
	ResizePrioritiesList(DefaultPriorities, MaxInternalIndex);

	ObjectsWithNewStaticPriority.SetNumBits(MaxInternalIndex);
}

void FReplicationPrioritization::ResizePrioritiesList(TArray<float>& OutPriorities, FInternalNetRefIndex MaxInternalIndex)
{
	const int32 PreviousSize = OutPriorities.Num();
	OutPriorities.SetNumUninitialized(MaxInternalIndex, EAllowShrinking::No);

	for (int32 PrioIndex = PreviousSize; PrioIndex < (int32)MaxInternalIndex; ++PrioIndex)
	{
		OutPriorities[PrioIndex] = DefaultPriority;
	}
}

void FReplicationPrioritization::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	MaxInternalNetRefIndex = NewMaxInternalIndex;

	// 1) 自身的 per-object 数组扩容；
	SetNetObjectListsSize(NewMaxInternalIndex);

	// 2) 通知所有 prioritizer 扩容它们的内部数据；
	for (FPrioritizerInfo& PrioritizerInfo : PrioritizerInfos)
	{
		PrioritizerInfo.Prioritizer->OnMaxInternalNetRefIndexIncreased(NewMaxInternalIndex);
	}

	// 3) 每个有效连接的 Priorities 数组扩容（无效连接保留空数组，以省内存）。
	for (FPerConnectionInfo& ConnectionInfo : ConnectionInfos)
	{
		if (ConnectionInfo.IsValid)
		{
			ResizePrioritiesList(ConnectionInfo.Priorities, NewMaxInternalIndex);
		}
	}
}

void FReplicationPrioritization::SetStaticPriority(uint32 ObjectIndex, float NewPrio)
{
	// 防御：负数优先级无意义。
	if (!ensureMsgf(NewPrio >= 0.0f, TEXT("Trying to set invalid priority %f"), NewPrio))
	{
		return;
	}

	uint8& Prioritizer = ObjectIndexToPrioritizer[ObjectIndex];
	float& Prio = DefaultPriorities[ObjectIndex];

	bool bPrioritizerDiffers = Prioritizer != FReplicationPrioritization_InvalidNetObjectPrioritizerIndex;
	bool bPrioDiffers = Prio != NewPrio;
	if (bPrioritizerDiffers || bPrioDiffers)
	{
		Prio = NewPrio;
		// 如果之前绑定了 prioritizer，先解绑：从 prioritizer 移除该对象 + 计数减一。
		if (bPrioritizerDiffers)
		{
			FPrioritizerInfo& PrioritizerInfo = PrioritizerInfos[Prioritizer];
			--PrioritizerInfo.ObjectCount;
			PrioritizerInfo.Prioritizer->RemoveObject(ObjectIndex, NetObjectPrioritizationInfos[ObjectIndex]);
			Prioritizer = FReplicationPrioritization_InvalidNetObjectPrioritizerIndex;
		}

		// 标记需要把新静态优先级广播到所有连接（在下一次 Prioritize 的 UpdatePrioritiesForNewAndDeletedObjects 阶段）。
		ObjectsWithNewStaticPriority.SetBit(ObjectIndex);
	}
}

bool FReplicationPrioritization::SetPrioritizer(uint32 ObjectIndex, FNetObjectPrioritizerHandle NewPrioritizer)
{
	if (!ensureMsgf(NewPrioritizer != InvalidNetObjectPrioritizerHandle, TEXT("%s"), TEXT("Call SetStaticPriority if you want to use a static priority for the object.")))
	{
		return false;
	}

	if (PrioritizerInfos.Num() == 0 || !ensureMsgf(NewPrioritizer < FNetObjectPrioritizerHandle(uint32(PrioritizerInfos.Num())), TEXT("Trying to set invalid prioritizer 0x%08x"), NewPrioritizer))
	{
		return false;
	}

	/**
	  * Not marking this object as in need of copying a new priority to each connection. 
	  * We keep the old priority value regardless of which prioritizer was previously used.
	  * That should work ok when we're throttling priority calculations. Let's just set the
	  * new default priority.
	  */
	// 切换 prioritizer 时不广播默认优先级——保持每连接的旧值，下一帧 prioritizer 会用 max 累加新值。
	DefaultPriorities[ObjectIndex] = 0.0f;

	// Unregister object from old prioritizer
	uint8& Prioritizer = ObjectIndexToPrioritizer[ObjectIndex];
	FNetObjectPrioritizationInfo& NetObjectPrioritizationInfo = NetObjectPrioritizationInfos[ObjectIndex];
	const bool bWasUsingStaticPriority = (Prioritizer == FReplicationPrioritization_InvalidNetObjectPrioritizerIndex);
	if (!bWasUsingStaticPriority)
	{
		FPrioritizerInfo& OldPrioritizerInfo = PrioritizerInfos[Prioritizer];
		--OldPrioritizerInfo.ObjectCount;
		OldPrioritizerInfo.Prioritizer->RemoveObject(ObjectIndex, NetObjectPrioritizationInfo);
	}

	// Register object with new prioritizer
	{
		const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);

		// 关键：调用 prioritizer.AddObject 之前必须把 16 字节槽清零（约定）。
		NetObjectPrioritizationInfo = FNetObjectPrioritizationInfo{};
		FNetObjectPrioritizerAddObjectParams AddParams = {NetObjectPrioritizationInfo, ObjectData.InstanceProtocol, ObjectData.Protocol, NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex)};
		FPrioritizerInfo& PrioritizerInfo = PrioritizerInfos[NewPrioritizer];
		if (PrioritizerInfo.Prioritizer->AddObject(ObjectIndex, AddParams))
		{
			Prioritizer = static_cast<uint8>(NewPrioritizer);
			++PrioritizerInfo.ObjectCount;
			return true;
		}

		// If we fail setting the new prioritizer we default to use default static priority.
		// AddObject 拒绝（如缺 RepTag_WorldLocation）→ 回退到静态默认优先级。
		UE_LOG(LogIris, Verbose, TEXT("Prioritizer '%s' does not support prioritizing object %u"), ToCStr(PrioritizerInfo.Prioritizer->GetFName().GetPlainNameString()), ObjectIndex);

		// If we were previously using static priority we don't have to do anything, otherwise force set default priority.
		if (!bWasUsingStaticPriority)
		{
			DefaultPriorities[ObjectIndex] = DefaultPriority;
			Prioritizer = FReplicationPrioritization_InvalidNetObjectPrioritizerIndex;
			ObjectsWithNewStaticPriority.SetBit(ObjectIndex);
		}
	}

	return false;
}

FNetObjectPrioritizerHandle FReplicationPrioritization::GetPrioritizerHandle(const FName PrioritizerName) const
{
	FNetObjectPrioritizerHandle Handle = 0;
	// Expect few prioritizers to be registered. Just do a linear search.
	// prioritizer 通常 <= 5 个，线性查找足够；句柄即数组下标。
	for (const FPrioritizerInfo& Info : PrioritizerInfos)
	{
		if (Info.Name == PrioritizerName)
		{
			return Handle;
		}

		++Handle;
	}

	// 特殊别名："DefaultPrioritizer" → 默认空间 prioritizer 句柄（=0）。
	if (PrioritizerName == UE::Net::Private::NAME_DefaultPrioritizer)
	{
		return DefaultSpatialNetObjectPrioritizerHandle;
	}

	return InvalidNetObjectPrioritizerHandle;
}

UNetObjectPrioritizer* FReplicationPrioritization::GetPrioritizer(const FName PrioritizerName) const
{
	// Expect few prioritizers to be registered. Just do a linear search.
	for (const FPrioritizerInfo& Info : PrioritizerInfos)
	{
		if (Info.Name == PrioritizerName)
		{
			return Info.Prioritizer.Get();
		}
	}

	return nullptr;
}

/**
  * There are many different ways to determine which objects should be prioritized. They have different performance
  * characteristics. Here are some ways:
  *
  * - Prioritize all objects that have been marked as dirty this frame along with each connection's wishes due to packet loss.
  *   packet loss should be rare and the list of objects with lost state should be small. This way works well if the number of
  *   dirty objects is relatively small. This should save a lot of time on the setup of the prioritization data. This can be coupled
  *   with having lost objects get a priority bump every frame until it's been resent. The latter would mean that there's no extra per
  *   connection setup cost at all other than filling in the right pointer to the priority data. A similar approach can also be taken
  *   with throttling of how often connections are replicated to. For example if a third of all connections are considered each net
  *   tick then one would maintain three different dirty object structures.
  *
  * - Get the list of objects to prioritize from each connection. This can make sense if there are many connections and they have 
  *   very different sets of scoped objects, for example if spatial filtering is applied and players are far away from eachother. 
  *   Prioritizing all connections' relevant objects would then cause ConnectionCount*PerConnectionObjectCount to be prioritized
  *   per connection which could be very expensive.
  *
  * If performance issues arise in certain scenarios like initial world spawn or late joing here are some additional thoughts.
  *
  *  During world spawn or late join it's likely a connection wants to replicate many objects. This can stress this system
  *  if there are a lot of objects using dynamic prioritization. Perhaps having the ReplicationDataStream in a special state 
  *  where many (or special) objects are replicated first before starting prioritizing dirty objects as well is something to consider.
  *  One could also just consider a smaller amount of objects (say 200) each frame and continue from the last object index
  *  prioritized the next time we get here. This works not only at initial replication but also if there is often a very large
  *  amount of dynamically prioritized objects. The connection will get the last priority value for the objects that aren't
  *  prioritized this frame. A very simple yet effective optimization! There can of course be artifacts such as an object
  *  receiving a low priority and then the player rotates quickly so that object is in the line of sight and it doesn't get replicated
  *  for a few fames until it receives the high priority it should have had. Perhaps one could mark either certain objects as not
  *  being subject to priority calculation throttling or mark a prioritizer as not being subject to throttling, in which case
  *  all dirty objects with such a prioritizer would always get an updated priority.
  */
// 上方是 Epic 内部的设计 memo（保留原文）：讨论 prioritization 的多种实现策略与未来优化方向。

/**
  * Prioritize objects as per connection's wishes.
  */
// =============================================================================================================================
// 主入口：每帧由 FReplicationSystemImpl::UpdatePrioritization 阶段调用一次。
// 流程：
//   1) UpdatePrioritiesForNewAndDeletedObjects：处理本帧增/删/静态优先级变更对象的广播；
//   2) NotifyPrioritizersOfDirtyObjects：让每个 prioritizer 刷新缓存（如 Location）；
//   3) 提前返回：若无任何复制连接（dedicated server 启动 / 全员断线）；
//   4) PrePrioritize：所有有对象的 prioritizer 一次性调 PrePrioritize；
//   5) 对每个有视图的连接：
//      - 取该连接的 ObjectsRequiringPriorityUpdate（来自 ReplicationWriter，含本帧脏 + 需重发 + 新建对象）；
//      - PrioritizeForConnection 内部按 prioritizer 分桶切片调 Prioritize；
//      - 把 ConnectionInfos[ConnId].Priorities 数组指针交给 ReplicationWriter::UpdatePriorities；
//   6) PostPrioritize；
// =============================================================================================================================
void FReplicationPrioritization::Prioritize(const FNetBitArrayView& ReplicatingConnections, const FNetBitArrayView& DirtyObjectsThisFrame)
{
	// 阶段 1+2：与连接无关的全局更新。
	UpdatePrioritiesForNewAndDeletedObjects();
	NotifyPrioritizersOfDirtyObjects(DirtyObjectsThisFrame);

	// 没有任何连接需要发送 → 不必跑后面的 prioritizer 计算。
	if (!ReplicatingConnections.IsAnyBitSet())
	{
		return;
	}

	// Give prioritizers a chance to prepare for prioritization. It's only called if there's a chance Prioritize() will be called.
	// 阶段 4：PrePrioritize 一次性。仅对有对象的 prioritizer 调用——无对象也调没意义且浪费。
	{
		FNetObjectPrePrioritizationParams PrePrioParams;
		for (FPrioritizerInfo& Info : PrioritizerInfos)
		{
			if (Info.ObjectCount == 0U)
			{
				continue;
			}
			
			Info.Prioritizer->PrePrioritize(PrePrioParams);
		}
	}
	

	// 把 ReplicatingConnections 位图展开成连接 ID 数组（栈分配，避免堆开销）。
	uint32* ConnectionIds = static_cast<uint32*>(FMemory_Alloca(ReplicatingConnections.GetNumBits()*4));
	uint32 ReplicatingConnectionCount = 0;
	ReplicatingConnections.ForAllSetBits([ConnectionIds, &ReplicatingConnectionCount](uint32 Bit) { ConnectionIds[ReplicatingConnectionCount++] = Bit; });

	// 一个 BatchHelper 可被所有连接复用（只是 chunks 会被 Reset，分配保留）。
	FPrioritizerBatchHelper BatchHelper(PrioritizerInfos.Num());

	for (const uint32 ConnId : MakeArrayView(ConnectionIds, ReplicatingConnectionCount))
	{
		// If there's no view we do not prioritize
		// 无视图（如 dedicated server / 仅监听连接）→ 跳过该连接。Sphere/FOV 都依赖 View。
		if (Connections->GetReplicationView(ConnId).Views.Num() <= 0)
		{
			continue;
		}

		FReplicationConnection* Connection = Connections->GetConnection(ConnId);
		FReplicationWriter* ReplicationWriter = Connection->ReplicationWriter;
		// ObjectsRequiringPriorityUpdate：Writer 维护的位图，含本帧脏 + 需重发 + 新建对象（Writer.UpdateScope 阶段构建）。
		const FNetBitArray& Objects = ReplicationWriter->GetObjectsRequiringPriorityUpdate();
		if (Objects.GetNumBits() == 0)
		{
			continue;
		}

		PrioritizeForConnection(ConnId, BatchHelper, MakeNetBitArrayView(Objects));

		// Pass updated priorities to the ReplicationWriter. Currently it is assumed priorities are stored persistently per object per connection.
		// 把整个 priorities 数组指针交给 Writer——Writer 在 Write 阶段读取并据此挑选发送顺序。
		// 注意：这是"指针交付"，不是拷贝；Writer 不能在下一帧 Prioritize 之前读旧值（约定上不会跨帧）。
		{
			FPerConnectionInfo& ConnInfo = ConnectionInfos[ConnId];
			const float* Priorities = ConnInfo.Priorities.GetData();
			ReplicationWriter->UpdatePriorities(Priorities);
		}
	}

	// Give prioritizers a chance to cleanup after prioritization. It's called if PrePrioritize() was called.
	// 阶段 6：PostPrioritize（与 PrePrioritize 对称）。
	{
		FNetObjectPostPrioritizationParams PostPrioParams;
		for (FPrioritizerInfo& Info : PrioritizerInfos)
		{
			if (Info.ObjectCount == 0U)
			{
				continue;
			}

			Info.Prioritizer->PostPrioritize(PostPrioParams);
		}
	}
}

void FReplicationPrioritization::AddConnection(uint32 ConnectionId)
{
	// 按 ConnectionId 索引扩容（只升不降）。
	if (ConnectionId >= (uint32)ConnectionInfos.Num())
	{
		ConnectionInfos.SetNum(ConnectionId + 1U, EAllowShrinking::No);
	}

	++ConnectionCount;
	FPerConnectionInfo& ConnectionInfo = ConnectionInfos[ConnectionId];
	// 关键：新连接的初始 Priorities 数组 = DefaultPriorities 的拷贝。
	// 这样新连接对所有已存在对象的优先级"持平"于其他连接，避免上线瞬间被旧对象洪水。
	ConnectionInfo.Priorities = DefaultPriorities;
	ConnectionInfo.NextObjectIndexToProcess = 0;
	ConnectionInfo.IsValid = 1;

	// 通知所有 prioritizer 分配 per-connection 状态（如 NetObjectCountLimiter 的 LastConsiderFrames）。
	for (FPrioritizerInfo& Info : PrioritizerInfos)
	{
		Info.Prioritizer->AddConnection(ConnectionId);
	}
}

void FReplicationPrioritization::RemoveConnection(uint32 ConnectionId)
{
	checkSlow(ConnectionId < (uint32)ConnectionInfos.Num());

	--ConnectionCount;
	FPerConnectionInfo& ConnectionInfo = ConnectionInfos[ConnectionId];
	ConnectionInfo.IsValid = 0;
	ConnectionInfo.Priorities.Empty();

	for (FPrioritizerInfo& Info : PrioritizerInfos)
	{
		Info.Prioritizer->RemoveConnection(ConnectionId);
	}
}

/**
  * For new objects we copy the most recently set priority to each connection.
  * For deleted objects we reset the priority to default. Propagation of this priority to each connection is unnecessary
  * as we propagate the priority, which may have changed from the default, when the object is detected as new.
  */
// 处理本帧增 / 删 / 静态优先级变更的对象：
//   - 删除对象：解绑 prioritizer + 重置 DefaultPriorities[i] = 1.0；不广播到连接（被删的对象不会再被发送）。
//   - 新增对象（仅当存在连接）：把 DefaultPriorities[i] 拷贝到所有连接的 Priorities[i]——避免新对象在某些连接上保留陈旧值。
//   - 静态优先级变更对象（ObjectsWithNewStaticPriority）：与"新增对象"走同一广播路径，确保所有连接看到最新静态值。
void FReplicationPrioritization::UpdatePrioritiesForNewAndDeletedObjects()
{
	IRIS_PROFILER_SCOPE(FReplicationPrioritization_UpdatePrioritiesForNewAndDeletedObjects);

	// 通过对比"上一帧 scope"与"本帧 scope"两个位图找出 added / removed 集合。
	const FNetBitArrayView PrevScopedIndices = NetRefHandleManager->GetPrevFrameScopableInternalIndices();
	const FNetBitArrayView ScopedIndices = NetRefHandleManager->GetCurrentFrameScopableInternalIndices();

	// 删除对象处理（共用闭包）。
	auto ForEachRemovedObject = [this](uint32 ObjectIndex)
	{
		uint8& Prioritizer = ObjectIndexToPrioritizer[ObjectIndex];
		if (Prioritizer != FReplicationPrioritization_InvalidNetObjectPrioritizerIndex)
		{
			FPrioritizerInfo& OldPrioritizerInfo = PrioritizerInfos[Prioritizer];
			--OldPrioritizerInfo.ObjectCount;
			OldPrioritizerInfo.Prioritizer->RemoveObject(ObjectIndex, NetObjectPrioritizationInfos[ObjectIndex]);
		}

		Prioritizer = FReplicationPrioritization_InvalidNetObjectPrioritizerIndex;
		DefaultPriorities[ObjectIndex] = DefaultPriority;
	};

	// 新增对象处理：只有存在连接时才需要拷贝优先级到连接（无连接时省去 O(NewIndices) 工作）。
	TArray<uint32> NewIndices;
	TFunction<void(uint32)> DoNothing = [](uint32 ObjectIndex){};
	TFunction<void(uint32)> AddIndexAndClearFromNewPriority = [&NewIndices, this](uint32 ObjectIndex)
	{
		NewIndices.Add(ObjectIndex);
		// Prevent the same index from being added twice
		// 同时清除"新静态优先级"标志：如果该对象既是新对象又被设过 SetStaticPriority，避免下面再 push 一次。
		this->ObjectsWithNewStaticPriority.ClearBit(ObjectIndex);
	};

	TFunction<void(uint32)> ForEachNewObject = (ConnectionCount > 0 ? AddIndexAndClearFromNewPriority : DoNothing);
	if (ConnectionCount > 0)
	{
		// 经验值：单帧新增 1024 对象算大量；预分配减少后续 Add 的 realloc。
		NewIndices.Reserve(FMath::Min(1024U, MaxInternalNetRefIndex));
	}

	// 一次遍历 ScopedIndices ⊕ PrevScopedIndices，分别派给"新增"与"删除"回调。
	FNetBitArrayView::ForAllExclusiveBits(ScopedIndices, PrevScopedIndices, ForEachNewObject, ForEachRemovedObject);

	// 再次合并：本帧 SetStaticPriority 改过的对象（若不是新对象）也需要广播。
	if (HasNewObjectsWithStaticPriority)
	{
		HasNewObjectsWithStaticPriority = 0;

		if (ConnectionCount > 0)
		{
			ObjectsWithNewStaticPriority.ForAllSetBits([this, &NewIndices](uint32 ObjectIndex)
			{ 
				NewIndices.Add(ObjectIndex); 
			});
		}

		// $IRIS TODO: Want ForAllSetBits with clear.
		// TODO 优化：希望 FNetBitArray 提供 ForAllSetBitsAndClear 一次完成（当前是先遍历再清空）。
		ObjectsWithNewStaticPriority.ClearAllBits();
	}

	// Copy the priorities for the new objects to each connection
	// 把新对象 / 静态优先级变更对象的优先级广播到所有有效连接的 Priorities 数组。
	if (NewIndices.Num() > 0 && ConnectionCount > 0)
	{
		const TArrayView<uint32> ObjectIndices = MakeArrayView(NewIndices);
		for (FPerConnectionInfo& ConnectionInfo : ConnectionInfos)
		{
			if (!ConnectionInfo.IsValid)
			{
				continue;
			}

			for (const uint32 ObjectIndex : ObjectIndices)
			{
				ConnectionInfo.Priorities[ObjectIndex] = DefaultPriorities[ObjectIndex];
			}
		}
	}
}

/**
 * 1. Sort indices by prioritizer first, index second. Try to keep static priority objects last.
 * 2. Loop through index list until new prioritizer is found and pass the info to the prioritizer for processing.
 */
// 单连接打分：用 BatchHelper 把对象按 prioritizer 分桶切片，逐桶 dispatch；末尾可选高优先级覆盖视图目标。
void FReplicationPrioritization::PrioritizeForConnection(uint32 ConnId, FPrioritizerBatchHelper& BatchHelper, const FNetBitArrayView Objects)
{
	IRIS_PROFILER_SCOPE(FReplicationPrioritization_PrioritizeForConnection);

	FPerConnectionInfo& ConnInfo = ConnectionInfos[ConnId];

	FNetObjectPrioritizationParams PrioParameters;
	// Setup static part of the prio parameters.
	// 这些字段在所有 dispatch 调用之间保持不变。
	{
		PrioParameters.Priorities = ConnInfo.Priorities.GetData();
		PrioParameters.PrioritizationInfos = NetObjectPrioritizationInfos.GetData();
		PrioParameters.ConnectionId = ConnId;
		PrioParameters.View = Connections->GetReplicationView(ConnId);
	}

	/**
	  * Split objects per prioritizer and copy priorities for the objects to the connection specific priority array.
	  * The latter allows throttling of priority calculations by keeping the latest priority until a new one is calculated.
	  */
	// 主循环：BatchHelper.PrepareBatch 一次最多积攒 1024 个候选；返回三种状态决定下一步：
	//   - ProcessAllBatchesAndStop：全部候选都已分桶完毕——把每个 prioritizer 的所有 chunks 全部 Prioritize 并 break；
	//   - ProcessFullBatchesAndContinue：某 prioritizer 已积满——只 dispatch 满批，回到 PrepareBatch 继续；
	//   - 其它（NothingToProcess）：异常——也退出循环。
	BatchHelper.InitForConnection();
	while (true)
	{
		const FPrioritizerBatchHelper::EBatchProcessStatus ProcessStatus = BatchHelper.PrepareBatch(ConnInfo, Objects, ObjectIndexToPrioritizer.GetData(), DefaultPriorities.GetData());
		if (ProcessStatus == FPrioritizerBatchHelper::EBatchProcessStatus::ProcessAllBatchesAndStop)
		{
			// 处理所有 prioritizer 的所有 chunks（FIFO 弹出）。
			for (FPrioritizerBatchHelper::FPerPrioritizerInfo& PerPrioritizerInfo : BatchHelper.PerPrioritizerInfos)
			{
				for (int32 ObjectCount = PerPrioritizerInfo.ObjectIndices.Num(); ObjectCount > 0; ObjectCount = PerPrioritizerInfo.ObjectIndices.Num())
				{
					PrioParameters.ObjectIndices = PerPrioritizerInfo.ObjectIndices.GetFirstChunkData();
					PrioParameters.ObjectCount = PerPrioritizerInfo.ObjectIndices.GetFirstChunkNum();

					// 通过指针偏移反推 PrioritizerIndex（PerPrioritizerInfos 是数组，一一对应 PrioritizerInfos）。
					const int32 PrioritizerIndex = static_cast<int32>(&PerPrioritizerInfo - BatchHelper.PerPrioritizerInfos.GetData());
					UNetObjectPrioritizer* Prioritizer = PrioritizerInfos[PrioritizerIndex].Prioritizer.Get();
					Prioritizer->Prioritize(PrioParameters);

					// 弹出已处理的 chunk，下一轮迭代取下一个 chunk。
					PerPrioritizerInfo.ObjectIndices.PopChunkSafe();
				}
			}
			break;
		}
		else if (ProcessStatus == FPrioritizerBatchHelper::EBatchProcessStatus::ProcessFullBatchesAndContinue)
		{
			// 只处理已满批（≥ 1024）的 prioritizer，未满批留到下轮。
			for (FPrioritizerBatchHelper::FPerPrioritizerInfo& PerPrioritizerInfo : BatchHelper.PerPrioritizerInfos)
			{
				if (PerPrioritizerInfo.ObjectIndices.Num() < FPrioritizerBatchHelper::MaxObjectCountPerBatch)
				{
					continue;
				}

				PrioParameters.ObjectIndices = PerPrioritizerInfo.ObjectIndices.GetFirstChunkData();
				PrioParameters.ObjectCount = PerPrioritizerInfo.ObjectIndices.GetFirstChunkNum();

				const int32 PrioritizerIndex = static_cast<int32>(&PerPrioritizerInfo - BatchHelper.PerPrioritizerInfos.GetData());
				UNetObjectPrioritizer* Prioritizer = PrioritizerInfos[PrioritizerIndex].Prioritizer.Get();
				Prioritizer->Prioritize(PrioParameters);

				PerPrioritizerInfo.ObjectIndices.PopChunkSafe();
			}

			continue;
		}

		// We're done
		break;
	}

	// Optionally force very high priority on view targets
	// CVar net.Iris.ForceConnectionViewerPriority > 0 时：把 Controller / ViewTarget 的优先级强制设为 1e7，
	// 保证视图主体（玩家自己）始终在每帧第一时间被发送，避免输入响应延迟。
	if (CVar_ForceConnectionViewerPriority > 0)
	{
		SetHighPriorityOnViewTargets(MakeArrayView(ConnInfo.Priorities), PrioParameters.View);
	}
}

void FReplicationPrioritization::SetHighPriorityOnViewTargets(const TArrayView<float>& Priorities, const FReplicationView& ReplicationView)
{
	using namespace UE::Net::Private;

	// We allow a view target to appear multiple times. It will get the same priority regardless.
	// 同一对象出现多次也无所谓——重复赋同一值，最终结果一致。
	TArray<FNetHandle, TInlineAllocator<16>> ViewTargets;
	for (const FReplicationView::FView& View : ReplicationView.Views)
	{
		if (View.Controller.IsValid())
		{
			ViewTargets.Add(View.Controller);
		}
		// ViewTarget 不等于 Controller 时也加入（自由视角 / Spectator / SpawnedPawn 等场景）。
		if (View.ViewTarget != View.Controller && View.ViewTarget.IsValid())
		{
			ViewTargets.Add(View.ViewTarget);
		}
	}

	for (FNetHandle NetHandle : ViewTargets)
	{
		const FInternalNetRefIndex ViewTargetInternalIndex = NetRefHandleManager->GetInternalIndexFromNetHandle(NetHandle);
		if (ViewTargetInternalIndex != FNetRefHandleManager::InvalidInternalIndex)
		{
			Priorities[ViewTargetInternalIndex] = ViewTargetHighPriority;
		}
	}
}

/**
 * Notify prioritizers of which objects have been updated since last frame.	 
 */
// 把全帧脏对象按 prioritizer 分桶切片调用 UpdateObjects（让 prioritizer 刷新缓存）。
// 与 PrioritizeForConnection 相比：
//   - 这里是"全局一次"，不针对某连接；
//   - 切片大小 512（更小，因为 UpdateObjects 通常涉及读 ExternalSrcBuffer，缓存不友好）。
void FReplicationPrioritization::NotifyPrioritizersOfDirtyObjects(const FNetBitArrayView& DirtyObjectsThisFrame)
{
	IRIS_PROFILER_SCOPE(FReplicationPrioritization_NotifyPrioritizersOfDirtyObjects);

	FUpdateDirtyObjectsBatchHelper BatchHelper(NetRefHandleManager, PrioritizerInfos.Num());

	constexpr SIZE_T MaxBatchObjectCount = FUpdateDirtyObjectsBatchHelper::Constants::MaxObjectCountPerBatch;
	uint32 ObjectIndices[MaxBatchObjectCount];

	// 按 512 一批扫过整个脏位图。
	const uint32 BitCount = ~0U;
	for (uint32 ObjectCount, StartIndex = 0; (ObjectCount = DirtyObjectsThisFrame.GetSetBitIndices(StartIndex, BitCount, ObjectIndices, MaxBatchObjectCount)) > 0; )
	{
		BatchNotifyPrioritizersOfDirtyObjects(BatchHelper, ObjectIndices, ObjectCount);

		StartIndex = ObjectIndices[ObjectCount - 1] + 1U;
		// 终止条件：本批不满（位图扫到尾） or 已抵达位图末尾。
		if ((StartIndex == DirtyObjectsThisFrame.GetNumBits()) | (ObjectCount < MaxBatchObjectCount))
		{
			break;
		}
	}
}

void FReplicationPrioritization::BatchNotifyPrioritizersOfDirtyObjects(FUpdateDirtyObjectsBatchHelper& BatchHelper, uint32* ObjectIndices, uint32 ObjectCount)
{
	BatchHelper.PrepareBatch(ObjectIndices, ObjectCount, ObjectIndexToPrioritizer.GetData());

	FNetObjectPrioritizerUpdateParams UpdateParameters;
	UpdateParameters.StateBuffers = &NetRefHandleManager->GetReplicatedObjectStateBuffers();
	UpdateParameters.PrioritizationInfos = NetObjectPrioritizationInfos.GetData();

	// 对每个非空 prioritizer 桶调 UpdateObjects。
	for (const FUpdateDirtyObjectsBatchHelper::FPerPrioritizerInfo& PerPrioritizerInfo : BatchHelper.PerPrioritizerInfos)
	{
		if (PerPrioritizerInfo.ObjectCount == 0)
		{
			continue;
		}

		UpdateParameters.ObjectIndices = PerPrioritizerInfo.ObjectIndices;
		UpdateParameters.ObjectCount = PerPrioritizerInfo.ObjectCount;
		UpdateParameters.InstanceProtocols = PerPrioritizerInfo.InstanceProtocols;

		const int32 PrioritizerIndex = static_cast<int32>(&PerPrioritizerInfo - BatchHelper.PerPrioritizerInfos.GetData());
		UNetObjectPrioritizer* Prioritizer = PrioritizerInfos[PrioritizerIndex].Prioritizer.Get();
		Prioritizer->UpdateObjects(UpdateParameters);
	}
}

void FReplicationPrioritization::InitPrioritizers()
{
	/**
	  * $IRIS TODO: Figure out what kind of hotfixing support we need. There are different trade-offs
	  * depending how we set prioritizer on an object and how the user decides to cache or not cache
	  * prioritizer handles. Not having handles and always set by name doesn't really solve much
	  * as the SetPrioritizer call would just return false anyway if the prioritizer does not exist.
	  * However we currently do not invalidate handles, which is perhaps a good thing as it allows
	  * switching prioritizers behind the scenes.
	  * As for hotfixing prioritizer configs that's up to the implementor of the prioritizer.
	  */
	// 创建 ini 配置对象（NewObject 会触发 PostInitProperties → LoadDefinitions → StaticLoadClass）。
	PrioritizerDefinitions = TStrongObjectPtr<UNetObjectPrioritizerDefinitions>(NewObject<UNetObjectPrioritizerDefinitions>());
	TArray<FNetObjectPrioritizerDefinition> Definitions;
	PrioritizerDefinitions->GetValidDefinitions(Definitions);

	// We store a uint8 per object to prioritizer.
	// 关键限制：句柄被 uint8 存储 → 最多 256 个 prioritizer（0xFF 是哨兵 → 实际上限 255）。
	check(Definitions.Num() <= 256);

	PrioritizerInfos.Reserve(Definitions.Num());
	for (FNetObjectPrioritizerDefinition& Definition : Definitions)
	{
		// NewObject 创建实例：MakeUniqueObjectName 保证名字唯一（同 Class 多实例需要）。
		TStrongObjectPtr<UNetObjectPrioritizer> Prioritizer(NewObject<UNetObjectPrioritizer>((UObject*)GetTransientPackage(), Definition.Class, MakeUniqueObjectName(nullptr, Definition.Class, Definition.PrioritizerName)));

		FNetObjectPrioritizerInitParams InitParams;
		InitParams.ReplicationSystem = ReplicationSystem;
		// Config 也走 NewObject（不是用 CDO，让每实例可独立修改 Config 字段）。
		InitParams.Config = (Definition.ConfigClass != nullptr ? NewObject<UNetObjectPrioritizerConfig>((UObject*)GetTransientPackage(), Definition.ConfigClass) : nullptr);
		InitParams.AbsoluteMaxNetObjectCount = NetRefHandleManager->GetMaxActiveObjectCount();
		InitParams.CurrentMaxInternalIndex = MaxInternalNetRefIndex;
		InitParams.MaxConnectionCount = Connections->GetMaxConnectionCount();

		Prioritizer->Init(InitParams);

		FPrioritizerInfo& Info = PrioritizerInfos.Emplace_GetRef();
		Info.Prioritizer = Prioritizer;
		Info.Name = Definition.PrioritizerName;
		Info.ObjectCount = 0;
	}

#if UE_GAME || UE_SERVER
	// 实战警告：服务端 / 独立游戏在没注册任何 prioritizer 时，所有对象都使用 DefaultPriority(=1.0)。
	// 远近对象同优先级 → 玩家附近的关键 Actor 不会被优先发送 → 体验会很差。
	UE_CLOG(PrioritizerInfos.Num() == 0, LogIris, Warning, TEXT("%s"), TEXT("No prioritizers have been registered. This may result in a bad gameplay experience because nearby actors will not have higher priority than actors far away."));
#endif
}

}
