// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// ReplicationConditionals.h（Private）
// ---------------------------------------------------------------------------------------------
// 模块定位：Iris / ReplicationSystem / Conditionals 子模块的核心。
//
// 一句话职责：
//   把"协议级 lifetime 条件 + 运行期动态条件 + Owner / Autonomous / Physics 信息"
//   组合成"per-(connection, object) 的最终 ConditionalsMask"，并用它去裁剪每帧的 ChangeMask。
//
// 关键概念：
//   * FConditionalsMask（位图，每位对应一个 ELifetimeCondition）：
//         一个连接能"看到"哪些 lifetime 条件的字段；
//   * 协议端：FReplicationStateDescriptor::MemberLifetimeConditionDescriptors[i]
//         给每个成员标注它的 ELifetimeCondition；
//   * 这里的工作就是：对每个 (Conn, Object) 求 LifetimeConditionals，再与"上一帧的
//     LifetimeConditionals"做 diff，把"刚被开启"的字段 ChangeMask 置脏（让它至少发一次），
//     把"被关闭"的字段从 ChangeMask 里剔除。
//   * 还要处理 SubObject 的 NetGroup（COND_NetGroup）+ OwnerOnly / ReplayOnly 等。
//
// 帧阶段（见 Iris_Architecture.md §4 顶层帧循环）：
//   PreSendUpdate
//     ...
//     9.  Conditionals.Update                     ← 处理"全局 lifetime 条件被脏标"的对象
//     10. QuantizeDirtyStateData
//     ...
//   per-conn ReplicationWriter.WriteObject 内部会调用：
//     Conditionals.GetSubObjectsToReplicate       ← 为该连接挑选可复制的子对象集合
//     Conditionals.ApplyConditionalsToChangeMask  ← 对最终 ChangeMask 做 mask（含 custom + lifetime）
//
// 与其它子系统协作：
//   * FReplicationFiltering：通过 GetOwningConnection 判断 OwnerOnly；
//                            通过 GetSubObjectFilterStatus 处理 COND_NetGroup；
//   * FNetObjectGroups     ：保存 NetGroup 关系（OwnerNetObjectGroup / ReplayNetObjectGroup
//                            / SubObjectFilterGroup）；
//   * FDeltaCompressionBaselineInvalidationTracker：当条件变化导致原本不发的字段开始发
//                            （例如 SetCondition(ReplicatePhysics, true) / SetPropertyCustom
//                             Condition(true)）时，需要作废现有基线（旧基线里这些字段没意义）。
//   * FReplicationConnections：枚举 ValidConnections，并通过其 ReplicationWriter
//                              `UpdateDirtyGlobalLifetimeConditionals` 通知发送侧。
//
// 备注（来源代码原注释）：本类大部分逻辑是为兼容传统 RepLayout / 按属性 lifetime 条件 而存在；
// 对"声明式"复制状态来说很多事情可以在状态本身里完成。
// =============================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Net/Core/NetBitArray.h"

enum ELifetimeCondition : int;
namespace UE::Net
{
	enum class EReplicationCondition : uint32;
	struct FReplicationProtocol;
	namespace Private
	{
		class FDeltaCompressionBaselineInvalidationTracker;
		typedef uint32 FInternalNetRefIndex;
		class FNetRefHandleManager;
		class FReplicationConnections;
		class FNetObjectGroups;
		class FReplicationFiltering;
		class FNetObjectGroups;
	}
}

namespace UE::Net::Private
{

/** 子系统初始化参数集合。由 FReplicationSystemImpl::Init 阶段填充并下发。 */
struct FReplicationConditionalsInitParams
{
	const FNetRefHandleManager* NetRefHandleManager = nullptr;          // 全局 NetObject 表（查 Protocol / SubObject / NetHandle）
	const FReplicationFiltering* ReplicationFiltering = nullptr;        // 查询 OwningConnection / SubObjectFilterStatus
	FReplicationConnections* ReplicationConnections = nullptr;          // 枚举 ValidConnections / 拿 ReplicationWriter
	const FNetObjectGroups* NetObjectGroups = nullptr;                  // 查询 SubObject 所属 NetGroup
	FDeltaCompressionBaselineInvalidationTracker* BaselineInvalidationTracker = nullptr; // 条件变化时作废基线
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;                    // 当前 NetRefHandle 表上限
	uint32 MaxConnectionCount = 0;                                      // 最大并发连接数
};

/**
 * NOTE: Almost everything in this class is for backward compatibility mode only. As such some functions are
 * fairly slow. For declared replication states this overhead will be eliminated because the state
 * itself can do all the relevant tracking locally. The one thing that will possibly remain is the unmasking
 * of changemasks. The unmasking could then either be done on the sending side, CPU vs bandwidth trade-off,
 * or on the receiving side, not so CPU costly as there's typically one connection and maybe tens of objects.
 * The unmasking is then likely to move to protocol operations.
 *
 * ----------------------------------------------------------------------------
 * 中文：
 *   绝大多数逻辑都是为兼容传统 lifetime 条件而存在；声明式复制状态可以在 state 自身处理大部分
 *   工作，但 ChangeMask 的 "unmasking"（按条件清位）很可能仍会保留——可以放在发送侧（节省带宽
 *   但耗 CPU）或接收侧（CPU 便宜，因为典型一连接 + 数十对象）。未来可能挪到 protocol 操作里。
 * ----------------------------------------------------------------------------
 */
class FReplicationConditionals
{
public:
	FReplicationConditionals();

	/** 一次性初始化：把外部依赖指针保存下来并按 Max{InternalNetRefIndex,ConnectionCount} 预分配数组。 */
	void Init(FReplicationConditionalsInitParams& Params);

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	// 中文：当 NetRefHandle 池上限提高时回调，扩容 PerObjectInfos / 每连接 ObjectConditionals / 脏位图。
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	/** Called when when one or more NetRefInternalIndices have been freed and can be re-assigned to new objects. */
	// 中文：当若干 InternalNetRefIndex 被回收时回调，清掉对象的 PerObjectInfo + 所有连接的 ConditionalsMask 缓存。
	void OnInternalNetRefIndicesFreed(const TConstArrayView<FInternalNetRefIndex>& FreedIndices);

	/** 新连接接入：为该连接分配 ObjectConditionals 数组（每对象一个 FConditionalsMask 缓存上一帧值）。 */
	void AddConnection(uint32 ConnectionId);
	/** 连接断开：释放该连接的 ObjectConditionals 数组。 */
	void RemoveConnection(uint32 ConnectionId);

	/**
	 * 设置/清除"按连接的"条件，目前仅 RoleAutonomous 支持。
	 *   bEnable = true ：将 ConnectionId 标为该对象的 Autonomous 连接（其余连接全部回退为 Simulated）；
	 *   bEnable = false：清除（如果当前正是该 Conn）。
	 *
	 * 副作用：
	 *   * MarkRemoteRoleDirty —— 把对象 RemoteRole 字段置脏，确保 role 切换能复制到客户端；
	 *   * SetBit(ObjectsWithDirtyLifetimeConditionals) —— 下次 Update 时通知所有连接的 Writer；
	 *   * InvalidateBaselinesForObjectHierarchy —— 作废受影响连接的基线（只针对发生变化的那条连接）。
	 *
	 * @return false 如果 ConnectionId 越界或 Condition 不被支持。
	 */
	bool SetConditionConnectionFilter(FInternalNetRefIndex ObjectIndex, EReplicationCondition Condition, uint32 ConnectionId, bool bEnable);

	/**
	 * 设置/清除"对象级（不分连接）"的条件，目前仅 ReplicatePhysics。
	 * 开启时会作废所有连接的基线（因为可能引入此前未发送的字段）；关闭时只标脏 lifetime 条件。
	 */
	bool SetCondition(FInternalNetRefIndex ObjectIndex, EReplicationCondition Condition, bool bEnable);

	/** 设置对象的 Owning Connection（影响 COND_OwnerOnly / COND_InitialOrOwner 等）。变化时也会作废涉及的两端连接的基线。 */
	void SetOwningConnection(FInternalNetRefIndex ObjectIndex, uint32 ConnectionId);

	// For property custom conditions only
	// ------------- 以下三个 API 是针对"按属性的 custom condition / dynamic condition"的兼容支持 -------------

	/**
	 * 在对象注册到 Iris 时被调用：根据 FRepChangedPropertyTracker 中的当前激活态，初始化外部
	 * state buffer 中的 ConditionalChangeMask；同时把每个 COND_Dynamic 属性的当前动态值同步到本类。
	 */
	void InitPropertyCustomConditions(FInternalNetRefIndex ObjectIndex);

	/**
	 * 修改某属性（按 RepIndex）的 custom condition 激活态。
	 *   bIsActive = true  ：在该属性的 ConditionalChangeMask 上置位，并将其 ChangeMask 同步置脏，
	 *                       同时作废所有连接的基线（因为旧基线里该属性没值）；
	 *   bIsActive = false ：仅在 ConditionalChangeMask 上清位即可。
	 *
	 * @return 是否找到了 RepIndex 对应的成员。
	 */
	bool SetPropertyCustomCondition(FInternalNetRefIndex ObjectIndex, const void* Owner, uint16 RepIndex, bool bIsActive);

	/**
	 * 把某属性当前的动态条件（仅当该属性原本声明为 COND_Dynamic）改成具体的 ELifetimeCondition。
	 * 当条件变化"可能"导致字段从不复制变为复制时，作废所有连接的基线（DynamicConditionChangeRequiresBaselineInvalidation）。
	 */
	bool SetPropertyDynamicCondition(FInternalNetRefIndex ObjectIndex, const void* Owner, uint16 RepIndex, ELifetimeCondition Condition);

	/** 一次性把整个 NetGroup 内对象的 lifetime 条件标脏（典型场景：NetGroup 成员变化）。 */
	void MarkLifeTimeConditionalsDirtyForObjectsInGroup(FNetObjectGroupHandle GroupHandle);
	
	/** Unconditionally marks a property as dirty, causing it to replicate with the object at the earliest convenience. */
	// 中文：无条件把某 RepIndex 对应属性标脏（绕过条件判定，下一次发送一定会带）。
	void MarkPropertyDirty(FInternalNetRefIndex ObjectIndex, uint16 RepIndex);

	/**
	 * 帧主循环。当前实现：仅把 ObjectsWithDirtyLifetimeConditionals 中的对象逐 conn 通知到
	 * 各 ReplicationWriter（UpdateDirtyGlobalLifetimeConditionals），让发送侧重新评估这些对象。
	 * 受 net.Iris.EnableUpdateObjectsWithDirtyConditionals 控制（见 cpp 文件）。
	 */
	void Update();

	/**
	 * 一个紧凑的位图：每位对应一个 ELifetimeCondition（最大 16 个）。
	 * 用来表示"在某 (Connection, Object) 组合下，哪些 lifetime 条件是被启用的"。
	 *
	 * 注意：本枚举位下标必须 < 16（COND_MAX 约 14 个）。
	 */
	struct FConditionalsMask
	{
		bool IsUninitialized() const { return ConditionalsMask == 0; }
		bool IsConditionEnabled(int Condition) const { checkSlow(Condition < 16); return ConditionalsMask & (uint16(1) << unsigned(Condition)); }
		bool SetConditionEnabled(int Condition, bool bEnabled) { checkSlow(Condition < 16); return ConditionalsMask |= (uint16(bEnabled ? 1 : 0) << unsigned(Condition)); }

		// Each LifetimeCondition is represented in this member via (1U << ELifetimeCondition)
		// 中文：低 16 位每一位对应一个 ELifetimeCondition：bit i ↔ COND_i 是否启用。
		uint16 ConditionalsMask;
	};

	/**
	 * 帧内每对象/每连接调用一次：把"最终的"条件结果应用到要发送的 ChangeMask 上。
	 *
	 * 输入：
	 *   ChangeMaskData            —— 量化产物中"想发送"的 ChangeMask（in-out）；
	 *   ConditionalChangeMaskData —— 来自外部 state buffer 的 custom-condition 位图（可空）；
	 *   ParentObjectIndex         —— 算 LifetimeConditionals 时永远以 root 对象的 owner/role 为准；
	 *   ObjectIndex               —— 当前真正要写的对象（root 或 subobject）；
	 *   bIsInitialState           —— 第一次发该对象时为 true，控制 COND_InitialOnly / COND_InitialOrOwner。
	 *
	 * 公式（每位 b）：
	 *   FinalChangeMask[b] =
	 *       ChangeMaskData[b]
	 *     & (LifetimeOK[member(b)]   ? 1 : 0)         // lifetime 条件按位求值
	 *     & (ConditionalChangeMask[b] ? 1 : 0)        // custom condition mask（如果提供）
	 *
	 *   且若该位的 lifetime 条件本帧由 disable→enable，则会主动 SetBit 让其至少发一次。
	 *
	 * @return ChangeMask 是否被本函数修改过（影响调用方是否需要重新置脏 PerObjectInfo）。
	 */
	bool ApplyConditionalsToChangeMask(uint32 ReplicatingConnectionId, bool bIsInitialState, FInternalNetRefIndex ParentObjectIndex, FInternalNetRefIndex ObjectIndex, uint32* ChangeMaskData, const uint32* ConditionalChangeMaskData, const FReplicationProtocol* Protocol);

	using FSubObjectsToReplicateArray = TArray<FInternalNetRefIndex, TInlineAllocator<32>>;

	/**
	 * 对给定 (Conn, RootObject) 选出本次需要复制的 SubObject 列表（按层级递归，
	 * 模拟旧系统"子子对象先于父对象"的顺序）。
	 *
	 * 处理顺序：
	 *   1. 计算 root 的 LifetimeConditionals（COND_OwnerOnly / Autonomous / Physics / ...）；
	 *   2. 遍历每个 child SubObject：
	 *      - 若声明 COND_NetGroup：查 NetGroup（OwnerNetGroup → COND_OwnerOnly；
	 *        ReplayNetGroup → COND_ReplayOnly；其余通过 SubObjectFilter 决定）；
	 *      - 否则看 LifetimeConditionals 中对应 bit 是否打开。
	 */
	void GetSubObjectsToReplicate(uint32 ReplicationConnectionId, FInternalNetRefIndex ParentObjectIndex, FSubObjectsToReplicateArray& OutSubObjectsToReplicate);

private:
	/**
	 * 每对象级的"对象级条件"摘要（不分连接）。占用 2 字节。
	 *
	 *   * AutonomousConnectionId：被设为 Autonomous 的连接 ID（0 表示无）；
	 *   * bRepPhysics          ：当前是否启用 ReplicatePhysics 条件。
	 */
	struct FPerObjectInfo
	{
		// Assume there can only be one connection which has the role autonomous connection and all else are simulated
		uint16 AutonomousConnectionId : 15;
		uint16 bRepPhysics : 1;
	};

	/**
	 * 每连接的状态。ObjectConditionals[i] 记录"上一次为对象 i 计算出来的 LifetimeConditionals"，
	 * 用于在 ApplyConditionalsToChangeMask 内做 prev → current 的差分（为新启用条件主动置脏）。
	 */
	struct FPerConnectionInfo
	{
		TArray<FConditionalsMask> ObjectConditionals;
	};

	/** SubObject 自身的 lifetime 条件（int8 紧凑存储 ELifetimeCondition）。 */
	struct FSubObjectConditionInfo
	{
		// This is a more compact storage form of ELifetimeCondition
		int8 Condition;
	};

	/**
	 * 对象上"声明为 COND_Dynamic 的属性"的当前动态条件值表：
	 * key = property RepIndex, value = ELifetimeCondition（int16 存储）。
	 */
	struct FObjectDynamicConditions
	{
		// RepIndex and ELifetimeCondition expressed as int16
		TMap<uint16, int16> DynamicConditions;
	};

private:
	/** Update 主体：把脏对象批量推送到所有 ValidConnection 的 ReplicationWriter。 */
	void UpdateAndResetObjectsWithDirtyConditionals();

	/**
	 * 算出 (Conn, RootObject) 当前帧的 LifetimeConditionals 位图：
	 *   COND_None / COND_Custom / COND_Dynamic     恒置 1；
	 *   COND_OwnerOnly                              ←  Conn == OwningConn(Object)
	 *   COND_SkipOwner                              ← !COND_OwnerOnly
	 *   COND_AutonomousOnly                         ←  Conn == AutonomousConnectionId
	 *   COND_SimulatedOnly / NoReplay               ← !COND_AutonomousOnly
	 *   COND_SimulatedOrPhysics / NoReplay          ←  COND_SimulatedOnly | bRepPhysics
	 *   COND_InitialOnly                            ←  bInitialState
	 *   COND_InitialOrOwner                         ←  bInitialState | COND_OwnerOnly
	 *   COND_ReplayOrOwner                          ←  COND_OwnerOnly （Replay 标志在此实现里恒为否）
	 *   COND_SkipReplay                             ←  恒置 1（Iris 不再走 Replay 通路）
	 */
	FConditionalsMask GetLifetimeConditionals(uint32 ReplicatingConnectionId, FInternalNetRefIndex ParentObjectIndex, bool bInitialState) const;

	FPerObjectInfo* GetPerObjectInfo(FInternalNetRefIndex ObjectIndex);
	const FPerObjectInfo* GetPerObjectInfo(FInternalNetRefIndex ObjectIndex) const;

	/** 对象被回收：清 PerObjectInfo + DynamicConditions[ObjectIndex]。 */
	void ClearPerObjectInfo(FInternalNetRefIndex ObjectIndex);

	/** 对象被回收：清所有连接的 ObjectConditionals[ObjectIndex] 缓存。 */
	void ClearConnectionInfosForObject(const FNetBitArray& ValidConnections, FInternalNetRefIndex ObjectIndex);

	/** GetSubObjectsToReplicate 的递归辅助。 */
	void GetChildSubObjectsToReplicate(uint32 ReplicatingConnectionId, const FConditionalsMask& LifetimeConditionals, const FInternalNetRefIndex ParentObjectIndex, FSubObjectsToReplicateArray& OutSubObjectsToReplicate);

	/** 取/设动态条件（按对象 + RepIndex）。未设置则视为 COND_Dynamic（后备语义）。 */
	ELifetimeCondition GetDynamicCondition(FInternalNetRefIndex ObjectIndex, uint16 RepIndex) const;
	void SetDynamicCondition(FInternalNetRefIndex ObjectIndex, uint16 RepIndex, ELifetimeCondition Condition);

	/**
	 * 判断 Old → New 的动态条件变化是否需要作废基线。
	 * 只有当 Old "可能未发送某连接" 且 New 不是 COND_Never 时才需要——
	 * 等价于：可能引入"客户端基线里没有"的字段值。
	 */
	bool DynamicConditionChangeRequiresBaselineInvalidation(ELifetimeCondition OldCondition, ELifetimeCondition NewCondition) const;

	/** 把对象上的 RemoteRole 属性标脏（角色切换时必须复制）。需要先在协议中找到 RemoteRole 的 RepIndex。 */
	void MarkRemoteRoleDirty(FInternalNetRefIndex ObjectIndex);
	uint16 GetRemoteRoleRepIndex(const FReplicationProtocol* Protocol);

	// Invalidates baselines for root object and subobjects with lifetime conditionals.
	// 中文：对 root 与全部带 lifetime 条件 trait 的 SubObject 在指定连接上作废基线。
	void InvalidateBaselinesForObjectHierarchy(uint32 ObjectIndex, const TConstArrayView<uint32>& ConnectionsToInvalidate);

private:
	/** 表示 RepIndex 无效（未找到 RemoteRole 等）的哨兵值。 */
	static constexpr uint16 InvalidRepIndex = 65535U;

	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	const FReplicationFiltering* ReplicationFiltering = nullptr;
	FReplicationConnections* ReplicationConnections = nullptr;
	FDeltaCompressionBaselineInvalidationTracker* BaselineInvalidationTracker = nullptr;
	const FNetObjectGroups* NetObjectGroups = nullptr;

	/** 每对象的紧凑信息表。下标即 InternalNetRefIndex。 */
	TArray<FPerObjectInfo> PerObjectInfos;

	/** 每连接的状态表。下标即 ConnectionId（0 不使用）。 */
	TArray<FPerConnectionInfo> ConnectionInfos;

	/** 仅为"声明了 COND_Dynamic 的对象"分配条目，按需稀疏存。 */
	TMap<FInternalNetRefIndex, FObjectDynamicConditions> DynamicConditions;

	/**
	 * "本帧需要在所有连接上重新评估 lifetime 条件的对象"位图。
	 * 触发场景：SetConditionConnectionFilter / SetCondition / SetOwningConnection /
	 *           MarkLifeTimeConditionalsDirtyForObjectsInGroup。
	 * Update() 会消费并清零该位图。
	 */
	FNetBitArray ObjectsWithDirtyLifetimeConditionals;

	FInternalNetRefIndex MaxInternalNetRefIndex = 0;
	uint32 MaxConnectionCount = 0;

	/** RemoteRole 在协议中的 RepIndex 缓存（首次找到后复用）。 */
	uint16 CachedRemoteRoleRepIndex = InvalidRepIndex;
};

inline FReplicationConditionals::FPerObjectInfo* FReplicationConditionals::GetPerObjectInfo(FInternalNetRefIndex ObjectIndex)
{
	return PerObjectInfos.GetData() + ObjectIndex;
}

inline const FReplicationConditionals::FPerObjectInfo* FReplicationConditionals::GetPerObjectInfo(FInternalNetRefIndex ObjectIndex) const
{
	return PerObjectInfos.GetData() + ObjectIndex;
}

}
