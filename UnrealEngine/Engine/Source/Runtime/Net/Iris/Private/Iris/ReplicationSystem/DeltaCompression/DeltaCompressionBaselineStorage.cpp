// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaselineStorage.cpp
// ---------------------------------------------------------------------------------------------
// 实现 FDeltaCompressionBaselineStorage：基线槽位池 + 引用计数 + Reserve/Commit 二段提交。
// 详细生命周期见 DeltaCompressionBaseline.h 顶部 ASCII 图。
// =============================================================================================

#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineStorage.h"
#include "Iris/IrisConfigInternal.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/ReplicationState/ReplicationStateStorage.h"
#include "HAL/LowLevelMemTracker.h"

// 按 IrisConfigInternal 中的 UE_NET_VALIDATE_DC_BASELINES 决定是否启用调试断言。
#if UE_NET_VALIDATE_DC_BASELINES
#define UE_NET_DC_BASELINE_CHECK(...) check(__VA_ARGS__)
#else
#define UE_NET_DC_BASELINE_CHECK(...) 
#endif

namespace UE::Net::Private
{

FDeltaCompressionBaselineStorage::FDeltaCompressionBaselineStorage()
{
	// 槽位 0 永远是无效槽，约定与 NetRefHandleManager::InvalidInternalIndex 一致，
	// 这样上层代码可以用 0 作为 "no baseline" 的哨兵。
	static_assert(InvalidDeltaCompressionBaselineStateInfoIndex == 0, "This class assumes InvalidDeltaCompressionBaselineStateInfoIndex == 0");
}

FDeltaCompressionBaselineStorage::~FDeltaCompressionBaselineStorage()
{
	// 内部仅占位的 0 号槽位被 set，UsedBaselineStateInfos.IsAnyBitSet() 用于检测真实泄漏。
	checkf(UsedBaselineStateInfos.IsAnyBitSet() == false, TEXT("Leak in FDeltaCompressionBaselineStorage"));
}

void FDeltaCompressionBaselineStorage::Init(FDeltaCompressionBaselineStorageInitParams& InitParams)
{
	// MaxBaselineCount + 1：第 0 位用作 invalid 占位。
	UsedBaselineStateInfos.Init(InitParams.MaxBaselineCount + 1U);
	UsedBaselineStateInfos.SetBit(InvalidDeltaCompressionBaselineStateInfoIndex);
	ReplicationStateStorage = InitParams.ReplicationStateStorage;
}

void FDeltaCompressionBaselineStorage::Deinit()
{
	// 由 Manager 在它的 Deinit 中调用；此时所有 PerObjectInfo 应已释放完毕。
	FreeAllBaselineStateInfos();
}

// 立即创建一份与当前 SendState 相同内容的基线副本。RefCount 默认 1。
// 失败：池满或底层 AllocBaseline 失败。
FDeltaCompressionBaselineStateInfo FDeltaCompressionBaselineStorage::CreateBaselineFromCurrentState(uint32 ObjectIndex)
{
	FDeltaCompressionBaselineStateInfo BaselineStateInfo;
	
	const uint32 StateInfoIndex = AllocBaselineStateInfo();
	if (StateInfoIndex == InvalidDeltaCompressionBaselineStateInfoIndex)
	{
		return BaselineStateInfo;
	}

	// 通过 ReplicationStateStorage 立即克隆一份 CurrentSendState 内容到独立 buffer。
	uint8* StateBuffer = ReplicationStateStorage->AllocBaseline(ObjectIndex, EReplicationStateType::CurrentSendState);
	if (StateBuffer == nullptr)
	{
		FreeBaselineStateInfo(StateInfoIndex);
		return BaselineStateInfo;
	}

	// Fill in internal baseline info
	{
		FInternalBaselineStateInfo* InternalBaselineStateInfo = GetBaselineStateInfo(StateInfoIndex);
		InternalBaselineStateInfo->StateBuffer = StateBuffer;
		InternalBaselineStateInfo->ObjectIndex = ObjectIndex;
	}

	// Fill in return value
	BaselineStateInfo.StateBuffer = StateBuffer;
	BaselineStateInfo.StateInfoIndex = StateInfoIndex;

	return BaselineStateInfo;
}

void FDeltaCompressionBaselineStorage::AddRefBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex)
{
	// 调用方应保证 StateInfoIndex 当前有效。
	UE_NET_DC_BASELINE_CHECK(UsedBaselineStateInfos.GetBit(StateInfoIndex));

	FInternalBaselineStateInfo* InternalBaselineInfo = GetBaselineStateInfo(StateInfoIndex);
	++InternalBaselineInfo->RefCount;
}

void FDeltaCompressionBaselineStorage::ReleaseBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex)
{
	UE_NET_DC_BASELINE_CHECK(UsedBaselineStateInfos.GetBit(StateInfoIndex));

	FInternalBaselineStateInfo* InternalBaselineInfo = GetBaselineStateInfo(StateInfoIndex);
	UE_NET_DC_BASELINE_CHECK(InternalBaselineInfo->RefCount > 0);
	if (--InternalBaselineInfo->RefCount == 0)
	{
		// 引用归零：释放槽位 + 调用 ReplicationStateStorage::FreeBaseline 释放底层 buffer。
		FreeBaselineStateInfo(StateInfoIndex);
	}
}

FDeltaCompressionBaselineStateInfo FDeltaCompressionBaselineStorage::GetBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex) const
{
	UE_NET_DC_BASELINE_CHECK(UsedBaselineStateInfos.GetBit(StateInfoIndex));

	const FInternalBaselineStateInfo* InternalBaselineInfo = GetBaselineStateInfo(StateInfoIndex);

	FDeltaCompressionBaselineStateInfo BaselineStateInfo;
	BaselineStateInfo.StateBuffer = InternalBaselineInfo->StateBuffer;
	BaselineStateInfo.StateInfoIndex = StateInfoIndex;

	return BaselineStateInfo;
}

// Reserve 路径：仅占位，不立即拷贝；返回的 StateBuffer 指向当前 SendState（最新数据）。
// 这样如果本帧后续没人 AddRef，就直接 Cancel；否则 Commit 时再做一次性拷贝（节省冗余 memcpy）。
FDeltaCompressionBaselineStateInfo FDeltaCompressionBaselineStorage::ReserveBaselineForCurrentState(uint32 ObjectIndex)
{
	FDeltaCompressionBaselineStateInfo BaselineStateInfo;
	
	const uint32 StateInfoIndex = AllocBaselineStateInfo();
	if (StateInfoIndex == InvalidDeltaCompressionBaselineStateInfoIndex)
	{
		return BaselineStateInfo;
	}

	FReplicationStateStorage::FBaselineReservation Reservation = ReplicationStateStorage->ReserveBaseline(ObjectIndex, EReplicationStateType::CurrentSendState);
	if (!Reservation.IsValid())
	{
		FreeBaselineStateInfo(StateInfoIndex);
		return BaselineStateInfo;
	}

	// Fill in internal baseline info
	{
		FInternalBaselineStateInfo* InternalBaselineStateInfo = GetBaselineStateInfo(StateInfoIndex);
		// ReservedStorage = Commit 时将真正拷贝到的目的地；现在先把它记下来供后续 Cancel/Commit 引用。
		InternalBaselineStateInfo->StateBuffer = Reservation.ReservedStorage;
		InternalBaselineStateInfo->ObjectIndex = ObjectIndex;
	}

	// Fill in return value
	// BaselineBaseStorage = 当前 SendState 起始地址。Reserve 阶段调用方拿到的是这份"鲜活"数据。
	BaselineStateInfo.StateBuffer = const_cast<uint8*>(Reservation.BaselineBaseStorage);
	BaselineStateInfo.StateInfoIndex = StateInfoIndex;

	return BaselineStateInfo;
}

void FDeltaCompressionBaselineStorage::OptionallyCommitAndDoReleaseBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex)
{
	UE_NET_DC_BASELINE_CHECK(UsedBaselineStateInfos.GetBit(StateInfoIndex));

	FInternalBaselineStateInfo* InternalBaselineInfo = GetBaselineStateInfo(StateInfoIndex);
	UE_NET_DC_BASELINE_CHECK(InternalBaselineInfo->RefCount > 0);
	if (--InternalBaselineInfo->RefCount == 0)
	{
		// 没人引用 → 取消预留（不做拷贝，仅释放占位空间）。
		ReplicationStateStorage->CancelBaselineReservation(InternalBaselineInfo->ObjectIndex, InternalBaselineInfo->StateBuffer);
		InternalBaselineInfo->StateBuffer = nullptr;

		FreeBaselineStateInfo(StateInfoIndex);
	}
	else
	{
		// 还有人引用 → 此刻才把 CurrentSendState 真正克隆到预留区域，使其成为持久基线。
		ReplicationStateStorage->CommitBaselineReservation(InternalBaselineInfo->ObjectIndex, InternalBaselineInfo->StateBuffer, EReplicationStateType::CurrentSendState);
	}
}

// 取已 reserve 槽位的 info：与 GetBaseline 区别在于 StateBuffer 取自当前 SendState 而非槽位副本。
FDeltaCompressionBaselineStateInfo FDeltaCompressionBaselineStorage::GetBaselineReservationForCurrentState(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex)
{
	UE_NET_DC_BASELINE_CHECK(UsedBaselineStateInfos.GetBit(StateInfoIndex));

	FInternalBaselineStateInfo* InternalBaselineInfo = GetBaselineStateInfo(StateInfoIndex);
	UE_NET_DC_BASELINE_CHECK(InternalBaselineInfo->RefCount > 0);

	FDeltaCompressionBaselineStateInfo BaselineStateInfo;
	BaselineStateInfo.StateBuffer = const_cast<uint8*>(ReplicationStateStorage->GetState(InternalBaselineInfo->ObjectIndex, EReplicationStateType::CurrentSendState));
	BaselineStateInfo.StateInfoIndex = StateInfoIndex;

	return BaselineStateInfo;
}

void FDeltaCompressionBaselineStorage::ConstructBaselineStateInfo(FInternalBaselineStateInfo* BaselineStateInfo) const
{
	// 默认构造：StateBuffer=null / ObjectIndex=0 / RefCount=1（见结构体声明）。
	*BaselineStateInfo = FInternalBaselineStateInfo();
}

void FDeltaCompressionBaselineStorage::DestructBaselineStateInfo(FInternalBaselineStateInfo* BaselineStateInfo)
{
	// 槽位回收 → 真正释放底层 buffer。注意：Cancel 路径会把 StateBuffer 设为 null 来跳过这里。
	if (BaselineStateInfo->StateBuffer != nullptr)
	{
		ReplicationStateStorage->FreeBaseline(BaselineStateInfo->ObjectIndex, BaselineStateInfo->StateBuffer);
	}
}

DeltaCompressionBaselineStateInfoIndexType FDeltaCompressionBaselineStorage::AllocBaselineStateInfo()
{
	// 找第一个未占用的槽位（FindFirstZero 在位图上 O(words/64)）。
	const uint32 StateInfoIndex = UsedBaselineStateInfos.FindFirstZero();
	if (StateInfoIndex != FNetBitArray::InvalidIndex)
	{
		// 必要时按 BaselineStateInfoGrowCount 段扩容内部记录数组。
		if (StateInfoIndex >= uint32(BaselineStateInfos.Num()))
		{
			BaselineStateInfos.Add(BaselineStateInfoGrowCount);
			UE_NET_DC_BASELINE_CHECK(StateInfoIndex < uint32(BaselineStateInfos.Num()));
		}

		FInternalBaselineStateInfo& BaselineStateInfo = BaselineStateInfos[StateInfoIndex];
		ConstructBaselineStateInfo(&BaselineStateInfo);

		UsedBaselineStateInfos.SetBit(StateInfoIndex);

		return StateInfoIndex;
	}

	// 池满（已达 MaxBaselineCount）：返回 invalid，Manager 端会优雅降级（不创建基线）。
	return InvalidDeltaCompressionBaselineStateInfoIndex;
}

void FDeltaCompressionBaselineStorage::FreeBaselineStateInfo(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex)
{
	UE_NET_DC_BASELINE_CHECK(UsedBaselineStateInfos.GetBit(StateInfoIndex));

	UsedBaselineStateInfos.ClearBit(StateInfoIndex);

	FInternalBaselineStateInfo* BaselineStateInfo = GetBaselineStateInfo(StateInfoIndex);
	DestructBaselineStateInfo(BaselineStateInfo);
}

void FDeltaCompressionBaselineStorage::FreeAllBaselineStateInfos()
{
	auto DestructStateInfo = [this](uint32 StateInfoIndex)
	{
		FInternalBaselineStateInfo* ObjectInfo = GetBaselineStateInfo(StateInfoIndex);
		DestructBaselineStateInfo(ObjectInfo);
	};

	// 先把 0 号占位清掉以免触发析构（其 StateBuffer 始终为 null，但保险起见）。
	UsedBaselineStateInfos.ClearBit(InvalidDeltaCompressionBaselineStateInfoIndex);
	UsedBaselineStateInfos.ForAllSetBits(DestructStateInfo);
	UsedBaselineStateInfos.ClearAllBits();
}

}

#undef UE_NET_DC_BASELINE_CHECK
