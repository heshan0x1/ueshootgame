// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/StructTypeBitSet.h"
#include "Templates/UnrealTypeTraits.h"

/**
 * Traits configuring finer details of TTypeBitSetBuilder behavior.
 * Provides a way to enforce compile-time checks and validations.
 *
 * 中文说明：
 *   TTypeBitSetTraits 是为 TTypeBitSetBuilder 提供"编译期类型校验"的 traits 模板。
 *   默认实现只做一件事：要求被加入位集的类型必须派生自 TBaseType（例如 FMassFragment）。
 *   特定基类（如 FMassFragment、FMassTag）可以在别处做偏特化，把"派生自"改为更严格的 Mass Concept
 *   （见 MassBitSetRegistry.h 中对 FMassTag / FMassFragment 的偏特化，使用 UE::Mass::CTag / CFragment）。
 *   这样 TypeBitSetBuilder::Add<T>() 在写错类型时会直接编译失败，而不是在运行时拿到错误的位索引。
 */
template <typename TBaseType>
struct TTypeBitSetTraits final
{
	/** 
	 * Static constexpr boolean to verify if a tested type is valid.
	 * Ensures that the tested type is derived from the base type.
	 */
	// 编译期布尔：仅当 TTestedType 派生自 TBaseType 时为 true。所有 Add<T>/Remove<T>/Contains<T>
	// 模板内部都会 static_assert 这个常量，保证把错误类型扔进位集会立即编译错误。
	template <typename TTestedType>
	static constexpr bool IsValidType = TIsDerivedFrom<TTestedType, TBaseType>::IsDerived;
};

/**
 * TTypeBitSetBuilder is a template class for building and managing type-specific bitsets.
 * It extends TTypeBitSetBase to provide functionalities specific to bitset building.
 *
 * @param TBaseStruct the base struct type that all stored types must derive from.
 * @param TUStructType Unreal's struct type, typically UScriptStruct or UClass.
 * @param bTestInheritanceAtRuntime flag to enable runtime inheritance checks.
 * @param TContainer the container type for storing bitsets (default is FStructTypeBitSet::FBitSetContainer).
 *
 * 中文说明：
 *   TTypeBitSetBuilder 是 Mass 里"类型位集"的可读/可写操作视图。在 Mass 体系中，每一种
 *   FMassFragment/FMassTag/... 派生类型都会被 FStructTracker 分配一个全局唯一的位索引；
 *   一个 Archetype 持有哪些 fragment/tag，就对应着这些位索引组成的位集。
 *
 *   Builder 本身不拥有位数据，而是持有对外部 bitset 容器的引用 + 对 FStructTracker 的引用，
 *   以此完成："类型 T -> 位索引 -> 把这一位置上/下"的闭环。典型用法：
 *     - 通过 FBitTypeRegistry::MakeBuilder(BitSet) 取得一个 builder，对已有 BitSet 做修改；
 *     - 或通过 FBitSetFactory（见 MassBitSetRegistry.h）零初始化构建一个全新的 BitSet。
 *
 *   为什么要用 BitSet：这是 archetype 匹配 / query 需求求值的性能关键路径，O(1) 的并集/差集/
 *   "HasAll/HasAny/HasNone"决定了"给定一组 requirement，过滤出匹配的 archetype"能否批量执行。
 *
 *   模板参数：
 *     - TBaseStruct：所有可加入位集的 C++ 类型必须派生的基类，如 FMassFragment。
 *     - TUStructType：对应的反射基类（默认 UScriptStruct，部分场景可能是 UClass）。
 *     - bTestInheritanceAtRuntime：是否在运行期再次验证"给定类型派生自 TBaseStruct"；
 *       默认值 WITH_STRUCTUTILS_DEBUG 意味着 debug 构建才做，发布构建省开销。
 *     - TContainer：底层位数组容器，默认 FStructTypeBitSet::FBitSetContainer。
 *
 *   注意：Builder 只是"视图"，其引用的 StructTracker 与底层 BitSet 必须活得比它久。
 */
template<typename TBaseStruct, typename TUStructType = UScriptStruct, bool bTestInheritanceAtRuntime=WITH_STRUCTUTILS_DEBUG, typename TContainer=FStructTypeBitSet::FBitSetContainer>
struct TTypeBitSetBuilder : TTypeBitSetBase<TTypeBitSetBuilder<TBaseStruct, TUStructType, bTestInheritanceAtRuntime>, TBaseStruct, TUStructType, TContainer&, bTestInheritanceAtRuntime>
{
	/** Define the base class for easier reference */
	// CRTP 基类别名：TTypeBitSetBase 提供一套通用的位集操作，本类只补充 Mass 相关行为。
	using Super = TTypeBitSetBase<TTypeBitSetBuilder, TBaseStruct, TUStructType, TContainer&, bTestInheritanceAtRuntime>;
	/** Alias for Unreal's struct type */
	// 反射结构类型别名（UScriptStruct / UClass）。
	using FUStructType = TUStructType; 
	/** Alias for the base struct type */
	// C++ 基类别名（FMassFragment / FMassTag / ...）。
	using FBaseStruct = TBaseStruct;   
	/** Traits for compile-time checks */
	// 编译期类型校验 traits（见上方 TTypeBitSetTraits）。
	using FTraits = TTypeBitSetTraits<TBaseStruct>;

private:
	/**
	 * Internal bitset class extending TContainer.
	 * Used to ensure proper comparison of bitsets.
	 *
	 * 中文说明：
	 *   私有内部类型：把 TContainer 再包一层，目的是重写 operator==，采用
	 *   "只比较置位的 bit"语义（bMissingBitValue=false）。这样即便两个位集容量
	 *   不一致（例如一个 builder 后来又注册了新类型），只要实际置位的类型集合相同，就视为相等。
	 *   这是"相同 fragment 组合 -> 相同 archetype"这一核心等价性的基础。
	 */
	struct FBitSet : TContainer
	{
		/**
		 * Equality operator to compare bitsets.
		 * Uses CompareSetBits to compare the set bits, ignoring missing bits.
		 */
		inline bool operator==(const FBitSet& Other) const
		{
			return TContainer::CompareSetBits(Other, /*bMissingBitValue=*/false);
		}
	};

public:
	/** Alias for a const bitset, this is the type to use to represent the stored bit set */
	// 对外暴露 const 版本的 BitSet 类型。外部代码应该把"BitSet 实例"当作不可变快照来用，
	// 需要修改时再用 Builder 做操作。
	using FConstBitSet = const FBitSet;

	/** Friend declaration to allow base class access */
	friend Super;

	/** Bring base class methods into scope */
	// 把 Super（TTypeBitSetBase）中的成员/方法引入本作用域，使重载解析能看到。
	using Super::StructTypesBitArray;
	using Super::GetBaseUStruct;
	using Super::Add;
	using Super::Remove;
	using Super::Contains;
	using Super::operator+=;
	using Super::operator-=;
	using Super::operator+;
	using Super::operator-;
	using Super::ExportTypes;

	/**
	 * Constructor that initializes the builder with a struct tracker and a source bitset.
	 * @param InStructTracker he struct tracker to use.
	 * @param Source the source bitset to initialize from.
	 *
	 * 中文说明：
	 *   外部入口构造：把一个外部 BitSet 绑定到 Builder 上。由于 Super 要求可写引用，
	 *   这里对传入的 const Source 做 const_cast；调用者的约定是"只要它以非 const Builder
	 *   访问，就可以修改底层 BitSet"。InStructTracker 提供类型<->位索引映射。
	 */
	TTypeBitSetBuilder(FStructTracker& InStructTracker, FConstBitSet& Source)
		: Super(const_cast<FBitSet&>(Source))
		, StructTracker(InStructTracker)
	{
	}

private:	
	/**
	 * Private constructor used internally to initialize with a specific bit index.
	 * @param InStructTracker the struct tracker to use.
	 * @param Source the source bitset to initialize from.
	 * @param BitToSet the bit index to set.
	 *
	 * 中文说明：
	 *   私有构造：基于一个外部 BitSet 新建 Builder，并立刻置上 BitToSet 这一位。
	 *   仅供"按类型构造"的静态工厂路径内部使用。
	 */
	TTypeBitSetBuilder(FStructTracker& InStructTracker, FConstBitSet& Source, const int32 BitToSet)
		: Super(const_cast<FBitSet&>(Source))
		, StructTracker(InStructTracker)
	{
		StructTypesBitArray.AddAtIndex(BitToSet); 
	}

public:
	/**
	 * Assignment operator to copy the bitset from another builder.
	 * Ensures that both builders use the same struct tracker.
	 * @param Source the other builder to copy from.
	 * @return Reference to this builder.
	 *
	 * 中文说明：
	 *   赋值操作：只复制位数据，不修改 StructTracker 引用。断言两个 Builder 必须来自同一个
	 *   StructTracker —— 否则"位索引"的含义就不同，跨 tracker 赋值会得到错误的 archetype 语义。
	 */
	TTypeBitSetBuilder& operator=(const TTypeBitSetBuilder& Source)
	{
		ensureMsgf(&Source.StructTracker == &StructTracker, TEXT("Assignment is only allowed between two instances created with the same StructTracker."));
		StructTypesBitArray = Source.StructTypesBitArray; 
		return *this;
	}

	/**
	 * Retrieves the index of a struct type within the struct tracker.
	 * If the type is not registered, it will be added.
	 * @param InStructType the struct type to get the index for.
	 * @return The index of the struct type.
	 *
	 * 中文说明：
	 *   反射对象 -> 位索引：如果该类型是首次见到则由 tracker 分配新位索引，否则返回已有索引。
	 *   有副作用（可能注册新类型），因此非 const 语义上不合适，但 FindOrAddStructTypeIndex 在
	 *   tracker 内部加锁，所以这里保留 const 以便位集只读路径也能解析类型。
	 *   当 bTestInheritanceAtRuntime 为 true 时，还会在运行期再校验"InStructType 必须派生自
	 *   TBaseStruct"，防止脏数据污染位集语义。
	 */
	int32 GetTypeIndex(const TUStructType& InStructType) const
	{
		if constexpr (bTestInheritanceAtRuntime)
		{
			// Ensure the struct type derives from the base struct
			// 运行期校验继承关系。失败时 ensure 并返回 INDEX_NONE，调用方会拿到一个无效索引。
			if (UNLIKELY(!ensureMsgf(InStructType.IsChildOf(GetBaseUStruct())
				, TEXT("Creating index for '%s' while it doesn't derive from the expected struct type %s")
				, *InStructType.GetPathName(), *GetBaseUStruct()->GetName())))
			{
				return INDEX_NONE;
			}
		}

		// Find or add the struct type index in the tracker
		// 在 tracker 中查找或注册该类型，取得其稳定位索引。
		return GetStructTracker().FindOrAddStructTypeIndex(InStructType);
	}

	/**
	 * Static method to get the type index for a given struct type.
	 * @param InStructTracker the struct tracker to use.
	 * @param InStructType the struct type to get the index for.
	 * @return The index of the struct type.
	 *
	 * 中文说明：静态版本，免去构造 Builder 实例的开销。行为与实例版相同。
	 */
	static int32 GetTypeIndex(const FStructTracker& InStructTracker, const TUStructType& InStructType)
	{
		if constexpr (bTestInheritanceAtRuntime)
		{
			// Ensure the struct type derives from the base struct
			if (UNLIKELY(!ensureMsgf(InStructType.IsChildOf(GetBaseUStruct())
				, TEXT("Creating index for '%s' while it doesn't derive from the expected struct type %s")
				, *InStructType.GetPathName(), *GetBaseUStruct()->GetName())))
			{
				return INDEX_NONE;
			}
		}

		// Find or add the struct type index in the tracker
		return InStructTracker.FindOrAddStructTypeIndex(InStructType);
	}

	/**
	 * Template method to get the type index for a specific C++ type.
	 * Ensures at compile-time that the type is valid.
	 * @param T the C++ type to get the index for.
	 * @return The index of the struct type.
	 *
	 * 中文说明：
	 *   针对 C++ 类型 T 的位索引查询。关键点是用 `static const int32 TypeIndex = ...;`
	 *   做函数级静态缓存，同一个 (tracker, T) 组合只在首次调用时真正去 tracker 里查一次，
	 *   后续调用都是 O(1) 直接读局部静态变量。这对 Mass 的热路径（processor 每帧都会间接用到）
	 *   很重要。
	 *   static_assert 会在误用（T 不派生自基类、或不是合法的 Mass 元素类型）时编译失败。
	 */
	template<typename T>
	static int32 GetTypeIndex(FStructTracker& InStructTracker)
	{
		static_assert(FTraits::template IsValidType<T>, "Given struct is not a valid type for this TypeBitSetBuilder.");
		static const int32 TypeIndex = GetTypeIndex(InStructTracker, *UE::StructUtils::GetAsUStruct<T>());
		return TypeIndex;
	}

	/**
	 * Template method to get the type index for a specific C++ type.
	 * Ensures at compile-time that the type is valid.
	 * @param T the C++ type to get the index for.
	 * @return The index of the struct type.
	 *
	 * 中文说明：实例版本。行为同上，走的是 this->GetTypeIndex() 以复用 tracker 引用。
	 */
	template<typename T>
	int32 GetTypeIndex() const
	{
		static_assert(FTraits::template IsValidType<T>, "Given struct is not a valid type for this TypeBitSetBuilder.");
		static const int32 TypeIndex = GetTypeIndex(*UE::StructUtils::GetAsUStruct<T>());
		return TypeIndex;
	}
	
	/**
	 * Template method to create a bitset for a specific C++ type.
	 * @param T the C++ type.
	 * @param InStructType the Unreal struct type.
	 * @return A new TTypeBitSetBuilder instance with the specified type.
	 */
	// 以单一类型 T 为基础生成 Builder。注意此重载实现上依赖一个接收 int32 的构造函数，
	// 但本类当前只有 (tracker, BitSet) 和 (tracker, BitSet, BitToSet) 两个构造，因此
	// 这段代码实际走的是调用方自己的扩展路径；保留是为了 API 兼容。
	template<typename T>
	static TTypeBitSetBuilder GetTypeBitSet(const TUStructType& InStructType)
	{
		static_assert(FTraits::template IsValidType<T>, "Given struct is not a valid type for this TypeBitSetBuilder.");
		return TTypeBitSetBuilder(GetTypeIndex<T>(InStructType));
	}

	/**
	 * Retrieves the struct type at a given index from the struct tracker.
	 * @param Index the index of the struct type.
	 * @return Pointer to the struct type.
	 *
	 * 中文说明：位索引 -> 反射对象。可能返回 nullptr（比如 tracker 里该槽是 WeakObjectPtr 且已失效）。
	 */
	const TUStructType* GetTypeAtIndex(const int32 Index)
	{
		return Cast<const TUStructType>(StructTracker.GetStructType(Index));
	}
	
	/**
	 * Template method to add a struct type to the bitset.
	 * @param T the C++ type to add.
	 * @return The index of the added struct type.
	 */
	// 把类型 T 对应的位置 1。返回 T 的位索引，便于调用方再做其它记录。
	template<typename T>
	inline int32 Add()
	{
		static_assert(FTraits::template IsValidType<T>, "Given struct is not a valid type for this TypeBitSetBuilder.");
		const int32 StructTypeIndex = GetTypeIndex<T>();
		StructTypesBitArray.AddAtIndex(StructTypeIndex); 
		return StructTypeIndex;
	}

	/**
	 * Template method to remove a struct type from the bitset.
	 * @param T the C++ type to remove.
	 * @return The index of the removed struct type.
	 */
	// 把类型 T 对应的位清 0。返回 T 的位索引。
	template<typename T>
	inline int32 Remove()
	{
		static_assert(FTraits::template IsValidType<T>, "Given struct is not a valid type for this TypeBitSetBuilder.");
		const int32 StructTypeIndex = GetTypeIndex<T>();
		StructTypesBitArray.RemoveAtIndex(StructTypeIndex); 
		return StructTypeIndex;
	}

	/**
	 * Removes all bits set in another builder's bitset from this builder's bitset.
	 * @param Other the other builder whose bits to remove.
	 */
	// 集合语义：this <- this \ Other（差集，就地修改）。
	inline void Remove(const TTypeBitSetBuilder& Other)
	{
		StructTypesBitArray -= Other.StructTypesBitArray;
	}

	/**
	 * Template method to check if a struct type is contained in the bitset.
	 * @param T the C++ type to check.
	 * @return True if the type is contained; false otherwise.
	 */
	// O(1) 查询某类型是否在位集中。Mass 里判断"archetype 是否含某 fragment"正是这条路径。
	template<typename T>
	inline bool Contains() const
	{
		static_assert(FTraits::template IsValidType<T>, "Given struct is not a valid type for this TypeBitSetBuilder.");
		const int32 StructTypeIndex = GetTypeIndex<T>();
		return StructTypesBitArray.Contains(StructTypeIndex);
	}

	/**
	 * Subtracts another builder's bitset from this builder's bitset.
	 * @param Other the other builder.
	 * @return A new builder with the result.
	 */
	// 差集：返回一个新 Builder（其引用的底层容器是按值拷贝出来的副本）。
	inline TTypeBitSetBuilder operator-(const TTypeBitSetBuilder& Other) const
	{
		TTypeBitSetBuilder Result = *this;
		Result -= Other;
		return MoveTemp(Result);
	}

	/**
	 * Adds another builder's bitset to this builder's bitset.
	 * @param Other the other builder.
	 * @return A new builder with the result.
	 */
	// 并集：本实现里先构造 Result(StructTracker)（依赖一个只接 tracker 的构造），再做按位 OR。
	// EBitwiseOperatorFlags::MaxSize 表示结果长度取两边较长者，避免数据被截断。
	inline TTypeBitSetBuilder operator+(const TTypeBitSetBuilder& Other) const
	{
		TTypeBitSetBuilder Result(StructTracker);
		Result.StructTypesBitArray = TBitArray<>::BitwiseOR(StructTypesBitArray, Other.StructTypesBitArray, EBitwiseOperatorFlags::MaxSize);
		return MoveTemp(Result);
	}

	/**
	 * Performs a bitwise AND with another builder's bitset.
	 * @param Other the other builder.
	 * @return A new builder with the result.
	 */
	// 交集：MinSize 意味着取较短的一边作为结果长度（超出部分 AND 必然为 0，可直接截掉）。
	inline TTypeBitSetBuilder operator&(const TTypeBitSetBuilder& Other) const
	{
		return TTypeBitSetBuilder(StructTracker, TBitArray<>::BitwiseAND(StructTypesBitArray, Other.StructTypesBitArray, EBitwiseOperatorFlags::MinSize));
	}

	/**
	 * Performs a bitwise OR with another builder's bitset.
	 * @param Other the other builder.
	 * @return A new builder with the result.
	 */
	// 并集（返回新 Builder 版本）。
	inline TTypeBitSetBuilder operator|(const TTypeBitSetBuilder& Other) const
	{
		return TTypeBitSetBuilder(StructTracker, TBitArray<>::BitwiseOR(StructTypesBitArray, Other.StructTypesBitArray, EBitwiseOperatorFlags::MaxSize));
	}

	/**
	 * Gets the overlap between this builder's bitset and another's.
	 * @param Other the other builder.
	 * @return A new builder representing the overlap.
	 */
	// 语义糖：GetOverlap 等价于按位 AND。常用于求"archetype 与 query 的共同 fragment"。
	inline TTypeBitSetBuilder GetOverlap(const TTypeBitSetBuilder& Other) const
	{
		return *this & Other;
	}

	/**
	 * Checks if this builder's bitset is equivalent to another's.
	 * @param Other the other builder.
	 * @return True if equivalent; false otherwise.
	 */
	// 等价性：只比较"已置位的 bit"。容量不同但置位集合相同仍判为相等。
	inline bool IsEquivalent(const TTypeBitSetBuilder& Other) const
	{
		return StructTypesBitArray.CompareSetBits(Other.StructTypesBitArray, /*bMissingBitValue=*/false);
	}

	/**
	 * Checks if this builder's bitset has all bits set in another's.
	 * @param Other the other builder.
	 * @return True if all bits are set; false otherwise.
	 */
	// 超集判定：Other 的每一个置位在 this 中都存在。Mass 里用来判断
	// "archetype 是否满足 query 的 All Requirements"。
	inline bool HasAll(const TTypeBitSetBuilder& Other) const
	{
		return StructTypesBitArray.HasAll(Other.StructTypesBitArray);
	}

	/**
	 * Checks if this builder's bitset has any bits set in another's.
	 * @param Other the other builder.
	 * @return True if any bits are set; false otherwise.
	 */
	// 相交判定：至少有一个共同置位。用于实现 query 的 Any Requirement。
	inline bool HasAny(const TTypeBitSetBuilder& Other) const
	{
		return StructTypesBitArray.HasAny(Other.StructTypesBitArray);
	}

	/**
	 * Checks if this builder's bitset has none of the bits set in another's.
	 * @param Other the other builder.
	 * @return True if no bits are set; false otherwise.
	 */
	// 不相交判定：常用于实现 query 的 None Requirement（"必须不含这些 fragment/tag"）。
	inline bool HasNone(const TTypeBitSetBuilder& Other) const
	{
		return !StructTypesBitArray.HasAny(Other.StructTypesBitArray);
	}

	/**
	 * Checks if the bitset is empty.
	 * @return True if empty; false otherwise.
	 */
	// 整个位集无任何置位。
	bool IsEmpty() const 
	{ 
		return StructTypesBitArray.IsEmpty();
	}

	/**
	 * Checks if a specific bit is set in the bitset.
	 * @param BitIndex the index of the bit to check.
	 * @return True if the bit is set; false otherwise.
	 */
	// 以原始位索引查询。一般调用方应该用 Contains<T>()，这个接口留给持有位索引的底层代码。
	inline bool IsBitSet(const int32 BitIndex) const
	{
		return StructTypesBitArray.Contains(BitIndex);
	}

	/**
	 * Counts the number of set bits in the bitset.
	 * @return The number of set bits.
	 */
	// 置位计数 = 当前位集表示的类型数量。
	int32 CountStoredTypes() const
	{
		return StructTypesBitArray.CountSetBits();
	}

	/**
	 * Retrieves the maximum number of types tracked by the struct tracker.
	 * @return The maximum number of types.
	 */
	// 返回 tracker 中已注册的类型总数（不是位集置位数），用作上限/容量参考。
	int32 GetMaxNum() const
	{		
		return StructTracker.Num();
	}

	/**
	 * Static method to get the maximum number of types from a struct tracker.
	 * @param StructTracker the struct tracker to query.
	 * @return The maximum number of types.
	 */
	static int32 GetMaxNum(const FStructTracker& StructTracker)
	{		
		return StructTracker.Num();
	}

	/**
	 * Equality operator to compare two builders.
	 * @param Other the other builder to compare.
	 * @return True if equal; false otherwise.
	 */
	// 相等即等价（只看置位），见 IsEquivalent。
	inline bool operator==(const TTypeBitSetBuilder& Other) const
	{
		return StructTypesBitArray.CompareSetBits(Other.StructTypesBitArray, /*bMissingBitValue=*/false);
	}

	/**
	 * Inequality operator to compare two builders.
	 * @param Other the other builder to compare.
	 * @return True if not equal; false otherwise.
	 */
	inline bool operator!=(const TTypeBitSetBuilder& Other) const
	{
		return !(*this == Other);
	}

	/**
	 * Conversion operator to a const bitset.
	 * @return A const reference to the bitset.
	 */
	// 隐式转换：Builder -> FConstBitSet。这样上层代码可以把 Builder 当成"BitSet 快照"使用。
	operator FConstBitSet() const
	{
		return static_cast<FConstBitSet&>(StructTypesBitArray);
	}

	/**
	 * Exports the types stored in the bitset to an output array.
	 * Note: This method can be slow due to the use of weak pointers in the struct tracker.
	 * @param TOutStructType the output struct type.
	 * @param Allocator the allocator for the output array.
	 * @param OutTypes the array to populate with struct types.
	 *
	 * 中文说明：
	 *   遍历所有置位 -> 通过 tracker 将位索引反解为反射对象 -> 追加到 OutTypes。
	 *   由于 tracker 内部可能用 TWeakObjectPtr 存放 UScriptStruct，每次解引用都要通过弱引用表，
	 *   因此这个接口不应该出现在热路径中。一般只用于调试、编辑器、序列化等场景。
	 */
	template<typename TOutStructType, typename Allocator>
	void ExportTypes(TArray<const TOutStructType*, Allocator>& OutTypes) const
	{
		// 使用 TBitArray 的 FConstIterator，只遍历有置位的位置；IsValid 时 GetValue 返回 true。
		TBitArray<>::FConstIterator It(StructTypesBitArray);
		while (It)
		{
			if (It.GetValue())
			{
				// Get the struct type from the tracker and add to the output array
				// 将位索引解析回反射对象；可能返回 nullptr（弱引用失效时）。
				OutTypes.Add(Cast<TOutStructType>(StructTracker.GetStructType(It.GetIndex())));
			}
			++It;
		}
	}

	/**
	 * Lists all types used by this bitset, calling the provided callback for each one.
	 * Returning false from the callback will early-out of iterating over the types.
	 * Note: This method can be slow due to the use of weak pointers in the struct tracker.
	 * @param Callback the callback function to call for each type.
	 *
	 * 中文说明：回调版本的 ExportTypes。回调返回 false 时提前终止遍历，便于"找到第一个符合条件的类型"类用例。
	 */
	void ExportTypes(TFunctionRef<bool(const TUStructType*)> Callback) const
	{
		TBitArray<>::FConstIterator It(StructTypesBitArray);
		bool bKeepGoing = true;
		while (bKeepGoing && It)
		{
			if (It.GetValue())
			{
				bKeepGoing = Callback(GetTypeAtIndex(It.GetIndex()));
			}
			++It;
		}
	}

	/**
	 * Retrieves the allocated size of the bitset.
	 * @return The allocated size in bytes.
	 */
	// 位数组占用的字节数，供内存统计使用。
	SIZE_T GetAllocatedSize() const
	{
		return StructTypesBitArray.GetAllocatedSize();
	}

	/**
	 * Provides a debug string description of the bitset contents.
	 * @return A string describing the contents of the bitset.
	 *
	 * 中文说明：
	 *   调试字符串形式的位集内容。仅在 WITH_STRUCTUTILS_DEBUG 宏开启的构建下返回真实内容，
	 *   发布构建下返回固定占位字符串以避免把类型名保留进可执行文件。
	 */
	FString DebugGetStringDesc() const
	{
	#if WITH_STRUCTUTILS_DEBUG
		FStringOutputDevice Ar;
		DebugGetStringDesc(Ar);
		return static_cast<FString>(Ar);
	#else
		return TEXT("DEBUG INFO COMPILED OUT");
	#endif //WITH_STRUCTUTILS_DEBUG
	}

// 以下块仅在 WITH_STRUCTUTILS_DEBUG（通常 = 非 Shipping）下编译，提供面向开发/测试的内省接口。
#if WITH_STRUCTUTILS_DEBUG
	/**
	 * Provides a debug string description of the bitset contents, via the provided FOutputDevice.
	 * @param Ar the output device to write to.
	 */
	// 输出 "TypeNameA, TypeNameB, ..." 形式的字符串到给定 FOutputDevice。
	void DebugGetStringDesc(FOutputDevice& Ar) const
	{
		for (int32 Index = 0; Index < StructTypesBitArray.Num(); ++Index)
		{
			if (StructTypesBitArray[Index])
			{
				// Get the name of the struct type and log it
				Ar.Logf(TEXT("%s, "), *StructTracker.DebugGetStructTypeName(Index).ToString());
			}
		}
	}

	/**
	 * Gets the names of individual struct types in the bitset.
	 * @param OutFNames Array to populate with struct type names.
	 */
	// 把位集中每个类型的名字填入 OutFNames，便于调试 UI 或日志差异比较。
	void DebugGetIndividualNames(TArray<FName>& OutFNames) const
	{
		for (int32 Index = 0; Index < StructTypesBitArray.Num(); ++Index)
		{
			if (StructTypesBitArray[Index])
			{
				OutFNames.Add(StructTracker.DebugGetStructTypeName(Index));
			}
		}
	}

	/**
	 * Retrieves the name of a struct type at a given index.
	 * @param StructTypeIndex the index of the struct type.
	 * @return The name of the struct type.
	 */
	FName DebugGetStructTypeName(const int32 StructTypeIndex) const
	{
		return StructTracker.DebugGetStructTypeName(StructTypeIndex);
	}

	/**
	 * Retrieves all registered struct types as a view.
	 * @return Array view of weak pointers to struct types.
	 */
	// 返回 tracker 中全部已注册类型的视图（弱引用形式），包括未置位的。
	TConstArrayView<TWeakObjectPtr<const TUStructType>> DebugGetAllStructTypes() const
	{
		return StructTracker.DebugGetAllStructTypes<TUStructType>();
	}

	/**
	 * Static method to retrieve the name of a struct type at a given index from a struct tracker.
	 * @param StructTracker the struct tracker to use.
	 * @param StructTypeIndex the index of the struct type.
	 * @return The name of the struct type.
	 */
	static FName DebugGetStructTypeName(const FStructTracker& StructTracker, const int32 StructTypeIndex)
	{
		return StructTracker.DebugGetStructTypeName(StructTypeIndex);
	}

	/**
	 * Static method to retrieve all registered struct types from a struct tracker.
	 * @param StructTracker the struct tracker to use.
	 * @return Array view of weak pointers to struct types.
	 */
	static TConstArrayView<TWeakObjectPtr<const TUStructType>> DebugGetAllStructTypes(const FStructTracker& StructTracker)
	{
		return StructTracker.DebugGetAllStructTypes<TUStructType>();
	}

protected:
	/** For unit testing purposes only */
	// 仅供单元测试直接检查底层位数组结构。
	const TBitArray<>& DebugGetStructTypesBitArray() const 
	{ 
		return StructTypesBitArray; 
	}

	TBitArray<>& DebugGetMutableStructTypesBitArray() 
	{ 
		return StructTypesBitArray; 
	}
#endif // WITH_STRUCTUTILS_DEBUG

protected:
	/**
	 * Retrieves the struct tracker used by this builder.
	 * @return Reference to the struct tracker.
	 */
	FStructTracker& GetStructTracker() const
	{
		return StructTracker;
	}

private:
	/** 
	 * Reference to the struct tracker used. It's TTypeBitSetBuilder's instance creator responsibility 
	 * to ensure that the instance doesn't outlive the referenced object.
	 *
	 * 中文说明：
	 *   对外部 FStructTracker 的引用。Builder 本身不拥有 tracker，创建方必须保证
	 *   tracker 的生命周期长于任何基于它构造的 Builder。在 Mass 里一般由 FBitTypeRegistry
	 *   （每种基类一个全局注册表）持有 tracker，因此 tracker 的生命期等同进程生命期。
	 */
	FStructTracker& StructTracker; 
};
