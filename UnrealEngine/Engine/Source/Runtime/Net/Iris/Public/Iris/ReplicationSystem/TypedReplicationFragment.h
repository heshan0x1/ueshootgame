// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// TypedReplicationFragment.h —— 模板 CRTP 便捷 Fragment 基类
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/  ← 对外 API。
// 角色：当用户使用"代码生成 / 手写宏生成"出来的强类型 ReplicationState（即 ReplicationStateT 提供
//      GetReplicationStateDescriptor() 静态方法 + 用户 Owner 类型 T 提供 ApplyReplicationState 方法）时，
//      不必从零写 FReplicationFragment 派生类。直接用 TReplicationFragment<T, ReplicationStateT> 即可获得：
//          - 与 Descriptor 的自动绑定（注册时通过 ReplicationStateT::GetReplicationStateDescriptor() 获取）；
//          - ApplyReplicatedState 默认实现：reinterpret_cast 到 ReplicationStateT 然后调用 Owner.ApplyReplicationState。
//
// 与文档对应：
//   - ReplicationState.md §1.1（ReplicationStateDescriptor 宏 / 实现宏）—— 生成的 ReplicationStateT 暴露
//     GetReplicationStateDescriptor() 给本模板使用。
//   - Iris_Architecture.md §3.7（"扩展点"）。
//
// 使用模式：
//   1) 定义 ReplicationStateT（IRIS_BEGIN_*_DESCRIPTOR + IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR 宏）。
//   2) 在 Owner 类 T 中实现 void ApplyReplicationState(const ReplicationStateT&, FReplicationStateApplyContext&);
//   3) 在 RegisterReplicationFragments 中：
//          MyFragment.Register(Context);
//      其中 MyFragment 类型为 TReplicationFragment<T, ReplicationStateT>。
//
// 两种构造模式：
//   - 仅接收（Receive-only）：TReplicationFragment(T& Owner)            —— 用于纯客户端接收侧 fragment。
//   - 复制（Replicate）：    TReplicationFragment(T& Owner, ReplicationStateT& SrcState) —— 提供发送源 state 缓冲。
// 这两个构造函数会分别设置 Traits 为 CanReceive 或 CanReplicate；具体何时使用哪种由 Bridge 注册阶段决定。
//
// 注意：
//   - 默认 ApplyReplicatedState 假设 Context.StateBufferData.ExternalStateBuffer 即为已构造的 ReplicationStateT 实例
//     （即 Fragment 没有设置 HasPersistentTargetStateBuffer trait，Iris 帮我们临时构造了 external state）。
//   - $TODO 标注的"状态生命周期管理"是 Iris 的已知改进点：当前一次 Apply 后临时 state 即被释放。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/ReplicationFragment.h"

namespace UE::Net
{

/*
* Typed replication fragments are used when we have generated ReplicationStates
* and will call the SetReplicatedState method on the parent class
* $IRIS TODO: Introduce policies based on ReplicationStateTraits, i.e. should we call other functions on Init etc?
*/
// 模板 CRTP Fragment：
//   T                 —— Owner 类型，需要提供 void ApplyReplicationState(const ReplicationStateT&, FReplicationStateApplyContext&);
//   ReplicationStateT —— 强类型 state，需要提供 static const FReplicationStateDescriptor* GetReplicationStateDescriptor();
template <typename T, typename ReplicationStateT>
class TReplicationFragment : protected FReplicationFragment
{
public:
	// Receive only
	// 仅接收：构造时不持有 SrcState。Traits = CanReceive。
	explicit TReplicationFragment(T& OwnerIn) : FReplicationFragment(EReplicationFragmentTraits::CanReceive), Owner(OwnerIn), SrcState(nullptr) {}

	// Replicate 
	// 复制：传入用户提供的 SrcState 引用，Traits = CanReplicate。
	// SrcState 的生命周期由用户自己保证（通常嵌入 Owner 内）。
	TReplicationFragment(T& OwnerIn, ReplicationStateT& SrcReplicationState) : FReplicationFragment(EReplicationFragmentTraits::CanReplicate), Owner(OwnerIn), SrcState(&SrcReplicationState) {}

	// 注册到 Context：把自身、Descriptor、SrcState 一并加入。
	void Register(FFragmentRegistrationContext& Context);

protected:
	// FReplicationFragment
	// 默认 Apply：直接 reinterpret_cast 拿强类型 state，转交给 Owner 的 ApplyReplicationState。
	virtual void ApplyReplicatedState(FReplicationStateApplyContext& Context) const override;


private:
	// Owner 引用——派生类期望它在 fragment 生命周期内持续有效。
	T& Owner;
	// 发送源 state；Receive-only 模式下为 nullptr。
	ReplicationStateT* SrcState;
};

template <typename T, typename ReplicationStateT>
void TReplicationFragment<T, ReplicationStateT>::ApplyReplicatedState(FReplicationStateApplyContext& Context) const
{
	// Cast to the expected type
	// Iris 在调用 Apply 前已经为我们临时构造好一份 ReplicationStateT 在 ExternalStateBuffer 上，直接 reinterpret_cast 即可。
	const ReplicationStateT* ReplicationState  = reinterpret_cast<const ReplicationStateT*>(Context.StateBufferData.ExternalStateBuffer);

	// $TODO: We need a state management system here so that we can keep states around until update time
	// either we implement this at this level or we make this part of the replication system, maybe a linear allocator during receive that we will free after we have applied the states
	// 已知改进点：当前一次 Apply 后临时 state 即被释放，不能跨帧复用。Iris 计划引入线性 allocator 持久化此类 state。
	Owner.ApplyReplicationState(*ReplicationState, Context);
}

template <typename T, typename ReplicationStateT>
void TReplicationFragment<T, ReplicationStateT>::Register(FFragmentRegistrationContext& Context)
{
	// 调用 ReplicationStateT::GetReplicationStateDescriptor() 拿到该 state 的描述符；SrcState 仅在 Replicate 模式下非空。
	Context.RegisterReplicationFragment(this, ReplicationStateT::GetReplicationStateDescriptor(), SrcState);
}

}
