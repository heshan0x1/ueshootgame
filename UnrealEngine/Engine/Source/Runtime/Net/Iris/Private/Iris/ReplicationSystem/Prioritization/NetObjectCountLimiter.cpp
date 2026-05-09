// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：UNetObjectCountLimiter 实现 —— RoundRobin / Fill 两种数量限额策略。
// 关键流程：
//   PrePrioritize（每帧一次）：
//     - PrioFrame++；
//     - RoundRobin：从 NextIndexToConsider 顺序取下 N 个 internal index 设位（未来该帧 Prioritize 时使用）；
//     - Fill：无全局准备工作，全部 per-conn 进行。
//   Prioritize（每帧每连接）：
//     - RoundRobin：直接遍历对象，仅当对象 internal index 在 RoundRobin 选中位图里（或是 owned & FastLane 开启）时设优先级；
//     - Fill：用 FMemory_Alloca 分配 sort 数组，按 (bIsOwnedByConnection ↓, FrameCountSinceConsidered ↓, InternalIndex ↑) 排序，
//             取前 N 个（不含 owned，如果 FastLane 开），更新 LastConsiderFrame。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Prioritization/NetObjectCountLimiter.h"
#include "Iris/IrisConstants.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFiltering.h"
#include "Iris/Core/IrisProfiler.h"
#include "Algo/Sort.h"
#include "Containers/ArrayView.h"
#include "HAL/PlatformMemory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectCountLimiter)

UNetObjectCountLimiter::UNetObjectCountLimiter()
: PrioFrame(0)
, ReplicationSystemId(UE::Net::InvalidReplicationSystemId)
{
	// 编译期断言：FObjectInfo 必须严格放在 16 字节槽内，禁止扩成员（其它 prioritizer 也会复用同一槽）。
	static_assert(sizeof(FObjectInfo) == sizeof(FNetObjectPrioritizationInfo), "Can't add members to FNetObjectPrioritizationInfo.");
}

void UNetObjectCountLimiter::Init(FNetObjectPrioritizerInitParams& Params)
{
	checkf(Params.Config != nullptr, TEXT("Need config to operate."));
	// uint16 编码 ConnectionId（FObjectInfo::Data[1]），所以 MaxConnectionCount < 65536。
	checkf(Params.MaxConnectionCount < 65536U, TEXT("Assumption being able to use uint16 for ConnectionIds is incorrect."));
	Config = TStrongObjectPtr<UNetObjectCountLimiterConfig>(CastChecked<UNetObjectCountLimiterConfig>(Params.Config));
	// MaxObjectCount=0 时所有对象只在被创建时复制一次后再不会复制——这通常不是预期；输出 ensure 警告。
	ensureMsgf(Config->MaxObjectCount >= 1U, TEXT("Prioritizer will not consider any object for replication. They will be replicated once when constructed, but never again."));

	InternalObjectIndices.Init(ObjectGrowCount);
	// +1 是为了 ConnectionId 0 留位（Iris 连接 ID 从 1 开始，但数组按 ID 索引）。
	PerConnectionInfos.SetNum(Params.MaxConnectionCount + 1);

	ReplicationSystem = Params.ReplicationSystem;
}

void UNetObjectCountLimiter::Deinit()
{
	ReplicationSystem = nullptr;
	Config = nullptr;

	InternalObjectIndices.Empty();
}

void UNetObjectCountLimiter::AddConnection(uint32 ConnectionId)
{
	// 新连接的 LastConsiderFrames 初始化为当前 PrioFrame——避免新连接立即认为所有对象"已久未考虑"。
	FPerConnectionInfo& Info = PerConnectionInfos[ConnectionId];
	Info.LastConsiderFrames.Init(PrioFrame, InternalObjectIndices.GetNumBits());
}

void UNetObjectCountLimiter::RemoveConnection(uint32 ConnectionId)
{
	// 释放该连接的 LastConsiderFrames 内存。
	FPerConnectionInfo& Info = PerConnectionInfos[ConnectionId];
	FPerConnectionInfo Empty;
	Info = MoveTemp(Empty);
}

bool UNetObjectCountLimiter::AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params)
{
	// 不需要任何 RepTag——任何对象都可以受 CountLimiter 管理。
	FObjectInfo& Info = static_cast<FObjectInfo&>(Params.OutInfo);
	const uint16 Index = AllocInternalIndex();
	Info.SetPrioritizerInternalIndex(Index);
	return true;
}

void UNetObjectCountLimiter::RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo& InInfo)
{
	const FObjectInfo& Info = static_cast<const FObjectInfo&>(InInfo);
	FreeInternalIndex(Info.GetPrioritizerInternalIndex());
}

void UNetObjectCountLimiter::UpdateObjects(FNetObjectPrioritizerUpdateParams& Params)
{
	// 每帧把所有受影响对象的 OwningConnection 同步进 FObjectInfo（Owner 由 ReplicationFiltering 维护）。
	const UE::Net::Private::FReplicationFiltering& ReplicationFiltering = ReplicationSystem->GetReplicationSystemInternal()->GetFiltering();

	for (const uint32 ObjectIndex : MakeArrayView(Params.ObjectIndices, Params.ObjectCount))
	{
		FObjectInfo& ObjectInfo = static_cast<FObjectInfo&>(Params.PrioritizationInfos[ObjectIndex]);
		const uint32 OwningConnection = ReplicationFiltering.GetOwningConnection(ObjectIndex);
		ObjectInfo.SetOwningConnection(OwningConnection);
	}
}

void UNetObjectCountLimiter::PrePrioritize(FNetObjectPrePrioritizationParams& Params)
{
	// 帧计数器自增（Fill 模式排序依赖此值）。
	++PrioFrame;
	switch (Config->Mode)
	{
		case ENetObjectCountLimiterMode::RoundRobin:
		{
			// RoundRobin：全局选 N 个对象做"本帧候选"。所有连接共用这同一选择。
			PrePrioritizeForRoundRobin();
			break;
		};

		case ENetObjectCountLimiterMode::Fill:
		{
			// There's no meaningful work to do as everything is connection specific.
			break;
		};

		default:
		{
			break;
		}
	};
}

void UNetObjectCountLimiter::Prioritize(FNetObjectPrioritizationParams& Params)
{
	switch (Config->Mode)
	{
		case ENetObjectCountLimiterMode::RoundRobin:
		{
			PrioritizeForRoundRobin(Params);
			break;
		};

		case ENetObjectCountLimiterMode::Fill:
		{
			PrioritizeForFill(Params);
			break;
		};
	}
}

uint16 UNetObjectCountLimiter::AllocInternalIndex()
{
	// FindFirstZero 找第一个 0 位；找不到说明位图已满，按 ObjectGrowCount 扩容。
	uint32 Index = InternalObjectIndices.FindFirstZero();
	if (Index == UE::Net::FNetBitArray::InvalidIndex)
	{
		Index = InternalObjectIndices.GetNumBits();
		// 上限：65535（uint16 编码）。
		check(Index <= 65535U);
		InternalObjectIndices.AddBits(ObjectGrowCount);
	}

	InternalObjectIndices.SetBit(Index);
	return static_cast<uint16>(Index);
}

void UNetObjectCountLimiter::FreeInternalIndex(uint16 Index)
{
	InternalObjectIndices.ClearBit(Index);
}

void UNetObjectCountLimiter::PrePrioritizeForRoundRobin()
{
	IRIS_PROFILER_SCOPE(UNetObjectCountLimiter_PrePrioritizeForRoundRobin);

	// Find the next N viable objects. These will be considered for replication for all connections if they're dirty.
	// 从 NextIndexToConsider 开始扫描下一个 N 个有效 internal index。环形回绕。
	const uint32 IndexCount = InternalObjectIndices.GetNumBits();
	RoundRobinState.InternalObjectIndices.Init(IndexCount);
	if (RoundRobinState.NextIndexToConsider >= IndexCount)
	{
		RoundRobinState.NextIndexToConsider = 0;
	}

	const uint32 MaxObjectCount = Config->MaxObjectCount;
	// Obey the user's wishes. No objects will be prioritized. There's an ensure in Init().
	if (MaxObjectCount == 0)
	{
		return;
	}

	// Alloca 栈分配，避免堆开销。N 通常很小（典型值 2~10）。
	uint32* Indices = static_cast<uint32*>(FMemory_Alloca(MaxObjectCount*sizeof(uint32)));
	// 第一次扫描：从 NextIndexToConsider 到末尾。
	uint32 ObjectCount = InternalObjectIndices.GetSetBitIndices(RoundRobinState.NextIndexToConsider, ~0U, Indices, MaxObjectCount);
	// 不够 N 个 → 从 0 到 NextIndexToConsider-1 继续扫描（环形）。
	if (RoundRobinState.NextIndexToConsider > 0 && ObjectCount < MaxObjectCount)
	{
		ObjectCount += InternalObjectIndices.GetSetBitIndices(0U, RoundRobinState.NextIndexToConsider - 1U, Indices + ObjectCount, MaxObjectCount - ObjectCount);
	}

	if (ObjectCount)
	{
		// 下次起点 = 最后一个被选中的 +1。
		RoundRobinState.NextIndexToConsider = static_cast<uint16>(Indices[ObjectCount - 1U] + 1U);
		for (uint32 Index : MakeArrayView(Indices, ObjectCount))
		{
			RoundRobinState.InternalObjectIndices.SetBit(Index);
		}
	}
}

void UNetObjectCountLimiter::PrioritizeForRoundRobin(FNetObjectPrioritizationParams& Params) const
{
	IRIS_PROFILER_SCOPE(UNetObjectCountLimiter_PrioritizeForRoundRobin);

	const bool bEnableOwnedObjectsFastLane = Config->bEnableOwnedObjectsFastLane;
	const float StandardPriority = Config->Priority;
	const float OwningConnectionPriority = Config->OwningConnectionPriority;

	const uint32 MaxConsiderCount = Config->MaxObjectCount;
	uint32 ConsiderCount = 0U;
	if (bEnableOwnedObjectsFastLane)
	{
		// 走快速通道：owned 对象不计入 N 限额（始终给 OwningConnectionPriority）。
		for (const uint32 ObjectIndex : MakeArrayView(Params.ObjectIndices, Params.ObjectCount))
		{
			const FObjectInfo& Info = static_cast<const FObjectInfo&>(Params.PrioritizationInfos[ObjectIndex]);
			const bool bIsRoundRobin = RoundRobinState.InternalObjectIndices.GetBit(Info.GetPrioritizerInternalIndex());
			const bool bIsOwnedByConnection = Info.GetOwningConnection() == Params.ConnectionId;
			// 既不是本帧轮到、也不是 owned → 跳过。
			if (!(bIsRoundRobin | bIsOwnedByConnection))
			{
				continue;
			}

			if (bIsOwnedByConnection)
			{
				Params.Priorities[ObjectIndex] = OwningConnectionPriority;
			}
			else if (ConsiderCount < MaxConsiderCount)
			{
				++ConsiderCount;
				Params.Priorities[ObjectIndex] = StandardPriority;
			}
		}
	}
	else
	{
		// 不走快速通道：owned 对象也占名额，达到 N 后立即 break。
		for (const uint32 ObjectIndex : MakeArrayView(Params.ObjectIndices, Params.ObjectCount))
		{
			const FObjectInfo& Info = static_cast<const FObjectInfo&>(Params.PrioritizationInfos[ObjectIndex]);
			if (!RoundRobinState.InternalObjectIndices.GetBit(Info.GetPrioritizerInternalIndex()))
			{
				continue;
			}

			const float Priority = (Info.GetOwningConnection() == Params.ConnectionId ? OwningConnectionPriority : StandardPriority);
			Params.Priorities[ObjectIndex] = Priority;
			if (++ConsiderCount == MaxConsiderCount)
			{
				break;
			}
		}
	}
}

void UNetObjectCountLimiter::PrioritizeForFill(FNetObjectPrioritizationParams& Params)
{
	IRIS_PROFILER_SCOPE(UNetObjectCountLimiter_PrioritizeForFill);

	/*
	 * Warn about entering this function twice in the same frame for the same connection
	 * as that means there were so many objects that they were split into multiple batches.
	 * With Fill mode it's vital that we get all the dirty objects in the same batch so
	 * we can see which of those were replicated least recently. There's no proper fix
	 * as we cannot/shouldn't rely on priority pointers being valid outside
	 * the scope of the Prioritize() call. The priority system batch size must be increased
	 * or the mode for this instance changed to RoundRobin. What will happen when this ensure
	 * happens is that each batch could potentially prioritize N objects.
	 */
	// Fill 模式的限制：必须一次看到全部脏对象才能正确排序。
	// 当对象数 > FPrioritizerBatchHelper::MaxObjectCountPerBatch(=1024) 时会被切片导致重入此函数。
	// 此时每批都会独立排序选 N 个，相当于"每批 N 个"，违反原意。
	// 解决方案：提升 batch size 或改用 RoundRobin。
	ensureMsgf(FillState.LastPrioFrame != PrioFrame || FillState.LastConnectionId != Params.ConnectionId, TEXT("%s"), TEXT("UNetObjectCountLimiter::PrioritizeForFill. Too many objects are being prioritized"));

	FillState.LastPrioFrame = PrioFrame;
	FillState.LastConnectionId = Params.ConnectionId;

	const uint32 ConnectionId = Params.ConnectionId;
	FPerConnectionInfo& ConnectionInfo = PerConnectionInfos[Params.ConnectionId];

	// Make sure we can get/set info for all potential objects
	// LastConsiderFrames 按需扩容（新对象在 AddObject 时不立即触发扩容）。
	if (static_cast<uint32>(ConnectionInfo.LastConsiderFrames.Num()) < InternalObjectIndices.GetNumBits())
	{
		/*
		 * When new objects have been added it's ok if the LastConsiderFrame is 0.
		 * That's why we don't fill new objects with some special value.
		 * New objects will be replicated for creation anyway and if they are considered 
		 * by this prioritizer we won't waste bandwidth by adding even more objects. 
		 */
		// 新对象的 LastConsiderFrame=0（默认值）：FrameCountSinceConsidered = PrioFrame，会得到很高的"久未考虑度"。
		// 这是有意设计——新对象在创建时本来就会被复制，不会因此浪费带宽。
		ConnectionInfo.LastConsiderFrames.SetNum(InternalObjectIndices.GetNumBits());
	}

	uint32* LastConsideredFrames = ConnectionInfo.LastConsiderFrames.GetData();

	// 排序信息（紧凑结构，便于栈分配）。
	struct FSortInfo
	{
		uint32 ObjectIndex;
		uint32 InternalIndex;
		uint32 FrameCountSinceConsidered;   // PrioFrame - LastConsiderFrame；越大越优先选中。
		bool bIsOwnedByConnection;
	};

	// Can use some sort of scratch pad if this is problematic. We don't expect this to require more than 16KiB.
	// 栈分配：N≤1024，sizeof(FSortInfo)=16 → ≤ 16 KiB，安全。
	FSortInfo* SortInfosAlloc = static_cast<FSortInfo*>(FMemory_Alloca(sizeof(FSortInfo)*Params.ObjectCount));
	TArrayView<FSortInfo> SortInfos = MakeArrayView(SortInfosAlloc, Params.ObjectCount);

	// Prep sort
	{
		FSortInfo* SortInfoIter = SortInfosAlloc;
		for (const uint32 ObjectIndex : MakeArrayView(Params.ObjectIndices, Params.ObjectCount))
		{
			const FObjectInfo& ObjectInfo = static_cast<const FObjectInfo&>(Params.PrioritizationInfos[ObjectIndex]);
			const uint32 PrioritizerInternalIndex = ObjectInfo.GetPrioritizerInternalIndex();

			FSortInfo& SortInfo = *SortInfoIter++;
			SortInfo.ObjectIndex = ObjectIndex;
			SortInfo.InternalIndex = PrioritizerInternalIndex;
			// This should work reasonably well even with FrameIndex wraparound.
			// 利用 uint32 减法的环绕（PrioFrame 溢出回 0 时仍能得到正确的"经过帧数"）。
			SortInfo.FrameCountSinceConsidered = PrioFrame - LastConsideredFrames[PrioritizerInternalIndex];
			SortInfo.bIsOwnedByConnection = (ObjectInfo.GetOwningConnection() == ConnectionId);
		}
	}

	// Sort and prioritize
	{
		const float StandardPriority = Config->Priority;
		const float OwningConnectionPriority = Config->OwningConnectionPriority;
		const uint32 MaxConsiderCount = Config->MaxObjectCount;
		uint32 ConsiderCount = 0U;

		if (Config->bEnableOwnedObjectsFastLane)
		{
			// 排序键：owned 在前 → 久未考虑在前 → 内部索引小在前（稳定 tie-breaker）。
			auto ByOwnerAndLeastConsidered = [](const FSortInfo& A, const FSortInfo& B)
			{
				if (A.bIsOwnedByConnection != B.bIsOwnedByConnection)
				{
					return A.bIsOwnedByConnection;
				}

				if (A.FrameCountSinceConsidered != B.FrameCountSinceConsidered)
				{
					return A.FrameCountSinceConsidered > B.FrameCountSinceConsidered;
				}

				// Tie-breaker
				return (A.InternalIndex < B.InternalIndex);
			};

			Algo::Sort(SortInfos, ByOwnerAndLeastConsidered);

			for (const FSortInfo& Info : SortInfos)
			{
				LastConsideredFrames[Info.InternalIndex] = PrioFrame;

				const float Priority = (Info.bIsOwnedByConnection ? OwningConnectionPriority : StandardPriority);
				Params.Priorities[Info.ObjectIndex] = Priority;
				
				// We're sorting so that owned objects end up first so if we've reached MaxConsiderCount we're done.
				// owned 不计入名额；遇到第一个非 owned 起开始计数；累计 MaxConsiderCount 个非 owned 后跳出。
				ConsiderCount += !Info.bIsOwnedByConnection;
				if (ConsiderCount == MaxConsiderCount)
				{
					break;
				}
			}
		}
		else
		{
			// 不走快速通道：仅按 FrameCountSinceConsidered 排序，所有对象一视同仁。
			auto ByLeastConsidered = [](const FSortInfo& A, const FSortInfo& B)
			{
				if (A.FrameCountSinceConsidered != B.FrameCountSinceConsidered)
				{
					return A.FrameCountSinceConsidered > B.FrameCountSinceConsidered;
				}

				// Tie-breaker
				return (A.InternalIndex < B.InternalIndex);
			};
			Algo::Sort(SortInfos, ByLeastConsidered);

			for (const FSortInfo& Info : SortInfos)
			{
				LastConsideredFrames[Info.InternalIndex] = PrioFrame;

				const float Priority = (Info.bIsOwnedByConnection ? OwningConnectionPriority : StandardPriority);
				Params.Priorities[Info.ObjectIndex] = Priority;
				
				if (++ConsiderCount == MaxConsiderCount)
				{
					break;
				}
			}
		}
	}
}
