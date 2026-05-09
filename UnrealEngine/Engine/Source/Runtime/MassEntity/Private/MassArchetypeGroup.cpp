// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassArchetypeGroup.h"

/*
 * 文件说明：
 *   FArchetypeGroups 的非内联实现。这里的几个关键点：
 *     - IDContainer 以 GroupType 的 int32 值直接作为稀疏数组下标，因此"稀疏数组的最大 index"
 *       = 曾经用过的最大 GroupType。当移除最后一个位置时，可以顺手 Shrink 收缩尾部空洞。
 *     - 所有 "... const" 变体（Immutable 风格）都遵循"先完整拷贝再修改副本再返回"的模式，
 *       让调用方能把 FArchetypeGroups 当作值类型/快照使用。
 *     - GetTypeHash 需要同时覆盖 (GroupType, GroupID) 两个维度，因此遍历 IDContainer 把每对组合
 *       都合成 Handle 再累加 HashCombine，保证不同"维度分布"能散列到不同位置。
 */

namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// FArchetypeGroups
	//-----------------------------------------------------------------------------
	// 相等：直接委托给 TSparseArray 的 operator==。要求稀疏布局与元素逐一相同。
	bool FArchetypeGroups::operator==(const FArchetypeGroups& OtherGroups) const
	{
		return IDContainer == OtherGroups.IDContainer;
	}

	// 移动赋值：直接 MoveTemp IDContainer，调用方之后不应再使用 InGroups。
	FArchetypeGroups& FArchetypeGroups::operator=(FArchetypeGroups&& InGroups)
	{
		IDContainer = MoveTemp(InGroups.IDContainer);
		return *this;
	}

	// 拷贝赋值：深拷贝 IDContainer。
	FArchetypeGroups& FArchetypeGroups::operator=(const FArchetypeGroups& InGroups)
	{
		IDContainer = InGroups.IDContainer;
		return *this;
	}

	// 就地 Add：
	//   - 若 GroupType 的槽位已被分配（IsValidIndex 为真），直接覆盖原 GroupID；
	//   - 否则用 EmplaceAt 在该下标处新建。EmplaceAt 会自动把中间的空洞标记为稀疏未分配。
	void FArchetypeGroups::Add(FArchetypeGroupHandle GroupHandle)
	{
		const int32 GroupTypeIndex = static_cast<int32>(GroupHandle.GetGroupType());
		if (IDContainer.IsValidIndex(GroupTypeIndex))
		{
			IDContainer[GroupTypeIndex] = GroupHandle.GetGroupID();
		}
		else
		{
			IDContainer.EmplaceAt(GroupTypeIndex, GroupHandle.GetGroupID());
		}
	}

	// Immutable 风格的 Add：行为同上，但操作的是 Copy 而非 *this。
	FArchetypeGroups FArchetypeGroups::Add(FArchetypeGroupHandle GroupHandle) const
	{
		FArchetypeGroups Copy = *this;
		const int32 GroupTypeIndex = static_cast<int32>(GroupHandle.GetGroupType());
		if (IDContainer.IsValidIndex(GroupTypeIndex))
		{
			Copy.IDContainer[GroupTypeIndex] = GroupHandle.GetGroupID();
		}
		else
		{
			Copy.IDContainer.EmplaceAt(GroupTypeIndex, GroupHandle.GetGroupID());
		}

		return MoveTemp(Copy);
	}

	// 就地 Remove：
	//   1. 若该 GroupType 不在容器中，静默返回；
	//   2. 若是容器尾部的元素（删掉它以后会留出尾部空洞），顺势调用 Shrink 把尾部空洞清除；
	//   3. RemoveAtUninitialized 适合 trivially-destructible 元素（FArchetypeGroupID 就是 uint32 包装），免析构。
	void FArchetypeGroups::Remove(FArchetypeGroupType GroupType)
	{
		const int32 GroupTypeIndex = static_cast<int32>(GroupType);
		if (IDContainer.IsValidIndex(GroupTypeIndex))
		{
			// 判断"这是不是 SparseArray 内部最高已分配下标"：GetMaxIndex 返回的是"上界"（= 最大下标 + 1）。
			const bool bIsLastElement = ((IDContainer.GetMaxIndex() - 1) == GroupTypeIndex);
			IDContainer.RemoveAtUninitialized(GroupTypeIndex);
			if (bIsLastElement)
			{
				// 尾部不再有有效元素，主动 Shrink 避免空洞长期占用。
				Shrink();
			}
		}
	}

	// Immutable 版本的 Remove。
	FArchetypeGroups FArchetypeGroups::Remove(FArchetypeGroupType GroupType) const
	{
		const int32 GroupTypeIndex = static_cast<int32>(GroupType);
		if (IDContainer.IsValidIndex(GroupTypeIndex))
		{
			FArchetypeGroups Copy = *this;
			Copy.IDContainer.RemoveAtUninitialized(GroupTypeIndex);

			// if we removed the last element we need to shrink the container too.
			// 尾部元素被移除后同样做一次 Shrink，保证返回的副本也处于紧凑状态。
			// 注意这里判断用的是原 IDContainer 的 MaxIndex（没改过），语义上等价于判断"这是不是最后一个"。
			if (const bool bIsLastElement = ((IDContainer.GetMaxIndex() - 1) == GroupTypeIndex))
			{
				Copy.Shrink();
			}

			return MoveTemp(Copy);
		}
		// GroupType 不存在时返回 *this 的拷贝（通过返回值的隐式拷贝）。
		return *this;
	}

	// 主动 Shrink：委托给 TSparseArray 的 Shrink，会把尾部未使用段裁掉。
	void FArchetypeGroups::Shrink()
	{
		IDContainer.Shrink();
	}

	/*
	 * IsShrunk：
	 *   判断 IDContainer 是否已处于"无尾部空洞"的状态。
	 *   - MaxIndex == 0：容器为空或从未 EmplaceAt 过，天然紧凑。
	 *   - IsValidIndex(MaxIndex - 1)：最高下标处的元素仍然有效，说明尾部没有空洞。
	 *   若最高下标处元素已被 Remove 但尚未 Shrink，则返回 false。
	 */
	bool FArchetypeGroups::IsShrunk() const
	{
		const int32 MaxIndex = IDContainer.GetMaxIndex();
		// if the IDContainer has been shrunk, or never needed to be shrunk, the last element in the available
		// container is valid.
		return MaxIndex == 0 || IDContainer.IsValidIndex(MaxIndex - 1) ;
	}

	/*
	 * GetTypeHash：
	 *   遍历 IDContainer 的所有已分配元素（CreateConstIterator 只访问有效位），
	 *   把每个 (GroupType=It.GetIndex(), GroupID=IDContainer[It.GetIndex()]) 合成一个 Handle
	 *   并用 HashCombine 累加。
	 *
	 *   为什么把 GroupType 也参与哈希：如果只哈希 GroupID，两个 archetype 即便在不同维度上属于
	 *   相同编号的组（这很常见，因为 GroupID 只有"维度内"语义），也会散列到同一桶，哈希表性能下降。
	 *
	 *   注意：哈希依赖遍历顺序。TSparseArray 的迭代器按下标升序遍历，因此对同一个映射集合，
	 *   结果是稳定的；不同次构造得到相同集合，哈希也一致。
	 */
	uint32 GetTypeHash(const FArchetypeGroups& Instance)
	{
		uint32 Hash = 0;

		for (auto It = Instance.IDContainer.CreateConstIterator(); It; ++It)
		{
			const FArchetypeGroupHandle GroupHandle(FArchetypeGroupType(It.GetIndex()), Instance.IDContainer[It.GetIndex()]);
			Hash = HashCombine(Hash, GetTypeHash(GroupHandle));
		}

		return Hash;
	}
} // namespace UE::Mass
