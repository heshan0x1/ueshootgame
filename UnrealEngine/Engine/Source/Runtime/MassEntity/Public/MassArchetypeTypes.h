// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "UObject/Class.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "Containers/ArrayView.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "Containers/StridedView.h"
#include "Containers/UnrealString.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityTypes.h"

#define UE_API MASSENTITY_API

struct FMassEntityManager;
struct FMassArchetypeData;
struct FMassExecutionContext;
struct FMassFragment;
struct FMassArchetypeChunkIterator;
struct FMassEntityHandle;
struct FMassEntityQuery;
struct FMassArchetypeEntityCollection;
struct FMassEntityView;
struct FMassDebugger;
struct FMassArchetypeHelper;
struct FMassArchetypeVersionedHandle;

// ============================================================================
// 【Processor/Query 回调函数 typedef】
// 这三个 TFunction 别名是 Mass Processor 在遍历 chunk 时使用的三种回调签名：
//
//   FMassEntityExecuteFunction —— 针对"单个实体"的回调（接收 EntityIndex），
//       调用方内部通常仍是循环一个 chunk，但把每个实体拆出来调用。开销较高，
//       主要在需要逐实体逻辑而非 SoA 批处理时使用。
//
//   FMassExecuteFunction        —— Processor 的主流回调：接收 FMassExecutionContext，
//       context 里已经绑定好当前 chunk 的所有 fragment TArrayView<T>。processor
//       一般写 `for (int i = 0; i < Context.GetNumEntities(); ++i) { ... }`。
//       这是 Mass 性能最好的遍历方式：外层循环 chunk，内层 tight loop 走 SoA。
//
//   FMassChunkConditionFunction —— 在进入 chunk 之前测试的谓词；常用于 chunk
//       fragment（如 LOD、区域等）的早退。返回 false 则整个 chunk 被跳过。
// ============================================================================
using FMassEntityExecuteFunction = TFunction< void(FMassExecutionContext& /*ExecutionContext*/, int32 /*EntityIndex*/) >;
using FMassExecuteFunction = TFunction< void(FMassExecutionContext& /*ExecutionContext*/) >;
using FMassChunkConditionFunction = TFunction< bool(const FMassExecutionContext& /*ExecutionContext*/) >;

//-----------------------------------------------------------------------------
// FMassArchetypeHandle
//-----------------------------------------------------------------------------
/** An opaque handle to an archetype */
/**
 * 【不透明 archetype 句柄】
 *
 * Mass 框架向外暴露 archetype 时使用的包装类型。内部只有一个 TSharedPtr<FMassArchetypeData>，
 * 但 FMassArchetypeData 定义放在 Internal/ 目录下，外部代码既看不到布局也无法直接访问
 * 存储细节，只能通过 FMassEntityManager 提供的 API 间接操作。
 *
 * 设计意图：
 *  - Archetype 的生命周期由 EntityManager 管理，所有持有者共享同一份 FMassArchetypeData；
 *  - 对用户而言它是值类型，可自由拷贝/比较/哈希；
 *  - 相等语义是"指向同一 archetype 数据对象"（按指针相等而不是按 composition 相等）。
 *
 * 注意：IsValid() 只检查底层指针非空，并不判断 archetype 是否已初始化完毕。
 */
struct FMassArchetypeHandle final
{
	FMassArchetypeHandle() = default;
	bool IsValid() const;

	bool operator==(const FMassArchetypeHandle& Other) const;
	bool operator!=(const FMassArchetypeHandle& Other) const;

	MASSENTITY_API friend uint32 GetTypeHash(const FMassArchetypeHandle& Instance);

	void Reset();

private:
	FMassArchetypeHandle(const TSharedPtr<FMassArchetypeData>& InDataPtr);

	// 底层 archetype 数据，使用 TSharedPtr 实现引用计数；外部代码看不到 FMassArchetypeData 定义
	TSharedPtr<FMassArchetypeData> DataPtr;

	// Mass 内部类通过 friend 访问 DataPtr，对外保持黑盒
	friend FMassArchetypeHelper;
	friend FMassEntityManager;
	friend FMassArchetypeVersionedHandle;
};

/**
 * 【带版本号的 archetype 句柄】
 *
 * 在 FMassArchetypeHandle 的基础上附加 HandleVersion（即 FMassArchetypeData::EntityOrderVersion
 * 的一个快照）。用途：
 *  - 判断从句柄创建到现在，archetype 内实体的顺序是否发生了改变
 *    （典型触发：RemoveEntity 的 swap-and-pop、BatchMove、CompactEntities）；
 *  - 如果变化了，之前构建的 FMassArchetypeEntityCollection（内部记录的是 chunk 内绝对索引）
 *    就"过时"了，继续用会读到错误的实体。
 *
 * 所以任何"先建集合、再异步跑 processor"的流程，都应在执行前调用 IsUpToDate()。
 * 注意 HandleVersion 不参与 GetTypeHash —— 它是运行期辅助信息，而不是身份的一部分。
 */
struct FMassArchetypeVersionedHandle final
{
	FMassArchetypeVersionedHandle() = default;
	FMassArchetypeVersionedHandle(const FMassArchetypeHandle& InHandle);
	FMassArchetypeVersionedHandle(FMassArchetypeHandle&& InHandle);

	bool IsValid() const;
	bool operator==(const FMassArchetypeVersionedHandle& Other) const;
	bool operator!=(const FMassArchetypeVersionedHandle& Other) const;
	/** 返回本句柄记录的版本是否仍与底层 archetype 当前版本一致；不一致说明实体顺序已变动 */
	MASSENTITY_API bool IsUpToDate() const;

	operator FMassArchetypeHandle() const;

	MASSENTITY_API friend uint32 GetTypeHash(const FMassArchetypeHandle& Instance);
private:
	FMassArchetypeHandle ArchetypeHandle;
	/**
	 * This value indicates whether the target archetype had its entities moved around since the handle creations.
	 * The information is useful in a couple of scenarios (like making sure an entity collection is up to date),
	 * but in most cases the users should not concern themselves with this value.
	 * Note that the value is not used as part of hash calculation, it's effectively transient. 
	 */
	/**
	 * 构造时从 archetype 拷贝来的 EntityOrderVersion 快照；若之后 archetype 因 swap-and-pop、
	 * 压缩等原因更改了实体排列，archetype 内的 EntityOrderVersion 会递增，此处仍保留旧值，
	 * 即可通过 IsUpToDate() 比对检测"旧集合"。
	 * 注意该字段不纳入 hash —— 两个 "同一 archetype 但不同版本" 的句柄在哈希上等价。
	 */
	uint32 HandleVersion = 0;
};

//-----------------------------------------------------------------------------
// FMassArchetypeEntityCollection
//-----------------------------------------------------------------------------
/** A struct that converts an arbitrary array of entities of given Archetype into a sequence of continuous
 *  entity chunks. The goal is to have the user create an instance of this struct once and run through a bunch of
 *  systems. The runtime code usually uses FMassArchetypeChunkIterator to iterate on the chunk collection.
 */
/**
 * 【Archetype 内的"实体批处理集合"】
 *
 * 把一堆属于同一 archetype 的实体，压缩成一串"连续 range"的表示，每个 range 形如
 *   (ChunkIndex, SubchunkStart, Length)
 * 覆盖某个 chunk 内从 SubchunkStart 开始的 Length 个连续槽位。
 *
 * 为什么这么设计？
 *  - Mass 的 processor 在 tight loop 中批量扫 chunk，如果实体散布在各 chunk 的各处，
 *    按连续 range 访问可以让 SoA 布局下的内层循环保持 cache-friendly；
 *  - 构造成本仅发生一次：把零散的 FMassEntityHandle 数组"映射到"chunk 内坐标、
 *    排序、折叠相邻的连续槽位；构造完后可交给多个 processor 复用。
 *
 * 典型用法：
 *   1. 用户收集一批实体（如同屏玩家、AOI 附近 NPC）；
 *   2. 构造 FMassArchetypeEntityCollection —— 会根据实体在 chunk 内的排布自动折叠成 ranges；
 *   3. 将其传给 EntityManager / Query / Processor；
 *   4. 内部通过 FMassArchetypeChunkIterator 逐 range 调用用户回调。
 *
 * 关键约束：本集合只对"同一个 archetype"有效。实体一旦跨 archetype（被加/减过 tag、
 * fragment），或 archetype 内发生了 swap-and-pop、压缩（EntityOrderVersion 变化），
 * 此集合就失效 —— 见 IsUpToDate()。
 */
struct FMassArchetypeEntityCollection 
{
public:
	/**
	 * 一段"连续范围"：指向 ChunkIndex 号 chunk 内、从 SubchunkStart 起的 Length 个槽。
	 * 特殊约定：Length <= 0 表示"吃掉从 SubchunkStart 到 chunk 末尾的所有实体"，
	 * 这个语义允许在实体数量随时间变化（chunk 尾部追加）时仍然保持 range 有效。
	 */
	struct FArchetypeEntityRange
	{
		// 所属 chunk 在 archetype 的 Chunks 数组内的下标；INDEX_NONE 表示未设置
		int32 ChunkIndex = INDEX_NONE;
		 /** 
		  * The index of the first entity within the specified chunk that starts this subchunk.
		  */
		// chunk 内第一个被此 range 覆盖的实体槽位下标 [0, NumEntitiesPerChunk)
		int32 SubchunkStart = 0;
		/** 
		 * The number of entities in this subchunk.
		 * If Length is 0 or negative, it indicates that the range covers all remaining entities 
		 * in the chunk starting from SubchunkStart.
		 */
		// 本 range 覆盖的实体个数；<=0 表示"覆盖从 SubchunkStart 到 chunk 当前末尾的全部实体"
		int32 Length = 0;

		FArchetypeEntityRange() = default;
		explicit FArchetypeEntityRange(const int32 InChunkIndex, const int32 InSubchunkStart = 0, const int32 InLength = 0) : ChunkIndex(InChunkIndex), SubchunkStart(InSubchunkStart), Length(InLength) {}
		/** Note that we consider invalid-length chunks valid as long as ChunkIndex and SubchunkStart are valid */
		// 只要 ChunkIndex 和 SubchunkStart 合法就视为 IsSet；Length<=0 是合法特殊值
		bool IsSet() const { return ChunkIndex != INDEX_NONE && SubchunkStart >= 0; }

		/** Checks if given InRange comes right after this instance */
		// 判断 Other 是否紧贴在 this 之后（可用于合并相邻 range）
		bool IsAdjacentAfter(const FArchetypeEntityRange& Other) const
		{
			return ChunkIndex == Other.ChunkIndex && SubchunkStart + Length == Other.SubchunkStart;
		}

		// 判断两个 range 是否发生槽位重叠；注意 Length==0 表示"到 chunk 末尾"这一特殊语义
		bool IsOverlapping(const FArchetypeEntityRange& Other) const
		{
			return ChunkIndex == Other.ChunkIndex
				&& (*this < Other
					// note that Length == 0 means "all the entities starting from SubchunkStart
					? (SubchunkStart + Length > Other.SubchunkStart || Length == 0)
					: (Other.SubchunkStart + Other.Length > SubchunkStart || Other.Length == 0)
				);
		}

		bool operator==(const FArchetypeEntityRange& Other) const
		{
			return ChunkIndex == Other.ChunkIndex && SubchunkStart == Other.SubchunkStart && Length == Other.Length;
		}
		bool operator!=(const FArchetypeEntityRange& Other) const { return !(*this == Other); }

		// 排序顺序：先按 ChunkIndex，再按 SubchunkStart，最后按 Length
		// 用于让 range 数组变得有序 / 相邻可合并
		bool operator<(const FArchetypeEntityRange& Other) const
		{
			return (ChunkIndex != Other.ChunkIndex)
				? ChunkIndex < Other.ChunkIndex
				: (SubchunkStart != Other.SubchunkStart
					? SubchunkStart < Other.SubchunkStart
					: Length < Other.Length);
		}
	};

	/** 输入实体数组是否可能含重复的处理策略 */
	enum EDuplicatesHandling
	{
		NoDuplicates,	// indicates that the caller guarantees there are no duplicates in the input Entities collection
						// note that in no-shipping builds a `check` will fail if duplicates are present.
						// 调用方承诺无重复；非 shipping 构建下若仍发现重复会 check 失败
		FoldDuplicates,	// indicates that it's possible that Entities contains duplicates. The input Entities collection 
						// will be processed and duplicates will be removed.
						// 可能含重复；集合内部会主动去重
	};

	/** 构造策略：是否立即把 archetype 所有 chunk 装进来 */
	enum EInitializationType
	{
		GatherAll,	// default behavior, makes given FMassArchetypeEntityCollection instance represent all entities of the given archetype
					// 默认：让集合代表 archetype 全部实体（每个 chunk 一个 Length==0 的 range）
		DoNothing,	// meant for procedural population by external code (like child classes)
					// 不做初始化；由外部（如 WithPayload 版本）自己填
	};

	using FEntityRangeArray = TArray<FArchetypeEntityRange>;
	using FConstEntityRangeArrayView = TConstArrayView<FArchetypeEntityRange>;

private:
	// 有序的 range 序列，代表集合内的全部实体
	FEntityRangeArray Ranges;
	/** entity indices indicated by EntityRanges are only valid with given Archetype */
	// 关联的 archetype，带版本号；Ranges 内的 chunk 下标只在此 archetype 且版本一致时有效
	FMassArchetypeVersionedHandle Archetype;

public:
	FMassArchetypeEntityCollection() = default;
	// 从任意顺序的实体数组构造：会查找每个实体在 archetype 内的绝对索引、排序、折叠相邻 range
	UE_API FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetype, TConstArrayView<FMassEntityHandle> InEntities, EDuplicatesHandling DuplicatesHandling);
	/** optimized, special case for a single-entity */
	// 单实体特化：跳过排序/折叠逻辑，直接算出一个 Length==1 的 range
	UE_API FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetype, const FMassEntityHandle EntityHandle);
	UE_API FMassArchetypeEntityCollection(FMassArchetypeHandle&& InArchetype, const FMassEntityHandle EntityHandle);
	// 全 archetype 构造：GatherAll 会为每个 chunk 生成一个 (ChunkIdx, 0, 0) range —— Length==0 意味着"整个 chunk"
	UE_API explicit FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetypeHandle, const EInitializationType Initialization = EInitializationType::GatherAll);
	UE_API explicit FMassArchetypeEntityCollection(TSharedPtr<FMassArchetypeData>& InArchetype, const EInitializationType Initialization = EInitializationType::GatherAll);
	// 直接移交现成的 Ranges 数组的构造（供 WithPayload 等流程内部使用）
	FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetypeHandle, FEntityRangeArray&& InEntityRanges)
		: Ranges(Forward<FEntityRangeArray>(InEntityRanges))
		, Archetype(InArchetypeHandle)
	{}

	FConstEntityRangeArrayView GetRanges() const;
	FMassArchetypeHandle GetArchetype() const;
	bool IsEmpty() const;
	// 集合当前记录的 archetype 版本是否仍然有效（见 FMassArchetypeVersionedHandle::IsUpToDate）
	bool IsUpToDate() const;

	UE_DEPRECATED(5.6, "This function is deprecated. Use !IsEmpty() instead.")
	bool IsSet() const;

	// 清空集合：丢掉 Ranges 并重置 archetype 引用
	void Reset() 
	{ 
		Archetype = FMassArchetypeVersionedHandle();
		Ranges.Reset();
	}

	/** The comparison function that checks if Other is identical to this. Intended for diagnostics/debugging. */
	// 深比较，供 debug/诊断用；相等要求 archetype、版本号、全部 range 都完全一致
	UE_API bool IsSame(const FMassArchetypeEntityCollection& Other) const;
	bool IsSameArchetype(const FMassArchetypeEntityCollection& Other) const;

	/**
	 * Appends ranges of the given FMassArchetypeEntityCollection instance. Note that it can be safely done only
	 * when both collections host entities of the same archetype, and both were created with the same version
	 * of said archetype.
	 * Additionally, we don't expect the operation to produce overlapping entity ranges and this assumption is
	 * only verified in debug builds (i.e. use it only when you're certain no range overlaps are possible).
	 */
	/**
	 * 把另一 collection 的 ranges 追加进来；前提是两者 archetype 和版本都相同，且 range 不重叠。
	 * 追加后若原本非空会重新排序；range 重叠只在 slow-guard 构建下验证。
	 */
	template<typename T>
	requires std::is_same_v<typename TDecay<T>::Type, FMassArchetypeEntityCollection>
	void Append(T&& Other);

	/**
	 * Converts stored entity ranges to FMassEntityHandles and appends them to InOutHandles.
	 * Note that the operation is only supported for already created entities (i.e. not "reserved")
	 * @return whether any entity handles have been actually exported
	 */
	/**
	 * 把 ranges 还原成原始 FMassEntityHandle 追加到 InOutHandles。
	 * 仅对"已创建的实体"有效（预留但未实际放入 archetype 的实体无 chunk 内坐标）。
	 * @return 是否实际导出了至少一个 handle
	 */
	UE_API bool ExportEntityHandles(TArray<FMassEntityHandle>& InOutHandles) const;

	// 诊断辅助：检查给定 ranges 视图中是否存在槽位重叠（O(n)，仅相邻对比；要求已排序）
	static UE_API bool DoesContainOverlappingRanges(FConstEntityRangeArrayView Ranges);

#if WITH_MASSENTITY_DEBUG
	// debug-only：汇总集合内实体总数（需要去 chunk 查 Length<=0 的"到末尾"语义）
	UE_API int32 DebugCountEntities() const;
#endif // WITH_MASSENTITY_DEBUG

protected:
	friend struct FMassArchetypeEntityCollectionWithPayload;
	// 把已排序的绝对索引序列（strided view 是为了和 payload 索引共用同一缓冲）折叠成 ranges
	UE_API void BuildEntityRanges(TStridedView<const int32> TrueIndices);
	// 单实体辅助：把 entity 在 archetype 内的绝对索引换算成 range
	static UE_API FArchetypeEntityRange CreateRangeForEntity(const FMassArchetypeHandle& InArchetype, const FMassEntityHandle EntityHandle);

private:
	// GatherAll 模式下收集 archetype 的每个 chunk 作为一个 range
	void GatherChunksFromArchetype();
};

/**
 * 【集合 + payload 切片】
 *
 * 在 FMassArchetypeEntityCollection 的基础上附带一份"类型擦除的列式 payload"（FMassGenericPayloadViewSlice）。
 * 用途：批量设置 fragment 值（BatchSetEntityFragmentValues）的时候，payload 本身是外部提供的 SoA 数组，
 * 而 Ranges 指明"这些 payload 元素应当写入 archetype 的哪些槽位"，两者一一对应。
 *
 * 构造助手 CreateEntityRangesWithPayload 会：
 *  1. 把输入实体按 archetype 分组（一次 sort）；
 *  2. 按 archetype 产出一组 Collection+PayloadSlice，每组对应一个 archetype 的写入任务；
 *  3. 处理可能的重复（把重复 payload 槽位换到末尾屏蔽掉）。
 */
struct FMassArchetypeEntityCollectionWithPayload
{
	explicit FMassArchetypeEntityCollectionWithPayload(const FMassArchetypeEntityCollection& InEntityCollection)
		: Entities(InEntityCollection)
	{
	}

	explicit FMassArchetypeEntityCollectionWithPayload(FMassArchetypeEntityCollection&& InEntityCollection)
		: Entities(MoveTempIfPossible(InEntityCollection))
	{
	}

	// 静态工厂：把"一堆任意 archetype 的实体 + 一份平铺 payload"拆分成每个 archetype 一个 Collection+Slice
	// 实现要点：先按 (ArchetypeIndex, TrueIndex) 排序，排序时同时 swap payload 保持对齐
	static UE_API void CreateEntityRangesWithPayload(const FMassEntityManager& EntityManager, const TConstArrayView<FMassEntityHandle> Entities
		, const FMassArchetypeEntityCollection::EDuplicatesHandling DuplicatesHandling, FMassGenericPayloadView Payload
		, TArray<FMassArchetypeEntityCollectionWithPayload>& OutEntityCollections);

	const FMassArchetypeEntityCollection& GetEntityCollection() const { return Entities; }
	const FMassGenericPayloadViewSlice& GetPayload() const { return PayloadSlice; }

private:
	FMassArchetypeEntityCollectionWithPayload(const FMassArchetypeHandle& InArchetype, TStridedView<const int32> TrueIndices, FMassGenericPayloadViewSlice&& Payload);

	FMassArchetypeEntityCollection Entities;  // 集合部分（Ranges）
	FMassGenericPayloadViewSlice PayloadSlice;// 对应此 archetype 的 payload 片段
};

//-----------------------------------------------------------------------------
// FMassArchetypeChunkIterator
//-----------------------------------------------------------------------------
/**
 *  The type used to iterate over given archetype's chunks, be it full, continuous chunks or sparse subchunks. It hides
 *  this details from the rest of the system.
 */
/**
 * 【Range 序列的轻量迭代器】
 *
 * 封装 "FMassArchetypeEntityCollection::Ranges 数组"的 C++ forward-iterator 风格遍历器。
 * Processor 内部循环通常写作：
 *   for (FMassArchetypeChunkIterator It(EntityRanges); It; ++It) {
 *       int32 Start = It->SubchunkStart;
 *       int32 Len   = It->Length;  // 可能 <= 0，表示"到 chunk 末尾"
 *       ...
 *   }
 * 让调用方不必关心 ranges 底层数组/特殊 Length 语义。
 */
struct FMassArchetypeChunkIterator
{
private:
	// 被迭代的 ranges 视图（不拥有）
	FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRanges;
	// 当前 range 索引
	int32 CurrentChunkIndex = 0;

public:
	explicit FMassArchetypeChunkIterator(const FMassArchetypeEntityCollection::FConstEntityRangeArrayView& InEntityRanges) : EntityRanges(InEntityRanges), CurrentChunkIndex(0) {}

	// bool 转换用于 `for (;It;++It)` 终止条件：当前 range 必须合法
	operator bool() const { return EntityRanges.IsValidIndex(CurrentChunkIndex) && EntityRanges[CurrentChunkIndex].IsSet(); }
	FMassArchetypeChunkIterator& operator++() { ++CurrentChunkIndex; return *this; }

	const FMassArchetypeEntityCollection::FArchetypeEntityRange* operator->() const { check(bool(*this)); return &EntityRanges[CurrentChunkIndex]; }
	const FMassArchetypeEntityCollection::FArchetypeEntityRange& operator*() const { check(bool(*this)); return EntityRanges[CurrentChunkIndex]; }
};

//-----------------------------------------------------------------------------
// FMassRawEntityInChunkData
//-----------------------------------------------------------------------------
/**
 * 【"实体在 chunk 内的原始地址"】—— 一对 (ChunkRawMemory, IndexWithinChunk) 指针对。
 *
 * 实体的数据在 Mass 中以 SoA 存储于 chunk 的 RawMemory 缓冲区里，给定一个实体想访问它的
 * fragment，需要两条信息：所在 chunk 的内存起点、在 chunk 内的槽位下标。
 * 这个结构把两者打包成值类型，方便在热路径上传递。
 *
 * 注意：它仅仅是"指针+索引"，不携带版本号；如果 chunk 发生 swap-and-pop 等变动，此结构会
 * 指向"错位的实体"（内存地址仍合法，但解读出的 fragment 不再属于原实体）。
 * 需要安全检查时使用下面的 FMassEntityInChunkDataHandle。
 */
struct FMassRawEntityInChunkData 
{
	FMassRawEntityInChunkData() = default;
	FMassRawEntityInChunkData(uint8* InChunkRawMemory, const int32 InIndexWithinChunk);

	// 判断是否已设置（两个字段都非默认）
	bool IsSet() const;
	bool operator==(const FMassRawEntityInChunkData & Other) const;

	// chunk 裸内存起点（FMassArchetypeChunk::RawMemory）
	uint8* const ChunkRawMemory = nullptr;
	// 实体在本 chunk 内的槽位下标
	const int32 IndexWithinChunk = INDEX_NONE;;
};

//-----------------------------------------------------------------------------
// FMassEntityInChunkDataHandle
//-----------------------------------------------------------------------------
/**
 * This is an extension of FMassRawEntityInChunkData that provides additional safety features.
 * It can be used to detect that the underlying data has changed. 
 */
/**
 * 【带版本校验的 Raw 扩展】
 *
 * 在 FMassRawEntityInChunkData 基础上额外记录：ChunkIndex（用于在 archetype Chunks 数组查找）
 * 与 ChunkSerialNumber（创建本句柄时 chunk 的 SerialModificationNumber 快照）。
 *
 * 在使用前可以调用 IsValid(Archetype)，archetype 会拿当前 chunk 的 SerialModificationNumber
 * 与存下来的 ChunkSerialNumber 比较 —— 不一致说明该 chunk 发生过 add/remove 引发的 swap-and-pop，
 * 句柄所指槽位可能已被覆盖成别的实体。
 *
 * 典型用途：跨帧缓存"指向某个实体的快速引用"的场景。
 */
struct FMassEntityInChunkDataHandle : FMassRawEntityInChunkData 
{
	FMassEntityInChunkDataHandle() = default;
	FMassEntityInChunkDataHandle(FMassEntityInChunkDataHandle&&) = default;
	FMassEntityInChunkDataHandle(const FMassEntityInChunkDataHandle&) = default;
	// 自定义赋值：用 placement new 绕过成员都是 const 的限制
	FMassEntityInChunkDataHandle& operator=(const FMassEntityInChunkDataHandle&);
	FMassEntityInChunkDataHandle& operator=(FMassEntityInChunkDataHandle&&);
	FMassEntityInChunkDataHandle(uint8* InChunkRawMemory, const int32 InIndexWithinChunk, const int32 InChunkIndex, const int32 InChunkSerialNumber);

	// 校验本句柄在指定 archetype 中仍然有效：chunk 索引合法且 SerialNumber 未变
	MASSENTITY_API bool IsValid(const FMassArchetypeData* ArchetypeData) const;
	MASSENTITY_API bool IsValid(const FMassArchetypeHandle& ArchetypeHandle) const;
	bool operator==(const FMassEntityInChunkDataHandle& Other) const;

	// 所在 chunk 在 Archetype::Chunks 中的下标
	const int32 ChunkIndex = INDEX_NONE;
	// 构造时 chunk 的 SerialModificationNumber 快照；chunk 每次 add/remove 会 ++
	const int32 ChunkSerialNumber = INDEX_NONE;
};

//-----------------------------------------------------------------------------
// FMassQueryRequirementIndicesMapping
//-----------------------------------------------------------------------------
/**
 * 【需求→archetype 内索引 的缓存数组】
 *
 * TInlineAllocator<16>：典型 query 的 requirement 数量在十几个以内，inline 存储避免堆分配。
 * 元素含义：针对 FMassFragmentRequirements 里的每一条 "RequiresBinding" 需求，记录该需求
 * 的类型在目标 archetype FragmentConfigs / ChunkFragments / ShareValues 中的实际下标
 * （INDEX_NONE 表示 optional 未命中）。
 *
 * 这样 processor 在 tight loop 里绑定 view 时，就不用每次再去 TMap 里查 "StructType→idx"，
 * 直接按顺序取这里缓存的 int 即可。
 */
using FMassFragmentIndicesMapping = TArray<int32, TInlineAllocator<16>>;

/**
 * 【按 query 缓存的四类 fragment 索引映射】
 *
 * 一个 FMassEntityQuery 对某个 archetype 的"需求→该 archetype 内的 fragment index"映射会
 * 缓存为此结构，按类别分四个 mapping 数组。后续执行时直接复用。
 *
 * IsEmpty() 的判定条件有点反直觉：只要 EntityFragments 或 ChunkFragments 为空就算"空"
 * —— 因为 Mass 认为 processor 至少要通过 entity fragment 或 chunk fragment 来 bind，
 * 纯 shared-only query 并不会走这条缓存路径。
 */
struct FMassQueryRequirementIndicesMapping
{
	FMassQueryRequirementIndicesMapping() = default;

	FMassFragmentIndicesMapping EntityFragments;      // Per-entity fragment 的映射
	FMassFragmentIndicesMapping ChunkFragments;       // Chunk fragment（每 chunk 一份）
	FMassFragmentIndicesMapping ConstSharedFragments; // 只读 shared fragment
	FMassFragmentIndicesMapping SharedFragments;      // 可写 shared fragment
	inline bool IsEmpty() const
	{
		return EntityFragments.Num() == 0 || ChunkFragments.Num() == 0;
	}
};


//-----------------------------------------------------------------------------
// INLINES
//-----------------------------------------------------------------------------
// 以下为轻量成员函数的 inline 定义，集中放在此处避免在声明区堆积实现。
inline bool FMassArchetypeVersionedHandle::IsValid() const
{
	return ArchetypeHandle.IsValid();
}

inline bool FMassArchetypeVersionedHandle::operator==(const FMassArchetypeVersionedHandle& Other) const 
{ 
	return ArchetypeHandle == Other.ArchetypeHandle && HandleVersion == Other.HandleVersion; 
}

inline bool FMassArchetypeVersionedHandle::operator!=(const FMassArchetypeVersionedHandle& Other) const 
{ 
	return !(*this == Other);
}

inline FMassArchetypeVersionedHandle::operator FMassArchetypeHandle() const
{
	return ArchetypeHandle;
}

inline bool FMassArchetypeHandle::IsValid() const
{
	return DataPtr.IsValid();
}

inline bool FMassArchetypeHandle::operator==(const FMassArchetypeHandle& Other) const
{
	return DataPtr == Other.DataPtr;
}

inline bool FMassArchetypeHandle::operator!=(const FMassArchetypeHandle& Other) const
{
	return DataPtr != Other.DataPtr;
}

inline void FMassArchetypeHandle::Reset()
{
	DataPtr.Reset();
}

inline FMassArchetypeHandle::FMassArchetypeHandle(const TSharedPtr<FMassArchetypeData>& InDataPtr)
	: DataPtr(InDataPtr)
{
	
}

inline FMassArchetypeEntityCollection::FConstEntityRangeArrayView FMassArchetypeEntityCollection::GetRanges() const
{
	return Ranges;
}

inline FMassArchetypeHandle FMassArchetypeEntityCollection::GetArchetype() const
{
	return Archetype;
}

inline bool FMassArchetypeEntityCollection::IsEmpty() const
{
	// 空集合判定：既没有 ranges 也没有关联 archetype；注意若仅 Ranges 为空但 archetype
	// 有效，仍可代表"0 个实体但 archetype 存在"的状态 —— 此处按严格空定义
	return Ranges.Num() == 0 && Archetype.IsValid() == false;
}

inline bool FMassArchetypeEntityCollection::IsUpToDate() const
{
	// 空集合恒为 up-to-date；否则委托给 versioned handle 比较版本号
	return IsEmpty() || Archetype.IsUpToDate();
}

inline bool FMassArchetypeEntityCollection::IsSet() const
{
	return !IsEmpty();
}

inline bool FMassArchetypeEntityCollection::IsSameArchetype(const FMassArchetypeEntityCollection& Other) const
{
	return Archetype == Other.Archetype;
}

template<typename T>
requires std::is_same_v<typename TDecay<T>::Type, FMassArchetypeEntityCollection>
void FMassArchetypeEntityCollection::Append(T&& Other)
{
	const bool bWasEmpty = Ranges.IsEmpty();
	checkf(IsSameArchetype(Other), TEXT("Unable to merge two entity collections representing different archetypes"));

	// 追加 Other 的 ranges 到本集合（支持 T&&，右值会 move 底层数组）
	Ranges.Append(Forward<T>(Other).Ranges);

	if (bWasEmpty == false)
	{
		// 为了维持"按 (ChunkIndex, SubchunkStart) 升序"的不变式，追加后重新排序
		Ranges.Sort();
		// slow-guard 构建下验证两个源集合合并后没有 range 重叠（正常业务不应出现）
		checkfSlow(DoesContainOverlappingRanges(Ranges) == false
			, TEXT("Entity collection ranges overlap as a result of %hs"), __FUNCTION__);
	}
}

inline FMassRawEntityInChunkData::FMassRawEntityInChunkData(uint8* InChunkRawMemory, const int32 InIndexWithinChunk)
	: ChunkRawMemory(InChunkRawMemory), IndexWithinChunk(InIndexWithinChunk)
{}

inline bool FMassRawEntityInChunkData::IsSet() const
{
	return ChunkRawMemory != nullptr && IndexWithinChunk != INDEX_NONE;
}

inline bool FMassRawEntityInChunkData::operator==(const FMassRawEntityInChunkData & Other) const
{
	return ChunkRawMemory == Other.ChunkRawMemory && IndexWithinChunk == Other.IndexWithinChunk;
}

inline FMassEntityInChunkDataHandle& FMassEntityInChunkDataHandle::operator=(const FMassEntityInChunkDataHandle& Other)
{
	// 由于基类/本类所有成员均为 const，常规赋值语法不可用 —— 使用 placement new 覆盖现有对象
	new (this) FMassEntityInChunkDataHandle(Other);
	return *this;
}

inline FMassEntityInChunkDataHandle& FMassEntityInChunkDataHandle::operator=(FMassEntityInChunkDataHandle&& Other)
{
	// 同理用 placement new；此处虽是 rvalue，成员都是值类型无移动开销，直接走拷贝构造即可
	new (this) FMassEntityInChunkDataHandle(Other);
	return *this;
}

inline FMassEntityInChunkDataHandle::FMassEntityInChunkDataHandle(uint8* InChunkRawMemory, const int32 InIndexWithinChunk, const int32 InChunkIndex, const int32 InChunkSerialNumber)
		: FMassRawEntityInChunkData(InChunkRawMemory, InIndexWithinChunk)
		, ChunkIndex(InChunkIndex), ChunkSerialNumber(InChunkSerialNumber)
{
}

inline bool FMassEntityInChunkDataHandle::operator==(const FMassEntityInChunkDataHandle& Other) const
{
	return FMassRawEntityInChunkData::operator==(Other)
		&& ChunkIndex == Other.ChunkIndex
		&& ChunkSerialNumber == Other.ChunkSerialNumber;
}

#undef UE_API 