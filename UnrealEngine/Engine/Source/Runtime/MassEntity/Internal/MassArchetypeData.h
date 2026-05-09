// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityHandle.h"
#include "MassArchetypeTypes.h"
#include "MassArchetypeGroup.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityManager.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "HAL/LowLevelMemTracker.h"

struct FMassEntityQuery;
struct FMassExecutionContext;
class FOutputDevice;
struct FMassArchetypeEntityCollection;
struct FMassFragmentRequirementDescription;
struct FMassFragmentRequirements;

namespace UE::Mass
{
	// Chunk 内存大小 sanitizer：把用户给的大小 clamp 到 [MinChunkMemorySize, MaxChunkMemorySize]
	// 实现细节在 .cpp 里，常量目前是 [1KB, 512KB]
	uint32 SanitizeChunkMemorySize(const uint32 InChunkMemorySize, const bool bLogMismatch = true);
}

// This is one chunk within an archetype
/**
 * 【FMassArchetypeChunk —— 一段 SoA 列式存储缓冲区】
 *
 * 一个 archetype 内每 chunk 都是一块**连续**的 uint8 缓冲 RawMemory，容量 AllocSize 字节（默认 128KB）。
 * 所有 fragment 的实例按"列"方式紧密排布：
 *
 *    RawMemory 布局（由 FMassArchetypeData::ConfigureFragments 计算）：
 *    ┌──────────────────────────────────────────────────────────────────────┐
 *    │ EntityHandle[0..N-1]        ← 先放 N 个 FMassEntityHandle（"身份列"）│
 *    ├──────────────────────────────────────────────────────────────────────┤
 *    │ FragA[0..N-1]               ← FragmentConfigs[0].ArrayOffsetWithinChunk │
 *    ├──────────────────────────────────────────────────────────────────────┤
 *    │ FragB[0..N-1]               ← FragmentConfigs[1].ArrayOffsetWithinChunk │
 *    ├──────────────────────────────────────────────────────────────────────┤
 *    │ ...                                                                  │
 *    ├──────────────────────────────────────────────────────────────────────┤
 *    │ (padding 直到 AllocSize)                                             │
 *    └──────────────────────────────────────────────────────────────────────┘
 * 其中 N = NumEntitiesPerChunk，在 ConfigureFragments 里按"能容纳的最大实体数"算。
 * fragment 列按 FragmentType 的 MinAlignment 对齐。
 *
 * 这种布局的意义：
 *  - processor 需要某个 fragment 时，拿到该列的起点指针 + SubchunkStart*SizeOf 就能得到一段连续
 *    的 TArrayView<T>，内层循环是 SoA tight loop —— cache 命中率非常高；
 *  - 多种 fragment 不在同一个 cache line 上，只读/只写其中几种时对其它 fragment 零污染。
 *
 * 成员还包含：
 *  - ChunkFragmentData —— 每 chunk 一份的"元数据"结构（chunk fragments，如统计、边界盒等）；
 *  - SharedFragmentValues —— 本 chunk 绑定的 shared fragment 值（决定该 chunk 归属哪些共享组）；
 *  - SerialModificationNumber —— 每次 add/remove 实例都递增，用于 FMassEntityInChunkDataHandle 校验。
 *
 * 生命周期：RawMemory 用 FMemory::Malloc 分配，chunk 清空时会主动释放以节省内存（见 RemoveMultipleInstances）；
 * 再次 Recycle 时重新 Malloc。chunk 析构时兜底 Free。
 */
struct FMassArchetypeChunk
{
private:
	// 裸内存缓冲（列式布局起点）；chunk 空时为 nullptr 以节省内存
	uint8* RawMemory = nullptr;
	// 本 chunk 的总字节数 = GetChunkAllocSize()，由 archetype 在构造时传入
	SIZE_T AllocSize = 0;
	// 当前已占用的实体槽位数 [0, NumEntitiesPerChunk]
	int32 NumInstances = 0;
	// 修改序列号：chunk 每发生 add/remove/recycle 都会 ++
	// FMassEntityInChunkDataHandle 用它来检测 chunk 是否被动过（swap-and-pop 后 handle 就失效了）
	int32 SerialModificationNumber = 0;
	// Chunk-level fragments：每个 chunk 一份（不跟实体个数挂钩），如 chunk aggregate / LOD flag 等
	TArray<FInstancedStruct> ChunkFragmentData;
	// 本 chunk 所关联的 shared fragment 值（同一 archetype 内，不同 shared value 的实体会被分到不同 chunk）
	FMassArchetypeSharedFragmentValues SharedFragmentValues;

public:
	// 构造 chunk：立刻分配 AllocSize 字节的缓冲；ChunkFragmentData 从 archetype 的模板拷贝过来
	// LLM_SCOPE_BYNAME 用于将本次分配归类到 "Mass/ArchetypeChunk" 预算项，方便内存分析
	explicit FMassArchetypeChunk(const SIZE_T InAllocSize, TConstArrayView<FInstancedStruct> InChunkFragmentTemplates, FMassArchetypeSharedFragmentValues InSharedFragmentValues)
		: AllocSize(InAllocSize)
		, ChunkFragmentData(InChunkFragmentTemplates)
		, SharedFragmentValues(InSharedFragmentValues)
	{
		
		LLM_SCOPE_BYNAME(TEXT("Mass/ArchetypeChunk"));
		RawMemory = (uint8*)FMemory::Malloc(AllocSize);
	}

	~FMassArchetypeChunk()
	{
		// Only release memory if it was not done already.
		// —— chunk 变空时 RemoveMultipleInstances 已主动 free；此处兜底
		if (RawMemory != nullptr)
		{
			FMemory::Free(RawMemory);
			RawMemory = nullptr;
		}
	}

	// Returns the Entity array element at the specified index
	/**
	 * 访问 chunk 里"身份列"第 IndexWithinChunk 个槽位的 FMassEntityHandle 的可变引用。
	 * ChunkBase 参数是 entity 列在 RawMemory 中的起始偏移（通常就是 0，见
	 * FMassArchetypeData::EntityListOffsetWithinChunk）。
	 */
	FMassEntityHandle& GetEntityArrayElementRef(int32 ChunkBase, int32 IndexWithinChunk)
	{
		uint8* RawMemoryChunkBase = RawMemory + ChunkBase;
		// check: 地址对齐 FMassEntityHandle 的 alignment，并且未越界
		checkSlow(ChunkBase + IndexWithinChunk * sizeof(FMassEntityHandle) < AllocSize
			&& (reinterpret_cast<SIZE_T>(RawMemoryChunkBase) % alignof(FMassEntityHandle)) == 0);
		return reinterpret_cast<FMassEntityHandle*>(RawMemoryChunkBase)[IndexWithinChunk];
	}

	// 只读拿 entity 列的数组基址（连续 NumInstances 个 FMassEntityHandle）
	const FMassEntityHandle* GetEntityArray(int32 ChunkBase) const
	{
		uint8* RawMemoryChunkBase = RawMemory + ChunkBase;
		checkSlow(ChunkBase < AllocSize
			&& (reinterpret_cast<SIZE_T>(RawMemoryChunkBase) % alignof(FMassEntityHandle)) == 0);
		return reinterpret_cast<const FMassEntityHandle*>(RawMemoryChunkBase);
	}

	// 暴露 RawMemory 起点给 FragmentConfig::GetFragmentData 计算实际指针
	uint8* GetRawMemory() const
	{
		return RawMemory;
	}

	int32 GetNumInstances() const
	{
		return NumInstances;
	}

	// 批量增减实例计数；递增 SerialModificationNumber 使旧的 InChunkDataHandle 失效
	void AddMultipleInstances(uint32 Count)
	{
		NumInstances += Count;
		SerialModificationNumber++;
	}

	void RemoveMultipleInstances(uint32 Count)
	{
		NumInstances -= Count;
		check(NumInstances >= 0);
		SerialModificationNumber++;

		// Because we only remove trailing chunks to avoid messing up the absolute indices in the entities map,
		// We are freeing the memory here to save memory
		// —— 变空但仍保留 Chunk 对象（避免挪动末尾之外的 chunk 破坏 EntityMap 的绝对索引）；
		// 但此时不再需要 RawMemory，释放之省内存。下次被 Recycle 时再 Malloc 回来
		if (NumInstances == 0)
		{
			FMemory::Free(RawMemory);
			RawMemory = nullptr;
		}
	}

	void AddInstance()
	{
		AddMultipleInstances(1);
	}

	void RemoveInstance()
	{
		RemoveMultipleInstances(1);
	}

	// 取当前 chunk 的修改序列号（每次增删 ++）；供 InChunkDataHandle 校验用
	int32 GetSerialModificationNumber() const
	{
		return SerialModificationNumber;
	}

	// 取第 Index 个 chunk fragment 的可变视图
	FStructView GetMutableChunkFragmentViewChecked(const int32 Index) { return FStructView(ChunkFragmentData[Index]); }

	// 按类型查找 chunk fragment 实例；IsChildOf 允许匹配派生类型
	FInstancedStruct* FindMutableChunkFragment(const UScriptStruct* Type)
	{
		return ChunkFragmentData.FindByPredicate([Type](const FInstancedStruct& Element)
			{
				return Element.GetScriptStruct()->IsChildOf(Type);
			});
	}

	/**
	 * 复用一个"已空"的 chunk：重置 chunk fragments 模板、shared values、SerialNumber，
	 * 若 RawMemory 之前被释放过则重新分配。避免反复申请/释放 chunk 对象本身。
	 */
	void Recycle(TConstArrayView<FInstancedStruct> InChunkFragmentsTemplate, const FMassArchetypeSharedFragmentValues& InSharedFragmentValues)
	{
		checkf(NumInstances == 0, TEXT("Recycling a chunk that is not empty."));
		SerialModificationNumber++;
		ChunkFragmentData = InChunkFragmentsTemplate;
		SharedFragmentValues = InSharedFragmentValues;
		
		// If this chunk previously had entity and it does not anymore, we might have to reallocate the memory as it was freed to save memory
		// —— RemoveMultipleInstances(最后一个) 时会 free RawMemory；此处按需重新分配
		if (RawMemory == nullptr)
		{
			RawMemory = (uint8*)FMemory::Malloc(AllocSize);
		}
	}

	// 判定 subchunk (StartIndex, Length) 是否落在本 chunk 合法范围内
	bool IsValidSubChunk(const int32 StartIndex, const int32 Length) const
	{
		return StartIndex >= 0 && StartIndex < NumInstances && (StartIndex + Length) <= NumInstances;
	}

#if WITH_MASSENTITY_DEBUG
	int32 DebugGetChunkFragmentCount() const { return ChunkFragmentData.Num(); }
#endif // WITH_MASSENTITY_DEBUG

	FMassArchetypeSharedFragmentValues& GetMutableSharedFragmentValues() { return SharedFragmentValues; }
	const FMassArchetypeSharedFragmentValues& GetSharedFragmentValues() const { return SharedFragmentValues; }
};

// Information for a single fragment type in an archetype
/**
 * 【FMassArchetypeFragmentConfig —— 某个 fragment 类型在本 archetype 中的列信息】
 *
 * archetype 里的 fragment 类型按 FScriptStructSortOperator 排序后，每种分配一个列：
 *  - FragmentType：该列存储的类型；
 *  - ArrayOffsetWithinChunk：该列起点相对于 chunk RawMemory 的字节偏移。
 *
 * 计算方式（ConfigureFragments）：先占 N 个 FMassEntityHandle 给身份列，然后依次按
 * 对齐要求摆放各 fragment 列，每列宽度 N * sizeof(Fragment)。
 */
struct FMassArchetypeFragmentConfig
{
	// 本列的 UScriptStruct 类型（cache 不变）
	const UScriptStruct* FragmentType = nullptr;
	// 本列在 chunk 缓冲内的起始字节偏移
	int32 ArrayOffsetWithinChunk = 0;

	/**
	 * 计算 chunk 内第 IndexWithinChunk 个实体的本 fragment 实例指针：
	 * addr = ChunkBase + ArrayOffsetWithinChunk + IndexWithinChunk * sizeof(Fragment)
	 * 由于列式布局，读连续实体的本列等同于 stride = sizeof(Fragment) 的 SoA 步进。
	 */
	void* GetFragmentData(uint8* ChunkBase, int32 IndexWithinChunk) const
	{
		return ChunkBase + ArrayOffsetWithinChunk + (IndexWithinChunk * FragmentType->GetStructureSize());
	}
};

// An archetype is defined by a collection of unique fragment types (no duplicates).
// Order doesn't matter, there will only ever be one FMassArchetypeData per unique set of fragment types per entity manager subsystem
/**
 * 【FMassArchetypeData —— Mass 存储的核心】
 *
 * 一个 archetype = "固定的 fragment/tag/chunk-fragment/shared-fragment 组合"。持有该组合的所有
 * 实体共享同样的 chunk 布局，存储在 Chunks 数组里。entity manager 保证同一 composition 只有
 * 一份 FMassArchetypeData 实例。
 *
 * 关键数据：
 *  - CompositionDescriptor：完整组合（5 个 bitset）；
 *  - FragmentConfigs + FragmentIndexMap：每种 fragment 的 (offset, size) + 反向索引；
 *  - Chunks：列式 chunk 的动态数组；新实体先填非满 chunk，否则新增；删除只折叠末尾空 chunk；
 *  - EntityMap：FMassEntityHandle.Index → 该实体在整个 archetype 内的"绝对索引" AbsoluteIndex；
 *    ChunkIndex = AbsoluteIndex / N，IndexWithinChunk = AbsoluteIndex % N；
 *  - EntityOrderVersion：实体顺序每变动一次就 ++，供 versioned handle 检测过期；
 *  - CreatedArchetypeDataVersion：archetype 创建时的全局版本号，供 query 做增量匹配。
 *
 * 不变式：
 *  - Chunks 中间不会出现"空 chunk 被删"的情况，因为 EntityMap 存的是绝对索引，移除中间 chunk 会
 *    让后续 chunk 的所有绝对索引错位。只有"末尾连续的空 chunk"可以被 pop。
 *  - 同一 chunk 内所有实体共享同一组 SharedFragmentValues；不同 shared value 的实体分到不同 chunk。
 */
struct FMassArchetypeData
{
private:
	// One-stop-shop variable describing the archetype's fragment and tag composition 
	// 组合描述符：5 个 bitset（fragments/tags/chunk-fragments/shared/const-shared）。archetype 的"身份"
	FMassArchetypeCompositionDescriptor CompositionDescriptor;

	// Pre-created default chunk fragment templates
	// chunk fragment 的默认值模板，新 chunk 创建时拷贝一份供其使用
	TArray<FInstancedStruct> ChunkFragmentsTemplate;

	// 每 fragment 一个 config（列类型 + 列偏移）；按 UScriptStruct 的名字排序以保持跨次一致
	// TInlineAllocator<16>：绝大多数 archetype 少于 16 个 fragment，避免堆分配
	TArray<FMassArchetypeFragmentConfig, TInlineAllocator<16>> FragmentConfigs;
	
	// 所有 chunk 的动态数组；中间位置的 chunk 即便变空也**不会被删**（否则绝对索引会错位）
	TArray<FMassArchetypeChunk> Chunks;

	// Entity ID to index within archetype
	//@TODO: Could be folded into FEntityData in the entity manager at the expense of a bit
	// of loss of encapsulation and extra complexity during archetype changes
	// 反向索引：FMassEntityHandle.Index → archetype 内绝对索引 (AbsoluteIndex)。
	// AbsoluteIndex / NumEntitiesPerChunk = chunk 下标；% = chunk 内槽位
	// TODO 注释提示：未来可以挪到 EntityManager 的 FEntityData 里减少一次查表，但会损失封装
	TMap<int32, int32> EntityMap;
	
	// UScriptStruct → FragmentConfigs 的下标；processor 按需求类型快速定位列
	TMap<const UScriptStruct*, int32> FragmentIndexMap;

	// Archetype 所属的 group 集合（标签式分组），供 UE::Mass::FArchetypeGroups 查询
	UE::Mass::FArchetypeGroups Groups;

	// 每 chunk 能容纳的实体数 = (ChunkSize - alignmentPadding) / TotalBytesPerEntity
	int32 NumEntitiesPerChunk;
	// 每个实体占用的字节数（所有 fragment 之和 + 1 个 FMassEntityHandle）
	uint32 TotalBytesPerEntity = 0;
	// Entity 列在 chunk 缓冲里的起始偏移；目前恒为 0（身份列在最前面）
	int32 EntityListOffsetWithinChunk;

	/**
	 * Archetype version at which this archetype was created, useful for query to do incremental archetype matching.
	 * Note that it's set once and never changed afterward.
	 */
	/**
	 * 创建此 archetype 时 EntityManager 的"archetype 版本号"。Query 维护一个"上次看到的版本号"，
	 * 本字段 > 它则表示 query 需要把本 archetype 做一次 requirement 匹配。创建后不再变化。
	 */
	uint32 CreatedArchetypeDataVersion = 0;

	/**
	 * Incremented whenever an operation modifies the order of hosted entities, for example entity removal and compaction.
	 * This value is used to validate stored entity ranges, including FMassArchetypeEntityCollection.
	 */
	/**
	 * 【实体顺序版本号】每当实体槽位顺序可能变化时 ++。典型触发：
	 *  - RemoveEntityInternal 的 swap-and-pop；
	 *  - BatchRemoveEntitiesInternal；
	 *  - CompactEntities（压缩 chunk）；
	 * FMassArchetypeVersionedHandle / FMassArchetypeEntityCollection 会比对此值检测过期。
	 */
	uint32 EntityOrderVersion = 0;

	/** Defaults to UMassEntitySettings.ChunkMemorySize. In near future will support being set via constructor. */
	/** 单 chunk 分配字节数；默认读 UMassEntitySettings.ChunkMemorySize（通常 128KB）。初始化后不变 */
	const uint32 ChunkMemorySize = 0;

#if WITH_MASSENTITY_DEBUG
	/** Arrays of names the archetype is referred as. */
	/** 调试名：同一个 archetype 可被多处代码命名，这里汇总 */
	TArray<FName> DebugNames;

	/**
	 * Color to be used when representing this archetype. If not set with FMassArchetypeCreationParams
	 * will be deterministically set based on archetype's composition. Can be overridden at any point 
	 * via SetDebugColor.
	 */
	/** 调试色：未指定时从 composition hash 派生，保证同一 archetype 每次显示同色 */
	FColor DebugColor;
#endif // WITH_MASSENTITY_DEBUG
	
	friend FMassEntityQuery;
	friend FMassArchetypeEntityCollection;
	friend FMassDebugger;

public:
	// 构造函数：仅记录 ChunkMemorySize 和 debug 信息；真正的 fragment 布局要等 Initialize 调用
	explicit FMassArchetypeData(const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	TConstArrayView<FMassArchetypeFragmentConfig> GetFragmentConfigs() const { return FragmentConfigs; }
	const FMassFragmentBitSet& GetFragmentBitSet() const { return CompositionDescriptor.GetFragments(); }
	const FMassTagBitSet& GetTagBitSet() const { return CompositionDescriptor.GetTags(); }
	const FMassChunkFragmentBitSet& GetChunkFragmentBitSet() const { return CompositionDescriptor.GetChunkFragments(); }
	const FMassSharedFragmentBitSet& GetSharedFragmentBitSet() const { return CompositionDescriptor.GetSharedFragments(); }
	const FMassConstSharedFragmentBitSet& GetConstSharedFragmentBitSet() const { return CompositionDescriptor.GetConstSharedFragments(); }

	const FMassArchetypeCompositionDescriptor& GetCompositionDescriptor() const { return CompositionDescriptor; }
	// 按 EntityIndex 快速定位所在 chunk 并返回其 shared values（热路径：processor 读 shared fragment）
	FORCEINLINE const FMassArchetypeSharedFragmentValues& GetSharedFragmentValues(int32 EntityIndex) const
	{ 
		const int32 AbsoluteIndex = EntityMap.FindChecked(EntityIndex);
		const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;

		return Chunks[ChunkIndex].GetSharedFragmentValues();
	}
	FORCEINLINE const FMassArchetypeSharedFragmentValues& GetSharedFragmentValues(FMassEntityHandle Entity) const
	{
		return GetSharedFragmentValues(Entity.Index);
	}

	const UE::Mass::FArchetypeGroups& GetGroups() const;
	bool IsInGroup(const UE::Mass::FArchetypeGroupHandle GroupHandle) const;
	bool IsInGroupOfType(const UE::Mass::FArchetypeGroupType GroupType) const;

	/** Method to iterate on all the fragment types */
	// 遍历所有 fragment 列的类型（按定义顺序 = 按 Struct 名字排序后的顺序）
	void ForEachFragmentType(TFunction< void(const UScriptStruct* /*FragmentType*/)> Function) const;
	// 是否含给定 fragment 类型
	bool HasFragmentType(const UScriptStruct* FragmentType) const;
	// 是否含给定 tag 类型
	bool HasTagType(const UScriptStruct* FragmentType) const { check(FragmentType); return CompositionDescriptor.GetTags().Contains(*FragmentType); }

	// 判断本 archetype 的 composition + groups 是否与给定参数一致（用于 EntityManager 查找已有 archetype）
	bool IsEquivalent(const FMassArchetypeCompositionDescriptor& OtherCompositionDescriptor, const UE::Mass::FArchetypeGroups& OtherGroups) const;

	// 初始化：设置 composition，计算 fragment 列布局，准备 chunk fragment 模板
	void Initialize(const FMassEntityManager& EntityManager, const FMassArchetypeCompositionDescriptor& InCompositionDescriptor, const uint32 ArchetypeDataVersion);

	/** 
	 * A special way of initializing an archetype resulting in a copy of BaseArchetype's setup with OverrideTags
	 * replacing original tags of BaseArchetype
	 */
	/**
	 * 基于已有 archetype "克隆 + 改 tag/group" 创建新 archetype。
	 * 若 fragment 集合不变则直接复用 BaseArchetype 的 FragmentConfigs 等列布局信息，节省初始化开销。
	 */
	void InitializeWithSimilar(const FMassEntityManager& EntityManager, const FMassArchetypeData& BaseArchetype
		, FMassArchetypeCompositionDescriptor&& NewComposition, const UE::Mass::FArchetypeGroups& InGroups, const uint32 ArchetypeDataVersion);

	// 单实体 add：找（或创建）含相同 SharedValues 的 chunk 末尾槽位，并初始化 fragment 构造
	void AddEntity(FMassEntityHandle Entity, const FMassArchetypeSharedFragmentValues& InSharedFragmentValues);
	// 单实体 remove：DestroyStruct 所有 fragment，然后 swap-and-pop 将末尾实体补到空缺位置
	void RemoveEntity(FMassEntityHandle Entity);

	// 组合中是否含 FragmentType（名称上容易和 GetFragmentDataForEntity 混淆）
	bool HasFragmentDataForEntity(const UScriptStruct* FragmentType, int32 EntityIndex) const;
	// 取 fragment 数据指针；找不到会 checkFail
	void* GetFragmentDataForEntityChecked(const UScriptStruct* FragmentType, int32 EntityIndex) const;
	// 取 fragment 数据指针；找不到返回 nullptr
	void* GetFragmentDataForEntity(const UScriptStruct* FragmentType, int32 EntityIndex) const;

	// 反向索引查找：未找到返回 nullptr
	FORCEINLINE const int32* GetInternalIndexForEntity(const int32 EntityIndex) const { return EntityMap.Find(EntityIndex); }
	// 反向索引查找：未找到 checkFail
	FORCEINLINE int32 GetInternalIndexForEntityChecked(const int32 EntityIndex) const { return EntityMap.FindChecked(EntityIndex); }
	int32 GetNumEntitiesPerChunk() const { return NumEntitiesPerChunk; }
	SIZE_T GetBytesPerEntity() const { return TotalBytesPerEntity; }

	int32 GetNumEntities() const { return EntityMap.Num(); }

	SIZE_T GetChunkAllocSize() const { return ChunkMemorySize; }

	int32 GetChunkCount() const { return Chunks.Num(); }
	// 非空 chunk 数量（跳过 RawMemory==nullptr 的空 chunk）
	int32 GetNonEmptyChunkCount() const;

	/**
	 * 计算 range 实际覆盖的实体数：
	 *  - Length > 0 → 按字段；
	 *  - Length <=0 → "到 chunk 末尾" 即 NumInstances - SubchunkStart。
	 */
	FORCEINLINE static int32 CalculateRangeLength(FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange, const FMassArchetypeChunk& Chunk)
	{
		return EntityRange.Length > 0
			? EntityRange.Length
			: (Chunk.GetNumInstances() - EntityRange.SubchunkStart);	
	}

	int32 CalculateRangeLength(FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange) const
	{
		check(Chunks.IsValidIndex(EntityRange.ChunkIndex));
		const FMassArchetypeChunk& Chunk = Chunks[EntityRange.ChunkIndex];
		return CalculateRangeLength(EntityRange, Chunk);
	}

	uint32 GetCreatedArchetypeDataVersion() const;
	uint32 GetEntityOrderVersion() const;

	/**
	 * 按给定 EntityRangeContainer 执行 Function；内部会为每 range：
	 *  1. 绑定 chunk/shared fragment views；
	 *  2. 可选跑 ChunkCondition；
	 *  3. 绑定 entity fragment views 并调用 Function。
	 * 热路径 —— 请看 .cpp 注释。
	 */
	void ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping
		, FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRangeContainer, const FMassChunkConditionFunction& ChunkCondition);
	// 遍历 archetype 所有 chunk 版本；支持 ExecutionLimiter 做分帧/分段执行
	void ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping
		, const FMassChunkConditionFunction& ChunkCondition, UE::Mass::FExecutionLimiter* ExecutionLimiter = nullptr);

	// 只对单个 range（一个 chunk 内的连续段）执行的版本
	void ExecutionFunctionForChunk(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping
		, const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange, const FMassChunkConditionFunction& ChunkCondition = FMassChunkConditionFunction());

	/**
	 * Compacts entities to fill up chunks as much as possible
	 * @return number of entities moved around
	 */
	/**
	 * 压缩稀疏 chunk：把同 shared values 的 chunks 里的实体往前填，让部分 chunk 彻底变空。
	 * 输入 TimeAllowed 为秒级时间预算，超预算会中途 break。
	 * @return 挪动了多少个实体（>0 会 ++EntityOrderVersion）
	 */
	int32 CompactEntities(const double TimeAllowed);

	/**
	 * Moves the entity from this archetype to another, will only copy all matching fragment types
	 * @param Entity is the entity to move
	 * @param NewArchetype the archetype to move to
	 * @param SharedFragmentValuesOverride if provided will override all given Entity's shared fragment values
	 */
	/**
	 * 把实体从本 archetype 迁到 NewArchetype：
	 *  - 两 archetype 共有的 fragment 会 memcpy 原值过去（不触发构造/析构，为性能）；
	 *  - NewArchetype 独有的 fragment 会用默认值 InitializeStruct；
	 *  - 本 archetype 独有的 fragment 会 DestroyStruct。
	 * 注意：跨 archetype 不等于"重新创建实体"，EntityHandle 不变。
	 */
	void MoveEntityToAnotherArchetype(const FMassEntityHandle Entity, FMassArchetypeData& NewArchetype, const FMassArchetypeSharedFragmentValues* SharedFragmentValuesOverride = nullptr);

	/**
	 * Set all fragment sources data on specified entity, will check if there are fragment sources type that does not exist in the archetype
	 * @param Entity is the entity to set the data of all fragments
	 * @param FragmentSources are the fragments to copy the data from
	 */
	/**
	 * 批量覆盖指定实体的多个 fragment；FragmentSources 里若含 archetype 不存在的类型会 checkFail
	 */
	void SetFragmentsData(const FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentSources);

	/** For all entities indicated by EntityCollection the function sets the value of fragment of type
	 *  FragmentSource.GetScriptStruct to the value represented by FragmentSource.GetMemory */
	/** 对 EntityRangeContainer 覆盖的所有实体，把 FragmentSource 类型的值批量 copy 进去 */
	void SetFragmentData(FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRangeContainer, const FInstancedStruct& FragmentSource);

	/** Returns conversion from given Requirements to archetype's fragment indices */
	/** 把 query requirement（按类型）映射到本 archetype 的 fragment 列下标；optional 缺失填 INDEX_NONE */
	void GetRequirementsFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices) const;

	/** Returns conversion from given ChunkRequirements to archetype's chunk fragment indices */
	/** 同上，但用于 chunk fragment（需遵循排序好的 ChunkFragmentsTemplate 顺序） */
	void GetRequirementsChunkFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> ChunkRequirements, FMassFragmentIndicesMapping& OutFragmentIndices) const;

	/** Returns conversion from given const shared requirements to archetype's const shared fragment indices */
	/** const shared fragment 的映射：chunk[0] 的 shared values 即可代表整个 archetype 的 shared 类型布局 */
	void GetRequirementsConstSharedFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices) const;

	/** Returns conversion from given shared requirements to archetype's shared fragment indices */
	/** 可变 shared fragment 的映射 */
	void GetRequirementsSharedFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices) const;

	// 汇总 archetype 占用的总内存（含 chunk 缓冲 + 元数据容器）
	SIZE_T GetAllocatedSize() const;

	// 导出 Ranges 覆盖的 FMassEntityHandle 到 InOutHandles 末尾（按 range 顺序 memcpy）
	void ExportEntityHandles(const TConstArrayView<FMassArchetypeEntityCollection::FArchetypeEntityRange> Ranges, TArray<FMassEntityHandle>& InOutHandles) const;

	// 导出所有 chunk 的全部实体
	void ExportEntityHandles(TArray<FMassEntityHandle>& InOutHandles) const;

	// Converts the list of fragments into a user-readable debug string
	FString DebugGetDescription() const;

	/** Copies debug names from another archetype data. */
	void CopyDebugNamesFrom(const FMassArchetypeData& Other)
	{ 
#if WITH_MASSENTITY_DEBUG
		DebugNames = Other.DebugNames; 
#endif // WITH_MASSENTITY_DEBUG
	}

#if WITH_MASSENTITY_DEBUG
	/** Fetches how much memory is allocated for active chunks, and how much of that memory is actually occupied */
	/** debug：活跃 chunk 总大小 vs 实际实体占用大小，用于评估碎片率 */
	void DebugGetEntityMemoryNumbers(SIZE_T& OutActiveChunksMemorySize, SIZE_T& OutActiveEntitiesMemorySize) const;

	/** Adds new debug name associated with the archetype. */
	void AddUniqueDebugName(const FName& Name) { DebugNames.AddUnique(Name); }
	
	/** @return array of debug names associated with this archetype. */
	const TConstArrayView<FName> GetDebugNames() const { return DebugNames; }
	
	/** @return string of all debug names combined */
	FString GetCombinedDebugNamesAsString() const;

	/**
	 * Prints out debug information about the archetype
	 */
	/** 打印 chunk 数、内存布局、占用率等诊断信息 */
	void DebugPrintArchetype(FOutputDevice& Ar);

	/**
	 * Prints out fragment's values for the specified entity. 
	 * @param Entity The entity for which we want to print fragment values
	 * @param Ar The output device
	 * @param InPrefix Optional prefix to remove from fragment names
	 */
	void DebugPrintEntity(FMassEntityHandle Entity, FOutputDevice& Ar, const TCHAR* InPrefix = TEXT("")) const;
#endif // WITH_MASSENTITY_DEBUG

	void SetDebugColor(const FColor InDebugColor);

	// TODO: 旧接口，预备移除；直接拿某 chunk 第 0 号槽位起的某 fragment 列指针
	void REMOVEME_GetArrayViewForFragmentInChunk(int32 ChunkIndex, const UScriptStruct* FragmentType, void*& OutChunkBase, int32& OutNumEntities);

	//////////////////////////////////////////////////////////////////////
	// low level api
	// 【低阶 API】给高度性能敏感的调用方：把"类型 → 列下标"的一次查找手动缓存，后续用 idx 直接访问
	FORCEINLINE const int32* GetFragmentIndex(const UScriptStruct* FragmentType) const { return FragmentIndexMap.Find(FragmentType); }
	FORCEINLINE int32 GetFragmentIndexChecked(const UScriptStruct* FragmentType) const { return FragmentIndexMap.FindChecked(FragmentType); }

	// 用 raw handle（不带版本校验）取 fragment 指针；性能最好但不安全
	FORCEINLINE void* GetFragmentData(const int32 FragmentIndex, const FMassRawEntityInChunkData RawEntityInChunkHandle) const
	{
		return FragmentConfigs[FragmentIndex].GetFragmentData(RawEntityInChunkHandle.ChunkRawMemory, RawEntityInChunkHandle.IndexWithinChunk);
	}

	// 校验 InChunkDataHandle 是否指向仍然存在的 chunk 且 SerialNumber 未变
	FORCEINLINE bool IsValidHandle(const FMassEntityInChunkDataHandle Handle) const
	{
		return Handle.IsSet() && Chunks.IsValidIndex(Handle.ChunkIndex) && Chunks[Handle.ChunkIndex].GetSerialModificationNumber() == Handle.ChunkSerialNumber;
	}

	// 带版本校验的 handle 版本；失败会 checkFail
	FORCEINLINE void* GetFragmentData(const int32 FragmentIndex, const FMassEntityInChunkDataHandle EntityInChunkHandle) const
	{
		checkf(IsValidHandle(EntityInChunkHandle), TEXT("Input FMassRawEntityInChunkData is out of date."));
		return FragmentConfigs[FragmentIndex].GetFragmentData(EntityInChunkHandle.ChunkRawMemory, EntityInChunkHandle.IndexWithinChunk);
	}

	// 构造不带版本的 raw handle：通过 EntityIndex 查 AbsoluteIndex 并换算 chunk 内坐标
	FORCEINLINE FMassRawEntityInChunkData MakeRawEntityHandle(int32 EntityIndex) const
	{
		const int32 AbsoluteIndex = EntityMap.FindChecked(EntityIndex);
		const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	
		return FMassRawEntityInChunkData(Chunks[ChunkIndex].GetRawMemory(), AbsoluteIndex % NumEntitiesPerChunk);
	}

	FORCEINLINE FMassRawEntityInChunkData MakeRawEntityHandle(const FMassEntityHandle Entity) const
	{
		return MakeRawEntityHandle(Entity.Index); 
	}

	// 构造带版本的 handle：再抓一个 SerialModificationNumber 快照用于后续校验
	FORCEINLINE FMassEntityInChunkDataHandle MakeEntityHandle(int32 EntityIndex) const
	{
		const int32 AbsoluteIndex = EntityMap.FindChecked(EntityIndex);
		const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
		const FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

		return FMassEntityInChunkDataHandle(Chunk.GetRawMemory(), AbsoluteIndex % NumEntitiesPerChunk
			, ChunkIndex, Chunk.GetSerialModificationNumber());
	}

	FORCEINLINE FMassEntityInChunkDataHandle MakeEntityHandle(const FMassEntityHandle Entity) const
	{
		return MakeEntityHandle(Entity.Index); 
	}

	// 是否已 Initialize 过：有 fragment 且 TotalBytesPerEntity 已计算
	bool IsInitialized() const { return TotalBytesPerEntity > 0 && FragmentConfigs.IsEmpty() == false; }

	//////////////////////////////////////////////////////////////////////
	// batched api
	// 【批处理 API】比逐实体 Add/Remove/Move 性能显著更高，一次性处理 range
	// 批量销毁：按 range 析构 fragment + swap-and-pop 补洞，最后统一从 EntityMap 删除
	void BatchDestroyEntityChunks(FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRangeContainer, TArray<FMassEntityHandle>& OutEntitiesRemoved);
	// 批量新增：按 chunk 剩余空间切分成多段 range，内部保证每段在同一 chunk
	void BatchAddEntities(TConstArrayView<FMassEntityHandle> Entities, const FMassArchetypeSharedFragmentValues& SharedFragmentValues
		, TArray<FMassArchetypeEntityCollection::FArchetypeEntityRange>& OutNewRanges);
	/** 
	 * @param SharedFragmentValuesOverride if provided will override shared fragment values for the entities being moved
	 */
	/**
	 * 批量跨 archetype 迁移：
	 *  - EntityCollection 指定要迁的实体批次；
	 *  - 可指定 SharedFragment 增删集合，用于"同时改 shared"
	 *  - OutNewChunks 返回迁移后在新 archetype 中的 range 列表（可用于后续 processor 操作）
	 */
	void BatchMoveEntitiesToAnotherArchetype(const FMassArchetypeEntityCollection& EntityCollection, FMassArchetypeData& NewArchetype
		, TArray<FMassEntityHandle>& OutEntitiesBeingMoved, TArray<FMassArchetypeEntityCollection::FArchetypeEntityRange>* OutNewChunks = nullptr
		, const FMassArchetypeSharedFragmentValues* SharedFragmentValuesToAdd = nullptr
		, const FMassSharedFragmentBitSet* SharedFragmentToRemoveBitSet = nullptr
		, const FMassConstSharedFragmentBitSet* ConstSharedFragmentToRemoveBitSet = nullptr);
	// 批量 set：按 range 向每列 memcpy payload
	void BatchSetFragmentValues(TConstArrayView<FMassArchetypeEntityCollection::FArchetypeEntityRange> EntityCollection, const FMassGenericPayloadViewSlice& Payload);

protected:
	// 为一批待入 archetype 的实体准备下一段连续 range：找非满 chunk（或新建）并分配尾部槽位
	FMassArchetypeEntityCollection::FArchetypeEntityRange PrepareNextEntitiesSpanInternal(TConstArrayView<FMassEntityHandle> Entities, const FMassArchetypeSharedFragmentValues& InSharedFragmentValues, const int32 StartingChunk = 0);
	// 批量移除实现：用"chunk 尾部 NumberToMove 个实体"覆盖 [StartIndexWithinChunk, +NumberToRemove) 段
	void BatchRemoveEntitiesInternal(const int32 ChunkIndex, const int32 StartIndexWithinChunk, const int32 NumberToRemove);

	// 跨 chunk 的内存定位（archetype + 裸指针 + 槽位），仅作临时参数用
	struct FTransientChunkLocation
	{
		uint8* RawChunkMemory;
		int32 IndexWithinChunk;
	};
	// 把 ElementsNum 个实体的 fragment 数据从 Source 挪到 TargetArchetype 的 Target 位置；
	// 处理"两 archetype 仅部分 fragment 重合"的情形：共有的 memcpy、新增的 InitializeStruct、丢弃的 DestroyStruct
	void MoveFragmentsToAnotherArchetypeInternal(FMassArchetypeData& TargetArchetype, FTransientChunkLocation Target, const FTransientChunkLocation Source, const int32 ElementsNum);
	// 同 archetype 内的拷贝（用于 swap-and-pop / compaction）：所有 fragment 都按本 archetype 的 FragmentConfigs memcpy
	void MoveFragmentsToNewLocationInternal(FTransientChunkLocation Target, const FTransientChunkLocation Source, const int32 NumberToMove);
	// 根据 CompositionDescriptor 计算每 fragment 列的 offset/size，得出 NumEntitiesPerChunk / TotalBytesPerEntity
	void ConfigureFragments(const FMassEntityManager& EntityManager);

	// protected 版本的 GetFragmentData：接受裸指针+槽位；供内部批处理用
	FORCEINLINE void* GetFragmentData(const int32 FragmentIndex, uint8* ChunkRawMemory, const int32 IndexWithinChunk) const
	{
		return FragmentConfigs[FragmentIndex].GetFragmentData(ChunkRawMemory, IndexWithinChunk);
	}

	// 【ApplyFragmentRequirements 家族】把 archetype 的列数据绑定到 ExecutionContext 的 TArrayView 上
	// BindEntityRequirements：让 RunContext.FragmentViews[i] = TArrayView<T>(列指针, SubchunkLength) —— processor 遍历时就能直接 Context.GetFragmentView<T>() 访问
	void BindEntityRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& EntityFragmentsMapping, FMassArchetypeChunk& Chunk, const int32 SubchunkStart, const int32 SubchunkLength);
	// BindChunkFragmentRequirements：绑定每 chunk 一份的元数据 fragment
	void BindChunkFragmentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& ChunkFragmentsMapping, FMassArchetypeChunk& Chunk);
	// BindConstSharedFragmentRequirements：绑定只读 shared fragment（同一 shared values 的多个 chunk 共享实例）
	void BindConstSharedFragmentRequirements(FMassExecutionContext& RunContext, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassFragmentIndicesMapping& ChunkFragmentsMapping);
	// BindSharedFragmentRequirements：绑定可写 shared fragment
	void BindSharedFragmentRequirements(FMassExecutionContext& RunContext, FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassFragmentIndicesMapping& ChunkFragmentsMapping);

	/**
	 * The function first creates new FMassArchetypeSharedFragmentValues instance combining existing values
	 * and the contents of SharedFragmentValueOverrides. Then that is used to find the target chunk for Entity,
	 * and if one cannot be found a new one will be created. 
	 * @param SharedFragmentValueOverrides is expected to contain only instance of types already
	 *    present in given archetypes FMassArchetypeSharedFragmentValues
	 */
	/**
	 * 修改实体的 shared fragment 值（同 archetype 但换 chunk）：
	 *  1. 把旧 shared 与 override 合并排序得到新 shared values；
	 *  2. 在本 archetype 找到/新建匹配的 chunk 当作目标槽位；
	 *  3. MoveFragmentsToNewLocationInternal 把 fragment 数据挪过去；
	 *  4. 原槽位做 swap-and-pop。
	 */
	void SetSharedFragmentsData(const FMassEntityHandle Entity, TConstArrayView<FSharedStruct> SharedFragmentValueOverrides);

	// 根据 SharedFragmentValues 找到可追加的 chunk：优先填已存在且非满的 chunk，其次回收空 chunk，最后新建
	FMassArchetypeChunk& GetOrAddChunk(const FMassArchetypeSharedFragmentValues& SharedFragmentValues, int32& OutAbsoluteIndex, int32& OutIndexWithinChunk);
	
private:
	// 核心新增：找目标 chunk，累加实例计数，写 EntityMap + entity 列槽位，不做 fragment 构造（由 AddEntity 完成）
	int32 AddEntityInternal(FMassEntityHandle Entity, const FMassArchetypeSharedFragmentValues& InSharedFragmentValues);
	// 核心移除：swap-and-pop。见 .cpp 详细注释
	void RemoveEntityInternal(const int32 AbsoluteIndex);
};


/**
 * 【FMassArchetypeHelper —— 句柄/数据互转工具】
 *
 * 为什么单独存在？因为 FMassArchetypeHandle::DataPtr 是 private，外部模块（Query、
 * Collection、Debugger 等）需要一个统一通道取裸指针，避免到处加 friend。
 */
struct FMassArchetypeHelper
{
	FORCEINLINE static FMassArchetypeData* ArchetypeDataFromHandle(const FMassArchetypeHandle& ArchetypeHandle) { return ArchetypeHandle.DataPtr.Get(); }
	FORCEINLINE static FMassArchetypeData& ArchetypeDataFromHandleChecked(const FMassArchetypeHandle& ArchetypeHandle)
	{
		check(ArchetypeHandle.IsValid());
		return *ArchetypeHandle.DataPtr.Get();
	}
	FORCEINLINE static FMassArchetypeHandle ArchetypeHandleFromData(const TSharedPtr<FMassArchetypeData>& Archetype)
	{
		return FMassArchetypeHandle(Archetype);
	}

	/**
	 * Determines whether given Archetype matches given Requirements. In case of failure to match and if WITH_MASSENTITY_DEBUG
	 * the function will also log the reasons for said failure (at VeryVerbose level).
	 * @param bBailOutOnFirstFail if true will skip the remaining tests as soon as a single mismatch is detected. This option
	 *	is used when looking for matching archetypes. For debugging purposes use `false` to list all the mismatching elements.
	 */
	/**
	 * 判断 archetype 是否满足 query requirements。debug 版可记录不匹配原因供诊断。
	 * bBailOutOnFirstFail=true 是热路径默认值；false 用于 "请告诉我所有不匹配点" 的诊断场景。
	 */
#if WITH_MASSENTITY_DEBUG
	MASSENTITY_API static bool DoesArchetypeMatchRequirements(const FMassArchetypeData& Archetype, const FMassFragmentRequirements& Requirements
		, const bool bBailOutOnFirstFail = true, FOutputDevice* OutputDevice = nullptr);
#endif // WITH_MASSENTITY_DEBUG

	MASSENTITY_API static bool DoesArchetypeMatchRequirements(const FMassArchetypeData& Archetype, const FMassFragmentRequirements& Requirements);
	MASSENTITY_API static bool DoesArchetypeMatchRequirements(const FMassArchetypeCompositionDescriptor& ArchetypeComposition, const FMassFragmentRequirements& Requirements);
};

//-----------------------------------------------------------------------------
// INLINES
//-----------------------------------------------------------------------------
inline const UE::Mass::FArchetypeGroups& FMassArchetypeData::GetGroups() const
{
	return Groups;
}

inline bool FMassArchetypeData::IsInGroup(const UE::Mass::FArchetypeGroupHandle GroupHandle) const
{
	if (GroupHandle.IsValid())
	{
		UE::Mass::FArchetypeGroupID FoundGroupID = Groups.GetID(GroupHandle.GetGroupType());
		return FoundGroupID.IsValid() && FoundGroupID == GroupHandle.GetGroupID();
	}
	return false;
}

inline bool FMassArchetypeData::IsInGroupOfType(const UE::Mass::FArchetypeGroupType GroupType) const
{
	return Groups.ContainsType(GroupType);
}

inline uint32 FMassArchetypeData::GetCreatedArchetypeDataVersion() const
{
	return CreatedArchetypeDataVersion;
}

inline uint32 FMassArchetypeData::GetEntityOrderVersion() const
{
	return EntityOrderVersion;
}
