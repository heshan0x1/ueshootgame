// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaselineInvalidationTracker.cpp
// ---------------------------------------------------------------------------------------------
// 实现 FDeltaCompressionBaselineInvalidationTracker：单帧聚合"基线作废事件"，供 Manager 消费。
// =============================================================================================

#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineInvalidationTracker.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineManager.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"

namespace UE::Net::Private
{

FDeltaCompressionBaselineInvalidationTracker::FDeltaCompressionBaselineInvalidationTracker()
{
}

FDeltaCompressionBaselineInvalidationTracker::~FDeltaCompressionBaselineInvalidationTracker()
{
}

void FDeltaCompressionBaselineInvalidationTracker::Init(FDeltaCompressionBaselineInvalidationTrackerInitParams& InitParams)
{
	BaselineManager = InitParams.BaselineManager;
	InvalidatedObjects.Init(InitParams.MaxInternalNetRefIndex);
}

void FDeltaCompressionBaselineInvalidationTracker::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	// NetRefHandle 池上限提高时同步扩容位图（不影响已有数据）。
	InvalidatedObjects.SetNumBits(NewMaxInternalIndex);
}

void FDeltaCompressionBaselineInvalidationTracker::InvalidateBaselines(FInternalNetRefIndex ObjectIndex, uint32 ConnId)
{
	// 去重：同对象本帧已经被全连接作废过 → 直接忽略后续登记（再加入 list 也只会被释放空槽，浪费）。
	if (InvalidatedObjects.GetBit(ObjectIndex))
	{
		return;
	}

	// 优化：未启用 DC 的对象根本没有基线，无须收集（避免占满 list）。
	if (BaselineManager->GetDeltaCompressionStatus(ObjectIndex) != ENetObjectDeltaCompressionStatus::Allow)
	{
		return;
	}

	// 仅对"全连接作废"做位图记录；按单连接作废并不去重，因为 ConnId 多种排列组合较多。
	if (ConnId == InvalidateBaselineForAllConnections)
	{
		InvalidatedObjects.SetBit(ObjectIndex);
	}

	// 容量预扩展（避免每次 Add 都触发 grow）。
	if (InvalidationInfos.GetSlack() == 0)
	{
		InvalidationInfos.Reserve(InvalidationInfos.Num() + InvalidationInfoGrowCount);
	}

	InvalidationInfos.Emplace(FInvalidationInfo{ConnId, ObjectIndex});
}

void FDeltaCompressionBaselineInvalidationTracker::PreSendUpdate()
{
	// Could consider sorting of object indices here.
	// 中文：当前为空。日后可在此按 ObjectIndex 排序提高 cache 命中率（对应消费侧的随机访问）。
}

void FDeltaCompressionBaselineInvalidationTracker::PostSendUpdate()
{
	// 一帧用完即扔。Empty() 不会缩容，重复使用容量。
	if (InvalidationInfos.Num() > 0)
	{
		InvalidationInfos.Empty();
		InvalidatedObjects.ClearAllBits();
	}
}

}
