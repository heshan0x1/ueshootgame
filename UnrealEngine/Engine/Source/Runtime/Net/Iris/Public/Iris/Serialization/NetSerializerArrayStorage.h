// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   本头文件定义 Iris 序列化层的"动态量化存储"工具：
//     - FNetSerializerArrayStorage<T, Policy>：用于在 Quantized 状态中存储
//       数量可变的同类型元素数组（典型场景：FArrayPropertyNetSerializer、
//       FStringNetSerializer、FFastArraySerializer 等）。
//     - FNetSerializerAlignedStorage：无类型、自定义对齐的字节缓冲区版本，
//       用于 InstancedStruct/Polymorphic 等"运行时确定大小"的存储。
//     - 两个分配策略：
//         FElementAllocationPolicy        总是间接堆分配（最简单）
//         TInlinedElementAllocationPolicy<N>  小于等于 N 个元素时内联在容器
//                                          内部，超过 N 才"溢出"到间接堆分配
//
//   重要约束（参见类注释）：
//     1) 仅在"Quantized 复制状态"内部使用——它的 Free/Clone 必须配合
//        NetSerializer 的 FreeDynamicState/CloneDynamicState 钩子；并且
//        NetSerializer 必须显式声明 static constexpr bool bHasDynamicState = true。
//     2) 零初始化的存储被视为"有效的空状态"。这保证了 Quantize 函数无需先
//        显式构造，可以直接 Memcpy 0 后再 AdjustSize。
//     3) 使用的元素类型必须是 POD 且可平凡赋值（static_assert 强制）。这是
//        因为 Iris 在 Quantized 表示之间会用 Memcpy/Memmove 大块搬运状态。
//     4) 内存通过 FNetSerializationContext::GetInternalContext()->Alloc/Free/Realloc
//        分配，所有分配都由 Iris 的池/子系统统一管理（详见 .cpp）。
// =============================================================================

#pragma once

#include "HAL/Platform.h"
#include "Iris/IrisConfig.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Templates/AlignmentTemplates.h"
#include "Templates/IsPODType.h"

namespace UE::Net
{

namespace AllocationPolicies
{

/**
 * Default AllocationPolicy for FNetSerializerArrayStorage helper class
 *
 * 中文：默认分配策略——总是通过间接（堆）分配。该策略不预留任何内联槽位，
 *      容量为 0 时不持有内存，AdjustSize/Realloc 会经由 Iris 的内存上下文分配。
 *      析构时若 Data != nullptr 会 check 失败——上层必须先调用 Free 释放。
 */
class FElementAllocationPolicy
{
public:
	using SizeType = uint32;

	/**
	 * 内层模板：把 Allocator 绑定到具体的 ElementType，便于 sizeof/alignof 查询。
	 */
	template<typename ElementType>
	class ForElementType
	{
	public:
		/** 默认构造：指针归零，认为这是一个"空且有效"的状态。 */
		ForElementType() : Data(nullptr) {}

		/**
		 * 析构：要求调用方在销毁前主动调用 Free()——
		 * 因为 dtor 没有 Context 参数，无法访问 Iris 内部分配器。
		 * 检测到泄漏时 checkf 触发，便于尽早暴露错误。
		 */
		~ForElementType() {	checkf(Data == nullptr, TEXT("Data allocated for dynamic states must be explictly freed")); }

		/** 取得当前持有的元素首地址（可能为 nullptr）。 */
		ElementType* GetAllocation() const { return Data; }

		/** 重置为"空且有效"状态——配合 Clone 时使用，注意不会释放原内存。 */
		void Initialize() { Data = nullptr; }
	
		/** 调整为 NumElements 的容量；NumElements==0 等价于 Free。详见 .cpp 实现。 */
		void ResizeAllocation(FNetSerializationContext& Context, SizeType PreviousNumElements, SizeType NumElements);

		/** 该策略下"新容量 == 请求元素数"，无 slack 预留。 */
		SizeType CalculateNewCapacity(SizeType NumElements) const { return NumElements; }

		/** 是否已分配真正的内存（区分"空状态" vs "有内存"）。 */
		bool HasAllocation() const { return !!Data; }

		/** 默认分配策略没有内联槽位，初始容量为 0。 */
		SizeType GetInitialCapacity() const { return 0; }

	private:

		// 显式禁用拷贝构造与拷贝赋值——存储所有权语义不允许浅拷贝。
		ForElementType(const ForElementType&);
		ForElementType& operator=(const ForElementType&);

		/** 当前持有的堆指针；nullptr 表示空。 */
		ElementType* Data;
	};

private:

	/** 转发到 FNetSerializationContext::GetInternalContext()->Free。 */
	static IRISCORE_API void Free(FNetSerializationContext& Context, void* Ptr);
	/** 转发到 FNetSerializationContext::GetInternalContext()->Realloc，按字节大小+对齐分配。 */
	static IRISCORE_API void* Realloc(FNetSerializationContext& Context, void* Original, SIZE_T Size, uint32 Alignment);
};

/**
 * The InlinedElementAllocationPolicy allocates up to a specified number of elements in the same allocation as the container.
 * Any allocation needed beyond that causes all data to be moved into an indirect allocation.
 *
 * 中文：内联+溢出分配策略。
 *      容量 ≤ NumInlineElements 时数据存放在容器内部 InlineData[]（无堆分配）；
 *      一旦超过 N，所有数据会被搬到 SecondaryAllocator 管理的间接堆内存中
 *      （GetInlineElements 不再可用）。
 *      用途：在 Quantized 状态较小的常见情况下避免堆分配；典型 N 取 1~4。
 *
 *  @tparam NumInlineElements 内联槽位数（至少 1）。
 *  @tparam SecondaryAllocator 溢出后使用的二级分配策略，默认 FElementAllocationPolicy。
 */
template <uint32 NumInlineElements, typename SecondaryAllocator = FElementAllocationPolicy>
class TInlinedElementAllocationPolicy
{
public:
	using SizeType = typename SecondaryAllocator::SizeType;

	template<typename ElementType>
	class ForElementType
	{
	public:

		/** 默认构造——InlineData[] 由 TTypeCompatibleBytes 提供未初始化字节，SecondaryData 也未分配。 */
		ForElementType() {}

		/**
		 * 取首地址：优先返回 SecondaryData（已溢出到间接分配的话），否则返回内联槽位指针。
		 * 上层不应缓存指针——AdjustSize 一旦跨越 N 边界就会失效。
		 */
		ElementType* GetAllocation() const
		{
			if (ElementType* Result = SecondaryData.GetAllocation())
			{
				return Result;
			}
			return GetInlineElements();
		}

		/** 等价于 SecondaryData.Initialize()——内联区不需要重置（POD）。 */
		void Initialize() { SecondaryData.Initialize(); }

		/** 调整容量；逻辑见 .cpp（在 N 边界两侧需要做 RelocateConstructItems 搬运）。 */
		void ResizeAllocation(FNetSerializationContext& Context, SizeType PreviousNumElements, SizeType NumElements);

		/** 计算"实际分配容量"：≤N 时一律用 N（充分利用内联槽位作为天然 slack），>N 时委托给 SecondaryAllocator。 */
		SizeType CalculateNewCapacity(SizeType NumElements) const;

		/** 注意：仅当数据"溢出到间接分配"时返回 true；纯内联状态返回 false。 */
		bool HasAllocation() const { return SecondaryData.HasAllocation(); }

		/** 初始容量等于内联槽位数——无需任何分配即可容纳 N 个元素。 */
		SizeType GetInitialCapacity() const { return NumInlineElements; }

	private:
		// 同样禁用拷贝。
		ForElementType(const ForElementType&);
		ForElementType& operator=(const ForElementType&);

		/** The data is stored in this array if less than NumInlineElements is needed. */
		/** 中文：内联槽位——使用 TTypeCompatibleBytes 是为了避免对 ElementType 的构造/析构副作用（POD 保证）。 */
		TTypeCompatibleBytes<ElementType> InlineData[NumInlineElements];

		/** The data is allocated through the indirect allocation policy if more than NumInlineElements is needed. */
		/** 中文：溢出后的间接分配；仅在容量>N 时持有真正的堆指针。 */
		typename SecondaryAllocator::template ForElementType<ElementType> SecondaryData;

		/** @return the base of the aligned inline element data */
		/** 中文：把 InlineData 字节区强转为 ElementType*（POD 类型上类型双关安全）。 */
		ElementType* GetInlineElements() const { return (ElementType*)InlineData; }
	};
};

}

/** 
 * Helper class to manage storage of dynamic arrays in quantized data
 * FNetSerializerArrayStorage is only intended to be used from within Quantized replication states that requires dynamic storage as it has very specific expectations and limitations. 
 * NOTE: A NetSerializer using FNetSerializerArrayStorage MUST specify the trait bHasDynamicState
 * A zero constructed state is considered valid
 *
 * 中文：在 Quantized 状态中存储"动态长度数组"的工具类。
 *      关键约束：
 *        - 仅供 Quantized 复制状态内部使用；不要把它放进非 Quantized 数据。
 *        - 元素类型必须是 POD（编译期 static_assert 强制）。
 *        - 使用此类的 NetSerializer 必须声明 bHasDynamicState=true，否则 Iris
 *          不会为该 Serializer 调用 Free/Clone 钩子，导致内存泄漏。
 *        - 全零初始化（FMemory::Memzero(state)）后立即 AdjustSize/Free 都是合法的；
 *          这一约定让 Iris 在 batch 反量化前直接 Memzero 大块状态变得安全。
 *
 *  @tparam QuantizedElementType 元素类型（必须 POD + TriviallyCopyAssignable）。
 *  @tparam AllocationPolicy 分配策略，默认 FElementAllocationPolicy（纯堆分配）。
 */
template <typename QuantizedElementType, typename AllocationPolicy = AllocationPolicies::FElementAllocationPolicy>
class FNetSerializerArrayStorage
{
public:
	// Verify that type is pod and trivially copyable
	// 中文：编译期断言 ElementType 必须是 POD 且可平凡赋值——
	//      Iris 会用 Memcpy/Memmove 大块搬运 Quantized 状态。
	static_assert(TIsPODType<QuantizedElementType>::Value, "Only pod types are supported by FNetSerializerArrayHelper");
	static_assert(TIsTriviallyCopyAssignable<QuantizedElementType>::Value, "Only types are triviallyCopyAssignable is supported by FNetSerializerArrayHelper");

	typedef FNetSerializerArrayStorage<QuantizedElementType, AllocationPolicy> ArrayType;
	typedef QuantizedElementType ElementType;

	typedef typename AllocationPolicy::template ForElementType<ElementType> ElementAllocatorType;
	typedef typename AllocationPolicy::SizeType SizeType;

public:

	/**
	 * Constructor - Typically not invoked as states containing FNetSerializerArrayStorage typically are zero initialized
	 *
	 * 中文：默认构造。注意：实际上在 Iris 流程里 Quantized 状态多以 Memzero 初始化，
	 *      所以本 ctor 很少被调用。它只是把 ArrayNum=0、ArrayMaxCapacity 设为
	 *      AllocatorInstance 的初始容量（默认策略下为 0；内联策略下为 N）。
	 */
	inline FNetSerializerArrayStorage();

	/**
	 * AdjustSize - Adjust the size of FNetSerializerArrayStorage as needed
	 * The state storage in which the storage is located is expected to be in a valid state which can either be zero initialized or a previous valid state
	 *
	 * 中文：把元素数量调整为 InNum。
	 *      - 若 InNum > 当前容量：通过分配策略扩容，并对新增区域 Memzero（防止
	 *        旧分配器返回的"脏"页或残留 padding 被后续 Quantize 误读）。
	 *      - 若 InNum < 当前 Num：缩容（具体策略由 AllocationPolicy 决定，可能
	 *        保留容量也可能立刻释放）。
	 *      - 调用前要求当前状态是"零初始化"或者"前一次合法的状态"——禁止在
	 *        未初始化字节上直接调用本函数。
	 */
	inline void AdjustSize(FNetSerializationContext& Context, SizeType InNum);
	
	/**
	 * Free - Free allocated memory and reset state. it is valid to pass in a zero-initialized state 
	 * The state storage in which the storage is located is expected to be in a valid state which can either be zero initialized or a previous valid state.	
	 * Should typically only be called from within a the implemntation of NetSerializer::FreeDynamicState()
	 *
	 * 中文：释放所有动态分配的内存，并把成员重置为"零初始化等价的空状态"。
	 *      允许对零初始化状态调用——AllocatorInstance 的 ResizeAllocation(0) 内
	 *      会做空指针保护。
	 *      调用时机：仅应在 NetSerializer::FreeDynamicState 内调用，由 Iris 在
	 *      销毁/重置 Quantized 状态时驱动。
	 */
	inline void Free(FNetSerializationContext& Context);

	/**
	 * Clone - Clone dynamic storage from Source
	 * No assumptions of the validity of the target is made, as this is typically called AFTER a memcopy is made to the target state
	 * which invalidates all dynamic data
	 * Should typically only be called from within a the implemntation of NetSerializer::CloneDynamicState()
	 *
	 * 中文：从 Source 克隆动态存储到 *this。
	 *      关键点：调用前 *this 的成员可能"看起来已经持有指针"——因为 Iris 在
	 *      调用 CloneDynamicState 之前通常会先把整块 Quantized 状态 Memcpy 过来，
	 *      这会让 AllocatorInstance.Data 指向 Source 的内存。所以 Clone 必须先
	 *      调用 AllocatorInstance.Initialize() 把指针归零（防止释放共享内存），
	 *      然后再分配自己的副本并 CopyAssignItems。
	 *      调用时机：仅应在 NetSerializer::CloneDynamicState 内调用。
	 */
	inline void Clone(FNetSerializationContext& Context, const ArrayType& Source);

	/** 取首地址（const）——若空返回 nullptr。 */
	const ElementType* GetData() const { return AllocatorInstance.GetAllocation(); }
	/** 取首地址（mutable）。 */
	ElementType* GetData() { return AllocatorInstance.GetAllocation(); }
	/** 当前元素数量（不是容量）。 */
	SizeType Num() const { return ArrayNum; }

private:
	/** 由 AllocationPolicy 注入的元素分配器（持有真实指针/内联区/SecondaryData）。 */
	ElementAllocatorType AllocatorInstance;
	/** 当前已"逻辑使用"的元素数。 */
	SizeType ArrayNum;
	/** 已分配的最大容量；用于决定 AdjustSize 是否需要 Realloc。 */
	SizeType ArrayMaxCapacity;
};

/**
 * Helper class to manage storage of untyped dynamic storage with arbitrary alignment in quantized data.
 * If storage is used to store elements that themselves uses dynamic allocations the user must take care to properly free and clone such elements properly prior to and/or after changing sizes.
 * FNetSerializerAlignedStorage is only intended to be used from within Quantized replication states that requires dynamic storage as it has very specific expectations and limitations.
 * NOTE: A NetSerializer using FNetSerializerAlignedStorage MUST specify the trait bHasDynamicState and forward appropriate calls to Clone and Free to this class.
 * A zero constructed state is considered valid.
 *
 * 中文：无类型 + 自定义对齐的动态存储版本。
 *      与 FNetSerializerArrayStorage 的差异：
 *        - 元素类型在编译期未知（按 uint8* 字节存放），对齐通过 InAlignment 运行时传入；
 *        - 仅做整块字节的分配/拷贝/释放，不会调用任何元素构造/析构；
 *        - 如果存储的"字节"内本身又含有需要释放的动态指针（如嵌套 ArrayStorage），
 *          调用方必须在 AdjustSize/Free 之前/之后亲自处理这些子对象的生命周期。
 *      使用场景：FInstancedStructNetSerializer / FPolymorphicStructNetSerializer——
 *      Quantized 状态的具体大小/对齐要等运行时确定多态目标后才知道。
 */
class FNetSerializerAlignedStorage
{
public:
	// Use whatever SizeType that is typically used by FNetSerializerArrayStorage
	// 中文：与默认元素分配策略保持一致的 SizeType（uint32），便于跨容器互操作。
	typedef typename AllocationPolicies::FElementAllocationPolicy::SizeType SizeType;

public:
	/**
	 * AdjustSize - Adjust the size of FNetSerializerArrayStorage as needed
	 * The state storage in which the storage is located is expected to be in a valid state which can either be zero initialized or a previous valid state
	 *
	 * 中文：把存储调整为 InNum 字节、InAlignment 字节对齐。
	 *      若现有分配的对齐不满足或容量太小，会重新分配并把旧字节 memcpy 过去；
	 *      若现有分配足够大且对齐合规，仅更新 StorageNum（缩容时 Memzero 多余尾部）。
	 *      InNum==0 等价于调用 Free。
	 */
	IRISCORE_API void AdjustSize(FNetSerializationContext& Context, SizeType InNum, SizeType InAlignment);

	/**
	 * Free - Free allocated memory and reset state. it is valid to pass in a zero-initialized state
	 * The state storage in which the storage is located is expected to be in a valid state which can either be zero initialized or a previous valid state.
	 * Should typically only be called from within a the implemntation of NetSerializer::FreeDynamicState()
	 *
	 * 中文：释放并把所有成员置零。可以对零初始化状态安全调用。
	 */
	IRISCORE_API void Free(FNetSerializationContext& Context);

	/**
	 * Clone - Clone dynamic storage from Source
	 * No assumptions of the validity of the target is made, as this is typically called AFTER a memcopy is made to the target state
	 * which invalidates all dynamic data
	 * Should typically only be called from within a the implemntation of NetSerializer::CloneDynamicState()
	 *
	 * 中文：从 Source 克隆字节缓冲区。注意调用前 *this 内字段可能仍指向 Source.Data
	 *      （来自上游 Memcpy）——这里的实现采用"无视既有字段、按 Source 的需求
	 *      重新分配"的策略；调用方必须保证不会泄漏。
	 */
	IRISCORE_API void Clone(FNetSerializationContext& Context, const FNetSerializerAlignedStorage& Source);

	/** 取存储首地址（const）；空状态返回 nullptr。 */
	inline const uint8* GetData() const;
	/** 取存储首地址（mutable）。 */
	inline uint8* GetData();
	/** 当前已使用的字节数。 */
	inline SizeType Num() const;
	/** 已分配内存的对齐字节数；空状态时为 0（无意义）。 */
	inline SizeType GetAlignment() const;

private:
	/** 实际字节缓冲指针；nullptr 表示空。 */
	uint8* Data = nullptr;
	/** 当前逻辑使用的字节数。 */
	SizeType StorageNum = 0;
	/** 已分配的最大字节容量；当 StorageNum 缩小时不一定立即缩容。 */
	SizeType StorageMaxCapacity = 0;
	// An alignment of 0 isn't valid but the value is only used if there's actual data involved.
	// 中文：对齐为 0 在 IsAligned 上下文中不合法，但只有在 Data!=nullptr 时才会被使用，
	//      所以这里允许"零状态"的对齐为 0（哨兵值）。
	SizeType StorageAlignment = 0;
};

// FNetSerializerArrayStorage Implementation
// =============================================================================
// 中文：以下为 FNetSerializerArrayStorage 的内联模板实现。
// =============================================================================
template <typename QuantizedElementType, typename AllocationPolicy>
FNetSerializerArrayStorage<QuantizedElementType, AllocationPolicy>::FNetSerializerArrayStorage()
: ArrayNum(0)
, ArrayMaxCapacity(AllocatorInstance.GetInitialCapacity())   // 默认策略=0，内联策略=N
{
}

template <typename QuantizedElementType, typename AllocationPolicy>
void FNetSerializerArrayStorage<QuantizedElementType, AllocationPolicy>::AdjustSize(FNetSerializationContext& Context, SizeType InNum)
{
	// 1) 委托给分配策略计算"应当分配多少元素"——内联策略下 ≤N 一律用 N，
	//    默认策略下等于 InNum。
	const SizeType NewCapacity = AllocatorInstance.CalculateNewCapacity(InNum);

	// 2) 触发实际分配/缩容（内联策略可能在 N 边界搬运数据）。
	//    PreviousNumElements 这里传的是 ArrayNum（旧逻辑数量），用于内联策略
	//    决定要 RelocateConstructItems 多少个元素。
	AllocatorInstance.ResizeAllocation(Context, ArrayNum, NewCapacity);
	ArrayMaxCapacity = NewCapacity;

	// 3) 若实际容量大于旧元素数，把"新出现"的尾部 Memzero。
	//    原因：Realloc 返回的页面可能含脏字节；如果元素自身的某些字段在
	//    Quantize 时只覆盖部分位（如位字段、Padding），残留脏数据会让
	//    NetIsEqualDefault（按整块 memcmp）误判 NotEqual，导致不必要的差量发送。
	if (NewCapacity > ArrayNum)
	{
		// To avoid issues with bad data in padding we always zero initialize new memory.
		FMemory::Memzero(GetData() + ArrayNum, (NewCapacity - ArrayNum) * sizeof(ElementType));
	}

	// 4) 注意：逻辑数量上限不能超过实际容量（防止内联策略下 InNum>NewCapacity 越界）。
	ArrayNum = FMath::Min(InNum, NewCapacity);
}

template <typename QuantizedElementType, typename AllocationPolicy>
void FNetSerializerArrayStorage<QuantizedElementType, AllocationPolicy>::Free(FNetSerializationContext& Context)
{
	// 通过 ResizeAllocation(... , 0) 触发释放（默认策略 → Free；内联策略 → 仅释放 Secondary）。
	AllocatorInstance.ResizeAllocation(Context, ArrayNum, 0);
	// 把容量恢复到"初始容量"——默认策略=0，内联策略=N（内联区无需释放）。
	ArrayMaxCapacity = AllocatorInstance.GetInitialCapacity();
	ArrayNum = 0;
}

template <typename QuantizedElementType, typename AllocationPolicy>
void FNetSerializerArrayStorage<QuantizedElementType, AllocationPolicy>::Clone(FNetSerializationContext& Context, const ArrayType& Source)
{
	// 关键：调用方很可能刚刚把 Source 整块 memcpy 到 *this，因此 AllocatorInstance.Data
	// 此时是"借用 Source 内存的悬挂指针"——必须先 Initialize 把它清零，否则下面的
	// ResizeAllocation 会把 Source 的内存当成自己的旧分配释放。
	AllocatorInstance.Initialize();

	const SizeType SourceNum = Source.Num();
	const SizeType NewCapacity = AllocatorInstance.CalculateNewCapacity(SourceNum);

	// 用 PreviousNumElements=0 调用——保证 ResizeAllocation 走"全新分配"路径。
	AllocatorInstance.ResizeAllocation(Context, 0, NewCapacity);
	// CopyAssignItems 对 POD 等价于 memcpy。
	CopyAssignItems(GetData(), Source.GetData(), SourceNum);

	if (NewCapacity > SourceNum)
	{
		// To avoid issues with bad data in padding we always zero initialize new memory.
		// 中文：与 AdjustSize 同理，多余容量做 Memzero 保证后续 IsEqual 比较的稳定性。
		FMemory::Memzero(GetData() + SourceNum, (NewCapacity - SourceNum) * sizeof(ElementType));
	}

	ArrayMaxCapacity = NewCapacity;
	ArrayNum = SourceNum;
}

// FNetSerializerAlignedStorage implementation
// =============================================================================
// 中文：FNetSerializerAlignedStorage 的内联 getter 实现（其余方法在 .cpp）。
// =============================================================================

inline const uint8* FNetSerializerAlignedStorage::GetData() const
{
	return Data;
}

inline uint8* FNetSerializerAlignedStorage::GetData()
{
	return Data;
}

inline FNetSerializerAlignedStorage::SizeType FNetSerializerAlignedStorage::Num() const
{
	return StorageNum;
}

inline FNetSerializerAlignedStorage::SizeType FNetSerializerAlignedStorage::GetAlignment() const
{
	return StorageAlignment;
}

namespace AllocationPolicies
{

// FElementAllocationPolicy implementation
// =============================================================================
// 中文：FElementAllocationPolicy 的内联模板实现。
// =============================================================================

template<typename ElementType>
void FElementAllocationPolicy::ForElementType<ElementType>::ResizeAllocation(FNetSerializationContext& Context, SizeType PreviousNumElements, SizeType NumElements)
{
	if (NumElements == 0)
	{
		// 缩容到 0 → 释放并清空指针；空状态保持 dtor 处的不变式。
		if (Data)
		{
			Free(Context, Data);
			Data = nullptr;
		}
	}
	else
	{
		// 计算字节大小与对齐，然后通过 Iris 上下文提供的 Realloc 完成真正分配。
		// Realloc 内部会处理"原指针为 nullptr → 等价 Alloc"的情况。
		constexpr SIZE_T ElementSize = sizeof(ElementType);
		constexpr SIZE_T ElementAlignment = alignof(ElementType);

		Data = (ElementType*)Realloc(Context, Data, NumElements*ElementSize, ElementAlignment);
	}
}

// TInlinedElementAllocationPolicy implementation
// =============================================================================
// 中文：TInlinedElementAllocationPolicy 的内联模板实现。
//      重点：N 边界两侧的"内联 ↔ 间接"切换需要 RelocateConstructItems 搬运。
// =============================================================================

template<uint32 NumInlineElements, typename SecondaryAllocator>
template<typename ElementType>
void TInlinedElementAllocationPolicy<NumInlineElements, SecondaryAllocator>::ForElementType<ElementType>::ResizeAllocation(FNetSerializationContext& Context, SizeType PreviousNumElements, SizeType NumElements)
{
	// Check if the new allocation will fit in the inline data area.
	// 中文：新容量是否能容纳进内联区？
	if (NumElements <= NumInlineElements)
	{
		// If the old allocation wasn't in the inline data area, relocate it into the inline data area.
		// 中文：如果之前数据在间接堆分配里（已溢出），需要把前 N 个元素搬回内联区，
		//      然后释放间接分配。
		if (SecondaryData.GetAllocation())
		{
			// 关键：搬运的元素数取 min(PreviousNumElements, N)——因为内联区只能装 N 个；
			// RelocateConstructItems 对 POD 等价于 memcpy。
			RelocateConstructItems<ElementType>((void*)InlineData, (ElementType*)SecondaryData.GetAllocation(), FMath::Min(PreviousNumElements, NumInlineElements));

			// Free the old indirect allocation.
			// 中文：搬完后释放间接分配（ResizeAllocation 到 0 等价 Free）。
			SecondaryData.ResizeAllocation(Context, 0, 0);
		}
		// 否则旧数据本就在内联区——什么也不用做，新容量直接是 N（CalculateNewCapacity 保证）。
	}
	else
	{
		// 新容量 > N，必须落在间接分配。
		if (!SecondaryData.GetAllocation())
		{
			// Allocate new indirect memory for the data.
			// 中文：第一次溢出——先分配间接内存。
			SecondaryData.ResizeAllocation(Context, 0, NumElements);

			// Move the data out of the inline data area into the new allocation.
			// 中文：再把内联区的旧元素全部搬到新间接内存（PreviousNumElements 个）。
			RelocateConstructItems<ElementType>((void*)SecondaryData.GetAllocation(), GetInlineElements(), PreviousNumElements);
		}
		else
		{
			// Reallocate the indirect data for the new size.
			// 中文：已经在间接分配——直接 Realloc 调整大小。
			SecondaryData.ResizeAllocation(Context, PreviousNumElements, NumElements);
		}
	}
}

template<uint32 NumInlineElements, typename SecondaryAllocator>
template<typename ElementType>
typename TInlinedElementAllocationPolicy<NumInlineElements, SecondaryAllocator>::SizeType TInlinedElementAllocationPolicy<NumInlineElements, SecondaryAllocator>::ForElementType<ElementType>::CalculateNewCapacity(SizeType NumElements) const
{
	// If the elements use less space than the inline allocation, only use the inline allocation as slack.
	// 中文：≤N 时直接返回 N，把内联槽位天然作为 slack 复用，避免无谓的小堆分配；
	//      否则委托给二级分配策略计算（默认就是请求量）。
	return NumElements <= NumInlineElements ? NumInlineElements : SecondaryData.CalculateNewCapacity(NumElements);
}

}

}
