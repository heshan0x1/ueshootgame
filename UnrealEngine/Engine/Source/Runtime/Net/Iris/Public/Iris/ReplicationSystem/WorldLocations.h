// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   FWorldLocations: Iris 复制系统中“每对象世界位置 + Cull 距离” 的集中存储模块。
//
//   职责:
//     - 为每个 InternalNetRefIndex 缓存一组 (WorldLocation, CullDistance, CullDistanceOverride)。
//     - 提供 “脏位” 集合 (ObjectsWithDirtyInfo): 仅当本帧位置或 CullDistance 改变时才置位,
//       供 Grid/Sphere/FOV Prioritizer 与 GridFilter 拉取最小变动集做空间索引。
//     - 提供 “需要每帧更新” 的对象集合 (ObjectsRequiringFrequentWorldLocationUpdate): 
//       例如 attach 到其它对象的 actor, 自身可能不脏但位置随父更新, 必须强制每帧上报。
//     - 通过 LockDirtyInfoList(true/false) 在 prioritizer 阶段做并发只读保护(DO_ENSURE 下的断言)。
//     - 通过 PostSendUpdate() 在每帧 send 完成后清理脏位, 准备下一帧。
//
//   数据布局:
//     - StoredObjectInfo: TNetChunkedArray<FObjectInfo, StorageElementsPerChunk>, 64KB/chunk.
//     - StorageIndexes:  ObjectIndex -> 在 StoredObjectInfo 中的下标 (稀疏 -> 紧凑映射, 节省空间)。
//     - ReservedStorageSlot: 紧凑下标的占用位图。
//
//   触发更新:
//     UObjectReplicationBridge::CallUpdateInstancesWorldLocation()
//       -> 对每个被注册过 InitObjectInfoCache 的对象调用 SetObjectInfo()
//       -> 内部检测变动, 写入 ObjectsWithDirtyInfo
//
//   配置:
//     UWorldLocationsConfig (Engine.ini, [/Script/IrisCore.WorldLocationsConfig]):
//       MinPos / MaxPos          - 世界坐标钳制盒, 防止极端坐标破坏 Grid 索引。
//       MaxNetCullDistance       - CullDistance 软上限, 超过仅 Verbose 警告 + ensure。
//
//   与 ReplicationSystem.md §7 (辅助 & 周边) 一致, 是 Filter / Prioritizer 的输入源。
// =====================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/ChunkedArray.h"
#include "Iris/Core/NetChunkedArray.h"
#include "Net/Core/NetBitArray.h"
#include "Math/Vector.h"
#include "Misc/Optional.h"
#include "WorldLocations.generated.h"

class UReplicationSystem;

namespace UE::Net::Private
{
	typedef uint32 FInternalNetRefIndex;
	class FNetRefHandleManager;
}

/**
* Common settings used to configure how the GridFilter behaves
*
* (中文) 该 UCLASS 通过 [Engine] 配置段读取, 用于:
*   - 设定世界边界盒 (Min/MaxPos): SetObjectInfo 中会把传入位置 BoundToBox 钳制进盒子内,
*     避免超大坐标击穿 GridFilter 的稀疏哈希。
*   - 设定 MaxNetCullDistance: 检测人为设置过大的 CullDistance, 提示考虑改成 always relevant。
*/
UCLASS(Config=Engine)
class UWorldLocationsConfig : public UObject
{
	GENERATED_BODY()

public:
	/** All world positions will be clamped to MinPos and MaxPos. */
	UPROPERTY(Config)
	FVector MinPos = { -0.5f * 2097152.0f, -0.5f * 2097152.0f, -0.5f * 2097152.0f };

	/** All world positions will be clamped to MinPos and MaxPos. */
	UPROPERTY(Config)
	FVector MaxPos = { +0.5f * 2097152.0f, +0.5f * 2097152.0f, +0.5f * 2097152.0f };

	/** We will issue a warning if user sets a higher NetCullDistance or NetCullDistanceOverride than the MaxNetCullDistance. */
	UPROPERTY(Config)
	float MaxNetCullDistance = 150000.f;
};

namespace UE::Net
{

struct FWorldLocationsInitParams
{
	TObjectPtr<UReplicationSystem> ReplicationSystem = nullptr;

	// 当前 ReplicationSystem 内部对象索引上限 (会用于初始化 NetBitArray 长度)
	UE::Net::Private::FInternalNetRefIndex MaxInternalNetRefIndex = 0;

	/** How many world info storage slots to preallocate. */
	// 预分配的 FObjectInfo 紧凑槽位数 (后续可按 StorageElementsPerChunk 动态扩容)
	uint32 PreallocatedStorageCount = 0;
};

/**
 * 每对象世界位置 + Cull 距离的集中存储与脏位跟踪。
 * 由 ReplicationSystemInternal 持有, Bridge / Filter / Prioritizer 通过 const 访问其位图与数据。
 */
class FWorldLocations
{
public: 

	/** Publically available information of a replicated root object */
	/** 对外暴露的“当前已生效”世界信息 (合并了 CullDistanceOverride 之后的最终值)。 */
	struct FWorldInfo
	{
		/** Absolute coordinate of the object */
		FVector WorldLocation = FVector::ZeroVector;

		/** The current network cull distance of the object */
		float CullDistance = 0.0f;
	};

public:

	/** 由 ReplicationSystemInternal 调用; 分配位图、紧凑存储, 并读取 UWorldLocationsConfig。 */
	void Init(const FWorldLocationsInitParams& InitParams);
	/** 每帧 SendUpdate 完成后调用: 清理 ObjectsWithDirtyInfo 并解锁脏列表。 */
	void PostSendUpdate();

	/** Returns whether the object has a valid cached data or not. */
	bool HasInfoForObject(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
	{
		return ValidInfoIndexes.IsBitSet(ObjectIndex);
	}

	/** Returns the object's world location if it's valid or a zero vector if it's not. */
	inline FVector GetWorldLocation(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const;

	/** Get the object's current cull distance. */
	/** (中文) 若设置了 Override 则返回 Override, 否则返回默认 CullDistance; 未注册 -> 0。 */
	inline float GetCullDistance(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const;

	/** Return the current stored world information of the given object, or NullOpt if the object did not register in the world location cache. */
	inline TOptional<FWorldLocations::FWorldInfo> GetWorldInfo(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const;

	/** Set the mandatory info of a replicated root object*/
	/**
	 * (中文) 写入对象的世界位置和默认 CullDistance。会自动:
	 *   - 钳制位置到 [MinPos, MaxPos]
	 *   - 比较前后值, 仅当真正变动才置 ObjectsWithDirtyInfo 位
	 *   - DO_ENSURE 下检测 bLockdownDirtyList(防止在 prioritizer/filter 读阶段被并发修改)
	 */
	void SetObjectInfo(UE::Net::Private::FInternalNetRefIndex ObjectIndex, const FVector& Location, float NetCullDistance);
	
	/** Assign a world information cache to the replicated object */
	/** (中文) 为对象在紧凑存储中申请一个 slot 并把 ValidInfoIndexes 对应位置 1; slot 不足时按 chunk 扩容。 */
	void InitObjectInfoCache(UE::Net::Private::FInternalNetRefIndex ObjectIndex);

	/** Remove the world information cache of the replicated object */
	/** (中文) 释放对象的 slot, 默认构造 FObjectInfo 以便后续复用; 同时清理脏位与“频繁更新位”。 */
	void RemoveObjectInfoCache(UE::Net::Private::FInternalNetRefIndex ObjectIndex);

	/**
	 * Objects are not necessarily marked as dirty just because they're moving, such as objects attached to other objects. 
	 * If such objects are spatially filtered they need to update their world locations in order for replication to work as expected.
	 * Use SetObjectRequiresFrequentWorldLocationUpdate to force frequent world location update on an object.
	 *
	 * (中文) 例如 attach 到载具骨骼的武器, 武器属性自身没变, 但其世界位置随载具变化。
	 * 启用此位后, Bridge::CallUpdateInstancesWorldLocation 每帧都会重新查询其位置并写入 SetObjectInfo,
	 * 否则空间过滤器(Grid/Sphere)会读到陈旧位置导致漏复制或误复制。
	 */
	void SetObjectRequiresFrequentWorldLocationUpdate(UE::Net::Private::FInternalNetRefIndex ObjectIndex, bool bRequiresFrequentUpdate)
	{
		// 仅当对象已经注册 (ValidInfoIndexes) 时才允许置位; 这样避免在 RemoveObjectInfoCache 后仍残留为 1
		ObjectsRequiringFrequentWorldLocationUpdate.SetBitValue(ObjectIndex, ValidInfoIndexes.GetBit(ObjectIndex) && bRequiresFrequentUpdate);
	}

	/** Returns whether an object requires frequent world location updates. */
	bool GetObjectRequiresFrequentWorldLocationUpdate(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
	{
		return ObjectsRequiringFrequentWorldLocationUpdate.GetBit(ObjectIndex);
	}

	/** 
	 * Add a temporary net cull distance that will have priority over the regular net cull distance.
	 * Returns true if the object had registered to use the world location cache and can store the override.
	 *
	 * (中文) Override 优先级高于普通 CullDistance, 用于 “暂时把视距拉远/拉近” 的玩法逻辑。
	 *        FLT_MAX 作为 “未设置” 哨兵。
	 */
	bool SetCullDistanceOverride(UE::Net::Private::FInternalNetRefIndex ObjectIndex, float CullDistance);

	/** 
	 * Remove the temporary net cull distance override and instead use the regular net cull distance 
	 * Returns true if the object had registered to use the world location cache and had an override value previously set
	 */
	bool ClearCullDistanceOverride(UE::Net::Private::FInternalNetRefIndex ObjectIndex);

	/** Returns true if the object was set a cull distance override and is using it instead of his default cull distance value */
	bool HasCullDistanceOverride(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
	{
		return ValidInfoIndexes.IsBitSet(ObjectIndex) ? GetObjectInfo(ObjectIndex).CullDistanceOverride != FLT_MAX : false;
	}

	/** Returns the list of objects that need to check for a location change every frame */
	const FNetBitArrayView GetObjectsRequiringFrequentWorldLocationUpdate() const { return MakeNetBitArrayView(ObjectsRequiringFrequentWorldLocationUpdate); }

	/** Returns the list of objects that changed world location or cull distance this frame */
	const FNetBitArrayView GetObjectsWithDirtyInfo() const { return MakeNetBitArrayView(ObjectsWithDirtyInfo); }

	/** Returns the list of objects that registered world location information */
	const FNetBitArrayView GetObjectsWithWorldInfo() const { return MakeNetBitArrayView(ValidInfoIndexes); }

	/** Reset the list of objects that changed location or cull distance */
	void ResetObjectsWithDirtyInfo();

	/** Debug tool to track when its legal to modify the DirtyInfo list. */
	/** (中文) 在 Filter/Prioritizer 读取阶段把脏列表锁住, 此期间任何 SetObjectInfo 都会触发 ensure。 */
	void LockDirtyInfoList(bool bLock);

	/** Return the world boundaries (min and max position). */
	const FVector& GetWorldMinPos() const { return MinWorldPos; };
	const FVector& GetWorldMaxPos() const { return MaxWorldPos; };
	
	/** Return a position clamped to the configured world boundary. */
	FVector ClampPositionToBoundary(const FVector& Position) const
	{
		return Position.BoundToBox(GetWorldMinPos(), GetWorldMaxPos());
	}

	/** Is the location without the configured Min/Max WorldPos*/
	bool IsValidLocation(const FVector& Location) const
	{
		return (Location.X >= MinWorldPos.X && Location.Y >= MinWorldPos.Y && Location.Z >= MinWorldPos.Z &&
				Location.X <= MaxWorldPos.X && Location.Y <= MaxWorldPos.Y && Location.Z <= MaxWorldPos.Z);
	}

	/** ReplicationSystem 扩容 InternalNetRefIndex 上限时同步扩展所有位图与映射数组。 */
	void OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex);

private:

	/** Contains the cached object data we are storing. */
	/** (中文) 单对象的存储项, 紧凑数组中每个 slot 一份。 */
	struct FObjectInfo
	{
		/** Absolute coordinate of the object */
		FVector WorldLocation = FVector::ZeroVector;

		/** The default network cull distance of the object */
		float CullDistance = 0.0f;

		/** The optional temporary cull distance override. Max means it is not used */
		// FLT_MAX 表示未设置 Override
		float CullDistanceOverride = FLT_MAX;
	};

	enum : uint32
	{
		// 单 chunk 64KB 容量内可装下的 FObjectInfo 数(整除); TNetChunkedArray 按此粒度扩容
		StorageElementsPerChunk = 65536U / sizeof(FObjectInfo),
	};

private:

	const int32 GetStorageIndex(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
	{
		return StorageIndexes[ObjectIndex];
	}

	const FObjectInfo& GetObjectInfo(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
	{
		return StoredObjectInfo[GetStorageIndex(ObjectIndex)];
	}

	FObjectInfo& GetObjectInfo(UE::Net::Private::FInternalNetRefIndex ObjectIndex)
	{
		return StoredObjectInfo[GetStorageIndex(ObjectIndex)];
	}

private:

	/** Set bits indicate that we have stored information for this internal object index */
	// 已注册的对象集合 (调用过 InitObjectInfoCache)
	FNetBitArray ValidInfoIndexes;
	
	/** Set bits indicate that the world location or net cull distance has changed since last update */
	// 本帧脏对象集合: 仅当真正变化时置位, PostSendUpdate 中清空
	FNetBitArray ObjectsWithDirtyInfo;
	
	/** Set bits indicate that the object requires frequent world location updates */
	// 强制每帧重新查询的对象集合 (attach 到他人/无脏标记但位置随父变)
	FNetBitArray ObjectsRequiringFrequentWorldLocationUpdate;

	/** Map that returns the storage index for the world info of a registered object. */
	// ObjectIndex -> 紧凑存储下标 (稀疏 ObjectIndex 到密集 slot 的映射, 节省内存)
	TArray<int32> StorageIndexes;

	/** List detailing if a given index slot is free to be used to store world info. */
	// 紧凑 slot 占用位图; 释放后可被新对象复用
	FNetBitArray ReservedStorageSlot;

	// 真正的紧凑数据存储, 分块按需扩容 (StorageElementsPerChunk 一组)
	TNetChunkedArray<FObjectInfo, StorageElementsPerChunk> StoredObjectInfo;

	const UE::Net::Private::FNetRefHandleManager* NetRefHandleManager = nullptr;

	/** World boundaries (min and max position). */
	FVector MinWorldPos;
	FVector MaxWorldPos;
	float MaxNetCullDistance;

	/** Controls if the dirty list can be modified */
	// 由 LockDirtyInfoList 切换; DO_ENSURE 下保证读阶段无人写
	bool bLockdownDirtyList = false;
};

inline FVector FWorldLocations::GetWorldLocation(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
{
	return ValidInfoIndexes.IsBitSet(ObjectIndex) ? GetObjectInfo(ObjectIndex).WorldLocation : FVector::Zero();
}

inline float FWorldLocations::GetCullDistance(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
{
	if (ValidInfoIndexes.IsBitSet(ObjectIndex))
	{
		const FObjectInfo& Info = GetObjectInfo(ObjectIndex);
		return Info.CullDistanceOverride == FLT_MAX ? Info.CullDistance : Info.CullDistanceOverride;
	}

	return 0.0f;
}

inline TOptional<FWorldLocations::FWorldInfo> FWorldLocations::GetWorldInfo(UE::Net::Private::FInternalNetRefIndex ObjectIndex) const
{
	if (ValidInfoIndexes.IsBitSet(ObjectIndex))
	{
		const FObjectInfo& Info = GetObjectInfo(ObjectIndex);
		return FWorldInfo
		{ 
			.WorldLocation = Info.WorldLocation, 
			.CullDistance = Info.CullDistanceOverride == FLT_MAX ? Info.CullDistance : Info.CullDistanceOverride,
		};
	}

	return NullOpt;
}

} // end namespace UE::Net
