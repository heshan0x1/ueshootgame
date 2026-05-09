// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRequirements.h"
#include "MassArchetypeData.h"
#include "MassProcessorDependencySolver.h"
#include "MassTypeManager.h"
#include "MassEntityManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassRequirements)

#if WITH_MASSENTITY_DEBUG
#include "MassRequirementAccessDetector.h"
#endif // WITH_MASSENTITY_DEBUG


namespace UE::Mass::Private
{
	/**
	 * 通用模板：把一组 FMassFragmentRequirementDescription 按 access mode 分流写入
	 * TMassExecutionAccess<TContainer>{ Read, Write } 两个位集。
	 *
	 * 用于 FMassFragmentRequirements::ExportRequirements——把声明转为 Solver 看得懂的 R/W 集合。
	 *
	 * 注意：Presence == None 的需求会被跳过——因为 None 只是"必须不存在"，不需要绑定数据，
	 * 也就不参与依赖求解的 R/W 冲突分析。
	 */
	template<typename TContainer>
	void ExportRequirements(TConstArrayView<FMassFragmentRequirementDescription> Requirements, TMassExecutionAccess<TContainer>& Out)
	{
		for (const FMassFragmentRequirementDescription& Requirement : Requirements)
		{
			if (Requirement.Presence != EMassFragmentPresence::None)
			{
				check(Requirement.StructType);
				if (Requirement.AccessMode == EMassFragmentAccess::ReadOnly)
				{
					Out.Read.Add(*Requirement.StructType);
				}
				else if (Requirement.AccessMode == EMassFragmentAccess::ReadWrite)
				{
					Out.Write.Add(*Requirement.StructType);
				}
			}
		}
	}

	/**
	 * 特化：FMassConstSharedFragmentBitSet 强制 ReadOnly。
	 * 即使调用方误传了 ReadWrite，这里 ensure 报警并丢弃——保护 const 语义不被打破。
	 */
	template<>
	void ExportRequirements<FMassConstSharedFragmentBitSet>(TConstArrayView<FMassFragmentRequirementDescription> Requirements
		, TMassExecutionAccess<FMassConstSharedFragmentBitSet>& Out)
	{
		for (const FMassFragmentRequirementDescription& Requirement : Requirements)
		{
			if (Requirement.Presence != EMassFragmentPresence::None)
			{
				check(Requirement.StructType);
				if (ensureMsgf(Requirement.AccessMode == EMassFragmentAccess::ReadOnly, TEXT("ReadOnly is the only supported AccessMode for ConstSharedFragments")))
				{
					Out.Read.Add(*Requirement.StructType);
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// FMassSubsystemRequirements
//-----------------------------------------------------------------------------

/**
 * 把声明的 R/W subsystem 集合并入执行需求——供 FMassProcessorDependencySolver 进行
 * 跨 processor 的读写冲突分析（subsystem 级别的并行调度）。
 */
void FMassSubsystemRequirements::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	OutRequirements.RequiredSubsystems.Read += RequiredConstSubsystems;
	OutRequirements.RequiredSubsystems.Write += RequiredMutableSubsystems;
}

/** 全部清空。可重复使用本实例。 */
void FMassSubsystemRequirements::Reset()
{
	RequiredConstSubsystems.Reset();
	RequiredMutableSubsystems.Reset();
	bRequiresGameThreadExecution = false;
}

/**
 * 通过 EntityManager 的 TypeManager 查找 SubsystemClass 对应的 FSubsystemTypeTraits，
 * 读取 bGameThreadOnly 字段。
 *
 * 失败兜底：若 type info 缺失或不是 system traits，默认返回 true——保守起见，
 * "宁可全跑 GT 也别在错误线程上跑导致难以排查的崩溃/数据竞争"。
 */
bool FMassSubsystemRequirements::IsGameThreadOnlySubsystem(const TSubclassOf<USubsystem> SubsystemClass, const TSharedRef<FMassEntityManager>& EntityManager)
{
	const UE::Mass::FTypeInfo* TypeInfo = EntityManager->GetTypeManager().GetTypeInfo(SubsystemClass);
	if (ensureMsgf(TypeInfo, TEXT("Failed to find type information for %s"), *GetNameSafe(SubsystemClass)))
	{
		const UE::Mass::FSubsystemTypeTraits* SystemTraits = TypeInfo->GetAsSystemTraits();
		if (ensureMsgf(SystemTraits, TEXT("Type information for %s doesn't represent subsystem traits"), *GetNameSafe(SubsystemClass)))
		{
			return SystemTraits->bGameThreadOnly;
		}
	}
	// using `true` as default as the safer one of the options
	// since it's safer to run everything on GT rather than on an arbitrary thread
	return true;
}

//-----------------------------------------------------------------------------
// FMassFragmentRequirements
//-----------------------------------------------------------------------------

/** 从 TSharedPtr 构造——非空时自动 Initialize。 */
FMassFragmentRequirements::FMassFragmentRequirements(const TSharedPtr<FMassEntityManager>& EntityManager)
{
	if (ensure(EntityManager))
	{
		Initialize(EntityManager.ToSharedRef());
	}
}

/** 从 TSharedRef 构造——总是 Initialize。推荐路径。 */
FMassFragmentRequirements::FMassFragmentRequirements(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Initialize(EntityManager);
}

/**
 * 绑定 EntityManager。
 * - 已 Initialized 直接 return（幂等）
 * - 重复初始化但绑定到不同的 manager 会发警告（潜在的逻辑错误）
 */
void FMassFragmentRequirements::Initialize(const TSharedRef<FMassEntityManager>& EntityManager)
{
	UE_CLOG(CachedEntityManager && (CachedEntityManager != EntityManager), LogMass, Warning
		, TEXT("Trying to initialize FMassFragmentRequirements with another entity manager"));
	if (bInitialized)
	{
		return;
	}

	CachedEntityManager = EntityManager;
	bInitialized = true;
}

/**
 * 从所有 4 套 tag 位集（All / Any / None / Optional）中减去给定 tag 集合。
 * 用于父-子 query 的需求"放宽"场景，或动态修改 query 时的清理。
 */
FMassFragmentRequirements& FMassFragmentRequirements::ClearTagRequirements(const FMassTagBitSet& TagsToRemoveBitSet)
{
	RequiredAllTags.Remove(TagsToRemoveBitSet);
	RequiredAnyTags.Remove(TagsToRemoveBitSet);
	RequiredNoneTags.Remove(TagsToRemoveBitSet);
	RequiredOptionalTags.Remove(TagsToRemoveBitSet);

	return *this;
}

/**
 * 计算 query 整体的类型 hash——用于 query 去重 / dependency cache key 等。
 *
 * 实现：把 4 个 TArray 的原始内存做 CRC32（FMassFragmentRequirementDescription 是 POD），
 * 再把所有 5×4 ≈ 20 个位集的 hash 组合在一起，最后混入 EntityManager 指针以区分不同 world。
 *
 * 注：内联的 TODO 提示——可考虑跳过空位集以省时间。
 */
uint32 GetTypeHash(const FMassFragmentRequirements& Instance)
{
	// @todo consider calculating hash only for non-empty elements, or any other optimization
	uint32 Hash = FCrc::MemCrc32(Instance.FragmentRequirements.GetData(), Instance.FragmentRequirements.Num() * sizeof(FMassFragmentRequirementDescription));
	Hash = HashCombine(Hash, FCrc::MemCrc32(Instance.ChunkFragmentRequirements.GetData(), Instance.ChunkFragmentRequirements.Num() * sizeof(FMassFragmentRequirementDescription)));
	Hash = HashCombine(Hash, FCrc::MemCrc32(Instance.ConstSharedFragmentRequirements.GetData(), Instance.ConstSharedFragmentRequirements.Num() * sizeof(FMassFragmentRequirementDescription)));
	Hash = HashCombine(Hash, FCrc::MemCrc32(Instance.SharedFragmentRequirements.GetData(), Instance.SharedFragmentRequirements.Num() * sizeof(FMassFragmentRequirementDescription)));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAllTags));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAnyTags));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredNoneTags));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredOptionalTags));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAllFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAnyFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredOptionalFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredNoneFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAllChunkFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredOptionalChunkFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredNoneChunkFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAllSharedFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredOptionalSharedFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredNoneSharedFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredAllConstSharedFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredOptionalConstSharedFragments));
	Hash = HashCombine(Hash, GetTypeHash(Instance.RequiredNoneConstSharedFragments));
	return HashCombine(Hash, PointerHash(Instance.CachedEntityManager.Get()));
}

/**
 * 对 4 个需求三元组数组按 FScriptStructSortOperator 排序——使其与 archetype 内
 * FragmentConfigs 的顺序一致。这样在 BindRequirementsWithMapping 阶段对
 * FragmentConfigs 的访问是顺序而非随机，提高 CPU 缓存命中。
 */
void FMassFragmentRequirements::SortRequirements()
{
	// we're sorting the Requirements the same way ArchetypeData's FragmentConfig is sorted (see FMassArchetypeData::Initialize)
	// so that when we access ArchetypeData.FragmentConfigs in FMassArchetypeData::BindRequirementsWithMapping
	// (via GetFragmentData call) the access is sequential (i.e. not random) and there's a higher chance the memory
	// FragmentConfigs we want to access have already been fetched and are available in processor cache.
	FragmentRequirements.Sort(FScriptStructSortOperator());
	ChunkFragmentRequirements.Sort(FScriptStructSortOperator());
	ConstSharedFragmentRequirements.Sort(FScriptStructSortOperator());
	SharedFragmentRequirements.Sort(FScriptStructSortOperator());
}

/**
 * 通过 TypeManager 查 FSharedFragmentTypeTraits::bGameThreadOnly。
 * 与 IsGameThreadOnlySubsystem 同样的兜底逻辑——缺信息时安全地默认 true。
 *
 * @note 仅 AddSharedRequirement 的"运行时类型"重载使用本函数；模板版直接读
 *       TMassSharedFragmentTraits<T>::GameThreadOnly（编译期 trait）。
 */
bool FMassFragmentRequirements::IsGameThreadOnlySharedFragment(TNotNull<const UScriptStruct*> SharedFragmentType) const
{
	checkf(CachedEntityManager, TEXT("Not having a cached EntityManager at this point is not expected."));

	const UE::Mass::FTypeInfo* TypeInfo = CachedEntityManager->GetTypeManager().GetTypeInfo(SharedFragmentType);
	if (ensureMsgf(TypeInfo, TEXT("Failed to find type information for %s"), *SharedFragmentType->GetName()))
	{
		const UE::Mass::FSharedFragmentTypeTraits* SharedFragmentTraits = TypeInfo->GetAsSharedFragmentTraits();
		if (ensureMsgf(SharedFragmentTraits, TEXT("Type information for %s doesn't represent shared fragment traits"), *SharedFragmentType->GetName()))
		{
			return SharedFragmentTraits->bGameThreadOnly;
		}
	}
	// using `true` as default as the safer one of the options
	// since it's safer to run everything on GT rather than on an arbitrary thread
	return true;
}

/**
 * 计算并缓存 bHasPositive/Negative/OptionalRequirements 三个标志。
 *
 * - bHasPositive：任一 All/Any 类位集非空
 *   注意：tag 的 Any 也算 positive（因为它代表"必须有"语义，只是相对宽松）；
 *   chunk/shared/constShared 没有 Any，故只看 All*。
 * - bHasNegative：任一 None 类位集非空
 * - bHasOptional：任一 Optional 类位集非空
 *
 * 由 IncrementChangeCounter() 让 bPropertiesCached 失效，下次自动重算。
 */
FORCEINLINE void FMassFragmentRequirements::CacheProperties() const
{
	if (bPropertiesCached == false)
	{
		bHasPositiveRequirements = !(RequiredAllTags.IsEmpty()
			&& RequiredAnyTags.IsEmpty()
			&& RequiredAllFragments.IsEmpty()
			&& RequiredAnyFragments.IsEmpty()
			&& RequiredAllChunkFragments.IsEmpty()
			&& RequiredAllSharedFragments.IsEmpty()
			&& RequiredAllConstSharedFragments.IsEmpty());

		bHasNegativeRequirements = !(RequiredNoneTags.IsEmpty()
			&& RequiredNoneFragments.IsEmpty()
			&& RequiredNoneChunkFragments.IsEmpty()
			&& RequiredNoneSharedFragments.IsEmpty()
			&& RequiredNoneConstSharedFragments.IsEmpty());

		bHasOptionalRequirements = !(RequiredOptionalFragments.IsEmpty()
				&& RequiredOptionalTags.IsEmpty()
				&& RequiredOptionalChunkFragments.IsEmpty()
				&& RequiredOptionalSharedFragments.IsEmpty()
				&& RequiredOptionalConstSharedFragments.IsEmpty());

		bPropertiesCached = true;
	}
}

/**
 * 简单合法性检查：positive/negative/optional 三组中至少一组非空（不允许彻底空 query）。
 * 注意 TODO：当前未检测自相矛盾（如同时声明 RequiredAll 与 RequiredNone 同一 tag）。
 */
bool FMassFragmentRequirements::CheckValidity() const
{
	CacheProperties();
	// @todo we need to add more sophisticated testing somewhere to detect contradicting requirements - like having and not having a given tag.
	return bHasPositiveRequirements || bHasNegativeRequirements || bHasOptionalRequirements;
}

/** "空 query"——即三标志都为 false。当前等同于 !CheckValidity()，未来加更多校验后会分化。 */
bool FMassFragmentRequirements::IsEmpty() const
{
	CacheProperties();
	// note that even though at the moment the following condition is the same as negation of current CheckValidity value
	// that will change in the future (with additional validity checks).
	return !bHasPositiveRequirements && !bHasNegativeRequirements && !bHasOptionalRequirements;
}

/**
 * "全 optional"路径下的匹配。
 *
 * 逻辑：仅当 query 声明了 optional 类需求，且 archetype composition 命中
 * 任一 Optional* 中元素时返回 true。即"我说了几个 optional element，archetype 至少能提供其中一个"。
 *
 * 在 DoesArchetypeMatchRequirements 中，当没有任何 positive 但有 optional 时使用。
 * 如果还有 positive 需求，optional 不参与匹配判定（只会决定 binding 时是否 nullable）。
 */
bool FMassFragmentRequirements::DoesMatchAnyOptionals(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const
{
	return bHasOptionalRequirements
		&& (ArchetypeComposition.GetFragments().HasAny(RequiredOptionalFragments)
			|| ArchetypeComposition.GetTags().HasAny(RequiredOptionalTags)
			|| ArchetypeComposition.GetChunkFragments().HasAny(RequiredOptionalChunkFragments)
			|| ArchetypeComposition.GetSharedFragments().HasAny(RequiredOptionalSharedFragments)
			|| ArchetypeComposition.GetConstSharedFragments().HasAny(RequiredOptionalConstSharedFragments));
}

/** Handle 版本——解出 ArchetypeData 后转发到 composition 版本。 */
bool FMassFragmentRequirements::DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle) const
{
	check(ArchetypeHandle.IsValid());
	const FMassArchetypeData* Archetype = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
	CA_ASSUME(Archetype);

	return DoesArchetypeMatchRequirements(Archetype->GetCompositionDescriptor());
}
	
/**
 * 【匹配核心算法】给定 archetype composition，判定是否满足本 query。
 *
 *   step1 negative filter：archetype 不能命中任何 None* 中元素
 *           - 5 类 element 全检：fragment / tag / chunk / shared / constShared
 *           - bHasNegative == false 时跳过此步（直接 pass）
 *
 *   step2 若通过 negative：
 *      step2a) 有 positive：
 *              - HasAll(All*) 全部命中
 *              - 若 Any* 非空，HasAny(Any*) 命中至少一个；为空则跳过 Any 校验
 *              - 5 类元素并联（fragment AND tag AND chunk AND shared AND constShared）
 *              - 注意 chunk/shared/constShared 没有 Any 概念，只校验 All
 *      step2b) 无 positive 但有 optional：DoesMatchAnyOptionals
 *      step2c) 既无 positive 也无 optional（仅 negative）：return true（已经通过 negative）
 *
 * 全部位集运算 O(BitSetWords)，没有任何动态分配。
 *
 * 性能要点：缓存的三个 bool 标志让常见情况（无 negative / 全 positive）能跳过整段判断。
 */
bool FMassFragmentRequirements::DoesArchetypeMatchRequirements(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const
{
	CacheProperties();

	const bool bPassNegativeFilter = bHasNegativeRequirements == false
		|| (ArchetypeComposition.GetFragments().HasNone(RequiredNoneFragments)
			&& ArchetypeComposition.GetTags().HasNone(RequiredNoneTags)
			&& ArchetypeComposition.GetChunkFragments().HasNone(RequiredNoneChunkFragments)
			&& ArchetypeComposition.GetSharedFragments().HasNone(RequiredNoneSharedFragments)
			&& ArchetypeComposition.GetConstSharedFragments().HasNone(RequiredNoneConstSharedFragments));
	
	if (bPassNegativeFilter)
	{
		if (bHasPositiveRequirements)
		{
			return ArchetypeComposition.GetFragments().HasAll(RequiredAllFragments)
				&& (RequiredAnyFragments.IsEmpty() || ArchetypeComposition.GetFragments().HasAny(RequiredAnyFragments))
				&& ArchetypeComposition.GetTags().HasAll(RequiredAllTags)
				&& (RequiredAnyTags.IsEmpty() || ArchetypeComposition.GetTags().HasAny(RequiredAnyTags))
				&& ArchetypeComposition.GetChunkFragments().HasAll(RequiredAllChunkFragments)
				&& ArchetypeComposition.GetSharedFragments().HasAll(RequiredAllSharedFragments)
				&& ArchetypeComposition.GetConstSharedFragments().HasAll(RequiredAllConstSharedFragments);
		}
		else if (bHasOptionalRequirements)
		{
			return DoesMatchAnyOptionals(ArchetypeComposition);
		}
		// else - it's fine, we passed all the filters that have been set up
		return true;
	}
	return false;
}

/**
 * 把 fragment / chunk / shared / constShared 需求按 R/W 拆分写入 OutRequirements。
 * 同时直接复制 RequiredAll/Any/None Tags 三个位集。
 *
 * 用途：FMassProcessorDependencySolver 据此构建跨 processor 的依赖图。
 *
 * 注意 design choice："不导出 Optional tags" —— 因为 optional 不影响硬筛选与 R/W 冲突，
 * Solver 拿不到也不影响调度判断；同时减小 hash key 的搅动。
 */
void FMassFragmentRequirements::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	using UE::Mass::Private::ExportRequirements;
	ExportRequirements<FMassFragmentBitSet>(FragmentRequirements, OutRequirements.Fragments);
	ExportRequirements<FMassChunkFragmentBitSet>(ChunkFragmentRequirements, OutRequirements.ChunkFragments);
	ExportRequirements<FMassSharedFragmentBitSet>(SharedFragmentRequirements, OutRequirements.SharedFragments);
	ExportRequirements<FMassConstSharedFragmentBitSet>(ConstSharedFragmentRequirements, OutRequirements.ConstSharedFragments);

	OutRequirements.RequiredAllTags = RequiredAllTags;
	OutRequirements.RequiredAnyTags = RequiredAnyTags;
	OutRequirements.RequiredNoneTags = RequiredNoneTags;
	// not exporting optional tags by design
	// 不导出 optional tags（design choice）：optional 不影响 R/W 调度
}

/**
 * 清空所有需求（4 个三元组数组 + 全部位集 + 变更计数）。
 *
 * 注意保留：
 *   - bInitialized = true
 *   - CachedEntityManager
 *   - bRequiresGameThreadExecution（**未清零**——见下文 bug 提示）
 *
 * 设计意图：把 query 容器归"空"但仍可继续添加。
 *
 * @note 疑似 bug：bRequiresGameThreadExecution 在 Reset() 中没有被清零。
 *       一旦曾经声明过 GT-only 的 shared fragment，Reset 后再添加非 GT-only 的需求，
 *       该实例仍会汇报 GT-only。建议复查（见交付摘要）。
 */
void FMassFragmentRequirements::Reset()
{
	FragmentRequirements.Reset();
	ChunkFragmentRequirements.Reset();
	ConstSharedFragmentRequirements.Reset();
	SharedFragmentRequirements.Reset();
	RequiredAllTags.Reset();
	RequiredAnyTags.Reset();
	RequiredNoneTags.Reset();
	RequiredAllFragments.Reset();
	RequiredAnyFragments.Reset();
	RequiredOptionalFragments.Reset();
	RequiredNoneFragments.Reset();
	RequiredAllChunkFragments.Reset();
	RequiredOptionalChunkFragments.Reset();
	RequiredNoneChunkFragments.Reset();
	RequiredAllSharedFragments.Reset();
	RequiredOptionalSharedFragments.Reset();
	RequiredNoneSharedFragments.Reset();
	RequiredAllConstSharedFragments.Reset();
	RequiredOptionalConstSharedFragments.Reset();
	RequiredNoneConstSharedFragments.Reset();

	IncrementalChangesCount = 0;

	// note that we're not resetting bInitialized nor CachedEntityManager, on purpose
	// the point of this function is to just reset the contents while still being able
	// to add elements to it. This "requirements" instance is now "empty" but still valid
	// 故意不重置 bInitialized / CachedEntityManager：仅清空内容、保留可继续 Add 的状态。
}

//-----------------------------------------------------------------------------
// DEPRECATED
// 已废弃 (5.6) 的初始化构造方式。
// 这两个构造函数不会调用 Initialize 来设置 bInitialized——所以它们的 AddRequirement
// 会触发"必须先 Initialize"的 check 失败。这正是 5.6 把它们标 deprecated 的主要原因。
// 保留只是为了向下兼容编译，不应在新代码中使用。
//-----------------------------------------------------------------------------
FMassFragmentRequirements::FMassFragmentRequirements(std::initializer_list<UScriptStruct*> InitList)
{
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassFragmentRequirements::FMassFragmentRequirements(TConstArrayView<const UScriptStruct*> InitList)
{
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}
