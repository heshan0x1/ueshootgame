// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   本 cpp 实现两件事：
//     1) FElementAllocationPolicy 的私有静态 Realloc/Free——把分配请求转发到
//        FNetSerializationContext::GetInternalContext()->Realloc/Free。
//        所有 Iris 序列化期间的动态分配都集中走 InternalContext，便于：
//          - 在 batch 失败时统一回滚（Iris 的内存账本机制）；
//          - 切换分配器（默认 FMemory，但可被注入）；
//          - 让 LLM 标签 Iris/IrisState 正确归类。
//     2) FNetSerializerAlignedStorage 的 AdjustSize/Free/Clone：无类型字节级
//        存储，支持任意对齐。AdjustSize 在对齐不满足或容量不够时重新分配并
//        memcpy 旧字节；缩容时 Memzero 多余尾部以避免后续 IsEqual 假阳性。
// =============================================================================

#include "Iris/Serialization/NetSerializerArrayStorage.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"

namespace UE::Net::AllocationPolicies
{

void* FElementAllocationPolicy::Realloc(FNetSerializationContext& Context, void* Original, SIZE_T Size, uint32 Alignment)
{
	// 转发到 Iris 的内部上下文 Realloc。
	// 该函数实现位于 InternalNetSerializationContext.cpp，最终会调到注入的
	// 分配器（默认 FMemory::Realloc，并打 LLM 标签 Iris/IrisState）。
	return Context.GetInternalContext()->Realloc(Original, Size, Alignment);
}

void FElementAllocationPolicy::Free(FNetSerializationContext& Context, void* Ptr)
{
	// 转发到 Iris 的内部上下文 Free（与上面的 Realloc 配对）。
	return Context.GetInternalContext()->Free(Ptr);
}

}


namespace UE::Net
{

void FNetSerializerAlignedStorage::AdjustSize(FNetSerializationContext& Context, SizeType InNum, SizeType InAlignment)
{
	if (!InNum)
	{
		// 请求容量为 0 → 直接 Free，不再分配空块（节省内存）。
		Free(Context);
		return;
	}

	// If the allocation isn't properly aligned or if the allocation is too small we make a new allocation.
	// 中文：两种情况需要全新分配：
	//        a) 请求字节数超过现有最大容量 StorageMaxCapacity；
	//        b) 现有分配指针对 InAlignment 不对齐——这是为什么需要保留
	//           StorageAlignment 字段：调用方可能多次以不同对齐请求同一存储。
	if ((InNum > StorageMaxCapacity) || !IsAligned(Data, InAlignment))
	{
		// 步骤 1：以请求的字节数+对齐分配新缓冲，并 Memzero 整块（统一保证
		// padding 干净，避免 IsEqual 误判）。
		void* NewData = Context.GetInternalContext()->Alloc(InNum, InAlignment);
		FMemory::Memzero(NewData, InNum);
		// Copy old data
		// 中文：如果之前持有数据，搬运 min(StorageNum, InNum) 字节；这里用
		// StorageNum（旧字节数）即可——若 InNum < StorageNum 多出来的部分会被
		// 上面的 Memzero 覆盖，多出来的旧字节本就不该被复制。
		if (StorageNum > 0)
		{
			FMemory::Memcpy(NewData, Data, StorageNum);
		}
		// 步骤 2：释放旧分配（Iris 内部 Free 对 nullptr 安全）。
		Context.GetInternalContext()->Free(Data);

		// 步骤 3：把新指针/规格写回成员。
		Data = static_cast<uint8*>(NewData);
		StorageNum = InNum;
		StorageMaxCapacity = InNum;
		StorageAlignment = InAlignment;
	}
	// Requested data size fits the current allocation
	else
	{
		// 中文：现有分配既足够大、又满足新对齐要求 → 原地更新长度，无需 Realloc。
		// Clear capacity we're not using anymore. If we're growing we don't need to clear as it has already been cleared.
		// 中文：若是缩容（InNum < StorageNum），把多余尾部 Memzero。
		// 若是扩容（InNum > StorageNum）则不需要清——超出 StorageNum 的字节
		// 在前一次 AdjustSize/Clone 时已经被 Memzero 过（不变式：
		// [StorageNum, StorageMaxCapacity) 区域始终为 0）。
		if (InNum < StorageNum)
		{
			FMemory::Memzero(static_cast<void*>(Data + InNum), StorageNum - InNum);
		}
		StorageNum = InNum;
		// Use the requested alignment as the StorageAlignment. This allows Clone to allocate using the minimum required alignment.
		// 中文：把对齐字段更新为本次请求值——后续 Clone 会按这个对齐分配，
		// 避免无谓的过度对齐。
		StorageAlignment = InAlignment;
	}
}

void FNetSerializerAlignedStorage::Free(FNetSerializationContext& Context)
{
	// 仅当 Data 非空时才 Free（Iris 内部 Free 对 nullptr 也安全，但显式判断更清晰）。
	if (Data != nullptr)
	{
		Context.GetInternalContext()->Free(Data);
	}

	// 把所有成员置 0——零初始化状态被本类视为"合法的空"。
	Data = nullptr;
	StorageNum = 0;
	StorageMaxCapacity = 0;
	StorageAlignment = 0;
}

void FNetSerializerAlignedStorage::Clone(FNetSerializationContext& Context, const FNetSerializerAlignedStorage& Source)
{
	// Only allocate and copy the exact amount of memory needed.
	// 中文：与 ArrayStorage 的 Clone 一样，调用前 *this 可能是 Source 的 memcpy
	// "悬挂副本"。本实现选择"按 Source 实际大小重新分配"——比 ArrayStorage 更
	// 紧凑，因为这里没有 slack 概念。
	if (Source.StorageNum > 0)
	{
		// 用与 Source 相同的对齐分配——保证后续 IsAligned 检测通过。
		Data = static_cast<uint8*>(Context.GetInternalContext()->Alloc(Source.StorageNum, Source.StorageAlignment));
		StorageNum = Source.StorageNum;
		StorageMaxCapacity = Source.StorageNum;
		StorageAlignment = Source.StorageAlignment;
		// Memcpy 直接拷贝字节。注意若 Source.Data 中含嵌套动态指针，调用方
		// 必须自行另外深拷贝（FNetSerializerAlignedStorage 不感知元素语义）。
		FMemory::Memcpy(Data, Source.Data, Source.StorageNum);
	}
	else
	{
		// Source 为空 → 我们也保持空状态。注意这里没有 Free 旧 Data——这是
		// 故意的：Iris 流程上调用 Clone 之前总会先 memcpy 块状态过来，让
		// *this.Data 指向 Source.Data；现在 Source 为空，意味着 *this.Data
		// 此刻也应为 nullptr，无需释放。如果约定不成立，调用方需先调 Free。
		Data = nullptr;
		StorageNum = 0;
		StorageMaxCapacity = 0;
		StorageAlignment = 0;
	}
}

}
