// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// PropertyReplicationFragment.h —— 基于 UProperty 的通用 Fragment
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/  ← 对外 API。
// 角色：FPropertyReplicationFragment 是 Iris 默认的"反射型 Fragment"实现，覆盖绝大多数普通 ReplicatedProperty 场景。
//       一个 PropertyReplicationFragment 对应一份 FReplicationStateDescriptor，由 ReplicationFragmentUtil 在
//       CreateAndRegisterFragmentsForObject 流程中按 Class 自动构建并注册。
//
// 核心数据成员：
//   - SrcReplicationState：发送源——FPropertyReplicationState 对外部 SrcBuffer + ChangeMask 的封装；
//        服务端 Poll 时从 Owner 抓取属性值写入此 state。
//   - PrevReplicationState：上一帧应用前快照——仅在需要 RepNotify 旧值时分配；
//        与 KeepPreviousState trait 联动。
//   - ReplicationStateDescriptor：状态描述（成员、ChangeMask、RepNotify、PushModel）。
//   - Owner：原始 UObject —— 用于 PropertyReplicationState 的 Push（写回属性）+ ProcessEvent（调 RepNotify）。
//
// 关键交互：
//   1) 注册：CreateAndRegisterFragment（静态工厂）→ new + 设置 DeleteWithInstanceProtocol → Register。
//   2) Poll：PollReplicatedState 根据 PollFlags 选择全量 / 仅脏 / 仅引用刷新；
//        - PollAllState     → FPropertyReplicationState::PollPropertyReplicationState（全比较）
//        - PollDirtyState   → PollPushBasedPropertyReplicationState（PushModel 推路径）
//        - ForceRefreshGC   → PollObjectReferences（仅刷新引用，不影响其它字段）。
//   3) Apply：ApplyReplicatedState 把接收到的 ExternalStateBuffer 包装成 FPropertyReplicationState
//        然后 PushPropertyReplicationState 写回 Owner（按 ChangeMask 部分写）。
//        若需 KeepPreviousState：Apply 前把当前 owner 状态存到 PrevReplicationState。
//   4) RepNotify：CallRepNotifies
//        - bUsePrevReceivedStateForOnReps=true（CVar）→ 用上一帧已接收状态作为旧值，不走 owner 比对；
//        - bUsePrevReceivedStateForOnReps=false → 在 Apply 之前快照 owner 状态作为旧值，能反映"客户端本地修改"。
//        - init state 与 default state 比较以决定是否触发首次 OnRep。
//
// 与文档对应：
//   - ReplicationState.md §2.4（FPropertyReplicationState）。
//   - ReplicationSystem.md §2.6 + §6.6（Polling）。
//
// FastArray 路径单独由 FastArrayReplicationFragment 处理（CreateAndRegisterFragment 会拒绝 FastArray Descriptor，返回 nullptr）。
// =====================================================================================================================

#pragma once
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

namespace UE::Net
{
	class FPropertyReplicationState;
}

namespace UE::Net
{

/**
* FPropertyReplicationFragment - used to bind PropertyReplicationStates to their owner
*/
// FPropertyReplicationFragment ── 基于 UProperty 反射的通用 Fragment。
// 一个该 fragment = 一份 FReplicationStateDescriptor + 一个 FPropertyReplicationState（SrcState）+ 可选 PrevState。
class FPropertyReplicationFragment : public FReplicationFragment
{
public:
	// 构造：根据传入 traits 决定是否分配 SrcState / PrevState；并基于 Descriptor traits 推导自身 Traits（PushBased / RepNotify / Owner ref 等）。
	IRISCORE_API FPropertyReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor);

	IRISCORE_API ~FPropertyReplicationFragment();

	/** Allow access to replication state */
	// 让外部读取 SrcReplicationState（Receive-only fragment 时为 nullptr）。
	const FPropertyReplicationState* GetPropertyReplicationState() const { return SrcReplicationState.Get(); }

	/**
	 * Register an already existing PropertyReplicationFragment, for example one that is carried around by a base class
	*/
	// 注册自身到 Context；若 SrcReplicationState 存在则把 ExternalSrcBuffer 一并提供（pure poll 路径需要这个指针）。
	void Register(FFragmentRegistrationContext& Fragments);
	
	/** 
	* Create and register a PropertyReplicationFragment using the provided descriptor, the lifetime of the fragment will be managed by the ReplicationSystem
	* Lifetime of the created fragment will be managed by the ReplicationSystem
	* returns a pointer to the created fragment
	*/
	// 静态工厂：被 ReplicationFragmentUtil 在自动注册路径上调用。
	// 拒绝 FastArray Descriptor（IsFastArrayReplicationState）—— FastArray 走另一条 Fragment 路径。
	// 返回的 Fragment 设置 DeleteWithInstanceProtocol，由 InstanceProtocol 销毁时自动 delete。
	IRISCORE_API static FPropertyReplicationFragment* CreateAndRegisterFragment(UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, FFragmentRegistrationContext& Context);

protected:

	/** FReplicationFragment Implementation */
	// 接收侧应用：把接收到的 ExternalStateBuffer 推回 Owner 的属性内存（PushPropertyReplicationState 内部按 ChangeMask 部分写入）。
	virtual void ApplyReplicatedState(FReplicationStateApplyContext& Context) const override;

	// 收集 Pre/PostNetReceive 的 owner —— 始终是构造时传入的那一个 UObject*。
	virtual void CollectOwner(FReplicationStateOwnerCollector* Owners) const override;
	
	// 触发 RepNotify：根据是否有 PrevState 与 cvar 决定如何比较旧值；
	// init 状态额外与默认状态比较，避免无意义的"零值 → 零值"调用。
	virtual void CallRepNotifies(FReplicationStateApplyContext& Context) override;
	
	// 服务端 Poll：从 Owner 读取属性。
	// - PollAllState：全量轮询并 copy 进 SrcState；
	// - PollDirtyState：PushModel 路径，仅 dirty 成员（同时若该状态有引用且需 GC 后刷新则也走 poll）；
	// - ForceRefreshCachedObjectReferencesAfterGC：仅刷新引用（PollObjectReferences）。
	virtual bool PollReplicatedState(EReplicationFragmentPollFlags PollOption) override;

	// 调试输出：把 ExternalStateBuffer 包装成 FPropertyReplicationState 后调用 ToString。
	virtual void ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags) const override;

private:
	// This is the source state from which we source our state data
	// 服务端发送源 state；客户端 Receive-only fragment 时为空。
	TUniquePtr<FPropertyReplicationState> SrcReplicationState;

	// Previous applied state, only carried around if needed
	// 仅当 RepNotify 需要旧值且 cvar 不允许 use-prev-received 时分配。
	TUniquePtr<FPropertyReplicationState> PrevReplicationState;

	// 描述符引用计数智能指针 —— 与 ReplicationStateDescriptorRegistry 共享（同一类对象的多个 fragment 复用同一份 Descriptor）。
	TRefCountPtr<const FReplicationStateDescriptor> ReplicationStateDescriptor;

	// Owner
	// 与 Fragment 绑定的 UObject —— 所有 Push/RepNotify 都作用在此对象。
	UObject* Owner;
};

}
