// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityCollection.h"
#include "MassEntityUtils.h"

namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// FEntityCollection
	//-----------------------------------------------------------------------------
	// 中文：以下三个构造函数是 "default + AppendXxx" 的便利封装。

	// 中文：移动构造一个 archetype collection 进来——AppendCollection 内部会 Export 出 handle 并 seed 缓存。
	FEntityCollection::FEntityCollection(FMassArchetypeEntityCollection&& InEntityCollection)
	{
		AppendCollection(Forward<FMassArchetypeEntityCollection>(InEntityCollection));
	}

	// 中文：拷贝版本——同上，但 ExportEntityHandles 内部会做 const 引用路径。
	FEntityCollection::FEntityCollection(const FMassArchetypeEntityCollection& InEntityCollection)
	{
		AppendCollection(InEntityCollection);
	}

	// 中文：仅 handles 版本——直接拷贝进 EntityHandles。CachedCollections 留空，
	//   首次 GetUpToDate... 时 lazy 重建（这种情况下未指定去重策略，因构造时未触发 dirty 标记，
	//   默认 CollectionCreationDuplicatesHandling=NoDuplicates，调用方需自行确保无重复，否则建议构造后调一次
	//   UpdateAndRemoveDuplicates(bForceOperation=true)）。
	FEntityCollection::FEntityCollection(const TConstArrayView<FMassEntityHandle> InEntityHandles)
		: EntityHandles(InEntityHandles)
	{	
	}

	// 中文：handles + collection seeding 版本——调用方保证两者一致，避免首次 GetUpToDate... 触发重建。
	FEntityCollection::FEntityCollection(const TConstArrayView<FMassEntityHandle> InEntityHandles, FMassArchetypeEntityCollection&& InEntityCollection)
		: EntityHandles(InEntityHandles)
	{
		CachedCollections.Add(Forward<FMassArchetypeEntityCollection>(InEntityCollection));
	}

	// 中文：lazy 重建的核心入口。语义：
	//   - 先 IsUpToDate（同时会清掉过期缓存）；
	//   - 仍不是 up-to-date（即缓存被清空了），调 Utils::CreateEntityCollections 把 EntityHandles 按 archetype 分桶；
	//   - 重新填满 CachedCollections。
	void FEntityCollection::ConditionallyUpdate(const FMassEntityManager& EntityManager) const
	{
		if (IsUpToDate() == false)
		{
			ensureMsgf(CachedCollections.IsEmpty(), TEXT("Failing IsUpToDate test should result in clearing out the cached collections"));
			Utils::CreateEntityCollections(EntityManager, EntityHandles
				, CollectionCreationDuplicatesHandling
				, CachedCollections);
		}
	}

	// 中文：批量追加 handle，无对应 collection。三步走：
	//   1) Append 到真值表；
	//   2) MarkDirty 把缓存清掉（下一次重建）；
	//   3) 切到 FoldDuplicates 模式——因为我们无法保证新 handle 不与既有 handle 重复。
	void FEntityCollection::AppendHandles(TConstArrayView<FMassEntityHandle> Handles)
	{
		EntityHandles.Append(Handles);
		MarkDirty();
		CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::FoldDuplicates;
	}

	// 中文：带 collection 提示的追加——能并入则并入缓存，否则 ConditionallyStoreCollection 会丢弃缓存。
	//   注意：这里 **没有** 切换 CollectionCreationDuplicatesHandling，因为如果 collection 仍能并入，
	//   就意味着调用方保证 handle 无重复（缓存仍完整即没引入重复）。
	//   bug 提示：若调用前缓存已过期但还未被检测，bWasEmpty=false 且 CachedCollections 非空但 stale，
	//   ConditionallyStoreCollection 仍会把 InEntityCollection 并入——这一并入的 collection 不会过期，
	//   但与之前的 stale collection 共存反而把 stale 数据"延寿"了（IsUpToDate 才会触发清理）。
	void FEntityCollection::AppendHandles(TConstArrayView<FMassEntityHandle> Handles, FMassArchetypeEntityCollection&& InEntityCollection)
	{
		const bool bWasEmpty = EntityHandles.IsEmpty();
		EntityHandles.Append(Handles);
		ConditionallyStoreCollection(bWasEmpty, Forward<FMassArchetypeEntityCollection>(InEntityCollection));
	}

	// 中文：移动版批量追加，语义同第一个 AppendHandles，但避免一次拷贝。
	void FEntityCollection::AppendHandles(TArray<FMassEntityHandle>&& Handles)
	{
		EntityHandles.Append(Forward<TArray<FMassEntityHandle>>(Handles));
		MarkDirty();
		CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::FoldDuplicates;
	}

	// 中文：单个 handle 追加。性能上不如先攒数组再一次性 AppendHandles。
	void FEntityCollection::AddHandle(FMassEntityHandle Handle)
	{
		EntityHandles.Emplace(Handle);
		MarkDirty();
		CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::FoldDuplicates;
	}

	// 中文：规整化操作。仅当 (bForceOperation || 当前模式==FoldDuplicates) 时才执行：
	//   - 否则我们认为 EntityHandles 没有重复，规整无意义，省一次重建。
	// 步骤：
	//   1) 清缓存；
	//   2) 调 CreateEntityCollections（用 FoldDuplicates 模式做去重 + 分桶）；
	//   3) 把 EntityHandles 重置，再从 CachedCollections 里挨个 ExportEntityHandles 回来——
	//      这样 EntityHandles 顺序变成 "按 archetype 分组、组内连续" 的形式，而且无重复；
	//   4) 切到 NoDuplicates 模式，下次重建可省去重；
	//   5) 返回是否检测到重复（前后 handle 数变化）。
	// 不变量：EntityHandles.Num() <= 起始 handle 数（重建只会去重，不会凭空增加）。
	bool FEntityCollection::UpdateAndRemoveDuplicates(const FMassEntityManager& EntityManager, bool bForceOperation)
	{
		const int32 StartingHandlesCount = EntityHandles.Num();
		if (bForceOperation || CollectionCreationDuplicatesHandling == FMassArchetypeEntityCollection::FoldDuplicates)
		{
			CachedCollections.Reset();

			Utils::CreateEntityCollections(EntityManager, EntityHandles
				, FMassArchetypeEntityCollection::FoldDuplicates
				, CachedCollections);

			EntityHandles.Reset();
			for (const FMassArchetypeEntityCollection& Collection : CachedCollections)
			{
				Collection.ExportEntityHandles(EntityHandles);
			}

			CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::NoDuplicates;

			ensureMsgf(EntityHandles.Num() <= StartingHandlesCount, TEXT("We don't expect to gain new handles"));
		}
		return StartingHandlesCount != EntityHandles.Num();
	}

	// 中文：缓存新鲜度检测。
	//   - 不变量校验：(handle 空) <=> (cache 空)；不一致直接 ensure 并清 cache 视为非新鲜；
	//   - 否则逐个 collection 询问 IsUpToDate，任意一个过期 -> Reset 整个 cache 返回 false。
	//   注意：此函数会 **副作用** 地清空过期 cache（声明 const + cache 是 mutable）；
	//   这是一种"探测时顺便修复状态"的设计。
	bool FEntityCollection::IsUpToDate() const
	{
		if (CachedCollections.IsEmpty() != EntityHandles.IsEmpty())
		{
			ensureMsgf(CachedCollections.IsEmpty(), TEXT("Unexpected development. We don't expect to have cached collections without any stored handles"));
			CachedCollections.Reset();
			return false;
		}

		for (const FMassArchetypeEntityCollection& Collection : GetCachedPerArchetypeCollections())
		{
			if (Collection.IsUpToDate() == false)
			{
				CachedCollections.Reset();
				return false;
			}
		}
		return true;
	}
}