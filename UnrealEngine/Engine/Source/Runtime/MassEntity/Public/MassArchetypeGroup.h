// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Containers/Map.h"


namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// FArchetypeGroupType
	//-----------------------------------------------------------------------------
	/**
	 * 中文说明：
	 *   FArchetypeGroupType 标识"一种 archetype 分组的维度"。Mass 体系下，除了由 fragment/tag
	 *   组合自然形成的 archetype 之外，上层业务常常需要用**额外的正交维度**再把 archetype 分组，
	 *   比如按 LOD 等级、AOI 区块、玩法阵营等。每一种这样的维度对应一个 FArchetypeGroupType，
	 *   其内部只是一个 uint32 标识符，由上层分组系统负责分配与解释。
	 *
	 *   典型用法是：用户为自己的分组维度注册得到一个 FArchetypeGroupType，然后把 (GroupType, GroupID)
	 *   打包成 FArchetypeGroupHandle，用来在 FArchetypeGroups 中记录"这个 archetype 在 GroupType
	 *   维度里被分到了哪个组"。
	 */
	struct FArchetypeGroupType
	{
		// 标记"未设置"状态的标识符（INDEX_NONE 的 uint32 形式，即 0xFFFFFFFF）。
		constexpr static uint32 InvalidArchetypeGroupTypeIdentifier = static_cast<uint32>(INDEX_NONE);

		// explicit 防止 int 被隐式转成 GroupType；默认参数构造就是"无效状态"。
		explicit FArchetypeGroupType(const uint32 Value = InvalidArchetypeGroupTypeIdentifier)
			: Identifier(Value)
		{
		}

		FArchetypeGroupType(const FArchetypeGroupType& Source) = default;

		// 哈希函数，使 GroupType 可作为 TMap/TSet 的键。
		friend uint32 GetTypeHash(const FArchetypeGroupType& Instance)
		{
			return GetTypeHash(Instance.Identifier);
		}

		bool operator==(const FArchetypeGroupType Other) const
		{
			return Identifier == Other.Identifier;
		}

		// 严格弱序：用于放入有序容器或做二分。
		bool operator<(const FArchetypeGroupType Other) const
		{
			return Identifier < Other.Identifier;
		}

		// 转成 int32 以便用作 SparseArray 下标（见 FArchetypeGroups::IDContainer）。
		// explicit 防止和无关整数算术意外混用。
		explicit operator int32() const
		{
			return static_cast<int32>(Identifier);
		}

		// 是否是一个合法、已分配的 GroupType 标识符。
		bool IsValid() const
		{
			return Identifier != InvalidArchetypeGroupTypeIdentifier;
		}

	private:
		// 真实底层标识符。设计上只增不减，由上层分组系统顺序分配。
		uint32 Identifier;
	};


	/**
	 * 中文说明：
	 *   FArchetypeGroupID 标识"在某个 GroupType 维度下的具体分组编号"。和 GroupType 一样是个 uint32，
	 *   语义上是"该维度内部的序号"。配合 First/Next 可以按顺序产出一串 ID，供上层按需求生成新组。
	 *
	 *   典型用法：分组系统内部维护一个 "下一个可用 GroupID"，每次要分一个新组就把它记下来并调用 Next()
	 *   推进到下一个；GroupID 是否存活由分组系统自己追踪。
	 */
	struct FArchetypeGroupID
	{
		// 无效 ID：0xFFFFFFFF。默认构造就是这个值。
		constexpr static uint32 InvalidArchetypeGroupID = static_cast<uint32>(INDEX_NONE);
		// 第一个合法 ID，0。
		constexpr static uint32 FirstGroupID = 0;

		FArchetypeGroupID() = default;
		// 注意：这里 uint32 的构造没加 explicit，因此 int 可能被隐式转成 GroupID。
		FArchetypeGroupID(const uint32 InID)
			: ID(InID)
		{
		}

		bool operator==(const FArchetypeGroupID Other) const
		{
			return ID == Other.ID;
		}

		bool IsValid() const
		{
			return ID != InvalidArchetypeGroupID;
		}

		// 隐式转 int32，方便用作下标；注意和 GroupType 的 explicit 不同。
		operator int32() const
		{
			return static_cast<int32>(ID);
		}

		// 取起始 ID。分组系统通常用它初始化"下一个可分配 ID"计数器。
		static FArchetypeGroupID First()
		{
			return FArchetypeGroupID(FirstGroupID);
		}

		// 产生下一个 ID，形成链式分配：First -> Next -> Next ...
		// 该函数是 pure：不改动当前对象，返回新对象。
		FArchetypeGroupID Next() const
		{
			return FArchetypeGroupID(ID + 1);
		}

	private:
		uint32 ID = InvalidArchetypeGroupID;
	};

	//-----------------------------------------------------------------------------
	// FArchetypeGroupHandle
	//-----------------------------------------------------------------------------
	/**
	 * 中文说明：
	 *   FArchetypeGroupHandle 是 (GroupType, GroupID) 的组合句柄：它同时回答了
	 *   "是哪个维度上的分组"和"在这个维度里编号多少"。这是 Archetype 与上层分组系统交互的基本单位：
	 *     - 当某 archetype 被归入一个自定义分组时，调用方构造一个 Handle 交给 FArchetypeGroups；
	 *     - 查询、移除时也用 Handle 指定目标组。
	 *
	 *   哈希策略：把 (GroupType, GroupID) 打包成一个 uint64 再哈希，保证两个字段在键里都有效参与比较。
	 */
	struct FArchetypeGroupHandle
	{
		// 显式构造：要求调用方提供完整的 (GroupType, GroupID) —— 避免只填一半造成歧义。
		explicit FArchetypeGroupHandle(const FArchetypeGroupType InGroupType, const FArchetypeGroupID InGroupID)
			: GroupType(InGroupType), GroupID(InGroupID)
		{
		}

		// 默认构造的 Handle 里 GroupType 和 GroupID 都是无效状态。
		FArchetypeGroupHandle() = default;

		// 两字段皆相等才判等。
		bool operator==(const FArchetypeGroupHandle Other) const
		{
			return GroupType == Other.GroupType && GroupID == Other.GroupID;
		}

		bool operator!=(const FArchetypeGroupHandle Other) const
		{
			return !(*this == Other);
		}

		// 把 (GroupType, GroupID) 拼成 uint64 做哈希。高 32 位是 GroupType，低 32 位是 GroupID，
		// 避免两个维度互相交叉时哈希碰撞。
		friend uint32 GetTypeHash(const FArchetypeGroupHandle& Instance)
		{
			const uint64 CombinedHandle = static_cast<uint64>(static_cast<int32>(Instance.GroupType)) << 32 | Instance.GroupID;
			return GetTypeHash(CombinedHandle);
		}

		FArchetypeGroupType GetGroupType() const
		{
			return GroupType;
		}

		FArchetypeGroupID GetGroupID() const
		{
			return GroupID;
		}

		// 先按 GroupType 排序，再按 GroupID。主要用于放进 TSortedMap 或做稳定输出。
		bool operator<(const FArchetypeGroupHandle Other) const
		{
			return GroupType < Other.GroupType || (GroupType == Other.GroupType && GroupID < Other.GroupID);
		}

		/*
		 * 中文说明：
		 *   仅更新 GroupID 字段，保持 GroupType 不变。使用 placement-new 重建对象主要是因为
		 *   FArchetypeGroupType / FArchetypeGroupID 可能是"只通过构造赋值"的风格（无显式 setter）。
		 *   调用方必须保证 Other 与当前句柄是同一个 GroupType，否则是逻辑错误，有 ensure 保护。
		 */
		void UpdateID(const FArchetypeGroupHandle Other)
		{
			if (ensureMsgf(Other.GroupType == GroupType, TEXT("Updating ID is only supported for group handles of the same type")))
			{
				new (this) FArchetypeGroupHandle(GroupType, Other.GroupID);
			}
		}

		// 两个字段都必须有效才判定句柄有效。
		bool IsValid() const
		{
			return GroupType.IsValid() && GroupID.IsValid();
		}

	private:
		FArchetypeGroupType GroupType;   // 维度标识
		FArchetypeGroupID GroupID;       // 维度内编号
	};

	//-----------------------------------------------------------------------------
	// FArchetypeGroups
	//-----------------------------------------------------------------------------
	/**
	 * 中文说明：
	 *   FArchetypeGroups 保存"一个 archetype 在各个分组维度上所属的 GroupID 集合"。内部用
	 *   TSparseArray<FArchetypeGroupID> 以 GroupType 的 int32 值作为下标 —— 稀疏数组的稀疏语义
	 *   天然适合"大多数 archetype 只参与少数几个维度"的情况。
	 *
	 *   API 分两套：
	 *     - 就地修改版本：Add(GroupHandle)/Remove(GroupType)，在当前对象上修改。
	 *     - Immutable 风格版本：Add(...) const / Remove(...) const，返回修改后的副本，
	 *       原对象保持不变。`[[nodiscard]]` 防止调用方在 const 对象上调用后忘记接住结果。
	 *
	 *   同一个 GroupType 在一个 FArchetypeGroups 里只能映射到一个 GroupID（因为使用 GroupType
	 *   当下标），即"一个 archetype 在同一个维度只能属于一个组"。如果 Add 同 GroupType 不同 GroupID，
	 *   行为是覆盖旧值。
	 *
	 *   GetTypeHash 把整个映射表哈希成一个值，用于以 FArchetypeGroups 为键的哈希容器。
	 */
	struct FArchetypeGroups
	{
		FArchetypeGroups() = default;
		FArchetypeGroups(const FArchetypeGroups& InGroups) = default;
		FArchetypeGroups(FArchetypeGroups&& InGroups) = default;

		// 完全相等：两个 SparseArray 的稀疏布局与元素都相同才相等（依赖 TSparseArray 的 operator==）。
		bool operator==(const FArchetypeGroups& OtherGroups) const;
		FArchetypeGroups& operator=(FArchetypeGroups&& InGroups);
		FArchetypeGroups& operator=(const FArchetypeGroups& InGroups);

		/**
		 * Adds or updates the given (GroupType, GroupID) combination to IDContainer
		 *
		 * 中文说明：就地写入版本。如果 GroupType 维度已经存在记录，则直接覆盖成新的 GroupID。
		 */
		void Add(FArchetypeGroupHandle GroupHandle);

		/**
		 * Adds or updates the given (GroupType, GroupID) combination to IDContainer
		 * @return a copy of this FArchetypeGroups container with GroupHandle added to the ID container
		 * @note using [[nodiscard]] to avoid accidental calls on const instances that would not produce any effects
		 *
		 * 中文说明：Immutable 版本。先拷贝再改，适用于持有 const FArchetypeGroups 的调用方以"返回新值"
		 *   的方式产出变更后的版本（对应 archetype 重分组后生成新的 GroupsMap 条目）。
		 */
		[[nodiscard]] FArchetypeGroups Add(FArchetypeGroupHandle GroupHandle) const;

		/**
		 * Removes the stored GroupID associated with the given GroupType.
		 * If the given group type is not stored in IDContainer the request is ignored.
		 *
		 * 中文说明：就地删除某维度的记录；若原本就没有则静默返回。
		 */
		void Remove(FArchetypeGroupType GroupType);

		/**
		 * Removes the stored GroupID associated with the given GroupType.
		 * If the given group type is not stored in IDContainer the request is ignored.
		 * @return a copy of this FArchetypeGroups container with GroupType removed.
		 * @note using [[nodiscard]] to avoid accidental calls on const instances that would not produce any effects
		 *
		 * 中文说明：Immutable 版本的 Remove。若 GroupType 不存在，返回 *this 的一份拷贝（逻辑上不变）。
		 */
		[[nodiscard]] FArchetypeGroups Remove(FArchetypeGroupType GroupType) const;

		// 主动收缩底层 SparseArray 的尾部空洞，降低内存占用。
		void Shrink();
		// 判断底层 SparseArray 是否已处于"无尾部空洞"的紧凑状态。
		bool IsShrunk() const;

		// 查询：返回该 archetype 在 GroupType 维度上的 GroupID。若未参与该维度，返回默认构造的无效 GroupID。
		// 注意：这里用 IsValidIndex 而非 IsAllocated 检查，意味着：当 GroupType 下标在已扩展范围内
		// 但对应稀疏位为空（被 Remove 过）时，将返回容器内残留的未初始化数据 —— 调用方应尽量先用 ContainsType 判断。
		FArchetypeGroupID GetID(const FArchetypeGroupType GroupType) const
		{
			return IDContainer.IsValidIndex(static_cast<int32>(GroupType)) ? IDContainer[static_cast<int32>(GroupType)] : FArchetypeGroupID();
		}

		// 查询该 archetype 是否记录了某个 GroupType 维度的分组信息。
		bool ContainsType(const FArchetypeGroupType GroupType) const
		{
			return IDContainer.IsValidIndex(static_cast<int32>(GroupType));
		}

		// 整个 FArchetypeGroups 的哈希：遍历稀疏数组中每个已分配的 (GroupType, GroupID) 合成 Handle 并累加哈希。
		friend uint32 GetTypeHash(const FArchetypeGroups& Instance);

	protected:
		/*
		 * 底层存储：以 GroupType 的整数值为下标的稀疏数组。
		 *   - 下标范围对应已被系统分配的最大 GroupType 值；
		 *   - 稀疏：大多数 archetype 只会参与少数几个 GroupType，浪费的空槽不会占用元素内存（只占 AllocationFlags 位）。
		 *
		 * 设计权衡：用稀疏数组而不是 TMap<GroupType, GroupID> 的原因，是 GroupType 数量一般很少（< 64），
		 * 稀疏数组在随机访问与内存局部性上都更好。
		 */
		TSparseArray<FArchetypeGroupID> IDContainer;
	};

} // namespace UE::Mass
