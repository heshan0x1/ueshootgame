// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ChangeMaskUtil.h
// 模块：Iris / ReplicationSystem（辅助 & 周边）
// 功能：ChangeMask 位图的"存储载体 + 分配器 + 拷贝工具"集合。
//
// 关键抽象：
//   1. ChangeMaskStorageType        —— 与 FNetBitArrayView 一致的字类型（默认 uint32）。
//   2. FGlobalChangeMaskAllocator  —— 用全局 Heap 分配 ChangeMask 字数组。
//   3. FMemStackChangeMaskAllocator —— 用 FMemStackBase（线性栈分配器）分配，
//                                       适合"帧内一次性、PostSendUpdate 释放"场景。
//   4. FChangeMaskStorageOrPointer —— **小尺寸内联 / 大尺寸指针** 的双态存储：
//                                       BitCount<=64 时直接把位图塞进 8 字节本身，
//                                       否则 8 字节解释为 uint64 指针指向外部分配。
//   5. FChangeMaskUtil             —— 由 Storage + BitCount 构造 FNetBitArrayView，
//                                       以及若干 Copy 帮助函数。
//
// 这里没有提供独立的 OR/AND/POPCOUNT；这些操作直接由 FNetBitArrayView 的成员函数
// （Combine/IsAnyBitSet/CountSetBits 等）承担——本工具只解决"如何拿到 View"。
//
// 与 FReplicationWriter 的协作：
//   * FReplicationWriter 的 PerObjectInfo 里保存着每对象、每连接的累积
//     ChangeMaskStorageOrPointer；UpdateDirtyChangeMasks 时取出 View 与
//     FChangeMaskCache 中的本帧位图按位 OR。
// =====================================================================================

#pragma once
#include "Net/Core/NetBitArray.h"

class FMemStackBase;

namespace UE::Net::Private
{

// 中文：ChangeMask 的存储字类型——与 FNetBitArrayView 共享，便于直接以位图形式
// 解释/拼装。
using ChangeMaskStorageType = FNetBitArrayView::StorageWordType;

// 中文：全局堆分配器（FMemory::Malloc/Free），生命周期跨帧的 ChangeMask
// （例如 per-object PerConnection 累积位图）使用。
struct FGlobalChangeMaskAllocator
{
	void* Alloc(uint32 Size, uint32 Alignment);
	void Free(void* Pointer);
};

// 中文：MemStack 线性分配器适配，PostSendUpdate 一次性 Free 整段内存。
// Free() 是 no-op，因为 MemStack 不支持单块释放。
struct FMemStackChangeMaskAllocator
{
	FMemStackBase* MemStack;

	FMemStackChangeMaskAllocator(FMemStackBase* InMemStack);
	void* Alloc(uint32 Size, uint32 Alignment);
	void Free(void* Pointer);
};

/**
 * FChangeMaskStorageOrPointer
 * ---------------------------
 * 紧凑型 ChangeMask 容器：8 字节空间，按 BitCount 双态解释。
 *   * BitCount <= 64：内联存储——8 字节即位图本身（最多 64 位）。
 *   * BitCount  > 64：指针存储——8 字节解释为 uint64 形态的堆指针，指向
 *                     由 AllocatorType 分配的字数组。
 *
 * 用法：
 *   FChangeMaskStorageOrPointer Storage;
 *   FChangeMaskStorageOrPointer::Alloc(Storage, BitCount, Allocator);
 *   FNetBitArrayView View = FChangeMaskUtil::MakeChangeMask(Storage, BitCount);
 *   ... View.SetBit / ClearBit / Combine ...
 *   FChangeMaskStorageOrPointer::Free(Storage, BitCount, Allocator);
 *
 * 注意：Free 时必须传**相同的 BitCount**，否则双态判断会错位（小尺寸不释放、
 *       大尺寸误释放）。
 */
class FChangeMaskStorageOrPointer
{
public:
	typedef ChangeMaskStorageType StorageWordType;

	// 中文：临界值 64：UE NetBitArrayView 的 word=32-bit，64 位刚好两个字、
	// 占满 8 字节内联槽。
	static constexpr bool UseInlinedStorage(uint32 BitCount) { return BitCount <= 64; }
	// 中文：返回容纳 BitCount 位需要的字节数（按 32-bit 字向上取整）。
	static constexpr uint32 GetStorageSize(uint32 BitCount) { return FNetBitArrayView::CalculateRequiredWordCount(BitCount) * sizeof(StorageWordType); }

	// 中文：取位图首字指针——内联模式返回 &ChangeMaskOrPointer，外置模式
	// 返回 reinterpret 后的堆指针。
	inline StorageWordType* GetPointer(uint32 BitCount);
	inline const StorageWordType* GetPointer(uint32 BitCount) const;

public:
	// 中文：默认初始化为 0（既代表"内联模式下位图全 0"，也代表"外置模式下空指针"）。
	inline FChangeMaskStorageOrPointer() : ChangeMaskOrPointer(0) {}

	// Allocate storage for ChangeMask
	// If the changemask fits in the storage pointer no memory is allocated, if it does not fit memory is allocated from the provided Allocator
	// 中文：BitCount<=64 时不分配（直接复用 8 字节内联），否则向 Allocator 分配。
	template <typename AllocatorType>
	static void Alloc(FChangeMaskStorageOrPointer& Storage, uint32 BitCount, AllocatorType& Allocator);

	// 中文：Alloc + 同时构造一个全 0 的 FNetBitArrayView 以便立刻使用。
	template <typename AllocatorType>
	static FNetBitArrayView AllocAndInitBitArray(FChangeMaskStorageOrPointer& Storage, uint32 BitCount, AllocatorType& Allocator);

	// Free memory (if) allocated for ChangeMask
	// 中文：仅当外置模式时才真正 Free；内联模式 no-op。
	template <typename AllocatorType>
	static void Free(const FChangeMaskStorageOrPointer& Storage, uint32 BitCount, AllocatorType& Allocator);

private:
	// 中文：双态字段——小尺寸是位图本身、大尺寸是 reinterpret 的 uint64 指针。
	uint64 ChangeMaskOrPointer;
};

// 中文：纯静态工具集——围绕 FChangeMaskStorageOrPointer 的"造 View / 拷贝"。
struct FChangeMaskUtil
{
	// Construct a changemask from storage and bitcount
	// Storage must be allocated
	// 中文：把 (Storage, BitCount) 包成可读写 View；Storage 必须已 Alloc。
	static inline FNetBitArrayView MakeChangeMask(const FChangeMaskStorageOrPointer& Storage, uint32 BitCount);
	// 中文：把现成的 FNetBitArrayView 拷贝进 DestStorage（按 BitCount 派生大小）。
	static inline void CopyChangeMask(FChangeMaskStorageOrPointer& DestStorage, const FNetBitArrayView& ChangeMask);
	// 中文：在两个 StorageOrPointer 之间按 BitCount 字节级 memcpy。
	static inline void CopyChangeMask(FChangeMaskStorageOrPointer& DestStorage, FChangeMaskStorageOrPointer& SrcStorage, uint32 BitCount);
	// 中文：直接对裸字数组的 memcpy（最底层）。
	static inline void CopyChangeMask(ChangeMaskStorageType* DstData, const ChangeMaskStorageType* SrcData, uint32 BitCount);
};

//////////////////////////////////////////////////////////////////////////
// FChangeMaskStorageOrPointer Impl
//////////////////////////////////////////////////////////////////////////

inline FChangeMaskStorageOrPointer::StorageWordType* FChangeMaskStorageOrPointer::GetPointer(uint32 BitCount)
{
	// 内联模式：取 ChangeMaskOrPointer 自身的地址当字数组首址。
	// 外置模式：把 ChangeMaskOrPointer 解释为 uint64 形态的堆指针。
	uint64* Ptr = UseInlinedStorage(BitCount) ? reinterpret_cast<uint64*>(&ChangeMaskOrPointer) : reinterpret_cast<uint64*>(ChangeMaskOrPointer);
	return reinterpret_cast<StorageWordType*>(Ptr);
}

inline const FChangeMaskStorageOrPointer::StorageWordType* FChangeMaskStorageOrPointer::GetPointer(uint32 BitCount) const
{
	const uint64* Ptr = UseInlinedStorage(BitCount) ? reinterpret_cast<const uint64*>(&ChangeMaskOrPointer) : reinterpret_cast<const uint64*>(ChangeMaskOrPointer);
	return reinterpret_cast<const StorageWordType*>(Ptr);
}

template <typename AllocatorType>
void FChangeMaskStorageOrPointer::Alloc(FChangeMaskStorageOrPointer& Storage, uint32 BitCount, AllocatorType& Allocator)
{
	// 仅外置模式才真正分配；内联模式直接复用 8 字节内联槽（保持初始 0）。
	if (!UseInlinedStorage(BitCount))
	{
		Storage.ChangeMaskOrPointer = (uint64)(Allocator.Alloc(GetStorageSize(BitCount), alignof(StorageWordType)));
	}
}

template <typename AllocatorType>
FNetBitArrayView FChangeMaskStorageOrPointer::AllocAndInitBitArray(FChangeMaskStorageOrPointer& Storage, uint32 BitCount, AllocatorType& Allocator)
{
	if (!UseInlinedStorage(BitCount))
	{
		Storage.ChangeMaskOrPointer = (uint64)(Allocator.Alloc(GetStorageSize(BitCount), alignof(StorageWordType)));
	}

	// 中文：构造 BitArrayView 并 Reset（清 0），保证拿到手就是干净状态。
	return FNetBitArrayView(Storage.GetPointer(BitCount), BitCount, FNetBitArrayView::ResetOnInit);
}

template <typename AllocatorType>
void FChangeMaskStorageOrPointer::Free(const FChangeMaskStorageOrPointer& Storage, uint32 BitCount, AllocatorType& Allocator)
{
	// 内联模式无需释放；外置模式按调用方持有的同一 Allocator 释放。
	if (!UseInlinedStorage(BitCount))
	{
		Allocator.Free((void*)Storage.ChangeMaskOrPointer);
	}
}

//////////////////////////////////////////////////////////////////////////
// FChangeMaskUtil Impl
//////////////////////////////////////////////////////////////////////////

FNetBitArrayView FChangeMaskUtil::MakeChangeMask(const FChangeMaskStorageOrPointer& Storage, uint32 BitCount)
{
	// 中文：用 (字指针, 位数) 包装成可读写的 NetBitArrayView。
	// 注意 const_cast：FNetBitArrayView 的字段非常量，但实际操作语义由调用方决定。
	return MakeNetBitArrayView(Storage.GetPointer(BitCount), BitCount);
}

void FChangeMaskUtil::CopyChangeMask(ChangeMaskStorageType* DstData, const ChangeMaskStorageType* SrcData, uint32 BitCount)
{
	// 中文：按 BitCount 派生的字节数 memcpy。最尾字内的 padding 位会原样复制——
	// 调用方需保证 SrcData padding 已 ClearPaddingBits 过，否则会污染 Dst。
	FPlatformMemory::Memcpy(&DstData[0], &SrcData[0], FChangeMaskStorageOrPointer::GetStorageSize(BitCount));
}

void FChangeMaskUtil::CopyChangeMask(FChangeMaskStorageOrPointer& DestStorage, FChangeMaskStorageOrPointer& SrcStorage, uint32 BitCount)
{
	CopyChangeMask(DestStorage.GetPointer(BitCount), SrcStorage.GetPointer(BitCount), BitCount);
}

void FChangeMaskUtil::CopyChangeMask(FChangeMaskStorageOrPointer& DestStorage, const FNetBitArrayView& ChangeMask)
{
	const uint32 BitCount = ChangeMask.GetNumBits();
	// 中文：以 NoResetNoValidate 直接拿 View（不再清 0、不再校验大小），
	// 然后调用 NetBitArrayView::Copy 完成位级拷贝（含 padding 处理）。
	FNetBitArrayView DstChangeMask(DestStorage.GetPointer(BitCount), BitCount, FNetBitArrayView::NoResetNoValidate);
	DstChangeMask.Copy(ChangeMask);
}

}
