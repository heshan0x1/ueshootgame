// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =====================================================================================================================
// 文件角色：Mass ECS 核心类型的"枢纽头文件"。
// 此头文件在 MassEntity 模块内被大量 include，它声明/定义了以下基础设施：
//   1. 日志类别 LogMass 与性能统计组 STATGROUP_Mass（通过 DECLARE_* 宏）。
//   2. 五大 element 位集 + 外部 Subsystem 位集（通过 DECLARE_STRUCTTYPEBITSET_EXPORTED / DECLARE_CLASSTYPEBITSET_EXPORTED）：
//        FMassFragmentBitSet          —— 对应 FMassFragment 派生类
//        FMassTagBitSet               —— 对应 FMassTag 派生类
//        FMassChunkFragmentBitSet     —— 对应 FMassChunkFragment 派生类
//        FMassSharedFragmentBitSet    —— 对应 FMassSharedFragment 派生类
//        FMassConstSharedFragmentBitSet —— 对应 FMassConstSharedFragment 派生类
//        FMassExternalSubsystemBitSet —— 对应 USubsystem 派生类（processor 声明依赖的外部 Subsystem 集合）
//   3. FMassArchetypeCompositionDescriptor —— 一个 Archetype 的完整"组合指纹"（5 个位集）。
//      极其关键：两个实体若拥有相同的 Descriptor，就共享同一个 FMassArchetypeData —— 这是 Archetype ECS 的立身之本。
//   4. FMassArchetypeSharedFragmentValues —— 运行期"shared fragment 的实际值"容器。
//      注意它与 Descriptor 的差异：Descriptor 只记录"有哪些 shared 类型"，这里还记录具体哪个实例引用。
//   5. EMassObservedOperation / EMassObservedOperationFlags —— Observer 机制关心的 4 种变更操作。
//   6. EMassExecutionContextType —— 区分执行上下文是来自 Processor 还是业务代码"本地"调用。
//   7. UE::Mass::FExecutionLimiter —— 遍历限流器。
//   8. FMassGenericPayloadView / ViewSlice —— 类型擦除的列式 payload，用于批量 API。
//   9. UE::Mass::TMultiTypeList / TMultiArray —— 变参模板辅助（编译期强类型列表）。
//  10. FMassArchetypeCreationParams —— 创建 Archetype 时可选参数（chunk 大小、调试名等）。
// =====================================================================================================================

#include "StructUtils/StructTypeBitSet.h"
#include "MassProcessingTypes.h"
#include "StructUtils/StructArrayView.h"
#include "Subsystems/Subsystem.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
// 过时 include：5.6 起这些头文件不再默认导出，仅在打开旧包含顺序兼容宏时才引入。
#include "MassExternalSubsystemTraits.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "StructUtils/SharedStruct.h"
#include "MassEntityElementTypes.h"
#include "MassEntityConcepts.h"
#include "MassTestableEnsures.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassExternalSubsystemTraits.h"
#include "MassEntityHandle.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityTypes.generated.h"


// 日志类别：LogMass。在全模块范围内可用。Warning 及以上默认输出，All 表示编译期不裁剪任何等级。
// 实际定义点在 MassProcessingTypes.cpp 的 DEFINE_LOG_CATEGORY(LogMass)。
MASSENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogMass, Warning, All);

// 性能统计组：STATGROUP_Mass，用于 unreal insights / stat 命令分组。
DECLARE_STATS_GROUP(TEXT("Mass"), STATGROUP_Mass, STATCAT_Advanced);
// "Mass Total Frame Time"：一帧内 Mass 系统总耗时的 cycle stat。实际累加在 Processor/Phase 代码中。
DECLARE_CYCLE_STAT_EXTERN(TEXT("Mass Total Frame Time"), STAT_Mass_Total, STATGROUP_Mass, MASSENTITY_API);

// 展开下方宏：DECLARE_STRUCTTYPEBITSET_EXPORTED(API, BitSetType, BaseStructType)
// 会生成 `struct BitSetType : TStructTypeBitSet<BaseStructType, ...> { ... };` 及若干辅助符号，
// 并带上 MASSENTITY_API 供跨模块使用。
// 语义：BitSetType 是一个"位图"，每个 bit 对应注册过的一个 BaseStructType 派生类型。
// 五大 element 位集 —— Archetype / Query 的基础比较与匹配都依赖这些位集：
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassFragmentBitSet, FMassFragment);                       // 逐实体列式数据
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassTagBitSet, FMassTag);                                 // 零大小标签（仅位存在性）
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassChunkFragmentBitSet, FMassChunkFragment);             // 每个 chunk 一份的数据
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassSharedFragmentBitSet, FMassSharedFragment);           // 跨 archetype 共享（可写）
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassConstSharedFragmentBitSet, FMassConstSharedFragment); // 跨 archetype 共享（只读）
// 外部 Subsystem 位集：Processor 通过 AddRequirement 声明需要哪些 USubsystem，这里用位集描述整个需求集。
DECLARE_CLASSTYPEBITSET_EXPORTED(MASSENTITY_API, FMassExternalSubsystemBitSet, USubsystem);

// 前向声明（实现放在其他头/源文件）：
struct FMassArchetypeData;   // Archetype 的核心数据结构（列式 chunk 容器）
struct FMassEntityQuery;     // 查询对象（根据 requirements 匹配 archetype）

namespace UE::Mass
{
	// 编译期常量 false。用在 `static_assert(TAlwaysFalse<T>, ...)` 中：
	// 普通 static_assert(false, ...) 会在模板实例化之前就触发；借助模板参数 T 使其延迟到实例化时评估，
	// 是 "unreachable branch 的 compile-time error" 惯用法。
	template<typename T>
	static constexpr bool TAlwaysFalse = false;

	/**
	 * FExecutionLimiter is used to limit the execution of a query to a set entity count.
	 * 查询遍历的"限流器"：把 Query 的 ForEachEntityChunk 截到指定实体数量（EntityLimit）为止。
	 * 跨多次调用时通过 ChunkIndex/ArchetypeIndex 记忆上次遍历到哪里，实现"分批处理"。
	 * 所有成员除 EntityLimit 外都是 private，仅授信好友（FMassArchetypeData/FMassEntityQuery）可写。
	 */
	struct FExecutionLimiter
	{
		friend struct ::FMassArchetypeData;
		friend struct ::FMassEntityQuery;

		// 显式构造：指定本次最多处理多少实体。其它游标字段使用"尚未开始"的哨兵值。
		explicit FExecutionLimiter(int32 InEntityLimit)
			: EntityLimit(InEntityLimit)
			, ChunkIndex(INDEX_NONE)
			, ArchetypeIndex(INDEX_NONE)
			, MaxChunkIndex(INDEX_NONE)
			, EntityCountRemaining(0)
		{
		}
		
		int32 EntityLimit;      // 总限额（调用方配置，不变）

	private:
		int32 ChunkIndex;           // 当前处理到哪个 chunk（archetype 内部的索引）
		int32 ArchetypeIndex;       // 当前处理到哪个 archetype（Query 匹配到的 archetype 列表内索引）
		int32 MaxChunkIndex;        // 本次要处理的 chunk 上界（用于分批切片）
		int32 EntityCountRemaining; // 还剩多少实体可处理；归 0 即停止
	};
}

/** The type summarily describing a composition of an entity or an archetype. It contains information on both the
 *  fragments and tags 
 *  一个 Entity/Archetype 的"完整组合描述符"：由 5 个位集聚合而成。
 *  ========================================================================
 *  ★ ECS 核心概念：
 *    - Archetype 的身份 = Fragments + Tags + ChunkFragments + SharedFragments + ConstSharedFragments 这 5 个位集。
 *    - 两个实体若 Descriptor 相等（IsIdentical），则它们必然位于同一个 FMassArchetypeData 的 chunk 里。
 *    - 调整 Entity 组合（Add/Remove fragment/tag）= 改变 Descriptor = 需要把 Entity 迁移到新 Archetype。
 *  - 位集本身不保存类型对象或数据，只是每个类型占 1 bit 的位图；具体类型→bit 的映射由 TypeBitSet 的静态注册表维护。
 *  - CalculateHash：将 5 个位集哈希合并，作为 Archetype 的索引键（FMassEntityManager 用它去重 archetype）。
 *  ========================================================================
 *  注意：数据成员 Fragments / Tags / ... 在 5.7 被标记为 deprecated（建议走 Getter/Setter），
 *  但由于模板 GetContainer<T> 内部仍会直接引用，本 struct 被 PRAGMA_DISABLE_DEPRECATION_WARNINGS 包裹。 */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
struct FMassArchetypeCompositionDescriptor
{
	// 默认构造：所有位集为空（即无任何组合，CountStoredTypes()==0）。
	FMassArchetypeCompositionDescriptor() = default;
	// 拷贝式构造：五个位集都按值传入（const&，调用方保留）。
	FMassArchetypeCompositionDescriptor(const FMassFragmentBitSet& InFragments,
		const FMassTagBitSet& InTags,
		const FMassChunkFragmentBitSet& InChunkFragments,
		const FMassSharedFragmentBitSet& InSharedFragments,
		const FMassConstSharedFragmentBitSet& InConstSharedFragments)
		: Fragments(InFragments)
		, Tags(InTags)
		, ChunkFragments(InChunkFragments)
		, SharedFragments(InSharedFragments)
		, ConstSharedFragments(InConstSharedFragments)
	{}

	// 从 UScriptStruct 数组构造 Fragments 位集（会遍历每个 struct 并在位集中点亮对应 bit）。
	FMassArchetypeCompositionDescriptor(TConstArrayView<const UScriptStruct*> InFragments,
		const FMassTagBitSet& InTags,
		const FMassChunkFragmentBitSet& InChunkFragments,
		const FMassSharedFragmentBitSet& InSharedFragments,
		const FMassConstSharedFragmentBitSet& InConstSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragments), InTags, InChunkFragments, InSharedFragments, InConstSharedFragments)
	{}

	// 从 FInstancedStruct（即"带值的 fragment 实例"）数组构造 Fragments 位集；只取类型、忽略值。
	FMassArchetypeCompositionDescriptor(TConstArrayView<FInstancedStruct> InFragmentInstances,
		const FMassTagBitSet& InTags,
		const FMassChunkFragmentBitSet& InChunkFragments,
		const FMassSharedFragmentBitSet& InSharedFragments,
		const FMassConstSharedFragmentBitSet& InConstSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragmentInstances), InTags, InChunkFragments, InSharedFragments, InConstSharedFragments)
	{}

	// 移动构造：五个位集使用 MoveTemp 转移底层存储，避免拷贝大位集。
	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments,
		FMassTagBitSet&& InTags,
		FMassChunkFragmentBitSet&& InChunkFragments,
		FMassSharedFragmentBitSet&& InSharedFragments,
		FMassConstSharedFragmentBitSet&& InConstSharedFragments)
		: Fragments(MoveTemp(InFragments))
		, Tags(MoveTemp(InTags))
		, ChunkFragments(MoveTemp(InChunkFragments))
		, SharedFragments(MoveTemp(InSharedFragments))
		, ConstSharedFragments(MoveTemp(InConstSharedFragments))
	{}

	// 快捷构造：仅有 Fragments 位集（其它为空）。用于临时构造"增量"描述符。
	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments)
		: Fragments(MoveTemp(InFragments))
	{}

	// 快捷构造：仅有 Tags 位集。
	FMassArchetypeCompositionDescriptor(FMassTagBitSet&& InTags)
		: Tags(MoveTemp(InTags))
	{}

	// 重置所有 5 个位集为空（等价于恢复默认构造状态）。
	void Reset()
	{
		Fragments.Reset();
		Tags.Reset();
		ChunkFragments.Reset();
		SharedFragments.Reset();
		ConstSharedFragments.Reset();
	}

	/**
	 * Compares contents of two FMassArchetypeCompositionDescriptor instances, ignoring the trailing empty bits in the bitsets
	 * "等价"比较：忽略位集末尾未使用 bit（不同时间注册顺序导致位集容量不同，但有效 bit 相同则视为等价）。
	 * 用途：Archetype 去重/查找（例如 Query 匹配）。相比 IsIdentical 更宽松。
	 */
	bool IsEquivalent(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return Fragments.IsEquivalent(OtherDescriptor.Fragments)
			&& Tags.IsEquivalent(OtherDescriptor.Tags)
			&& ChunkFragments.IsEquivalent(OtherDescriptor.ChunkFragments)
			&& SharedFragments.IsEquivalent(OtherDescriptor.SharedFragments)
			&& ConstSharedFragments.IsEquivalent(OtherDescriptor.ConstSharedFragments);
	}

	/**
	 * Checks whether contents of two FMassArchetypeCompositionDescriptor instances are identical.
	 * 严格比较：要求底层存储的 bit 数与内容都完全一致（用 operator==）。
	 */
	bool IsIdentical(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return Fragments == OtherDescriptor.Fragments
			&& Tags == OtherDescriptor.Tags
			&& ChunkFragments == OtherDescriptor.ChunkFragments
			&& SharedFragments == OtherDescriptor.SharedFragments
			&& ConstSharedFragments == OtherDescriptor.ConstSharedFragments;
	}

	// 是否所有位集都为空。
	bool IsEmpty() const 
	{ 
		return Fragments.IsEmpty()
			&& Tags.IsEmpty()
			&& ChunkFragments.IsEmpty()
			&& SharedFragments.IsEmpty()
			&& ConstSharedFragments.IsEmpty();
	}

	// `*this` 是否完全包含 Other（每个位集都是 Other 对应位集的超集）。
	// Query 筛选 Archetype 时常用：要求 archetype 至少拥有 query 所需的所有元素。
	bool HasAll(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return Fragments.HasAll(OtherDescriptor.Fragments)
			&& Tags.HasAll(OtherDescriptor.Tags)
			&& ChunkFragments.HasAll(OtherDescriptor.ChunkFragments)
			&& SharedFragments.HasAll(OtherDescriptor.SharedFragments)
			&& ConstSharedFragments.HasAll(OtherDescriptor.ConstSharedFragments);
	}

	// 按位集并集："把 Other 的所有元素都加上"。位集本身实现了 operator+=。
	void Append(const FMassArchetypeCompositionDescriptor& OtherDescriptor)
	{
		Fragments += OtherDescriptor.Fragments;
		Tags += OtherDescriptor.Tags;
		ChunkFragments += OtherDescriptor.ChunkFragments;
		SharedFragments += OtherDescriptor.SharedFragments;
		ConstSharedFragments += OtherDescriptor.ConstSharedFragments;
	}

	// 按位集差集："把 Other 中出现的元素都从 this 中去掉"。
	void Remove(const FMassArchetypeCompositionDescriptor& OtherDescriptor)
	{
		Fragments -= OtherDescriptor.Fragments;
		Tags -= OtherDescriptor.Tags;
		ChunkFragments -= OtherDescriptor.ChunkFragments;
		SharedFragments -= OtherDescriptor.SharedFragments;
		ConstSharedFragments -= OtherDescriptor.ConstSharedFragments;
	}

	/**
	 * Finds all the elements contained in `this` while missing in `OtherDescriptor` and returns
	 * the data as a FMassArchetypeCompositionDescriptor instance
	 * 返回 "this \\ Other"（在 this 中有、在 Other 中无）的差集，作为新的 Descriptor。
	 * 用于计算 archetype 变更时需要添加/移除的 element 清单。
	 */
	FMassArchetypeCompositionDescriptor CalculateDifference(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		FMassArchetypeCompositionDescriptor Diff;

		Diff.Fragments = Fragments - OtherDescriptor.Fragments;
		Diff.Tags = Tags - OtherDescriptor.Tags;
		Diff.ChunkFragments = ChunkFragments - OtherDescriptor.ChunkFragments;
		Diff.SharedFragments = SharedFragments - OtherDescriptor.SharedFragments;
		Diff.ConstSharedFragments = ConstSharedFragments - OtherDescriptor.ConstSharedFragments;

		return Diff;
	}

	// 静态哈希计算（分 5 个位集分别求 hash 后 HashCombine）。用于 Archetype 注册表的键。
	MASSENTITY_API static uint32 CalculateHash(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags
		, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragmentBitSet
		, const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet);

	// 实例方法：委托到静态版本。
	uint32 CalculateHash() const 
	{
		return CalculateHash(Fragments, Tags, ChunkFragments, SharedFragments, ConstSharedFragments);
	}

	// 统计本 Descriptor 中各位集已注册/使用的类型总数（5 个位集的 CountStoredTypes 之和）。
	MASSENTITY_API int32 CountStoredTypes() const;

	// 调试输出：把 Descriptor 的 5 部分内容友好打印到 FOutputDevice。仅 WITH_MASSENTITY_DEBUG 下有效。
	MASSENTITY_API void DebugOutputDescription(FOutputDevice& Ar) const;

	// ---- 编译期泛型访问器：根据 element 基类类型自动选择对应位集成员 ----
	// 典型调用：GetContainer<FMassFragment>() 返回 Fragments。
	// 内部通过 if constexpr + std::is_same_v 做派发；落到 else 分支会触发 static_assert。
	template<typename T>
	auto& GetContainer() const;

	template<typename T>
	auto& GetContainer();

	// 基于 T（具体 fragment/tag 类型）判断本 Descriptor 是否包含该类型。
	// 内部先用 UE::Mass::TElementType<T> 推断它属于五大类别中的哪一个，再到对应位集里查。
	template<typename T>
	bool Contains() const;

	// 添加 T 对应的 bit 到对应位集。
	template<typename T>
	void Add();

	// ---- 类型明确的 Getter（推荐使用，替代直接访问成员） ----
	const FMassFragmentBitSet& GetFragments() const;
	const FMassTagBitSet& GetTags() const;
	const FMassChunkFragmentBitSet& GetChunkFragments() const;
	const FMassSharedFragmentBitSet& GetSharedFragments() const;
	const FMassConstSharedFragmentBitSet& GetConstSharedFragments() const;

	FMassFragmentBitSet& GetFragments();
	FMassTagBitSet& GetTags();
	FMassChunkFragmentBitSet& GetChunkFragments();
	FMassSharedFragmentBitSet& GetSharedFragments();
	FMassConstSharedFragmentBitSet& GetConstSharedFragments();

	// ---- Setter（整包替换某个位集） ----
	void SetFragments(const FMassFragmentBitSet& InBitSet);
	void SetTags(const FMassTagBitSet& InBitSet);
	void SetChunkFragments(const FMassChunkFragmentBitSet& InBitSet);
	void SetSharedFragments(const FMassSharedFragmentBitSet& InBitSet);
	void SetConstSharedFragments(const FMassConstSharedFragmentBitSet& InBitSet);

	// ---- 数据成员 ----
	// 自 5.7 起直接访问被 deprecated；推荐用对应 Getter/Setter。
	// 保留是为了二进制/源码兼容旧调用方。
	UE_DEPRECATED(5.7, "Direct access to FMassArchetypeCompositionDescriptor's bitsets is deprecated. Use any of the newly added getters instead.")
	FMassFragmentBitSet Fragments;                  // 逐实体列式数据类型集合

	UE_DEPRECATED(5.7, "Direct access to FMassArchetypeCompositionDescriptor's bitsets is deprecated. Use any of the newly added getters instead.")
	FMassTagBitSet Tags;                            // 零尺寸标签集合（只需存在性）

	UE_DEPRECATED(5.7, "Direct access to FMassArchetypeCompositionDescriptor's bitsets is deprecated. Use any of the newly added getters instead.")
	FMassChunkFragmentBitSet ChunkFragments;        // 每 chunk 一份的数据类型集合

	UE_DEPRECATED(5.7, "Direct access to FMassArchetypeCompositionDescriptor's bitsets is deprecated. Use any of the newly added getters instead.")
	FMassSharedFragmentBitSet SharedFragments;      // 可写共享 fragment 类型集合

	UE_DEPRECATED(5.7, "Direct access to FMassArchetypeCompositionDescriptor's bitsets is deprecated. Use any of the newly added getters instead.")
	FMassConstSharedFragmentBitSet ConstSharedFragments; // 只读共享 fragment 类型集合

	// ---- 已废弃的构造函数（缺少 ConstSharedFragmentBitSet 参数） ----
	// 5.5 之前 ConstSharedFragment 尚未独立分类，此处提供默认空位集的兼容重载。
	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(InFragments, InTags, InChunkFragments, InSharedFragments, FMassConstSharedFragmentBitSet())
	{}

	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(TConstArrayView<const UScriptStruct*> InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragments), InTags, InChunkFragments, InSharedFragments, FMassConstSharedFragmentBitSet())
	{}

	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(TConstArrayView<FInstancedStruct> InFragmentInstances, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragmentInstances), InTags, InChunkFragments, InSharedFragments, FMassConstSharedFragmentBitSet())
	{}

	// 注意：这个 move 构造的兼容版本只会 ensure 失败——它被标注了 deprecated 且"不再工作"。
	// 调用方必须迁移到 5 参数版本。保留此重载只是为了让旧编译单元仍能链接。
	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments, FMassTagBitSet&& InTags, FMassChunkFragmentBitSet&& InChunkFragments, FMassSharedFragmentBitSet&& InSharedFragments)
	{
		ensureMsgf(false, TEXT("This constructor is defunct. Please update your implementation based on deprecation warning."));
	}
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

/** 
 * Wrapper for const and non-const shared fragment containers that tracks which struct types it holds (via a FMassSharedFragmentBitSet).
 * Note that having multiple instanced of a given struct type is not supported and Add* functions will fetch the previously 
 * added fragment instead of adding a new one.
 * 
 * 共享 fragment "值"容器：保存可写 / 只读 两类 shared fragment 的实际实例引用（FSharedStruct / FConstSharedStruct）。
 * -----------------------------------------------------------------------
 * ★ 与 FMassArchetypeCompositionDescriptor 的分工：
 *   - Descriptor 只说："这个 archetype 有 A、B、C 三种 shared fragment 类型"（位集层面）。
 *   - 本类说："这个 archetype 的当前实例中，shared A 指向内存地址 X、shared B 指向 Y、..."
 *   - 同一个 archetype 可能对应多个 SharedFragmentValues（shared 值不同即 archetype 内部被细分为不同"子档"）。
 * 
 * ★ 每种类型只能存一个实例：Add* 若检测到同类型已存在，会触发 ensure 并返回既存实例，而不是新增。
 * 
 * ★ 哈希/排序：为了让"值集合相等"在 TMap/TSet 中被一致识别，CalculateHash() 要求数组先 Sort()。
 *   标志 bSorted 通过 DirtyHashCache() 在每次修改后置位控制。 */
struct FMassArchetypeSharedFragmentValues
{
	// 默认构造：空容器。bSorted 初始为 true（视"空"为已排序）。
	FMassArchetypeSharedFragmentValues() = default;
	FMassArchetypeSharedFragmentValues(const FMassArchetypeSharedFragmentValues& OtherFragmentValues) = default;
	FMassArchetypeSharedFragmentValues(FMassArchetypeSharedFragmentValues&& OtherFragmentValues) = default;
	FMassArchetypeSharedFragmentValues& operator=(const FMassArchetypeSharedFragmentValues& OtherFragmentValues) = default;
	FMassArchetypeSharedFragmentValues& operator=(FMassArchetypeSharedFragmentValues&& OtherFragmentValues) = default;

	// 同时对比可写与只读两份位集是否"完全一致"。等价关系，不考虑位集存储长度差异。
	inline bool HasExactFragmentTypesMatch(const FMassSharedFragmentBitSet& InSharedFragmentBitSet, const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet) const
	{
		return HasExactSharedFragmentTypesMatch(InSharedFragmentBitSet)
			&& HasExactConstSharedFragmentTypesMatch(InConstSharedFragmentBitSet);
	}

	// 可写 shared fragment 类型集合完全匹配？
	inline bool HasExactSharedFragmentTypesMatch(const FMassSharedFragmentBitSet& InSharedFragmentBitSet) const
	{
		return SharedFragmentBitSet.IsEquivalent(InSharedFragmentBitSet);
	}

	// 是否"至少"包含 InSharedFragmentBitSet 的所有类型（superset 关系）。
	inline bool HasAllRequiredSharedFragmentTypes(const FMassSharedFragmentBitSet& InSharedFragmentBitSet) const
	{
		return SharedFragmentBitSet.HasAll(InSharedFragmentBitSet);
	}

	// 只读 shared fragment 类型完全匹配？
	inline bool HasExactConstSharedFragmentTypesMatch(const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet) const
	{
		return ConstSharedFragmentBitSet.IsEquivalent(InConstSharedFragmentBitSet);
	}

	// 是否包含 InConstSharedFragmentBitSet 的所有类型。
	inline bool HasAllRequiredConstSharedFragmentTypes(const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet) const
	{
		return ConstSharedFragmentBitSet.HasAll(InConstSharedFragmentBitSet);
	}

	/**
	 * @return whether the stored shared fragment values exactly match shared fragment types indicated by InDescriptor
	 * 判断本容器持有的类型集合是否与 Descriptor 声明的 shared 类型完全一致。
	 * 用于校验"SharedFragmentValues 是否与 Archetype 匹配"。
	 */
	bool DoesMatchComposition(const FMassArchetypeCompositionDescriptor& InDescriptor) const 
	{
		return HasExactSharedFragmentTypesMatch(InDescriptor.GetSharedFragments())
			&& HasExactConstSharedFragmentTypesMatch(InDescriptor.GetConstSharedFragments());
	}

	// "哈希等价"比较。由于 CalculateHash 已经包括"指针 PointerHash + 顺序"，哈希相等即近似值相等。
	// 需要更严格可用 HasSameValues。
	inline bool IsEquivalent(const FMassArchetypeSharedFragmentValues& OtherSharedFragmentValues) const
	{
		return GetTypeHash(*this) == GetTypeHash(OtherSharedFragmentValues);
	}

	/** 
	 * Compares contents of `this` and the Other, and allows different order of elements in both containers.
	 * Note that the function ignores "nulls", i.e. empty FConstSharedStruct and FSharedStruct instances. The function
	 * does care however about matching "mode", meaning ConstSharedFragments and SharedFragments arrays are compared
	 * independently.
	 * 按"值"做顺序无关比较：可写 vs 只读独立比；忽略"空槽"（Reset 后没被 compact 掉的元素）。
	 * 返回 true 时：两个容器持有的"有效"shared fragment 值完全一致（含值相等）。
	 */
	MASSENTITY_API bool HasSameValues(const FMassArchetypeSharedFragmentValues& Other) const;

	// 按 UScriptStruct* 判断是否已有该类型的 shared fragment 值（无论可写/只读）。
	// 依据类型基类自动分发：FMassSharedFragment 查 SharedFragmentBitSet；FMassConstSharedFragment 查 ConstSharedFragmentBitSet。
	inline bool ContainsType(const UScriptStruct* FragmentType) const
	{
		if (UE::Mass::IsA<FMassSharedFragment>(FragmentType))
		{
			return SharedFragmentBitSet.Contains(*FragmentType);
		}

		if (UE::Mass::IsA<FMassConstSharedFragment>(FragmentType))
		{
			return ConstSharedFragmentBitSet.Contains(*FragmentType);
		}

		return false;
	}

	// 编译期版本：通过 C++20 concept（CConstSharedFragment / CSharedFragment）派发到对应位集。
	// 若 T 既非 const 也非 mutable shared fragment，返回 false（不会编译失败）。
	template<typename T>
	inline bool ContainsType() const
	{
		if constexpr (UE::Mass::CConstSharedFragment<T>)
		{
			return ConstSharedFragmentBitSet.Contains(*T::StaticStruct());
		}
		else if constexpr (UE::Mass::CSharedFragment<T>)
		{
			return SharedFragmentBitSet.Contains(*T::StaticStruct());
		}
		else
		{
			return false;
		}
	}

	/**
	 * Adds Fragment to the collection.
	 * Method will ensure if a fragment of the given FMassConstSharedFragment subclass has already been added.
	 * 添加只读 shared fragment。重复同类型会触发 ensure。
	 */
	void Add(const FConstSharedStruct& Fragment)
	{
		(void)Add_GetRef(Fragment);
	}

	/** 
	 * Adds Fragment to the collection.
	 * Method will ensure if a fragment of the given FMassConstSharedFragment subclass has already been added.
	 * In that case the method will return the previously added instance if the given type has been added
	 * as a CONST shared fragment and if not it will return an empty FConstSharedStruct.
	 * 添加并返回引用。
	 * - 若本类型已作为 CONST shared fragment 存在：返回既存实例，并触发 ensure 失败（"重复添加"）。
	 * - 若本类型曾以 NON-CONST 方式添加（"模式错误"）：返回空 FConstSharedStruct + ensure 失败。
	 * - 否则正常追加并返回新实例引用。
	 */
	MASSENTITY_API FConstSharedStruct Add_GetRef(const FConstSharedStruct& Fragment);

	// 5.6 废弃：命名"AddConstSharedFragment"不如"Add_GetRef"通用；函数本体无差异。
	UE_DEPRECATED(5.6, "Use Add or Add_GetRef instead depending on whether you need the return value.")
		FConstSharedStruct AddConstSharedFragment(const FConstSharedStruct& Fragment)
	{
		return Add_GetRef(Fragment);
	}

	/**
	 * Adds Fragment to the collection.
	 * Method will ensure if a fragment of the given FMassSharedFragment subclass has already been added.
	 * 添加可写 shared fragment；重复同类型触发 ensure。
	 */
	void Add(const FSharedStruct& Fragment)
	{
		(void)Add_GetRef(Fragment);
	}

	/** 
	 * Adds Fragment to the collection.
	 * Method will ensure if a fragment of the given FMassSharedFragment subclass has already been added.
	 * In that case the method will return the previously added instance if the given type has been added
	 * as a NON-CONST shared fragment and if not it will return an empty FSharedStruct.
	 * 与 const 版本对称。
	 */
	MASSENTITY_API FSharedStruct Add_GetRef(const FSharedStruct& Fragment);

	UE_DEPRECATED(5.6, "Use Add or Add_GetRef instead depending on whether you need the return value.")
	FSharedStruct AddSharedFragment(const FSharedStruct& Fragment)
	{
		return Add_GetRef(Fragment);
	}

	/**
	 * Finds instances of fragment types given by Fragments and replaces their values with contents of respective
	 * element of Fragments.
	 * Note that it's callers responsibility to ensure every fragment type in Fragments already has an instance in
	 * this FMassArchetypeSharedFragmentValues instance. Failing that assumption will result in ensure failure. 
	 * 替换已有可写 shared fragment 的值（按类型查找对应槽位并赋值）。
	 * 要求所有传入类型都已存在——这是"更新"而非"添加"接口，用于同 Archetype 但 shared 值需变动的场景。
	 */
	MASSENTITY_API void ReplaceSharedFragments(TConstArrayView<FSharedStruct> Fragments);

	/** 
	 * Appends contents of Other to `this` instance. All common fragments will get overridden with values in Other.
	 * Note that changing a fragments "role" (being const or non-const) is not supported and the function will fail an
	 * ensure when that is attempted.
	 * @return number of fragments added or changed
	 * 合并 Other 到 `this`：共有的类型会被 Other 的值覆盖；Other 独有的会被追加。
	 * 不支持改变"role"（如把一个原本 const 的类型当作 non-const 添加），违规时 ensure。
	 */
	MASSENTITY_API int32 Append(const FMassArchetypeSharedFragmentValues& Other);

	/** 
	 * Note that the function removes the shared fragments by type
	 * @return number of fragments types removed
	 * 按类型移除可写 shared fragment（只看位集匹配，不看值）。
	 */
	MASSENTITY_API int32 Remove(const FMassSharedFragmentBitSet& SharedFragmentToRemoveBitSet);

	/** 
	 * Note that the function removes the const shared fragments by type
	 * @return number of fragments types removed
	 * 按类型移除只读 shared fragment。
	 */
	MASSENTITY_API int32 Remove(const FMassConstSharedFragmentBitSet& ConstSharedFragmentToRemoveBitSet);

	/**
	 * Remove all the shared and const shared fragments indicated by InDescriptor
	 * @return number of fragments types removed
	 * Descriptor 版便捷重载：同时移除 shared + const shared。
	 */
	int32 Remove(const FMassArchetypeCompositionDescriptor& InDescriptor)
	{
		return Remove(InDescriptor.GetSharedFragments()) + Remove(InDescriptor.GetConstSharedFragments());
	}

	// 只读访问 const shared 数组。
	inline const TArray<FConstSharedStruct>& GetConstSharedFragments() const
	{
		return ConstSharedFragments;
	}

	// 可写访问 shared 数组（供外部代码就地修改元素）。
	inline TArray<FSharedStruct>& GetMutableSharedFragments()
	{
		return SharedFragments;
	}
	
	// 只读访问 shared 数组。
	inline const TArray<FSharedStruct>& GetSharedFragments() const
	{
		return SharedFragments;
	}
	
	// 按 UScriptStruct* 查找 const shared 实例；未找到返回空 FConstSharedStruct。
	FConstSharedStruct GetConstSharedFragmentStruct(const UScriptStruct* StructType) const
	{
		const int32 FragmentIndex = ConstSharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		return FragmentIndex != INDEX_NONE ? ConstSharedFragments[FragmentIndex] : FConstSharedStruct();
	}
		
	// 按类型查可写 shared；返回非 const 版本。
	FSharedStruct GetSharedFragmentStruct(const UScriptStruct* StructType)
	{
		const int32 FragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		return FragmentIndex != INDEX_NONE ? SharedFragments[FragmentIndex] : FSharedStruct();
	}

	// const 版本：注意这里返回值类型写的是 FConstSharedStruct 而实际函数体里返回的是 FSharedStruct（隐式转换到 FConstSharedStruct）。
	FConstSharedStruct GetSharedFragmentStruct(const UScriptStruct* StructType) const
	{
		const int32 FragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		return FragmentIndex != INDEX_NONE ? SharedFragments[FragmentIndex] : FSharedStruct();
	}

	const FMassSharedFragmentBitSet& GetSharedFragmentBitSet() const
	{
		return SharedFragmentBitSet;
	}

	const FMassConstSharedFragmentBitSet& GetConstSharedFragmentBitSet() const
	{
		return ConstSharedFragmentBitSet;
	}

	// 标记哈希缓存失效。任何修改成员后都必须调用它，否则 GetTypeHash 可能返回陈旧值。
	// 副作用：把 bSorted 重置为"元素数≤1"的情形（单元素默认已排序）。
	inline void DirtyHashCache()
	{
		HashCache = UINT32_MAX;
		// we consider a single shared fragment as being "sorted"
		// 把"元素数 ≤ 1"也视为"已排序"，简化后续 Sort 判断。
		bSorted = (SharedFragments.Num() + ConstSharedFragments.Num() <= 1) ;
	}

	// 若缓存无效则计算并写入 HashCache；否则直接用。UINT32_MAX 作为"未计算"哨兵值。
	inline void CacheHash() const
	{
		if (HashCache == UINT32_MAX)
		{
			HashCache = CalculateHash();
		}
	}

	// TMap/TSet 支持。首次访问会触发 CacheHash（可能内部计算）。
	friend inline uint32 GetTypeHash(const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
	{
		SharedFragmentValues.CacheHash();
		return SharedFragmentValues.HashCache;
	}

	// 真正的哈希计算：基于每个 shared struct 的内存指针 PointerHash。
	// 前置要求：bSorted==true（否则不同插入顺序会得到不同哈希，失去一致性）。
	MASSENTITY_API uint32 CalculateHash() const;
	SIZE_T GetAllocatedSize() const;                          // 统计堆分配大小（用于内存报表）

	// 按 UScriptStruct* 排序两个数组，让 CalculateHash 的顺序稳定。
	// 调用后 bSorted=true；已排序则为空操作。
	void Sort()
	{
		if(!bSorted)
		{
			ConstSharedFragments.Sort(FStructTypeSortOperator());
			SharedFragments.Sort(FStructTypeSortOperator());
			bSorted = true;
		}
	}

	bool IsSorted() const;           // 当前是否标记为已排序
	bool IsEmpty() const;            // 两个数组均为空？
	void Reset();                    // 清空所有成员与缓存

protected:
	// 哈希缓存。UINT32_MAX 表示"未计算/已失效"。
	mutable uint32 HashCache = UINT32_MAX;
	/**
	 * We consider empty FMassArchetypeSharedFragmentValues a sorted container.Same goes for a container containing
	 * a single element, @see DirtyHashCache
	 * 空容器以及"单元素"都默认视为已排序，以减少不必要的 Sort 调用。
	 */ 
	mutable bool bSorted = true; 
	
	FMassSharedFragmentBitSet SharedFragmentBitSet;        // 可写 shared 的"类型存在位集"（镜像 SharedFragments）
	FMassConstSharedFragmentBitSet ConstSharedFragmentBitSet; // 只读 shared 的类型位集（镜像 ConstSharedFragments）
	TArray<FConstSharedStruct> ConstSharedFragments;        // 只读 shared 实际引用数组
	TArray<FSharedStruct> SharedFragments;                  // 可写 shared 实际引用数组

public:
	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
	// 5.5 起的单参数 HasExactFragmentTypesMatch 已过时（歧义：是否也含 const）。
	// 请改用带两个参数的版本，或 HasExactSharedFragmentTypesMatch。
	UE_DEPRECATED(5.5, "HasExactFragmentTypesMatch is deprecated. Use HasExactSharedFragmentTypesMatch or the two-parameter version of HasExactFragmentTypesMatch.")
	inline bool HasExactFragmentTypesMatch(const FMassSharedFragmentBitSet& InSharedFragmentBitSet) const
	{
		return HasExactSharedFragmentTypesMatch(InSharedFragmentBitSet);
	}
};

/**
 * The enum is used to categorize any operation an entity can be a subject to.
 * 4 种 Observer 可监听的操作类型（单值枚举；多选见下方 Flags 版本）。
 * - AddElement / RemoveElement：对已存在实体增删 fragment / tag
 * - CreateEntity / DestroyEntity：整个实体的生死（相当于"一次性增/删全部 element"）
 * 
 * Observer 注册接口接收此枚举，以及 fragment 类型，二者组合形成订阅条件。
 */
UENUM()
enum class EMassObservedOperation : uint8
{
	AddElement,	// when an element (a fragment, tag...) is added to an existing entity
	            // 已存在实体上新增某个 fragment/tag 时触发
	RemoveElement,	// when an element (a fragment, tag...) is removed from an existing entity
	                // 已存在实体上移除某个 fragment/tag 时触发
	DestroyEntity,	// when an entity is destroyed, which is a special case of RemoveElement, because the entity gets all of its elements removed
	                // 实体被销毁——可看作"批量移除所有 element"的特殊情形
	CreateEntity,	// when an entity is created, which is a special case of AddElement, because the entity gets all of its elements added
	                // 实体被创建——"批量添加所有 element"的特殊情形

	// @todo another planned supported operation type
	// Touch,       // 计划扩展：Touch（写访问但不改变组合）
	// -- new operations above this line -- //

	MAX,            // 哨兵：枚举值数量

	// the following values are deprecated. Use one of the values above 
	// 下面两个是 5.x 前的旧名；新代码不要使用。
	Add UMETA(Deprecated, DisplayName="DEPRECATED_Add"),
	Remove UMETA(Deprecated, DisplayName="DEPRECATED_Remove")
};

// 上述 EMassObservedOperation 的 flags 版本，允许按位或组合订阅多个操作类型。
// 位值对齐：`1 << static_cast<uint8>(op)`，保证与 EMassObservedOperation 的编号一一对应。
// 派生组合值（Add / Remove / All）为常用场景的预设：例如 "Add" = "任一形式的添加"，
// "Remove" = "任一形式的移除"，"All" = 全部 4 种。
enum class EMassObservedOperationFlags : uint8
{
	None = 0,
	AddElement = 1 << static_cast<uint8>(EMassObservedOperation::AddElement),
	RemoveElement = 1 << static_cast<uint8>(EMassObservedOperation::RemoveElement),
	CreateEntity = 1 << static_cast<uint8>(EMassObservedOperation::CreateEntity),
	DestroyEntity = 1 << static_cast<uint8>(EMassObservedOperation::DestroyEntity),
	
	Add = AddElement | CreateEntity,      // 任意形式的"添加"
	Remove = RemoveElement | DestroyEntity, // 任意形式的"移除"
	All = Add | Remove,                    // 所有 4 种操作
};
ENUM_CLASS_FLAGS(EMassObservedOperationFlags);
// 将 flags 值转为调试字符串。实现见 MassEntityTypes.cpp。
MASSENTITY_API FString LexToString(const EMassObservedOperationFlags Value);

// 执行上下文类型：决定某次查询/命令是从 Processor 中还是从"本地业务代码"发起。
// 这会影响 CommandBuffer 的 flush 行为 —— Processor 流程中的命令会批量缓冲到阶段结束一并 flush，
// 本地流程则一般立即执行或按调用方需求 flush。
enum class EMassExecutionContextType : uint8
{
	Local,      // 业务代码直接调用（例如通过 EntityView）
	Processor,  // Processor Tick 流程中调用
	MAX
};

/** 
 * Note that this is a view and is valid only as long as the source data is valid. Used when flushing mass commands to
 * wrap different kinds of data into a uniform package so that it can be passed over to a common interface.
 * 类型擦除的"列式 payload 视图"：多个 FStructArrayView 组成的数组，每个 FStructArrayView 代表
 * 一种 fragment 类型的批量值（列）。典型用途：CommandBuffer flush 时把"新实体创建"这类批操作的
 * 数据按列打包，统一传给 FMassArchetypeData 实现批量写入。
 * 
 * 重要：这是"视图"类型，不拥有数据；源数组销毁后本视图失效。 */
struct FMassGenericPayloadView
{
	FMassGenericPayloadView() = default;
	// 从 TArray<FStructArrayView> 引用构造（指向该数组的存储）。
	FMassGenericPayloadView(TArray<FStructArrayView>&SourceData)
		: Content(SourceData)
	{}
	// 从 TArrayView<FStructArrayView> 构造（多层视图包装）。
	FMassGenericPayloadView(TArrayView<FStructArrayView> SourceData)
		: Content(SourceData)
	{}

	// 列数（即 fragment 类型数量，也是"多维数组"的宽度）。
	int32 Num() const { return Content.Num(); }

	// 让视图指向空；不会影响源数据。
	void Reset()
	{
		Content = TArrayView<FStructArrayView>();
	}

	// 对每列同时交换 A、B 两个元素。用于对 payload 的"整列"排序/洗牌（保持列之间的对齐关系）。
	inline void Swap(const int32 A, const int32 B)
	{
		for (FStructArrayView& View : Content)
		{
			View.Swap(A, B);
		}
	}

	/** Moves NumToMove elements to the back of the viewed collection. */
	/** 将 [StartIndex, StartIndex+NumToMove) 段的每列元素整体移动到列末尾。
	 *  相当于 payload 的"分区"操作：把要"延后处理"的实体（某些失败/跳过）集中到末尾。
	 *  实现见 MassEntityTypes.cpp：使用 Memmove + Memcpy 方式原地搬移，避免对象拷贝构造。 */
	void SwapElementsToEnd(int32 StartIndex, int32 NumToMove);

	TArrayView<FStructArrayView> Content;  // 底层视图数组（每个 FStructArrayView 是一列 typed view）
};

/**
 * Used to indicate a specific slice of a preexisting FMassGenericPayloadView, it's essentially an access pattern
 * Note: accessing content generates copies of FStructArrayViews stored (still cheap, those are just views). 
 * Payload 的"行切片"视图：对 FMassGenericPayloadView 给定 [StartIndex, StartIndex+Count) 行范围。
 * 支持 operator[] 读取某一列的对应切片（切片也是 view，不拥有数据）。
 */
struct FMassGenericPayloadViewSlice
{
	FMassGenericPayloadViewSlice() = default;
	// 构造：将 InSource 的所有列按相同 [InStartIndex, InStartIndex+InCount) 截取。
	FMassGenericPayloadViewSlice(const FMassGenericPayloadView& InSource, const int32 InStartIndex, const int32 InCount)
		: Source(InSource), StartIndex(InStartIndex), Count(InCount)
	{
	}

	// 取第 Index 列的切片（返回新的 FStructArrayView，内部只是重新设置 data ptr + count，开销低）。
	FStructArrayView operator[](const int32 Index) const
	{
		return Source.Content[Index].Slice(StartIndex, Count);
	}

	/** @return the number of "layers" (i.e. number of original arrays) this payload has been built from */
	/** 列数（与 Source.Num() 相同）。 */
	int32 Num() const 
	{
		return Source.Num();
	}

	// 注意逻辑：只有当 Source 至少有 1 列且 Count>0 时才视为非空。
	bool IsEmpty() const
	{
		return !(Source.Num() > 0 && Count > 0);
	}

private:
	FMassGenericPayloadView Source;   // 底层 payload 视图
	const int32 StartIndex = 0;        // 行起点（本切片从第几行开始）
	const int32 Count = 0;             // 行数
};

namespace UE::Mass
{
	/**
	 * A statically-typed list of related types. Used mainly to differentiate type collections at compile-type as well as
	 * efficiently produce TStructTypeBitSet representing given collection.
	 * 
	 * 编译期强类型列表（类似 std::tuple 的类型标签，但不带存储）。
	 * 设计目的：
	 *   1. 让批量 API 在模板签名里把"参与的 fragment 类型集合"编码为一个类型，
	 *      不同调用点有不同 TMultiTypeList<...>，编译期就能区分。
	 *   2. 提供 PopulateBitSet() 静态方法，把该类型列表转成对应的 TStructTypeBitSet。
	 * 
	 * 实现方式：变参模板递归继承——一层剥离 1 个类型，Super 是剩余 N-1 个类型的 TMultiTypeList。
	 * Ordinal 字段记录"本层在原始 pack 中的索引"（基于 0）。
	 */
	template<typename T, typename... TOthers>
	struct TMultiTypeList : TMultiTypeList<TOthers...>
	{
		using Super = TMultiTypeList<TOthers...>;
		// 去掉 T 的引用与 const 修饰符，得到纯类型。避免 T 被用户传成 "const MyFragment&" 时位集查不到。
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		enum
		{
			Ordinal = Super::Ordinal + 1  // 递归定义：Super 的序号 +1
		};

		// 把本层与所有上层的类型都点亮到 OutBitSet。
		// 依靠 TBitSetType::template GetTypeBitSet<FType>() 获取"只含单一类型位"的小位集。
		template<typename TBitSetType>
		constexpr static void PopulateBitSet(TBitSetType& OutBitSet)
		{
			Super::PopulateBitSet(OutBitSet);
			OutBitSet += TBitSetType::template GetTypeBitSet<FType>();
		}
	};
		
	/** Single-type specialization of TMultiTypeList. */
	/** 递归终止特化：只剩一个类型 T 时的基例，Ordinal=0。 */
	template<typename T>
	struct TMultiTypeList<T>
	{
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		enum
		{
			Ordinal = 0
		};

		template<typename TBitSetType>
		constexpr static void PopulateBitSet(TBitSetType& OutBitSet)
		{
			OutBitSet += TBitSetType::template GetTypeBitSet<FType>();
		}
	};

	/** 
	 * The type hosts a statically-typed collection of TArrays, where each TArray is strongly-typed (i.e. it contains 
	 * instances of given structs rather than structs wrapped up in FInstancedStruct). This type lets us do batched 
	 * fragment values setting by simply copying data rather than setting per-instance. 
	 * 
	 * 编译期多列数组容器。每"层"对应一个类型 T，内部存 TArray<T>。
	 * 与 FMassGenericPayloadView 的差别：
	 *   - FMassGenericPayloadView 是"运行期 + 类型擦除"的视图（FStructArrayView 一视同仁）。
	 *   - TMultiArray 是"编译期 + 强类型"的存储：每层都是 TArray<具体类型>，可直接 memcpy。
	 * 用法：批量为 N 个新实体赋 fragment 值时，可按类型逐列填充、再统一交给 Archetype。
	 * 
	 * GetAsGenericMultiArray 把强类型存储"降级"为 FStructArrayView 数组（即 PayloadView），
	 * 以便交给只识别 FStructArrayView 的统一接口。
	 */
	template<typename T, typename... TOthers>
	struct TMultiArray : TMultiArray<TOthers...>
	{
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		using Super = TMultiArray<TOthers...>;

		enum
		{
			Ordinal = Super::Ordinal + 1
		};

		// 统计包括本层在内所有层的分配内存大小（递归累加）。
		SIZE_T GetAllocatedSize() const
		{
			return FragmentInstances.GetAllocatedSize() + Super::GetAllocatedSize();
		}

		// 数组层数（类型数量）。
		int GetNumArrays() const { return Ordinal + 1; }

		// 按"先 T、再 TOthers..."顺序追加一行：每层 FragmentInstances 各 Add 一个元素。
		// 注意：参数顺序要与模板参数顺序一致。
		void Add(const FType& Item, TOthers... Rest)
		{
			FragmentInstances.Add(Item);
			Super::Add(Rest...);
		}

		// 把各层 TArray 转为 FStructArrayView 数组（类型擦除降级），输出到 A。
		// 递归顺序：先 Super（即"更外层"类型），再本层——外层类型排前，保持与 PopulateBitSet 同序。
		void GetAsGenericMultiArray(TArray<FStructArrayView>& A) /*const*/
		{
			Super::GetAsGenericMultiArray(A);
			A.Add(FStructArrayView(FragmentInstances));
		}

		// 把本 TMultiArray 涉及的所有 fragment 类型汇总到 OutBitSet（仅 FMassFragmentBitSet）。
		// 注意：该方法只支持 fragment 类型，若层内有 tag/chunk/shared 会静默不写入正确位。
		void GetheredAffectedFragments(FMassFragmentBitSet& OutBitSet) const
		{
			Super::GetheredAffectedFragments(OutBitSet);
			OutBitSet += FMassFragmentBitSet::GetTypeBitSet<FType>();
		}

		// 清空所有层的 TArray（保留容量）。
		void Reset()
		{
			Super::Reset();
			FragmentInstances.Reset();
		}

		TArray<FType> FragmentInstances;  // 本层持有的元素数组（强类型）
	};

	/**TMultiArray single-type specialization */
	/** 递归终止特化：单层，Ordinal=0。 */
	template<typename T>
	struct TMultiArray<T>
	{
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		enum { Ordinal = 0 };

		SIZE_T GetAllocatedSize() const
		{
			return FragmentInstances.GetAllocatedSize();
		}

		int GetNumArrays() const { return Ordinal + 1; }

		void Add(const FType& Item) { FragmentInstances.Add(Item); }

		void GetAsGenericMultiArray(TArray<FStructArrayView>& A) /*const*/
		{
			A.Add(FStructArrayView(FragmentInstances));
		}

		void GetheredAffectedFragments(FMassFragmentBitSet& OutBitSet) const
		{
			OutBitSet += FMassFragmentBitSet::GetTypeBitSet<FType>();
		}

		void Reset()
		{
			FragmentInstances.Reset();
		}

		TArray<FType> FragmentInstances;
	};

} // UE::Mass


// ---------------------------------------------------------------------------------------------------------------------
// FMassArchetypeCreationParams
// ---------------------------------------------------------------------------------------------------------------------
// 创建 Archetype 时的可选参数包。传给 FMassEntityManager::CreateArchetype()。
// 若不指定，默认使用 UE::Mass::ChunkSize（见 MassEntityManagerConstants.h）并不设置调试信息。
struct FMassArchetypeCreationParams
{
	FMassArchetypeCreationParams() = default;
	// 从已有 Archetype 复制"同形参数"（主要是 ChunkMemorySize）。用于复刻一个布局相似的新 archetype。
	explicit FMassArchetypeCreationParams(const struct FMassArchetypeData& Archetype);

	/** Created archetype will have chunks of this size. 0 denotes "use default" (see UE::Mass::ChunkSize) */
	/** 每个 chunk 的字节数。0 表示使用默认值（UE::Mass::ChunkSize）。调小可增加并行度，调大利于缓存局部性。 */
	int32 ChunkMemorySize = 0;

	/** Name to identify the archetype while debugging*/
	/** 调试用 Archetype 名字（不影响语义/哈希）。 */
	FName DebugName;

#if WITH_MASSENTITY_DEBUG
	// 调试可视化颜色（仅 WITH_MASSENTITY_DEBUG 构建生效）。
	FColor DebugColor{0};
#endif // WITH_MASSENTITY_DEBUG
};

//-----------------------------------------------------------------------------
// INLINES
//-----------------------------------------------------------------------------
// 以下内联模板/函数定义必须放在类外（而非类内），以便在包含本头的 TU 中展开。
// PRAGMA_DISABLE_DEPRECATION_WARNINGS 包裹是因为 Fragments/Tags 等成员已 deprecated，
// 但内部访问不应触发警告。
PRAGMA_DISABLE_DEPRECATION_WARNINGS
// GetContainer<T>() const：按 element 基类类型派发到对应位集。
// if constexpr 保证编译期选择唯一分支，不会产生无效代码。
// 落入 else：T 不是 5 大 element 基类之一 → 模板实例化时 static_assert 失败。
template<typename T>
auto& FMassArchetypeCompositionDescriptor::GetContainer() const
{
	if constexpr (std::is_same_v<FMassFragment, T>)
	{
		return Fragments;
	}
	else if constexpr (std::is_same_v<FMassTag, T>)
	{
		return Tags;
	}
	else if constexpr (std::is_same_v<FMassChunkFragment, T>)
	{
		return ChunkFragments;
	}
	else if constexpr (std::is_same_v<FMassSharedFragment, T>)
	{
		return SharedFragments;
	}
	else if constexpr (std::is_same_v<FMassConstSharedFragment, T>)
	{
		return ConstSharedFragments;
	}
	else
	{
		// 触发编译期错误：传入的 T 不是 Mass 的 5 种 element 基类之一。
		static_assert(UE::Mass::TAlwaysFalse<T>, "Unknown element type passed to GetContainer.");
	}
}

// GetContainer<T>() 非 const 版本。与 const 版本一一对应，仅返回类型不同。
template<typename T>
auto& FMassArchetypeCompositionDescriptor::GetContainer()
{
	if constexpr (std::is_same_v<FMassFragment, T>)
	{
		return Fragments;
	}
	else if constexpr (std::is_same_v<FMassTag, T>)
	{
		return Tags;
	}
	else if constexpr (std::is_same_v<FMassChunkFragment, T>)
	{
		return ChunkFragments;
	}
	else if constexpr (std::is_same_v<FMassSharedFragment, T>)
	{
		return SharedFragments;
	}
	else if constexpr (std::is_same_v<FMassConstSharedFragment, T>)
	{
		return ConstSharedFragments;
	}
	else
	{
		static_assert(UE::Mass::TAlwaysFalse<T>, "Unknown element type passed to GetContainer.");
	}
}

// Contains<T>()：传入的是具体 fragment/tag 类（比如 MyFragment），先通过 TElementType<T>
// 推断 T 的 element 基类（FMassFragment / FMassTag / ...），再取对应位集的 Contains<T>。
// 相比"自己拼两步调用"，这里把推断逻辑集中了。
template<typename T>
bool FMassArchetypeCompositionDescriptor::Contains() const
{
	using FElementType = UE::Mass::TElementType<T>;
	return GetContainer<FElementType>().template Contains<T>();
}

// Add<T>()：在对应位集上点亮 T 这一位。不操作任何实际数据——本结构只是"有哪些类型"的元信息。
template<typename T>
void FMassArchetypeCompositionDescriptor::Add()
{
	using FElementType = UE::Mass::TElementType<T>;
	GetContainer<FElementType>().template Add<T>();
}

// --- 内联 Getter/Setter 实现：trivial，直接转发到成员 ---
inline const FMassFragmentBitSet& FMassArchetypeCompositionDescriptor::GetFragments() const 
{ 
	return Fragments; 
}

inline const FMassTagBitSet& FMassArchetypeCompositionDescriptor::GetTags() const 
{ 
	return Tags; 
}

inline const FMassChunkFragmentBitSet& FMassArchetypeCompositionDescriptor::GetChunkFragments() const 
{ 
	return ChunkFragments; 
}

inline const FMassSharedFragmentBitSet& FMassArchetypeCompositionDescriptor::GetSharedFragments() const 
{ 
	return SharedFragments; 
}

inline const FMassConstSharedFragmentBitSet& FMassArchetypeCompositionDescriptor::GetConstSharedFragments() const 
{ 
	return ConstSharedFragments; 
}

inline FMassFragmentBitSet& FMassArchetypeCompositionDescriptor::GetFragments() 
{ 
	return Fragments; 
}

inline FMassTagBitSet& FMassArchetypeCompositionDescriptor::GetTags() 
{ 
	return Tags; 
}

inline FMassChunkFragmentBitSet& FMassArchetypeCompositionDescriptor::GetChunkFragments() 
{ 
	return ChunkFragments; 
}

inline FMassSharedFragmentBitSet& FMassArchetypeCompositionDescriptor::GetSharedFragments() 
{ 
	return SharedFragments; 
}

inline FMassConstSharedFragmentBitSet& FMassArchetypeCompositionDescriptor::GetConstSharedFragments() 
{ 
	return ConstSharedFragments; 
}

inline void FMassArchetypeCompositionDescriptor::SetFragments(const FMassFragmentBitSet& InBitSet)
{ 
	Fragments = InBitSet; 
}

inline void FMassArchetypeCompositionDescriptor::SetTags(const FMassTagBitSet& InBitSet)
{ 
	Tags = InBitSet; 
}

inline void FMassArchetypeCompositionDescriptor::SetChunkFragments(const FMassChunkFragmentBitSet& InBitSet)
{ 
	ChunkFragments = InBitSet; 
}

inline void FMassArchetypeCompositionDescriptor::SetSharedFragments(const FMassSharedFragmentBitSet& InBitSet)
{ 
	SharedFragments = InBitSet; 
}

inline void FMassArchetypeCompositionDescriptor::SetConstSharedFragments(const FMassConstSharedFragmentBitSet& InBitSet)
{ 
	ConstSharedFragments = InBitSet; 
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

// 内存报表：4 块存储累加。注：BitSet 本身是 inline 存储的小位图，也会有少量堆分配（超过阈值时）。
inline SIZE_T FMassArchetypeSharedFragmentValues::GetAllocatedSize() const
{
	return SharedFragmentBitSet.GetAllocatedSize()
		+ ConstSharedFragmentBitSet.GetAllocatedSize()
		+ ConstSharedFragments.GetAllocatedSize()
		+ SharedFragments.GetAllocatedSize();
}

inline bool FMassArchetypeSharedFragmentValues::IsSorted() const
{
	return bSorted;
}

inline bool FMassArchetypeSharedFragmentValues::IsEmpty() const
{
	return ConstSharedFragments.IsEmpty() && SharedFragments.IsEmpty();
}

// Reset：与 DirtyHashCache 相似，但这里 bSorted 被置为 false —— 差异在于 Reset 语义更彻底，
// 外部应当显式 Sort() 才能再进入"已排序"状态。注意这与 DirtyHashCache 的"空容器视为已排序"不同。
inline void FMassArchetypeSharedFragmentValues::Reset()
{
	HashCache = UINT32_MAX;
	bSorted = false; 
	SharedFragmentBitSet.Reset();
	ConstSharedFragmentBitSet.Reset();
	ConstSharedFragments.Reset();
	SharedFragments.Reset();
}
