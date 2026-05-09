// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassArchetypeData.h"
#include "MassEntityTypes.h"
#include "MassExecutionContext.h"
#include "MassEntitySettings.h"
#include "MassDebugger.h"
#include "MassRequirements.h"
#include "Misc/StringBuilder.h"
#include "Misc/StringOutputDevice.h"

DECLARE_CYCLE_STAT(TEXT("Mass Archetype BatchAdd"), STAT_Mass_ArchetypeBatchAdd, STATGROUP_Mass);

namespace UE::Mass
{
	namespace Private
	{
		// 用于标记尚未初始化的 int32 字段（代替 INDEX_NONE/-1 的语义）
		constexpr int32 UninitializedInt32 = -1;
		// Chunk 缓冲最小 1 KiB —— 防止用户配置过小导致单个实体都放不下
		constexpr uint32 MinChunkMemorySize = 1024;
		// Chunk 缓冲最大 512 KiB —— 避免单 chunk 大到影响局部性和分配
		constexpr uint32 MaxChunkMemorySize = 512 * 1024;
	}

	// 把用户在 UMassEntitySettings 里给的 ChunkMemorySize 夹到 [1KB, 512KB]，并可选打 warning
	uint32 SanitizeChunkMemorySize(const uint32 InChunkMemorySize, const bool bLogMismatch)
	{
		const uint32 SanitizedSize = FMath::Clamp(InChunkMemorySize, Private::MinChunkMemorySize, Private::MaxChunkMemorySize);
		UE_CLOG(bLogMismatch && SanitizedSize != InChunkMemorySize, LogMass, Warning
			, TEXT("ChunkMemorySize sanitization resulted in changing value. Old: %u, modified: %u")
			, InChunkMemorySize, SanitizedSize);
		return SanitizedSize;
	}
}

//-----------------------------------------------------------------------------
// FMassArchetypeData
//-----------------------------------------------------------------------------
/**
 * 构造：仅记录 ChunkMemorySize 和 debug 信息；真正的 fragment 布局计算要等 Initialize。
 * CreationParams.ChunkMemorySize==0 时会 fallback 到 settings 里的全局默认值。
 */
FMassArchetypeData::FMassArchetypeData(const FMassArchetypeCreationParams& CreationParams)
	: NumEntitiesPerChunk(UE::Mass::Private::UninitializedInt32)
	, EntityListOffsetWithinChunk(UE::Mass::Private::UninitializedInt32)
	, ChunkMemorySize(UE::Mass::SanitizeChunkMemorySize(CreationParams.ChunkMemorySize ? CreationParams.ChunkMemorySize : GET_MASS_CONFIG_VALUE(ChunkMemorySize)))
{
#if WITH_MASSENTITY_DEBUG
	DebugNames.Add(CreationParams.DebugName);
	DebugColor = CreationParams.DebugColor;
#endif // WITH_MASSENTITY_DEBUG
}

void FMassArchetypeData::ForEachFragmentType(TFunction< void(const UScriptStruct* /*Fragment*/)> Function) const
{
	for (const FMassArchetypeFragmentConfig& FragmentData : FragmentConfigs)
	{
		Function(FragmentData.FragmentType);
	}
}

bool FMassArchetypeData::HasFragmentType(const UScriptStruct* FragmentType) const
{
	return (FragmentType && CompositionDescriptor.GetFragments().Contains(*FragmentType));
}

/**
 * Initialize：从 composition descriptor 产出 archetype 的完整内存布局。
 * 步骤：
 *  1. 记录 fragments 到 composition；
 *  2. ConfigureFragments 计算每 fragment 列的 offset/size、NumEntitiesPerChunk；
 *  3. 记录 tags；
 *  4. 生成 chunk fragment 模板数组（每 chunk 创建时拷贝一份）；
 *  5. 记录 shared/const-shared fragment 组合；
 *  6. EntityListOffsetWithinChunk = 0（身份列放在最前）。
 * 只能调一次；重复调用会 ensure 失败但不 crash。
 */
void FMassArchetypeData::Initialize(const FMassEntityManager& EntityManager, const FMassArchetypeCompositionDescriptor& InCompositionDescriptor, const uint32 ArchetypeDataVersion)
{
	if (!ensureMsgf(Chunks.Num() == 0, TEXT("Trying to re-initialize non-empty Mass Archetype is not supported")))
	{
		return;
	}
	if (!ensureMsgf(CreatedArchetypeDataVersion == 0, TEXT("MassArchetype has already been initialized")))
	{
		return;
	}

	CreatedArchetypeDataVersion = ArchetypeDataVersion;
	CompositionDescriptor.SetFragments(InCompositionDescriptor.GetFragments());
	ConfigureFragments(EntityManager);  // 关键：计算列布局

	// Tags
	CompositionDescriptor.SetTags(InCompositionDescriptor.GetTags());

	// Chunk fragments —— 每 chunk 一份的元数据结构
	CompositionDescriptor.SetChunkFragments(InCompositionDescriptor.GetChunkFragments());
	TArray<const UScriptStruct*, TInlineAllocator<16>> ChunkFragmentList;
	CompositionDescriptor.GetChunkFragments().ExportTypes(ChunkFragmentList);
	// 排序保证相同 composition 的 archetype 每次得到相同的 chunk fragment 顺序 —— 供后续 GetRequirementsChunkFragmentMapping 使用
	ChunkFragmentList.Sort(FScriptStructSortOperator());
	for (const UScriptStruct* ChunkFragmentType : ChunkFragmentList)
	{
		check(ChunkFragmentType);
		ChunkFragmentsTemplate.Emplace(ChunkFragmentType);
	}

	// Share fragments
	CompositionDescriptor.SetSharedFragments(InCompositionDescriptor.GetSharedFragments());
	CompositionDescriptor.SetConstSharedFragments(InCompositionDescriptor.GetConstSharedFragments());

	EntityListOffsetWithinChunk = 0;

#if WITH_MASSENTITY_DEBUG
	SetDebugColor(DebugColor);
#endif // WITH_MASSENTITY_DEBUG
}

/**
 * 基于 BaseArchetype 克隆出 fragment 布局相同、tag/shared 不同的新 archetype。
 * 优化点：若 fragment 集合完全一致，则直接"拷贝" BaseArchetype 的 FragmentConfigs/
 * FragmentIndexMap/TotalBytesPerEntity/NumEntitiesPerChunk，省去 ConfigureFragments 的计算。
 */
void FMassArchetypeData::InitializeWithSimilar(const FMassEntityManager& EntityManager, const FMassArchetypeData& BaseArchetype
	, FMassArchetypeCompositionDescriptor&& NewComposition, const UE::Mass::FArchetypeGroups& InGroups, const uint32 ArchetypeDataVersion)
{
	checkf(IsInitialized() == false, TEXT("Trying to InitializeWithSimilar but this archetype has already been initialized"));

	CreatedArchetypeDataVersion = ArchetypeDataVersion;

	// note that we're calling this function rarely, so we can be a little bit inefficient here.
	CompositionDescriptor = MoveTemp(NewComposition);
	if (CompositionDescriptor.GetFragments() != BaseArchetype.GetCompositionDescriptor().GetFragments())
	{
		// fragment 集合变了，重新计算列布局
		ConfigureFragments(EntityManager);
	}
	else
	{
		// fragment 集合完全一致：直接复用 base 的布局
		FragmentConfigs = BaseArchetype.FragmentConfigs;
		FragmentIndexMap = BaseArchetype.FragmentIndexMap;
		TotalBytesPerEntity = BaseArchetype.TotalBytesPerEntity;
		NumEntitiesPerChunk = BaseArchetype.NumEntitiesPerChunk;
	}
	ChunkFragmentsTemplate = BaseArchetype.ChunkFragmentsTemplate;

	Groups = InGroups;

	EntityListOffsetWithinChunk = 0;

#if WITH_MASSENTITY_DEBUG
	SetDebugColor(DebugColor);
#endif // WITH_MASSENTITY_DEBUG
}

/**
 * 【chunk 内存布局计算的核心】
 *
 * 从 fragment 位集合得出：
 *  - TotalBytesPerEntity: 一个实体占的字节数 = sizeof(EntityHandle) + Σ sizeof(Fragment)
 *  - NumEntitiesPerChunk: (ChunkSize - alignmentPadding) / TotalBytesPerEntity
 *  - FragmentConfigs[i].ArrayOffsetWithinChunk: 按对齐要求依次摆放每列的起点偏移
 *
 * SoA 列式布局示例（N = NumEntitiesPerChunk）：
 *
 *   Offset 0x0000:  EntityHandle[N]              ← 身份列（sizeof(FMassEntityHandle) 字节/实体）
 *   Offset 0x????:  FragmentConfigs[0] 列 [N]    ← 按 alignment 对齐后紧贴身份列
 *   Offset 0x????:  FragmentConfigs[1] 列 [N]    ← 按 alignment 对齐后紧贴上一列
 *   ...
 *   Offset 0x????:  (剩余 padding，浪费空间，DebugPrintArchetype 里会打印)
 *
 * AlignmentPadding 是保守上界（每列最多浪费 alignof(Fragment) 字节），所以实际可用空间
 * 比公式稍多，但简单稳妥。
 */
void FMassArchetypeData::ConfigureFragments(const FMassEntityManager& EntityManager)
{
	TArray<const UScriptStruct*, TInlineAllocator<16>> SortedFragmentList;
	CompositionDescriptor.GetFragments().ExportTypes(SortedFragmentList);

	// 按类型名排序，保证同一 composition 在任何进程都得到相同列顺序（跨次可复现）
	SortedFragmentList.Sort(FScriptStructSortOperator());

	// Figure out how many bytes all of the individual fragments (and metadata) will cost per entity
	SIZE_T FragmentSizeTallyBytes = 0;

	// Alignment padding computation is currently very conservative and over-estimated.
	// —— 保守估计：每列最多消耗 alignof(Fragment) 字节对齐填充
	SIZE_T AlignmentPadding = 0;
	
	// Save room for the 'metadata' (entity array)
	// —— 身份列放最前：每实体一个 FMassEntityHandle
	FragmentSizeTallyBytes += sizeof(FMassEntityHandle);

	// Tally up the fragment sizes and place them in the index map
	FragmentConfigs.AddDefaulted(SortedFragmentList.Num());
	FragmentIndexMap.Reserve(SortedFragmentList.Num());

	for (int32 FragmentIndex = 0; FragmentIndex < SortedFragmentList.Num(); ++FragmentIndex)
	{
		const UScriptStruct* FragmentType = SortedFragmentList[FragmentIndex];
		check(FragmentType);
		FragmentConfigs[FragmentIndex].FragmentType = FragmentType;

		// 累加 alignment（保守估计）与本 fragment 大小
		AlignmentPadding += SIZE_T(FragmentType->GetMinAlignment());
		FragmentSizeTallyBytes += SIZE_T(FragmentType->GetStructureSize());

		// 反向索引：UScriptStruct* → 列下标
		FragmentIndexMap.Add(FragmentType, FragmentIndex);
	}

	checkf(FragmentSizeTallyBytes < TNumericLimits<uint32>::Max(), TEXT("Single entity's size exceeds 2^32. This is not supported."));
	TotalBytesPerEntity = static_cast<uint32>(FragmentSizeTallyBytes);
	const SIZE_T ChunkAvailableSize = GetChunkAllocSize() - AlignmentPadding;
	checkf(TotalBytesPerEntity <= ChunkAvailableSize, TEXT("Single entity's size is larger than max chunk size - no entities will be created."));

	// 核心：一个 chunk 能装多少实体
	NumEntitiesPerChunk = static_cast<int32>(ChunkAvailableSize / TotalBytesPerEntity);

	// Set up the offsets for each fragment into the chunk data
	// —— 依次摆放每列，按 alignment 对齐后连续：[Entity × N][Frag0 × N][Frag1 × N]...
	int32 CurrentOffset = NumEntitiesPerChunk * sizeof(FMassEntityHandle);
	for (FMassArchetypeFragmentConfig& FragmentData : FragmentConfigs)
	{
		CurrentOffset = Align(CurrentOffset, FragmentData.FragmentType->GetMinAlignment());
		FragmentData.ArrayOffsetWithinChunk = CurrentOffset;
		const int32 SizeOfThisFragmentArray = NumEntitiesPerChunk * FragmentData.FragmentType->GetStructureSize();
		CurrentOffset += SizeOfThisFragmentArray;
	}
}

/**
 * 【AddEntity —— 单实体入 archetype】
 *
 * 两步：
 *  1. AddEntityInternal：找到/新建目标 chunk，占用一个槽位，登记 EntityMap 和身份列；
 *  2. 对该槽位的每个 fragment 调 InitializeStruct（零初始化 + 构造），保证 fragment 处于已构造状态。
 *
 * 注意：这里不会 ++EntityOrderVersion —— 因为新实体只 append 到末尾，不会改变现有实体顺序。
 */
void FMassArchetypeData::AddEntity(FMassEntityHandle Entity, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	const int32 AbsoluteIndex = AddEntityInternal(Entity, SharedFragmentValues);

	// Initialize fragments
	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;
	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
	// 对刚分配到的这个槽位上的每个 fragment 列调用 InitializeStruct 触发默认构造
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		void* FragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexWithinChunk);
		FragmentConfig.FragmentType->InitializeStruct(FragmentPtr);
	}
}

/**
 * AddEntityInternal：仅做"找槽位 + 登记"，不构造 fragment。
 * 前置校验：SharedFragmentValues 必须已排序、且类型集合与 archetype 的 shared 组合完全一致。
 * @return 该实体在 archetype 中的绝对索引（AbsoluteIndex = ChunkIndex*N + IndexWithinChunk）
 */
int32 FMassArchetypeData::AddEntityInternal(FMassEntityHandle Entity, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	checkf(SharedFragmentValues.IsSorted(), TEXT("Expecting shared fragment values to be previously sorted"));
	checkf(SharedFragmentValues.HasExactFragmentTypesMatch(CompositionDescriptor.GetSharedFragments(), CompositionDescriptor.GetConstSharedFragments())
		, TEXT("Expecting values for every specified shared fragment in the archetype and only those"))

	int32 IndexWithinChunk = 0;
	int32 AbsoluteIndex = 0;

	// 找到/新建匹配 SharedFragmentValues 的目标 chunk，并返回槽位坐标
	FMassArchetypeChunk& DestinationChunk = GetOrAddChunk(SharedFragmentValues, AbsoluteIndex, IndexWithinChunk);
	DestinationChunk.AddInstance();  // NumInstances++, SerialNumber++

	// Add to the table and map
	EntityMap.Add(Entity.Index, AbsoluteIndex);
	// 写入身份列：该槽位的 FMassEntityHandle
	DestinationChunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexWithinChunk) = Entity;

	return AbsoluteIndex;
}

/**
 * 【GetOrAddChunk —— chunk 选择策略】
 *
 * 选 chunk 的优先级：
 *  1. 遍历 Chunks，遇到"非满 + SharedValues 匹配"的 chunk 立刻用它（从前往后找，让后面的
 *     空 chunk 有机会被进一步释放）；
 *  2. 如果一路上看见过"空 chunk"（NumInstances==0 但对象还在），记下第一个候选，循环结束后
 *     若没找到匹配项就 Recycle 它；
 *  3. 否则 Emplace 新 chunk 追加到末尾。
 *
 * OutAbsoluteIndex 在循环中维护为"到当前 chunk 首槽位的累计偏移"，找到目标 chunk 后再加上
 * IndexWithinChunk 得到该槽的绝对索引。
 */
FMassArchetypeChunk& FMassArchetypeData::GetOrAddChunk(const FMassArchetypeSharedFragmentValues& SharedFragmentValues, int32& OutAbsoluteIndex, int32& OutIndexWithinChunk)
{
	OutAbsoluteIndex = 0;
	OutIndexWithinChunk = 0;

	int32 ChunkIndex = 0;
	int32 EmptyChunkIndex = INDEX_NONE;
	int32 EmptyAbsoluteIndex = INDEX_NONE;

	FMassArchetypeChunk* DestinationChunk = nullptr;
	// Check chunks for a free spot (trying to reuse the earlier ones first so later ones might get freed up) 
	//@TODO: This could be accelerated to include a cached index to the first chunk with free spots or similar
	// —— TODO：可缓存"第一个可用 chunk"的下标加速
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		if (Chunk.GetNumInstances() == 0)
		{
			// Remember first empty chunk but continue looking for a chunk that has space and same group tag
			// —— 空 chunk 留作 fallback；继续找匹配 SharedValues 的非空非满 chunk
			if (EmptyChunkIndex == INDEX_NONE)
			{
				EmptyChunkIndex = ChunkIndex;
				EmptyAbsoluteIndex = OutAbsoluteIndex;
			}
		}
		else if (Chunk.GetNumInstances() < NumEntitiesPerChunk && Chunk.GetSharedFragmentValues().IsEquivalent(SharedFragmentValues))
		{
			// 找到匹配且非满 → 槽位 = 当前 NumInstances
			OutIndexWithinChunk = Chunk.GetNumInstances();
			OutAbsoluteIndex += OutIndexWithinChunk;

			DestinationChunk = &Chunk;
			break;
		}
		// 跳到下一个 chunk：累计绝对偏移跨 NumEntitiesPerChunk 个槽位
		OutAbsoluteIndex += NumEntitiesPerChunk;
		++ChunkIndex;
	}

	if (DestinationChunk == nullptr)
	{
		// Check if it is a recycled chunk
		// —— 没找到匹配的非满 chunk，但有空 chunk → 回收它
		if (EmptyChunkIndex != INDEX_NONE)
		{
			DestinationChunk = &Chunks[EmptyChunkIndex];
			DestinationChunk->Recycle(ChunkFragmentsTemplate, SharedFragmentValues);
			OutAbsoluteIndex = EmptyAbsoluteIndex;
		}
		else
		{
			// 所有 chunk 都满了或不匹配，在末尾追加新 chunk
			DestinationChunk = &Chunks.Emplace_GetRef(GetChunkAllocSize(), ChunkFragmentsTemplate, SharedFragmentValues);
		}
	}

	check(DestinationChunk);
	return *DestinationChunk;
}

/**
 * 【RemoveEntity —— 单实体从 archetype 移除】
 *
 * 两步：
 *  1. 对该槽位的每个 fragment 调 DestroyStruct 触发析构；
 *  2. RemoveEntityInternal 做 swap-and-pop 补洞 + 折叠末尾空 chunk。
 */
void FMassArchetypeData::RemoveEntity(FMassEntityHandle Entity)
{
	const int32 AbsoluteIndex = EntityMap.FindAndRemoveChecked(Entity.Index);

	// Destroy fragments
	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;
	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		// Destroy the fragment data
		void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexWithinChunk);
		FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr);
	}

	RemoveEntityInternal(AbsoluteIndex);
}

/**
 * 【RemoveEntityInternal —— swap-and-pop 的经典实现】
 *
 * 关键算法：要删除 chunk 内第 IndexWithinChunk 个实体，怎么处理？
 *   如果逐个往前挪 —— O(N) 且破坏顺序；
 *   挪动 map 里其他实体的绝对索引 —— 代价同样 O(N)；
 *
 * Mass 的策略是：**把 chunk 末尾的实体整体 memcpy 覆盖到被删的槽位**，再把末尾 pop 掉。
 * 这样：
 *  - 数据搬动是 O(fragments)（每列一次 memcpy），与 chunk 内实体数无关；
 *  - 末尾实体的绝对索引从 (ChunkIdx, Last) → (ChunkIdx, IndexWithinChunk)，只改它一个的 EntityMap；
 *  - 代价：chunk 内实体顺序不再保证（但 Mass 本来就不依赖此顺序）。
 *
 * 同时 ++EntityOrderVersion，使外部持有的旧 FMassArchetypeEntityCollection / InChunkDataHandle
 * 能通过版本号发现自己已过期。
 *
 * 最后做"trailing empty chunk compaction"：从末尾连续弹出空 chunk 释放内存（绝不删中间的！
 * 否则后面 chunk 的绝对索引会错位）。
 */
void FMassArchetypeData::RemoveEntityInternal(const int32 AbsoluteIndex)
{
	++EntityOrderVersion;  // 让持有旧版本号的 collection/handle 失效

	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;

	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

	// chunk 最后一个实体槽位下标
	const int32 IndexToSwapFrom = Chunk.GetNumInstances() - 1;

	// Remove and swap the last entry in the chunk to the location of the removed item (if it's not the same as the dying entry)
	// —— 若删的就是末尾那个，则什么都不用 swap；否则把末尾实体的所有 fragment 列 + 身份位 memcpy 过来
	if (IndexToSwapFrom != IndexWithinChunk)
	{
		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexWithinChunk);
			void* MovingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexToSwapFrom);

			// Move last entry
			// —— 逐列 memcpy（每列一次），总 fragment 搬动次数 = FragmentConfigs.Num()
			FMemory::Memcpy(DyingFragmentPtr, MovingFragmentPtr, FragmentConfig.FragmentType->GetStructureSize());
		}

		// Update the entity table and map
		// —— 把身份列里末尾那条也搬过来，并在 EntityMap 改被挪动实体的绝对索引
		const FMassEntityHandle EntityBeingSwapped = Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexToSwapFrom);
		Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexWithinChunk) = EntityBeingSwapped;
		EntityMap.FindChecked(EntityBeingSwapped.Index) = AbsoluteIndex;  // 改反向索引为新槽位
	}
	
	Chunk.RemoveInstance();  // NumInstances--, SerialNumber++（chunk 空了还会释放 RawMemory）

	// If the chunk itself is empty now, see if we can remove it entirely
	// Note: This is only possible for trailing chunks, to avoid messing up the absolute indices in the entities map
	// —— 只折叠末尾连续的空 chunk，避免中间删除破坏 AbsoluteIndex 语义
	while ((Chunks.Num() > 0) && (Chunks.Last().GetNumInstances() == 0))
	{
		Chunks.RemoveAt(Chunks.Num() - 1, EAllowShrinking::No);
	}
}

/**
 * 【批量销毁 —— BatchDestroyEntityChunks】
 *
 * 按 range 批量移除实体。关键难点：一个 chunk 内可能有多个 range 要删，若从前往后处理，
 * 删前面的 range 会因 swap-and-pop 搬动末尾实体，使后面 range 记录的 SubchunkStart 错位。
 *
 * 解法：把 ranges 按 (ChunkIndex 升序, SubchunkStart **降序**) 排序后逐段处理。
 * 这样同一 chunk 内总是先删靠后的段，swap 拉进来的末尾实体位于被删段之前，不会污染。
 */
void FMassArchetypeData::BatchDestroyEntityChunks(FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRangeContainer, TArray<FMassEntityHandle>& OutEntitiesRemoved)
{
	const int32 InitialOutEntitiesCount = OutEntitiesRemoved.Num();

	// Sorting the subchunks info so that subchunks of a given chunk are processed "from the back". Otherwise removing 
	// a subchunk from the front of the chunk would inevitably invalidate following subchunks' information.
	// —— 见上方注释，同 chunk 内 SubchunkStart 降序，避免 swap 污染
	FMassArchetypeEntityCollection::FEntityRangeArray SortedRangeCollection(EntityRangeContainer);
	SortedRangeCollection.Sort([](const FMassArchetypeEntityCollection::FArchetypeEntityRange& A, const FMassArchetypeEntityCollection::FArchetypeEntityRange& B) 
		{ 
			return A.ChunkIndex < B.ChunkIndex || (A.ChunkIndex == B.ChunkIndex && A.SubchunkStart > B.SubchunkStart);
		});

	for (const FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange : SortedRangeCollection)
	{ 
		FMassArchetypeChunk& Chunk = Chunks[EntityRange.ChunkIndex];
		const int32 RangeLength = CalculateRangeLength(EntityRange, Chunk);

		// gather entities we're about to remove
		// —— 先把被删实体的 FMassEntityHandle 收集起来供调用方后续 cleanup（如通知、删 EntityMap）
		FMassEntityHandle* DyingEntityPtr = &Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, EntityRange.SubchunkStart);
		OutEntitiesRemoved.Append(DyingEntityPtr, RangeLength);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			// Destroy the fragment data
			// —— 对整段连续槽位调用一次 DestroyStruct(Ptr, count) 触发 count 次析构
			void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), EntityRange.SubchunkStart);
			FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr, RangeLength);
		}

		// 批量 swap-and-pop：把 chunk 末尾 RangeLength 个实体搬过来补洞
		BatchRemoveEntitiesInternal(EntityRange.ChunkIndex, EntityRange.SubchunkStart, RangeLength);
	}

	// 集中从 EntityMap 删除所有被消灭的实体
	for (int i = InitialOutEntitiesCount; i < OutEntitiesRemoved.Num(); ++i)
	{
		EntityMap.FindAndRemoveChecked(OutEntitiesRemoved[i].Index);
	}

	// If the chunk itself is empty now, see if we can remove it entirely
	// Note: This is only possible for trailing chunks, to avoid messing up the absolute indices in the entities map
	// —— 同 RemoveEntityInternal：只能弹末尾的空 chunk
	while ((Chunks.Num() > 0) && (Chunks.Last().GetNumInstances() == 0))
	{
		Chunks.RemoveAt(Chunks.Num() - 1, EAllowShrinking::No);
	}
}

// 仅查 composition；注意参数 EntityIndex 目前未使用（语义不随实体而变，仅看类型在不在 bitset）
bool FMassArchetypeData::HasFragmentDataForEntity(const UScriptStruct* FragmentType, int32 EntityIndex) const
{
	return (FragmentType && CompositionDescriptor.GetFragments().Contains(*FragmentType));
}

// Checked 版本：fragment 必须存在于 archetype；用于已知类型的热路径
void* FMassArchetypeData::GetFragmentDataForEntityChecked(const UScriptStruct* FragmentType, int32 EntityIndex) const
{
	const FMassRawEntityInChunkData InternalIndex = MakeEntityHandle(EntityIndex);
	
	// failing the below Find means given entity's archetype is missing given FragmentType
	const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);
	return GetFragmentData(FragmentIndex, InternalIndex);
}

// 找不到返回 nullptr 的版本，调用方需自行判空
void* FMassArchetypeData::GetFragmentDataForEntity(const UScriptStruct* FragmentType, int32 EntityIndex) const
{
	if (const int32* FragmentIndex = FragmentIndexMap.Find(FragmentType))
	{
		FMassRawEntityInChunkData InternalIndex = MakeEntityHandle(EntityIndex);
		// failing the below Find means given entity's archetype is missing given FragmentType
		return GetFragmentData(*FragmentIndex, InternalIndex);
	}
	return nullptr;
}

/**
 * 修改单实体的 shared fragment 值 —— 因为 shared values 决定实体归属哪个 chunk，所以需要"同档 archetype 内换 chunk"。
 * 步骤：
 *  1. 把旧 chunk 的 SharedValues 与 override 合并得到新的 NewSharedFragmentValues（再排序）；
 *  2. 在本 archetype 内找/新建一个 chunk 匹配 NewSharedFragmentValues；
 *  3. 在新 chunk 占用一个槽位；写 EntityMap 和身份列；
 *  4. MoveFragmentsToNewLocationInternal 把每列数据 memcpy 过去（同 archetype 所以所有 fragment 都要搬）；
 *  5. 原槽位做 swap-and-pop 补洞。
 */
void FMassArchetypeData::SetSharedFragmentsData(const FMassEntityHandle Entity, TConstArrayView<FSharedStruct> SharedFragmentValueOverrides)
{
	// Gets the current chunk where the entity is located
	const int32 OldAbsoluteIndex = EntityMap.FindChecked(Entity.Index);
	const int32 OldChunkIndex = OldAbsoluteIndex / NumEntitiesPerChunk;
	const int32 OldIndexWithinChunk = OldAbsoluteIndex % NumEntitiesPerChunk;
	const FMassArchetypeChunk& OldChunk = Chunks[OldChunkIndex];

	// Gets or adds a new chunk that will hold the new entity with the new shared values
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldChunk.GetSharedFragmentValues());
	NewSharedFragmentValues.ReplaceSharedFragments(SharedFragmentValueOverrides);
	NewSharedFragmentValues.Sort();  // 保持 shared values 的内部排序以便正确匹配 chunk

	int32 NewAbsoluteIndex = 0;
	int32 NewIndexWithinChunk = 0;
	FMassArchetypeChunk& NewChunk = GetOrAddChunk(NewSharedFragmentValues, NewAbsoluteIndex, NewIndexWithinChunk);

	if (ensureMsgf(&NewChunk != &OldChunk, TEXT("Found target chunk is the same as the source chunk. Probably "
		"caused by setting shared fragment values resulted in no change, meaning the target values equal the source values")))
	{
		NewChunk.AddInstance();

		// Update the new entity in the table and map
		EntityMap[Entity.Index] = NewAbsoluteIndex;
		NewChunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, NewIndexWithinChunk) = Entity;
		
		// Move the current entity fragments into the new chunk
		// —— 同 archetype 内的迁移，所有 fragment 列都要按本 archetype 布局 memcpy 过去
		MoveFragmentsToNewLocationInternal({ OldChunk.GetRawMemory(), OldIndexWithinChunk }, { NewChunk.GetRawMemory(), NewIndexWithinChunk }, 1);

		// Clean up the old chunk
		RemoveEntityInternal(OldAbsoluteIndex);
	}
}

// 单实体多 fragment 写入：每个 FInstancedStruct 必须是 archetype 拥有的 fragment 类型
void FMassArchetypeData::SetFragmentsData(const FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstances)
{
	FMassRawEntityInChunkData InternalIndex = MakeEntityHandle(Entity);

	for (const FInstancedStruct& Instance : FragmentInstances)
	{
		const UScriptStruct* FragmentType = Instance.GetScriptStruct();
		check(FragmentType);
		const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);
		void* FragmentMemory = GetFragmentData(FragmentIndex, InternalIndex);
		// CopyScriptStruct 处理含 UObject 引用、pointer 等需要正确赋值的 struct（不仅仅是 memcpy）
		FragmentType->CopyScriptStruct(FragmentMemory, Instance.GetMemory());
	}
}

/**
 * 把指定 ranges 覆盖的所有实体的 FragmentSource 类型 fragment 全部覆写为 FragmentSource 的值。
 * 内层循环按 fragment 大小步进 —— 这是常规 SoA 列遍历模式。
 * 若 archetype 不含此 fragment 类型，仅打 warning 不报错（业务可能有意从父类调用）。
 */
void FMassArchetypeData::SetFragmentData(FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRangeContainer, const FInstancedStruct& FragmentSource)
{
	check(FragmentSource.IsValid());
	const UScriptStruct* FragmentType = FragmentSource.GetScriptStruct();
	check(FragmentType);
	const int32* FragmentIndex = FragmentIndexMap.Find(FragmentType);
	if (LIKELY(FragmentIndex))
	{
		const int32 FragmentTypeSize = FragmentType->GetStructureSize();
		const uint8* FragmentSourceMemory = FragmentSource.GetMemory();
		check(FragmentSourceMemory);
	
		for (FMassArchetypeChunkIterator ChunkIterator(EntityRangeContainer); ChunkIterator; ++ChunkIterator)
		{
			uint8* FragmentMemory = (uint8*)FragmentConfigs[*FragmentIndex].GetFragmentData(Chunks[ChunkIterator->ChunkIndex].GetRawMemory(), ChunkIterator->SubchunkStart);
			for (int i = ChunkIterator->Length; i; --i, FragmentMemory += FragmentTypeSize)
			{
				FragmentType->CopyScriptStruct(FragmentMemory, FragmentSourceMemory);
			}
		}
	}
	else
	{
		UE_LOG(LogMass, Warning
			, TEXT("Attempting to set value of fragment of type %s, while it's not part of the archetype's composition")
			, *FragmentType->GetName());
	}
}

/**
 * 【MoveEntityToAnotherArchetype —— 跨 archetype 迁移】
 *
 * 当实体加/减一个 tag 或 fragment 时，它的 archetype 就变了（因为 composition 是 archetype 的身份）。
 * 整个流程：
 *  1. 从本 archetype 的 EntityMap 移除本实体（记录旧坐标）；
 *  2. 在新 archetype 上调 AddEntityInternal 占据一个槽位 —— 注意此时 shared values 用旧的 chunk 值
 *     或 Override（允许同时改 shared）；
 *  3. MoveFragmentsToAnotherArchetypeInternal 处理 fragment 迁移：
 *     - 两 archetype 共有的 fragment：memcpy 原值（不走构造/析构）；
 *     - 新 archetype 独有：InitializeStruct 默认构造；
 *     - 旧 archetype 独有：DestroyStruct 析构；
 *  4. 旧 archetype 做 swap-and-pop（RemoveEntityInternal）回收槽位。
 *
 * 关键点：共有 fragment 的数据**不经过**构造函数/析构函数 —— 原位 bytes 直接挪过去，性能高。
 * 这依赖 fragment 类型是 POD-like（trivially copyable），Mass 的 FMassFragment 设计隐含此约束。
 */
void FMassArchetypeData::MoveEntityToAnotherArchetype(const FMassEntityHandle Entity, FMassArchetypeData& NewArchetype, const FMassArchetypeSharedFragmentValues* SharedFragmentValuesOverride)
{
	check(&NewArchetype != this);

	const int32 AbsoluteIndex = EntityMap.FindAndRemoveChecked(Entity.Index);
	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;
	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

	// 在目标 archetype 分配槽位；shared values 优先用 override，否则沿用原 chunk 的
	const int32 NewAbsoluteIndex = NewArchetype.AddEntityInternal(Entity, SharedFragmentValuesOverride ? *SharedFragmentValuesOverride : Chunk.GetSharedFragmentValues());
	const int32 NewChunkIndex = NewAbsoluteIndex / NewArchetype.NumEntitiesPerChunk;
	const int32 NewIndexWithinChunk = NewAbsoluteIndex % NewArchetype.NumEntitiesPerChunk;
	FMassArchetypeChunk& NewChunk = NewArchetype.Chunks[NewChunkIndex];

	UE_TRACE_MASS_ARCHETYPE_CREATED(NewArchetype)
	UE_TRACE_MASS_ENTITY_MOVED(Entity, NewArchetype)

	// 搬 fragment：共有的 memcpy，新独有的 InitializeStruct，旧独有的 DestroyStruct
	MoveFragmentsToAnotherArchetypeInternal(NewArchetype, { NewChunk.GetRawMemory(), NewIndexWithinChunk }, { Chunk.GetRawMemory(), IndexWithinChunk }, /*Count=*/1);

	// 旧槽位 swap-and-pop
	RemoveEntityInternal(AbsoluteIndex);
}

/**
 * 【ExecuteFunction (按给定 ranges) —— processor 热路径之一】
 *
 * 流程：为每个 range 绑定所有需要的 view 到 RunContext，再调用 Function(RunContext)：
 *  1. SetCurrentArchetypeCompositionDescriptor：供 processor 做条件判断；
 *  2. 遍历 ranges（FMassArchetypeChunkIterator）；
 *  3. 对每个 chunk 若 SharedValues 与上次不同，重新绑 const/mutable shared fragment views；
 *     —— 这里用 hash 比较而不是指针/内容比较，因为同一 archetype 内 shared values 的种类固定，hash 相同即可判等；
 *  4. 设置 chunk 的 SerialModificationNumber 到 RunContext（供后续 handle 校验）；
 *  5. 绑定 chunk fragment views；
 *  6. 跑 ChunkCondition，false 则跳过；
 *  7. 绑定 entity fragment views（TArrayView<T>，指向本 range 的 fragment 列），调 Function(RunContext)。
 */
void FMassArchetypeData::ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, FMassArchetypeEntityCollection::FConstEntityRangeArrayView EntityRangeContainer, const FMassChunkConditionFunction& ChunkCondition)
{
	if (GetNumEntities() == 0)
	{
		return;
	}

	// @todo do we really want users to check composition of the archetype being processed at the moment?
	RunContext.SetCurrentArchetypeCompositionDescriptor(GetCompositionDescriptor());
#if WITH_MASSENTITY_DEBUG
	RunContext.DebugSetColor(DebugColor);
#endif // WITH_MASSENTITY_DEBUG

	// 用 hash 缓存避免重复 rebind shared fragments（相邻 chunk 通常 SharedValues 相同）
	uint32 PrevSharedFragmentValuesHash = TNumericLimits<uint32>::Max();
	for (FMassArchetypeChunkIterator ChunkIterator(EntityRangeContainer); ChunkIterator; ++ChunkIterator)
	{
		FMassArchetypeChunk& Chunk = Chunks[ChunkIterator->ChunkIndex];

		// Length <=0 时展开成"到 chunk 末尾"
		const int32 SubchunkLength = ChunkIterator->Length > 0 ? ChunkIterator->Length : (Chunk.GetNumInstances() - ChunkIterator->SubchunkStart);
		if (SubchunkLength)
		{
			const uint32 SharedFragmentValuesHash = GetTypeHash(Chunk.GetSharedFragmentValues());
			if (PrevSharedFragmentValuesHash != SharedFragmentValuesHash)
			{
				PrevSharedFragmentValuesHash = SharedFragmentValuesHash;
				BindConstSharedFragmentRequirements(RunContext, Chunk.GetSharedFragmentValues(), RequirementMapping.ConstSharedFragments);
				BindSharedFragmentRequirements(RunContext, Chunk.GetMutableSharedFragmentValues(), RequirementMapping.SharedFragments);
			}

			checkf((ChunkIterator->SubchunkStart + SubchunkLength) <= Chunk.GetNumInstances() && SubchunkLength > 0, TEXT("Invalid subchunk, it is going over the number of instances in the chunk or it is empty."));

			RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
			BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);

			if (!ChunkCondition || ChunkCondition(RunContext))
			{
				// 绑 entity fragment views：让 processor 可以 Context.GetFragmentView<T>() 得到 TArrayView<T>
				BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk, ChunkIterator->SubchunkStart, SubchunkLength);
				Function(RunContext);
			}
		}
	}
}

/**
 * 【ExecuteFunction (遍历 archetype 全部 chunk) —— processor 热路径之二】
 *
 * 与上一个版本的区别：没有给定 ranges，而是直接扫 Chunks 数组所有 chunk。
 * 支持 ExecutionLimiter，可在"时间片内处理若干 chunk"的场景下分帧跨帧续跑。
 *
 * 外层 for (ChunkIndex;...) 按 chunk 处理：
 *  - 跳过空 chunk；
 *  - 相邻 chunk 同 SharedValues 则跳过 rebind；
 *  - ChunkCondition 为 false 则 chunk 跳过但 ExecutionLimiter.EntityCountRemaining 不扣；
 *  - ExecutionLimiter 更新当前进度以便下次续跑。
 */
void FMassArchetypeData::ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FMassChunkConditionFunction& ChunkCondition, UE::Mass::FExecutionLimiter* ExecutionLimiter)
{
	if (GetNumEntities() == 0)
	{
		return;
	}

	RunContext.SetCurrentArchetypeCompositionDescriptor(GetCompositionDescriptor());
#if WITH_MASSENTITY_DEBUG
	RunContext.DebugSetColor(DebugColor);
#endif // WITH_MASSENTITY_DEBUG

	int32 ChunkIndex = 0;
	int32 EntityCountRemaining = TNumericLimits<int32>::Max();
	int32 MaxChunkIndex = Chunks.Num();

	if (ExecutionLimiter)
	{
		ChunkIndex = FMath::Max(ExecutionLimiter->ChunkIndex, 0);
		EntityCountRemaining = ExecutionLimiter->EntityCountRemaining;
		MaxChunkIndex = FMath::Min(MaxChunkIndex, ExecutionLimiter->MaxChunkIndex);
	}

	uint32 PrevSharedFragmentValuesHash = TNumericLimits<uint32>::Max();
	for (; ChunkIndex < MaxChunkIndex && EntityCountRemaining > 0; ChunkIndex++)
	{
		FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
		const int32 EntityCount = Chunk.GetNumInstances();

		if (EntityCount)
		{
			const uint32 SharedFragmentValuesHash = GetTypeHash(Chunk.GetSharedFragmentValues());
			if (PrevSharedFragmentValuesHash != SharedFragmentValuesHash)
			{
				PrevSharedFragmentValuesHash = SharedFragmentValuesHash;
				BindConstSharedFragmentRequirements(RunContext, Chunk.GetSharedFragmentValues(), RequirementMapping.ConstSharedFragments);
				BindSharedFragmentRequirements(RunContext, Chunk.GetMutableSharedFragmentValues(), RequirementMapping.SharedFragments);
			}

			RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
			BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);

			if (!ChunkCondition || ChunkCondition(RunContext))
			{
				BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk, 0, Chunk.GetNumInstances());
				Function(RunContext);

				EntityCountRemaining -= EntityCount;
			}
		}
	}

	if (ExecutionLimiter)
	{
		// set the limiter to continue on the next chunk if this archetype has unprocessed chunks
		// —— 记住处理到哪一 chunk，让下次调用从这里继续
		ExecutionLimiter->ChunkIndex = ++ChunkIndex;
		ExecutionLimiter->EntityCountRemaining = EntityCountRemaining;
	}
}

/**
 * 单 range 版的 execute：跑一个 chunk 内的一段。通常由内部按 chunk 调度。
 */
void FMassArchetypeData::ExecutionFunctionForChunk(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange, const FMassChunkConditionFunction& ChunkCondition)
{
	FMassArchetypeChunk& Chunk = Chunks[EntityRange.ChunkIndex];
	const int32 RangeLength = CalculateRangeLength(EntityRange, Chunk);

	if (RangeLength > 0)
	{
		BindConstSharedFragmentRequirements(RunContext, Chunk.GetSharedFragmentValues(), RequirementMapping.ConstSharedFragments);
		BindSharedFragmentRequirements(RunContext, Chunk.GetMutableSharedFragmentValues(), RequirementMapping.SharedFragments);

		RunContext.SetCurrentArchetypeCompositionDescriptor(GetCompositionDescriptor());
		RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
#if WITH_MASSENTITY_DEBUG
		RunContext.DebugSetColor(DebugColor);
#endif // WITH_MASSENTITY_DEBUG

		BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);

		if (!ChunkCondition || ChunkCondition(RunContext))
		{
			BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk, EntityRange.SubchunkStart, RangeLength);
			Function(RunContext);
		}
	}
}

/**
 * 【CompactEntities —— 稀疏 chunk 压缩算法】
 *
 * 目的：随着实体不断增删，同一 SharedValues 的多个 chunk 可能都变得半满，浪费内存。
 * 压缩策略（time-boxed）：
 *  1. 按 SharedFragmentValues 的 hash 将非满、非空的 chunk 分组（因为只有同组的 chunk 可以互相迁移实体）；
 *  2. 每组 chunks 按 NumInstances 升序排序：前面的"最空"，后面的"最满"；
 *  3. 双指针夹逼：从"最空"取一个 ChunkToFill（指针 ChunkToFillSortedIdx 前移），从"最满"取一个
 *     ChunkToEmpty（指针 ChunkToEmptySortedIdx 后移）。注意"最空"是相对于 NumInstances 小的，
 *     其实是作为"受填目标"而非"被清空方"；
 *     —— 译者注：这里命名有点反直觉，ChunkToFill 才是被填充的 chunk；算法本意是优先让
 *        较满 chunk 更满 / 较空 chunk 清空，Mass 实际的选择是"当前最小 NumInstances 的 chunk
 *        作为填充目标"，把末尾实体搬来尽量填满它；
 *  4. 每次从 ChunkToEmpty 末尾搬 NumberOfEntitiesToMove 个实体到 ChunkToFill 末尾；
 *  5. 更新 EntityMap 里被挪动实体的 AbsoluteIndex；
 *  6. 每轮检查是否超 TimeAllowed，是则中断。
 *
 * 若实际挪动了实体，++EntityOrderVersion，通知旧 collection/handle 失效。
 */
int32 FMassArchetypeData::CompactEntities(const double TimeAllowed)
{
	int32 TotalEntitiesMoved = 0;
	const double TimeAllowedEnd = FPlatformTime::Seconds() + TimeAllowed;

	// 按 SharedValues hash 分组 —— 只有同 SharedValues 的 chunk 之间可以互搬实体
	TMap<uint32, TArray<FMassArchetypeChunk*>> SortedChunksBySharedValues;
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		// Skip already full chunks
		const int32 NumInstances = Chunk.GetNumInstances();
		if (NumInstances > 0 && NumInstances < NumEntitiesPerChunk)
		{
			const uint32 SharedFragmentHash = GetTypeHash(Chunk.GetSharedFragmentValues());
			TArray<FMassArchetypeChunk*>& SortedChunks = SortedChunksBySharedValues.FindOrAddByHash(SharedFragmentHash, SharedFragmentHash, TArray<FMassArchetypeChunk*>());
			SortedChunks.Add(&Chunk);
		}
	}

	for (TPair<uint32, TArray<FMassArchetypeChunk*>>& Pair : SortedChunksBySharedValues)
	{
		TArray<FMassArchetypeChunk*>& SortedChunks = Pair.Value;

		// Check if there is anything to compact at all
		if (SortedChunks.Num() <= 1)
		{
			continue;
		}

		// 升序：ChunkToFillSortedIdx 从"最空"开始取（作为被填充目标）；
		// ChunkToEmptySortedIdx 从"最满"开始取（作为被搬出源）
		SortedChunks.Sort([](const FMassArchetypeChunk& LHS, const FMassArchetypeChunk& RHS)
		{
			return LHS.GetNumInstances() < RHS.GetNumInstances();
		});

		int32 ChunkToFillSortedIdx = 0;
		int32 ChunkToEmptySortedIdx = SortedChunks.Num() - 1;
		while (ChunkToFillSortedIdx < ChunkToEmptySortedIdx && FPlatformTime::Seconds() < TimeAllowedEnd)
		{
			// 跳过已满的填充目标（NumInstances 已到上限）
			while (ChunkToFillSortedIdx < SortedChunks.Num() && SortedChunks[ChunkToFillSortedIdx]->GetNumInstances() == NumEntitiesPerChunk)
			{
				ChunkToFillSortedIdx++;
			}
			// 跳过已空的搬出源
			while (ChunkToEmptySortedIdx >= 0 && SortedChunks[ChunkToEmptySortedIdx]->GetNumInstances() == 0)
			{
				ChunkToEmptySortedIdx--;
			}
			if (ChunkToFillSortedIdx >= ChunkToEmptySortedIdx)
			{
				break;
			}

			FMassArchetypeChunk* ChunkToFill = SortedChunks[ChunkToFillSortedIdx];
			FMassArchetypeChunk* ChunkToEmpty = SortedChunks[ChunkToEmptySortedIdx];
			// 最多搬多少：受目标剩余空间 & 源现有实体数限制
			const int32 NumberOfEntitiesToMove = FMath::Min(NumEntitiesPerChunk - ChunkToFill->GetNumInstances(), ChunkToEmpty->GetNumInstances());
			const int32 FromIndex = ChunkToEmpty->GetNumInstances() - NumberOfEntitiesToMove;  // 从源尾部取
			const int32 ToIndex = ChunkToFill->GetNumInstances();                              // 追加到目标尾部
			check(NumberOfEntitiesToMove > 0);

			// 所有 fragment 列 memcpy；同 archetype，整列逐行
			MoveFragmentsToNewLocationInternal({ChunkToFill->GetRawMemory(), ToIndex}, {ChunkToEmpty->GetRawMemory(), FromIndex}
				, NumberOfEntitiesToMove);

			// 身份列 memcpy
			FMassEntityHandle* FromEntity = &ChunkToEmpty->GetEntityArrayElementRef(EntityListOffsetWithinChunk, FromIndex);
			FMassEntityHandle* ToEntity = &ChunkToFill->GetEntityArrayElementRef(EntityListOffsetWithinChunk, ToIndex);
			FMemory::Memcpy(ToEntity, FromEntity, NumberOfEntitiesToMove * sizeof(FMassEntityHandle));
			ChunkToFill->AddMultipleInstances(NumberOfEntitiesToMove);
			ChunkToEmpty->RemoveMultipleInstances(NumberOfEntitiesToMove);

			// 用 ChunkToFill 在 Chunks 数组中的下标计算新 AbsoluteIndex，刷新 EntityMap
			const int32 ChunkToFillIdx = UE_PTRDIFF_TO_INT32(ChunkToFill - &Chunks[0]);
			check(ChunkToFillIdx >= 0 && ChunkToFillIdx < Chunks.Num());
			const int32 AbsoluteIndex = ChunkToFillIdx * NumEntitiesPerChunk + ToIndex;

			for (int32 i = 0; i < NumberOfEntitiesToMove; i++, ++ToEntity)
			{
				EntityMap.FindChecked(ToEntity->Index) = AbsoluteIndex + i;
			}

			TotalEntitiesMoved += NumberOfEntitiesToMove;
		}
	}

	if (TotalEntitiesMoved > 0)
	{
		// 仅在真的挪动了实体时才失效旧版本 —— 避免空操作冲掉 collection 缓存
		++EntityOrderVersion;
	}

	return TotalEntitiesMoved;
}

/**
 * 把 Requirements 里每条"RequiresBinding"的需求的 fragment 类型查到 archetype 内的列下标，
 * optional 缺失填 INDEX_NONE。结果交给 FMassQueryRequirementIndicesMapping 缓存，
 * processor 执行时无需再查 map。
 */
void FMassArchetypeData::GetRequirementsFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices) const
{
	OutFragmentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirementDescription& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32* FragmentIndex = FragmentIndexMap.Find(Requirement.StructType);
			check(FragmentIndex != nullptr || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex ? *FragmentIndex : INDEX_NONE);
		}
	}
}

// @todo make ChunkRequirements a dedicated type, so that we can ensure that the contents are sorted as expected by the for loop below
void FMassArchetypeData::GetRequirementsChunkFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> ChunkRequirements, FMassFragmentIndicesMapping& OutFragmentIndices) const
{
	int32 LastFoundFragmentIndex = -1;
	OutFragmentIndices.Reset(ChunkRequirements.Num());
	for (const FMassFragmentRequirementDescription& Requirement : ChunkRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			int32 FragmentIndex = INDEX_NONE;
			for (int32 i = LastFoundFragmentIndex + 1; i < ChunkFragmentsTemplate.Num(); ++i)
			{
				if (ChunkFragmentsTemplate[i].GetScriptStruct()->IsChildOf(Requirement.StructType))
				{
					FragmentIndex = i;
					break;
				}
			}

			check(FragmentIndex != INDEX_NONE || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex);
			LastFoundFragmentIndex = FragmentIndex;
		}
	}
}

void FMassArchetypeData::GetRequirementsConstSharedFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices) const
{
	if (Chunks.Num() == 0)
	{
		return;
	}
	// All shared fragment values for this archetype should have deterministic indices, so anyone will work to calculate them
	const FMassArchetypeSharedFragmentValues& SharedFragmentValues = Chunks[0].GetSharedFragmentValues();

	OutFragmentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirementDescription& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32 FragmentIndex = SharedFragmentValues.GetConstSharedFragments().IndexOfByPredicate(FStructTypeEqualOperator(Requirement.StructType));
			check(FragmentIndex != INDEX_NONE || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex);
		}
	}
}

void FMassArchetypeData::GetRequirementsSharedFragmentMapping(TConstArrayView<FMassFragmentRequirementDescription> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices) const
{
	if (Chunks.Num() == 0)
	{
		return;
	}

	// All shared fragment values for this archetype should have deterministic indices, so anyone will work to calculate them
	const FMassArchetypeSharedFragmentValues& SharedFragmentValues = Chunks[0].GetSharedFragmentValues();

	OutFragmentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirementDescription& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32 FragmentIndex = SharedFragmentValues.GetSharedFragments().IndexOfByPredicate(FStructTypeEqualOperator(Requirement.StructType));
			check(FragmentIndex != INDEX_NONE || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex);
		}
	}
}

/**
 * 【BindEntityRequirements —— processor 访问 fragment 的关键桥接】
 *
 * 输入：
 *  - RunContext.GetMutableRequirements()：processor 声明的 requirement 列表（每个包含类型和
 *    一个待填充的 FragmentView 字段）；
 *  - EntityFragmentsMapping：预先计算好的"第 i 条 requirement → archetype fragment 列下标"；
 *  - Chunk：目标 chunk；
 *  - SubchunkStart/SubchunkLength：本 range 在 chunk 内的起止。
 *
 * 做什么：为每条 requirement 填充 Requirement.FragmentView = TArrayView<FMassFragment>(ptr, N)
 * 其中 ptr 指向 chunk 的 fragment 列 [SubchunkStart..SubchunkStart+N) 区间的起点。
 * processor 拿到 TArrayView<T> 后内层循环就是纯 SoA tight loop：
 *
 *     auto Velocities = Context.GetFragmentView<FVelocity>();
 *     for (int32 i = 0; i < Velocities.Num(); ++i) {
 *         Velocities[i].X += dt * ...;
 *     }
 *
 * 这里的 Velocities 指针就是本 chunk 里 Velocity 列的起点加上 SubchunkStart 偏移。
 *
 * 两条分支：
 *  - 有预计算 mapping → 快路径：直接按下标取 FragmentConfigs[idx] 计算地址，无 map 查找；
 *  - 没 mapping → 慢路径：逐 requirement 现查 FragmentIndexMap。
 *
 * 最后还把 EntityListView 设好，processor 里 Context.GetEntity(i) 就能拿到 FMassEntityHandle。
 */
void FMassArchetypeData::BindEntityRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& EntityFragmentsMapping, FMassArchetypeChunk& Chunk, const int32 SubchunkStart, const int32 SubchunkLength)
{
	// auto-correcting number of entities to process in case SubchunkStart +  SubchunkLength > Chunk.GetNumInstances()
	// —— 处理 Length<=0 或越界的情况，夹到 chunk 当前实际实体数
	const int32 NumEntities = SubchunkLength >= 0 ? FMath::Min(SubchunkLength, Chunk.GetNumInstances() - SubchunkStart) : Chunk.GetNumInstances();
	check(SubchunkStart >= 0 && SubchunkStart < Chunk.GetNumInstances());

	if (EntityFragmentsMapping.Num() > 0)
	{
		// 快路径：用预计算的列下标数组
		check(RunContext.GetMutableRequirements().Num() == EntityFragmentsMapping.Num());

		for (int i = 0; i < EntityFragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FFragmentView& Requirement = RunContext.FragmentViews[i];
			const int32 FragmentIndex = EntityFragmentsMapping[i];

			check(FragmentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			if (FragmentIndex != INDEX_NONE)
			{
				// 核心：从 chunk 的 FragmentType 列计算地址，构造一个 NumEntities 长度的 TArrayView
				Requirement.FragmentView = TArrayView<FMassFragment>((FMassFragment*)GetFragmentData(FragmentIndex, Chunk.GetRawMemory(), SubchunkStart), NumEntities);
			}
			else
			{
				// @todo this might not be needed
				// optional 需求但 archetype 没这列 → 空 view
				Requirement.FragmentView = TArrayView<FMassFragment>();
			}
		}
	}
	else
	{
		// Map in the required data arrays from the current chunk to the array views
		// —— 慢路径：没预计算，逐 requirement 现查。Archetype 第一次被 query 匹配时走这里
		for (FMassExecutionContext::FFragmentView& Requirement : RunContext.GetMutableRequirements())
		{
			const int32* FragmentIndex = FragmentIndexMap.Find(Requirement.Requirement.StructType);
			check(FragmentIndex != nullptr || Requirement.Requirement.IsOptional());
			if (FragmentIndex)
			{
				Requirement.FragmentView = TArrayView<FMassFragment>((FMassFragment*)GetFragmentData(*FragmentIndex, Chunk.GetRawMemory(), SubchunkStart), NumEntities);
			}
			else
			{
				Requirement.FragmentView = TArrayView<FMassFragment>();
			}
		}
	}

	// 身份列视图：processor 可用 Context.GetEntity(i) / Context.GetEntities() 访问
	RunContext.EntityListView = TArrayView<FMassEntityHandle>(&Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, SubchunkStart), NumEntities);
}

// Chunk fragment 绑定：chunk fragment 是每 chunk 一份的元数据（非每实体），返回 FStructView
void FMassArchetypeData::BindChunkFragmentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& ChunkFragmentsMapping, FMassArchetypeChunk& Chunk)
{
	if (ChunkFragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableChunkRequirements().Num() == ChunkFragmentsMapping.Num());

		for (int i = 0; i < ChunkFragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FChunkFragmentView& ChunkRequirement = RunContext.ChunkFragmentViews[i];
			const int32 ChunkFragmentIndex = ChunkFragmentsMapping[i];

			check(ChunkFragmentIndex != INDEX_NONE || ChunkRequirement.Requirement.IsOptional());
			ChunkRequirement.FragmentView = ChunkFragmentIndex != INDEX_NONE ? Chunk.GetMutableChunkFragmentViewChecked(ChunkFragmentIndex) : FStructView();
		}
	}
	else
	{
		for (FMassExecutionContext::FChunkFragmentView& ChunkRequirement : RunContext.GetMutableChunkRequirements())
		{
			FInstancedStruct* ChunkFragmentInstance = Chunk.FindMutableChunkFragment(ChunkRequirement.Requirement.StructType);
			check(ChunkFragmentInstance != nullptr || ChunkRequirement.Requirement.IsOptional());
			ChunkRequirement.FragmentView = ChunkFragmentInstance ? FStructView(*ChunkFragmentInstance) : FStructView();
		}
	}
}

void FMassArchetypeData::BindConstSharedFragmentRequirements(FMassExecutionContext& RunContext, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassFragmentIndicesMapping& FragmentsMapping)
{
	if (FragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableConstSharedRequirements().Num() == FragmentsMapping.Num());

		for (int i = 0; i < FragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FConstSharedFragmentView& Requirement = RunContext.ConstSharedFragmentViews[i];
			const int32 FragmentIndex = FragmentsMapping[i];

			check(FragmentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = FragmentIndex != INDEX_NONE ? FConstStructView(SharedFragmentValues.GetConstSharedFragments()[FragmentIndex]) : FConstStructView();
		}
	}
	else
	{
		for (FMassExecutionContext::FConstSharedFragmentView& Requirement : RunContext.GetMutableConstSharedRequirements())
		{
			const FConstSharedStruct* SharedFragment = SharedFragmentValues.GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(Requirement.Requirement.StructType) );
			check(SharedFragment != nullptr || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = SharedFragment ? FConstStructView(*SharedFragment) : FConstStructView();
		}
	}
}

void FMassArchetypeData::BindSharedFragmentRequirements(FMassExecutionContext& RunContext, FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassFragmentIndicesMapping& FragmentsMapping)
{
	if (FragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableSharedRequirements().Num() == FragmentsMapping.Num());

		for (int i = 0; i < FragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FSharedFragmentView& Requirement = RunContext.SharedFragmentViews[i];
			const int32 FragmentIndex = FragmentsMapping[i];

			check(FragmentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = FragmentIndex != INDEX_NONE ? FStructView(SharedFragmentValues.GetMutableSharedFragments()[FragmentIndex]) : FStructView();
		}
	}
	else
	{
		for (FMassExecutionContext::FSharedFragmentView& Requirement : RunContext.GetMutableSharedRequirements())
		{
			FSharedStruct* SharedFragment = SharedFragmentValues.GetMutableSharedFragments().FindByPredicate(FStructTypeEqualOperator(Requirement.Requirement.StructType));
			check(SharedFragment != nullptr || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = SharedFragment ? FStructView(*SharedFragment) : FStructView();
		}
	}
}

int32 FMassArchetypeData::GetNonEmptyChunkCount() const
{
	int32 NumAllocatedChunks = 0;
	for (const FMassArchetypeChunk& Chunk : Chunks)
	{
		if (Chunk.GetRawMemory() != nullptr)
		{
			++NumAllocatedChunks;
		}
	}
	return NumAllocatedChunks;
}

SIZE_T FMassArchetypeData::GetAllocatedSize() const
{
	const int32 NumAllocatedChunkBuffers = GetNonEmptyChunkCount();

	return sizeof(FMassArchetypeData) +
		ChunkFragmentsTemplate.GetAllocatedSize() +
		FragmentConfigs.GetAllocatedSize() +
		Chunks.GetAllocatedSize() +
		(NumAllocatedChunkBuffers * GetChunkAllocSize()) +
		EntityMap.GetAllocatedSize() +
		FragmentIndexMap.GetAllocatedSize();
}

void FMassArchetypeData::ExportEntityHandles(const TConstArrayView<FMassArchetypeEntityCollection::FArchetypeEntityRange> Ranges, TArray<FMassEntityHandle>& InOutHandles) const
{
	int32 TotalEntities = 0;
	for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Range : Ranges)
	{
		check(Chunks.IsValidIndex(Range.ChunkIndex));
		TotalEntities += (Range.Length > 0) ? Range.Length : (Chunks[Range.ChunkIndex].GetNumInstances() - Range.SubchunkStart);
	}

	int32 StartIndex = InOutHandles.Num();
	InOutHandles.AddUninitialized(TotalEntities);

	for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Range : Ranges)
	{
		const FMassArchetypeChunk& Chunk = Chunks[Range.ChunkIndex];
		const FMassEntityHandle* EntitiesArray = Chunk.GetEntityArray(EntityListOffsetWithinChunk);
		const int32 RangeLength = CalculateRangeLength(Range, Chunk);
		FMemory::Memcpy(&InOutHandles[StartIndex], &EntitiesArray[Range.SubchunkStart], RangeLength * sizeof(FMassEntityHandle));

		StartIndex += RangeLength;
	}
}

void FMassArchetypeData::ExportEntityHandles(TArray<FMassEntityHandle>& InOutHandles) const
{
	for (const FMassArchetypeChunk& Chunk : Chunks)
	{
		InOutHandles.Append(Chunk.GetEntityArray(EntityListOffsetWithinChunk), Chunk.GetNumInstances());
	}
}

FString FMassArchetypeData::DebugGetDescription() const
{
#if WITH_MASSENTITY_DEBUG
	FStringOutputDevice OutDescription;

	if (!DebugNames.IsEmpty())
	{
		OutDescription += TEXT("Name: ");
		OutDescription += GetCombinedDebugNamesAsString();
		OutDescription += TEXT("\n");
	}
	OutDescription += TEXT("Chunk fragments: ");
	CompositionDescriptor.GetChunkFragments().DebugGetStringDesc(OutDescription);
	OutDescription += TEXT("\nTags: ");
	CompositionDescriptor.GetTags().DebugGetStringDesc(OutDescription);
	OutDescription += TEXT("\nFragments: ");
	CompositionDescriptor.GetFragments().DebugGetStringDesc(OutDescription);
	
	return static_cast<FString>(OutDescription);
#else
	return {};
#endif
}

#if WITH_MASSENTITY_DEBUG
void FMassArchetypeData::DebugGetEntityMemoryNumbers(SIZE_T& OutActiveChunksMemorySize, SIZE_T& OutActiveEntitiesMemorySize) const
{
	OutActiveChunksMemorySize = GetChunkAllocSize() * GetNonEmptyChunkCount();
	OutActiveEntitiesMemorySize = TotalBytesPerEntity * EntityMap.Num();
}

FString FMassArchetypeData::GetCombinedDebugNamesAsString() const
{
	TStringBuilder<256> StringBuilder;
	for (int i = 0; i < DebugNames.Num(); i++)
	{
		if (i > 0)
		{
			StringBuilder.Append(TEXT(", "));;
		}
		StringBuilder.Append(DebugNames[i].ToString());
	}
	return StringBuilder.ToString();
}

void FMassArchetypeData::DebugPrintArchetype(FOutputDevice& Ar)
{
	Ar.Logf(ELogVerbosity::Log, TEXT("Name: %s"), *GetCombinedDebugNamesAsString());

	FStringOutputDevice TagsDescription;
	CompositionDescriptor.GetTags().DebugGetStringDesc(TagsDescription);
	Ar.Logf(ELogVerbosity::Log, TEXT("Tags: %s"), *TagsDescription);
	Ar.Logf(ELogVerbosity::Log, TEXT("Fragments: %s"), *DebugGetDescription());
	Ar.Logf(ELogVerbosity::Log, TEXT("\tChunks: %d x %llu KB = %llu KB total"), Chunks.Num(), GetChunkAllocSize() / 1024, (GetChunkAllocSize()*Chunks.Num()) / 1024);
	
	int ChunkWithFragmentsCount = 0;
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		ChunkWithFragmentsCount += Chunk.DebugGetChunkFragmentCount() > 0 ? 1 : 0;
	}
	if (ChunkWithFragmentsCount)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tChunks with fragments: %d"), ChunkWithFragmentsCount);
	}

	const int32 CurrentEntityCapacity = Chunks.Num() * NumEntitiesPerChunk;
	Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Count    : %d"), EntityMap.Num());
	Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Capacity : %d"), CurrentEntityCapacity);
	if (Chunks.Num() > 1)
	{
		const float Scaler = 100.0f / static_cast<float>(CurrentEntityCapacity);
		// count non-last chunks to see how occupied they are
		int EntitiesPerChunkMin = CurrentEntityCapacity;
		int EntitiesPerChunkMax = 0;
		for (int ChunkIndex = 0; ChunkIndex < Chunks.Num() - 1; ++ChunkIndex)
		{
			const int Population = Chunks[ChunkIndex].GetNumInstances();
			EntitiesPerChunkMin = FMath::Min(Population, EntitiesPerChunkMin);
			EntitiesPerChunkMax = FMath::Max(Population, EntitiesPerChunkMax);
		}
		Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Occupancy: %.1f%% (min: %.1f%%, max: %.1f%%)"),
			Scaler * static_cast<float>(EntityMap.Num()),
			Scaler * static_cast<float>(EntitiesPerChunkMin),
			Scaler * static_cast<float>(EntitiesPerChunkMax));
	}
	else 
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Occupancy: %.1f%%"),
			CurrentEntityCapacity > 0 ? ((static_cast<float>(EntityMap.Num()) * 100.0f) / static_cast<float>(CurrentEntityCapacity)) : 0.f);
	}
	Ar.Logf(ELogVerbosity::Log, TEXT("\tBytes / Entity  : %u"), TotalBytesPerEntity);
	Ar.Logf(ELogVerbosity::Log, TEXT("\tEntities / Chunk: %d"), NumEntitiesPerChunk);

	Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04X: Entity[] (%llu bytes each)"), EntityListOffsetWithinChunk, sizeof(FMassEntityHandle));
	int32 TotalBytesOfValidData = sizeof(FMassEntityHandle) * NumEntitiesPerChunk;
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		TotalBytesOfValidData += FragmentConfig.FragmentType->GetStructureSize() * NumEntitiesPerChunk;
		Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04X: %s[] (%d bytes each)"), FragmentConfig.ArrayOffsetWithinChunk, *FragmentConfig.FragmentType->GetName(), FragmentConfig.FragmentType->GetStructureSize());
	}

	//@TODO: Print out padding in between things?

	const SIZE_T UnusablePaddingOffset = TotalBytesPerEntity * NumEntitiesPerChunk;
	const SIZE_T UnusablePaddingAmount = GetChunkAllocSize() - UnusablePaddingOffset;
	if (UnusablePaddingAmount > 0)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04llX: WastePadding[] (%llu bytes total)"), UnusablePaddingOffset, UnusablePaddingAmount);
	}

	if (GetChunkAllocSize() != TotalBytesOfValidData + UnusablePaddingAmount)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\t@TODO: EXTRA PADDING HERE:  TotalBytesOfValidData: %d (%llu missing)"), TotalBytesOfValidData, GetChunkAllocSize() - TotalBytesOfValidData);
	}
}

void FMassArchetypeData::DebugPrintEntity(FMassEntityHandle Entity, FOutputDevice& Ar, const TCHAR* InPrefix) const
{
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		void* Data = GetFragmentDataForEntityChecked(FragmentConfig.FragmentType, Entity.Index);
		
		FString FragmentName = FragmentConfig.FragmentType->GetName();
		FragmentName.RemoveFromStart(InPrefix);

		FString ValueStr;
		FragmentConfig.FragmentType->ExportText(ValueStr, Data, /*Default*/nullptr, /*OwnerObject*/nullptr, EPropertyPortFlags::PPF_IncludeTransient, /*ExportRootScope*/nullptr);

		Ar.Logf(TEXT("%s: %s"), *FragmentName, *ValueStr);
	}
}

#endif // WITH_MASSENTITY_DEBUG

/**
 * SetDebugColor：未指定（FColor{0}）时基于 composition hash 派生 HSV 颜色，确保同 archetype 一致。
 * 指定时直接用。颜色用于 debug 可视化（如 Mass Visual Logger）。
 */
void FMassArchetypeData::SetDebugColor(const FColor InDebugColor)
{
#if WITH_MASSENTITY_DEBUG
	if (InDebugColor == FColor{0})
	{
		// pick a color based on the composition
		// —— 取 composition hash 的 4 个字节分别映射到 HSV 三通道，做适度饱和度/亮度 boost 让颜色不偏暗
		const uint32 CompositionHash = CompositionDescriptor.CalculateHash();
		const uint8* Bytes = reinterpret_cast<const uint8*>(&CompositionHash);
		
		const FLinearColor AdjustedColor = FLinearColor::MakeFromHSV8(
			static_cast<uint8>((Bytes[0] >> 1) + (Bytes[1] >> 1)),
			static_cast<uint8>((Bytes[2] >> 1) + 128),
			static_cast<uint8>((Bytes[3] >> 1) + 128)
		);
		DebugColor = AdjustedColor.ToFColorSRGB();
	}
	else
	{
		DebugColor = InDebugColor;
	}
#endif // WITH_MASSENTITY_DEBUG
}

void FMassArchetypeData::REMOVEME_GetArrayViewForFragmentInChunk(int32 ChunkIndex, const UScriptStruct* FragmentType, void*& OutChunkBase, int32& OutNumEntities)
{
	const FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
	const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);

	OutChunkBase = FragmentConfigs[FragmentIndex].GetFragmentData(Chunk.GetRawMemory(), 0);
	OutNumEntities = Chunk.GetNumInstances();
}

//-----------------------------------------------------------------------------
// FMassArchetypeData batched api
//-----------------------------------------------------------------------------
/**
 * 【BatchAddEntities —— 批量新增】
 *
 * 把一批实体塞进 archetype，可能需要跨多个 chunk（当前 chunk 装不下时继续下一个）。
 * 循环逻辑：
 *  1. PrepareNextEntitiesSpanInternal：找（或建）一个非满 chunk，返回本轮能装多少的 range；
 *  2. 对整段连续槽位调 InitializeStruct(count) 批量构造；
 *  3. 收集新 range 到 OutNewRanges；
 *  4. 如果还有实体没放完，继续循环（注意从上次的 chunk 继续往后找，避免重复）。
 */
void FMassArchetypeData::BatchAddEntities(TConstArrayView<FMassEntityHandle> Entities, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TArray<FMassArchetypeEntityCollection::FArchetypeEntityRange>& OutNewRanges)
{
	SCOPE_CYCLE_COUNTER(STAT_Mass_ArchetypeBatchAdd);

	testableCheckfReturn(SharedFragmentValues.HasExactSharedFragmentTypesMatch(GetCompositionDescriptor().GetSharedFragments()), return, TEXT("%hs parameter SharedFragmentValues doesn't match archetype's composition"), __FUNCTION__);
	testableCheckfReturn(SharedFragmentValues.HasExactConstSharedFragmentTypesMatch(GetCompositionDescriptor().GetConstSharedFragments()), return, TEXT("%hs parameter ConstSharedFragmentValues doesn't match archetype's composition"), __FUNCTION__);

	FMassArchetypeEntityCollection::FArchetypeEntityRange ResultSubchunk;
	ResultSubchunk.ChunkIndex = 0;
	int32 NumberMoved = 0;
	do 
	{
		// 从上次的 ChunkIndex 开始找下一段空间；返回本轮真实分配到的 range
		ResultSubchunk = PrepareNextEntitiesSpanInternal(MakeArrayView(Entities.GetData() + NumberMoved, Entities.Num() - NumberMoved), SharedFragmentValues, ResultSubchunk.ChunkIndex);
		check(Chunks.IsValidIndex(ResultSubchunk.ChunkIndex) && Chunks[ResultSubchunk.ChunkIndex].IsValidSubChunk(ResultSubchunk.SubchunkStart, ResultSubchunk.Length));
		
		// 对整段槽位批量构造每个 fragment（InitializeStruct 支持 count 参数）
		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			void* FragmentPtr = FragmentConfig.GetFragmentData(Chunks[ResultSubchunk.ChunkIndex].GetRawMemory(), ResultSubchunk.SubchunkStart);
			FragmentConfig.FragmentType->InitializeStruct(FragmentPtr, ResultSubchunk.Length);
		}

		NumberMoved += ResultSubchunk.Length;

		OutNewRanges.Add(ResultSubchunk);

	} while (NumberMoved < Entities.Num());
}

/**
 * 【BatchMoveEntitiesToAnotherArchetype —— 批量跨 archetype 迁移】
 *
 * 核心流程：
 *  (1) 校验：若指定了 shared fragment 增删，验证目标 archetype 的 shared 组合 = 当前 shared 组合经修改后的结果；
 *  (2) 对每个源 range，尝试向目标 archetype 的多个 chunk 依次追加（PrepareNextEntitiesSpanInternal）；
 *      —— 目标每段与源段用 MoveFragmentsToAnotherArchetypeInternal 做列映射迁移；
 *      —— OutNewRanges 若给出，会把相邻的目标 range 合并起来；
 *  (3) 所有迁移完成后，按 "SubchunkStart 降序" 回头从源 archetype 批量删除这些 range；
 *  (4) 最后统一从 EntityMap 清除被迁出的实体。
 *
 * 为什么先迁再删？保留源数据直到迁移完成，可直接 memcpy；且删的顺序与迁的顺序可解耦，
 * 方便在循环里先做容易出错的"挪数据"再做"更新索引"。
 */
void FMassArchetypeData::BatchMoveEntitiesToAnotherArchetype(const FMassArchetypeEntityCollection& EntityCollection
	, FMassArchetypeData& NewArchetype, TArray<FMassEntityHandle>& OutEntitiesBeingMoved
	, TArray<FMassArchetypeEntityCollection::FArchetypeEntityRange>* OutNewRanges, const FMassArchetypeSharedFragmentValues* SharedFragmentValuesToAdd
	, const FMassSharedFragmentBitSet* SharedFragmentToRemoveBitSet, const FMassConstSharedFragmentBitSet* ConstSharedFragmentToRemoveBitSet)
{
	check(&NewArchetype != this);

	// verify the new archetype's shared fragment composition matches current archetype's composition modified as requested
	if (SharedFragmentValuesToAdd)
	{
		bool bIsValidArchetype = true;
		if (SharedFragmentToRemoveBitSet)
		{
			FMassSharedFragmentBitSet NewSharedFragmentsBitset = GetSharedFragmentBitSet();
			NewSharedFragmentsBitset -= *SharedFragmentToRemoveBitSet;
			NewSharedFragmentsBitset += SharedFragmentValuesToAdd->GetSharedFragmentBitSet();
			bIsValidArchetype = NewArchetype.GetCompositionDescriptor().GetSharedFragments() == NewSharedFragmentsBitset;
		}

		if (bIsValidArchetype && ConstSharedFragmentToRemoveBitSet)
		{
			FMassConstSharedFragmentBitSet NewConstSharedFragmentsBitset = GetConstSharedFragmentBitSet();
			NewConstSharedFragmentsBitset -= *ConstSharedFragmentToRemoveBitSet;
			NewConstSharedFragmentsBitset += SharedFragmentValuesToAdd->GetConstSharedFragmentBitSet();
			bIsValidArchetype = NewArchetype.GetCompositionDescriptor().GetConstSharedFragments() == NewConstSharedFragmentsBitset;
		}

		testableCheckfReturn(bIsValidArchetype, return, TEXT("%hs parameter SharedFragmentValues doesn't match archetype's composition"), __FUNCTION__);
	}

	TArray<FMassArchetypeEntityCollection::FArchetypeEntityRange> Subchunks(EntityCollection.GetRanges());

	const int32 InitialOutEntitiesCount = OutEntitiesBeingMoved.Num();

	for (const FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange : Subchunks)
	{
		if (!ensureMsgf(EntityRange.IsSet() && EntityRange.Length >= 0, TEXT("We only expect to get valid EntityRanges at this point.")))
		{
			continue;
		}

		FMassArchetypeChunk& Chunk = Chunks[EntityRange.ChunkIndex];
		const int32 RangeLength = CalculateRangeLength(EntityRange, Chunk);

		// 0 - consider compacting new archetype to ensure larger empty spaces
		// 1. find next free spot in the destination archetype
		// 2. min(amount of elements) to move

		// gather entities we're about to remove
		FMassEntityHandle* DyingEntityPtr = &Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, EntityRange.SubchunkStart);
		OutEntitiesBeingMoved.Append(DyingEntityPtr, RangeLength);

		FMassArchetypeEntityCollection::FArchetypeEntityRange ResultSubChunk;
		ResultSubChunk.ChunkIndex = 0;
		ResultSubChunk.Length = 0;
		int32 NumberMoved = 0;
		const bool bChangeSharedFragments = SharedFragmentValuesToAdd || SharedFragmentToRemoveBitSet || ConstSharedFragmentToRemoveBitSet;

		do
		{
			const int32 IndexWithinChunk = EntityRange.SubchunkStart + NumberMoved;

			if (bChangeSharedFragments == false)
			{
				ResultSubChunk = NewArchetype.PrepareNextEntitiesSpanInternal(MakeArrayView(DyingEntityPtr + NumberMoved, RangeLength - NumberMoved)
					, Chunk.GetSharedFragmentValues(), ResultSubChunk.ChunkIndex);
			}
			else
			{
				// create new shared values
				FMassArchetypeSharedFragmentValues NewSharedValues = Chunk.GetSharedFragmentValues();
				if (SharedFragmentToRemoveBitSet)
				{
					NewSharedValues.Remove(*SharedFragmentToRemoveBitSet);
				}
				if (ConstSharedFragmentToRemoveBitSet)
				{
					NewSharedValues.Remove(*ConstSharedFragmentToRemoveBitSet);
				}
				if (SharedFragmentValuesToAdd)
				{
					NewSharedValues.Append(*SharedFragmentValuesToAdd);
				}
				NewSharedValues.Sort();

				ResultSubChunk = NewArchetype.PrepareNextEntitiesSpanInternal(MakeArrayView(DyingEntityPtr + NumberMoved, RangeLength - NumberMoved)
					, NewSharedValues, ResultSubChunk.ChunkIndex);
			}

			FMassArchetypeChunk& NewChunk = NewArchetype.Chunks[ResultSubChunk.ChunkIndex];
			MoveFragmentsToAnotherArchetypeInternal(NewArchetype, {NewChunk.GetRawMemory(), ResultSubChunk.SubchunkStart}, {Chunk.GetRawMemory(), IndexWithinChunk}, ResultSubChunk.Length);

			NumberMoved += ResultSubChunk.Length;

			if (OutNewRanges)
			{
				// if the new ResultSubChunk is right next to the last stored one then merge them both
				if (OutNewRanges->Num() && OutNewRanges->Last().IsAdjacentAfter(ResultSubChunk))
				{
					OutNewRanges->Last().Length += ResultSubChunk.Length;
				}
				else // just add
				{
					OutNewRanges->Add(ResultSubChunk);
				}
			}

		} while (NumberMoved < RangeLength);

	}

	// Sorting the subchunks info so that subchunks of a given chunk are processed "from the back". Otherwise removing 
	// a subchunk from the front of the chunk would inevitably invalidate following subchunks' information.
	// Note that we do this after already having added the entities to the new archetype to preserve the order of entities 
	// as given by the input data.
	Subchunks.Sort([](const FMassArchetypeEntityCollection::FArchetypeEntityRange& A, const FMassArchetypeEntityCollection::FArchetypeEntityRange& B)
	{
		return A.ChunkIndex < B.ChunkIndex || (A.ChunkIndex == B.ChunkIndex && A.SubchunkStart > B.SubchunkStart);
	});

	for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Subchunk : Subchunks)
	{
		BatchRemoveEntitiesInternal(Subchunk.ChunkIndex, Subchunk.SubchunkStart, Subchunk.Length);
	}

	for (int i = InitialOutEntitiesCount; i < OutEntitiesBeingMoved.Num(); ++i)
	{
		EntityMap.FindAndRemoveChecked(OutEntitiesBeingMoved[i].Index);
	}
}

/**
 * 【PrepareNextEntitiesSpanInternal —— 给批量新增找下一段连续空间】
 *
 * 从 StartingChunk 开始线性扫 chunks，找到第一个"非满 + SharedValues 匹配"的 chunk。
 * 找到了：从其末尾开始放，最多放 NumEntitiesPerChunk - NumInstances 个；
 * 找不到：在末尾追加新 chunk。
 *
 * 注意：若找到的 chunk 当前 NumInstances==0（重生 chunk）会先 Recycle 一次（重置元数据 + Malloc 内存）。
 * 行为约束：
 *  - 不会返回 length==0；
 *  - 调用方循环时 ResultSubchunk.ChunkIndex 会作为下次的 StartingChunk —— 隐式假设新 chunk 总是
 *    被 append 到数组尾部（不会插入中间）。
 */
FMassArchetypeEntityCollection::FArchetypeEntityRange FMassArchetypeData::PrepareNextEntitiesSpanInternal(TConstArrayView<FMassEntityHandle> Entities, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, int32 StartingChunk)
{
	checkf(SharedFragmentValues.IsSorted(), TEXT("Expecting shared fragment values to be previously sorted"));
	checkf(SharedFragmentValues.HasExactFragmentTypesMatch(CompositionDescriptor.GetSharedFragments(), CompositionDescriptor.GetConstSharedFragments())
		, TEXT("Expecting values for every specified shared fragment in the archetype and only those"))

	int32 StartIndexWithinChunk = INDEX_NONE;
	int32 AbsoluteStartIndex = 0;

	FMassArchetypeChunk* DestinationChunk = nullptr;
	
	int32 ChunkIndex = StartingChunk;
	// find a chunk with any room left
	for (; ChunkIndex < Chunks.Num(); ++ChunkIndex)
	{
		FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
		if (Chunk.GetNumInstances() < NumEntitiesPerChunk && Chunk.GetSharedFragmentValues().IsEquivalent(SharedFragmentValues))
		{
			StartIndexWithinChunk = Chunk.GetNumInstances();
			AbsoluteStartIndex = ChunkIndex * NumEntitiesPerChunk + StartIndexWithinChunk;

			DestinationChunk = &Chunk;

			if (StartIndexWithinChunk == 0)
			{
				Chunk.Recycle(ChunkFragmentsTemplate, SharedFragmentValues);
			}
			break;
		}
	}

	// if no chunk found create one
	if (DestinationChunk == nullptr)
	{
		ChunkIndex = Chunks.Num();
		AbsoluteStartIndex = Chunks.Num() * NumEntitiesPerChunk;
		StartIndexWithinChunk = 0;

		DestinationChunk = &Chunks.Emplace_GetRef(GetChunkAllocSize(), ChunkFragmentsTemplate, SharedFragmentValues);
	}

	check(DestinationChunk);

	// we might be able to fit in less entities than requested
	const int32 NumToAdd = FMath::Min(NumEntitiesPerChunk - StartIndexWithinChunk, Entities.Num());
	check(NumToAdd);
	DestinationChunk->AddMultipleInstances(NumToAdd);

	// Add to the table and map
	int32 AbsoluteIndex = AbsoluteStartIndex;
	for (int32 i = 0; i < NumToAdd; ++i)
	{
		EntityMap.Add(Entities[i].Index, AbsoluteIndex++);
	}

	FMassEntityHandle* FirstAddedEntity = &DestinationChunk->GetEntityArrayElementRef(EntityListOffsetWithinChunk, StartIndexWithinChunk);
	FMemory::Memcpy(FirstAddedEntity, Entities.GetData(), sizeof(FMassEntityHandle) * NumToAdd);

	return FMassArchetypeEntityCollection::FArchetypeEntityRange(ChunkIndex, StartIndexWithinChunk, NumToAdd);
}

/**
 * 【BatchRemoveEntitiesInternal —— 批量 swap-and-pop 实现】
 *
 * 把 chunk 第 [StartIndexWithinChunk, +NumberToRemove) 段实体移除。
 * 算法：
 *  - 从 chunk 末尾取出 NumberToMove 个实体，批量 memcpy 覆盖被删段；
 *  - 更新身份列与 EntityMap 中被挪动实体的 AbsoluteIndex；
 *  - 调 RemoveMultipleInstances 减计数；
 *  - 弹尾部空 chunk。
 *
 * NumberToMove = min(被删段后剩余实体数, NumberToRemove)。前者代表"末尾有多少个实体可挪回来"，
 * 后者代表"被删的洞有多大"。两者取小，因为：
 *  - 若被删段已经在 chunk 最末，没东西好挪（NumberToMove==0）；
 *  - 若被删段中间还有别的实体，最多只能补 NumberToRemove 个（多余的会自然落尾被 pop）。
 *
 * 注意 check："Remove and Move ranges overlap" —— 调用方必须保证被删段与"末尾要挪过来的段"不重叠。
 */
void FMassArchetypeData::BatchRemoveEntitiesInternal(const int32 ChunkIndex, const int32 StartIndexWithinChunk, const int32 NumberToRemove)
{
	if (UNLIKELY(NumberToRemove <= 0))
	{
		return;
	}

	++EntityOrderVersion;  // 让 collection / handle 失效

	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
	
	// 末尾可挪回来的实体数：被删段之后剩余的实体数 vs 被删段大小，取小
	const int32 NumberToMove = FMath::Min(Chunk.GetNumInstances() - (StartIndexWithinChunk + NumberToRemove), NumberToRemove);
	checkf(NumberToMove >= 0, TEXT("Trying to move a negative number of elements indicates a problem with sub-chunk indicators, it's possibly out of date."));

	if (NumberToMove > 0)
	{
		FMassEntityHandle* DyingEntityPtr = &Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, StartIndexWithinChunk);

		// 末尾要被挪过来的起点
		const int32 SwapStartIndex = Chunk.GetNumInstances() - NumberToMove;
		checkf((StartIndexWithinChunk + NumberToMove - 1) < SwapStartIndex, TEXT("Remove and Move ranges overlap"));

		// 整段 fragment 列 memcpy；同 archetype，所有列都搬
		MoveFragmentsToNewLocationInternal({ Chunk.GetRawMemory(), StartIndexWithinChunk }, { Chunk.GetRawMemory(), SwapStartIndex }, NumberToMove);
		
		// Update the entity table and map
		// —— 改身份列 + EntityMap：被挪过来的实体获得新的 AbsoluteIndex
		const FMassEntityHandle* MovingEntityPtr = &Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, SwapStartIndex);
		int32 AbsoluteIndex = ChunkIndex * NumEntitiesPerChunk + StartIndexWithinChunk;

		for (int i = 0; i < NumberToMove; ++i)
		{
			DyingEntityPtr[i] = MovingEntityPtr[i];
			EntityMap.FindChecked(MovingEntityPtr[i].Index) = AbsoluteIndex++;
		}
	}

	Chunk.RemoveMultipleInstances(NumberToRemove);

	// If the chunk itself is empty now, see if we can remove it entirely
	// Note: This is only possible for trailing chunks, to avoid messing up the absolute indices in the entities map
	while ((Chunks.Num() > 0) && (Chunks.Last().GetNumInstances() == 0))
	{
		Chunks.RemoveAt(Chunks.Num() - 1, EAllowShrinking::No);
	}
}

/**
 * 【MoveFragmentsToAnotherArchetypeInternal —— 跨 archetype 的 fragment 列迁移】
 *
 * 处理两 archetype "fragment 集合不完全相同" 的情形：
 *  - 共有 fragment：源列 memcpy 到目标列，同时保留原值；
 *  - 目标独有：用默认值 InitializeStruct 构造（旧 archetype 没这列，无值可搬）；
 *  - 源独有：DestroyStruct 析构（目标 archetype 不要这列了）。
 *
 * 第一段循环驱动"目标"列，第二段循环驱动"源"列以处理被丢弃的列。
 */
void FMassArchetypeData::MoveFragmentsToAnotherArchetypeInternal(FMassArchetypeData& TargetArchetype, FMassArchetypeData::FTransientChunkLocation Target
	, const FMassArchetypeData::FTransientChunkLocation Source, const int32 ElementsNum)
{
	// for every TargetArchetype's fragment see if it was in the old archetype as well and if so copy it's value. 
	// If not then initialize the fragment.
	for (const FMassArchetypeFragmentConfig& TargetFragmentConfig : TargetArchetype.FragmentConfigs)
	{
		const int32* OldFragmentIndex = FragmentIndexMap.Find(TargetFragmentConfig.FragmentType);
		void* Dst = TargetFragmentConfig.GetFragmentData(Target.RawChunkMemory, Target.IndexWithinChunk);

		// Only copy if the fragment type exists in both archetypes
		if (OldFragmentIndex)
		{
			// 共有列：直接 memcpy ElementsNum 条记录（不构造/析构 —— fragment 必须是 trivially copyable）
			const void* Src = FragmentConfigs[*OldFragmentIndex].GetFragmentData(Source.RawChunkMemory, Source.IndexWithinChunk);
			FMemory::Memcpy(Dst, Src, TargetFragmentConfig.FragmentType->GetStructureSize() * ElementsNum);
		}
		else
		{
			// the fragment's unique to the TargetArchetype need to be initialized
			// @todo we're doing it for tags here as well. A tiny bit of perf lost. Probably not worth adding a check
			// but something to keep in mind. Will go away once tags are more of an archetype fragment than entity's
			// —— 目标独有列：默认构造。TODO 注释提到 tag 也走这里有点浪费，未来 tag 完全 archetype 化后会优化
			TargetFragmentConfig.FragmentType->InitializeStruct(Dst, ElementsNum);
		}
	}

	// Delete fragments that were left behind
	// —— 反向扫源 archetype 的列：在目标里不存在的 → 析构掉
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		// If the fragment is not in the new archetype, destroy it.
		const int32* NewFragmentIndex = TargetArchetype.FragmentIndexMap.Find(FragmentConfig.FragmentType);
		if (NewFragmentIndex == nullptr)
		{
			void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Source.RawChunkMemory, Source.IndexWithinChunk);
			FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr, ElementsNum);
		}
	}
}

/**
 * 【MoveFragmentsToNewLocationInternal —— 同 archetype 内的 fragment 搬运】
 *
 * 用于 swap-and-pop / compaction / SetSharedFragmentsData 等场景：源和目标都属于本 archetype，
 * 列布局完全一致，所以对每列 memcpy 即可，无需类型检查/初始化/析构。FORCEINLINE 是因为
 * 它在批量 remove / compaction 内部紧密循环里被调用。
 */
FORCEINLINE void FMassArchetypeData::MoveFragmentsToNewLocationInternal(FMassArchetypeData::FTransientChunkLocation Target, const FMassArchetypeData::FTransientChunkLocation Source, const int32 NumberToMove)
{
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Target.RawChunkMemory, Target.IndexWithinChunk);
		void* MovingFragmentPtr = FragmentConfig.GetFragmentData(Source.RawChunkMemory, Source.IndexWithinChunk); 

		// Swap fragments to the empty space just created.
		// —— 注意原英文注释说"swap"实际上只是 memcpy（把 source 的字节覆盖到 target）；
		// source 槽位很快会被 RemoveMultipleInstances 弹掉，所以不再"对称交换"
		FMemory::Memcpy(DyingFragmentPtr, MovingFragmentPtr, FragmentConfig.FragmentType->GetStructureSize() * NumberToMove);
	}
}

/**
 * 【BatchSetFragmentValues —— 把 payload 切片批量写入 archetype】
 *
 * Payload 是若干列（每列一种 fragment 类型，且元素数量 = 实体批次总数）。
 * 对每个 range：
 *  - 先 memcpy 第一列到本 range 对应的 fragment 列；
 *  - 再 memcpy 第二列；...
 * EntitiesHandled 记录已写入的实体总数，用于在 payload 列里偏移到本 range 的起点。
 *
 * 注意此处 FragmentType 通过 FindChecked 取列下标 —— 调用方必须确保 payload 列类型都是 archetype
 * 拥有的 fragment 类型，不会 fallback。
 */
void FMassArchetypeData::BatchSetFragmentValues(TConstArrayView<FMassArchetypeEntityCollection::FArchetypeEntityRange> EntityCollection, const FMassGenericPayloadViewSlice& Payload)
{
	int32 EntitiesHandled = 0;

	for (const FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange : EntityCollection)
	{
		FMassArchetypeChunk& Chunk = Chunks[EntityRange.ChunkIndex];
		const int32 RangeLength = CalculateRangeLength(EntityRange, Chunk);

		for (int i = 0; i < Payload.Num(); ++i)
		{
			FStructArrayView FragmentPayload = Payload[i];
			check(FragmentPayload.Num() - EntitiesHandled >= RangeLength);

			const UScriptStruct* FragmentType = FragmentPayload.GetScriptStruct();
			check(FragmentType);

			// 列下标 + payload 切片源 + chunk 内目的地：调 CopyScriptStruct 处理可能含 UObject 引用的 struct 拷贝
			const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);
			void* Dst = FragmentConfigs[FragmentIndex].GetFragmentData(Chunk.GetRawMemory(), EntityRange.SubchunkStart);
			const void* Src = FragmentPayload.GetDataAt(EntitiesHandled);

			FragmentType->CopyScriptStruct(Dst, Src, RangeLength);
		}

		EntitiesHandled += RangeLength;
	}
}

bool FMassArchetypeData::IsEquivalent(const FMassArchetypeCompositionDescriptor& OtherCompositionDescriptor, const UE::Mass::FArchetypeGroups& OtherGroups) const
{
	return CompositionDescriptor.IsEquivalent(OtherCompositionDescriptor) && Groups == OtherGroups;
}

//-----------------------------------------------------------------------------
// FMassArchetypeHelper
//-----------------------------------------------------------------------------
// 委托给 composition descriptor → requirements 的判定函数；不打 log
bool FMassArchetypeHelper::DoesArchetypeMatchRequirements(const FMassArchetypeData& Archetype, const FMassFragmentRequirements& Requirements)
{
	return DoesArchetypeMatchRequirements(Archetype.GetCompositionDescriptor(), Requirements);
}
bool FMassArchetypeHelper::DoesArchetypeMatchRequirements(const FMassArchetypeCompositionDescriptor& ArchetypeComposition, const FMassFragmentRequirements& Requirements)
{
	return Requirements.DoesArchetypeMatchRequirements(ArchetypeComposition);
}

#if WITH_MASSENTITY_DEBUG
/**
 * 调试版的需求匹配：失败时可向 OutputDevice 输出 "为什么不匹配" 的诊断报告。
 * 文件后半部分大段被注释掉的代码是历史遗留 —— 早期手工逐项打 log，后来统一到
 * FMassDebugger::GetArchetypeRequirementCompatibilityDescription 一次性生成。
 */
bool FMassArchetypeHelper::DoesArchetypeMatchRequirements(const FMassArchetypeData& Archetype, const FMassFragmentRequirements& Requirements
	, const bool bBailOutOnFirstFail, FOutputDevice* OutputDevice)
{
	if (DoesArchetypeMatchRequirements(Archetype.GetCompositionDescriptor(), Requirements))
	{
		// nothing to log
		return true;
	}
	
	if (OutputDevice)
	{
		// do logging
		OutputDevice->Logf(TEXT("%s")
			, *FMassDebugger::GetArchetypeRequirementCompatibilityDescription(Requirements, Archetype.GetCompositionDescriptor()));
	}

	return false;

	//bool bResult = true;
	//if (Requirements.IsOptionalsOnly() == false)
	//{
	//	if (Archetype.GetTagBitSet().HasAll(Requirements.GetRequiredAllTags()) == false)
	//	{
	//		// missing some required tags, skip.
	//		const FMassTagBitSet UnsatisfiedTags = Requirements.GetRequiredAllTags() - Archetype.GetTagBitSet();
	//		FStringOutputDevice Description;
	//		UnsatisfiedTags.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing tags: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetTagBitSet().HasNone(Requirements.GetRequiredNoneTags()) == false)
	//	{
	//		// has some tags required to be absent
	//		const FMassTagBitSet UnwantedTags = Requirements.GetRequiredNoneTags().GetOverlap(Archetype.GetTagBitSet());
	//		FStringOutputDevice Description;
	//		UnwantedTags.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype has tags required absent: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Requirements.GetRequiredAnyTags().IsEmpty() == false
	//		&& Archetype.GetTagBitSet().HasAny(Requirements.GetRequiredAnyTags()) == false)
	//	{
	//		FStringOutputDevice Description;
	//		Requirements.GetRequiredAnyTags().DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing \'any\' tags: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetFragmentBitSet().HasAll(Requirements.GetRequiredAllFragments()) == false)
	//	{
	//		// missing some required fragments, skip.
	//		const FMassFragmentBitSet UnsatisfiedFragments = Requirements.GetRequiredAllFragments() - Archetype.GetFragmentBitSet();
	//		FStringOutputDevice Description;
	//		UnsatisfiedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing Fragments: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetFragmentBitSet().HasNone(Requirements.GetRequiredNoneFragments()) == false)
	//	{
	//		// has some Fragments required to be absent
	//		const FMassFragmentBitSet UnwantedFragments = Requirements.GetRequiredNoneFragments().GetOverlap(Archetype.GetFragmentBitSet());
	//		FStringOutputDevice Description;
	//		UnwantedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype has Fragments required absent: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Requirements.GetRequiredAnyFragments().IsEmpty() == false
	//		&& Archetype.GetFragmentBitSet().HasAny(Requirements.GetRequiredAnyFragments()) == false)
	//	{
	//		FStringOutputDevice Description;
	//		Requirements.GetRequiredAnyFragments().DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing \'any\' fragments: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetChunkFragmentBitSet().HasAll(Requirements.GetRequiredAllChunkFragments()) == false)
	//	{
	//		// missing some required fragments, skip.
	//		const FMassChunkFragmentBitSet UnsatisfiedFragments = Requirements.GetRequiredAllChunkFragments() - Archetype.GetChunkFragmentBitSet();
	//		FStringOutputDevice Description;
	//		UnsatisfiedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing Chunk Fragments: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetChunkFragmentBitSet().HasNone(Requirements.GetRequiredNoneChunkFragments()) == false)
	//	{
	//		// has some Fragments required to be absent
	//		const FMassChunkFragmentBitSet UnwantedFragments = Requirements.GetRequiredNoneChunkFragments().GetOverlap(Archetype.GetChunkFragmentBitSet());
	//		FStringOutputDevice Description;
	//		UnwantedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype has Chunk Fragments required absent: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetSharedFragmentBitSet().HasAll(Requirements.GetRequiredAllSharedFragments()) == false)
	//	{
	//		// missing some required fragments, skip.
	//		const FMassSharedFragmentBitSet UnsatisfiedFragments = Requirements.GetRequiredAllSharedFragments() - Archetype.GetSharedFragmentBitSet();
	//		FStringOutputDevice Description;
	//		UnsatisfiedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing Shared Fragments: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetSharedFragmentBitSet().HasNone(Requirements.GetRequiredNoneSharedFragments()) == false)
	//	{
	//		// has some Fragments required to be absent
	//		const FMassSharedFragmentBitSet UnwantedFragments = Requirements.GetRequiredNoneSharedFragments().GetOverlap(Archetype.GetSharedFragmentBitSet());
	//		FStringOutputDevice Description;
	//		UnwantedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype has Shared Fragments required absent: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetConstSharedFragmentBitSet().HasAll(Requirements.GetRequiredAllConstSharedFragments()) == false)
	//	{
	//		// missing some required fragments, skip.
	//		const FMassConstSharedFragmentBitSet UnsatisfiedFragments = Requirements.GetRequiredAllConstSharedFragments() - Archetype.GetConstSharedFragmentBitSet();
	//		FStringOutputDevice Description;
	//		UnsatisfiedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype did not match due to missing Const Shared Fragments: %s"), *Description);

	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}

	//	if (Archetype.GetConstSharedFragmentBitSet().HasNone(Requirements.GetRequiredNoneConstSharedFragments()) == false)
	//	{
	//		// has some Fragments required to be absent
	//		const FMassConstSharedFragmentBitSet UnwantedFragments = Requirements.GetRequiredNoneConstSharedFragments().GetOverlap(Archetype.GetConstSharedFragmentBitSet());
	//		FStringOutputDevice Description;
	//		UnwantedFragments.DebugGetStringDesc(Description);
	//		OutputDevice->Logf(TEXT("Archetype has Const Shared Fragments required absent: %s"), *Description);

	//		// could skip the test as it's the final check we're performing, but leaving it in in case more cases are added
	//		// later without checking the existing code
	//		bResult = false;
	//		if (bBailOutOnFirstFail)
	//		{
	//			return false;
	//		}
	//	}
	//}
	//else
	//{
	//	// test if contains any of the optional elements
	//	if (Archetype.GetFragmentBitSet().HasNone(Requirements.GetRequiredOptionalFragments())
	//		&& Archetype.GetTagBitSet().HasNone(Requirements.GetRequiredOptionalTags())
	//		&& Archetype.GetChunkFragmentBitSet().HasNone(Requirements.GetRequiredOptionalChunkFragments())
	//		&& Archetype.GetSharedFragmentBitSet().HasNone(Requirements.GetRequiredOptionalSharedFragments())
	//		&& Archetype.GetConstSharedFragmentBitSet().HasNone(Requirements.GetRequiredOptionalConstSharedFragments()))
	//	{
	//		FStringOutputDevice Description;
	//		Requirements.GetRequiredOptionalFragments().DebugGetStringDesc(Description);
	//		Requirements.GetRequiredOptionalTags().DebugGetStringDesc(Description);
	//		Requirements.GetRequiredOptionalChunkFragments().DebugGetStringDesc(Description);
	//		Requirements.GetRequiredOptionalSharedFragments().DebugGetStringDesc(Description);
	//		Requirements.GetRequiredOptionalConstSharedFragments().DebugGetStringDesc(Description);
	//		
	//		OutputDevice->Logf(TEXT("Archetype has none of the optional elements: %s"), *Description);

	//		bResult = false;
	//	}
	//}

	//return bResult;
}
#endif // WITH_MASSENTITY_DEBUG