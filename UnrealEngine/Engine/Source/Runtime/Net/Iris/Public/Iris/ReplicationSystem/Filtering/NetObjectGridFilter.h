// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectGridFilter.h —— 基于二维网格（Grid）的空间兴趣管理过滤器（"AOI"风格）。
// ---------------------------------------------------------------------------------------------------------------------
// 算法概览：
//   1) 把世界 XY 平面划分为大小固定（CellSizeX × CellSizeY）的单元格 Cell。
//   2) 每个对象按"位置 ± CullDistance"得到覆盖的 CellBox（FCellBox 表示 [MinX..MaxX]×[MinY..MaxY]），
//      添加到所有覆盖到的 Cell 的 ObjectIndices Set 中。
//   3) 每帧 Filter() 对每个连接：
//      a. 用 View（视点位置）算出"viewer Cell"集合；
//      b. 收集这些 Cell 中的所有对象，得到候选集；
//      c. 若 bUseExactCullDistance：再做精确距离判定，对通过的对象写入 RecentObjectFrameCount，并
//         将其在 OutAllowedObjects 中置位；并按 FrameCountBeforeCulling 计数滞后衰减；
//      d. 若 bUseExactCullDistance=false：候选集中所有对象一律 Allow（开销最小，但允许"超距"复制）。
//   4) UpdateCellInfoForObject 负责对象移动时的"跨 Cell 迁移"（相交时增删 disjoint 部分；不相交时全删全加）。
//
// 两种派生：
//   - UNetObjectGridFilter（abstract）：要求子类实现 BuildObjectInfo + UpdateObjectInfo 来读取对象位置；
//   - UNetObjectGridWorldLocFilter：从 FWorldLocations 全局位置表读取（适用于通过 Bridge 把 Actor 位置同步进来的对象，
//     效率更高，能在 Polling 之前剔除非相关对象）。
//
// 关键内部数据：
//   - FObjectLocationInfo（继承 FNetObjectFilteringInfo）：用 4×uint16 编码 LocationStateOffset、LocationStateIndex、
//     FPerObjectInfo 索引（uint32 拆为低/高 16-bit）。
//   - FPerObjectInfo：位置 / CellBox / CullDistance（含平方）/ FrameCountBeforeCulling，存于 TChunkedArray。
//   - FCellCoord / FCellObjects：Cell 坐标 → 该 Cell 中的对象 Set。
//   - FPerConnectionInfo：RecentCells（最近用过的 Cell 列表，含 Timestamp）+ RecentObjectFrameCount（per-conn 滞后帧数）。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Iris/IrisConfig.h"
#include "Net/Core/NetBitArray.h"
#include "Containers/ChunkedArray.h"
#include "Math/Vector.h"
#include "UObject/StrongObjectPtr.h"
#include "NetObjectGridFilter.generated.h"

namespace UE::Net
{
	class FNetCullDistanceOverrides;
	class FWorldLocations;
	struct FRepTagFindInfo;
}

/**
 * Specialized template that configures unique properties.
 * Useful when you need to specialize a behavior per class or object type
 */
/** 单条特化 Profile：把"Profile 名"映射到"对象在被剔除前还要保留多少帧"。 */
USTRUCT()
struct FNetObjectGridFilterProfile
{
	GENERATED_BODY()

	/** The config name used to map to this profile */
	/** Profile 唯一标识名（在 ObjectReplicationBridgeConfig.FilterConfigs.FilterProfile 中引用）。 */
	UPROPERTY()
	FName FilterProfileName;

	/** Number of frames we keep the object relevant until it is officially culled out.*/
	/** 滞后帧数：被判定"超距"后还要保留多少帧才正式从 Allow 集合移除（用于 bUseExactCullDistance=true 路径）。 */
	UPROPERTY()
	uint16 FrameCountBeforeCulling = 4;

	bool operator==(FName Key) const { return FilterProfileName == Key; }
};

/**
 * Common settings used to configure how the GridFilter behaves
 */
/**
 * GridFilter 全局配置。Cell 尺寸、默认 cull 距离、过期帧数、是否用精确距离等。
 */
UCLASS(transient, config=Engine, MinimalAPI)
class UNetObjectGridFilterConfig : public UNetObjectFilterConfig
{
	GENERATED_BODY()

public:
	/** 
	 * How many frames a previous grid cell should continue to be considered relevant. To avoid culling issues when player borders cells. 
	 * Only used when bUseExactCullDistance is false.
	 */
	/**
	 * 进入新 Cell 后，旧 Cell 还视作"相关"多少帧（防止玩家在 Cell 边界来回移动时频繁切换 Cell 导致抖动）。
	 * 仅当 bUseExactCullDistance=false 时生效。
	 */
	UPROPERTY(Config)
	uint32 ViewPosRelevancyFrameCount = 2;

	/** 默认滞后帧数（对应 FNetObjectGridFilterProfile::FrameCountBeforeCulling 的兜底值）。 */
	UPROPERTY(Config)
	uint16 DefaultFrameCountBeforeCulling = 4;

	/** Cell 在 X 方向的尺寸（uu）。 */
	UPROPERTY(Config)
	float CellSizeX = 20000.0f;

	/** Cell 在 Y 方向的尺寸（uu）。 */
	UPROPERTY(Config)
	float CellSizeY = 20000.0f;

	/** Objects without a NetCullDistanceSquared property will assume to have this value but squared unless there's a cull distance override. */
	/** 缺省 cull 距离（当对象既没有 NetCullDistanceSquared 属性又没有 cull 距离覆写时使用）。 */
	UPROPERTY(Config)
	float DefaultCullDistance = 15000.0f;

	/** 
	 * If true: use the exact distance between an object and the viewer to determine if the object is relevant or should be culled out.
	 * When false: consider all objects within a grid cell to be relevant when a viewer is located within the cell. This can extend the relevant distance of objects beyond their cull distance. 
	 */
	/**
	 * true：精确判定对象-视点距离（FrameCountBeforeCulling 滞后机制生效）。
	 * false：粗放——只要 viewer 与对象 CellBox 重叠就算相关，可能让对象在超过 cull 距离时仍被复制（更便宜）。
	 */
	UPROPERTY(Config)
	bool bUseExactCullDistance = true;

	/** Map of specialized configuration profiles */
	/** 特化 Profile 列表（按 FilterProfileName 查找）。 */
	UPROPERTY(Config)
	TArray<FNetObjectGridFilterProfile> FilterProfiles;
};

/**
 * UNetObjectGridFilter（abstract）：网格空间过滤的通用实现。
 * 由派生类决定"如何获取对象位置"——通过 RepTag 在状态缓冲中查找（受限实现），或直接查 WorldLocations（推荐）。
 */
UCLASS(abstract)
class UNetObjectGridFilter : public UNetObjectFilter
{
	GENERATED_BODY()

protected:
	// UNetObjectFilter interface
	IRISCORE_API virtual void OnInit(const FNetObjectFilterInitParams&) override;            // 读 Config + 声明 Spatial trait
	IRISCORE_API virtual void OnDeinit() override;
	IRISCORE_API virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override;
	IRISCORE_API virtual void AddConnection(uint32 ConnectionId) override;                   // 重置 PerConnectionInfo
	IRISCORE_API virtual void RemoveConnection(uint32 ConnectionId) override;
	IRISCORE_API virtual bool AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams&) override;     // BuildObjectInfo + AddCellInfoForObject
	IRISCORE_API virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo&) override;    // RemoveCellInfoForObject + OnObjectRemoved
	IRISCORE_API virtual void PreFilter(FNetObjectPreFilteringParams&) override;             // 增 FrameIndex + 重置 stats
	IRISCORE_API virtual void Filter(FNetObjectFilteringParams&) override;                   // 主算法（见上方文件头）
	IRISCORE_API virtual void PostFilter(FNetObjectPostFilteringParams&) override;           // CSV 统计
	IRISCORE_API virtual FString PrintDebugInfoForObject(const FDebugInfoParams& Params, uint32 ObjectIndex) const override;

protected:
	/**
	 * per-object 私有数据布局：
	 *   Data[0] = LocationState offset           （在状态缓冲中读取位置的字节偏移；InvalidStateOffset 表示用 WorldLocations）
	 *   Data[1] = LocationState index            （RepTag 找到的字段索引；InvalidStateIndex 同上）
	 *   Data[2/3] = FPerObjectInfo index 的低/高 16 位（uint32）
	 */
	struct FObjectLocationInfo : public FNetObjectFilteringInfo
	{
		bool IsUsingWorldLocations() const  { return GetLocationStateIndex() == InvalidStateIndex; }
		bool IsUsingLocationInState() const { return GetLocationStateIndex() != InvalidStateIndex; }

		/**
		* Data mapping:
		* uint16 Data[0] = LocationState offset
		* uint16 Data[1] = LocationState index
		* uint16 Data[2] = FPerObjectInfo index (low bytes)
		* uint16 Data[3] = FPerObjectInfo index (high bytes)
		*/

		void SetLocationStateOffset(uint16 Offset)	{ Data[0] = Offset; }
		uint16 GetLocationStateOffset() const		{ return Data[0]; }

		void SetLocationStateIndex(uint16 Index) { Data[1] = Index; }
		uint16 GetLocationStateIndex() const	 { return Data[1]; }

		/** 把 32-bit FPerObjectInfo 索引拆/拼到两个 uint16。 */
		void SetInfoIndex(uint32 Index)	{ Data[2] = Index & 65535U; Data[3] = Index >> 16U; }
		uint32 GetInfoIndex() const		{ return (uint32(Data[3]) << 16U) | uint32(Data[2]); }
	};

	/** 把对象加入到其 CellBox 覆盖的所有 Cell（首次添加）。返回 false 表示因 cull 距离过大被拒绝。 */
	IRISCORE_API bool AddCellInfoForObject(const FObjectLocationInfo& ObjectInfo, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol);
	/** 从对象当前 CellBox 覆盖的所有 Cell 中移除。 */
	IRISCORE_API void RemoveCellInfoForObject(const FObjectLocationInfo& ObjectInfo);
	/** 对象移动后：先重算位置/cull，再增删 disjoint 部分（或在不相交时整套重做）。 */
	IRISCORE_API void UpdateCellInfoForObject(const FObjectLocationInfo& ObjectInfo, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol);

protected:

	enum : unsigned
	{
		ObjectInfosChunkSize = 64 * 1024,    // FPerObjectInfo 的 chunk 大小（字节）。TChunkedArray 用以减少大数组内存拷贝。
		InvalidStateIndex = 65535U,          // FObjectLocationInfo Data[1] 的"无效"哨兵：表示走 WorldLocations 路径。
		InvalidStateOffset = 65535U,         // FObjectLocationInfo Data[0] 的"无效"哨兵。
	};

	/** 二维 Cell 范围（闭区间 [MinX..MaxX] × [MinY..MaxY]）。MaxX < MinX 时表示空。 */
	struct FCellBox
	{
		int32 MinX = 0;
		int32 MaxX = -1;
		int32 MinY = 0;
		int32 MaxY = -1;

		bool operator==(const FCellBox&) const;
		bool operator!=(const FCellBox&) const;
	};

	// We can't fit all info we need in 4x16bits.
	/** per-object 主信息：位置、CellBox、ObjectIndex、cull 距离（含平方缓存）、滞后帧数。 */
	struct FPerObjectInfo
	{
		FVector Position = FVector::ZeroVector;
		FCellBox CellBox;
		uint32 ObjectIndex = 0U;
		uint16 FrameCountBeforeCulling = 0U;

		float GetCullDistance() const
		{
			return CullDistance;
		}

		float GetCullDistanceSq() const
		{
			return CullDistanceSq;
		}

		/** 一次性更新两份缓存（避免每次 Filter 计算平方）。 */
		void SetCullDistance(float Distance)
		{
			CullDistance = Distance;
			CullDistanceSq = Distance * Distance;
		}

		/** 反向：从平方距离倒推（开销略大，仅 init 时使用）。 */
		void SetCullDistanceSq(float DistanceSq)
		{
			CullDistance = FPlatformMath::Sqrt(DistanceSq);
			CullDistanceSq = DistanceSq;
		}

	private:
		float CullDistance = 0.0f;
		float CullDistanceSq = 0.0f;
	};

	/** Sets the current position of the object based on how we access it's given location. */
	/** 派生类必须实现：从合适来源（状态缓冲 / WorldLocations）刷入对象的最新位置/cull 距离。 */
	virtual void UpdateObjectInfo(FPerObjectInfo& PerObjectInfo, const FObjectLocationInfo& ObjectLocationInfo, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol) {}

	/** Build the data needed to properly filter this object. */
	/** 派生类必须实现：在 AddObject 时建立 FObjectLocationInfo 中的索引/偏移，决定走哪条位置访问路径。 */
	virtual bool BuildObjectInfo(uint32 ObjectIndex, FNetObjectFilterAddObjectParams& Params) { return false; }

	/** Callback when an object is removed from the grid. Useful to cleanup data tied to the object. */
	/** 对象从 grid 移除时回调（派生类可清理外挂资源）。 */
	virtual void OnObjectRemoved(uint32 ObjectIndex) {}

private:

	/** 二维整数 Cell 坐标（用作 TMap key）。GetTypeHash 把 (X,Y) 拼成 uint64 后再 hash。 */
	struct FCellCoord
	{
		int32 X;
		int32 Y;

	private:
		friend bool operator==(const FCellCoord& A, const FCellCoord& B)
		{
			return (A.X == B.X) & (A.Y == B.Y);
		}

		friend uint32 GetTypeHash(const FCellCoord& Coords)
		{
			return ::GetTypeHash((uint64(uint32(Coords.X)) << 32U) | uint64(uint32(Coords.Y)));
		}
	};

	/** 单个 Cell 内的对象集合（去重）。 */
	struct FCellObjects
	{
		TSet<uint32> ObjectIndices;
	};

	/** Cell + 时间戳（FrameIndex），便于淘汰过期 Cell（仅 bUseExactCullDistance=false 路径）。 */
	struct FCellAndTimestamp
	{
		FCellCoord Cell;
		uint32 Timestamp;

	private:
		friend bool operator==(const FCellAndTimestamp& A, const FCellAndTimestamp& B)
		{
			return (A.Cell == B.Cell) & (A.Timestamp == B.Timestamp);
		}
	};

	/** per-connection 数据。 */
	struct FPerConnectionInfo
	{
		// We don't expect a lot of view positions from a single connection
		/** 该连接最近活跃的 Cell（含分屏视图）。InlineAllocator<32> 兼容典型用例的零分配。 */
		TArray<FCellAndTimestamp, TInlineAllocator<32>> RecentCells;
		
		// Objects that have been recently visible to the connection and the frame countdown.
		/** 该连接的"最近可见对象 → 剩余滞后帧数"；每帧 Filter 时倒计：到 0 移出 Allow 集。 */
		TMap<uint32, uint16> RecentObjectFrameCount;
	};

	/** Aggregator for stats */
	/** CSV 统计聚合（仅 UE_NET_IRIS_CSV_STATS 启用）。 */
	struct FNetGridFilterStats
	{
		FNetGridFilterStats() = default;

		void Reset() { *this = FNetGridFilterStats(); }
		
		/** GridFilter stats */
		uint64 CullTestingTimeInCycles = 0;
		uint32 CullTestedObjects = 0;
	};

private:
	/** 在 ObjectInfos 中分配/回收一个 FPerObjectInfo 槽位（Bitmap-base allocator）。 */
	uint32 AllocObjectInfo();
	void FreeObjectInfo(uint32 Index);

	/** 调用派生类 UpdateObjectInfo 刷新位置/cull（由 AddCellInfoForObject、UpdateCellInfoForObject 使用）。 */
	void UpdatePositionAndCullDistance(const FObjectLocationInfo& ObjectLocationInfo, FPerObjectInfo& PerObjectInfo, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol);
	/** 由 Position ± CullDistance 计算覆盖的 CellBox（同时按世界尺寸 clamp）。 */
	void CalculateCellBox(const FPerObjectInfo& PerObjectInfo, FCellBox& OutCellBox);
	/** Pos → 单个 Cell 坐标（用于 viewer 当前所在 Cell 计算）。 */
	void CalculateCellCoord(FCellCoord& OutCoord, const FVector& Pos);

	/** 取 Profile 的 FrameCountBeforeCulling，找不到则返回默认值。 */
	uint16 GetFrameCountBeforeCulling(FName ProfileName) const;

	/** 两 CellBox 是否完全不相交。 */
	static bool AreCellsDisjoint(const FCellBox& A, const FCellBox& B);
	/** Coord 是否落在 Cell 范围内。 */
	static bool DoesCellContainCoord(const FCellBox& Cell, const FCellCoord& Coord);

	TStrongObjectPtr<UNetObjectGridFilterConfig> Config;
	/** per-connection 数据数组，索引为 ConnectionId（[0..MaxConn]）。 */
	TArray<FPerConnectionInfo> PerConnectionInfos;
	/** 所有对象的 FPerObjectInfo 池（chunked）。 */
	TChunkedArray<FPerObjectInfo, ObjectInfosChunkSize> ObjectInfos;
	/** ObjectInfos 槽位占用位图。 */
	UE::Net::FNetBitArray AssignedObjectInfoIndices;

#if UE_NET_IRIS_CSV_STATS
	FNetGridFilterStats Stats;
#endif

	/** Cell 表：FCellCoord → FCellObjects（包含该 Cell 内的对象索引集合）。 */
	TMap<FCellCoord, FCellObjects> Cells;
	/** 帧序号（每次 PreFilter 自增），用作 Cell 时间戳。 */
	uint32 FrameIndex = 0;

	/** UpdateCellInfoForObject 的诊断快照（崩溃时检查现场）。 */
	struct FDebugUpdateCellInfo
	{
		FCellCoord Coord;
		FCellBox NewCellBox;
		FCellBox PrevCellBox;
		SIZE_T CellsSize;
		FVector ObjectPosition;
		float ObjectCullDistance;
	};

	// Cached state from UpdateCellInfoForObject().
	FDebugUpdateCellInfo DebugUpdateCellInfo;

	/** World boundaries (min and max position). */
	/** 世界边界（来自 UWorldLocationsConfig，用于 clamp Cell 计算避免 int 溢出）。 */
	FVector MinWorldPos;
	FVector MaxWorldPos;
	/** Cull 距离上限（来自 UWorldLocationsConfig；超过则拒绝 AddObject）。0 表示无上限。 */
	float MaxNetCullDistance = 0.f;
};

/**
 * Filter for replicated objects that have a WorldLocation reference (e.g. Actors).
 * 
 * This filter is more efficient since it's run before Polling and culls out objects that are not relevant to any connection.
 */
/**
 * UNetObjectGridWorldLocFilter —— 推荐使用的 GridFilter 派生类。
 * 通过 FWorldLocations（由 ObjectReplicationBridge 在帧前同步进 Iris 的全局位置表）读取对象位置；
 * 由于在 Polling 之前就能剔除非相关对象，能省掉大量 PreUpdate / Poll / Copy 工作量，整体收益显著。
 */
UCLASS(transient, MinimalAPI)
class UNetObjectGridWorldLocFilter : public UNetObjectGridFilter
{
	GENERATED_BODY()

protected:
	IRISCORE_API virtual void OnInit(const FNetObjectFilterInitParams&) override;            // 缓存 WorldLocations 引用
	IRISCORE_API virtual void OnDeinit() override;                                            // 释放 WorldLocations 引用
	IRISCORE_API virtual void PreFilter(FNetObjectPreFilteringParams&) override;              // 对脏位置对象批量 UpdateCellInfo
	IRISCORE_API virtual void UpdateObjectInfo(FPerObjectInfo& PerObjectInfo, const UNetObjectGridFilter::FObjectLocationInfo& ObjectLocationInfo, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol) override;
	IRISCORE_API virtual bool BuildObjectInfo(uint32 ObjectIndex, FNetObjectFilterAddObjectParams& Params) override;

private:
	/** 全局位置/cull 距离表的引用（OnInit 时缓存）。 */
	const UE::Net::FWorldLocations* WorldLocations = nullptr;
};


//
inline bool UNetObjectGridFilter::FCellBox::operator==(const UNetObjectGridFilter::FCellBox& Other) const
{
	return (MinX == Other.MinX) & (MinY == Other.MinY) & (MaxX == Other.MaxX) & (MaxY == Other.MaxY);
}

inline bool UNetObjectGridFilter::FCellBox::operator!=(const UNetObjectGridFilter::FCellBox& Other) const
{
	return (MinX != Other.MinX) | (MinY != Other.MinY) | (MaxX != Other.MaxX) | (MaxY != Other.MaxY);
}
