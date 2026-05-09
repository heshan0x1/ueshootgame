// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateFwd.h —— Iris ReplicationState 前置声明 / FReplicationStateHeader
// -------------------------------------------------------------------------------------
// 本文件定义 ReplicationState 模块对外可见的最小核心：FReplicationStateHeader。
//
// 【FReplicationStateHeader 的作用】
//   每一块"外部状态缓冲"（External State Buffer，即 FPropertyReplicationState 持有的内
//   存）开头都嵌入一个 FReplicationStateHeader（4 字节）。NativeFastArray 例外，其
//   header 嵌在 FIrisFastArraySerializer 的固定位置，通过 Descriptor->ChangeMasksExternalOffset
//   - sizeof(FReplicationStateHeader) 反算偏移（参见 ReplicationStateUtil.h 的
//     GetReplicationStateHeader 实现）。
//
// 【32-bit 位布局】
//   ┌────────────────────────────────────┬──────────────┬─────────────┐
//   │ NetHandleId : 30                   │ bInitState   │ bState      │
//   │   30-bit 全局 NetHandle ID（绑定到 │ IsDirty : 1  │ IsDirty : 1 │
//   │   FNetHandleManager 的脏跟踪表）   │              │             │
//   └────────────────────────────────────┴──────────────┴─────────────┘
//   - NetHandleId == 0 表示未绑定（IsBound() 返回 false），此时所有 MarkDirty 调用都
//     不会去通知 GlobalDirtyNetObjectTracker，仅本地置位 changemask；
//   - bInitStateIsDirty：Init State（首次复制时一次性下发的属性集合）没有 changemask，
//     此位本身就是它的"changemask"；
//   - bStateIsDirty：除 Init 之外任意 changemask 位被首次置脏时设置一次（防抖用）。
//
// 【访问控制：FReplicationStateHeaderAccessor】
//   位字段全部 private，仅 friend 内部 accessor 可读写。这是 Iris 强制的封装边界——
//   游戏层代码只能通过 IsBound() 只读判断绑定，所有写操作必须走 ReplicationStateUtil.h
//   的 MarkDirty / MarkNetObjectStateHeaderDirty 入口（最终再走 accessor）。
// =====================================================================================

#pragma once

#include "HAL/Platform.h"
#include "Net/Core/NetHandle/NetHandle.h"

namespace UE::Net
{

namespace Private
{
	// 内部访问器前置声明，仅 Iris 框架内部代码可使用，用于读写 FReplicationStateHeader
	// 的 private 位字段（NetHandleId / bInitStateIsDirty / bStateIsDirty）。
	struct FReplicationStateHeaderAccessor;
}

/** 
 * A ReplicationState always contains a ReplicationStateHeader which we use to bind replication states for dirty tracking
 *
 * 【FReplicationStateHeader】每一个 ReplicationState 都持有一个 Header，
 * Iris 通过它把状态缓冲绑定到 FDirtyNetObjectTracker（脏跟踪系统）。
 *
 * 内存布局严格 4 字节（与 uint32 同 size/alignment，见下方 static_assert）：
 *   - NetHandleId      : 30 位，全局 NetHandle ID，0 表示未绑定
 *   - bInitStateIsDirty:  1 位，Init State 专用脏标记（Init 状态无 changemask）
 *   - bStateIsDirty    :  1 位，本对象任意状态是否脏（防止重复通知 tracker）
 */
struct FReplicationStateHeader
{
	// 默认构造：所有位清零，状态未绑定。
	FReplicationStateHeader() : NetHandleId(0), bInitStateIsDirty(0), bStateIsDirty(0) {}

	/** Returns true if the state is bound to the dirty tracking system */
	// IsBound：判断 State 是否已绑定到 NetHandle 脏跟踪系统。
	// 仅当 NetHandleId 非 0 时为 true。未绑定时调用 MarkDirty 不会向 tracker 上报。
	bool IsBound() const { return NetHandleId != 0; }

private:
	friend Private::FReplicationStateHeaderAccessor;

	// All replication states that are bound by an instance protocol is assigned a NetHandle for dirty state tracking
	// 30 位 NetHandle ID。当 InstanceProtocol 绑定该 State 时由 SetNetHandleId() 写入；
	// 之所以用 30 位是为了与 dirty 标志位共用 32-bit 字（保持 cache line 友好）。
	// NetHandle 总数上限因此为 2^30-1 ≈ 10 亿，足够任何实际场景。
	uint32 NetHandleId : 30;
	// Init state doesn't use changemasks, instead we have a reserved bit here
	// Init State（一次性初始属性集合）没有独立 ChangeMask，使用此位作为唯一的脏标记。
	uint32 bInitStateIsDirty : 1;
	// Track whether any state is dirty.
	// 任意常规 ChangeMask 被置脏时本位会被置 1；仅在第一次置脏时同步通知
	// FDirtyNetObjectTracker，避免重复通知（见 ReplicationStateUtil.h::MarkDirty 实现）。
	uint32 bStateIsDirty : 1;
};

// 编译期校验：Header 必须与 uint32 完全等价（4 字节、4 字节对齐），
// 以保证它能直接嵌入 ChangeMask 数组之前并按 32-bit 原子访问。
static_assert(sizeof(FReplicationStateHeader) == sizeof(uint32) && alignof(FReplicationStateHeader) == alignof(uint32), "FReplicationStateHeader must currently have the same size and alignment as uint32");

namespace Private
{

/** 
 * Internal access, should only be used by internal code
 *
 * 【FReplicationStateHeaderAccessor】Header 私有位字段的"穿透访问器"——
 *   仅 Iris 内部代码（ReplicationStateUtil、InstanceProtocol、FastArray 等）使用。
 *   外部代码（游戏层、第三方）一律不能读写 dirty 位，必须走 MarkDirty 等高层入口。
 */
struct FReplicationStateHeaderAccessor
{
	// 读取 30-bit NetHandle ID（绑定 ID）
	static uint32 GetNetHandleId(const FReplicationStateHeader& Header) { return Header.NetHandleId; }
	// 读取 Init 脏位
	static bool GetIsInitStateDirty(const FReplicationStateHeader& Header) { return Header.bInitStateIsDirty; }
	// 读取常规脏位
	static bool GetIsStateDirty(const FReplicationStateHeader& Header) { return Header.bStateIsDirty; }

	// 标记 Init State 为脏（用于初始一次性属性集合的脏跟踪）。
	static void MarkInitStateDirty(FReplicationStateHeader& Header) { Header.bInitStateIsDirty = true; }
	// 标记常规 State 为脏。由 ReplicationStateUtil::MarkDirty 在首次置脏时调用，
	// 然后再走 GlobalDirtyNetObjectTracker 上报全局。
	static void MarkStateDirty(FReplicationStateHeader& Header) { Header.bStateIsDirty = true; }

	/** Clears both state and init state dirtiness. */
	// 清空两个脏位，由 ReplicationWriter 在写入完成后调用以重置。
	// 注意：不清空 NetHandleId，绑定关系保持不变。
	static void ClearAllStateIsDirty(FReplicationStateHeader& Header)
	{ 
		Header.bInitStateIsDirty = false;
		Header.bStateIsDirty = false;
	}

	// 设置 NetHandle ID（绑定 State 到 tracker）。仅截取 NetHandle.GetId() 的低 30 位。
	// 由 InstanceProtocol 在 BindInstanceProtocol 阶段调用一次。
	static void SetNetHandleId(FReplicationStateHeader& Header, FNetHandle NetHandle) { Header.NetHandleId = NetHandle.GetId(); }
};

}

}
