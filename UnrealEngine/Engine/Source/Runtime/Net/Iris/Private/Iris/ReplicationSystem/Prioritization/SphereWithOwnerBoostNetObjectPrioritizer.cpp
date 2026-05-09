// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：USphereWithOwnerBoostNetObjectPrioritizer 实现 —— 在球体打分基础上为 owning connection 加成。
// 流水线（与基类相同 1024/批 SIMD 节奏）：
//   1) PrepareBatch：拷贝优先级/位置 +  顺路收集"本连接拥有"的本地索引；
//   2) PrioritizeBatch：调基类做球体打分；
//   3) BoostOwningConnectionPriorities：对收集到的本地索引做 += OwnerPriorityBoost；
//   4) FinishBatch：写回到全局 priorities 数组。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Prioritization/SphereWithOwnerBoostNetObjectPrioritizer.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFiltering.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Misc/MemStack.h"
#include <limits>

#include UE_INLINE_GENERATED_CPP_BY_NAME(SphereWithOwnerBoostNetObjectPrioritizer)

void USphereWithOwnerBoostNetObjectPrioritizer::Init(FNetObjectPrioritizerInitParams& Params)
{
	// Make sure our ConnectionId type is sufficient.
	// 我们用 uint16 存 ConnectionId，所以 MaxConnectionCount 不能 ≥ 65535。
	checkf(Params.MaxConnectionCount < std::numeric_limits<ConnectionId>::max(), TEXT("Need to increase size of ConnectionId type."));

	// We have no need for a special init, but need to make sure the config is of the expected type.
	// 仅用于类型检查；具体 Config 由基类 USphereNetObjectPrioritizer::Init 实际持有。
	check(CastChecked<USphereWithOwnerBoostNetObjectPrioritizerConfig>(Params.Config));
	Super::Init(Params);

	ReplicationSystem = Params.ReplicationSystem;

	// AssignedOwningConnectionIndices 与基类 AssignedLocationIndices 共用一套 index 空间。
	AssignedOwningConnectionIndices.Init(Params.AbsoluteMaxNetObjectCount);
}

void USphereWithOwnerBoostNetObjectPrioritizer::Deinit()
{
	Super::Deinit();

	ReplicationSystem = nullptr;

	AssignedOwningConnectionIndices.Empty();
	OwningConnections.Empty();
}

bool USphereWithOwnerBoostNetObjectPrioritizer::AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params)
{
	// 先让基类决定能否处理（要求有 RepTag_WorldLocation 或 WorldLocations 注册）。
	if (!Super::AddObject(ObjectIndex, Params))
	{
		return false;
	}

	const uint32 OwningConnectionIndex = AllocOwningConnection();
	// We depend on the index being the same as the one stored in the info. If this check triggers something must have changed in a base class and AllocOwningConnection() must adapt.
	// 关键不变量：本类与基类的 alloc 顺序必须一致——LocationIndex == OwningConnectionIndex。
	// 这样 GetOwningConnection(Info) 可以直接用 LocationIndex 寻址 OwningConnections。
	checkSlow(OwningConnectionIndex == static_cast<FObjectLocationInfo&>(Params.OutInfo).GetLocationIndex());

	// Defer updating the owning connection until the UpdateObjects call.
	// 此时 ReplicationFiltering.GetOwningConnection 可能尚未稳定（owner 设置在 Bridge 层异步写入），延后到 UpdateObjects。

	return true;
}

void USphereWithOwnerBoostNetObjectPrioritizer::RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo& Info)
{
	const FObjectLocationInfo& ObjectInfo = static_cast<const FObjectLocationInfo&>(Info);
	const uint32 OwningConnectionIndex = ObjectInfo.GetLocationIndex();
	OwningConnections[OwningConnectionIndex] = InvalidConnectionID;
	FreeOwningConnection(OwningConnectionIndex);

	// 把 LocationIndex 释放回基类（由基类 RemoveObject 释放）。
	return Super::RemoveObject(ObjectIndex, Info);
}

void USphereWithOwnerBoostNetObjectPrioritizer::UpdateObjects(FNetObjectPrioritizerUpdateParams& Params)
{
	// 先让基类刷新位置缓存。
	Super::UpdateObjects(Params);

	// Update owning connections.
	// 从 ReplicationFiltering 拉取最新 OwningConnection（由 UReplicationSystem::SetOwningNetConnection 维护）。
	const UE::Net::Private::FReplicationFiltering& ReplicationFiltering = ReplicationSystem->GetReplicationSystemInternal()->GetFiltering();

	for (const uint32 ObjectIndex : MakeArrayView(Params.ObjectIndices, Params.ObjectCount))
	{
		FObjectLocationInfo& ObjectInfo = static_cast<FObjectLocationInfo&>(Params.PrioritizationInfos[ObjectIndex]);
		const uint32 OwningConnection = ReplicationFiltering.GetOwningConnection(ObjectIndex);
		// uint32 → uint16 截断：依赖 Init 中的断言保证 MaxConnectionCount < 65535。
		OwningConnections[ObjectInfo.GetLocationIndex()] = static_cast<uint16>(OwningConnection);
	}
}

void USphereWithOwnerBoostNetObjectPrioritizer::Prioritize(FNetObjectPrioritizationParams& PrioritizationParams)
{
	IRIS_PROFILER_SCOPE(USphereWithOwnerBoostNetObjectPrioritizer_Prioritize);

	// 流水线与基类同形式，仅参数类型为 FOwnerBoostBatchParams。
	FMemStack& Mem = FMemStack::Get();
	FMemMark MemMark(Mem);

	// Trade-off memory/performance
	constexpr uint32 MaxBatchObjectCount = 1024U;

	FOwnerBoostBatchParams BatchParams;
	const uint32 BatchObjectCount = FMath::Min((PrioritizationParams.ObjectCount + 3U) & ~3U, MaxBatchObjectCount);
	SetupBatchParams(BatchParams, PrioritizationParams, BatchObjectCount, Mem);

	for (uint32 ObjectIt = 0, ObjectEndIt = PrioritizationParams.ObjectCount; ObjectIt < ObjectEndIt; )
	{
		const uint32 CurrentBatchObjectCount = FMath::Min(ObjectEndIt - ObjectIt, MaxBatchObjectCount);

		BatchParams.ObjectCount = CurrentBatchObjectCount;
		PrepareBatch(BatchParams, PrioritizationParams, ObjectIt);
		PrioritizeBatch(BatchParams);
		FinishBatch(BatchParams, PrioritizationParams, ObjectIt);

		ObjectIt += CurrentBatchObjectCount;
	}
}

void USphereWithOwnerBoostNetObjectPrioritizer::PrepareBatch(FOwnerBoostBatchParams& BatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset)
{
	IRIS_PROFILER_SCOPE(USphereWithOwnerBoostNetObjectPrioritizer_PrepareBatch);
	const float* ExternalPriorities = PrioritizationParams.Priorities;
	const uint32* ObjectIndices = PrioritizationParams.ObjectIndices;
	const FNetObjectPrioritizationInfo* PrioritizationInfos = PrioritizationParams.PrioritizationInfos;

	float* LocalPriorities = BatchParams.Priorities;
	VectorRegister* Positions = BatchParams.Positions;
	uint32* OwnedObjectsLocalIndices = BatchParams.OwnedObjectsLocalIndices;

	// Copy priorities
	{
		uint32 LocalObjIt = 0;
		for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = ObjIt + BatchParams.ObjectCount; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
		{
			const uint32 ObjectIndex = ObjectIndices[ObjIt];
			LocalPriorities[LocalObjIt] = ExternalPriorities[ObjectIndex];
		}
	}

	// Copy positions and owning connections.
	uint32 LocalObjIt = 0;
	{
		const uint32 OwningConnectionId = PrioritizationParams.ConnectionId;
		uint32 OwnedObjectCount = 0;
		// 同一遍循环里：拷贝位置 + 顺路检测 owner，避免二次扫描。
		for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = ObjIt + BatchParams.ObjectCount; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
		{
			const uint32 ObjectIndex = ObjectIndices[ObjIt];
			const FObjectLocationInfo& Info = static_cast<const FObjectLocationInfo&>(PrioritizationInfos[ObjectIndex]);
			Positions[LocalObjIt] = GetLocation(Info);
			if (GetOwningConnection(Info) == OwningConnectionId)
			{
				OwnedObjectsLocalIndices[OwnedObjectCount++] = LocalObjIt;
			}
		}
		BatchParams.OwnedObjectCount = OwnedObjectCount;
	}

	// Make sure we have a multiple of four valid entries
	for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = (ObjIt + 3U) & ~3U; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
	{
		LocalPriorities[LocalObjIt] = 0.0f;
		Positions[LocalObjIt] = VectorZero();
	}
}

void USphereWithOwnerBoostNetObjectPrioritizer::PrioritizeBatch(FOwnerBoostBatchParams& BatchParams)
{
	IRIS_PROFILER_SCOPE(USphereWithOwnerBoostNetObjectPrioritizer_PrioritizeBatch);

	// Let the super class deal with the prioritization and do fixup afterwards.
	// 复用基类 SIMD 球体打分（基类知晓 FBatchParams 即可，不依赖 FOwnerBoostBatchParams）。
	Super::PrioritizeBatch(BatchParams);

	// 然后对 owned 对象做 += OwnerPriorityBoost。注意此时数据仍在本地缓冲，尚未 FinishBatch 写回。
	BoostOwningConnectionPriorities(BatchParams);
}

void USphereWithOwnerBoostNetObjectPrioritizer::FinishBatch(const FOwnerBoostBatchParams& BatchParams, FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset)
{
	// 直接复用基类的写回逻辑（按 ObjectIndices 索引把本地优先级拷回连接的 priorities 数组）。
	Super::FinishBatch(BatchParams, PrioritizationParams, ObjectIndexStartOffset);
}

void USphereWithOwnerBoostNetObjectPrioritizer::BoostOwningConnectionPriorities(FOwnerBoostBatchParams& BatchParams) const
{
	// 标量循环——owned 对象数通常很少（玩家自己的几个 actor），不值得 SIMD。
	const float OwnerPriorityBoost = BatchParams.OwnerPriorityBoost;
	float* LocalPriorities = BatchParams.Priorities;
	const uint32* OwnedObjects = BatchParams.OwnedObjectsLocalIndices;
	for (uint32 ObjIt = 0, ObjEndIt = BatchParams.OwnedObjectCount; ObjIt < ObjEndIt; ++ObjIt)
	{
		const uint32 LocalIndex = OwnedObjects[ObjIt];
		LocalPriorities[LocalIndex] += OwnerPriorityBoost;
	}
}

void USphereWithOwnerBoostNetObjectPrioritizer::SetupBatchParams(FOwnerBoostBatchParams& OutBatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 MaxBatchObjectCount, FMemStackBase& Mem)
{
	// 基类负责 Positions / Priorities / Constants 的分配；本类追加 OwnedObjectsLocalIndices 与 OwnerPriorityBoost。
	Super::SetupBatchParams(OutBatchParams, PrioritizationParams, MaxBatchObjectCount, Mem);
	OutBatchParams.OwnedObjectsLocalIndices = static_cast<uint32*>(Mem.Alloc(MaxBatchObjectCount*sizeof(uint32), alignof(uint32)));

	// 从 Config 取出 boost 值（Config 由基类持有）。
	USphereWithOwnerBoostNetObjectPrioritizerConfig* OwnerBoostConfig = Cast<USphereWithOwnerBoostNetObjectPrioritizerConfig>(Config.Get());
	OutBatchParams.OwnerPriorityBoost = OwnerBoostConfig->OwnerPriorityBoost;
}

uint32 USphereWithOwnerBoostNetObjectPrioritizer::AllocOwningConnection()
{
	// 关键：实现与基类 AllocLocation 完全一致——FindFirstZero + 必要时按 chunk 扩容。
	// 两者交替调用，必须返回相同的 index 序列（在 AddObject 中通过 checkSlow 保证）。
	uint32 Index = AssignedOwningConnectionIndices.FindFirstZero();
	if (Index >= uint32(OwningConnections.Num()))
	{
		constexpr int32 NumElementsPerChunk = OwningConnectionsChunkSize/sizeof(ConnectionId);
		OwningConnections.Add(NumElementsPerChunk);
	}

	AssignedOwningConnectionIndices.SetBit(Index);
	return Index;
}

void USphereWithOwnerBoostNetObjectPrioritizer::FreeOwningConnection(uint32 Index)
{
	AssignedOwningConnectionIndices.ClearBit(Index);
}
