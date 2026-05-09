// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// PropertyReplicationFragment.cpp —— 通用 Fragment 实现
// ---------------------------------------------------------------------------------------------------------------------
// 实现要点：
//   1) 构造时根据传入 InTraits 与 Descriptor->Traits 推导出 Fragment 的最终 traits 集合：
//        - CanReplicate → 创建 SrcReplicationState；非纯 RPC（FunctionCount==0）才需要 NeedsPoll。
//        - CanReceive   → 处理 RepNotify：要旧值时分配 PrevReplicationState，否则用 prev-received 模式（cvar）。
//        - 总是设置 NeedsLegacyCallbacks（PreNet/PostNetReceive）。
//        - 透传 PushBasedDirtiness / FullPushBasedDirtiness / HasObjectReference。
//        - 加上 HasPropertyReplicationState + SupportsPartialDequantizedState（本 Fragment 能处理部分 dequantize）。
//   2) Apply 路径：
//        - 若需要 PrevState 且非 prev-received 模式：在 PushPropertyReplicationState 之前 StoreCurrentPropertyReplicationStateForRepNotifies；
//        - PushPropertyReplicationState 把 ChangeMask 中已 dirty 的成员写回 Owner 内存（其它成员保留客户端本地修改）。
//   3) RepNotify 路径：
//        - 普通帧：CallRepNotifies(Owner, PrevState, bIsInit, bOnlyCallIfDiffersFromLocal=!UsePrevReceived)；
//        - 没有 PrevState 且是 init：与 default state 比较以避免"全零默认值"无脑触发；
//        - bUsePrevReceivedStateForOnReps=true 时还要把 ReceivedState 备份到 PrevReplicationState 供下帧使用。
//   4) Poll 路径：
//        - PollAllState   → 全量比较；
//        - PollDirtyState + 非 FullPushBased → 仍走全量；FullPushBased 且没标脏可早退；
//        - ForceRefreshCachedObjectReferencesAfterGC → 仅刷新引用。
//
// CVar：
//   - net.Iris.UsePrevReceivedStateForOnReps（默认 false）：
//        true：节省一次"快照本地状态"的开销，但 RepNotify 旧值变成"上次接收态"而非"本地最近态"；
//              如果客户端代码在两次 OnRep 之间修改了属性，此模式会"漏掉"那段差异。
//        false：每次 Apply 前快照本地状态，OnRep 比较与 4.x 兼容。
// =====================================================================================================================

#include "Iris/ReplicationSystem/PropertyReplicationFragment.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisLog.h"
#include "HAL/IConsoleManager.h"
#include "Iris/IrisConfigInternal.h"

namespace UE::Net
{

static bool bUsePrevReceivedStateForOnReps = false;
static FAutoConsoleVariableRef CVarUsePrevReceivedStateForOnReps(
		TEXT("net.Iris.UsePrevReceivedStateForOnReps"),
		bUsePrevReceivedStateForOnReps,
		TEXT("If true OnReps will use the previous received state when doing onreps and not do any compares, if set to false we will copy the local state and do a compare before issuing onreps"
		));

FPropertyReplicationFragment::FPropertyReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor)
: FReplicationFragment(InTraits)
, ReplicationStateDescriptor(InDescriptor)
, Owner(InOwner)
{
	// 服务端：分配 SrcState 用于 Poll/Quantize 来源。
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReplicate))
	{
		SrcReplicationState = MakeUnique<FPropertyReplicationState>(InDescriptor);
	}
	
	// 客户端：处理 RepNotify。
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReceive))
	{
		if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasRepNotifies))
		{
			// We need to store the previous state if the onreps require this information or we are not using the previous received state received for onreps as we need to store the local state before overwriting the current state
			// 需要 PrevState 的两种情况：
			//   - cvar 关闭 prev-received 模式 → 必须在 Apply 前快照本地 state；
			//   - Descriptor 标记 KeepPreviousState（OnRep 函数带 OldValue 参数，必须保存上一帧值）。
			if (!bUsePrevReceivedStateForOnReps || EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::KeepPreviousState))
			{
				PrevReplicationState = MakeUnique<FPropertyReplicationState>(InDescriptor);

				// Full store of initial value for repnotifies
				// 用 Owner 当前值初始化 PrevState —— 第一次 Apply 时它就是"应用前的本地状态"。
				PrevReplicationState->StoreCurrentPropertyReplicationStateForRepNotifies(InOwner, nullptr);

				Traits |= EReplicationFragmentTraits::KeepPreviousState;
			}

			Traits |= EReplicationFragmentTraits::HasRepNotifies;
		}

		// For now we always expect pre/post operations for legacy states, we might make this
		// 现阶段所有兼容 fragment 都需要遗留回调（PreNet/PostNetReceive）；将来可能按需关闭。
		Traits |= EReplicationFragmentTraits::NeedsLegacyCallbacks;
	}
	
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReplicate))
	{
		// For PropertyReplicationStates, except for pure function states, we need to poll properties from our owner in order to detect state changes.
		// 纯函数 state（仅含 UFunction，没有数据成员）不需要 poll；其他 state 必须 poll。
		if (InDescriptor->FunctionCount == 0)
		{
			// In theory we wouldn't need CanReplicate/CanReceive but there's a lot of logic that would then have to check whether the fragment has the appropriate buffers.
			Traits |= EReplicationFragmentTraits::NeedsPoll;
		}
	}

	// Propagate push based dirtiness.
	// 透传 PushModel：Fragment 沿用 Descriptor 的设置——成员级 PushBased 标记由 Descriptor builder 写入。
	if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasPushBasedDirtiness))
	{
		Traits |= EReplicationFragmentTraits::HasPushBasedDirtiness;
		if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasFullPushBasedDirtiness))
		{
			Traits |= EReplicationFragmentTraits::HasFullPushBasedDirtiness;
		}
	}

	// Propagate object reference.
	if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasObjectReference))
	{
		Traits |= EReplicationFragmentTraits::HasObjectReference;
	}

	Traits |= EReplicationFragmentTraits::HasPropertyReplicationState;
	// We can handle partial state in all apply operations
	// 本 Fragment 的 Apply 路径能正确处理"按 ChangeMask 部分 dequantize"的状态——开启此 trait 让上层 Helper 知道。
	Traits |= EReplicationFragmentTraits::SupportsPartialDequantizedState;
}

FPropertyReplicationFragment::~FPropertyReplicationFragment() = default;

void FPropertyReplicationFragment::Register(FFragmentRegistrationContext& Context)
{
	Context.RegisterReplicationFragment(this, ReplicationStateDescriptor.GetReference(), SrcReplicationState ? SrcReplicationState->GetStateBuffer() : nullptr);
}

void FPropertyReplicationFragment::CollectOwner(FReplicationStateOwnerCollector* Owners) const
{
	Owners->AddOwner(Owner);
}

void FPropertyReplicationFragment::CallRepNotifies(FReplicationStateApplyContext& Context)
{
	IRIS_PROFILER_SCOPE(PropertyReplicationFragment_InvokeRepNotifies);

	// 把 Iris 提供的 ExternalStateBuffer 包装成 FPropertyReplicationState（不拷贝、轻量壳）。
	const FPropertyReplicationState ReceivedState(Context.Descriptor, Context.StateBufferData.ExternalStateBuffer);

	if (PrevReplicationState || !Context.bIsInit)
	{
		// 普通帧或带 PrevState 的 init 帧：CallRepNotifies 内部会按 ChangeMask 找出 dirty 成员、按 cvar 决定比较策略。
		FPropertyReplicationState::FCallRepNotifiesParameters Params;
		Params.PreviousState = PrevReplicationState.Get();
		Params.bIsInit = Context.bIsInit;
		// bOnlyCallIfDiffersFromLocal：仅当与本地当前值不同才触发 RepNotify（兼容 4.x 模式 = false 时关闭）。
		Params.bOnlyCallIfDiffersFromLocal = !bUsePrevReceivedStateForOnReps;

		ReceivedState.CallRepNotifies(Owner, Params);

		// We keep a copy of the previous state for RepNotifies that need the value
		// If we rely on received data for the onreps, we just copy the received state, otherwise we must store the local state before applying received data.
		// 在 prev-received 模式下：把刚接收到的 state 复制到 PrevState，下一帧的 OnRep 旧值直接来自这个备份。
		if (bUsePrevReceivedStateForOnReps && PrevReplicationState)
		{
			// Init is always a full state so we can just copy it
			if (Context.bIsInit)
			{
				*PrevReplicationState = ReceivedState;
			}
			else
			{
				// As apply now might provide us with partial states we should only copy dirty members.
				// 部分 state（按 ChangeMask）只需复制 dirty 成员；保留 PrevState 中其它成员的旧值。
				PrevReplicationState->CopyDirtyProperties(ReceivedState);
			}
		}
	}
	else
	{
		// As our default initial states is always treated as all dirty we need to compare against the default before applying initial repnotifies
		// 没有 PrevState 且是 init 帧：与默认状态比较，避免"全零默认 → 全零接收"无意义 OnRep。
		const FPropertyReplicationState DefaultState(Context.Descriptor);

		ReceivedState.CallRepNotifies(Owner, &DefaultState, Context.bIsInit);
	}
}

void FPropertyReplicationFragment::ApplyReplicatedState(FReplicationStateApplyContext& Context) const
{
	IRIS_PROFILER_SCOPE(PropertyReplicationFragment_ApplyReplicatedState);

	// Create a wrapping property replication state, cheap as we are simply injecting the already constructed state
	// 包装层壳——不拷贝。Iris 已经临时构造好 ExternalStateBuffer。
	const FPropertyReplicationState ReceivedState(Context.Descriptor, Context.StateBufferData.ExternalStateBuffer);

	// If we do not rely on received data to issue rep notifies we need to store a copy of the local state before we apply the new received state.
	// 4.x 兼容路径：先快照 owner 当前状态作为旧值，供后续 CallRepNotifies 比较。
	if (!bUsePrevReceivedStateForOnReps && PrevReplicationState)
	{
		PrevReplicationState->StoreCurrentPropertyReplicationStateForRepNotifies(Owner, &ReceivedState);
	}

	// Just push the state data to owner
	// PushPropertyReplicationState 内部按 ChangeMask 仅写 dirty 成员；同时触发 PushModel 的 MARK_PROPERTY_DIRTY 钩子（如有需要）。
	ReceivedState.PushPropertyReplicationState(Owner, static_cast<void*>(Owner));
}

bool FPropertyReplicationFragment::PollReplicatedState(EReplicationFragmentPollFlags PollOption)
{
	bool bIsDirty = false;
	const bool bEnableVerboseProfiling = EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::EnableVerboseProfiling);

	if (EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::PollAllState))
	{
		IRIS_PROFILER_SCOPE_CONDITIONAL(PropertyReplicationFragment_PollAllState, bEnableVerboseProfiling);

		// Since PollPropertyReplicationState always copies the new value we do not need to do anything special
		// with object references if the EReplicationFragmentPollOption::All flag is set
		// 全量 poll：逐成员比较 owner 与 SrcState；任何不同即拷贝并标脏。
		const void* ExternalSourceState = static_cast<void*>(Owner);
		return SrcReplicationState->PollPropertyReplicationState(ExternalSourceState);
	}
	else if (EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::PollDirtyState))
	{
		// PushModel 路径：是否需要刷新引用（GC 后即使没标脏也得 refresh）。
		const bool bForceRefreshObjectReferences = EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC) && ReplicationStateDescriptor->HasObjectReference();

		// We can skip, fully push based states if they are not dirty and we are not refreshing object references
		// FullPushBased 且没标脏 → 早退（最大优势：完全跳过 owner 比较）。
		if (!EnumHasAnyFlags(ReplicationStateDescriptor->Traits, EReplicationStateTraits::HasFullPushBasedDirtiness) || bForceRefreshObjectReferences || SrcReplicationState->IsDirtyForPolling())
		{
			IRIS_PROFILER_SCOPE_CONDITIONAL(PropertyReplicationFragment_PollDirtyState, bEnableVerboseProfiling);

			const void* ExternalSourceState = static_cast<void*>(Owner);
			const FPropertyReplicationState::FPollParameters Params = {.bEnableVerboseProfiling = bEnableVerboseProfiling, .bForceRefreshObjectReferences = bEnableVerboseProfiling };
			bIsDirty = SrcReplicationState->PollPushBasedPropertyReplicationState(ExternalSourceState, Params);
		}
	}
	else if (EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC))
	{
		IRIS_PROFILER_SCOPE_CONDITIONAL(PropertyReplicationFragment_PollObjectReferences, bEnableVerboseProfiling);

		// 仅刷新引用（GC 后专用）：不影响其它字段的 dirty 状态。
		const void* ExternalSourceState = static_cast<void*>(Owner);
		return SrcReplicationState->PollObjectReferences(ExternalSourceState);
	}

	return bIsDirty;
}

void FPropertyReplicationFragment::ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags) const
{
	const FPropertyReplicationState ReceivedState(Context.Descriptor, Context.StateBufferData.ExternalStateBuffer);

	const bool bIncludeAll = EnumHasAnyFlags(Flags, EReplicationStateToStringFlags::OnlyIncludeDirtyMembers) == false;
	ReceivedState.ToString(StringBuilder, bIncludeAll);
};

FPropertyReplicationFragment* FPropertyReplicationFragment::CreateAndRegisterFragment(UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, FFragmentRegistrationContext& Context)
{
	// Fast arrays needs to be bound explicitly
	// FastArray Descriptor 走另一条 Fragment（FastArrayReplicationFragment）；这里直接拒绝。
	if (InDescriptor == nullptr || EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::IsFastArrayReplicationState))
	{
		return nullptr;
	}
	
	FPropertyReplicationFragment* Fragment = new FPropertyReplicationFragment(Context.GetFragmentTraits(), InOwner, InDescriptor);

	// 由 InstanceProtocol 接管生命周期 —— 协议销毁时一并 delete。
	Fragment->Traits |= EReplicationFragmentTraits::DeleteWithInstanceProtocol;

	Fragment->Register(Context);

	return Fragment;
}

}
