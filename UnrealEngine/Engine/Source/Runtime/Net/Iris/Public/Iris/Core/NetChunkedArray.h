// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ChunkedArray.h"

namespace UE::Net
{

// ============================================================================
// TNetChunkedArray —— Iris per-object 容器首选
// ----------------------------------------------------------------------------
// 与 UE 原生 TChunkedArray 的差异（关键设计取舍）：
//   1) **首批 chunk 连续分配**：构造时根据 InNumElements 计算所需 chunk 数，
//      用单次 `new FChunk[NumChunks]` 申请一块连续内存，再把每个 chunk 指针
//      填入基类 Chunks 数组。这样"热点部分"（大多数 per-object 数组访问都在
//      这段范围内）享有更好的 cache locality。
//      副作用：析构时需用 `delete[]` 整体释放，因此新增 NumPreAllocatedChunks
//      记录首批数量，并在 InvalidatePreAllocatedChunks 中把这些 chunk 指针
//      置空后由 `delete[]` 统一回收，剩余"后来追加的 chunk"则由基类按
//      `delete FChunk*` 单独释放——两种释放策略必须精确配对，否则崩溃。
//
//   2) **禁用 Empty/Reset**：Iris 的许多子系统（NetRefHandleManager、
//      ReplicationStateStorage 等）把"数组下标"作为长期有效的句柄/ID 使用。
//      一旦允许清空/重用索引，所有持有这些下标的上层结构都会错乱。因此这里
//      直接把两个方法覆盖成 `checkf(false, ...)`，强制在编译期暴露误用。
//      与 UE 的习惯（TArray::Empty/Reset 是常用 API）形成显式反差。
//
//   3) **AddToIndexUninitialized / AddToIndexZeroed**：允许"按目标索引"直接
//      扩容，不关心中间空洞——典型使用方式是"我拿到了 internal index X，
//      我要确保 Array[X] 可访问"。对应于 Iris 里以 index 为主、以连续为辅
//      的访问模式。
//
//   4) 保留拷贝/移动语义并覆写：拷贝/移动时必须同时处理"已预分配块" vs
//      "追加块"两类 chunk，以便正确释放、正确保留 NumPreAllocatedChunks。
//
// 典型使用方：
//   - FNetRefHandleManager::ReplicatedObjectData（按 InternalNetRefIndex 索引）
//   - FReplicationStateStorage 的 per-object send/recv/baseline 存储
//   - 其他凡是以"对象索引"长期引用数据的容器
// ============================================================================

/**
 * Configure what happens when new chunks are allocated.
 *
 * 构造/扩容 chunk 时的初始化策略。
 */
enum class EInitMemory : uint8
{
	Zero,		// Memory will be filled with zeros.         —— 直接 memset 为 0（适合 POD、bit field 默认 0）。
	Constructor	// Memory will be initialized by the elements constructor. —— 走元素构造函数（含非 POD 成员时使用）。
};

/**
 * A variation of TChunkArray that is optimized for use in the Iris networking system. The initial chunks
 * created during construction are placed in a contiguous block of memory to promote locality of reference.
 *
 * Iris 网络子系统专用的 TChunkedArray 变体。与基类相比：
 *   - 首批 chunk 放在"单块连续内存"中，提升缓存命中率；
 *   - 禁用 Empty/Reset，保证索引作为句柄时的稳定性；
 *   - 额外提供 AddToIndex* 按索引寻址式扩容。
 *
 * @tparam InElementType    元素类型。
 * @tparam ElementsPerChunk 每个 chunk 所含元素数，默认 100。
 * @tparam AllocatorType    分配器类型。
 */
template<typename InElementType, uint32 ElementsPerChunk = 100, typename AllocatorType = FDefaultAllocator>
class TNetChunkedArray : public TChunkedArray<InElementType, sizeof(InElementType)* ElementsPerChunk, AllocatorType>
{
public:
	
	using Super = TChunkedArray<InElementType, sizeof(InElementType) * ElementsPerChunk, AllocatorType>;

	/**
	 * 构造函数。
	 * @param InNumElements 预期初始元素数（会按 NumElementsPerChunk 向上取整 chunk 数）。
	 * @param InitMemory    新 chunk 的初始化策略：Zero 直接清零；Constructor 走构造函数。
	 *
	 * 注意：这里**直接 `new FChunk[NumChunks]`** 一次性申请多个 chunk 的连续内存，
	 * 而不是逐个 new；后续在析构/拷贝时必须以 `delete[]` 整体回收——
	 * 见 InvalidatePreAllocatedChunks 的实现。
	 */
	TNetChunkedArray(int32 InNumElements = 0, EInitMemory InitMemory = EInitMemory::Constructor)
	{
		this->NumElements = InNumElements;

		// Compute the number of chunks needed.
		// 向上取整计算需要多少 chunk（例如 NumElements=101, ElementsPerChunk=100 → NumChunks=2）。
		const int32 NumChunks = (this->NumElements + this->NumElementsPerChunk - 1) / this->NumElementsPerChunk;

		// Initial blocks should come from a single block of memory.
		// 用单次 new[] 分配所有初始 chunk，保证它们在内存中连续。
		this->Chunks.Empty(NumChunks);
		typename Super::FChunk* StartChunks = new typename Super::FChunk[NumChunks];
		for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ChunkIndex++)
		{
			typename Super::FChunk* CurrentChunk = (StartChunks + ChunkIndex);
			if (InitMemory == EInitMemory::Zero)
			{
				// 策略为 Zero：整块 memset，跳过 FChunk 默认构造带来的逐元素初始化。
				FMemory::Memset<typename Super::FChunk>(*CurrentChunk, 0);
			}

			this->Chunks.Add(CurrentChunk);
		}

		// 记录"前 N 个 chunk 来源于单块 new[] 分配"，析构时需要特殊回收。
		NumPreAllocatedChunks = NumChunks;
	}

	/** 拷贝构造：复用赋值路径的 deep-copy 实现。 */
	TNetChunkedArray(const TNetChunkedArray& OtherChunkedArray)
	{
		CopyIncludingPreAllocatedChunks(OtherChunkedArray);
	}

	/** 拷贝赋值：释放自身资源后按对方布局重新分配并拷贝。 */
	TNetChunkedArray& operator=(const TNetChunkedArray& OtherChunkedArray)
	{
		CopyIncludingPreAllocatedChunks(OtherChunkedArray);
		return *this;
	}

	/** 移动构造：窃取对方的 Chunks 数组与 NumPreAllocatedChunks。 */
	TNetChunkedArray(TNetChunkedArray&& OtherChunkedArray)
	{
		MoveIncludingPreAllocatedChunks(OtherChunkedArray);
	}

	/** 移动赋值：自赋值保护 + 窃取。 */
	TNetChunkedArray& operator=(TNetChunkedArray&& OtherChunkedArray)
	{
		if (this != &OtherChunkedArray)
		{
			MoveIncludingPreAllocatedChunks(OtherChunkedArray);
		}
		return *this;
	}

	/**
	 * 析构：必须先把"首批连续 chunk"的指针单独收回（delete[] 一次），再让基类
	 * 对剩余的 chunk 逐个 delete；顺序错误会导致"对普通 new 指针执行 delete[]"
	 * 或"对数组指针执行 delete"的 UB。
	 */
	~TNetChunkedArray()
	{
		InvalidatePreAllocatedChunks();
	}

	/** 当前 chunk 数量。 */
	int32 NumChunks() const
	{
		return this->Chunks.Num();
	}

	/** 
	 * Return the maximum number of elements the array can hold before having to add another chunk.
	 *
	 * 当前容量 = chunk 数 × 每 chunk 元素数。超出需要再追加 chunk（非连续分配）。
	 */
	int32 Capacity() const
	{
		return this->Chunks.Num() * this->NumElementsPerChunk;
	}

	/**
	 * **禁用**：Iris 上层依赖索引稳定性，不允许清空。
	 * 调用即触发 checkf(false) 失败；保留签名仅为与 TChunkedArray 接口兼容时
	 * 能被静态发现（泛型模板误用时会在此编译期失败之外，运行期也会命中 check）。
	 */
	void Empty(int32 Slack = 0)
	{
		checkf(false, TEXT("This function is not supported"));
	}

	/** **禁用**：理由同 Empty，保证 per-object 索引作为句柄的稳定性。 */
	void Reset(int32 NewSize = 0)
	{
		checkf(false, TEXT("This function is not supported"));
	}

	/** 
	 * Add elements to the array so that an index can be successfully addressed, leaving 
	 * any new element's memory unitialized or initialized by the element's constructor.
	 * 
	 * @param Index The index that must be addressable.
	 *
	 * 按"目标索引"扩容：确保 Array[Index] 可寻址。新插入的元素保持默认构造
	 * （等价于基类 Add(N) 的行为），不清零。典型用法：某个 InternalNetRefIndex
	 * 在分配器处被分配后，把对应索引"占位"出来。
	 */
	void AddToIndexUninitialized(int32 Index)
	{
		if (Index >= this->NumElements)
		{
			int32 NewElementCount = (Index - this->NumElements) + 1;
			this->Add(NewElementCount);
		}
	}

	/**
	 * Add elements to the array so that an index can be successfully addressed, zeroing
	 * out the memory for each new element.
	 *
	 * @param Index The index that must be addressable.
	 *
	 * 同上，但新 chunk 额外做 memset(0)。适合"必须从零/默认位图状态开始"的
	 * 数据结构（如 ChangeMaskStorage、FNetBitArray 拆分式存储）。
	 */
	void AddToIndexZeroed(int32 Index)
	{
		const int32 OldChunkCount = this->Chunks.Num();
		AddToIndexUninitialized(Index);
		// 只把"本次新增"的 chunk 清零，已有 chunk 保持原样，避免误抹数据。
		for (int32 ChunkIndex = OldChunkCount; ChunkIndex < this->Chunks.Num(); ChunkIndex++)
		{
			FMemory::Memset<typename Super::FChunk>(*this->Chunks.GetData()[ChunkIndex], 0);
		}
	}

protected:
	/* The number of preallocated chunks. */
	/**
	 * 首批（连续内存）chunk 的数量。析构/拷贝路径上需要据此区分"是 new[] 的一部分"
	 * 还是"后来 new 单个追加的"——前者统一 delete[]，后者各自 delete。
	 */
	int32 NumPreAllocatedChunks = 0;

private:
	/**
	 * Invalidate the pre-allocated chunk pointers and free the associated block of memory.
	 *
	 * This function must be called before TChunkedArray frees memory (e.g. the constructor) because
	 * it assumes that each chunk is a seperate allocation and is unaware of the single block
	 * of memory used by the pre-allocated chunks.
	 *
	 * 释放"首批连续 chunk"：
	 *   1) 遍历前 NumPreAllocatedChunks 个 chunk 指针，把它们从 Chunks 数组
	 *      清成 nullptr（防止基类析构时再对它们执行 `delete`）；
	 *   2) 以第一个指针为首地址，`delete[]` 整块回收；
	 *   3) 剩余"后来追加"的 chunk 留给基类析构处理。
	 *
	 * 重要：该函数必须在基类销毁之前调用——这是为什么析构/拷贝赋值开头都先
	 *      调它一次的原因。
	 */
	void InvalidatePreAllocatedChunks()
	{
		// Invalid pre-allocated chunks from the chunks array.
		typename Super::FChunk* FirstChunk = nullptr;
		for (int32 ChunkIndex = 0; ChunkIndex < NumPreAllocatedChunks; ChunkIndex++)
		{
			FirstChunk = (FirstChunk == nullptr) ? this->Chunks.GetData()[ChunkIndex] : FirstChunk;
			this->Chunks.GetData()[ChunkIndex] = nullptr;
		}

		// The first chunk points to the beginning of the block of memory used by the pre-allocated chunks.
		// FirstChunk 指向 new FChunk[NumPreAllocatedChunks] 的首地址，必须用 delete[] 回收。
		delete[] FirstChunk;

		NumPreAllocatedChunks = 0;
	}

	/**
	 * Copy the contents of another TNetChunkedArray into this instance, ensuring that any existing chunks
	 * (pre-allocated and otherwise) are deallocated.
	 *
	 * Deep-copy：
	 *   1) 先释放自身的预分配块；
	 *   2) 拷贝对方的 NumElements 与 NumPreAllocatedChunks；
	 *   3) 申请自己的"单块 new[]"保存前 NumPreAllocatedChunks 个 chunk，
	 *      逐个按值拷贝；
	 *   4) 对于对方超过预分配数量的 chunk，逐个 new 单独的 FChunk（与基类
	 *      原生行为一致）。
	 */
	void CopyIncludingPreAllocatedChunks(const TNetChunkedArray& ChunkedArray)
	{
		// Free the memory for any existing pre-allocated chunks.
		InvalidatePreAllocatedChunks();

		this->NumElements = ChunkedArray.NumElements;
		this->NumPreAllocatedChunks = ChunkedArray.NumPreAllocatedChunks;

		// Compute the number of chunks to copy and prepare the chunked array.
		const int32 NumChunks = ChunkedArray.Chunks.Num();
		
		this->Chunks.Empty(NumChunks);

		// Copy the pre-allocated chunks.
		// 首批：单块 new[] + 逐 chunk 值拷贝，保持"连续内存"的不变式。
		typename Super::FChunk* PreAllocatedChunks = new typename Super::FChunk[this->NumPreAllocatedChunks];
		for (int32 ChunkIndex = 0; ChunkIndex < this->NumPreAllocatedChunks; ChunkIndex++)
		{
			typename Super::FChunk* CurrentChunk = (PreAllocatedChunks + ChunkIndex);
			
			*CurrentChunk = *ChunkedArray.Chunks.GetData()[ChunkIndex];

			this->Chunks.Add(CurrentChunk);
		}

		// Copy any remaining chunks.
		// 追加 chunk：与基类一致，逐个 new 单独的 FChunk（析构时由基类 delete）。
		for (int32 ChunkIndex = this->NumPreAllocatedChunks; ChunkIndex < NumChunks; ChunkIndex++)
		{
			const typename Super::FChunk* CurrentChunk = ChunkedArray.Chunks.GetData()[ChunkIndex];

			this->Chunks.Add(new typename Super::FChunk(*CurrentChunk));
		}
	}

	/**
	 * 移动实现：直接窃取基类的 Chunks 数组（含所有 chunk 指针）和 NumElements、
	 * NumPreAllocatedChunks；对方重置为空状态。
	 * 由于 chunk 指针的"预分配块归属"信息随 NumPreAllocatedChunks 一起转移，
	 * 目标对象析构时能正确回收；对方析构时不会误释放已转移出去的内存。
	 */
	void MoveIncludingPreAllocatedChunks(TNetChunkedArray& ChunkedArray)
	{
		this->Chunks = (typename Super::ChunksType&&)ChunkedArray.Chunks;
		this->NumElements = ChunkedArray.NumElements;
		this->NumPreAllocatedChunks = ChunkedArray.NumPreAllocatedChunks;
		
		ChunkedArray.NumElements = 0;
		ChunkedArray.NumPreAllocatedChunks = 0;
	}
};

}

