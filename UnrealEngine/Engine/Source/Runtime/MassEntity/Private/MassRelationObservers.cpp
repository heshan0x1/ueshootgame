// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a，Mass 关系系统 4 类 Observer 的实现）
// -----------------------------------------------------------------------------
// 见同名 .h 的整体说明。本文件按 .h 中的声明顺序实现：
//   1. UMassRelationObserver           ── 共同基类构造 + ConfigureQueries 实现
//   2. UMassRelationEntityCreation     ── 关系实体创建 observer（默认空 Execute）
//   3. UMassRelationEntityGuardDog     ── 调试守门狗
//   4. UMassRelationEntityDestruction  ── 关系实体销毁 → 反向更新 RoleMap
//   5. UMassRelationRoleDestruction    ── 角色实体销毁 → 按 policy 走 lambda
// =============================================================================

#include "MassRelationObservers.h"
#include "MassTypeManager.h"
#include "MassExecutionContext.h"
#include "MassObserverManager.h"

//-----------------------------------------------------------------------------
// UMassRelationObserver
//-----------------------------------------------------------------------------

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassRelationObservers)

/**
 * 中文：构造函数。
 *   * 初始化 EntityQuery（与 *this 关联，便于 query profile 标注 owner）。
 *   * bAutoRegisterWithObserverRegistry = false：不走 Mass 的"模块启动时自动注册"流水线，
 *     而由 EntityManager 在关系类型注册流程中显式 Instance + AddObserverInstance。
 *   * ObservedOperations = None：基类不指定具体观察的 op；具体 op（CreateEntity / DestroyEntity /
 *     RemoveElement）由各派生类在自己的构造里设置。
 */
UMassRelationObserver::UMassRelationObserver()
	: EntityQuery(*this)
{
	bAutoRegisterWithObserverRegistry = false;
	ObservedOperations = EMassObservedOperationFlags::None;
}

/** 中文：仅做 Super 转发，预留扩展点。 */
void UMassRelationObserver::InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::InitializeInternal(Owner, EntityManager);
}

/**
 * 中文：构建内部 EntityQuery 的过滤要求。
 *
 * 算法步骤（详见 .h 中说明）：
 *   1) ObservedType 必须非空，且必须是 Tag 或 Fragment 之一（其他类型会被 checkf 阻止）。
 *   2) 根据类型加 Requirement：
 *       - Tag：AddTagRequirement(presence=All)
 *       - Fragment：AddRequirement(access=ObservedTypeAccess)
 *   3) 若需自动追加"关系标识"：
 *       - bAutoAddRelationTagRequirement：从 RelationData.Traits 取 RelationTag，加 presence=All。
 *         （若 RelationTag 恰好就是 ObservedType，跳过避免重复加。）
 *       - bAutoAddRelationFragmentRequirement：取 RelationFragmentType，确认是 fragment（不是 tag）后
 *         加 Requirement(access=RelationFragmentAccessType, presence=All)。
 *   4) 拼 DebugDescription：含 RelationType / ObservedType / Operation 三个字段，便于日志识别。
 *
 * 调用约束：必须在 ConfigureRelationObserver 之后调用——它依赖 ObservedType / RelationTypeHandle 已就绪。
 */
void UMassRelationObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	check(ObservedType);
	checkf(UE::Mass::IsA<FMassFragment>(ObservedType) || UE::Mass::IsA<FMassTag>(ObservedType)
		, TEXT("Only tags and fragments are valid observed types for RelationObservers. Received %s")
		, *ObservedType->GetName());

	if (UE::Mass::IsA<FMassTag>(ObservedType))
	{
		// 中文：ObservedType 是 Tag → query 要求实体含此 Tag。
		EntityQuery.AddTagRequirement(*ObservedType, EMassFragmentPresence::All);
	}
	else // UE::Mass::IsA<FMassFragment>(ObservedType)
	{
		// 中文：ObservedType 是 Fragment → 要求实体含此 Fragment 且按 ObservedTypeAccess 访问。
		EntityQuery.AddRequirement(ObservedType, ObservedTypeAccess);
	}

	if (bAutoAddRelationFragmentRequirement || bAutoAddRelationTagRequirement)
	{
		const UE::Mass::FRelationManager& RelationManager = EntityManager->GetRelationManager();
		const UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(RelationTypeHandle);

		if (bAutoAddRelationTagRequirement)
		{
			// 中文：把"关系 Tag"作为额外存在性要求加入 query。
			//      关系 Tag 是 reify 关系实体身份的标识（如 FMassChildOfRelation）。
			const TNotNull<const UScriptStruct*> RelationTag = RelationData.Traits.GetRelationTagType();
			if (RelationTag != ObservedType)
			{
				EntityQuery.AddTagRequirement(*RelationTag, EMassFragmentPresence::All);
			}
		}

		if (bAutoAddRelationFragmentRequirement)
		{
			// 中文：把"关系 Fragment"加入 query（仅当它确实是 Fragment）。
			//      关系 Fragment 一般是 FMassRelationFragment{Subject, Object}。
			const TNotNull<const UScriptStruct*> RelationFragmentType = RelationData.Traits.RelationFragmentType;
			if (UE::Mass::IsA<FMassFragment>(RelationFragmentType) && RelationFragmentType != ObservedType)
			{
				EntityQuery.AddRequirement(RelationFragmentType, RelationFragmentAccessType, EMassFragmentPresence::All);
			}
		}
	}

	// 中文：Debug 描述字符串，主要用于日志和 stat profiler 的可读性。
	DebugDescription = FString::Printf(TEXT("RelationType: %s ObservedType: %s Operation: %s")
		, *RelationTypeHandle.ToString(), *ObservedType->GetName()
		, *LexToString(ObservedOperations));
}

/**
 * 中文：把"关系类型句柄 + traits"吃进 observer 的成员。
 *   * 默认行为：ObservedType ← Traits.RelationFragmentType（即关系 Fragment）；
 *     RelationTypeHandle ← InRegisteredTypeHandle。
 *   * 返回 true 表示配置成功；子类（如 UMassRelationRoleDestruction）会 override 返回 bool 决定
 *     是否丢弃该 observer。
 *   * const_cast：Traits 的字段是 `const UScriptStruct* RelationFragmentType`，但 ObservedType
 *     在 UMassObserverProcessor 中声明为 UScriptStruct*（非 const）。这里强制去掉 const 是因为
 *     UScriptStruct 反射元数据本身是只读的——const_cast 只是为了类型匹配，不会修改它。
 */
bool UMassRelationObserver::ConfigureRelationObserver(UE::Mass::FTypeHandle InRegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& Traits)
{
	ObservedType = &const_cast<UScriptStruct&>(*Traits.RelationFragmentType);
	RelationTypeHandle = InRegisteredTypeHandle;
	return true;
}

//-----------------------------------------------------------------------------
// UMassRelationEntityCreation
//-----------------------------------------------------------------------------

/**
 * 中文：构造函数——
 *   * ObservedOperations = CreateEntity：仅监听"创建实体"操作。
 *   * ExecutionPriority = 1024：高优先级，保证比"普通 observer（默认 0）"先跑。
 *     这样普通 observer 看到的 RelationManager 索引是已经被 RelationManager.CreateRelationInstances
 *     写好的——不会有"看到关系实体但 RoleMap 还没条目"的窗口。
 */
UMassRelationEntityCreation::UMassRelationEntityCreation()
{
	ObservedOperations = EMassObservedOperationFlags::CreateEntity;
	ExecutionPriority = RelationCreationObserverExecutionPriority;
};

/**
 * 中文：默认 Execute 是空的。
 *
 * 注释中保留了一段示例代码，演示如何遍历刚被创建的关系实体，把 (Subject, Object, RelationEntity)
 * 三元组收集起来——具体业务（如 ChildOf 把 Parent 写到 child fragment）由派生类完成。
 *
 * 当前 Mass 关系系统的索引（FRelationManager.RoleMap）实际上是在 RelationManager.CreateRelationInstances
 * 这条 API 内部写入的（M12 的实现），不需要在此 observer 里再做一次。observer 在这里更多是"扩展点"，
 * 给关系类型的具体实现留下时机插入逻辑。
 */
void UMassRelationEntityCreation::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	/*
	// here's example code showing how the freshly-created relation entity can be utilized.
	// at the current state of MassRelations implementation we don't need to do anything here. 

	struct FRelationInstanceRegistration
	{
		FMassEntityHandle SubjectHandle;
		FMassEntityHandle ObjectHandle;
		FMassEntityHandle RelationEntityHandle;
	};

	TArray<FRelationInstanceRegistration> RelationInstances;
	EntityQuery.ForEachEntityChunk(Context, [&RelationInstances, ObservedType = ObservedType](FMassExecutionContext& ExecutionContext)
	{
		TConstArrayView<FMassRelationFragment> RelationFragments = ExecutionContext.GetFragmentView<FMassRelationFragment>(ObservedType);
		for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			RelationInstances.Add({
				RelationFragments[EntityIt].Subject
				, RelationFragments[EntityIt].Object
				, ExecutionContext.GetEntity(EntityIt)
			});
		}
	});
	*/
}

//-----------------------------------------------------------------------------
// UMassRelationEntityGuardDog
//-----------------------------------------------------------------------------

/**
 * 中文：构造——监听 RemoveElement（移除 Fragment/Tag）操作。
 *   * 关系实体上的 fragment/tag 是 Mass 的私有 implementation detail，业务方应当通过
 *     销毁整个关系实体来"移除关系"，不应单独 Remove 其 RelationFragment / RelationTag。
 */
UMassRelationEntityGuardDog::UMassRelationEntityGuardDog()
{
	ObservedOperations = EMassObservedOperationFlags::RemoveElement;
}

/**
 * 中文：执行——仅在 WITH_MASSENTITY_DEBUG 编译开启时做检查。
 *   * 算法：
 *       1) 缓存当前 EntityQuery 匹配的 archetypes。
 *       2) 取出本批次的 EntityCollection（即正被 RemoveElement 操作的实体批次）。
 *       3) 若该 collection 的 archetype *存在于* query 匹配集中，说明这是一个真正的关系实体
 *          被违法地"删除其 RelationFragment"——触发 ensure 报警。
 *
 * #if WITH_MASSENTITY_DEBUG：发布版编译会把整个函数体编译为空，避免 release 版的检查开销。
 */
void UMassRelationEntityGuardDog::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& InContext)
{
#if WITH_MASSENTITY_DEBUG
	EntityQuery.CacheArchetypes();
	const FMassArchetypeEntityCollection& EntityCollection = InContext.GetEntityCollection();
	ensureMsgf(EntityQuery.GetArchetypes().Contains(EntityCollection.GetArchetype()) == false
		, TEXT("Trying to remove private-implementation-detail fragments from a relation entity is not supported!"));
#endif // WITH_MASSENTITY_DEBUG
}

//-----------------------------------------------------------------------------
// UMassRelationEntityDestruction
//-----------------------------------------------------------------------------

/** 中文：构造——监听 DestroyEntity（销毁实体）操作。 */
UMassRelationEntityDestruction::UMassRelationEntityDestruction()
{
	ObservedOperations = EMassObservedOperationFlags::DestroyEntity;
};

/**
 * 中文：当 *关系实体* 被销毁时，反向更新 RelationManager.RoleMap。
 *
 * 算法：
 *   1) 用 EntityQuery 遍历本批被销毁的关系实体，从其 FMassRelationFragment 上读出 (Subject, Object)，
 *      存入 EntitiesToClearOut 临时数组。
 *      —— 这里不能直接在遍历中改 RoleMap，因为遍历时 RoleMap 可能也在被读，避免迭代器失效与潜在锁竞争。
 *   2) 拿到 RelationManager.RoleMap：
 *        - 在 Subject 的条目中 [Object 角色组] 移除 Object（用 RemoveAllSwap，不缩容以保留容量）。
 *        - 在 Object 的条目中 [Subject 角色组] 移除 Subject。
 *      若某端在 RoleMap 中已没条目（之前被清空），Find 返回 nullptr，安全跳过。
 *   3) 不主动删除 RoleMap 中"空"的条目——这是后续清理或下次创建时一并处理。
 */
void UMassRelationEntityDestruction::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TArray<TPair<FMassEntityHandle, FMassEntityHandle>> EntitiesToClearOut;

	EntityQuery.ForEachEntityChunk(Context, [&EntitiesToClearOut, ObservedType = ObservedType](FMassExecutionContext& ExecutionContext)
	{
		TConstArrayView<FMassRelationFragment> RelationFragments = ExecutionContext.GetFragmentView<FMassRelationFragment>(ObservedType);
		for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			EntitiesToClearOut.Add({RelationFragments[EntityIt].Subject, RelationFragments[EntityIt].Object});
		}
	});

	if (EntitiesToClearOut.Num())
	{
		UE::Mass::FRelationManager& RelationManager = Context.GetEntityManagerChecked().GetRelationManager();
		UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(RelationTypeHandle);

		for (const TPair<FMassEntityHandle, FMassEntityHandle>& Pair : EntitiesToClearOut)
		{
			// 中文：从 Subject 实体的 RoleMap 条目中，移除"它指向 Object"的那项（角色 = Object）。
			if (UE::Mass::FRelationData::FRelationInstanceData* InstanceData = RelationData.RoleMap.Find(Pair.Get<0>()))
			{
				(*InstanceData)[static_cast<int32>(UE::Mass::ERelationRole::Object)].RemoveAllSwap(
					FMassRelationRoleInstanceHandle::FMassRelationRoleInstanceHandleFinder(Pair.Get<1>())
					, EAllowShrinking::No);
			}
			// 中文：从 Object 实体的 RoleMap 条目中，移除"它被 Subject 指向"的那项（角色 = Subject）。
			if (UE::Mass::FRelationData::FRelationInstanceData* InstanceData = RelationData.RoleMap.Find(Pair.Get<1>()))
			{
				(*InstanceData)[static_cast<int32>(UE::Mass::ERelationRole::Subject)].RemoveAllSwap(
					FMassRelationRoleInstanceHandle::FMassRelationRoleInstanceHandleFinder(Pair.Get<0>())
					, EAllowShrinking::No);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// UMassRelationRoleDestruction
//-----------------------------------------------------------------------------

/**
 * 中文：构造——
 *   * 监听 DestroyEntity；本 observer 关心的是"角色实体"（subject/object）被销毁。
 *   * 关闭 bAutoAddRelationFragmentRequirement / bAutoAddRelationTagRequirement：
 *     因为我们要 match *角色实体*（不携带 RelationFragment），关系系自动追加的"必须有
 *     RelationFragment"会让 query 永远匹配不到目标。
 */
UMassRelationRoleDestruction::UMassRelationRoleDestruction()
{
	ObservedOperations = EMassObservedOperationFlags::DestroyEntity;
	bAutoAddRelationFragmentRequirement = false;
	bAutoAddRelationTagRequirement = false;
};

/**
 * 中文：执行——直接把 ConfigureRelationObserver 时选定的 lambda 喂给 ForEachEntityChunk 遍历。
 *   * 不同 DestructionPolicy 有不同 lambda（CleanUp / Destroy / Splice），见
 *     ConfigureRelationObserver 的 switch。
 */
void UMassRelationRoleDestruction::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, ExecuteFunction);
}

/**
 * 中文：override 父类的 ConfigureQueries。
 *   * 先调 Super::ConfigureQueries——它会按 ObservedType 加基本要求（这里 ObservedType
 *     是角色端的 element 或关系 tag）。
 *   * 然后追加 ExcludedRelationFragmentType 的 None/None 要求：
 *       Access=None    意为不绑定数据（不读不写）；
 *       Presence=None  意为实体必须 *不含* 此 Fragment——把关系实体过滤掉，只留角色实体。
 *
 * @todo 注释提到一个缺失特性：当前 query 系统不支持"外部依赖"——即那种"我会触碰其他实体上的
 *       数据但不要求该 Fragment 出现在我自己的 archetype 里"的概念。所以这里只能用 None/None
 *       做硬过滤；未来可能引入一种"外部 requirement"机制更精确地表达依赖。
 */
void UMassRelationRoleDestruction::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::ConfigureQueries(EntityManager);
	// extend the query with "none" reliance on the relation entity's data fragment
	// @todo missing feature - this would cause the query to always fail to find archetypes. We need a way to
	//		add "external requirements" to queries, that we'd use to calculate dependencies, but not use for binding

	if (ensureMsgf(ExcludedRelationFragmentType
		, TEXT("We don't expect ExcludedRelationFragmentType to be null. Make sure ConfigureRelationObserver has been called first.")))
	{
		EntityQuery.AddRequirement(ExcludedRelationFragmentType, EMassFragmentAccess::None, EMassFragmentPresence::None);
	}
}

/**
 * 中文：核心配置函数——根据 RelationRole 与 RoleTraits[Role].DestructionPolicy 选定行为。
 *
 * static_assert：当前实现假设 ERelationRole::MAX == 2（即只有 Subject/Object 两种角色）。
 * 如果将来扩展到三方关系，本函数中的 (RoleAsIndex+1)%2 等代码需要重新审视。
 *
 * 主要分支：
 *   - RoleAsIndex/OppositeRoleIndex：把 RelationRole（可能是 Subject/Object/MAX）映射成 0/1 索引。
 *     当 RelationRole==MAX 时，统一使用 RoleAsIndex=0；OppositeRoleIndex 在 MAX 路径下其实不用。
 *   - ExcludedRelationFragmentType ← Traits.RelationFragmentType：用于在 ConfigureQueries 中
 *     过滤掉关系实体（None/None），让本 observer 只 match 角色实体。
 *   - ObservedType：
 *       优先使用 RoleTraits[RoleAsIndex].Element（角色端独有的 fragment/tag，如 FMassChildOfFragment）；
 *       若未提供 Element，则 fallback 到 RelationTypeHandle 的 Tag（即关系 Tag）。
 *   - 不调用 Super::ConfigureRelationObserver ── 因为父类默认会把 ObservedType 设为 RelationFragment，
 *     与本类的"角色实体观察"目标不符。
 *
 * DestructionPolicy 分支：
 *   * CleanUp：仅销毁所有相连的关系实体；保留对端角色实体存活。
 *       - RelationFragmentAccessType = None（不需要读关系 fragment 内部数据）。
 *       - RelationRole != MAX：只销毁 OppositeRoleIndex 那一侧的所有关系实体。
 *       - RelationRole == MAX：双向销毁所有 Subject/Object 端的关系实体。
 *   * Destroy：销毁关系实体 + 对端角色实体（级联）。
 *       - RelationFragmentAccessType = ReadOnly（需要读关系 fragment 拿到对端实体句柄）。
 *       - 通过 GetRelationEntities + GetRoleEntities 收集所有要删的实体，再 ExecutionContext.Defer().DestroyEntities。
 *   * Splice：关系系统的"链表节点删除"语义——
 *       - 若 A→B→C，B 被销毁，则把 A→C 重新连起来。
 *       - 实现：先销毁原有关系实体，拿出对端角色集合，再 RelationManager.CreateRelationInstances 重建。
 *       - 内部小工具 lambda DestroyRelationEntitiesAndGetRoleEntities：
 *           1) 取本端角色集合
 *           2) 对应的关系实体全部 deferred-destroy
 *           3) 返回对端角色实体的列表
 *   * Custom：业务方自处理；本 observer 不接管，返回 false 让调用方丢弃实例。
 *
 * 返回值：bExecutionFunctionAssigned —— 是否成功设置 ExecuteFunction。Custom 路径返回 false。
 */
bool UMassRelationRoleDestruction::ConfigureRelationObserver(UE::Mass::FTypeHandle InRegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& Traits)
{
	static_assert(static_cast<uint8>(UE::Mass::ERelationRole::MAX) == 2, "Current implementation relies on there being only two roles.");

	// 中文：把 RelationRole 转成 0/1 索引。MAX 时用 0 作为占位（实际 MAX 路径不依赖此值）。
	const int32 RoleAsIndex = RelationRole != UE::Mass::ERelationRole::MAX ? static_cast<int32>(RelationRole) : 0;
	const int32 OppositeRoleIndex = (RoleAsIndex + 1) % 2;
	bool bExecutionFunctionAssigned = false;

	// this processor is specifically implemented for handling destruction of regular entities
	// so we're using Traits.RelationFragmentType, which is only added to the relation entities,
	// to filter those out. This will ensure we only get entities that played a Role in the given relationship.
	// 中文：把 RelationFragmentType 缓存为 ExcludedRelationFragmentType，让 ConfigureQueries 用 None/None 过滤掉关系实体。
	ExcludedRelationFragmentType = Traits.RelationFragmentType;

	// note there we're deliberately not calling Super::ConfigureRelationObserver, since we're going to observe role-specific elements
	// 中文：不调 Super::ConfigureRelationObserver——本类要观察"角色端独有的 element"，与父类默认不同。
	RelationTypeHandle = InRegisteredTypeHandle;
	if (Traits.RoleTraits[RoleAsIndex].Element)
	{
		// 中文：优先使用角色端独有的 element（如 FMassChildOfFragment）作为观察类型。
		ObservedType = Traits.RoleTraits[RoleAsIndex].Element;
	}
	else
	{
		// 中文：未提供 element 时，回退到关系 Tag。
		TNotNull<const UScriptStruct*> RelationTypeTag = RelationTypeHandle.GetScriptStruct();
		ObservedType = RelationTypeTag;
	}

	switch (Traits.RoleTraits[RoleAsIndex].DestructionPolicy)
	{
		case UE::Mass::ERemovalPolicy::CleanUp:
			// 中文：CleanUp 策略——只销毁连带的关系实体，对端角色保持存活。
			RelationFragmentAccessType = EMassFragmentAccess::None;
			if (RelationRole != UE::Mass::Relations::ERelationRole::MAX)
			{
				// 中文：单向版——只处理 OppositeRoleIndex 一侧的关系实体。
				ExecuteFunction = [InRegisteredTypeHandle, OppositeRoleIndex](FMassExecutionContext& ExecutionContext)
					{
						UE::Mass::FRelationManager& RelationManager = ExecutionContext.GetEntityManagerChecked().GetRelationManager();
						UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(InRegisteredTypeHandle);
						for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
						{
							// 中文：从 RoleMap 取出本角色"对端"那一侧的所有 RoleInstanceHandle。
							TArray<FMassRelationRoleInstanceHandle>& Container = RelationData.RoleMap.FindChecked(ExecutionContext.GetEntity(EntityIt))[OppositeRoleIndex];
							// 中文：deferred-destroy 所有相连的关系实体；本角色实体本身不动（CleanUp 语义）。
							ExecutionContext.Defer().DestroyEntities(RelationManager.GetRelationEntities(Container));
							Container.Empty();
						}
					};
			}
			else
			{
				// this observer will only get called once and needs to handle both sides of the relation
				// 中文：双向版——同时处理 Subject 端和 Object 端。仅当两端的 element + policy 完全一致时才走此路径。
				ExecuteFunction = [InRegisteredTypeHandle](FMassExecutionContext& ExecutionContext)
					{
						UE::Mass::FRelationManager& RelationManager = ExecutionContext.GetEntityManagerChecked().GetRelationManager();
						UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(InRegisteredTypeHandle);
						for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
						{
							UE::Mass::FRelationData::FRelationInstanceData& InstanceData = RelationData.RoleMap.FindChecked(ExecutionContext.GetEntity(EntityIt));

							TArray<FMassRelationRoleInstanceHandle>& SubjectsContainer = InstanceData[static_cast<uint8>(UE::Mass::ERelationRole::Subject)];
							// this will instigate destruction of relations where Entity is the object
							// 中文：本实体作为 Object 端的所有关系，把 Subject 侧的 relation entities 全销毁。
							ExecutionContext.Defer().DestroyEntities(RelationManager.GetRelationEntities(SubjectsContainer));
							SubjectsContainer.Empty();

							TArray<FMassRelationRoleInstanceHandle>& ObjectsContainer = InstanceData[static_cast<uint8>(UE::Mass::ERelationRole::Object)];
							// this will instigate destruction of relations where Entity is the subject
							// 中文：本实体作为 Subject 端的所有关系，把 Object 侧的 relation entities 全销毁。
							ExecutionContext.Defer().DestroyEntities(RelationManager.GetRelationEntities(ObjectsContainer));
							ObjectsContainer.Empty();
						}
					};
			}
			bExecutionFunctionAssigned = true;
			break;
		case UE::Mass::ERemovalPolicy::Destroy:
			// 中文：Destroy 策略——级联销毁对端角色实体 + 关系实体。
			RelationFragmentAccessType = EMassFragmentAccess::ReadOnly;
			// destroy the other side of the relationship, and the relation entity
			if (RelationRole != UE::Mass::Relations::ERelationRole::MAX)
			{
				// 中文：单向版——只处理 OppositeRoleIndex 那侧（包括对端角色实体 + 关系实体）。
				ExecuteFunction = [InRegisteredTypeHandle, OppositeRoleIndex](FMassExecutionContext& ExecutionContext)
				{
					UE::Mass::FRelationManager& RelationManager = ExecutionContext.GetEntityManagerChecked().GetRelationManager();
					UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(InRegisteredTypeHandle);

					TArray<FMassEntityHandle> EntitiesToDestroy;

					for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
					{
						TArray<FMassRelationRoleInstanceHandle>& Container = RelationData.RoleMap.FindChecked(ExecutionContext.GetEntity(EntityIt))[OppositeRoleIndex];
						// 中文：先收集"关系实体"，再收集"对端角色实体"，全部累积到 EntitiesToDestroy 一并 deferred-destroy。
						RelationManager.GetRelationEntities(Container, EntitiesToDestroy);
						RelationManager.GetRoleEntities(Container, EntitiesToDestroy);
						Container.Empty();
					}

					ExecutionContext.Defer().DestroyEntities(EntitiesToDestroy);
				};
			}
			else
			{
				// this implementation handles both sides of relations
				// 中文：双向版——同时处理两端，逻辑等价于"我作为 Subject"和"我作为 Object"两轮。
				ExecuteFunction = [InRegisteredTypeHandle](FMassExecutionContext& ExecutionContext)
				{
					UE::Mass::FRelationManager& RelationManager = ExecutionContext.GetEntityManagerChecked().GetRelationManager();
					UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(InRegisteredTypeHandle);

					TArray<FMassEntityHandle> EntitiesToDestroy;

					for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
					{
						UE::Mass::FRelationData::FRelationInstanceData& InstanceData = RelationData.RoleMap.FindChecked(ExecutionContext.GetEntity(EntityIt));

						TArray<FMassRelationRoleInstanceHandle>& SubjectsContainer = InstanceData[static_cast<uint8>(UE::Mass::ERelationRole::Subject)];
						// this will instigate destruction of both the relation entities and the relation subjects
						RelationManager.GetRelationEntities(SubjectsContainer, EntitiesToDestroy);
						RelationManager.GetRoleEntities(SubjectsContainer, EntitiesToDestroy);
						SubjectsContainer.Empty();

						TArray<FMassRelationRoleInstanceHandle>& ObjectsContainer = InstanceData[static_cast<uint8>(UE::Mass::ERelationRole::Object)];
						// this will instigate destruction of both the relation entities and the relation objects
						RelationManager.GetRelationEntities(ObjectsContainer, EntitiesToDestroy);
						RelationManager.GetRoleEntities(ObjectsContainer, EntitiesToDestroy);
						ObjectsContainer.Empty();
					}

					ExecutionContext.Defer().DestroyEntities(EntitiesToDestroy);
				};
			}
			bExecutionFunctionAssigned = true;
			break;
		case UE::Mass::ERemovalPolicy::Splice:
			// use my Object as my Subject's Object, and vive versa
			// 中文：Splice 策略——把本节点"摘除"，将其 Subject 端与 Object 端互联。
			//      类似双向链表删除：原 (S)→(Me)→(O) 变为 (S)→(O)。
			ExecuteFunction = [InRegisteredTypeHandle](FMassExecutionContext& ExecutionContext)
			{
				/**
				 * 中文：内部小工具 lambda——
				 *   * 取出本端的 RoleInstanceHandle 数组。
				 *   * 把对应的关系实体全部 deferred-destroy。
				 *   * 返回 *对端* 角色实体的句柄列表（供 splice 重建用）。
				 */
				static auto DestroyRelationEntitiesAndGetRoleEntities = [](
					const FMassExecutionContext& LocalExecutionContext
					, const UE::Mass::FRelationManager& RelationManager
					, const UE::Mass::FRelationData::FRelationInstanceData& InstanceData
					, const UE::Mass::ERelationRole Role) -> TArray<FMassEntityHandle>
				{
					const TArray<FMassRelationRoleInstanceHandle>& RoleRelationInstanceHandles = InstanceData[Role];

					TArray<FMassEntityHandle> RoleRelationEntityHandles = RelationManager.GetRelationEntities(RoleRelationInstanceHandles);
					LocalExecutionContext.Defer().DestroyEntities(MoveTemp(RoleRelationEntityHandles));

					return RelationManager.GetRoleEntities(RoleRelationInstanceHandles);
				};

				UE::Mass::FRelationManager& RelationManager = ExecutionContext.GetEntityManagerChecked().GetRelationManager();
				UE::Mass::FRelationData& RelationData = RelationManager.GetRelationDataChecked(InRegisteredTypeHandle);

				for (FMassExecutionContext::FEntityIterator EntityIt = ExecutionContext.CreateEntityIterator(); EntityIt; ++EntityIt)
				{
					const FMassEntityHandle RoleEntity = ExecutionContext.GetEntity(EntityIt);
					if (UE::Mass::FRelationData::FRelationInstanceData* InstanceData = RelationData.RoleMap.Find(RoleEntity))
					{
						// @todo optimize array allocations here - we could create the arrays outside and re-use
						// 中文：拿到本节点的 Subject 端实体列表（对端就是"父"）和 Object 端实体列表（对端就是"子"）。
						TArray<FMassEntityHandle> SubjectEntities = DestroyRelationEntitiesAndGetRoleEntities(ExecutionContext, RelationManager, *InstanceData, UE::Mass::ERelationRole::Subject);
						TArray<FMassEntityHandle> ObjectEntities = DestroyRelationEntitiesAndGetRoleEntities(ExecutionContext, RelationManager, *InstanceData, UE::Mass::ERelationRole::Object);
						
						// 中文：用对端列表重新组合关系：把所有"父"与所有"子"两两建立新关系（笛卡尔积）。
						RelationManager.CreateRelationInstances(InRegisteredTypeHandle, MakeArrayView(SubjectEntities), MakeArrayView(ObjectEntities));
					}
				}
			};
			bExecutionFunctionAssigned = true;
			break;
		case UE::Mass::ERemovalPolicy::Custom: // fall through on purse
			// 中文：Custom 与 default fall-through——本 observer 不接管，bExecutionFunctionAssigned 保持 false，
			//      调用方（AddObserverInstances）应丢弃此实例。
		default: ;
	}

	return bExecutionFunctionAssigned;
}

/**
 * 中文：静态工厂——为给定关系类型创建并注册 RoleDestruction observer 实例。
 *
 * 决策树：
 *   * 若两个角色的 (Element, DestructionPolicy) 完全相同：
 *       仅创建 1 个 observer，RelationRole=MAX，让 lambda 一次性处理双方——节省一份 archetype 匹配开销。
 *       Custom policy 直接跳过。
 *   * 否则：
 *       为每个 Role（Subject、Object）单独创建 1 个 observer。
 *       Custom policy 的 Role 直接跳过。
 *
 * static_assert：保护"只有两个角色"的假设——若将来引入 Triadic relation，需要重写本函数。
 *
 * 调用流程：
 *   1) NewObject<UMassRelationRoleDestruction>()
 *   2) 设置 RelationRole
 *   3) ConfigureRelationObserver(...) ──若返回 false（Custom）则不挂到 ObserverManager，让对象自然 GC。
 *   4) ObserverManager.AddObserverInstance(observer) ──正式注册。
 */
void UMassRelationRoleDestruction::AddObserverInstances(FMassObserverManager& ObserverManager, UE::Mass::FTypeHandle InRegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& Traits)
{
	static_assert(static_cast<int32>(UE::Mass::ERelationRole::MAX) == 2, "This implementation is tailored the there being only two roles to any relation");

	// is we both roles have the same policy and the same element, we can get away with creating only one observer
	// as long as we configure it appropriately
	// 中文：两端 element 与 policy 完全相同 → 只用 1 个双向 observer。
	if (Traits.RoleTraits[1].Element == Traits.RoleTraits[0].Element && Traits.RoleTraits[1].DestructionPolicy == Traits.RoleTraits[0].DestructionPolicy)
	{
		if (Traits.RoleTraits[0].DestructionPolicy != UE::Mass::ERemovalPolicy::Custom)
		{
			UMassRelationRoleDestruction* ObserverProcessor = NewObject<UMassRelationRoleDestruction>();
			ObserverProcessor->RelationRole = UE::Mass::ERelationRole::MAX;
			if (ObserverProcessor->ConfigureRelationObserver(InRegisteredTypeHandle, Traits))
			{
				ObserverManager.AddObserverInstance(ObserverProcessor);
			}
		}
	}
	else
	{
		// 中文：两端不同 → 为每个 Role 各创建 1 个 observer。Custom 直接跳过。
		for (int32 RoleIndex = 0; RoleIndex < static_cast<int32>(UE::Mass::ERelationRole::MAX); ++RoleIndex)
		{
			if (Traits.RoleTraits[RoleIndex].DestructionPolicy != UE::Mass::ERemovalPolicy::Custom)
			{
				UMassRelationRoleDestruction* ObserverProcessor = NewObject<UMassRelationRoleDestruction>();
				ObserverProcessor->RelationRole = static_cast<UE::Mass::ERelationRole>(RoleIndex);
				if (ObserverProcessor->ConfigureRelationObserver(InRegisteredTypeHandle, Traits))
				{
					ObserverManager.AddObserverInstance(ObserverProcessor);
				}
			}
		}
	}
}
