// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ChangeMaskCache.h
// 模块：Iris / ReplicationSystem（L4 业务核心层 - 辅助 & 周边）
// 功能：每帧 ChangeMask（变更位图）累积缓存。
//
// 概念回顾：
//   * Iris 的每个可复制对象都有一个固定位数的 ChangeMask，每位对应描述符里
//     某个成员（或某段成员）的"是否脏"。
//   * `Quantize` 阶段（FReplicationSystemImpl::QuantizeDirtyStateData）会
//     对当前帧脏对象执行 Internal-Buffer Copy + ChangeMask 计算，并把结果
//     写入本缓存的 per-object 槽位。
//   * 之后 `FReplicationWriter::UpdateDirtyChangeMasks` 会遍历该缓存，
//     把每个对象的本帧 ChangeMask **OR** 到自己（每连接）的累积 ChangeMask
//     上，从而把"一份脏数据"广播到所有相关连接。
//
// 帧内生命周期：
//   PrepareCache (帧首) → AddChangeMaskForObject* (Quantize 期间) →
//   ReplicationWriter 读取 → ResetCache (PostSendUpdate)。
//
// 内存策略：
//   * Indices/Storage 都用 TArray + 内联 Allocator。注释中提到"可以用线性
//     分配器（per-frame arena），并在 PostSendUpdate 释放"——目前实现保
//     留 capacity，靠 Reset 复用，避免每帧分配。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Net/Core/NetBitArray.h"

namespace UE::Net::Private
{

/**
 * Cache used to propagate captured changemasks to all connections
 * This data could be allocated using linear allocator and released in PostSendUpdate
 *
 * 中文说明：
 *   FChangeMaskCache —— 每帧脏对象 ChangeMask 的"集中暂存区"，由
 *   QuantizeDirtyStateData 阶段写入、ReplicationWriter（每连接）阶段读取。
 *
 *   数据布局（典型 SoA + 偏移）：
 *     ┌──────────── Indices[] ────────────┐    ┌──────── Storage[] ────────┐
 *     │ {InternalIndex, StorageOffset, …} │ →  │ uint32 word #0            │
 *     │ {InternalIndex, StorageOffset, …} │    │ uint32 word #1            │
 *     │ …                                 │    │ uint32 word #2            │
 *     └───────────────────────────────────┘    │ uint32 word #3            │
 *                                              │ …                         │
 *                                              └───────────────────────────┘
 *   Indices 中每条 FCachedInfo 用 StorageOffset 指向 Storage 里属于这个
 *   对象的位图首字（按 FNetBitArrayView 的 32-bit 字对齐）。
 *
 *   一个对象可能有 3 种条目：
 *     1. 真正带脏 ChangeMask 的（AddChangeMaskForObject）；
 *     2. 仅"通知 SubObject 的 Owner 也脏"的标记条目（AddSubObjectOwnerDirty），
 *        没有自己的 Storage；
 *     3. 空 ChangeMask 占位（AddEmptyChangeMaskForObject），通常用于声明
 *        "对象本帧被 Poll 但所有成员都没变"。
 */
struct FChangeMaskCache
{
	// Objects that were copied this frame
	// 中文：本帧被 Copy/Quantize 处理过的对象在缓存中的元数据条目。
	struct FCachedInfo
	{
		// 该对象在 FNetRefHandleManager 里的 InternalIndex（per-RS 的稠密索引）。
		uint32 InternalIndex;
		// 该对象的位图首字在 Storage[] 中的字下标；类型为 24-bit 位段，足以
		// 寻址 16M 个 32-bit 字，远超实际所需。
		uint32 StorageOffset : 24U;
		// 是否要顺带把"作为 SubObject 时的 Owner（根对象）"也标脏；
		// 用于触发 owner 的 PreReplication / 调度。
		uint32 bMarkSubObjectOwnerDirty : 1U;
		// 本条目是否真的有脏的 ChangeMask（区分 AddEmpty/AddSubObjectOwnerDirty）。
		uint32 bHasDirtyChangeMask : 1;
	};
	// 元数据列表（按 Add 顺序追加，长度小用 Inline 1）。
	TArray<FCachedInfo, TInlineAllocator<1>> Indices;
	// 位图字存储（所有对象共用一段连续内存，靠 Indices.StorageOffset 切片）。
	TArray<uint32, TInlineAllocator<1>> Storage;

	/** Prepare cache for use by reserving space for expected data*/
	// 中文：帧首预留容量；Reset(N) 等价于 Reserve(N)+SetNum(0)，不会缩容。
	inline void PrepareCache(uint32 IndexCount, uint32 StorageSize);

	/** Empty cache without freeing memory */
	// 中文：清空但保留容量，PostSendUpdate 调用以便下一帧复用内存。
	inline void ResetCache();
	
	/** Empty cached data and free memory */
	// 中文：彻底释放（销毁/Deinit 时使用）。
	inline void EmptyCache();

	/** The ref is only valid until the next Add call. */
	// 中文：为对象追加一段 BitCount 位的 ChangeMask；返回的引用一旦再次 Add
	// 触发 TArray 扩容就会失效，调用方需要先用完再 Add。
	inline FCachedInfo& AddChangeMaskForObject(uint32 InternalIndex, uint32 BitCount);

	/** The ref is only valid until the next Add call. */
	// 中文：仅追加"该对象是某个 SubObject 的 Owner，需要被一同处理"的标记，
	// 不分配 Storage（仅用 InternalIndex 做信号）。
	inline FCachedInfo& AddSubObjectOwnerDirty(uint32 InternalIndex);

	/** The ref is only valid until the next Add call. */
	// 中文：追加一个不分配 Storage、bHasDirtyChangeMask=0 的占位条目，
	// 表示对象被处理过但没有真正脏成员；ReplicationWriter 据此区分
	// "完全无变化对象" 与 "未在缓存中出现的对象"。
	inline FCachedInfo& AddEmptyChangeMaskForObject(uint32 InternalIndex);

	/** The pointer is only valid until the next Add call. */
	// 中文：取该对象 ChangeMask 的字指针起点（指向 Storage 内部）。
	inline uint32* GetChangeMaskStorage(const FCachedInfo& Info);

	/** Reverts the effects of the last Add call. */
	// 中文：撤销最近一次 Add（同时回退 Indices 和 Storage 的 Num）。常用于
	// "尝试性追加但发现无变化"的回滚场景。
	inline void PopLastEntry();
};

void FChangeMaskCache::PrepareCache(uint32 IndexCount, uint32 StorageSize)
{
	Indices.Reset(IndexCount);
	Storage.Reset(StorageSize);
}

void FChangeMaskCache::ResetCache()
{
	Indices.Reset();
	Storage.Reset();
}

void FChangeMaskCache::EmptyCache()
{
	Indices.Empty();
	Storage.Empty();
}

FChangeMaskCache::FCachedInfo& FChangeMaskCache::AddChangeMaskForObject(uint32 InternalIndex, uint32 BitCount)
{
	// 记下当前 Storage 末尾位置作为本对象位图的起始字偏移。
	const uint32 StorageIndex = Storage.Num();
	// 把"BitCount 位"对齐到 32-bit 字数（向上取整）。
	const uint32 WordCount = FNetBitArrayView::CalculateRequiredWordCount(BitCount);

	FCachedInfo Info;
	Info.InternalIndex = InternalIndex;
	Info.StorageOffset = StorageIndex;
	Info.bMarkSubObjectOwnerDirty = 0U;
	// 注意：此处 bHasDirtyChangeMask 仍为 0；调用方在真正写入"非全 0"的位图后
	// 再决定是否置 1。
	Info.bHasDirtyChangeMask = 0U;

	// 预先把所需位图字以 0 追加进 Storage（Quantize 后再按位 OR）。
	Storage.AddZeroed(WordCount);

	return Indices.Add_GetRef(Info);
}

FChangeMaskCache::FCachedInfo& FChangeMaskCache::AddEmptyChangeMaskForObject(uint32 InternalIndex)
{
	FCachedInfo Info;
	Info.InternalIndex = InternalIndex;
	Info.StorageOffset = 0U;
	Info.bMarkSubObjectOwnerDirty = 0U;
	Info.bHasDirtyChangeMask = 0U;

	return Indices.Add_GetRef(Info);
}

uint32* FChangeMaskCache::GetChangeMaskStorage(const FCachedInfo& Info)
{
	return &Storage.GetData()[Info.StorageOffset];
}

void FChangeMaskCache::PopLastEntry()
{
	const FCachedInfo& Info = Indices.Last();

	// 同时把 Storage 缩回到本对象进入前的字偏移，避免内存浪费但保留 capacity。
	Storage.SetNum(Info.StorageOffset, EAllowShrinking::No);
	Indices.Pop(EAllowShrinking::No);
}

inline FChangeMaskCache::FCachedInfo& FChangeMaskCache::AddSubObjectOwnerDirty(uint32 InternalIndex)
{
	FCachedInfo Info;
	Info.InternalIndex = InternalIndex;
	Info.StorageOffset = 0U;
	Info.bMarkSubObjectOwnerDirty = 1U;
	Info.bHasDirtyChangeMask = 0U;

	return Indices.Add_GetRef(Info);
}

}
