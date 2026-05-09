// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectGroups.h —— "对象组（Group）"基础容器：复制系统中按命名分组管理对象，提供过滤等高层语义所需的成员维护。
// ---------------------------------------------------------------------------------------------------------------------
// 作用：
//   FNetObjectGroups 是 Filtering / SubObjectFilter 三阶段过滤的"组层"基础。它管理：
//     - 一组 FNetObjectGroup（每组：成员索引列表 + 名字 + GroupId + traits）；
//     - 每对象的"组成员关系"（GroupMemberships，单对象通常属于少数 Group，TInlineAllocator<2>）；
//     - 全局聚合位图 GroupFilteredOutObjects：包含"任何处于 Filter Group 中的对象"——FReplicationFiltering
//       可以用它快速判定某对象是否需走 Group 过滤路径。
//
// 三种"特殊保留 Group"（在更上层 ReplicationSystem 创建并固定）：
//   - NotReplicatedNetObjectGroup    —— 直接禁止复制；
//   - NetGroupOwnerNetObjectGroup    —— 仅向 Owner 复制；
//   - NetGroupReplayNetObjectGroup   —— 与 Replay 挂钩。
// 这些 Group 由 ReplicationSystem 持有 handle 指针并初始化时创建，详见 ReplicationSystem 文档与 §6.1。
//
// 三种 Filter trait（按位组合）：
//   - IsExclusionFiltering —— 排除组：组内对象默认对所有连接 Disallow，可逐连接 Allow；
//   - IsInclusionFiltering —— 纳入组：组内对象默认对所有连接 Disallow，按需 Allow（用于 FilterOut + Inclusion）；
//   - SubObject Filter（traits 不存于 Group，存于 FReplicationFiltering 的 SubObjectFilterGroups 位图中）。
//
// FNetObjectGroupHandle：3 字段 = (Index, Epoch, UniqueId)
//   - Index：在稀疏数组 Groups 中的位置；
//   - Epoch：标识"哪个 ReplicationSystem 实例"创建（PIE 下可能并存多个 RS 实例）；
//   - UniqueId：分配序号（GroupId），防止 Index 复用导致 Stale Handle 误用。
// =====================================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/SparseArray.h"
#include "UObject/NameTypes.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"

namespace UE::Net
{
	namespace Private
	{
		typedef uint32 FInternalNetRefIndex;

		class FNetRefHandleManager;
	}
}

namespace UE::Net::Private
{

/** Group 的"过滤特征"位（按位组合）。SubObject Filter 标志由 FReplicationFiltering 单独维护。 */
enum class ENetObjectGroupTraits : uint32
{
	None                 = 0x0000,
	/** 排除组：成员默认 Disallow 全连接，可逐连接 Allow。 */
	IsExclusionFiltering = 0x0001,
	/** 纳入组：与 FilterOutNetObjectFilter 配合，默认 Disallow，按需 Allow。 */
	IsInclusionFiltering = 0x0002,
};
ENUM_CLASS_FLAGS(ENetObjectGroupTraits);

/** 单个 Group 的核心数据。 */
struct FNetObjectGroup
{
	// Group members can only be replicated objects that have internal indices
	/** 组成员（按 InternalNetRefIndex）。仅"已注册到 NetRefHandleManager"的对象可加入。 */
	TArray<FInternalNetRefIndex> Members;
	/** 组名（唯一）。NAME_None 时由 CreateGroup 自动生成。 */
	FName GroupName;
	/** 组的全局唯一 ID（与 Handle.UniqueId 配对校验，防止 Index 复用引发 Stale Handle 命中）。 */
	uint32 GroupId = 0U;
	/** Filter traits（按位组合 ENetObjectGroupTraits）。 */
	ENetObjectGroupTraits Traits = ENetObjectGroupTraits::None;
};

/** FNetObjectGroups::Init 入参。 */
struct FNetObjectGroupInitParams
{
	/** 用于打印对象描述（仅在 verbose 日志中使用）。 */
	FNetRefHandleManager* NetRefHandleManager = nullptr;
	/** 当前对象索引上限：决定 GroupMemberships 数组与 GroupFilteredOutObjects 位图大小。 */
	uint32 MaxInternalNetRefIndex = 0U;
	/** Group 数量上限。 */
	uint32 MaxGroupCount = 0U;
};

/** FNetObjectGroups —— Group 容器主类。 */
class FNetObjectGroups
{
	UE_NONCOPYABLE(FNetObjectGroups)

public:
	FNetObjectGroups();
	~FNetObjectGroups();

	void Init(const FNetObjectGroupInitParams& Params);

	/** 创建 Group：传 NAME_None 自动生成 "NetObjectGroup<N>" 名字；返回包含 (Index, Epoch, UniqueId) 的 handle。 */
	FNetObjectGroupHandle CreateGroup(FName GroupName);
	/** 销毁 Group：先 ClearGroup（解除成员关系并清 GroupFilteredOutObjects 位），再从稀疏数组移除条目。 */
	void DestroyGroup(FNetObjectGroupHandle GroupHandle);

	/** 仅清空成员（保留 Group 自身），用于复用或大批量重建。 */
	void ClearGroup(FNetObjectGroupHandle GroupHandle);

	/** 按名称查找 Group handle（线性扫描）；找不到返回无效 handle。 */
	FNetObjectGroupHandle FindGroupHandle(FName GroupName) const;
	
	/** Handle → Group 指针（含 Epoch + UniqueId 校验）。 */
	const FNetObjectGroup* GetGroup(FNetObjectGroupHandle GroupHandle) const;
	FNetObjectGroup* GetGroup(FNetObjectGroupHandle GroupHandle);
	
	/** 仅按 Index 取 Group（不做 Epoch/UniqueId 校验，仅用于位图迭代等"已确认有效"的内部场景）。 */
	const FNetObjectGroup* GetGroupFromIndex(FNetObjectGroupHandle::FGroupIndexType GroupIndex) const;
	FNetObjectGroup* GetGroupFromIndex(FNetObjectGroupHandle::FGroupIndexType GroupIndex);

	/** Group* → Handle 反查（用 SparseArray 的 PointerToIndex）。 */
	FNetObjectGroupHandle GetHandleFromGroup(const FNetObjectGroup* InGroup) const;
	/** Index → Handle（含 Epoch / UniqueId 填充）。 */
	FNetObjectGroupHandle GetHandleFromIndex(FNetObjectGroupHandle::FGroupIndexType GroupIndex) const;

	inline FName GetGroupName(FNetObjectGroupHandle GroupHandle) const;
	inline FString GetGroupNameString(FNetObjectGroupHandle GroupHandle) const;

	bool IsValidGroup(FNetObjectGroupHandle GroupHandle) const;

	bool Contains(FNetObjectGroupHandle GroupHandle, FInternalNetRefIndex InternalIndex) const;
	/** 加入：去重，并在该 Group 是 Filter 类型时把对象的 GroupFilteredOutObjects 位置 1。 */
	void AddToGroup(FNetObjectGroupHandle GroupHandle, FInternalNetRefIndex InternalIndex);
	/** 移出：若移出后对象不再属于任何 Filter Group，清除 GroupFilteredOutObjects 位。 */
	void RemoveFromGroup(FNetObjectGroupHandle GroupHandle, FInternalNetRefIndex InternalIndex);

	/** Called when a group is to be used as an exclusion filter group */
	/** 把 Group 标记为 Exclusion 过滤组：附加 IsExclusionFiltering trait，并把所有现成员加入 GroupFilteredOutObjects。 */
	void AddExclusionFilterTrait(FNetObjectGroupHandle GroupHandle);

	/** Called when a group is no longer used as an exclusion filter group */
	/** 取消 Exclusion trait：清除 trait，并对成员逐个判断是否还属于其它 Filter Group，必要时清 GroupFilteredOutObjects 位。 */
	void RemoveExclusionFilterTrait(FNetObjectGroupHandle GroupHandle);

	/** Called when a group is to be used as an inclusion filter group */
	/** 把 Group 标记为 Inclusion 过滤组（不可与 Exclusion 同时存在）。 */
	void AddInclusionFilterTrait(FNetObjectGroupHandle GroupHandle);

	/** Called when a group is no longer used as an inclusion filter group */
	/** 取消 Inclusion trait（实现简单——直接位与，无 GroupFilteredOutObjects 维护）。 */
	void RemoveInclusionFilterTrait(FNetObjectGroupHandle GroupHandle);

	/** Does the group have a filter trait, either exclusion or inclusion */
	bool IsFilterGroup(FNetObjectGroupHandle GroupHandle) const;

	/** Does the group have the exclusion filter trait */
	bool IsExclusionFilterGroup(FNetObjectGroupHandle GroupHandle) const;

	/** Does the group have the inclusion filter trait */
	bool IsInclusionFilterGroup(FNetObjectGroupHandle GroupHandle) const;

	/** Get a reference to the indexes of all groups that the NetObject is a member of */
	/** 取对象所属的所有 Group 的 Index 视图（零拷贝；元素由 TInlineAllocator<2> 内联）。 */
	const TArrayView<const FNetObjectGroupHandle::FGroupIndexType> GetGroupIndexesOfNetObject(FInternalNetRefIndex InternalIndex) const;

	/** Get a list of all group handles the NetObject is a member of */
	/** 取对象所属的所有 Group 的 Handle 列表（含 Epoch + UniqueId）。 */
	void GetGroupHandlesOfNetObject(FInternalNetRefIndex InternalIndex, TArray<FNetObjectGroupHandle>& OutHandles) const;

	/** Returns a list of all objects currently part of a group with the filter trait */
	/** 全局位图视图："至少属于一个 Filter 类型 Group"的对象集合。FReplicationFiltering 用它做快速短路。 */
	const FNetBitArrayView GetGroupFilteredOutObjects() const { return MakeNetBitArrayView(GroupFilteredOutObjects); }

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	/** 索引上限扩容回调：扩 GroupMemberships + GroupFilteredOutObjects。 */
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

private:
	/**
	 * 单对象的"所属 Group 索引集"。每对象通常属于 0~2 个 Group，超过 2 时自动堆分配。
	 * 仅存 Index（GroupHandle 中的 16-bit 段），节省内存；通过 GetGroupFromIndex 配 Epoch 校验访问。
	 */
	struct FNetObjectGroupMembership
	{
	private:
		enum { NumInlinedGroupHandles = 2 };
		/** The indexes of the groups the netobject is a member of. */
		TArray<FNetObjectGroupHandle::FGroupIndexType, TInlineAllocator<NumInlinedGroupHandles>> GroupIndexes;

	public:

		bool ContainsMembership(FNetObjectGroupHandle InGroupHandle) const	{ return GroupIndexes.Contains(InGroupHandle.GetGroupIndex()); }
		void AddMembership(FNetObjectGroupHandle InGroupHandle)		{ GroupIndexes.Add(InGroupHandle.GetGroupIndex()); }
		/** RemoveSingleSwap：O(1) 不保序删除，性能优先。 */
		void RemoveMembership(FNetObjectGroupHandle InGroupHandle)	{ GroupIndexes.RemoveSingleSwap(InGroupHandle.GetGroupIndex()); }
		void ResetMemberships()										{ GroupIndexes.Reset(); }
		int32 NumMemberships() const								{ return GroupIndexes.Num(); }

		const TArrayView<const FNetObjectGroupHandle::FGroupIndexType> GetGroupIndexes() const { return MakeArrayView(GroupIndexes.GetData(), GroupIndexes.Num()); }
	};

	/** 把 Group handle 加入 Membership（去重）。 */
	static bool AddGroupMembership(FNetObjectGroupMembership& Target, FNetObjectGroupHandle Group);
	static void ResetGroupMembership(FNetObjectGroupMembership& Target);
	static bool IsMemberOf(const FNetObjectGroupMembership& Target, FNetObjectGroupHandle Group);

	/** Group& 上的轻量 trait 判定（避免 Handle 校验开销，仅内部使用）。 */
	bool IsFilterGroup(const FNetObjectGroup& Group) const;
	bool IsExclusionFilterGroup(const FNetObjectGroup& Group) const;
	bool IsInclusionFilterGroup(const FNetObjectGroup& Group) const;

	/** 对象是否属于"任意 Filter Group"（用于 RemoveFromGroup 判定是否清 GroupFilteredOutObjects 位）。 */
	bool IsInAnyFilterGroup(const FNetObjectGroupMembership& GroupMembership) const;

	/** SparseArray 指针 → 索引。 */
	const FNetObjectGroupHandle::FGroupIndexType GetIndexFromGroup(const FNetObjectGroup* InGroup) const;

private:

	FNetRefHandleManager* NetRefHandleManager = nullptr;

	// Group usage pattern should not be high frequency so memory layout should not be a major concern
	/** Group 主存：稀疏数组（保留索引稳定性，便于 SubObjectFilterGroups 之类位图按 Index 索引）。 */
	TSparseArray<FNetObjectGroup> Groups;

	// Track what groups each internal handle is a member of, we can tighten this up a bit if needed
	/** 反向索引：对象 → 所属 Group Index 列表。数组大小 = MaxInternalNetRefIndex。 */
	TArray<FNetObjectGroupMembership> GroupMemberships;
	
	// Maximum number of groups that can be exist at once
	uint32 MaxGroupCount = 0U;

	// Index to use for groups with auto-generated names
	/** 自动生成名字的递增计数器（FName::FName(BaseName, Number)）。 */
	int32 AutogeneratedGroupNameId = 0;

	// List of objects that are members of a group with a filter trait
	/** 全局位图：当前归属"任一 Filter Group"的对象集合。AddToGroup / Remove*FilterTrait 时维护。 */
	FNetBitArray GroupFilteredOutObjects;

	// Identifies the ReplicationSystem the group handles were created by
	/** 当前实例的 Epoch（构造时自增 NextEpoch & EpochMask）。区分 PIE 下多 RS 实例的 handle。 */
	FNetObjectGroupHandle::FGroupIndexType CurrentEpoch = 0U;

	// Unique Id assigned to each group handle
	/** UniqueId 分配器（递增；溢出会 ensure 报错）。 */
	uint32 NextGroupUniqueId = 1U;
};

/** Handle 有效性：Index 在稀疏数组中有效 + Epoch 匹配 + UniqueId 与组当前 GroupId 一致。 */
inline bool FNetObjectGroups::IsValidGroup(FNetObjectGroupHandle GroupHandle) const
{
	const bool bGroupIndexExists = GroupHandle.IsValid() && GroupHandle.Epoch == CurrentEpoch && Groups.IsValidIndex(GroupHandle.Index);
	return bGroupIndexExists && Groups[GroupHandle.Index].GroupId == GroupHandle.UniqueId;
}

inline bool FNetObjectGroups::IsFilterGroup(const FNetObjectGroup& Group) const
{
	return EnumHasAnyFlags(Group.Traits, ENetObjectGroupTraits::IsExclusionFiltering | ENetObjectGroupTraits::IsInclusionFiltering);
}

inline bool FNetObjectGroups::IsExclusionFilterGroup(const FNetObjectGroup& Group) const
{
	return EnumHasAnyFlags(Group.Traits, ENetObjectGroupTraits::IsExclusionFiltering);
}

inline bool FNetObjectGroups::IsInclusionFilterGroup(const FNetObjectGroup& Group) const
{
	return EnumHasAnyFlags(Group.Traits, ENetObjectGroupTraits::IsInclusionFiltering);
}

/** 线性扫描查找：Group 数量通常较小，未维护 name → handle 索引。 */
inline FNetObjectGroupHandle FNetObjectGroups::FindGroupHandle(FName InGroupName) const
{
	for (const FNetObjectGroup& Group : Groups)
	{
		if (Group.GroupName == InGroupName)
		{
			const int32 Index = GetIndexFromGroup(&Group);
			return FNetObjectGroupHandle((FNetObjectGroupHandle::FGroupIndexType)Index, CurrentEpoch, Group.GroupId);
		}
	}

	return FNetObjectGroupHandle();
}

inline FNetObjectGroupHandle FNetObjectGroups::GetHandleFromIndex(FNetObjectGroupHandle::FGroupIndexType GroupIndex) const
{
	const FNetObjectGroup* Group = GetGroupFromIndex(GroupIndex);
	return Group ? FNetObjectGroupHandle(GroupIndex, CurrentEpoch, Group->GroupId) : FNetObjectGroupHandle();
}

inline const FNetObjectGroupHandle::FGroupIndexType FNetObjectGroups::GetIndexFromGroup(const FNetObjectGroup* InGroup) const
{
	check(InGroup);
	return (FNetObjectGroupHandle::FGroupIndexType)Groups.PointerToIndex(InGroup);
}

inline FName FNetObjectGroups::GetGroupName(FNetObjectGroupHandle GroupHandle) const
{
	if (const FNetObjectGroup* Group = GetGroup(GroupHandle))
	{
		return Group->GroupName;
	}

	return FName();
}

inline FString FNetObjectGroups::GetGroupNameString(FNetObjectGroupHandle GroupHandle) const
{
	return GetGroupName(GroupHandle).ToString();
}

} // end namespace UE::Net::Private
