// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateUtil.h —— 外部状态缓冲（StateBuffer）的位区定位 / MarkDirty 入口
// -------------------------------------------------------------------------------------
// 本文件提供操作"外部状态缓冲"（External State Buffer）的工具内联函数。所有偏移信息
// 均来自 FReplicationStateDescriptor。
//
// 【StateBuffer 的内存布局】（由 Builder 烘焙完成后固定）
//   ┌──────────────────────────────────────────────────────────────────────────────┐
//   │ FReplicationStateHeader (4 字节)                                             │
//   │   - 注：仅在普通 ReplicationState 时位于 offset 0；NativeFastArray 状态的    │
//   │     Header 嵌在 FIrisFastArraySerializer 内部，需要按反向偏移取址（见       │
//   │     GetReplicationStateHeader 的 IsNativeFastArrayReplicationState 分支）    │
//   ├──────────────────────────────────────────────────────────────────────────────┤
//   │ Member 数据区（按 ExternalMemberOffset 排列）                                │
//   │   - 大小 = ExternalSize - ChangeMask 区域                                    │
//   │   - 每个 Property 的存放位置 = MemberDescriptors[i].ExternalMemberOffset     │
//   ├──────────────────────────────────────────────────────────────────────────────┤
//   │ ChangeMask 区域，从 ChangeMasksExternalOffset 开始                           │
//   │   ┌─ MemberChangeMask         (ChangeMaskBitCount bits, 32-bit 对齐 word)    │
//   │   ├─ MemberConditionalChangeMask                                             │
//   │   │    （仅当 HasLifetimeConditionals 时存在；同样大小，紧跟 ChangeMask）    │
//   │   └─ MemberPollMask           (MemberCount bits)                             │
//   └──────────────────────────────────────────────────────────────────────────────┘
//   - 三块 Mask 在物理上连续放置；Util 函数通过 Descriptor 的偏移函数定位每一块。
//
// 【三种 Mask 的语义】
//   - MemberChangeMask          ：每个被 push/poll 标脏的属性占 1+ bit（数组属性会
//     占多 bit 用于元素级 changemask）。是网络发送的"脏属性"位图。
//   - MemberConditionalChangeMask：与 LifetimeCondition 关联，标识哪些属性当前对该连
//     接生效。每位与 ChangeMask 一一对应；写包时进行 AND 过滤。
//   - MemberPollMask            ：MemberCount 位，每位对应一个 Member。Poll 阶段使用，
//     标识需要主动 poll（非 push-based）的成员。每帧重置后再选择性置位。
// =====================================================================================

#pragma once

#include "HAL/Platform.h"
#include "Net/Core/NetBitArray.h"
#include "Net/Core/NetHandle/NetHandleManager.h"
#include "Net/Core/DirtyNetObjectTracker/GlobalDirtyNetObjectTracker.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationState/ReplicationStateFwd.h"

namespace UE::Net
{
	/** Mark the Replication State Header as dirty so it can be copied in the CopyDirtyState pass. */
	// 【MarkNetObjectStateHeaderDirty】对外公开的"标 NetObject 脏"快捷入口。
	//   - 仅置位 Header 的 bStateIsDirty；
	//   - 调用方需自己保证 State 已绑定（IsBound()），否则置脏只对本地起作用，
	//     不会进入 ReplicationWriter 的脏对象列表；
	//   - 通常配合 GlobalDirtyNetObjectTracker 使用，例如 IrisFastArray 在 Item 变更
	//     时同时上报全局 tracker 与本 Header。
	inline void MarkNetObjectStateHeaderDirty(FReplicationStateHeader& Header)
	{
		Private::FReplicationStateHeaderAccessor::MarkStateDirty(Header);
	}

} // end namespace UE::Net

namespace UE::Net::Private
{

/**
 * Get FReplicationStateHeader from a ReplicationState
 *
 * 【GetReplicationStateHeader】从外部状态缓冲取出 Header。
 *   - 普通 State：Header 位于 StateBuffer 起始 offset 0；
 *   - NativeFastArray：Header 嵌在 FIrisFastArraySerializer 内部偏移
 *     (ChangeMasksExternalOffset - sizeof(Header))，因为 FastArray 的内存布局是
 *     "[FFastArraySerializer 自身字段][Header][ChangeMaskStorage[4]]"，由 Builder
 *     的 FixupDescriptorForNativeFastArray 校正。
 */
inline UE::Net::FReplicationStateHeader& GetReplicationStateHeader(void* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	// $IRIS: $TODO: Currently we do not store the offset to the internal replication state
	// 通过 Trait 判定使用哪种偏移：NativeFastArray 反向定位，普通 State 直接 0 偏移。
	const SIZE_T ReplicationStateHeaderOffset = (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::IsNativeFastArrayReplicationState) ? SIZE_T(Descriptor->ChangeMasksExternalOffset - sizeof(FReplicationStateHeader)) : SIZE_T(0));
	return *reinterpret_cast<UE::Net::FReplicationStateHeader*>(reinterpret_cast<uint8*>(StateBuffer) + ReplicationStateHeaderOffset);
}

// 【IsReplicationStateBound】快捷判断：取出 Header 后调用 IsBound()。
inline bool IsReplicationStateBound(void* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	return GetReplicationStateHeader(StateBuffer, Descriptor).IsBound();
}

/**
 * Get MemberChangeMask
 *
 * 【GetMemberChangeMask】取出 ChangeMask 视图。NoResetNoValidate 表示直接复用底层
 *   存储，不清零、不做边界 assert（已由 Descriptor 保证）。
 */
inline FNetBitArrayView GetMemberChangeMask(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	return FNetBitArrayView((FNetBitArrayView::StorageWordType*)(StateBuffer + Descriptor->GetChangeMaskOffset()), Descriptor->ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
}

/**
 * Get MemberConditionalChangeMask. Valid if state has lifetime conditionals.
 *
 * 【GetMemberConditionalChangeMask】取条件 ChangeMask 视图。仅当 Descriptor 带有
 *   HasLifetimeConditionals trait 时有效；本视图与 ChangeMask 等长，每位一一对应，
 *   写包时按 AND 过滤掉对当前连接不应发送的属性。
 */
inline FNetBitArrayView GetMemberConditionalChangeMask(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	return FNetBitArrayView((FNetBitArrayView::StorageWordType*)(StateBuffer + Descriptor->GetConditionalChangeMaskOffset()), Descriptor->ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
}

/**
 * Get MemberPollMask. Valid if state has members
 *
 * 【GetMemberPollMask】取 PollMask 视图。每位对应 Descriptor 中一个 Member。
 *   - Polling 阶段判断哪些 Member 需要从源对象 poll；
 *   - 比例上比 ChangeMask 更小（按成员数而非位数）。
 */
inline FNetBitArrayView GetMemberPollMask(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	return FNetBitArrayView((FNetBitArrayView::StorageWordType*)(StateBuffer + Descriptor->GetMemberPollMaskOffset()), Descriptor->MemberCount, FNetBitArrayView::NoResetNoValidate);
}

/**
 * Reset MemberPollMask
 *
 * 【ResetMemberPollMask】把 PollMask 全部清零。每帧 Poll 前调用。
 *   ResetOnInit 标志会立即将底层 word 全部置 0（构造时一次性 memset）。
 */
inline void ResetMemberPollMask(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	FNetBitArrayView((FNetBitArrayView::StorageWordType*)(StateBuffer + Descriptor->GetMemberPollMaskOffset()), Descriptor->MemberCount, FNetBitArrayView::ResetOnInit);
}

/**
 * Mark specific member dirty, if this is the first bit marked as dirty in the local MemberChangeMask and the state is bound, mark owning object as dirty as well.
 * Note that all bits described by the ChangeMaskInfo will be dirtied by the call. 
 *
 * 【MarkDirty —— Iris 标脏的统一入口】
 *   1) 检查 State 是否已绑定（IsBound）且 ChangeMaskInfo.BitOffset 处的"主位"
 *      此前是 0（首次置脏） —— 这是为了减少冗余通知；
 *   2) 若条件满足，则：
 *      a) 通过 accessor 设置 Header 的 bStateIsDirty；
 *      b) 调用 MarkNetObjectStateHeaderDirty（语义同 a，未来可能扩展为通知全局 tracker）。
 *   3) 把 ChangeMaskInfo 描述的连续 BitCount 位全部置脏：
 *      - 大多数 Member 占 1 bit；
 *      - 数组 Member 可能占多 bit（数组主位 + 各元素位）；
 *      - 注意只检查"主位"是否被脏判断"是否首次"，子位不用作脏判断。
 */
inline void MarkDirty(UE::Net::FReplicationStateHeader& InternalState, FNetBitArrayView& MemberChangeMask, const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo)
{
	// If this state is bound to a replicated object, notify the replication system that we have data to copy
	// Note that we only check the first bit of the changemask, as this is used to indicate whether the property is dirty or not
	// some properties might use additional bits but will only be treated as dirty if the parent bit is set.
	// 仅当 State 已绑定且"主位"原本未脏时，第一次置脏需要通知 NetObject Tracker。
	// 子位（如 TArray 元素 changemask）不参与"是否首次"判断。
	if (InternalState.IsBound() && !MemberChangeMask.GetBit(ChangeMaskInfo.BitOffset))
	{
		FReplicationStateHeaderAccessor::MarkStateDirty(InternalState);
		MarkNetObjectStateHeaderDirty(InternalState);
	}

	// 把 ChangeMaskInfo 描述的整个连续位段置脏（主位+所有子位）。
	MemberChangeMask.SetBits(ChangeMaskInfo.BitOffset, ChangeMaskInfo.BitCount);
}

}
