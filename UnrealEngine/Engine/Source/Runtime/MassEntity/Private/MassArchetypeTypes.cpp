// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassArchetypeTypes.h"
#include "MassEntityManager.h"
#include "MassArchetypeData.h"
#include "MassEntityUtils.h"
#include "Containers/StridedView.h"
#include "MassTestableEnsures.h"

//-----------------------------------------------------------------------------
// FMassArchetypeHandle
//-----------------------------------------------------------------------------
// 句柄哈希即底层指针哈希（身份相等）。注意 versioned handle 的 HandleVersion 不参与 hash
uint32 GetTypeHash(const FMassArchetypeHandle& Instance)
{
	return GetTypeHash(Instance.DataPtr.Get());
}

//-----------------------------------------------------------------------------
// FMassArchetypeVersionedHandle
//-----------------------------------------------------------------------------
// 构造时从 archetype 抓取当前 EntityOrderVersion 作为快照；若 archetype 无效则版本=0
FMassArchetypeVersionedHandle::FMassArchetypeVersionedHandle(const FMassArchetypeHandle& InHandle)
	: ArchetypeHandle(InHandle)
	, HandleVersion(ArchetypeHandle.IsValid() ? ArchetypeHandle.DataPtr->GetEntityOrderVersion() : 0)
{
}

FMassArchetypeVersionedHandle::FMassArchetypeVersionedHandle(FMassArchetypeHandle&& InHandle)
	: ArchetypeHandle(Forward<FMassArchetypeHandle>(InHandle))
	, HandleVersion(ArchetypeHandle.IsValid() ? ArchetypeHandle.DataPtr->GetEntityOrderVersion() : 0)
{
}

// 只要 archetype 目前的 EntityOrderVersion 仍等于构造时保存的快照，就认为句柄未过期
// 过期常见原因：RemoveEntity 的 swap-and-pop、BatchMove、CompactEntities 都会 ++EntityOrderVersion
bool FMassArchetypeVersionedHandle::IsUpToDate() const
{
	return ArchetypeHandle.IsValid() && (ArchetypeHandle.DataPtr->GetEntityOrderVersion() == HandleVersion);
}

//-----------------------------------------------------------------------------
// FMassArchetypeEntityCollection 
//-----------------------------------------------------------------------------
/**
 * 单实体 → range 的工具函数。
 * 步骤：
 *  1. 通过 EntityIndex 在 EntityMap 里查到实体在 archetype 内的绝对索引 TrueIndex；
 *  2. 用 TrueIndex / NumEntitiesPerChunk 得到 chunk 下标；
 *  3. 用 TrueIndex % NumEntitiesPerChunk 得到 chunk 内槽位；
 *  4. 生成 Length==1 的 range。
 * 若 archetype 为 nullptr（例如实体尚未分配 archetype），退化为用 Entity.Index 充当位置信息，
 * 仅用于排序分组，不会用于真正的 chunk 寻址。
 */
FORCEINLINE FMassArchetypeEntityCollection::FArchetypeEntityRange FMassArchetypeEntityCollection::CreateRangeForEntity(const FMassArchetypeHandle& InArchetype, const FMassEntityHandle EntityHandle)
{
	if (const FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(InArchetype))
	{
		const int32* TrueIndexPtr = ArchetypeData->GetInternalIndexForEntity(EntityHandle.Index);
		check(TrueIndexPtr);
		const int32 TrueIndex = *TrueIndexPtr;
		checkf(TrueIndex >= 0, TEXT("Negative values are unexpected here"));

		const int32 NumEntitiesPerChunk = ArchetypeData->GetNumEntitiesPerChunk();
		const int32 ChunkIndex = TrueIndex / NumEntitiesPerChunk;
		const int32 SubchunkStart = TrueIndex % NumEntitiesPerChunk;
		return FArchetypeEntityRange(ChunkIndex, SubchunkStart, 1);
	}

	return FArchetypeEntityRange(0, EntityHandle.Index, 1);
}

/**
 * 【主构造函数】从乱序的 FMassEntityHandle 数组构造 range 集合。
 * 流程：
 *  (A) 若只有 0 或 1 个 entity，直接走快速路径；
 *  (B) 为每个实体查出 "archetype 内绝对索引"（TrueIndex）放到 TrueIndices；
 *      没有 archetype 时回退到 Entity.Index（仅用于分组/排序，不直接当 chunk 坐标）；
 *  (C) 对 TrueIndices 排序 —— 这样接下来可以把连续区段折叠成 range；
 *  (D) 按 DuplicatesHandling 去重（NoDuplicates 时仅 slow-check 验证无重复）；
 *  (E) 调用 BuildEntityRanges 折叠成 ranges。
 */
FMassArchetypeEntityCollection::FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetype, TConstArrayView<FMassEntityHandle> InEntities, EDuplicatesHandling DuplicatesHandling)
	: Archetype(InArchetype)
{
	if (InEntities.Num() <= 0)
	{
		return;
	}
	if (InEntities.Num() == 1)
	{
		// 单实体捷径：省掉 sort/dedup 全套
		Ranges.Add(CreateRangeForEntity(InArchetype, InEntities[0]));
		return;
	}

	const FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(InArchetype);

	// InEntities has a real chance of not being sorted by AbsoluteIndex. We gotta fix that to optimize how we process the data 
	// —— 用户传来的实体数组几乎肯定不是按 absolute index 有序的（玩家侧数据几乎都是按创建顺序），
	// 这里必须自己排序，否则无法把连续的 range 折叠起来
	TArray<int32> TrueIndices;
	TrueIndices.AddUninitialized(InEntities.Num());
	int32 NumValidEntities = 0;
	if (ArchetypeData)
	{
		for (const FMassEntityHandle& Entity : InEntities)
		{
			if (Entity.IsValid())
			{
				// 在 archetype EntityMap 中查实体对应的绝对槽位；找不到说明实体不在此 archetype
				if (const int32* TrueIndex = ArchetypeData->GetInternalIndexForEntity(Entity.Index))
				{
					TrueIndices[NumValidEntities++] = *TrueIndex;
				}
			}
		}
	}
	else
	{
		// special case, where we have a bunch of entities that have been built but not assigned an archetype yet.
		// we use their base index for the sake of sorting here. Will still get some perf benefits and we can keep using
		// FMassArchetypeEntityCollection as the generic batched API wrapper for entities
		// —— 实体已创建但尚未分配到具体 archetype 的情形（预留实体）。
		// 这时用 Entity.Index 仅作为排序键，后续真正写数据时再查 archetype
		for (const FMassEntityHandle& Entity : InEntities)
		{
			if (Entity.IsValid())
			{
				TrueIndices[NumValidEntities++] = Entity.Index;
			}
		}
	}

	TrueIndices.SetNum(NumValidEntities, EAllowShrinking::No);
	TrueIndices.Sort();  // 排序：后续折叠 range 的前提

#if DO_GUARD_SLOW
	if (DuplicatesHandling == NoDuplicates)
	{
		// ensure there are no duplicates. 
		// —— 调用方承诺无重复时，slow-guard 下仍校验一遍相邻元素；
		// 若发现重复，development 构建会自动切到 FoldDuplicates 路径以保证不 crash
		int32 PrevIndex = TrueIndices[0];
		for (int j = 1; j < TrueIndices.Num(); ++j)
		{
			checkf(TrueIndices[j] != PrevIndex, TEXT("InEntities contains duplicate while DuplicatesHandling is set to NoDuplicates"));
			if (TrueIndices[j] == PrevIndex)
			{
				// fix it, for development's sake
				DuplicatesHandling = FoldDuplicates;
				break;
			}
			PrevIndex = TrueIndices[j];
		}
	}
#endif // DO_GUARD_SLOW

	if (DuplicatesHandling == FoldDuplicates)
	{
		// 排序后重复元素紧挨在一起，线性扫描去重：遇到与 PrevIndex 相同则整段移除
		int32 PrevIndex = TrueIndices[0];
		for (int j = 1; j < TrueIndices.Num(); ++j)
		{
			if (TrueIndices[j] == PrevIndex)
			{
				// 向后统计一连串相同值的长度
				const int32 Num = TrueIndices.Num();
				int Skip = 0;
				while ((j + ++Skip) < Num && TrueIndices[j + Skip] == PrevIndex);
				
				// 批量移除这 Skip 个重复；不收缩容量以避免重分配
				TrueIndices.RemoveAt(j, Skip, EAllowShrinking::No);
				--j;  // 回退一个位置以重新检查
				continue;
			}
			PrevIndex = TrueIndices[j];
		}
	}

	// 把已排序、已去重的绝对索引交给核心折叠算法
	BuildEntityRanges(MakeStridedView<const int32>(TrueIndices));
}

// 单实体快速构造：直接算 range
FMassArchetypeEntityCollection::FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetype, const FMassEntityHandle EntityHandle)
	: Ranges({CreateRangeForEntity(InArchetype, EntityHandle)})
	, Archetype(InArchetype)
{	
}

FMassArchetypeEntityCollection::FMassArchetypeEntityCollection(FMassArchetypeHandle&& InArchetype, const FMassEntityHandle EntityHandle)
	: Ranges({CreateRangeForEntity(InArchetype, EntityHandle)})
	, Archetype(MoveTemp(InArchetype))
{	
}

/**
 * 【核心折叠算法】把排好序的绝对索引数组折叠成一串连续 range。
 *
 * 关键观察：在 archetype 里，chunk i 的槽位绝对索引范围是 [i*N, (i+1)*N) （N=NumEntitiesPerChunk）。
 * 因此一组已排序的绝对索引若落在同一 chunk 且相邻（A+1 == B），就应归入同一 range。
 *
 * 迭代每个索引 Index：
 *  1. 若 Index >= 当前 chunk 末尾 (ChunkEnd)，说明跨 chunk → 开新 range；
 *  2. 或 Index != PrevAbsoluteIndex+1，说明同 chunk 但索引不连续 → 开新 range；
 *  3. 否则把当前 range 的 SubchunkLen++。
 * 每开新 range 前先把上一条的 Length 写回。循环结束后再写一次最后一条的 Length。
 *
 * 时间复杂度 O(N)，N 为 TrueIndices 元素数量。
 */
void FMassArchetypeEntityCollection::BuildEntityRanges(TStridedView<const int32> TrueIndices)
{
	checkf(Ranges.Num() == 0, TEXT("Calling %s is valid only for initial configuration"), ANSI_TO_TCHAR(__FUNCTION__));

	const FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(Archetype);
	// ArchetypeData 不可用时使用 MAX_int32，等同于"永远不触发跨 chunk 边界"，让所有索引进同一 range
	const int32 NumEntitiesPerChunk = ArchetypeData ? ArchetypeData->GetNumEntitiesPerChunk() : MAX_int32;

	// the following block of code is splitting up sorted AbsoluteIndices into 
	// continuous chunks
	int32 ChunkEnd = INDEX_NONE;
	FArchetypeEntityRange DummyChunk;
	// 指向当前正在累积的 range；初始指向 DummyChunk 作占位，第一次进入分支会重新赋值
	FArchetypeEntityRange* SubChunkPtr = &DummyChunk;
	int32 SubchunkLen = 0;
	int32 PrevAbsoluteIndex = INDEX_NONE;
	for (const int32 Index : TrueIndices)
	{
		// if run across a chunk border or run into an index discontinuity 
		// —— 跨 chunk 边界 或 索引不连续：关闭上一条 range，开新一条
		if (Index >= ChunkEnd || Index != (PrevAbsoluteIndex + 1))
		{
			SubChunkPtr->Length = SubchunkLen;           // 写回上一条 range 的长度
			// note that both ChunkIndex and ChunkEnd will change only if AbsoluteIndex >= ChunkEnd
			const int32 ChunkIndex = Index / NumEntitiesPerChunk;
			ChunkEnd = (ChunkIndex + 1) * NumEntitiesPerChunk;
			SubchunkLen = 0;
			// new subchunk
			const int32 SubchunkStart = Index % NumEntitiesPerChunk;
			// 新增一个 range 并让 SubChunkPtr 指向它（GetRef 保证引用稳定到数组不扩容为止）
			SubChunkPtr = &Ranges.Add_GetRef(FArchetypeEntityRange(ChunkIndex, SubchunkStart));
		}
		++SubchunkLen;
		PrevAbsoluteIndex = Index;
	}

	// 收尾：写回最后一条 range 的长度
	SubChunkPtr->Length = SubchunkLen;
}

// 带 initialization 选项的构造：GatherAll 时等效"集合覆盖 archetype 全部实体"
FMassArchetypeEntityCollection::FMassArchetypeEntityCollection(const FMassArchetypeHandle& InArchetypeHandle, const EInitializationType Initialization)
	: Archetype(InArchetypeHandle)
{
	if (Initialization == EInitializationType::GatherAll)
	{
		check(InArchetypeHandle.IsValid());
		GatherChunksFromArchetype();
	}
}

FMassArchetypeEntityCollection::FMassArchetypeEntityCollection(TSharedPtr<FMassArchetypeData>& InArchetype, const EInitializationType Initialization)
	: Archetype(FMassArchetypeHelper::ArchetypeHandleFromData(InArchetype))
{	
	if (Initialization == EInitializationType::GatherAll)
	{
		check(InArchetype.IsValid());
		GatherChunksFromArchetype();
	}
}

/**
 * GatherAll 实现：为 archetype 的每个 chunk 生成一个 (ChunkIdx, 0, 0) range。
 * 约定 Length==0 表示"覆盖该 chunk 内全部实体到当前末尾"，processor 遍历时会按实际
 * NumInstances 展开。这样无需在构造时遍历所有槽位。
 */
void FMassArchetypeEntityCollection::GatherChunksFromArchetype()
{
	if (const FMassArchetypeData* ArchetypePtr = FMassArchetypeHelper::ArchetypeDataFromHandle(Archetype))
	{
		const int32 ChunkCount = ArchetypePtr->GetChunkCount();
		Ranges.Reset(ChunkCount);
		for (int32 i = 0; i < ChunkCount; ++i)
		{
			Ranges.Add(FArchetypeEntityRange(i));
		}
	}
}

// 诊断比较：archetype+版本+所有 range 都相等才算 same
bool FMassArchetypeEntityCollection::IsSame(const FMassArchetypeEntityCollection& Other) const
{
	if (Archetype != Other.Archetype || Ranges.Num() != Other.Ranges.Num())
	{
		return false;
	}

	for (int i = 0; i < Ranges.Num(); ++i)
	{
		if (Ranges[i] != Other.Ranges[i])
		{
			return false;
		}
	}
	return true;
}

/**
 * 把集合内的 ranges 重新展开成原始 FMassEntityHandle 列表（追加到 InOutHandles 末尾）。
 * 实现：委托给 FMassArchetypeData::ExportEntityHandles，其会逐 range memcpy 出 chunk 里的
 * entity 数组段。
 * 前置条件：集合关联的是"已创建"实体（非预留），并且 archetype 版本未过期。
 */
bool FMassArchetypeEntityCollection::ExportEntityHandles(TArray<FMassEntityHandle>& InOutHandles) const
{
	if (Ranges.IsEmpty())
	{
		return false;
	}

	const int32 InOutHandlesStartingNum = InOutHandles.Num();
	const FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(Archetype);
	if (testableEnsureMsgf(ArchetypeData, TEXT("This operation is supported only for collections containing created entities (i.e. not 'reserved'")))
	{
		// 版本过期时 range 里的 chunk 内坐标可能已错位，拒绝导出
		testableCheckfReturn(IsUpToDate(), return false, TEXT("The entity collection is out of date. Unable to reliably generate handle list."));

		ArchetypeData->ExportEntityHandles(Ranges, InOutHandles);
		ensureMsgf(InOutHandlesStartingNum != InOutHandles.Num(), TEXT("Exporting non-empty ranges resulted in no entity handles"));
	}

	return InOutHandlesStartingNum != InOutHandles.Num();
}

// O(n) 重叠检测：只比较相邻对；前提是 ranges 已按 operator< 排序
bool FMassArchetypeEntityCollection::DoesContainOverlappingRanges(FConstEntityRangeArrayView Ranges)
{
	for (int RangeIndex = 1; RangeIndex < Ranges.Num(); ++RangeIndex) 
	{
		if (Ranges[RangeIndex-1].IsOverlapping(Ranges[RangeIndex]))
		{
			return true;
		}
	}
	return false;
}

#if WITH_MASSENTITY_DEBUG
// debug only：计算集合内实体总数。
// 有 archetype 时使用 CalculateRangeLength 解析"Length<=0"语义（到 chunk 末尾）；
// 无 archetype 时只能相信 Length 字段是正数。
int32 FMassArchetypeEntityCollection::DebugCountEntities() const
{
	int32 EntitiesCount = 0;

	if (const FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(Archetype))
	{
		for (const FArchetypeEntityRange& Range : Ranges)
		{
			EntitiesCount += ArchetypeData->CalculateRangeLength(Range);
		}
	}
	else
	{
		for (const FArchetypeEntityRange& Range : Ranges)
		{
			ensureAlwaysMsgf(Range.Length > 0, TEXT("Empty range length in archetype-less collection is unexpected. There's no way to determine what it means"));
			EntitiesCount += Range.Length;
		}
	}

	return EntitiesCount;
}
#endif // WITH_MASSENTITY_DEBUG

//-----------------------------------------------------------------------------
// FMassArchetypeEntityCollectionWithPayload
//-----------------------------------------------------------------------------
/**
 * 【批量 set fragment values 的关键入口】
 *
 * 输入：
 *  - Entities: 任意顺序、可能跨多个 archetype、可能重复的 handle 数组；
 *  - Payload : 类型擦除的列式 payload view（和 Entities 一一对应，Entities[i] 的数据在
 *              Payload 的每一列的第 i 个元素）。
 *
 * 输出：OutEntityCollections，每个元素对应一个 archetype，且其 EntityCollection 的 range 顺序
 *       与 PayloadSlice 的下标保持严格对应。这样后续 BatchSetFragmentValues 可以按 range 顺序
 *       memcpy 每一列到 archetype 的 chunk 里。
 *
 * 关键技术：不能直接 sort Entities（因为 Payload 是外部数据的 view，不能丢元素），
 * 于是排序时"同步 swap payload 各列"，借助 UE::Mass::Utils::AbstractSort 的 Swap 回调。
 * 去重时也用"swap 到末尾然后忽略"，payload 元素保留在 view 里只是被屏蔽掉。
 */
void FMassArchetypeEntityCollectionWithPayload::CreateEntityRangesWithPayload(const FMassEntityManager& EntityManager, const TConstArrayView<FMassEntityHandle> Entities
	, const FMassArchetypeEntityCollection::EDuplicatesHandling DuplicatesHandling, FMassGenericPayloadView Payload
	, TArray<FMassArchetypeEntityCollectionWithPayload>& OutEntityCollections)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass_CreateEntityRangesWithPayload");

	check(Payload.Num() > 0);
	// 前置校验：payload 每一列的元素数必须等于 entity 数
	for (const FStructArrayView& Element : Payload.Content)
	{
		check(Entities.Num() == Element.Num());
	}

	// 排序用的复合键：(ArchetypeIndex, TrueIndex)
	struct FEntityInArchetype
	{
		int32 ArchetypeIndex = INDEX_NONE;  // 所属 archetype 在局部 Archetypes 数组里的下标
		int32 TrueIndex = INDEX_NONE;       // 在该 archetype 内的绝对索引

		bool operator==(const FEntityInArchetype& Other) const
		{
			return ArchetypeIndex == Other.ArchetypeIndex && TrueIndex == Other.TrueIndex;
		}

		/** @return whether A should come before B in an ordered collection */
		// 排序键：ArchetypeIndex 降序（INDEX_NONE 最大，让"无效实体"排到末尾方便丢弃），
		// 同一 archetype 内按 TrueIndex 升序（和后续 BuildEntityRanges 对有序输入的假设一致）
		static bool Compare(const FEntityInArchetype& A, const FEntityInArchetype& B)
		{
			// using "greater" to ensure INDEX_NONE archetypes end up at the end of the collection
			return A.ArchetypeIndex > B.ArchetypeIndex || (A.ArchetypeIndex == B.ArchetypeIndex && A.TrueIndex < B.TrueIndex);
		}
	};

	TArray<FEntityInArchetype> EntityData;
	EntityData.AddUninitialized(Entities.Num());

	// 本地记录遇到的 archetype，Count 用于后续切分 PayloadSlice
	struct FArchetypeInfo
	{
		// @todo using a handle here is temporary. Once ArchetypeHandle switches to using an index we'll use that instead
		FMassArchetypeHandle Archetype;
		int32 Count = 0;  // 属于本 archetype 的实体数（去重后）

		bool operator==(const FMassArchetypeHandle& InArchetype) const
		{
			return Archetype == InArchetype;
		}
		bool operator==(const FArchetypeInfo& Other) const
		{
			return Archetype == Other.Archetype;
		}
	};
	TArray<FArchetypeInfo> Archetypes;

	// ---- 阶段 1：为每个实体填充 FEntityInArchetype；累计每 archetype 的 Count ----
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		const FMassEntityHandle& Entity = Entities[i];
		if (EntityManager.IsEntityValid(Entity))
		{
			// using Unsafe since we just checked that the entity is valid
			const FMassArchetypeHandle ArchetypeHandle = EntityManager.GetArchetypeForEntityUnsafe(Entity);
			const FMassArchetypeData* ArchetypePtr = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
		
			// @todo if FMassArchetypeHandle used indices the look up would be a lot faster
			// —— 线性查找本地 Archetypes 表；TODO 注释提示未来把 handle 换成索引可以加速到 O(1)
			int32 ArchetypeIndex = INDEX_NONE;
		
			if (Archetypes.Find(FArchetypeInfo{ ArchetypeHandle, 0 }, ArchetypeIndex) == false)
			{
				ArchetypeIndex = Archetypes.Add({ ArchetypeHandle, 0 });
			}
			++Archetypes[ArchetypeIndex].Count;
			// TrueIndex：有 archetype 就查真实绝对下标，否则回退到 Entity.Index（仅用于排序分组）
			EntityData[i] = { ArchetypeIndex, ArchetypePtr ? ArchetypePtr->GetInternalIndexForEntityChecked(Entity.Index) : Entity.Index };
		}
		else
		{
			// for invalid entities we create an entry that will result in all of them batched together 
			// (due to having the same archetype index, INDEX_NONE). Since the main logic loop below relies on entries 
			// in Archetypes and the "invalid" EntityData doesn't correspond to any, all the invalid entities 
			// will be silently filtered out.
			// —— 无效实体用全 INDEX_NONE；排序后这些会扎堆在末尾，main loop 按 Archetypes 遍历时就会自然跳过
			EntityData[i] = FEntityInArchetype();
			UE_LOG(LogMass, Warning, TEXT("%hs: Invalid entity handle passed in. Ignoring it, but check your code to make sure you don't mix synchronous entity-mutating Mass API function calls with Mass commands")
				, __FUNCTION__);
		}
	}

	// ---- 阶段 2：AbstractSort —— 排序时同步 swap payload 保持对齐 ----
	// A paranoid programmer might point out that there are no guarantees that a sorting algorithm will compare all elements.
	// While that's true we make an assumption here, that the elements next to each other will in fact all get compared
	// and since all we care about with `bDuplicatesFound` is whether same elements exist (that will be right next to each other
	// in the final lineup) we feel safe in the assumption.
	// —— 借助排序过程中相邻元素必然被比较这一性质，顺便检测是否存在重复（严格证明需要算法保证）
	bool bDuplicatesFound = false;
	UE::Mass::Utils::AbstractSort(Entities.Num(), [&EntityData, &bDuplicatesFound](const int32 LHS, const int32 RHS)
		{
			bDuplicatesFound = bDuplicatesFound || (EntityData[LHS] == EntityData[RHS]);
			return FEntityInArchetype::Compare(EntityData[LHS], EntityData[RHS]);
		}
		, [&EntityData, &Payload](const int32 A, const int32 B)
		{
			// 交换时要同时 swap payload 的对应元素，保证 EntityData[i] 与 payload 第 i 个仍对应
			::Swap(EntityData[A], EntityData[B]);
			Payload.Swap(A, B);
		});
	ensureMsgf(bDuplicatesFound == false || (DuplicatesHandling != FMassArchetypeEntityCollection::NoDuplicates)
		, TEXT("Caller declared lack of duplicates in the input data, but duplicates have been found"));

#if !UE_BUILD_SHIPPING
	// in non shipping builds we still want to verify that the assumption expressed in bDuplicatesFound comment above
	// is correct
	// —— 非 shipping：显式校验排序结果里，如果 bDuplicatesFound==false 则相邻元素确实两两不同
	if (!bDuplicatesFound && (DuplicatesHandling == FMassArchetypeEntityCollection::FoldDuplicates))
	{
		for (int32 EntryIndex = 0; EntryIndex < EntityData.Num() - 1; ++EntryIndex)
		{
			checkf(EntityData[EntryIndex] != EntityData[EntryIndex + 1], TEXT("Assumption regarding comparison between identical elements while sorting is wrong!"));
		}
	}
#endif // !UE_BUILD_SHIPPING

	// ---- 阶段 3：去重 —— 把重复条目在 EntityData 中删除，并把对应 payload 列 "swap 到末尾屏蔽" ----
	if (bDuplicatesFound && (DuplicatesHandling == FMassArchetypeEntityCollection::FoldDuplicates))
	{
		// we cannot remove elements from Payload, since it's a view to existing data, we need to sort the data in 
		// such a way that all the duplicates end up at the end of the view. We can then ignore the appropriate
		// number of elements.
		// —— payload 是外部视图不可删元素；这里通过 SwapElementsToEnd 把重复元素推到末尾，
		// 再用 ArchetypeInfo.Count 计数减掉，相当于"裁掉"末尾若干列
		
		// processing Num - 1 elements since there's no point in checking the last one - there's nothing to compare it against
		for (int32 EntryIndex = 0; EntryIndex < EntityData.Num() - 1; ++EntryIndex)
		{	
			FEntityInArchetype& Entry = EntityData[EntryIndex];
			if (Entry.ArchetypeIndex == INDEX_NONE)
			{
				// we're reached INDEX_NONE archetypes, which are at the end of EntityData
				// breaking since there's nothing more to process. 
				// —— 已经进入无效实体段，后面全是 INDEX_NONE，无需再处理
				break;
			}
			else if (Entry != EntityData[EntryIndex + 1])
			{
				continue;
			}

			// 统计从 EntryIndex+1 开始有多少个等价重复
			int32 DuplicateIndex = EntryIndex + 1;
			while (DuplicateIndex + 1 < EntityData.Num() && Entry == EntityData[DuplicateIndex + 1])
			{
				++DuplicateIndex;
			};

			const int32 NumDuplicates = DuplicateIndex - EntryIndex;

			EntityData.RemoveAt(EntryIndex + 1, NumDuplicates, EAllowShrinking::No);
			Payload.SwapElementsToEnd(EntryIndex + 1, NumDuplicates);  // payload 不删，仅挪到末尾
			// even though we don't remove the elements from payload we later limit the number of elements used with
			// ArchetypeInfo.Count, so we need to update that
			Archetypes[Entry.ArchetypeIndex].Count -= NumDuplicates;
		}
	}

	// ---- 阶段 4：按 archetype 切分 → 为每个 archetype 生成 Collection + PayloadSlice ----
	int32 ProcessedEntitiesCount = 0;
	// processing from the back since that's how EntityData is sorted - higher-index archetypes come first
	// —— 由于排序时 ArchetypeIndex 是降序，最大的 index 在最前面；我们反向遍历 Archetypes 数组
	for (int32 ArchetypeIndex = Archetypes.Num() - 1; ArchetypeIndex >= 0; --ArchetypeIndex)
	{
		FArchetypeInfo& ArchetypeInfo = Archetypes[ArchetypeIndex];
		// 切出属于本 archetype 的连续段 [ProcessedEntitiesCount, +Count)
		TArrayView<FEntityInArchetype> EntityDataSubset = MakeArrayView(&EntityData[ProcessedEntitiesCount], ArchetypeInfo.Count);
		ensure(EntityDataSubset[0].ArchetypeIndex == ArchetypeIndex);
		ensure(EntityDataSubset.Last().ArchetypeIndex == ArchetypeIndex);
		// 从复合键里抽出 TrueIndex 视图给 BuildEntityRanges
		TStridedView<int32> TrueIndices = MakeStridedView(EntityDataSubset, &FEntityInArchetype::TrueIndex);

		// 同步切分 payload：每列都裁出相同的 [offset, count) 切片
		FMassGenericPayloadViewSlice PayloadSubView(Payload, ProcessedEntitiesCount, ArchetypeInfo.Count);

		// 产出：Collection 由 BuildEntityRanges 折叠 TrueIndices 得到
		OutEntityCollections.Add(FMassArchetypeEntityCollectionWithPayload(ArchetypeInfo.Archetype, TrueIndices, MoveTemp(PayloadSubView)));

		ProcessedEntitiesCount += ArchetypeInfo.Count;
	}
}

// 私有构造：交给 BuildEntityRanges 折叠 TrueIndices 后，保存 PayloadSlice
FMassArchetypeEntityCollectionWithPayload::FMassArchetypeEntityCollectionWithPayload(const FMassArchetypeHandle& InArchetype, TStridedView<const int32> TrueIndices, FMassGenericPayloadViewSlice&& InPayloadSlice)
	: Entities(InArchetype, FMassArchetypeEntityCollection::DoNothing)
	, PayloadSlice(InPayloadSlice)
{
	Entities.BuildEntityRanges(TrueIndices);
}

//-----------------------------------------------------------------------------
// FMassEntityInChunkDataHandle
//-----------------------------------------------------------------------------
// 校验本 handle 是否仍有效：委托给 archetype 检查 ChunkIndex 合法 + SerialNumber 一致
bool FMassEntityInChunkDataHandle::IsValid(const FMassArchetypeData* ArchetypeData) const
{
	return ArchetypeData != nullptr
		&& ArchetypeData->IsValidHandle(*this);
}

bool FMassEntityInChunkDataHandle::IsValid(const FMassArchetypeHandle& ArchetypeHandle) const
{
	return IsValid(FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle));
}
