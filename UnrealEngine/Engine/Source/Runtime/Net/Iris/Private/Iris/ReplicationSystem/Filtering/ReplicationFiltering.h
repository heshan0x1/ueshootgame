// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationFiltering.h —— Filtering 子模块"核心调度器" FReplicationFiltering 的声明。
// ---------------------------------------------------------------------------------------------------------------------
// 目标：每帧（PreSendUpdate 阶段）为每个连接生成一份"本帧需要复制的对象集合" RelevantObjectsInScope。
//
// 三阶段过滤流水线（写入 per-connection 位图）：
//
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 0) UpdateObjectsInScope                                            │
//        │    全局 ScopeList(NetRefHandleManager) → 写入 ObjectsInScopeBefore │
//        │    DynamicFiltering（per-connection 缓存以避免每帧重算）            │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 1) Exclusion Group（UpdateGroupExclusionFiltering）                 │
//        │    Groups 中"IsExclusionFiltering"组：默认 Disallow，逐连接 Allow   │
//        │    位图：GroupExcludedObjects（per-conn）                          │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 2) Owner / Connection Filter（UpdateOwnerFiltering）                │
//        │    ToOwnerFilterHandle / ConnectionFilterHandle 等"静态"过滤实现   │
//        │    位图：ConnectionFilteredObjects（per-conn）                     │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 3) SubObject Filter（UpdateSubObjectFilters）                       │
//        │    使用 FSharedConnectionFilterStatusCollection（含父子连接）      │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 4) PreUpdateObjectScopeHysteresis                                  │
//        │    清"被销毁/解除动态过滤"的对象的滞后；做连接节流（每帧仅 1/N 连接）│
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 5) Dynamic Filter（UpdateDynamicFilters）                           │
//        │    驱动各 UNetObjectFilter（AlwaysRelevant / Grid / Connection ...）│
//        │    PreFilter → Filter（多批） → PostFilter                          │
//        │    位图：DynamicFilteredOutObjects（per-conn）                     │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 6) Inclusion Group（在 UpdateDynamicFiltering 内）                  │
//        │    把"FilterOut + Inclusion"模式中允许的对象重新加入 Allow         │
//        │    位图：GroupIncludedObjects（per-conn）                          │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 7) PostUpdateObjectScopeHysteresis + DynamicFilteredOutObjectsHyst │
//        │    Adjusted = 原过滤位图与"在滞后中"位图的差集 → 让正在滞后的对象保 │
//        │    留在 Scope 中。                                                  │
//        └────────────────────────────────────────────────────────────────────┘
//                  │
//                  ▼
//        ┌────────────────────────────────────────────────────────────────────┐
//        │ 8) FilterNonRelevantObjects                                        │
//        │    汇总 BuildAlwaysRelevantList + 每连接 ObjectsInScope            │
//        │    → NetRefHandleManager.RelevantObjectsInternalIndices（全局）   │
//        └────────────────────────────────────────────────────────────────────┘
//
// 交互的核心组件：
//   - FNetRefHandleManager：提供 ScopableInternalIndices / RelevantObjectsInternalIndices；
//   - FNetObjectGroups：组容器与 GroupFilteredOutObjects 全局位图；
//   - FReplicationConnections：连接生灭事件；
//   - FObjectScopeHysteresisUpdater（per-connection）：滞后机制；
//   - 各 UNetObjectFilter 实例：动态过滤的实际工作者。
// =====================================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/Filtering/SharedConnectionFilterStatus.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Iris/ReplicationSystem/Filtering/ObjectScopeHysteresisUpdater.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFilteringConfig.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Net/Core/Connection/ConnectionHandle.h"
#include "Containers/Array.h"
#include "UObject/StrongObjectPtr.h"

class UReplicationFilteringConfig;
class UReplicationSystem;
namespace UE::Net
{
	typedef uint32 FNetObjectFilterHandle;
	namespace Private
	{
		class FNetRefHandleManager;
		class FNetObjectGroups;
		class FReplicationConnections;
		typedef uint32 FInternalNetRefIndex;
	}
}

namespace UE::Net::Private
{

/**
 * Friend 通道：让 UNetObjectFilter::Init 安全访问 ReplicationSystem 中私有 NetObjectFilteringInfos 视图。
 * 仅暴露给 UNetObjectFilter（friend 关系），避免向所有用户公开内部数组。
 */
class FNetObjectFilteringInfoAccessor
{
private:
	/** Returns all the NetObjectFilteringInfos for the filtering system. */
	TArrayView<FNetObjectFilteringInfo> GetNetObjectFilteringInfos(UReplicationSystem* ReplicationSystem) const;

private:
	// Friends
	friend UNetObjectFilter;
};


/** FReplicationFiltering::Init 的入参聚合。 */
struct FReplicationFilteringInitParams
{
	TObjectPtr<UReplicationSystem> ReplicationSystem = nullptr;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	FNetObjectGroups* Groups = nullptr;
	FReplicationConnections* Connections = nullptr;
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;
	uint32 MaxGroupCount = 0;
};

/**
 * FReplicationFiltering —— Filtering 三阶段调度器。
 *
 * 主要职责：
 *   - 维护"对象 ↔ 过滤器"绑定（ObjectIndexToDynamicFilterIndex / ObjectsWithOwnerFilter / AllConnectionFilteredObjects）；
 *   - 维护"对象 ↔ Owning Connection"映射（ObjectIndexToOwningConnection）；
 *   - 维护各类位图：DynamicFilterEnabledObjects / Exclusion/InclusionFilterGroups / SubObjectFilterGroups 等；
 *   - 调度三阶段过滤；
 *   - 处理 hysteresis、AlwaysRelevant/CullNonRelevant 等高级特性；
 *   - 把所有 UNetObjectFilter 的注册与生命周期管理统一收敛到本类。
 */
class FReplicationFiltering
{
public:
	FReplicationFiltering();

	void Init(FReplicationFilteringInitParams& Params);
	void Deinit();

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	/** Called when when one or more NetRefInternalIndices have been freed and can be re-assigned to new objects. */
	/** 索引被回收时调用：清"对象 → OwningConnection"映射等长期持有的状态（避免泄漏到下一个对象）。 */
	void OnInternalNetRefIndicesFreed(const TConstArrayView<FInternalNetRefIndex>& FreedIndices);

	/**
	 * Executes group, owner and connection filtering then any dynamic filters.
	 * At the end any object that is not relevant to at least one connection will be removed from the scoped object list.
	 * Exception to this rule are always relevant (e.g. non-filtered) objects.
	 */
	/**
	 * 主入口：每帧由 FReplicationSystemImpl 在 PreSendUpdate 阶段调用一次。流程见文件头流程图。
	 */
	void Filter();

	/**  Returns the list of objects relevant to a given connection. This represents the global scope list minus the objects that were filtered out for the given connection. */
	/** 连接相关对象位图（全局 Scope - 被该连接过滤掉的对象）。供 ReplicationWriter::UpdateScope 使用。 */
	const FNetBitArrayView GetRelevantObjectsInScope(uint32 ConnectionId) const
	{
		return MakeNetBitArrayView(ConnectionInfos[ConnectionId].ObjectsInScope);
	}

	/** 连接的 Group 排除位图（被 Exclusion Group 过滤掉的对象）。 */
	const FNetBitArrayView GetGroupFilteredOutObjects(uint32 ConnectionId) const
	{
		return MakeNetBitArrayView(ConnectionInfos[ConnectionId].GroupExcludedObjects);
	}

	// Who owns what
	/** 设置对象的 OwningConnection（0 视作"无所有者，谁也不收"）。 */
	void SetOwningConnection(FInternalNetRefIndex ObjectIndex, uint32 ConnectionId);
	/** 取 OwningConnection；若有"脏 owner"待处理则走慢路径解析。 */
	uint32 GetOwningConnection(FInternalNetRefIndex ObjectIndex) const { return !bHasDirtyOwner ? ObjectIndexToOwningConnection[ObjectIndex] : GetOwningConnectionIfDirty(ObjectIndex); }

	/**
	 * Setup an object to be filtered by the passed filter handle.
	 * @param ObjectIndex The object that wants to be filtered
	 * @param Filter The handle to the filter to add the object into
	 * @param FilterConfigProfile Optional profile name that can be used to specialize the filter parameters
	 */
	/**
	 * 把对象绑定到指定 Filter（静态保留 handle 或 dynamic filter handle）。
	 * 同时维护各类位图与 ObjectIndexToDynamicFilterIndex 反查表。返回 false 表示绑定失败（如 Filter 拒绝接管）。
	 */
	bool SetFilter(FInternalNetRefIndex ObjectIndex, FNetObjectFilterHandle Filter, FName FilterConfigProfile);

	/** 
	 * Returns true if the object is part of a filter with the ENetFilterTraits::Spatial trait. 
	 * Indicates that the object is location filtered.
	 */
	/** 用于上层（DependentObject 等）判定该对象是否会被空间裁剪。 */
	bool IsUsingSpatialFilter(FInternalNetRefIndex ObjectIndex) const;

	/** 通过 Filter 名查 handle（含静态保留 + 动态注册）。 */
	FNetObjectFilterHandle GetFilterHandle(const FName FilterName) const;
	/** 通过名称取 Filter 实例（仅 dynamic filter）。 */
	UNetObjectFilter* GetFilter(const FName FilterName) const;

	/** Returns the name of the Filter represented by the handle. */
	FName GetFilterName(FNetObjectFilterHandle Filter) const;

	/** Fill the passed in list with root objects considered always relevant */
	/** 输出"系统级永远相关"的根对象集合（无 OwnerFilter / 无 DynamicFilter / 无 Group 过滤）。 */
	void BuildAlwaysRelevantList(FNetBitArrayView OutAlwaysRelevantList, const FNetBitArrayView ScopeList) const;

	/** Fill the passed in list with objects affected by a given filter*/
	/** 输出"归属指定 Filter 的对象集合"。 */
	void BuildObjectsInFilterList(FNetBitArrayView OutObjectsInFilter, FName FilterName) const;

	// Connection handling
	/** 新连接事件：标记 NewConnections 位图，下一帧 InitNewConnections 处理。 */
	void AddConnection(uint32 ConnectionId);
	/** 连接移除事件：保留位标记，下一帧 ResetRemovedConnections 清理。 */
	void RemoveConnection(uint32 ConnectionId);

	// Group based filtering
	/** 把 Group 标记为 Exclusion 过滤组：调用 NetObjectGroups::AddExclusionFilterTrait 并维护 ExclusionFilterGroups 位图。 */
	bool AddExclusionFilterGroup(FNetObjectGroupHandle GroupHandle);
	/** 同上，Inclusion。 */
	bool AddInclusionFilterGroup(FNetObjectGroupHandle GroupHandle);

	/** 取消任意 Group 过滤 trait（Exclusion 或 Inclusion）。 */
	void RemoveGroupFilter(FNetObjectGroupHandle GroupHandle);
	bool IsExclusionFilterGroup(FNetObjectGroupHandle GroupHandle) const { return GroupHandle.IsValid() && ExclusionFilterGroups.GetBit(GroupHandle.GetGroupIndex()); }
	bool IsInclusionFilterGroup(FNetObjectGroupHandle GroupHandle) const { return GroupHandle.IsValid() && InclusionFilterGroups.GetBit(GroupHandle.GetGroupIndex()); }

	/** 设置 Group 整体过滤状态（对所有连接）。 */
	void SetGroupFilterStatus(FNetObjectGroupHandle GroupHandle, ENetFilterStatus ReplicationStatus);
	/** 设置 Group 在多个连接上的过滤状态（位图入参）。 */
	void SetGroupFilterStatus(FNetObjectGroupHandle GroupHandle, const FNetBitArrayView& ConnectionsBitArray, ENetFilterStatus);
	/** 设置 Group 单连接过滤状态。 */
	void SetGroupFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);
	/** 查询 Group 在某连接的过滤状态。 */
	bool GetGroupFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ConnectionId, ENetFilterStatus& OutReplicationStatus) const;

	/** 对象加入 Group 时的回调（NetObjectGroups 不直接维护过滤器位图，需要通知本调度器）。 */
	void NotifyObjectAddedToGroup(FNetObjectGroupHandle GroupHandle, FInternalNetRefIndex ObjectIndex);
	void NotifyObjectRemovedFromGroup(FNetObjectGroupHandle GroupHandle, FInternalNetRefIndex ObjectIndex);

	/** 依赖对象关系增加时的回调（动态过滤可能因依赖关系而被覆盖）。 */
	void NotifyAddedDependentObject(FInternalNetRefIndex ObjectIndex);
	void NotifyRemovedDependentObject(FInternalNetRefIndex ObjectIndex);
	
	// SubObjectFilter status
	/** 把 Group 注册为 SubObject Filter Group。 */
	void AddSubObjectFilter(FNetObjectGroupHandle GroupHandle);
	void RemoveSubObjectFilter(FNetObjectGroupHandle GroupHandle);
	bool IsSubObjectFilterGroup(FNetObjectGroupHandle GroupHandle) const { return GroupHandle.IsValid() && SubObjectFilterGroups.GetBit(GroupHandle.GetGroupIndex()); }

	/** 设置 SubObject Filter Group 在某连接（含父子）的过滤状态。 */
	void SetSubObjectFilterStatus(FNetObjectGroupHandle GroupHandle, FConnectionHandle ConnectionHandle, ENetFilterStatus ReplicationStatus);
	bool GetSubObjectFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ParentConnectionId, ENetFilterStatus& OutReplicationStatus) const;

	/** Print the filter information we have regarding the passed ObjectIndex and his relation to the passed Connection */
	/** 调试用：把对象与连接的过滤现状（filter / group / hysteresis 等）格式化为字符串。 */
	FString PrintFilterObjectInfo(FInternalNetRefIndex ObjectIndex, uint32 ConnectionId) const;

private:
	/** per-connection 状态：所有按连接维度展开的位图与 hysteresis 都集中于此。 */
	struct FPerConnectionInfo
	{
		void Deinit();

		// Objects filtered depending on owning connection or user set connection filtering
		/** 静态阶段（Owner / Connection Filter）已过滤掉的对象。 */
		FNetBitArray ConnectionFilteredObjects;
		// Objects filtered out due to one or more exclusion groups it belongs to is filtered out
		/** Exclusion Group 阶段已过滤掉的对象。 */
		FNetBitArray GroupExcludedObjects;
		// Connection and group exclusion filtering is assumed to happen seldom. Avoid recalculating from scratch every frame.
		/** "动态过滤之前"对象集合（缓存以降低逐帧开销）。 */
		FNetBitArray ObjectsInScopeBeforeDynamicFiltering;
		// Group inclusion filtering is assumed to happen seldom. Applied after dynamic filtering, but before dependent objects.
		/** Inclusion Group 阶段允许进入 Scope 的对象（覆盖在动态过滤之上、依赖对象之下）。 */
		FNetBitArray GroupIncludedObjects;
		// Objects in scope after all kinds of filtering, including dynamic filtering, has been applied
		/** 最终过滤完成、本帧本连接需要复制的对象集合。GetRelevantObjectsInScope 返回此位图。 */
		FNetBitArray ObjectsInScope;
		// Which objects are filtered out after dynamic filters have been processed.
		/** 动态过滤阶段拒绝的对象（用于后续 hysteresis 调整）。 */
		FNetBitArray DynamicFilteredOutObjects;
		// List of objects currently filtered out after processing dynamic filter passes. This could be temporary allocations in UpdateDynamicFiltering() but does require one bitarray per connection.
		/** 动态过滤批次累积过程中的临时位图（可避免每帧重新分配）。 */
		FNetBitArray InProgressDynamicFilteredOutObjects;
		// Which objects are filtered out after dynamic filters, inclusion groups and hysteresis have been processed.
		/** 经 hysteresis 调整后的"真过滤"位图：原 DynamicFilteredOutObjects 减去仍在滞后的对象。 */
		FNetBitArray DynamicFilteredOutObjectsHysteresisAdjusted;
		// Updater of hysteresis for objects being dynamically filtered out
		/** 本连接的 hysteresis 更新器：被动态过滤的对象在此累计 hysteresis 帧数。 */
		FObjectScopeHysteresisUpdater HysteresisUpdater;
	};

	/** 紧凑的"对象 → 多连接 ID"位存储（柔性数组 = ConnectionIds 是变长的 uint32 数组）。 */
	struct FPerObjectInfo
	{
		// Note: Array is likely larger than one element.
		uint32 ConnectionIds[1];
	};

	static constexpr uint32 UsedPerObjectInfoStorageGrowSize = 32; // 256 bytes, 1024 indices
	typedef uint32 PerObjectInfoIndexType;

	/** 单 Group 的辅助信息（连接位图存储索引）。 */
	struct FPerGroupInfo
	{
		PerObjectInfoIndexType ConnectionStateIndex;
	};

	/** 每个 SubObject Filter Group 的辅助信息（含父子连接共享状态聚合器）。 */
	class FPerSubObjectFilterGroupInfo
	{
	public:
		// Tracks parent and child connection filter status. Necessary for splitscreen support.
		FSharedConnectionFilterStatusCollection ConnectionFilterStatus;
		// Index to storage for parent connection bit array. 
		PerObjectInfoIndexType ConnectionStateIndex = 0;
	};

	/** 单个动态 Filter 的元信息（Filter 实例 + 名字 + 当前接管对象数）。 */
	struct FFilterInfo
	{
		TStrongObjectPtr<UNetObjectFilter> Filter;
		FName Name;
		uint32 ObjectCount = 0;
	};

private:
	class FUpdateDirtyObjectsBatchHelper;
	friend FNetObjectFilteringInfoAccessor;
	friend FPerSubObjectFilterGroupInfo;
	
	/** 静态断言：保证位图分桶尺寸与本模块假设一致。 */
	static void StaticChecks();

	/** 启动期：按 ini 注册创建并初始化所有 Dynamic Filter；分配 NetObjectFilteringInfos。 */
	void InitFilters();
	/** 启动期：根据 Config 决定是否启用 hysteresis（及连接节流参数）。 */
	void InitObjectScopeHysteresis();

	/** 扩位图与映射数组（在 Init / OnMaxInternalNetRefIndexIncreased 中调用）。 */
	void SetNetObjectListsSize(FInternalNetRefIndex MaxInternalIndex);
	void SetPerConnectionListsSize(FPerConnectionInfo& ConnectionInfo, FInternalNetRefIndex NewMaxInternalIndex);

	// 三阶段过滤的细粒度子步骤（详见 .cpp 中的具体实现注释）
	void InitNewConnections();              // 处理本帧新增连接：分配位图、调用 Filter::AddConnection 等
	void ResetRemovedConnections();         // 处理本帧移除连接：清状态、调用 Filter::RemoveConnection 等
	void UpdateObjectsInScope();            // 阶段 0：把全局 ScopeList 拷入 per-conn 缓存
	void UpdateOwnerFiltering();            // 阶段 2：Owner / Connection Filter
	void UpdateGroupExclusionFiltering();   // 阶段 1：Exclusion Group
	void UpdateGroupInclusionFiltering();   // 阶段 6：Inclusion Group（在 UpdateDynamicFiltering 内复用）
	void UpdateSubObjectFilters();          // 阶段 3：SubObject Filter

	void UpdateDynamicFilters();            // 阶段 5 容器：通知脏对象 → PreFilter → Filter → PostFilter
	void PreUpdateDynamicFiltering();
	void UpdateDynamicFiltering();
	void PostUpdateDynamicFiltering();

	void PreUpdateObjectScopeHysteresis();  // 阶段 4：清"被销毁/解除动态过滤"的对象的滞后；连接节流
	void PostUpdateObjectScopeHysteresis(); // 阶段 7：HysteresisAdjusted = 原过滤 - 在滞后中
	void ClearObjectsFromHysteresis();      // 工具：批量清滞后

	/** Build the list of always relevant objects + objects that are currently relevant to at least one connection. */
	/** 阶段 8：把 AlwaysRelevant 列表 + 各连接 ObjectsInScope 的并集写入 RelevantObjectsInternalIndices。 */
	void FilterNonRelevantObjects();

	bool HasDynamicFilters() const;

	/** 处理"创建依赖父对象"的相关性传播（递归把父对象拉入 Scope）。 */
	void UpdateCreationDependentParent(uint32 ChildIndex, const FNetBitArrayView ObjectsWithCreationDependencies, FNetBitArrayView OutConnectionObjectsInScope, bool bIsRecursive) const;

	// Helper to update and reset group exclusion filter effects if objects are removed from a filter or after a filter status change, returns true if the group filter was changed
	bool ClearGroupExclusionFilterEffectsForObject(uint32 ObjectIndex, uint32 ConnectionId);
	// Helper to update and reset group inclusion filter effects if objects are removed from a filter or after a filter status change, returns true if the group filter was changed
	bool ClearGroupInclusionFilterEffectsForObject(uint32 ObjectIndex, uint32 ConnectionId);
	bool HasOwnerFilter(uint32 ObjectIndex) const;

	/** PerObjectInfo 池分配器：基于 UsedPerObjectInfoStorage 位图。 */
	PerObjectInfoIndexType AllocPerObjectInfo();
	void FreePerObjectInfo(PerObjectInfoIndexType Index);

	FPerObjectInfo* AllocPerObjectInfoForObject(uint32 ObjectIndex);
	void FreePerObjectInfoForObject(uint32 ObjectIndex);
	
	FPerObjectInfo* GetPerObjectInfo(PerObjectInfoIndexType Index);
	const FPerObjectInfo* GetPerObjectInfo(PerObjectInfoIndexType Index) const;

	/** 把 PerObjectInfo 中所有连接位写为统一状态（Allow/Disallow），用于 Group 整体过滤状态切换。 */
	void SetPerObjectInfoFilterStatus(FPerObjectInfo& ObjectInfo, ENetFilterStatus ReplicationStatus);

	// SubObjectGroup filtering support
	FPerSubObjectFilterGroupInfo& CreatePerSubObjectGroupFilterInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex);
	void DestroyPerSubObjectGroupFilterInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex);
	FPerSubObjectFilterGroupInfo* GetPerSubObjectFilterGroupInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex);
	const FPerSubObjectFilterGroupInfo* GetPerSubObjectFilterGroupInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex) const;

	ENetFilterStatus GetConnectionFilterStatus(const FPerObjectInfo& ObjectInfo, uint32 ConnectionId) const;
	bool IsAnyConnectionFilterStatusAllowed(const FPerObjectInfo& ObjectInfo) const;
	bool IsAnyConnectionFilterStatusDisallowed(const FPerObjectInfo& ObjectInfo) const;
	void SetConnectionFilterStatus(FPerObjectInfo& ObjectInfo, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);
	// Returns true if object is filtered out by any exclusion group.
	bool IsExcludedByAnyGroup(uint32 ObjectInternalIndex, uint32 ConnectionId) const;
	// Returns true if object is allowed to replicate by any inclusion group.
	bool IsIncludedByAnyGroup(uint32 ObjectInternalIndex, uint32 ConnectionId) const;
	void InternalSetExclusionGroupFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);
	void InternalSetInclusionGroupFilterStatus(FNetObjectGroupHandle GroupHandle, ENetFilterStatus ReplicationStatus);
	void InternalSetInclusionGroupFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);

	uint32 GetOwningConnectionIfDirty(uint32 ObjectIndex) const;

	void RemoveFromDynamicFilter(uint32 ObjectIndex, uint32 FilterIndex);

	/** 通知所有 Filter "本帧脏对象"批次（仅声明 NeedsUpdate trait 的 Filter 收到）。 */
	void NotifyFiltersOfDirtyObjects();
	void BatchNotifyFiltersOfDirtyObjects(FUpdateDirtyObjectsBatchHelper& BatchHelper, const uint32* ObjectIndices, uint32 ObjectCount);

	/** Returns all the filtering infos. */
	TArrayView<FNetObjectFilteringInfo> GetNetObjectFilteringInfos();

	/** 取 Profile 名对应的 hysteresis 帧数（无匹配则用 default）。 */
	uint8 GetObjectScopeHysteresisFrameCount(FName Profile) const;

	bool HasSubObjectInScopeWithFilteredOutRootObject(FNetBitArrayView Objects) const;
	bool HasSubObjectInScopeWithFilteredOutRootObject(uint32 connectionId) const;

private:
	/** Hysteresis 处理模式：编译期开关或运行时关停（如 Config 关闭时）。 */
	enum EHysteresisProcessingMode : uint32
	{
		Disabled,
		Enabled,
	};

	// Scope hysteresis state. Hysteresis is applied to objects going out of scope for objects that so desire.
	/** Filtering 全局 hysteresis 状态（连接级别的滞后器位于 FPerConnectionInfo 内）。 */
	struct FObjectScopeHysteresisState
	{
	public:
		/** 把指定对象从所有连接的 hysteresis 中清除。 */
		void ClearFromHysteresis(FInternalNetRefIndex NetRefIndex);

		// Processing mode
		EHysteresisProcessingMode Mode = EHysteresisProcessingMode::Disabled;
		// Which connection ID to start with for updating.
		/** 节流：本帧从该 ConnectionId 起步；下一帧 +1（mod 总连接数 / Stride）。 */
		uint32 ConnectionStartId = 0;
		// Stride for connection update throttling.
		uint32 ConnectionIdStride = 1;

		// Approximate number of objects that should be cleared from hysteresis.
		uint32 ObjectsToClearCount = 0;

		// Objects to clear from hysteresis due to being destroyed or removed from dynamic filtering.
		/** 因销毁/解除动态过滤需要清除滞后的对象集合。 */
		FNetBitArray ObjectsToClear;

		// Objects that should not be added to hysteresis this frame. Example use case is newly added objects that become filtered out on the first frame.
		/** 不应进入滞后的对象（如首帧就过滤掉的新对象——直接出 scope，不经滞后期）。 */
		FNetBitArray ObjectsExemptFromHysteresis;
	};

	// Used for ObjectIndexToDynamicFilterIndex lookup
	/** ObjectIndexToDynamicFilterIndex 的"无效"哨兵值。 */
	static constexpr uint8 InvalidDynamicFilterIndex = 255U;

	// Config
	/** ReplicationFilteringConfig CDO 的强引用（生命周期内一直持有）。 */
	TStrongObjectPtr<const UReplicationFilteringConfig> Config;

	// General
	TObjectPtr<UReplicationSystem> ReplicationSystem = nullptr;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	uint32 FrameIndex = 0;

	// Groups
	FNetObjectGroups* Groups = nullptr;

	// Connection specifics
	FReplicationConnections* Connections = nullptr;
	/** per-connection 状态数组（索引 = ConnectionId，[0..MaxConn]）。 */
	TArray<FPerConnectionInfo> ConnectionInfos;
	/** 当前有效连接位图。 */
	FNetBitArray ValidConnections;
	/** 本帧新加入但尚未在 InitNewConnections 中处理的连接位图。 */
	FNetBitArray NewConnections;

	// Object specifics
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;
	uint32 WordCountForObjectBitArrays = 0;

	// Filter specifics
	/** OwnerFiltering 阶段需要重新计算的对象（OwningConnection 变化等）。 */
	FNetBitArray ObjectsWithDirtyOwnerFilter;
	/** OwningConnection 设置过但还未应用的对象。 */
	FNetBitArray ObjectsWithDirtyOwner;

	/** 拥有 OwnerFilter（即 ToOwnerFilterHandle）的对象集合。 */
	FNetBitArray ObjectsWithOwnerFilter;

	//$TODO: This should be a on-demand data allocation. Very few objects have an owner and the memory usage would be minimal
	/** 对象 → OwningConnection 的反查表。$TODO 改成按需分配（绝大部分对象无 owner）。 */
	TArray<uint16> ObjectIndexToOwningConnection;

	// For non-owner filtered objects
	// The storage for PerObjectInfo.
	/** PerObjectInfo 的实际存储池（紧凑数组，按 PerObjectInfoStorageCountPerItem 步进）。 */
	TArray<uint32> PerObjectInfoStorage;
	// Storage for bit array indicating used/free status of PerObjectInfos
	/** PerObjectInfo 槽位占用位图。 */
	TArray<uint32> UsedPerObjectInfoStorage;

	/** 拥有独立 PerObjectInfo（即"逐连接控制"过滤）的对象集合。 */
	FNetBitArray ObjectsWithPerObjectInfo;

	// Groups
	/** 每 Group 一份的辅助信息（按 Group Index 索引，与 NetObjectGroups 的 Index 一一对应）。 */
	TArray<FPerGroupInfo> GroupInfos;
	uint32 MaxGroupCount = 0;

	// SubObject filter groups
	/** SubObject Filter Group 的辅助信息字典（按 GroupIndex 索引）。 */
	TMap<uint32, FPerSubObjectFilterGroupInfo> SubObjectFilterGroupInfos;

	// Hysteresis frame counts for dynamically filtered objects
	/** 每对象的 hysteresis 帧数（来自 Filter Profile 或 default）。0 表示无滞后。 */
	TArray<uint8> ObjectScopeHysteresisFrameCounts;

	/** NetObjectGroups used for filtering out objects. */
	/** "本系统视角下"被注册为 Exclusion 过滤组的 GroupIndex 位图（按 GroupIndex 索引）。 */
	FNetBitArray ExclusionFilterGroups;

	/** NetObjectGroups used to allow allowing replication of dynamically filtered out objects. */
	/** Inclusion 过滤组位图。 */
	FNetBitArray InclusionFilterGroups;

	/** Exclusion filtering groups with newly added members and that need to filter out objects for at least one connection. */
	FNetBitArray DirtyExclusionFilterGroups;

	/** Inclusion filtering groups with newly added members and that need to include objects for at least one connection. */
	FNetBitArray DirtyInclusionFilterGroups;

	// Group indices which are subobject filter groups
	FNetBitArray SubObjectFilterGroups;
	// Group indices which are subobject filter groups and in need of updating
	FNetBitArray DirtySubObjectFilterGroups;
	// Object indices with a connection filter
	/** 拥有"连接过滤"（ConnectionFilterHandle 等）的对象集合。 */
	FNetBitArray AllConnectionFilteredObjects;

	/** 对象 → PerObjectInfo 索引的映射表（0 表示无）。 */
	TArray<PerObjectInfoIndexType> ObjectIndexToPerObjectInfoIndex;
	uint32 PerObjectInfoStorageCountForConnections = 0;
	// How many elements from UsedPerObjectInfoStorage is needed to hold a FPerObjectInfo
	uint32 PerObjectInfoStorageCountPerItem = 0;

	// Dynamic filters 
	/** per-object 的 FilteringInfo 数组（被 friend Accessor 暴露给各 Filter）。 */
	TArray<FNetObjectFilteringInfo> NetObjectFilteringInfos;
	/** 对象 → 接管它的 dynamic filter index（InvalidDynamicFilterIndex=未接管）。 */
	TArray<uint8> ObjectIndexToDynamicFilterIndex;
	/** 所有已注册的 dynamic Filter 元信息表。 */
	TArray<FFilterInfo> DynamicFilterInfos;

	/** "已被某 Dynamic Filter 接管"的对象位图。 */
	FNetBitArray DynamicFilterEnabledObjects;
	/** 本帧需要通知 dynamic filter 做 UpdateObjects 的脏对象集合。 */
	FNetBitArray ObjectsRequiringDynamicFilterUpdate;

	// Object scope hystereris
	FObjectScopeHysteresisState HysteresisState;

	// 各种"是否有脏数据/状态"的快速短路标志（按位字段节省 cache 空间）
	uint32 bHasNewConnection : 1;
	uint32 bHasRemovedConnection : 1;
	uint32 bHasDirtyOwnerFilter: 1;
	uint32 bHasDirtyOwner : 1;
	uint32 bHasDynamicFilters : 1;
	uint32 bHasDirtyExclusionFilterGroup : 1;
	uint32 bHasDirtyInclusionFilterGroup : 1;
	// Is true if any initialized DynamicFilter has the NeedsUpdate trait
	uint32 bHasDynamicFiltersWithUpdateTrait : 1;
};

inline bool FReplicationFiltering::HasDynamicFilters() const
{
	return bHasDynamicFilters;
}

}

private:
	struct FPerConnectionInfo
	{
		void Deinit();

		// Objects filtered depending on owning connection or user set connection filtering
		FNetBitArray ConnectionFilteredObjects;
		// Objects filtered out due to one or more exclusion groups it belongs to is filtered out
		FNetBitArray GroupExcludedObjects;
		// Connection and group exclusion filtering is assumed to happen seldom. Avoid recalculating from scratch every frame.
		FNetBitArray ObjectsInScopeBeforeDynamicFiltering;
		// Group inclusion filtering is assumed to happen seldom. Applied after dynamic filtering, but before dependent objects.
		FNetBitArray GroupIncludedObjects;
		// Objects in scope after all kinds of filtering, including dynamic filtering, has been applied
		FNetBitArray ObjectsInScope;
		// Which objects are filtered out after dynamic filters have been processed.
		FNetBitArray DynamicFilteredOutObjects;
		// List of objects currently filtered out after processing dynamic filter passes. This could be temporary allocations in UpdateDynamicFiltering() but does require one bitarray per connection.
		FNetBitArray InProgressDynamicFilteredOutObjects;
		// Which objects are filtered out after dynamic filters, inclusion groups and hysteresis have been processed.
		FNetBitArray DynamicFilteredOutObjectsHysteresisAdjusted;
		// Updater of hysteresis for objects being dynamically filtered out
		FObjectScopeHysteresisUpdater HysteresisUpdater;
	};

	struct FPerObjectInfo
	{
		// Note: Array is likely larger than one element.
		uint32 ConnectionIds[1];
	};

	static constexpr uint32 UsedPerObjectInfoStorageGrowSize = 32; // 256 bytes, 1024 indices
	typedef uint32 PerObjectInfoIndexType;

	struct FPerGroupInfo
	{
		PerObjectInfoIndexType ConnectionStateIndex;
	};

	class FPerSubObjectFilterGroupInfo
	{
	public:
		// Tracks parent and child connection filter status. Necessary for splitscreen support.
		FSharedConnectionFilterStatusCollection ConnectionFilterStatus;
		// Index to storage for parent connection bit array. 
		PerObjectInfoIndexType ConnectionStateIndex = 0;
	};

	struct FFilterInfo
	{
		TStrongObjectPtr<UNetObjectFilter> Filter;
		FName Name;
		uint32 ObjectCount = 0;
	};

private:
	class FUpdateDirtyObjectsBatchHelper;
	friend FNetObjectFilteringInfoAccessor;
	friend FPerSubObjectFilterGroupInfo;
	
	static void StaticChecks();

	void InitFilters();
	void InitObjectScopeHysteresis();

	void SetNetObjectListsSize(FInternalNetRefIndex MaxInternalIndex);
	void SetPerConnectionListsSize(FPerConnectionInfo& ConnectionInfo, FInternalNetRefIndex NewMaxInternalIndex);

	void InitNewConnections();
	void ResetRemovedConnections();
	void UpdateObjectsInScope();
	void UpdateOwnerFiltering();
	void UpdateGroupExclusionFiltering();
	void UpdateGroupInclusionFiltering();
	void UpdateSubObjectFilters();

	void UpdateDynamicFilters();
	void PreUpdateDynamicFiltering();
	void UpdateDynamicFiltering();
	void PostUpdateDynamicFiltering();

	void PreUpdateObjectScopeHysteresis();
	void PostUpdateObjectScopeHysteresis();
	void ClearObjectsFromHysteresis();

	/** Build the list of always relevant objects + objects that are currently relevant to at least one connection. */
	void FilterNonRelevantObjects();

	bool HasDynamicFilters() const;

	void UpdateCreationDependentParent(uint32 ChildIndex, const FNetBitArrayView ObjectsWithCreationDependencies, FNetBitArrayView OutConnectionObjectsInScope, bool bIsRecursive) const;

	// Helper to update and reset group exclusion filter effects if objects are removed from a filter or after a filter status change, returns true if the group filter was changed
	bool ClearGroupExclusionFilterEffectsForObject(uint32 ObjectIndex, uint32 ConnectionId);
	// Helper to update and reset group inclusion filter effects if objects are removed from a filter or after a filter status change, returns true if the group filter was changed
	bool ClearGroupInclusionFilterEffectsForObject(uint32 ObjectIndex, uint32 ConnectionId);
	bool HasOwnerFilter(uint32 ObjectIndex) const;

	PerObjectInfoIndexType AllocPerObjectInfo();
	void FreePerObjectInfo(PerObjectInfoIndexType Index);

	FPerObjectInfo* AllocPerObjectInfoForObject(uint32 ObjectIndex);
	void FreePerObjectInfoForObject(uint32 ObjectIndex);
	
	FPerObjectInfo* GetPerObjectInfo(PerObjectInfoIndexType Index);
	const FPerObjectInfo* GetPerObjectInfo(PerObjectInfoIndexType Index) const;

	void SetPerObjectInfoFilterStatus(FPerObjectInfo& ObjectInfo, ENetFilterStatus ReplicationStatus);

	// SubObjectGroup filtering support
	FPerSubObjectFilterGroupInfo& CreatePerSubObjectGroupFilterInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex);
	void DestroyPerSubObjectGroupFilterInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex);
	FPerSubObjectFilterGroupInfo* GetPerSubObjectFilterGroupInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex);
	const FPerSubObjectFilterGroupInfo* GetPerSubObjectFilterGroupInfo(FNetObjectGroupHandle::FGroupIndexType GroupIndex) const;

	ENetFilterStatus GetConnectionFilterStatus(const FPerObjectInfo& ObjectInfo, uint32 ConnectionId) const;
	bool IsAnyConnectionFilterStatusAllowed(const FPerObjectInfo& ObjectInfo) const;
	bool IsAnyConnectionFilterStatusDisallowed(const FPerObjectInfo& ObjectInfo) const;
	void SetConnectionFilterStatus(FPerObjectInfo& ObjectInfo, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);
	// Returns true if object is filtered out by any exclusion group.
	bool IsExcludedByAnyGroup(uint32 ObjectInternalIndex, uint32 ConnectionId) const;
	// Returns true if object is allowed to replicate by any inclusion group.
	bool IsIncludedByAnyGroup(uint32 ObjectInternalIndex, uint32 ConnectionId) const;
	void InternalSetExclusionGroupFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);
	void InternalSetInclusionGroupFilterStatus(FNetObjectGroupHandle GroupHandle, ENetFilterStatus ReplicationStatus);
	void InternalSetInclusionGroupFilterStatus(FNetObjectGroupHandle GroupHandle, uint32 ConnectionId, ENetFilterStatus ReplicationStatus);

	uint32 GetOwningConnectionIfDirty(uint32 ObjectIndex) const;

	void RemoveFromDynamicFilter(uint32 ObjectIndex, uint32 FilterIndex);

	void NotifyFiltersOfDirtyObjects();
	void BatchNotifyFiltersOfDirtyObjects(FUpdateDirtyObjectsBatchHelper& BatchHelper, const uint32* ObjectIndices, uint32 ObjectCount);

	/** Returns all the filtering infos. */
	TArrayView<FNetObjectFilteringInfo> GetNetObjectFilteringInfos();

	uint8 GetObjectScopeHysteresisFrameCount(FName Profile) const;

	bool HasSubObjectInScopeWithFilteredOutRootObject(FNetBitArrayView Objects) const;
	bool HasSubObjectInScopeWithFilteredOutRootObject(uint32 connectionId) const;

private:
	enum EHysteresisProcessingMode : uint32
	{
		Disabled,
		Enabled,
	};

	// Scope hysteresis state. Hysteresis is applied to objects going out of scope for objects that so desire.
	struct FObjectScopeHysteresisState
	{
	public:
		void ClearFromHysteresis(FInternalNetRefIndex NetRefIndex);

		// Processing mode
		EHysteresisProcessingMode Mode = EHysteresisProcessingMode::Disabled;
		// Which connection ID to start with for updating.
		uint32 ConnectionStartId = 0;
		// Stride for connection update throttling.
		uint32 ConnectionIdStride = 1;

		// Approximate number of objects that should be cleared from hysteresis.
		uint32 ObjectsToClearCount = 0;

		// Objects to clear from hysteresis due to being destroyed or removed from dynamic filtering.
		FNetBitArray ObjectsToClear;

		// Objects that should not be added to hysteresis this frame. Example use case is newly added objects that become filtered out on the first frame.
		FNetBitArray ObjectsExemptFromHysteresis;
	};

	// Used for ObjectIndexToDynamicFilterIndex lookup
	static constexpr uint8 InvalidDynamicFilterIndex = 255U;

	// Config
	TStrongObjectPtr<const UReplicationFilteringConfig> Config;

	// General
	TObjectPtr<UReplicationSystem> ReplicationSystem = nullptr;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	uint32 FrameIndex = 0;

	// Groups
	FNetObjectGroups* Groups = nullptr;

	// Connection specifics
	FReplicationConnections* Connections = nullptr;
	TArray<FPerConnectionInfo> ConnectionInfos;
	FNetBitArray ValidConnections;
	FNetBitArray NewConnections;

	// Object specifics
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;
	uint32 WordCountForObjectBitArrays = 0;

	// Filter specifics
	FNetBitArray ObjectsWithDirtyOwnerFilter;
	FNetBitArray ObjectsWithDirtyOwner;

	FNetBitArray ObjectsWithOwnerFilter;

	//$TODO: This should be a on-demand data allocation. Very few objects have an owner and the memory usage would be minimal
	TArray<uint16> ObjectIndexToOwningConnection;

	// For non-owner filtered objects
	// The storage for PerObjectInfo.
	TArray<uint32> PerObjectInfoStorage;
	// Storage for bit array indicating used/free status of PerObjectInfos
	TArray<uint32> UsedPerObjectInfoStorage;

	FNetBitArray ObjectsWithPerObjectInfo;

	// Groups
	TArray<FPerGroupInfo> GroupInfos;
	uint32 MaxGroupCount = 0;

	// SubObject filter groups
	TMap<uint32, FPerSubObjectFilterGroupInfo> SubObjectFilterGroupInfos;

	// Hysteresis frame counts for dynamically filtered objects
	TArray<uint8> ObjectScopeHysteresisFrameCounts;

	/** NetObjectGroups used for filtering out objects. */
	FNetBitArray ExclusionFilterGroups;

	/** NetObjectGroups used to allow allowing replication of dynamically filtered out objects. */
	FNetBitArray InclusionFilterGroups;

	/** Exclusion filtering groups with newly added members and that need to filter out objects for at least one connection. */
	FNetBitArray DirtyExclusionFilterGroups;

	/** Inclusion filtering groups with newly added members and that need to include objects for at least one connection. */
	FNetBitArray DirtyInclusionFilterGroups;

	// Group indices which are subobject filter groups
	FNetBitArray SubObjectFilterGroups;
	// Group indices which are subobject filter groups and in need of updating
	FNetBitArray DirtySubObjectFilterGroups;
	// Object indices with a connection filter
	FNetBitArray AllConnectionFilteredObjects;

	TArray<PerObjectInfoIndexType> ObjectIndexToPerObjectInfoIndex;
	uint32 PerObjectInfoStorageCountForConnections = 0;
	// How many elements from UsedPerObjectInfoStorage is needed to hold a FPerObjectInfo
	uint32 PerObjectInfoStorageCountPerItem = 0;

	// Dynamic filters 
	TArray<FNetObjectFilteringInfo> NetObjectFilteringInfos;
	TArray<uint8> ObjectIndexToDynamicFilterIndex;
	TArray<FFilterInfo> DynamicFilterInfos;

	FNetBitArray DynamicFilterEnabledObjects;
	FNetBitArray ObjectsRequiringDynamicFilterUpdate;

	// Object scope hystereris
	FObjectScopeHysteresisState HysteresisState;

	uint32 bHasNewConnection : 1;
	uint32 bHasRemovedConnection : 1;
	uint32 bHasDirtyOwnerFilter: 1;
	uint32 bHasDirtyOwner : 1;
	uint32 bHasDynamicFilters : 1;
	uint32 bHasDirtyExclusionFilterGroup : 1;
	uint32 bHasDirtyInclusionFilterGroup : 1;
	// Is true if any initialized DynamicFilter has the NeedsUpdate trait
	uint32 bHasDynamicFiltersWithUpdateTrait : 1;
};

inline bool FReplicationFiltering::HasDynamicFilters() const
{
	return bHasDynamicFilters;
}

}
