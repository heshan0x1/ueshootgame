// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetObjectGroupHandle.h
// 角色：Iris "对象分组（Group）"的不透明句柄类型。
//
// 设计：
//   * 64-bit 紧凑布局：低 32-bit 拆为 24-bit Index + 8-bit Epoch；高 32-bit 为 UniqueId。
//   * Index   : 在 FNetObjectGroups 容器中的位置（最多 2^24 ≈ 1670 万个 Group）。
//   * Epoch   : 8-bit "代次"，Group 销毁后 slot 复用时 Epoch++，防止"悬挂句柄"误命中。
//   * UniqueId: 全局唯一 ID（用于跨进程 / Trace 比对）。
//
// 保留 Group：以下索引固定占用，调用者无法分配到这些 Index：
//   * NotReplicatedNetObjectGroupIndex  : 完全屏蔽（任何连接都不复制）
//   * NetGroupOwnerNetObjectGroupIndex  : SubObject 仅复制给 RootParent 的 Owner
//   * NetGroupReplayNetObjectGroupIndex : Replay 录制条件下才复制
//
// 与 Filtering 子系统协作：
//   * Exclusion Group : 默认禁止该组中所有对象向连接复制
//   * Inclusion Group : 在 Dynamic Filter 之后给一组连接强制开启复制（覆盖 dynamic 结果）
//   * SubObjectFilter : 针对 SubObject 的细分过滤
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Iris/IrisConfig.h"
#include "Containers/StringFwd.h"
#include "Templates/TypeHash.h"

// Forward declarations
class FString;

namespace UE::Net::Private
{
	class FNetObjectGroups;
}

namespace UE::Net
{

/**
 * Group 的不透明句柄。
 * 所有 SetGroupFilterStatus / AddToGroup / RemoveFromGroup 等 API 都接收此类型。
 */
class FNetObjectGroupHandle
{
public:
	typedef uint32 FGroupIndexType;

	enum { GroupIndexBits = 24U };                 // Index 占用位数
	enum { EpochBits = 8U };                       // Epoch 占用位数
	enum { EpochMask = (1U << EpochBits) - 1U };
	enum { MaxGroupIndexCount = 1U << GroupIndexBits };

	/** Reserved group indices */
	/** 保留的特殊 Group Index，调用者不会分配到这些值。 */
	enum : FGroupIndexType
	{
		InvalidNetObjectGroupIndex = 0,            // 无效（与 Value=0 对应）
		NotReplicatedNetObjectGroupIndex,          // 不复制给任何连接
		NetGroupOwnerNetObjectGroupIndex,          // 仅复制给 RootParent 的 Owner 连接
		NetGroupReplayNetObjectGroupIndex          // 仅在 Replay 条件下复制
	};

	/** 返回一个无效 Group 句柄（哨兵值）。 */
	inline static FNetObjectGroupHandle GetInvalid() {return FNetObjectGroupHandle();}

	FNetObjectGroupHandle() : Value(0u) {}

	// $IRIS TODO: IsValid could be considered a shortcut to Group->IsValidGroup but it's abolutely not the same thing. Rename this to IsInitialized() to remove the confusion.
	/** Returns true if the handle is valid, note this does not mean that the group is valid */
	/** 句柄是否被初始化过（注意：不等价于 Group 仍然存在；用 FNetObjectGroups::IsValidGroup 校验"未销毁"）。 */
	inline bool IsValid() const { return Value != 0u; }

	/** Returns the GroupIndex of the group associated with the handle */
	inline FGroupIndexType GetGroupIndex() const { return FGroupIndexType(Index); }

	/** Returns the unique id for this group */
	uint32 GetUniqueId() const { return UniqueId; }

	/** Returns true if the provided GroupIndex is a reserved NetObjectsGroupIndex */
	/** 给定 Index 是否为系统保留的特殊 Group。 */
	static bool IsReservedNetObjectGroupIndex(FGroupIndexType GroupIndex) { return GroupIndex >= NotReplicatedNetObjectGroupIndex && GroupIndex <= NetGroupReplayNetObjectGroupIndex; }

	/** Returns true if this is a reserved NetObjectsGroups */
	bool IsReservedNetObjectGroup() const { return FNetObjectGroupHandle::IsReservedNetObjectGroupIndex(Index); }

	/** Special group, NetHandles assigned to this group will be filtered out for all connections */
	/** 是否为"完全不复制"组：组内对象不会发送到任何连接。 */
	bool IsNotReplicatedNetObjectGroup() const { return Index == NotReplicatedNetObjectGroupIndex; }

	/** Special group, SubObjects assigned to this group will replicate to owner of RootParent */
	/** 是否为"仅 Owner 可见"组：仅复制给 RootParent 的 OwningNetConnection。 */
	bool IsNetGroupOwnerNetObjectGroup() const { return Index == NetGroupOwnerNetObjectGroupIndex; }

	/** Special group, SubObjects assigned to this group will replicate if replay netconditions is met  */
	/** 是否为"Replay 专属"组：仅在 Replay 条件满足时复制（如客户端录像/服务器录像）。 */
	bool IsNetGroupReplayNetObjectGroup() const { return Index == NetGroupReplayNetObjectGroupIndex; }

	/** 取整体 64-bit 字面值（用于 Hash / Trace）。 */
	uint64 GetRawValue() const
	{
		return Value;
	}
	
private:
	friend UE::Net::Private::FNetObjectGroups;

	/**
	 * Manager 内部构造：使用 Index/Epoch/UniqueId 拼装句柄。
	 * 若 Index 非法，回退为 Default 构造的"无效"句柄。
	 */
	FNetObjectGroupHandle(FGroupIndexType IndexIn, FGroupIndexType EpochIn, uint32 InUniqueId)
	{ 
		if (IndexIn == InvalidNetObjectGroupIndex)
		{
			*this = FNetObjectGroupHandle();
		}
		else
		{
			Index = (uint32)IndexIn;
			Epoch = (uint32)EpochIn;
			UniqueId = InUniqueId;
		}
	}

	// 紧凑布局：64-bit Value（一次性比较） / Index+Epoch+UniqueId（按位段读取）。
	union 
	{
		uint64 Value;
		struct
		{
			uint32 Index : GroupIndexBits;     // Group 在 Manager 中的位置
			uint32 Epoch : EpochBits;          // 代次：销毁回收后递增，防"复用 Index 但旧句柄仍指过来"
			uint32 UniqueId;                   // 全局唯一 ID
		};
	};

	// 句柄相等：要求 Index/Epoch/UniqueId 全部一致；任一不同都视为不同。
	friend inline bool operator==(const FNetObjectGroupHandle& Lhs, const FNetObjectGroupHandle& Rhs) { return Lhs.Value == Rhs.Value; }
	friend inline bool operator!=(const FNetObjectGroupHandle& Lhs, const FNetObjectGroupHandle& Rhs) { return Lhs.Value != Rhs.Value; }
};

static_assert(sizeof(FNetObjectGroupHandle) == sizeof(uint64), "FNetObjectGroupHandle must be of size 64bits.");

/** TMap/TSet 友元 Hash：基于 64-bit Value（包含 Epoch 与 UniqueId）。 */
inline uint32 GetTypeHash(const FNetObjectGroupHandle Handle)
{
	return ::GetTypeHash(Handle.GetRawValue());
}
}
