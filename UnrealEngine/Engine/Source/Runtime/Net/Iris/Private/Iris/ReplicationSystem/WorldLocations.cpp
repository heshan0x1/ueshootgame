// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   FWorldLocations 的实现. 关键点:
//     - InitObjectInfoCache: 第一次注册对象时申请一个紧凑 slot, 不足时按 StorageElementsPerChunk 扩容
//       (TNetChunkedArray::Add(N) 一次性扩出整个 chunk).
//     - SetObjectInfo: 钳制坐标 -> 比较前后值 -> 决定是否写入 ObjectsWithDirtyInfo.
//     - SetCullDistanceOverride / ClearCullDistanceOverride: 用 FLT_MAX 作为 “未设置” 哨兵.
//     - PostSendUpdate: 每帧 SendUpdate 完成后调用一次, 清空脏位 + 解锁脏列表.
//     - OnMaxInternalNetRefIndexIncreased: 配合 ReplicationSystem 扩容内部索引 (位图与映射数组同步增长).
//
//   Lockdown:
//     - 在 prioritizer / filter 读取脏列表的窗口中, 上层会 LockDirtyInfoList(true) 标记为只读;
//       此期间 SetObjectInfo / Override 修改会触发 ensure 提示并发错误.
// =====================================================================================================

#include "Iris/ReplicationSystem/WorldLocations.h"
#include "Iris/Core/IrisMemoryTracker.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldLocations)

namespace UE::Net
{

void FWorldLocations::Init(const FWorldLocationsInitParams& InitParams)
{
	// 三个 NetBitArray 的位数与 ReplicationSystem 内部索引上限一致, 一一对应:
	//   ValidInfoIndexes                          - 已注册位置数据的对象
	//   ObjectsWithDirtyInfo                      - 本帧位置/Cull 已变动
	//   ObjectsRequiringFrequentWorldLocationUpdate - 强制每帧更新
	ValidInfoIndexes.Init(InitParams.MaxInternalNetRefIndex);
	ObjectsWithDirtyInfo.Init(InitParams.MaxInternalNetRefIndex);
	ObjectsRequiringFrequentWorldLocationUpdate.Init(InitParams.MaxInternalNetRefIndex);
	StorageIndexes.SetNum(InitParams.MaxInternalNetRefIndex);

	// 紧凑存储相关: ReservedStorageSlot 跟踪已用 slot, StoredObjectInfo 实际数据
	ReservedStorageSlot.Init(InitParams.PreallocatedStorageCount);	
	StoredObjectInfo = TNetChunkedArray<FObjectInfo, StorageElementsPerChunk>(InitParams.PreallocatedStorageCount, EInitMemory::Constructor);

	// 从 .ini 配置读取边界与 Cull 上限
	MinWorldPos = GetDefault<UWorldLocationsConfig>()->MinPos;
	MaxWorldPos = GetDefault<UWorldLocationsConfig>()->MaxPos;
	MaxNetCullDistance = GetDefault<UWorldLocationsConfig>()->MaxNetCullDistance;
	
	NetRefHandleManager = &InitParams.ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
}

void FWorldLocations::PostSendUpdate()
{
	// Clear the dirty info list to start fresh for the next frame
	// 解锁脏列表 + 清空所有位; 下一帧 SetObjectInfo / Override 会重新置位
#if DO_ENSURE
	bLockdownDirtyList = false;
#endif

	ObjectsWithDirtyInfo.ClearAllBits();
}

void FWorldLocations::OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex)
{
	ValidInfoIndexes.SetNumBits(NewMaxInternalIndex);
	ObjectsWithDirtyInfo.SetNumBits(NewMaxInternalIndex);
	ObjectsRequiringFrequentWorldLocationUpdate.SetNumBits(NewMaxInternalIndex);
	StorageIndexes.SetNum(NewMaxInternalIndex);
}

void FWorldLocations::InitObjectInfoCache(UE::Net::Private::FInternalNetRefIndex ObjectIndex)
{
	if (ValidInfoIndexes.IsBitSet(ObjectIndex))
	{
		// Only init on first assignment
		// 已经注册过, 直接返回, 避免覆盖已有数据
		return;
	}

	ValidInfoIndexes.SetBit(ObjectIndex);

	// Find an available slot
	// 在已分配的 slot 中找一个空闲位
	uint32 AvailableSlot = ReservedStorageSlot.FindFirstZero();

	// No more slots available, grow the storage space by a single chunk
	// 没有空位 -> 一次性扩容一个 chunk (StorageElementsPerChunk 个 FObjectInfo, ~64KB)
	if (AvailableSlot == FNetBitArray::InvalidIndex)
	{
		LLM_SCOPE_BYTAG(Iris);
		AvailableSlot = ReservedStorageSlot.GetNumBits();
		StoredObjectInfo.Add(StorageElementsPerChunk);
		ReservedStorageSlot.SetNumBits(AvailableSlot + StorageElementsPerChunk);
	}

	ReservedStorageSlot.SetBit(AvailableSlot);
	StorageIndexes[ObjectIndex] = AvailableSlot;
}

void FWorldLocations::RemoveObjectInfoCache(UE::Net::Private::FInternalNetRefIndex ObjectIndex)
{
	if (!ValidInfoIndexes.IsBitSet(ObjectIndex))
	{
		// Object did not register a location
		return;
	}

	// 三组位图同步清理 (避免 RemoveObject 后还残留为 dirty/frequent-update)
	ValidInfoIndexes.ClearBit(ObjectIndex);
	ObjectsWithDirtyInfo.ClearBit(ObjectIndex);
	ObjectsRequiringFrequentWorldLocationUpdate.ClearBit(ObjectIndex);

	const uint32 StorageIndex = StorageIndexes[ObjectIndex];
	StorageIndexes[ObjectIndex] = INDEX_NONE;

	// Default construct the info since it can be reused in the future.
	// 重置数据以便 slot 后续被新对象复用 (CullDistanceOverride 回到 FLT_MAX 等)
	StoredObjectInfo[StorageIndex] = FObjectInfo();

	ReservedStorageSlot.ClearBit(StorageIndex);
}

void FWorldLocations::SetObjectInfo(UE::Net::Private::FInternalNetRefIndex ObjectIndex, const FVector& Location, const float NetCullDistance)
{
	// Lockdown 期间不允许写入(读取阶段) -> 触发 ensure
	ensure(!bLockdownDirtyList);

	checkSlow(ValidInfoIndexes.IsBitSet(ObjectIndex));
	FObjectInfo& TargetObjectInfo = GetObjectInfo(ObjectIndex);

	// 钳制位置, 防止极端坐标导致 GridFilter 哈希异常
	const FVector ClampedLoc = ClampPositionToBoundary(Location);
	
	// dirty 判定: 只要本帧之前已 dirty, 或值有变化, 就重新置位
	const bool bHasCullDistanceChanged = TargetObjectInfo.CullDistance != NetCullDistance;
	const bool bHasInfoChanged = ObjectsWithDirtyInfo.GetBit(ObjectIndex) || TargetObjectInfo.WorldLocation != ClampedLoc || bHasCullDistanceChanged;

	TargetObjectInfo.WorldLocation = ClampedLoc;

	// For now we just warn, this will be clamped by filter.
	// 超过 MaxNetCullDistance 时仅日志/ensure 提示, 不强制改写; 下游 GridFilter 自己再做钳制
	if (bHasCullDistanceChanged && MaxNetCullDistance > 0.f && NetCullDistance > MaxNetCullDistance)
	{
		UE_LOG(LogIrisNetCull, Verbose, TEXT("FWorldLocations::SetObjectInfo ReplicatedObject %s cull distance %f is above the max %f. Consider making object always relevant instead"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), NetCullDistance, MaxNetCullDistance);
		ensureMsgf(false, TEXT("FWorldLocations::SetObjectInfo ReplicatedObject %s cull distance %f is above the max %f. Consider making object always relevant instead"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), NetCullDistance, MaxNetCullDistance);
	}

	TargetObjectInfo.CullDistance = NetCullDistance;

	ObjectsWithDirtyInfo.OrBitValue(ObjectIndex, bHasInfoChanged);
}

bool FWorldLocations::SetCullDistanceOverride(UE::Net::Private::FInternalNetRefIndex ObjectIndex, float CullDistance)
{
	ensure(!bLockdownDirtyList);

	if (ValidInfoIndexes.IsBitSet(ObjectIndex))
	{
		// TODO: Check for zero or clamp down on huge values ?
		if (GetObjectInfo(ObjectIndex).CullDistanceOverride != CullDistance)
		{
			// For now we just warn, this will be clamped by filter.
			if (MaxNetCullDistance > 0.f && CullDistance > MaxNetCullDistance)
			{
				UE_LOG(LogIrisNetCull, Verbose, TEXT("FWorldLocations::SetCullDistanceOverride ReplicatedObject %s cull distance %f is above the max %f. Consider making object always relevant instead"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), CullDistance, MaxNetCullDistance);	
				ensureMsgf(false, TEXT("FWorldLocations::SetCullDistanceOverride ReplicatedObject %s cull distance %f is above the max %f. Consider making object always relevant instead"), *NetRefHandleManager->PrintObjectFromIndex(ObjectIndex), CullDistance, MaxNetCullDistance);	
			}

			GetObjectInfo(ObjectIndex).CullDistanceOverride = CullDistance;
			ObjectsWithDirtyInfo.SetBitValue(ObjectIndex, true);
		}
		return true;
	}

	return false;
}

bool FWorldLocations::ClearCullDistanceOverride(UE::Net::Private::FInternalNetRefIndex ObjectIndex)
{
	ensure(!bLockdownDirtyList);

	if (ValidInfoIndexes.IsBitSet(ObjectIndex) && GetObjectInfo(ObjectIndex).CullDistanceOverride != FLT_MAX)
	{
		GetObjectInfo(ObjectIndex).CullDistanceOverride = FLT_MAX;
		ObjectsWithDirtyInfo.SetBitValue(ObjectIndex, true);
		return true;
	}

	return false;
}

void FWorldLocations::LockDirtyInfoList(bool bLock)
{
#if DO_ENSURE
	bLockdownDirtyList = bLock;
#endif
}

} // end namespace UE::Net
