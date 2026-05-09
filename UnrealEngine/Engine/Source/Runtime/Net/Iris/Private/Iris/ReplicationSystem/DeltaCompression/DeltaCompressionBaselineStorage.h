// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaselineStorage.h（Private）
// ---------------------------------------------------------------------------------------------
// 模块定位：Iris / ReplicationSystem / DeltaCompression 子模块的"基线状态池"。
//
// 一句话职责：
//   提供按"基线槽位"(StateInfoIndex) 寻址的 StateBuffer 池，对外暴露
//   Reserve / Commit / AddRef / Release / Get 五种基本操作。底层真正的内存分配委托给
//   FReplicationStateStorage（与"当前发送状态" CurrentSendState 共用一份分配器）。
//
// 设计要点：
//   1. 槽位号(StateInfoIndex) 0 永远是 invalid（与 NetRefHandleManager 统一约定）。
//   2. 内部存储采用 TChunkedArray，按 BaselineStateInfoGrowCount 一次扩容 256 个槽位，
//      避免大块连续 realloc 导致已发出的指针失效。
//   3. 引用计数：多连接共享同一基线时通过 AddRef/Release 管理；引用归零时调用
//      ReplicationStateStorage::FreeBaseline 真正释放底层内存。
//   4. Reserve / Commit 二段式：
//      - ReserveBaselineForCurrentState：同帧内多个 Conn 申请同一对象基线时，先把"占位"挂到
//        当前 SendState 的尾部（不复制数据），调用方仍能拿到指向 SendState 的指针进行序列化对比；
//      - OptionallyCommitAndDoReleaseBaseline：本帧结束（或 refcount 减到 0）时，
//        若仍有引用 → 真正 Commit（克隆当前 state 成基线副本），否则 Cancel reservation。
//      这种延迟 Commit 让"本帧内被多个 Conn 引用同一基线"只产生一次拷贝。
//
// 与上层关系：
//   * FDeltaCompressionBaselineManager 是唯一的客户：它在 PreSendUpdate.ConstructBaselineSharing
//     Context / CreateBaseline / DestructBaselineSharingContext 中编排 Reserve/AddRef/Release。
//   * 与 FReplicationStateStorage：StateBuffer 实际归后者所有，本类只持索引/引用计数。
// =============================================================================================

#pragma once

#include "Containers/Array.h"
#include "Containers/ChunkedArray.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaseline.h"

namespace UE::Net
{
	class FReplicationStateStorage;
}

namespace UE::Net::Private
{

/** 基线槽位号类型。0 = 无效。1..N 为有效槽。 */
using DeltaCompressionBaselineStateInfoIndexType = uint32;

/** 哨兵：表示"无效槽位号"。InitParams.MaxBaselineCount + 1 个槽位中第 0 个永远占用。 */
constexpr DeltaCompressionBaselineStateInfoIndexType InvalidDeltaCompressionBaselineStateInfoIndex = 0;

/** 初始化参数：来自 FDeltaCompressionBaselineManager::Init。 */
struct FDeltaCompressionBaselineStorageInitParams
{
	/** 真正分配/释放 state buffer 的接口。 */
	FReplicationStateStorage* ReplicationStateStorage = nullptr;
	/** 池上限：MaxDeltaCompressedObjectCount * MaxConnectionCount * 2（双基线槽）。 */
	uint32 MaxBaselineCount = 0;
};

/**
 * 暴露给上层的基线槽位描述（包含 StateBuffer + 槽位号），不持有所有权。
 * 槽位号用于后续 AddRef/Release/Get/Cancel/Commit；StateBuffer 直接给 Writer 拿去比对/拷贝。
 */
class FDeltaCompressionBaselineStateInfo
{
public:
	bool IsValid() const { return StateBuffer != nullptr; }

	uint8* StateBuffer = nullptr;
	DeltaCompressionBaselineStateInfoIndexType StateInfoIndex = InvalidDeltaCompressionBaselineStateInfoIndex;
};


/**
 * 基线状态池。无线程安全（必须在 ReplicationSystem 单线程编排路径中访问）。
 */
class FDeltaCompressionBaselineStorage
{
public:
	FDeltaCompressionBaselineStorage();
	~FDeltaCompressionBaselineStorage();

	/** 初始化：保存 ReplicationStateStorage，按 MaxBaselineCount+1 分配 used 位图。 */
	void Init(FDeltaCompressionBaselineStorageInitParams& InitParams);
	/** 反初始化：释放所有未释放的槽位（leaks 在调试构建中会触发 check）。 */
	void Deinit();

	/**
	 * 立即建立一份基线：克隆当前 SendState 到一份新 buffer，引用计数置 1。
	 * 用法：典型外部调用（不走 reservation 路径）。
	 */
	FDeltaCompressionBaselineStateInfo CreateBaselineFromCurrentState(uint32 ObjectIndex);
	/** 同一基线被另一连接共享：引用计数 +1。 */
	void AddRefBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex);
	/** 引用计数 -1，归零时调用 ReplicationStateStorage::FreeBaseline 释放底层 state buffer。 */
	void ReleaseBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex);
	/* GetBaseline requires the StateInfoIndex to point to a created baseline, not one that is just reserved. */
	// 中文：仅当 StateInfoIndex 指向已 Created 的槽位时合法（不能用于尚处 Reserve 阶段的槽位）。
	FDeltaCompressionBaselineStateInfo GetBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex) const;

	/*
	 * Reserves a baseline for the current state. The returned info will contain a pointer to the current state.
	 * OptionallyCommitAndReleaseBaseline needs to be called later which will clone the current state if there are
	 * references after decrementing the ref count. AddRefBaseline and ReleaseBaseline may be called in between
	 * the first call to ReserveBaselineForCurrentState and a call to OptionallyCommitAndDoReleaseBaseline.
	 *
	 * ----------------------------------------------------------------------------
	 * 中文：为当前 state 预留一个基线槽位（不立即拷贝数据）。
	 * 返回值的 StateBuffer 指向当前 SendState（不是副本）。
	 * 调用顺序：
	 *   ReserveBaselineForCurrentState            (refcount = 1)
	 *   AddRefBaseline / ReleaseBaseline ...       （多连接共享或部分释放）
	 *   OptionallyCommitAndDoReleaseBaseline      (refcount-1：>0 则真正 Commit；=0 则 Cancel)
	 * ----------------------------------------------------------------------------
	 */
	FDeltaCompressionBaselineStateInfo ReserveBaselineForCurrentState(uint32 ObjectIndex);
	/* Decrements the ref count on the baseline and frees associated storage if it's zero and clones the current state if it's still referenced.  */
	// 中文：refcount-1：归零→Cancel reservation；非零→Commit reservation（真正克隆当前 state 成持久基线）。
	void OptionallyCommitAndDoReleaseBaseline(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex);
	/* When you want to retrieve the info for a reserved baseline when GetBaseline() cannot be used. */
	// 中文：拿到 Reserve 阶段的 info（与 GetBaseline 区别：此时数据在 SendState 末尾而非独立 buffer）。
	FDeltaCompressionBaselineStateInfo GetBaselineReservationForCurrentState(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex);


private:
	enum : unsigned
	{
		/** TChunkedArray 单段大小，避免大 realloc 失效已分发的指针。 */
		BaselineStateInfoGrowCount = 256U,
	};

	/**
	 * 每个槽位的内部记录：
	 *   - StateBuffer：指向 ReplicationStateStorage 中的 buffer；
	 *   - ObjectIndex：归属对象（释放/Commit 时回查 ReplicationStateStorage 需要）；
	 *   - RefCount  ：当前共享该基线的连接数。新槽位默认 1。
	 */
	struct FInternalBaselineStateInfo
	{
		uint8* StateBuffer = nullptr;
		uint32 ObjectIndex = 0;
		uint32 RefCount = 1;
	};

	void ConstructBaselineStateInfo(FInternalBaselineStateInfo* BaselineStateInfo) const;
	void DestructBaselineStateInfo(FInternalBaselineStateInfo* BaselineStateInfo);

	/** 申请下一个空闲槽位（FindFirstZero 位图）。返回 InvalidDeltaCompressionBaselineStateInfoIndex 表示池满。 */
	DeltaCompressionBaselineStateInfoIndexType AllocBaselineStateInfo();
	/** 释放槽位（位图清位 + DestructBaselineStateInfo 调用底层 FreeBaseline）。 */
	void FreeBaselineStateInfo(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex);
	/** Deinit 时清掉所有槽位（仅释放底层 buffer，不做 RefCount 检查）。 */
	void FreeAllBaselineStateInfos();

	FInternalBaselineStateInfo* GetBaselineStateInfo(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex);
	const FInternalBaselineStateInfo* GetBaselineStateInfo(DeltaCompressionBaselineStateInfoIndexType StateInfoIndex) const;

private:
	/** "已使用槽位"位图。下标 0 永远 set（占位 InvalidIndex）。 */
	FNetBitArray UsedBaselineStateInfos;
	/** 内部记录数组。按 BaselineStateInfoGrowCount 段扩容；指针稳定。 */
	TChunkedArray<FInternalBaselineStateInfo, BaselineStateInfoGrowCount*sizeof(FInternalBaselineStateInfo)> BaselineStateInfos;
	/** 实际 buffer 的所有者（AllocBaseline / FreeBaseline / Reserve / Commit / Cancel 接口）。 */
	FReplicationStateStorage* ReplicationStateStorage = nullptr;
};

inline FDeltaCompressionBaselineStorage::FInternalBaselineStateInfo* FDeltaCompressionBaselineStorage::GetBaselineStateInfo(uint32 StateInfoIndex)
{
	return &BaselineStateInfos[StateInfoIndex];
}

inline const FDeltaCompressionBaselineStorage::FInternalBaselineStateInfo* FDeltaCompressionBaselineStorage::GetBaselineStateInfo(uint32 StateInfoIndex) const
{
	return &BaselineStateInfos[StateInfoIndex];
}

}
