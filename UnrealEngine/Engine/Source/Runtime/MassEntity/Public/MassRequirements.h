// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassExternalSubsystemTraits.h"
#include "Templates/SubclassOf.h"
#include "MassRequirements.generated.h"

struct FMassDebugger;
struct FMassArchetypeHandle;
struct FMassExecutionRequirements;
struct FMassRequirementAccessDetector;
class USubsystem;

// =====================================================================================
// 【需求声明系统总览 / Requirement Declaration System Overview】
//
// 本文件定义 Mass ECS 中"Processor 对数据的需求声明"。任何需要批量操作 entity 的代码
// （典型为 FMassEntityQuery 及其使用者 UMassProcessor）都必须先声明：
//   1) 想访问哪些 element（fragment / tag / chunkFragment / sharedFragment / constSharedFragment）
//   2) 以何种 **访问权限**（ReadOnly / ReadWrite / 不绑定）—— 见 EMassFragmentAccess
//   3) 以何种 **存在性语义**（必须有 / 至少有一个 / 必须没有 / 可选）—— 见 EMassFragmentPresence
//   4) 还要访问哪些 USubsystem，以及是否必须在 GameThread 执行
//
// 这些声明是**声明式**的：它不直接读写数据，而是描述"我需要什么"。框架据此：
//   * 在 archetype 容器中筛选出能满足需求的 archetype（DoesArchetypeMatchRequirements）
//   * 由 FMassProcessorDependencySolver 分析多 processor 间的读写冲突，得出依赖图与并行度
//     （读读可并行，读写/写写需互斥；这是典型 ECS 调度套路）
//   * 在 WITH_MASSENTITY_DEBUG 模式下用 FMassRequirementAccessDetector 在运行时校验
//
// 本文件中的两个核心结构 FMassSubsystemRequirements / FMassFragmentRequirements 是
// FMassEntityQuery 的基类。
// =====================================================================================

/**
 * EMassFragmentAccess - 访问权限
 *
 * 描述一个 processor "如何"访问某个 fragment（**读还是写**），与"是否必须存在"是正交的（见 EMassFragmentPresence）。
 * 这是依赖求解器进行并行度分析的关键输入：两个 processor 若都对同一 fragment 仅声明 ReadOnly，
 * 调度器允许它们并行运行；只要任何一方声明了 ReadWrite，就必须串行化（典型 RW-lock 语义）。
 */
UENUM()
enum class EMassFragmentAccess : uint8
{
	/** no binding required */
	/** None - 无需绑定。常用于"只看存在性"（如 Tag / 排他 None / 只 Match 不读数据）的场景。 */
	None, 

	/** We want to read the data for the fragment */
	/** ReadOnly - 只读。多个 processor 可并行读同一 fragment。 */
	ReadOnly,

	/** We want to read and write the data for the fragment */
	/** ReadWrite - 读写。同一 fragment 上不能与任何其它 R/W processor 并行。 */
	ReadWrite,

	MAX
};

/**
 * EMassFragmentPresence - 存在性语义
 *
 * 描述一个 archetype 必须以怎样的方式包含某个 element 才算"匹配"。这是 archetype 筛选的核心：
 * archetype 的 composition 是固定的位集，匹配过程就是**位集运算**（HasAll / HasAny / HasNone）。
 *
 * 与 EMassFragmentAccess 完全正交。任意组合都有意义，例如：
 *   ┌──────────────┬──────────┬──────────────────────────────────────────────────────┐
 *   │ Presence     │ Access   │ 典型用途                                             │
 *   ├──────────────┼──────────┼──────────────────────────────────────────────────────┤
 *   │ All          │ ReadOnly │ "我要读 Position"——典型遍历                          │
 *   │ All          │ ReadWrite│ "我要更新 Velocity"——典型 tick                       │
 *   │ All          │ None     │ "必须有 PlayerTag 但我不读它"                        │
 *   │ Any          │ ReadOnly │ "至少有 A 或 B 之一，我都能处理"                     │
 *   │ Optional     │ ReadOnly │ "若有 LOD 数据则用它优化，没有也行"                  │
 *   │ Optional     │ ReadWrite│ "若有 Trail 组件则更新它"                            │
 *   │ None         │ ReadOnly │ 不合法——None 表示不存在，谈不上访问，AccessMode 被忽略 │
 *   └──────────────┴──────────┴──────────────────────────────────────────────────────┘
 *
 * 注意：Tag 类型由于不携带数据，AccessMode 总是隐含为 None，只有 Presence 起作用。
 */
UENUM()
enum class EMassFragmentPresence : uint8
{
	/** All the required fragments must be present */
	/** All - 逻辑 AND。每一个声明的 element 都必须出现在 archetype 中。最常用。 */
	All,

	/** One of the required fragments must be present */
	/** Any - 逻辑 OR。声明的几个 element 中至少有一个出现即匹配。 */
	Any,

	/** None of the required fragments can be present */
	/** None - 逻辑 NOT。声明的 element 一个都不能出现（exclusion / 反向过滤）。 */
	None,

	/** If fragment is present we'll use it */
	/**
	 * Optional - 有则用、无则空。匹配时：
	 *   - 不影响硬性筛选（如果还有 All/Any/None 等其它声明，按那些规则匹配）
	 *   - 但 processor 拿到 chunk 时会对应 nullptr 或 valid pointer，由用户运行时判断
	 *   - 当 requirement 全是 Optional 时，特殊地走 DoesMatchAnyOptionals 分支
	 */
	Optional,

	MAX
};


/**
 * FMassFragmentRequirementDescription - 单条需求描述（三元组）
 *
 * 描述对**单个 element 类型**的一条需求：(struct类型, 访问权限, 存在性)。
 * 这是给"有数据的"element（fragment / chunk fragment / shared fragment / const shared fragment）用的；
 * 纯 Tag 不需要这个结构，因为 Tag 没有数据，只用 BitSet 即可表达存在性。
 *
 * 该结构会被存入 FMassFragmentRequirements 内的 4 个 TArray（按类别分别存放），
 * 同时同步更新对应类别的 BitSet（见 FMassFragmentRequirements 注释）。
 *
 * 排序：CPP 中 SortRequirements() 会对该数组按结构指针/名称进行确定性排序，
 * 以便后续 BindRequirementsWithMapping 时与 archetype 内的 FragmentConfigs 顺序一致，
 * 提升缓存命中率。
 */
struct FMassFragmentRequirementDescription
{
	FMassFragmentRequirementDescription() = default;
	/** 构造一条需求；InStruct 必须非空（构造体内 check）。 */
	FMassFragmentRequirementDescription(const UScriptStruct* InStruct, const EMassFragmentAccess InAccessMode, const EMassFragmentPresence InPresence);

	/** @return 是否需要把数据指针绑定给 processor（即 AccessMode != None）。Tag-like 需求不需要绑定。 */
	bool RequiresBinding() const;
	/** @return 是否为可选——Optional 或 Any 都视为"非强制"。 */
	bool IsOptional() const;

	/** these functions are used for sorting. See FScriptStructSortOperator */
	/** 用于排序——按 struct 大小作为次级 key。 */
	int32 GetStructureSize() const;

	/** 用于排序——按 struct FName 作为主 key（确定性顺序）。 */
	FName GetFName() const;

	/** 该需求关联的 element 类型 (UScriptStruct*)。 */
	const UScriptStruct* StructType = nullptr;
	/** 访问权限：决定调度器读写依赖以及是否需要 binding。 */
	EMassFragmentAccess AccessMode = EMassFragmentAccess::None;
	/** 存在性：决定 archetype 匹配时如何使用此条需求。 */
	EMassFragmentPresence Presence = EMassFragmentPresence::Optional;
};

/**
 *  FMassSubsystemRequirements is a structure that declares runtime subsystem access type given calculations require.
 *
 *  FMassSubsystemRequirements - Subsystem 访问需求声明
 *
 *  与 fragment 需求并列的另一类需求：processor 在执行时可能需要读/写某些 USubsystem
 *  （如 USmartObjectSubsystem、UNavigationSystemV1 等）。
 *
 *  【为什么 subsystem 没有 Any/None/Optional，只有 R/W？】
 *    - Subsystem 不参与 archetype 匹配——它是世界级单例，不挂在 entity 上。
 *    - Subsystem 的访问语义只剩"读/写"：用于依赖求解时判定多 processor 间的并行性。
 *    - 因此存储用两个并列位集即可：RequiredConstSubsystems（读）/ RequiredMutableSubsystems（写）。
 *
 *  【bRequiresGameThreadExecution 的来源】
 *    通过 TMassExternalSubsystemTraits<T>::GameThreadOnly 编译期 trait 决定。如果模板特化里
 *    标了 GameThreadOnly = true（或运行时通过 FSubsystemTypeTraits::bGameThreadOnly），就强制
 *    本 query 在 GameThread 执行。多个 subsystem 中只要有一个 GameThreadOnly，整体就 GT-only。
 *
 *  【运行时变体 vs 编译期模板变体】
 *    - 模板版 AddSubsystemRequirement<T>：编译期类型已知，trait 编译期决定，最常用。
 *    - 接 TSubclassOf<USubsystem>+bGameThreadOnly：运行时调度（如 BP 或表驱动）。
 *    - 接 TSubclassOf<USubsystem>+EntityManager：用 TypeManager 查 FSubsystemTypeTraits 决定 GT。
 */
struct FMassSubsystemRequirements
{

	friend FMassDebugger;
	friend FMassRequirementAccessDetector;

	/**
	 * 模板版本——编译期已知 subsystem 类型 T。
	 * @tparam T 必须是 USubsystem 子类，且必须存在 TMassExternalSubsystemTraits<T> 特化（否则编译报错）
	 * @param AccessMode 必须是 ReadOnly 或 ReadWrite，不可为 None / MAX（运行时 check）
	 * @return *this，支持链式调用
	 *
	 * 副作用：
	 *   - ReadOnly  → 在 RequiredConstSubsystems 位集中置位
	 *   - ReadWrite → 在 RequiredMutableSubsystems 位集中置位
	 *   - 若 trait 标记 GameThreadOnly，则 bRequiresGameThreadExecution |= true（"或"积累）
	 */
	template<typename T>
	FMassSubsystemRequirements& AddSubsystemRequirement(const EMassFragmentAccess AccessMode)
	{
		check(AccessMode != EMassFragmentAccess::None && AccessMode != EMassFragmentAccess::MAX);

		// Compilation errors here like: 'GameThreadOnly': is not a member of 'TMassExternalSubsystemTraits<USmartObjectSubsystem>
		// indicate that there is a missing header that defines the subsystem's trait or that you need to define one for that subsystem type.
		// @see "MassExternalSubsystemTraits.h" for details
		// 此处编译错误形如 "GameThreadOnly is not a member of TMassExternalSubsystemTraits<X>"，
		// 说明你忘了为 X 定义/包含 trait 头文件。详见 MassExternalSubsystemTraits.h。

		switch (AccessMode)
		{
		case EMassFragmentAccess::ReadOnly:
			RequiredConstSubsystems.Add<T>();
			bRequiresGameThreadExecution |= TMassExternalSubsystemTraits<T>::GameThreadOnly;
			break;
		case EMassFragmentAccess::ReadWrite:
			RequiredMutableSubsystems.Add<T>();
			bRequiresGameThreadExecution |= TMassExternalSubsystemTraits<T>::GameThreadOnly;
			break;
		default:
			check(false);
		}

		return *this;
	}

	/**
	 * 运行时版本——通过 TSubclassOf 指定子系统类，调用方显式提供 bGameThreadOnly。
	 * 适用于无法在编译期确定类型的场景（数据驱动 / 蓝图）。
	 */
	FMassSubsystemRequirements& AddSubsystemRequirement(const TSubclassOf<USubsystem> SubsystemClass, const EMassFragmentAccess AccessMode, const bool bGameThreadOnly)
	{
		check(AccessMode != EMassFragmentAccess::None && AccessMode != EMassFragmentAccess::MAX);

		switch (AccessMode)
		{
		case EMassFragmentAccess::ReadOnly:
			RequiredConstSubsystems.Add(**SubsystemClass);
			bRequiresGameThreadExecution |= bGameThreadOnly;
			break;
		case EMassFragmentAccess::ReadWrite:
			RequiredMutableSubsystems.Add(**SubsystemClass);
			bRequiresGameThreadExecution |= bGameThreadOnly;
			break;
		default:
			check(false);
		}

		return *this;
	}

	/**
	 * 运行时版本——通过 EntityManager 的 TypeManager 自动判定 GameThreadOnly。
	 * 内部调用 IsGameThreadOnlySubsystem(SubsystemClass, EntityManager) 查询 FSubsystemTypeTraits。
	 * 若类型未注册，安全地默认为 true（GT-only 更安全）。
	 */
	FMassSubsystemRequirements& AddSubsystemRequirement(const TSubclassOf<USubsystem> SubsystemClass, const EMassFragmentAccess AccessMode, const TSharedRef<FMassEntityManager>& EntityManager)
	{
		return AddSubsystemRequirement(SubsystemClass, AccessMode, IsGameThreadOnlySubsystem(SubsystemClass, EntityManager));
	}

	/**
	 * 已废弃 (5.6)。无 EntityManager 上下文时退化为 bGameThreadOnly = true，过于保守。
	 * 推荐使用带 EntityManager 的重载，或通过 FMassEntityQuery::AddSubsystemRequirement 间接调用。
	 */
	UE_DEPRECATED(5.6, "This flavor of FMassSubsystemRequirements::AddSubsystemRequirement is deprecated. Use one of the other flavors, or call FMassEntityQuery::AddSubsystemRequirement if applicable.")
	FMassSubsystemRequirements& AddSubsystemRequirement(const TSubclassOf<USubsystem> SubsystemClass, const EMassFragmentAccess AccessMode)
	{
		return AddSubsystemRequirement(SubsystemClass, AccessMode, /*bGameThreadOnly=*/true);
	}

	/** 清空所有声明（包括 GT-only 标志）。可重复使用本实例。 */
	MASSENTITY_API void Reset();

	/** @return 只读 subsystem 位集。 */
	const FMassExternalSubsystemBitSet& GetRequiredConstSubsystems() const;
	/** @return 读写 subsystem 位集。 */
	const FMassExternalSubsystemBitSet& GetRequiredMutableSubsystems() const;
	/** @return 是否两个位集都为空（即未声明任何 subsystem 需求）。 */
	bool IsEmpty() const;

	/** @return 是否要求在 GameThread 执行（任一声明的 subsystem 标记 GT-only 即返回 true）。 */
	bool DoesRequireGameThreadExecution() const;

	/**
	 * 把当前 subsystem 需求合入 OutRequirements——供 FMassProcessorDependencySolver 进行依赖图构建。
	 * 实现：
	 *   OutRequirements.RequiredSubsystems.Read  += RequiredConstSubsystems
	 *   OutRequirements.RequiredSubsystems.Write += RequiredMutableSubsystems
	 * Solver 据此判定哪些 processor 间存在写后读 / 写后写依赖。
	 */
	MASSENTITY_API void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

	friend uint32 GetTypeHash(const FMassSubsystemRequirements& Instance);

protected:
	/**
	 * 静态辅助：通过 EntityManager.TypeManager 查 SubsystemClass 是否标记 GameThreadOnly。
	 * 若类型信息缺失（ensure 失败），默认返回 true（GT-only 更保守、更安全）。
	 */
	MASSENTITY_API static bool IsGameThreadOnlySubsystem(const TSubclassOf<USubsystem> SubsystemClass, const TSharedRef<FMassEntityManager>& EntityManager);

	/** 只读 subsystem 集合（位集，按 FMassExternalSubsystemBitSet 编码 USubsystem 子类）。 */
	FMassExternalSubsystemBitSet RequiredConstSubsystems;
	/** 读写 subsystem 集合。 */
	FMassExternalSubsystemBitSet RequiredMutableSubsystems;

private:
	/** 任一声明的 subsystem GT-only 即为 true，整体 query 必须 GT 执行。 */
	bool bRequiresGameThreadExecution = false;
};

/** 
 *  FMassFragmentRequirements is a structure that describes properties required of an archetype that's a subject of calculations.
 *
 *  FMassFragmentRequirements - Fragment / Tag / Chunk / Shared / ConstShared 完整需求描述
 *
 *  这是 Mass query 的核心数据结构。每条 query 用 5 类 element 各 4 套位集来精确描述
 *  "我需要怎样的 archetype"，再加上 4 个 TArray<FMassFragmentRequirementDescription> 来记录
 *  对那些"有数据"的 element 的访问模式（access mode）。
 *
 *  =============================================================================
 *  【20 个位集 × 5 类 element 的设计】
 *  =============================================================================
 *  对每一类 element（fragment / tag / chunkFragment / sharedFragment / constSharedFragment），
 *  都维护 4 个位集（共 5 × 4 = 20 个，但 chunk/shared/constShared 不支持 Any，所以实际略少）：
 *
 *      RequiredAll*       —— "AND" 语义：archetype 必须**全部**包含这些
 *      RequiredAny*       —— "OR"  语义：archetype 至少**有一个**包含这些
 *      RequiredOptional*  —— "If present" 语义：可选；不影响硬筛选；运行时 nullable
 *      RequiredNone*      —— "NOT" 语义：archetype **一个都不能**包含这些
 *
 *  这样设计的核心好处：**匹配过程纯粹是位集运算**，O(BitSetWords) 即完成 archetype 过滤；
 *  且四个互斥语义可以**任意组合**（同一个 query 可同时声明 All A/B + None C + Optional D）。
 *
 *  注意：
 *    - tag 没有 access mode，所以没有 TArray<FMassFragmentRequirementDescription> 存它。
 *    - chunk / shared / constShared **不支持 Any**（实现时 check 拒绝），因为这类 element
 *      在 archetype 上的语义不天然适合"或"——一个 archetype 的 chunk-fragment 集合就那么几个，
 *      用 All / Optional 已足够覆盖语义。
 *    - constShared 强制 ReadOnly（运行时 ensureMsgf）。
 *
 *  =============================================================================
 *  【bHasPositive / bHasNegative / bHasOptionalRequirements 缓存标志】
 *  =============================================================================
 *  CacheProperties() 会一次性计算并缓存这三个标志，供 DoesArchetypeMatchRequirements 快速分支：
 *    bHasPositive = 有任何 All/Any 类位集非空
 *    bHasNegative = 有任何 None     类位集非空
 *    bHasOptional = 有任何 Optional 类位集非空
 *  IncrementChangeCounter() 在每次修改后将 bPropertiesCached 置 false，下次自动重算。
 *
 *  =============================================================================
 *  【匹配算法 DoesArchetypeMatchRequirements】（详见 .cpp）
 *  =============================================================================
 *  1) 先过 negative filter：archetype 不能包含任何 None* 中的元素
 *  2) 再过 positive filter：archetype 必须包含所有 All*，并且若 Any* 非空则至少包含其中之一
 *  3) 若没有 positive 但有 optional，走 DoesMatchAnyOptionals：archetype 至少包含一个 Optional
 *  4) 若 positive/negative/optional 都空，return true（空 query 匹配一切，但 CheckValidity 会判 invalid）
 *
 *  =============================================================================
 *  【生命周期】
 *  =============================================================================
 *    构造 → Initialize(EntityManager) → 多次 AddXxxRequirement(...) → CheckValidity → 用于 query
 *  IncrementalChangesCount 用于让上层（FMassEntityQuery::CacheArchetypes）感知"位集已变更，
 *  需要重新刷新匹配的 archetype 缓存"。
 */
struct FMassFragmentRequirements
{
	friend FMassDebugger;
	friend FMassRequirementAccessDetector;

	FMassFragmentRequirements() = default;
	/** 从 TSharedPtr 构造——非空时自动 Initialize。 */
	MASSENTITY_API explicit FMassFragmentRequirements(const TSharedPtr<FMassEntityManager>& EntityManager);
	/** 从 TSharedRef 构造——总是 Initialize。推荐路径。 */
	MASSENTITY_API explicit FMassFragmentRequirements(const TSharedRef<FMassEntityManager>& EntityManager);

	/**
	 * 绑定 EntityManager，置 bInitialized = true。重复初始化（且换了不同的 manager）会发警告。
	 * 必须先 Initialize 才能调用任何 AddXxxRequirement（内部 check）。
	 */
	MASSENTITY_API void Initialize(const TSharedRef<FMassEntityManager>& EntityManager);

	/**
	 * 通用入口：根据 ElementType 自动派发到 AddRequirement（fragment）或 AddTagRequirement（tag）。
	 * 注意此函数仅区分 fragment 与 tag，对 chunk/shared 类型不会路由到正确的 Add 函数。
	 */
	FMassFragmentRequirements& AddElementRequirement(TNotNull<const UScriptStruct*> ElementType, const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		if (UE::Mass::IsA<FMassFragment>(ElementType))
		{
			return AddRequirement(ElementType, AccessMode, Presence);
		}
		return AddTagRequirement(ElementType, Presence);
	}

	/**
	 * 添加一条 fragment 需求（运行时类型版本）。
	 * @param FragmentType  必须是 FMassFragment 子类
	 * @param AccessMode    None / ReadOnly / ReadWrite
	 * @param Presence      All / Any / Optional / None
	 *
	 * 副作用：
	 *   - Presence != None：把三元组追加到 FragmentRequirements 数组（用于 binding 阶段）
	 *   - 把 FragmentType 加入对应的 RequiredAll/Any/Optional/None 位集
	 *   - 调用 IncrementChangeCounter() 让 archetype 缓存失效
	 *
	 * 同一类型重复添加会触发 check 失败（不允许重复）。
	 */
	FMassFragmentRequirements& AddRequirement(const UScriptStruct* FragmentType, const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(FragmentRequirements.FindByPredicate([FragmentType](const FMassFragmentRequirementDescription& Item){ return Item.StructType == FragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *GetNameSafe(FragmentType));
		
		if (Presence != EMassFragmentPresence::None)
		{
			FragmentRequirements.Emplace(FragmentType, AccessMode, Presence);
		}

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneFragments.Add(*FragmentType);
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		// 触发下次 query 使用时（或 CacheArchetypes 调用时）重新计算缓存。
		IncrementChangeCounter();
		return *this;
	}

	/** FMassFragmentRequirements ref returned for chaining */
	/**
	 * 模板版 AddRequirement——编译期类型 T 已知，享受 static_assert 校验（CFragment concept）。
	 * 推荐路径，所有 processor 模板代码使用此版本。
	 */
	template<typename T>
	FMassFragmentRequirements& AddRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(FragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());

		static_assert(UE::Mass::CFragment<T>, MASS_INVALID_FRAGMENT_MSG);
		
		if (Presence != EMassFragmentPresence::None)
		{
			FragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
		}
		
		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllFragments.Add<T>();
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyFragments.Add<T>();
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalFragments.Add<T>();
			break;
		case EMassFragmentPresence::None:
			RequiredNoneFragments.Add<T>();
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		IncrementChangeCounter();
		return *this;
	}

	/**
	 * 添加 tag 需求（运行时类型版本）。Tag 没有 access mode（只看存在性）。
	 * 注意：与 fragment 不同，**tag 不会写入 TArray<FMassFragmentRequirementDescription>**，
	 * 因为 tag 不需要数据 binding。
	 */
	FMassFragmentRequirements& AddTagRequirement(TNotNull<const UScriptStruct*> TagType, const EMassFragmentPresence Presence)
	{
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(int(Presence) != int(EMassFragmentPresence::MAX), TEXT("MAX presence is not a valid value for AddTagRequirement"));
		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllTags.Add(*TagType);
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyTags.Add(*TagType);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneTags.Add(*TagType);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalTags.Add(*TagType);
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/** 引用版本——转发到 TNotNull<const UScriptStruct*> 重载。 */
	void AddTagRequirement(const UScriptStruct& TagType, const EMassFragmentPresence Presence)
	{
		AddTagRequirement(&TagType, Presence);
	}

	/**
	 * 模板版 AddTagRequirement——编译期约束 T : FMassTag。
	 */
	template<typename T>
	FMassFragmentRequirements& AddTagRequirement(const EMassFragmentPresence Presence)
	{
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(int(Presence) != int(EMassFragmentPresence::MAX), TEXT("MAX presence is not a valid value for AddTagRequirement"));
		static_assert(UE::Mass::CTag<T>, "Given struct doesn't represent a valid tag type. Make sure to inherit from FMassFragment or one of its child-types.");
		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllTags.Add<T>();
				break;
			case EMassFragmentPresence::Any:
				RequiredAnyTags.Add<T>();
				break;
			case EMassFragmentPresence::None:
				RequiredNoneTags.Add<T>();
				break;
			case EMassFragmentPresence::Optional:
				RequiredOptionalTags.Add<T>();
				break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/** actual implementation in specializations */
	/**
	 * 批量加 tag——直接 OR 进对应位集。基础模板留空，实际实现在文件末尾的 4 个特化中
	 * （All/Any/None/Optional 各一）。
	 *
	 * 用途：从一个已有的 tag 集合（FMassTagBitSet）一次性合入需求，避免逐个 AddTagRequirement<T>()。
	 * 典型场景：spawn 模板带来的 tag 集合直接转 query 需求。
	 */
	template<EMassFragmentPresence Presence> 
	FMassFragmentRequirements& AddTagRequirements(const FMassTagBitSet& TagBitSet)
	{
		static_assert(Presence == EMassFragmentPresence::None || Presence == EMassFragmentPresence::All || Presence == EMassFragmentPresence::Any
			, "The only valid values for AddTagRequirements are All, Any and None");
		return *this;
	}

	/** Clears given tags out of all collected requirements, including negative ones */
	/**
	 * 从所有 4 套 tag 位集中清除指定 tag——用于动态修改/合并 query。
	 * 例如某个父 query 默认带 RequiredAllTags=PlayerTag，子 query 想"放宽"，
	 * 用这个 API 把它从 All/Any/None/Optional 全部移除。
	 */
	MASSENTITY_API FMassFragmentRequirements& ClearTagRequirements(const FMassTagBitSet& TagsToRemoveBitSet);

	/**
	 * 添加 chunk fragment 需求（模板版）。
	 * Chunk fragment 是按 chunk 而非按 entity 存储的元数据（每 chunk 一份）。
	 * 注意：**不支持 Presence::Any**（运行时 check 拒绝）；同一类型不能重复添加。
	 *
	 * 注意 None 分支不会写入 ChunkFragmentRequirements 数组（无 binding 必要）。
	 */
	template<typename T>
	FMassFragmentRequirements& AddChunkRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(UE::Mass::CChunkFragment<T>, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(ChunkFragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddChunkRequirement."));

		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllChunkFragments.Add<T>();
				ChunkFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
				break;
			case EMassFragmentPresence::Optional:
				RequiredOptionalChunkFragments.Add<T>();
				ChunkFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
				break;
			case EMassFragmentPresence::None:
				RequiredNoneChunkFragments.Add<T>();
				break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/** 添加 chunk fragment 需求（运行时类型版本）。语义同模板版。 */
	FMassFragmentRequirements& AddChunkRequirement(TNotNull<const UScriptStruct*> ChunkFragmentType, const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(ChunkFragmentRequirements.FindByPredicate([&ChunkFragmentType](const FMassFragmentRequirementDescription& Item) { return Item.StructType == ChunkFragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *ChunkFragmentType->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddChunkRequirement."));

		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllChunkFragments.Add(*ChunkFragmentType);
				ChunkFragmentRequirements.Emplace(ChunkFragmentType, AccessMode, Presence);
				break;
			case EMassFragmentPresence::Optional:
				RequiredOptionalChunkFragments.Add(*ChunkFragmentType);
				ChunkFragmentRequirements.Emplace(ChunkFragmentType, AccessMode, Presence);
				break;
			case EMassFragmentPresence::None:
				RequiredNoneChunkFragments.Add(*ChunkFragmentType);
				break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/**
	 * 添加 const shared fragment 需求（模板版）。
	 * Const shared fragment 是只读的、按 archetype 共享的不可变数据（如配置/settings）。
	 * 因此：
	 *   - **AccessMode 强制为 ReadOnly**（在 Emplace 时硬编码）
	 *   - 不支持 Presence::Any
	 */
	template<typename T>
	FMassFragmentRequirements& AddConstSharedRequirement(const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(UE::Mass::CConstSharedFragment<T>, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(ConstSharedFragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddConstSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllConstSharedFragments.Add<T>();
			ConstSharedFragmentRequirements.Emplace(T::StaticStruct(), EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalConstSharedFragments.Add<T>();
			ConstSharedFragmentRequirements.Emplace(T::StaticStruct(), EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneConstSharedFragments.Add<T>();
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/** 添加 const shared fragment 需求（运行时类型版本）。校验类型属于 FMassConstSharedFragment。 */
	FMassFragmentRequirements& AddConstSharedRequirement(const UScriptStruct* FragmentType, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		if (!ensureMsgf(UE::Mass::IsA<FMassConstSharedFragment>(FragmentType)
			, TEXT("Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.")))
		{
			return *this;
		}

		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(ConstSharedFragmentRequirements.FindByPredicate([FragmentType](const FMassFragmentRequirementDescription& Item) { return Item.StructType == FragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *FragmentType->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddConstSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllConstSharedFragments.Add(*FragmentType);
			ConstSharedFragmentRequirements.Emplace(FragmentType, EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalConstSharedFragments.Add(*FragmentType);
			ConstSharedFragmentRequirements.Emplace(FragmentType, EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneConstSharedFragments.Add(*FragmentType);
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/**
	 * 添加 shared fragment 需求（模板版，可读可写）。
	 * Shared fragment 在多个 entity 间共享一份可变数据。注意：
	 *   - 不支持 Presence::Any
	 *   - 若 AccessMode == ReadWrite 且 trait TMassSharedFragmentTraits<T>::GameThreadOnly == true，
	 *     则置 bRequiresGameThreadExecution = true（写共享数据要回到 GT，避免数据竞争）
	 */
	template<typename T>
	FMassFragmentRequirements& AddSharedRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(UE::Mass::CSharedFragment<T>, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(SharedFragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllSharedFragments.Add<T>();
			SharedFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= TMassSharedFragmentTraits<T>::GameThreadOnly;
			}
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalSharedFragments.Add<T>();
			SharedFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= TMassSharedFragmentTraits<T>::GameThreadOnly;
			}
			break;
		case EMassFragmentPresence::None:
			RequiredNoneSharedFragments.Add<T>();
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/**
	 * 添加 shared fragment 需求（运行时类型版本）。
	 * 与模板版差别：用 IsGameThreadOnlySharedFragment(SharedFragmentType) 通过 TypeManager
	 * 在运行时查 FSharedFragmentTypeTraits::bGameThreadOnly。
	 */
	FMassFragmentRequirements& AddSharedRequirement(TNotNull<const UScriptStruct*> SharedFragmentType, const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(UE::Mass::IsA<FMassSharedFragment>(SharedFragmentType), TEXT("Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.")); 
		checkf(bInitialized, TEXT("Modifying requirements before initialization is not supported."));
		checkf(SharedFragmentRequirements.FindByPredicate([&SharedFragmentType](const FMassFragmentRequirementDescription& Item) { return Item.StructType == SharedFragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *SharedFragmentType->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllSharedFragments.Add(*SharedFragmentType);
			SharedFragmentRequirements.Emplace(SharedFragmentType, AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= IsGameThreadOnlySharedFragment(SharedFragmentType);
			}
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalSharedFragments.Add(*SharedFragmentType);
			SharedFragmentRequirements.Emplace(SharedFragmentType, AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= IsGameThreadOnlySharedFragment(SharedFragmentType);
			}
			break;
		case EMassFragmentPresence::None:
			RequiredNoneSharedFragments.Add(*SharedFragmentType);
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/**
	 * 清空所有需求位集与 TArray，但**保留** bInitialized 与 CachedEntityManager。
	 * 即"恢复到刚 Initialize 完的空状态"，可继续 AddXxxRequirement。
	 */
	MASSENTITY_API void Reset();

	/** 
	 * The function validates requirements we make for queries. See the FMassFragmentRequirements struct description for details.
	 * Even though the code of the function is non-trivial the consecutive calls will be essentially free due to the result 
	 * being cached (note that the caching gets invalidated if the composition changes).
	 * @return whether this query's requirements follow the rules.
	 *
	 * 校验当前 query 是否合法。当前实现简单：只要 Positive / Negative / Optional 三组中
	 * 至少有一组非空就算合法（即不能是完全的"空 query"）。
	 * TODO 注释里也提到——更严格的校验（例如同时 All 和 None 同一 element 这种自相矛盾）
	 * 待后续实现。
	 *
	 * 调用 CacheProperties()：首次后 O(1)；位集变更后下次调用会重算。
	 */
	MASSENTITY_API bool CheckValidity() const;

	/** @return 4 类有数据 element 的需求三元组数组（用于 binding）。 */
	TConstArrayView<FMassFragmentRequirementDescription> GetFragmentRequirements() const;
	TConstArrayView<FMassFragmentRequirementDescription> GetChunkFragmentRequirements() const;
	TConstArrayView<FMassFragmentRequirementDescription> GetConstSharedFragmentRequirements() const;
	TConstArrayView<FMassFragmentRequirementDescription> GetSharedFragmentRequirements() const;
	/** @return 各类 element 各 4 套位集的访问器（共 ~20 个）。供 archetype 匹配 / 调试 / Solver 使用。 */
	const FMassFragmentBitSet& GetRequiredAllFragments() const;
	const FMassFragmentBitSet& GetRequiredAnyFragments() const;
	const FMassFragmentBitSet& GetRequiredOptionalFragments() const;
	const FMassFragmentBitSet& GetRequiredNoneFragments() const;
	const FMassTagBitSet& GetRequiredAllTags() const;
	const FMassTagBitSet& GetRequiredAnyTags() const;
	const FMassTagBitSet& GetRequiredNoneTags() const;
	const FMassTagBitSet& GetRequiredOptionalTags() const;
	const FMassChunkFragmentBitSet& GetRequiredAllChunkFragments() const;
	const FMassChunkFragmentBitSet& GetRequiredOptionalChunkFragments() const;
	const FMassChunkFragmentBitSet& GetRequiredNoneChunkFragments() const;
	const FMassSharedFragmentBitSet& GetRequiredAllSharedFragments() const;
	const FMassSharedFragmentBitSet& GetRequiredOptionalSharedFragments() const;
	const FMassSharedFragmentBitSet& GetRequiredNoneSharedFragments() const;
	const FMassConstSharedFragmentBitSet& GetRequiredAllConstSharedFragments() const;
	const FMassConstSharedFragmentBitSet& GetRequiredOptionalConstSharedFragments() const;
	const FMassConstSharedFragmentBitSet& GetRequiredNoneConstSharedFragments() const;

	/** @return 是否已绑定 EntityManager。Add* 之前必为 true。 */
	bool IsInitialized() const;
	/** @return 是否完全没声明任何需求（三类标志都为 false）。 */
	MASSENTITY_API bool IsEmpty() const;
	/** @return 是否存在 All/Any 类位集（"必须有什么"）。 */
	bool HasPositiveRequirements() const;
	/** @return 是否存在 None 类位集（"必须没有什么"）。 */
	bool HasNegativeRequirements() const;
	/** @return 是否存在 Optional 类位集。 */
	bool HasOptionalRequirements() const;

	/** 通过 archetype handle 调用——内部解出 composition 后转发。 */
	MASSENTITY_API bool DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle) const;
	/**
	 * 核心匹配函数。算法（详见 .cpp）：
	 *   step1 negative filter：archetype 不能命中任何 None* 中元素
	 *   step2 positive filter：archetype 必须含所有 All*；Any* 非空时至少一个命中
	 *   step3 若无 positive 但有 optional：DoesMatchAnyOptionals
	 *   step4 都没声明：return true
	 *
	 * 全程位集运算 (HasAll / HasAny / HasNone)，O(words) 复杂度。
	 */
	MASSENTITY_API bool DoesArchetypeMatchRequirements(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const;
	/**
	 * 当 query 全是 optional 需求时使用——只要 archetype 至少含一个 optional 元素即匹配。
	 * @return 命中至少一个 Optional* 中元素则 true。
	 */
	MASSENTITY_API bool DoesMatchAnyOptionals(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const;

	/** @return 是否要求 GameThread 执行（来源：写 shared fragment 时的 trait 累计）。 */
	bool DoesRequireGameThreadExecution() const;
	/**
	 * 把 fragment 需求合入 OutRequirements——供 FMassProcessorDependencySolver 进行依赖求解。
	 * 实现要点（见 .cpp）：
	 *   - fragment / chunk / shared 各自按 access mode 拆成 Read / Write 集
	 *   - constShared 强制 ReadOnly
	 *   - tag：仅导出 All / Any / None（**不导出 Optional tag**——design choice）
	 */
	MASSENTITY_API void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

	MASSENTITY_API friend uint32 GetTypeHash(const FMassFragmentRequirements& Instance);

protected:
	/**
	 * 排序 4 个 TArray<FMassFragmentRequirementDescription>，与 archetype 内部 FragmentConfigs
	 * 顺序保持一致，提升 BindRequirementsWithMapping 阶段的内存访问局部性（顺序访问 vs 随机）。
	 */
	MASSENTITY_API void SortRequirements();

	/** 自增 IncrementalChangesCount 并令 bPropertiesCached 失效。任何 Add*/Reset 都会调用。 */
	void IncrementChangeCounter();
	/** 上层（FMassEntityQuery::CacheArchetypes）确认已消费当前的变更后调用，归零计数。 */
	void ConsumeIncrementalChangesCount();
	/** @return 是否有未被上层消费的变更——若有，archetype 缓存需重建。 */
	bool HasIncrementalChanges() const;
	
	/**
	 * A helper function that passes the query over to CachedEntityManager.
	 * Main purpose is to have the implementation in cpp and not include the EntityManager header here
	 * @todo this function always returns True at the moment, proper implementation waiting for implementation of "type trait information" (WIP)
	 *
	 * 通过 CachedEntityManager.TypeManager 查 FSharedFragmentTypeTraits::bGameThreadOnly。
	 * 缺信息时 ensure 失败并默认返回 true（保守 GT-only）。
	 */
	MASSENTITY_API bool IsGameThreadOnlySharedFragment(TNotNull<const UScriptStruct*> SharedFragmentType) const;

	friend FMassRequirementAccessDetector;

	/** 4 个"有数据"类别的需求三元组数组（tag 不需要，因为没有数据 binding）。 */
	TArray<FMassFragmentRequirementDescription> FragmentRequirements;
	TArray<FMassFragmentRequirementDescription> ChunkFragmentRequirements;
	TArray<FMassFragmentRequirementDescription> ConstSharedFragmentRequirements;
	TArray<FMassFragmentRequirementDescription> SharedFragmentRequirements;

	// ---------------- Tag 位集 (4 套) ----------------
	/** 必须全部存在的 tag 集合。 */
	FMassTagBitSet RequiredAllTags;
	/** 至少存在一个的 tag 集合。 */
	FMassTagBitSet RequiredAnyTags;
	/** 一个都不能存在的 tag 集合。 */
	FMassTagBitSet RequiredNoneTags;
	/**
	 * note that optional tags have meaning only if there are no other strict requirements, i.e. everything is optional,
	 * so we're looking for anything matching any of the optionals (both tags as well as fragments).
	 *
	 * 可选 tag 集合。注意：当 query 全部需求都是 optional 时，optional tag 才参与匹配
	 * （DoesMatchAnyOptionals 路径）。同时 ExportRequirements **不**导出 optional tag
	 * 给 Solver——design choice。
	 */
	FMassTagBitSet RequiredOptionalTags;

	// ---------------- Fragment 位集 (4 套) ----------------
	FMassFragmentBitSet RequiredAllFragments;
	FMassFragmentBitSet RequiredAnyFragments;
	FMassFragmentBitSet RequiredOptionalFragments;
	FMassFragmentBitSet RequiredNoneFragments;

	// ---------------- ChunkFragment 位集 (3 套：无 Any) ----------------
	FMassChunkFragmentBitSet RequiredAllChunkFragments;
	FMassChunkFragmentBitSet RequiredOptionalChunkFragments;
	FMassChunkFragmentBitSet RequiredNoneChunkFragments;

	// ---------------- SharedFragment 位集 (3 套：无 Any) ----------------
	FMassSharedFragmentBitSet RequiredAllSharedFragments;
	FMassSharedFragmentBitSet RequiredOptionalSharedFragments;
	FMassSharedFragmentBitSet RequiredNoneSharedFragments;

	// ---------------- ConstSharedFragment 位集 (3 套：无 Any) ----------------
	FMassConstSharedFragmentBitSet RequiredAllConstSharedFragments;
	FMassConstSharedFragmentBitSet RequiredOptionalConstSharedFragments;
	FMassConstSharedFragmentBitSet RequiredNoneConstSharedFragments;

	/** 关联的 EntityManager，Initialize 时设置。用于查 TypeManager 等。 */
	TSharedPtr<FMassEntityManager> CachedEntityManager;

private:
	/**
	 * 一次性计算并缓存 bHasPositive/Negative/OptionalRequirements 三个标志。
	 * 由 IncrementChangeCounter() 令 bPropertiesCached 失效，下次自动重算。
	 */
	MASSENTITY_API void CacheProperties() const;

	/** 三标志的缓存有效位。 */
	mutable uint16 bPropertiesCached : 1 = false;
	/** 是否声明了任何 All/Any 类位集。CacheProperties 计算。 */
	mutable uint16 bHasPositiveRequirements : 1 = false;
	/** 是否声明了任何 None 类位集。 */
	mutable uint16 bHasNegativeRequirements : 1 = false;
	/** 
	 * Indicates that the requirements specify only optional elements, which means any composition having any one of 
	 * the optional elements will be accepted. Note that RequiredNone* requirements are handled separately and if specified 
	 * still need to be satisfied.
	 *
	 * 是否声明了任何 Optional 类位集。当 Positive 为空但 Optional 非空时走 DoesMatchAnyOptionals 路径。
	 * 注意 None 类需求始终独立生效，与 optional 状态无关。
	 */
	mutable uint16 bHasOptionalRequirements : 1 = false;

	/** Initialize 后置 true，必须 true 才能调用 Add*。 */
	uint16 bInitialized : 1 = false;
	/** 自上次 Consume 以来累计的变更次数，>0 表示 archetype 缓存需要刷新。 */
	uint16 IncrementalChangesCount = 0;

	/** 是否需要 GameThread 执行（写 shared 时累积；只能由内部 Add* 设 true，Reset 不清此处—— note：Reset 实际并未清此位）。 */
	bool bRequiresGameThreadExecution = false;

public:
	UE_DEPRECATED(5.6, "This type of FMassFragmentRequirements is no longer supported. Use one of the other constructors instead.")
	MASSENTITY_API FMassFragmentRequirements(std::initializer_list<UScriptStruct*> InitList);

	UE_DEPRECATED(5.6, "This type of FMassFragmentRequirements is no longer supported. Use one of the other constructors instead.")
	MASSENTITY_API FMassFragmentRequirements(TConstArrayView<const UScriptStruct*> InitList);
};

//-----------------------------------------------------------------------------
// INLINE
// 内联实现：上面声明的访问器/小工具函数。
// 大部分是单行 getter；少数在改 IncrementalChangesCount 时令缓存失效。
//-----------------------------------------------------------------------------
inline FMassFragmentRequirementDescription::FMassFragmentRequirementDescription(const UScriptStruct* InStruct, const EMassFragmentAccess InAccessMode, const EMassFragmentPresence InPresence)
	: StructType(InStruct)
	, AccessMode(InAccessMode)
	, Presence(InPresence)
{
	check(InStruct);
}

/** 是否需要数据 binding（Tag-like 需求 AccessMode==None 时不需要）。 */
inline bool FMassFragmentRequirementDescription::RequiresBinding() const
{
	return (AccessMode != EMassFragmentAccess::None);
}

/** Optional 与 Any 都视为非强制（chunk 在 archetype 上"可有可无"或"几选一"）。 */
inline bool FMassFragmentRequirementDescription::IsOptional() const
{
	return (Presence == EMassFragmentPresence::Optional || Presence == EMassFragmentPresence::Any);
}

inline int32 FMassFragmentRequirementDescription::GetStructureSize() const
{
	return StructType->GetStructureSize();
}

inline FName FMassFragmentRequirementDescription::GetFName() const
{
	return StructType->GetFName();
}

inline const FMassExternalSubsystemBitSet& FMassSubsystemRequirements::GetRequiredConstSubsystems() const
{
	return RequiredConstSubsystems;
}

inline const FMassExternalSubsystemBitSet& FMassSubsystemRequirements::GetRequiredMutableSubsystems() const
{
	return RequiredMutableSubsystems;
}

inline bool FMassSubsystemRequirements::IsEmpty() const
{
	return RequiredConstSubsystems.IsEmpty() && RequiredMutableSubsystems.IsEmpty();
}

inline bool FMassSubsystemRequirements::DoesRequireGameThreadExecution() const
{
	return bRequiresGameThreadExecution;
}

/** Hash 由两个位集组合而来——足以识别"读集 + 写集"等价的 subsystem 需求。 */
inline uint32 GetTypeHash(const FMassSubsystemRequirements& Instance)
{
	return HashCombine(GetTypeHash(Instance.RequiredConstSubsystems), GetTypeHash(Instance.RequiredMutableSubsystems));
}

// AddTagRequirements<Presence> 的 4 个特化：把整个 TagBitSet OR 进对应位集。
// 注意 += 是位集"并集"操作（详见 BitSet 注释）。
template<>
inline FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::All>(const FMassTagBitSet& TagBitSet)
{
	RequiredAllTags += TagBitSet;
	// force re-caching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

template<>
inline FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::Any>(const FMassTagBitSet& TagBitSet)
{
	RequiredAnyTags += TagBitSet;
	// force re-caching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

template<>
inline FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::None>(const FMassTagBitSet& TagBitSet)
{
	RequiredNoneTags += TagBitSet;
	// force re-caching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

template<>
inline FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::Optional>(const FMassTagBitSet& TagBitSet)
{
	RequiredOptionalTags += TagBitSet;
	// force re-caching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

// ===== 各类 element 的 TArray / BitSet 访问器 =====
// 这些 getter 主要被 FMassDebugger / FMassProcessorDependencySolver / FMassEntityQuery::CacheArchetypes 使用。
inline TConstArrayView<FMassFragmentRequirementDescription> FMassFragmentRequirements::GetFragmentRequirements() const
{ 
	return FragmentRequirements; 
}

inline TConstArrayView<FMassFragmentRequirementDescription> FMassFragmentRequirements::GetChunkFragmentRequirements() const
{ 
	return ChunkFragmentRequirements; 
}

inline TConstArrayView<FMassFragmentRequirementDescription> FMassFragmentRequirements::GetConstSharedFragmentRequirements() const
{ 
	return ConstSharedFragmentRequirements; 
}

inline TConstArrayView<FMassFragmentRequirementDescription> FMassFragmentRequirements::GetSharedFragmentRequirements() const
{ 
	return SharedFragmentRequirements; 
}

inline const FMassFragmentBitSet& FMassFragmentRequirements::GetRequiredAllFragments() const
{ 
	return RequiredAllFragments; 
}

inline const FMassFragmentBitSet& FMassFragmentRequirements::GetRequiredAnyFragments() const
{ 
	return RequiredAnyFragments; 
}

inline const FMassFragmentBitSet& FMassFragmentRequirements::GetRequiredOptionalFragments() const
{ 
	return RequiredOptionalFragments; 
}

inline const FMassFragmentBitSet& FMassFragmentRequirements::GetRequiredNoneFragments() const
{ 
	return RequiredNoneFragments; 
}

inline const FMassTagBitSet& FMassFragmentRequirements::GetRequiredAllTags() const
{ 
	return RequiredAllTags; 
}

inline const FMassTagBitSet& FMassFragmentRequirements::GetRequiredAnyTags() const
{ 
	return RequiredAnyTags; 
}

inline const FMassTagBitSet& FMassFragmentRequirements::GetRequiredNoneTags() const
{ 
	return RequiredNoneTags; 
}

inline const FMassTagBitSet& FMassFragmentRequirements::GetRequiredOptionalTags() const
{ 
	return RequiredOptionalTags; 
}

inline const FMassChunkFragmentBitSet& FMassFragmentRequirements::GetRequiredAllChunkFragments() const
{ 
	return RequiredAllChunkFragments; 
}

inline const FMassChunkFragmentBitSet& FMassFragmentRequirements::GetRequiredOptionalChunkFragments() const
{ 
	return RequiredOptionalChunkFragments; 
}

inline const FMassChunkFragmentBitSet& FMassFragmentRequirements::GetRequiredNoneChunkFragments() const
{ 
	return RequiredNoneChunkFragments; 
}

inline const FMassSharedFragmentBitSet& FMassFragmentRequirements::GetRequiredAllSharedFragments() const
{ 
	return RequiredAllSharedFragments; 
}

inline const FMassSharedFragmentBitSet& FMassFragmentRequirements::GetRequiredOptionalSharedFragments() const
{ 
	return RequiredOptionalSharedFragments; 
}

inline const FMassSharedFragmentBitSet& FMassFragmentRequirements::GetRequiredNoneSharedFragments() const
{ 
	return RequiredNoneSharedFragments; 
}

inline const FMassConstSharedFragmentBitSet& FMassFragmentRequirements::GetRequiredAllConstSharedFragments() const
{ 
	return RequiredAllConstSharedFragments; 
}

inline const FMassConstSharedFragmentBitSet& FMassFragmentRequirements::GetRequiredOptionalConstSharedFragments() const
{ 
	return RequiredOptionalConstSharedFragments; 
}

inline const FMassConstSharedFragmentBitSet& FMassFragmentRequirements::GetRequiredNoneConstSharedFragments() const
{ 
	return RequiredNoneConstSharedFragments; 
}

inline bool FMassFragmentRequirements::IsInitialized() const 
{ 
	return bInitialized; 
}

// 三个 Has* 都依赖 CacheProperties() 已计算过；首次调用前应至少触发一次（CheckValidity/IsEmpty/DoesArchetypeMatchRequirements 都会触发）。
inline bool FMassFragmentRequirements::HasPositiveRequirements() const 
{ 
	return bHasPositiveRequirements; 
}

inline bool FMassFragmentRequirements::HasNegativeRequirements() const 
{ 
	return bHasNegativeRequirements; 
}

inline bool FMassFragmentRequirements::HasOptionalRequirements() const 
{ 
	return bHasOptionalRequirements; 
}

inline bool FMassFragmentRequirements::DoesRequireGameThreadExecution() const
{
	return bRequiresGameThreadExecution;
}

/** 任何 Add*/Reset 都调用：自增计数 + 让缓存失效。 */
inline void FMassFragmentRequirements::IncrementChangeCounter()
{ 
	++IncrementalChangesCount; 
	bPropertiesCached = false;
}

/** 由上层（FMassEntityQuery）"消费"完变更后归零。 */
inline void FMassFragmentRequirements::ConsumeIncrementalChangesCount()
{
	IncrementalChangesCount = 0;
}

/** 是否有未消费的变更——上层据此决定是否重新缓存匹配的 archetype 列表。 */
inline bool FMassFragmentRequirements::HasIncrementalChanges() const
{
	return IncrementalChangesCount > 0;
}
