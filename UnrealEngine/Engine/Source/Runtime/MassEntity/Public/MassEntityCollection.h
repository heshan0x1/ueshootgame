// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "MassEntityHandle.h"
#include "MassArchetypeTypes.h"

namespace UE::Mass
{
	/**
	 * Type represents a collection of arbitrary EntityHandles. Under the hood the type stores also
	 * an array of FMassArchetypeEntityCollection instances. These cached collections can be tested for 
	 * being up to date, and re-created on demand, based on stored entity handles.
	 * 
	 * The type is intended to be used to collect entities available through different means: individual
	 * handles, handle arrays and or FMassArchetypeEntityCollection instances. Such accumulated handles
	 * can at any moment be turned into an array of up-to-date FMassArchetypeEntityCollection instance,
	 * which in turn is how entity sets are provided to MassEntityManager's batched API.
	 *
	 * The biggest win while using this type is that the user doesn't have to worry about FMassArchetypeEntityCollection
	 * instances going out of date (which happens whenever the target archetype is touched in a way that
	 * changes internal entity indices). The type automatically updates the collections and caches the result.
	 *
	 * 中文说明：
	 *   `FEntityCollection` 是一个 **"任意 entity handle 累积器"**，承担两个核心职责：
	 *     1) 存储一份 **真值表 (source of truth)** —— `EntityHandles`，可以是任意 archetype 的 entity；
	 *     2) **按需 lazy 重建** 一份按 archetype 分桶的 `TArray<FMassArchetypeEntityCollection>` 缓存，
	 *        以便喂给 EntityManager 的批量 API（DestroyEntities / BatchAdd... 等都按 "同一 archetype 一批" 的形式接受输入）。
	 *
	 *   它和 `FMassArchetypeEntityCollection` 的关键区别：
	 *     - `FMassArchetypeEntityCollection`：**要求**所有 entity 都必须属于同一个 archetype；
	 *       它内部记录 (chunk index, slot range) 而不是 entity handle，因此 archetype 内部布局变化（chunk 重排、
	 *       entity 添加/删除/迁移）会让它"过期"，需要 IsUpToDate 检测并重建；
	 *     - `FEntityCollection`：**不要求** archetype 一致，可以无脑往里塞各种 handle，最终在 GetCollections 时
	 *       自动按 archetype 打散并打包成多个 `FMassArchetypeEntityCollection`。
	 *
	 *   失效检测策略：
	 *     - 用户调用 mutating API（AppendHandles / AddHandle 等）后，缓存被 MarkDirty()（即 Reset）；
	 *     - 用户调用 IsUpToDate / GetUpToDatePerArchetypeCollections 时，会再次询问每个缓存 collection 是否仍然
	 *       有效（archetype 内部 version / chunk serial 变化时为否），过期则丢弃缓存；
	 *     - 真正取用时（GetUpToDatePerArchetypeCollections / ConsumeArchetypeCollections）才 lazy 重建。
	 *
	 *   典型使用流：
	 *       FEntityCollection C;
	 *       C.AppendHandles(SomeHandles);
	 *       C.AddHandle(AnotherHandle);
	 *       // ... 期间 archetype 可能被改动，无所谓
	 *       EntityManager.BatchDestroyEntities(C.GetUpToDatePerArchetypeCollections(EntityManager));
	 */
	struct FEntityCollection
	{
		FEntityCollection() = default;

		/**
		 * The following constructor are equivalent to using the default constructor and subsequently
		 * calling AppendCollection or AppendHandles.
		 *
		 * 中文说明：以下四个构造函数等价于"默认构造 + AppendCollection / AppendHandles"。
		 *   - 移动版本：把传入的 archetype collection 直接 take-ownership，并从中导出 entity handles；
		 *   - const 引用版本：拷贝（语义安全但成本稍高）；
		 *   - handles 版本：仅塞 handle，缓存视为"无 collection"，首次 GetUpToDate... 时再分桶；
		 *   - (handles + collection) 双参版：调用方已经知道这批 handle 全在该 collection 描述的 archetype 里，
		 *     可一次性 seeding，省一次重建。
		 */
		MASSENTITY_API explicit FEntityCollection(FMassArchetypeEntityCollection&& InEntityCollection);
		MASSENTITY_API explicit FEntityCollection(const FMassArchetypeEntityCollection& InEntityCollection);
		MASSENTITY_API explicit FEntityCollection(const TConstArrayView<FMassEntityHandle> InEntityHandles);
		MASSENTITY_API FEntityCollection(const TConstArrayView<FMassEntityHandle> InEntityHandles, FMassArchetypeEntityCollection&& InEntityCollection);

		//-----------------------------------------------------------------------------
		// mutating API
		// 中文：所有 mutating API 都会让缓存失效（直接或间接），下一次 GetUpToDate... 会 lazy 重建。
		//-----------------------------------------------------------------------------
		/**
		 * Appends Handles to stored EntityHandles. Results in marking cached FMassArchetypeEntityCollection as dirty.
		 *
		 * 中文：批量追加 handle。因为追加的 handle 可能跨多个 archetype，
		 *   未来重建时必须用 FoldDuplicates 模式保证安全；同时缓存 dirty。
		 */
		MASSENTITY_API void AppendHandles(TConstArrayView<FMassEntityHandle> Handles);

		/**
		 * Appends Handles to stored EntityHandles.
		 * The second parameter is relevant if, at the moment of calling, the cached FMassArchetypeEntityCollection instances
		 * are in sync with stored entity handles (meaning all entities stored in EntityHandles are also captured by one of
		 * FMassArchetypeEntityCollection). If that's the case then the InEntityCollection gets stored along with
		 * existing collections. Otherwise, InEntityCollection will be ignored. 
		 *
		 * 中文：
		 *   "带 collection 提示的"追加：若调用方明确知道这批 handle 与某个 archetype collection 一一对应，
		 *   且当前缓存仍然完整（每个已存 handle 都在某个缓存 collection 里），就把 InEntityCollection 顺手并入缓存，
		 *   下一次取用时省一次重建；否则忽略 InEntityCollection、保留 handle 即可（缓存被认为不完整时就丢掉）。
		 */
		MASSENTITY_API void AppendHandles(TConstArrayView<FMassEntityHandle> Handles, FMassArchetypeEntityCollection&& InEntityCollection);

		/**
		 * Appends Handles to stored EntityHandles. Results in marking cached FMassArchetypeEntityCollection as dirty.
		 *
		 * 中文：移动语义版本——直接吃掉外部数组。语义上等价于第一个 AppendHandles。
		 */
		MASSENTITY_API void AppendHandles(TArray<FMassEntityHandle>&& Handles);

		/**
		 * Appends the Handle to stored EntityHandles. Results in marking cached FMassArchetypeEntityCollection as dirty.
		 *
		 * 中文：单个 handle 追加。同样会让缓存 dirty 并把去重模式置为 FoldDuplicates。
		 *   注意：连续小批量 AddHandle 会反复 dirty 缓存，性能上不如先攒成数组再 AppendHandles 一次。
		 */
		MASSENTITY_API void AddHandle(FMassEntityHandle Handle);

		/**
		 * Based on the provided FMassArchetypeEntityCollection creates an array of entity handles and stores them. 
		 * If up to this point the cached FMassArchetypeEntityCollection-s are consistent with stored EntityHandles
		 * then InEntityCollection gets stored as well, and stored collections are not marked as dirty.
		 *
		 * 中文模板说明：
		 *   传入一个 archetype collection（按值/移动皆可，由 requires 限制类型必为 FMassArchetypeEntityCollection）。
		 *   1) 先把 collection 中的 handle ExportEntityHandles 到 EntityHandles；
		 *   2) 再走 ConditionallyStoreCollection：
		 *      - 如果之前是空的或缓存仍完整，则把 collection 也并进缓存（甚至与最后一个 same-archetype 的合并）；
		 *      - 否则丢掉缓存（让下一次 GetUpToDate... 重建）。
		 *   这个分支让 "已经按 archetype 整理好的批" 的累积具有 O(handles) 而不是 O(handles*log) 的代价。
		 */
		template<typename T> requires std::is_same_v<typename TDecay<T>::Type, FMassArchetypeEntityCollection>
		void AppendCollection(T&& InEntityCollection)
		{
			if (UNLIKELY(InEntityCollection.IsEmpty()))
			{
				return;
			}

			const bool bWasEmpty = EntityHandles.IsEmpty();
			if (InEntityCollection.ExportEntityHandles(EntityHandles))
			{
				ConditionallyStoreCollection(bWasEmpty, Forward<T>(InEntityCollection));
			}
		}

		/**
		 * Results in duplicate handles being removed from EntityHandles, the cached collections being up-to-date
		 * and CollectionCreationDuplicatesHandling being set to FMassArchetypeEntityCollection::NoDuplicates
		 * 
		 * @param bForceOperation by default the entity handles will be re-exported only if
		 *	CollectionCreationDuplicatesHandling == FMassArchetypeEntityCollection::FoldDuplicates
		 *	(which means we cannot rule out that there are duplicates). Using bForceOperation = true
		 *	will perform the operation regardless.
		 *	
		 * @return whether any duplicates were detected
		 *
		 * 中文：
		 *   规整化操作——一次性"重建并去重"，事后内部状态变成：
		 *     - EntityHandles 已去重（且已按 archetype 顺序排过）；
		 *     - 缓存 CachedCollections 是新鲜的；
		 *     - CollectionCreationDuplicatesHandling 切成 NoDuplicates，下一次重建可以跳过去重逻辑省成本。
		 *   适用于：在跑很多 batch 操作前做一次"清算"，把后续 GetUpToDate... 都变得便宜。
		 */
		MASSENTITY_API bool UpdateAndRemoveDuplicates(const FMassEntityManager& EntityManager, bool bForceOperation = false);

		//-----------------------------------------------------------------------------
		// state-querying API
		//-----------------------------------------------------------------------------
		/**
		 * 中文：把缓存数组直接清掉，标记需要下次重建。
		 *   注意：EntityHandles 不动，handle 仍是真值。
		 */
		void MarkDirty()
		{
			CachedCollections.Reset();
		}

		/**
		 * 中文：判空。同时做不变量校验：缓存非空但 handle 为空是"不该出现的状态"，发现就 ensure。
		 */
		bool IsEmpty() const
		{
			ensureMsgf(!(EntityHandles.Num() == 0 && CachedCollections.Num() != 0), TEXT("Stored entity array is empty while there are stored collections. This is unexpected."));
			return EntityHandles.IsEmpty();
		}

		/**
		 * Checks if cached collection data is up to date
		 * If CachedCollections are not up-to-date we reset them to cache the information (and make the subsequent tests cheaper)
		 * Note that, depending on the contents, the test might be non-trivial. Use responsibly.
		 *
		 * 中文：
		 *   检查缓存是否仍然是"新鲜的"。
		 *   - 如果缓存是空的（handle 也空），就视为新鲜；
		 *   - 否则逐个询问每个缓存 collection 自己 IsUpToDate，任意一个过期 -> 直接 Reset 整个缓存并返回 false。
		 *   注意：调用本函数会有一定开销（每个 collection 都要做版本检查），不要在 hot loop 里反复调用。
		 */
		MASSENTITY_API bool IsUpToDate() const;

		//-----------------------------------------------------------------------------
		// data reading API
		//-----------------------------------------------------------------------------
		/** 中文：以视图形式暴露真值表。返回值在下一次 mutate 之前一直有效。*/
		TConstArrayView<FMassEntityHandle> GetEntityHandlesView() const
		{
			return EntityHandles;
		}

		/**
		 * Retrieves the view to current contents of CachedCollections, which may be out of date.
		 * If you need valid, up-to-date collections call GetUpToDatePerArchetypeCollections instead.
		 *
		 * 中文：返回 **当前缓存** 视图——可能过期、可能是空的。仅适合性能敏感且容忍 stale 的路径。
		 */
		TConstArrayView<FMassArchetypeEntityCollection> GetCachedPerArchetypeCollections() const
		{
			return CachedCollections;
		}

		/**
		 * Fetches up-to-date FMassArchetypeEntityCollection instances matching stored
		 * entity handles.
		 *
		 * 中文：lazy 接口的主要入口——必要时重建缓存，再返回视图。
		 *   通常给 EntityManager 的批量 API 用：
		 *       Mgr.BatchDestroyEntities(C.GetUpToDatePerArchetypeCollections(Mgr));
		 *   返回视图在下一次 mutate / 重建之前有效。
		 */
		TConstArrayView<FMassArchetypeEntityCollection> GetUpToDatePerArchetypeCollections(const FMassEntityManager& EntityManager) const
		{
			ConditionallyUpdate(EntityManager);
			return CachedCollections;
		}

		/**
		 * Updates cached archetype collections and returns the container with move semantics
		 *
		 * 中文：
		 *   "消费式"接口——必要时重建，然后 **move** 出整个 collections 数组。仅当本对象是右值时可调用（&& 限定）。
		 *   调用后 CachedCollections 变空，等价于一次性把所有权转走。适合"消费一次就丢"的临时聚合。
		 */
		TArray<FMassArchetypeEntityCollection> ConsumeArchetypeCollections(const FMassEntityManager& EntityManager) &&
		{
			ConditionallyUpdate(EntityManager);
			return MoveTemp(CachedCollections);
		}

	private:
		/**
		 * 中文（私有辅助）：决定是否把新加入的 collection 并进缓存。
		 *   核心策略——只有当 "之前是空的" 或 "缓存仍完整" 时才并入；否则丢弃缓存（不一致的状态比 stale 更糟）。
		 *   优化分支：若末尾缓存项与新 collection 是同一 archetype，则直接 Append 合并，避免数组膨胀。
		 *   注意 todo：这里依赖一个全局不变量——只要往 EntityHandles 加了"无对应 collection"的 handle 就必须 Reset 缓存
		 *   （MarkDirty / AppendHandles 会做）。
		 */
		template<typename T> requires std::is_same_v<typename TDecay<T>::Type, FMassArchetypeEntityCollection>
		void ConditionallyStoreCollection(const bool bWasEmpty, T&& InEntityCollection)
		{
			// if there was no previous data, or the data was "complete", meaning we had collections for stored handles
			// Note: this condition only works as expected if we make sure that adding handles without associated collections
			// resets the CachedCollections array
			// @todo add unit tests to ensure this behavior
			if (bWasEmpty || !CachedCollections.IsEmpty())
			{
				if (!CachedCollections.IsEmpty() && CachedCollections.Last().IsSameArchetype(InEntityCollection))
				{
					// merge with the last collection since it's the same archetype.
					// 中文：与末尾缓存项是同一 archetype -> 直接 Append 进去。
					CachedCollections.Last().Append(Forward<T>(InEntityCollection));
				}
				else
				{
					CachedCollections.Emplace(Forward<T>(InEntityCollection));
				}
			}
			else
			{
				// 中文：之前已经存在"无 collection 的 handle"——缓存已经不完整，再加进去也没意义，直接 Reset。
				CachedCollections.Reset();
			}
		}

		/**
		 * 中文：检测 -> 必要时调用 Utils::CreateEntityCollections 走 archetype 分桶重建。
		 *   const 函数是为了允许从只读 const 路径触发 lazy 重建（CachedCollections 是 mutable）。
		 */
		MASSENTITY_API void ConditionallyUpdate(const FMassEntityManager& EntityManager) const;

		/**
		 * these are the entities represented by given instance of FEntityCollection.
		 * EntityHandles are the authority, source of truth regarding the contents
		 *
		 * 中文：**真值表**——所有 mutate 都先动这里，缓存只是按需派生的产物。
		 */
		TArray<FMassEntityHandle> EntityHandles;

		/**
		 * Cached per-archetype collections of entities. Can go our of date due to
		 * operations performed on this FEntityCollection instance (in this case we
		 * reset cached CachedCollections) or due to the stored entities being moved
		 * between archetypes.
		 *
		 * 中文：按 archetype 分桶的派生缓存。两种过期路径：
		 *   1) 调用 mutating API（已直接 Reset 或 ConditionallyStoreCollection 中决策丢弃）；
		 *   2) entity 被搬到别的 archetype / archetype 内部 chunk 重排（此时 IsUpToDate 检查会发现并 Reset）。
		 *   声明为 mutable，是为了让 const 路径也能触发 lazy 重建。
		 */
		mutable TArray<FMassArchetypeEntityCollection> CachedCollections;

		/**
		 * Stores information whether we can expect duplicates in EntityHandles when building CachedCollections
		 *
		 * 中文：去重模式。AppendHandles/AddHandle 之后置为 FoldDuplicates（必须去重），
		 *   UpdateAndRemoveDuplicates 之后置为 NoDuplicates（可以省去重以加速重建）。
		 */
		FMassArchetypeEntityCollection::EDuplicatesHandling CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::NoDuplicates;
	};
}
