// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRelationManager.h"
#include "MassEntityBuilder.h"
#include "MassEntityTypes.h"
#include "MassEntityManager.h"
#include "MassRelationObservers.h"
#include "MassExecutionContext.h"
#include "Interfaces/ITextureFormat.h"
#include "MassCommands.h"
#include "MassArchetypeGroupCommands.h"
#include "Algo/RemoveIf.h"

// =============================================================================
// 【实现说明】FRelationManager 与 FRelationData 的实现细节。
//
// 【核心算法分布】
//   - HandleRole / AddRoleInstance：维护 RoleMap（角色端→关系列表）的核心。
//   - CreateRelationInstances：批量创建关系（验证 + 实体创建 + 索引更新 + 层级分组）。
//   - GatherHierarchy：递归向下收集层级，支持环检测。
//   - IsSubjectOfRelationRecursive：BFS 检查"祖先链"包含关系。
//   - OnRelationTypeRegistered：注册时挂 observer + 创建 RelationData 桶。
// =============================================================================

DECLARE_CYCLE_STAT(TEXT("Mass Relation IsSubject"), STAT_Mass_IsSubject, STATGROUP_Mass);
DECLARE_CYCLE_STAT(TEXT("Mass Relation IsSubject Recursive"), STAT_Mass_IsSubjectRecursive, STATGROUP_Mass);

namespace UE::Mass
{
	namespace Relations::Private
	{
		/**
		 * 【HandleRole】更新 RoleMap：把"OperatorEntity 作为 Role 角色参与了关系 RelationEntity"这一事实记录下来。
		 *
		 * 【参数命名解释】
		 *   - OperatorEntity：本次更新的"主键 entity"——它在 RoleMap 中的条目要被更新；
		 *   - Role：与 OperatorEntity *相对*的角色（即"对手"在关系中扮演什么）；
		 *   - RoleEntity：扮演该 Role 的对手实体；
		 *   - RelationEntity：关系实体本身。
		 *
		 *   即 RoleMap[OperatorEntity][Role] 会追加一个 handle，handle 的 GetRoleEntityIndex() 是 RoleEntity。
		 *   语义上："OperatorEntity 关联了一个 Role=RoleEntity"。
		 *
		 * 【独占处理】
		 *   若 RoleTraits[Role].bExclusive==true 且已有旧条目，把旧关系实体加入待销毁队列，
		 *   再插入新条目——保证"该 OperatorEntity 在该 Role 上只有一个对手"。
		 *
		 *   典型场景：ChildOf 中 Object 端独占——一个 child 只能有一个 parent。
		 *   当再次为同一 child 创建 ChildOf 时，会把旧的 parent 关系销毁。
		 */
		inline void HandleRole(const FMassEntityManager& EntityManager, FRelationData& RelationData, const FMassEntityHandle& OperatorEntity, UE::Mass::ERelationRole Role
			, const FMassEntityHandle& RoleEntity, const FMassEntityHandle& RelationEntity
			, TArray<FMassEntityHandle>& InOutRelationEntitiesToDestroy)
		{
			const int32 RoleIndex = static_cast<int32>(Role);
			// 仅在 traits 要求 RelationManager 自动维护映射时才更新 RoleMap。
			if (RelationData.Traits.RoleTraits[RoleIndex].RequiresExternalMapping == EExternalMappingRequired::Yes)
			{
				// FindOrAdd：第一次为该 entity 添加关系时自动创建 FRelationInstanceData 条目。
				TArray<FMassRelationRoleInstanceHandle>& RoleData = RelationData.RoleMap.FindOrAdd(OperatorEntity)[RoleIndex];

				// 用 30+30+2 位打包构造新 handle。
				const FMassRelationRoleInstanceHandle NewInstanceHandle = FMassRelationRoleInstanceHandle::Create(Role, RoleEntity, RelationEntity);

				// if objects are exclusive we need to destroy the previous instance
				// 【独占处理】若该 role 配置为独占且已存在条目，把旧关系实体送入待销毁队列。
				if (RelationData.Traits.RoleTraits[RoleIndex].bExclusive
					&& RoleData.Num()
					&& RoleData[0] != NewInstanceHandle)
				{
					// Pop(EAllowShrinking::No)：保留 array 容量，避免反复 alloc/free。
					FMassRelationRoleInstanceHandle PreviousInstance = RoleData.Pop(EAllowShrinking::No);
					const FMassEntityHandle PreviousRelationEntity = PreviousInstance.GetRelationEntityHandle(EntityManager);
					InOutRelationEntitiesToDestroy.Add(PreviousRelationEntity);
				}

#if WITH_MASSENTITY_DEBUG
				// 调试校验：不该有重复的 handle 被插入——若出现说明上层逻辑有 bug。
				const int32 ExistingIndex = RoleData.Find(NewInstanceHandle);
				ensureMsgf(ExistingIndex == INDEX_NONE, TEXT("Given relation instance handle is already present in role mapping"));
#endif // WITH_MASSENTITY_DEBUG

				RoleData.Add(NewInstanceHandle);
			}
		}

		/**
		 * 【AddRoleInstance】给一对 (Subject, Object) 关系做完整的双向 RoleMap 更新。
		 *   关键：从 Subject 侧看，它的"对手"是 Object，对手 role 是 Object → 写入 RoleMap[Subject][Object]；
		 *         从 Object 侧看，它的"对手"是 Subject，对手 role 是 Subject → 写入 RoleMap[Object][Subject]。
		 *   这样 RoleMap[Subject][Object] = "我作为 Subject 时的对手 Object 们"。
		 */
		void AddRoleInstance(const FMassEntityManager& EntityManager, FRelationData& RelationData, const FMassEntityHandle Subject, const FMassEntityHandle Object, const FMassEntityHandle RelationEntityHandle
			, TArray<FMassEntityHandle>& InOutRelationEntitiesToDestroy)
		{
			HandleRole(EntityManager, RelationData, Subject, ERelationRole::Object, Object, RelationEntityHandle, InOutRelationEntitiesToDestroy);
			HandleRole(EntityManager, RelationData, Object, ERelationRole::Subject, Subject, RelationEntityHandle, InOutRelationEntitiesToDestroy);
		}
	}

	/**
	 * 【FScopedRecursiveLimit】RAII 计数器，用于防止"递归调用栈过深"。
	 *   当前限制为 10 层；构造时 ++，析构时 --，超出限制时 check 失败提前暴露问题。
	 *   主要用于 destruction observer 链——A 销毁触发 B 销毁触发 C 销毁等。
	 */
	struct FScopedRecursiveLimit
	{
		FScopedRecursiveLimit(int32& Limit)
			: LimitRef(Limit)
		{
			++LimitRef;
			check(LimitRef < 10);
		}
		~FScopedRecursiveLimit()
		{
			--LimitRef;
		}
		int32& LimitRef;
	};

	//-----------------------------------------------------------------------------
	// FRelationData
	//-----------------------------------------------------------------------------
	FRelationData::FRelationData(const FRelationTypeTraits& InTraits)
		: Traits(InTraits)
	{
	}

	TArray<FMassEntityHandle> FRelationData::GetParticipants(const FMassEntityManager& EntityManager, const FMassEntityHandle RoleEntity, const ERelationRole QueriedRole) const
	{
		const int32 QueriedRoleIndex = static_cast<int32>(QueriedRole);

		// 校验：该 role 必须配置为 RequiresExternalMapping=Yes，否则 RoleMap 中根本没数据。
		ensureMsgf(Traits.RoleTraits[QueriedRoleIndex].RequiresExternalMapping == EExternalMappingRequired::Yes
			, TEXT("Fetching relation participants or role %s, while role traits explicitly prevent generic mapping")
			, *LexToString(QueriedRole));

		TArray<FMassEntityHandle> ReturnSubjects;
		// 在 RoleMap 中查 RoleEntity 的条目；不存在说明该 entity 没参与任何此类型关系。
		if (const FRelationInstanceData* InstanceData = RoleMap.Find(RoleEntity))
		{
			ReturnSubjects.Reserve((*InstanceData)[QueriedRoleIndex].Num());
			// 遍历 [QueriedRole] 列表中的每个 handle，还原为完整 EntityHandle 后加入结果。
			// IsValid() 过滤掉那些已经被销毁但尚未从 RoleMap 中清理的悬空 handle。
			for (const FMassRelationRoleInstanceHandle RelationInstanceHandle : (*InstanceData)[QueriedRoleIndex])
			{
				const FMassEntityHandle RoleEntityHandle = RelationInstanceHandle.GetRoleEntityHandle(EntityManager);
				if (RoleEntityHandle.IsValid())
				{
					ReturnSubjects.Add(RoleEntityHandle);
				}
			}
		}
		return ReturnSubjects;
	}

	//-----------------------------------------------------------------------------
	// FRelationManager::FHierarchyEntitiesContainer
	//-----------------------------------------------------------------------------
	void FRelationManager::FHierarchyEntitiesContainer::StoreUnique(const uint32 Depth, TArray<FMassEntityHandle>& InOutArray)
	{
		// 从后往前扫，遇到已存在的就 RemoveAtSwap（O(1) 移除，不保序）。
		// 倒序是因为 RemoveAtSwap 会破坏当前位置之后元素的下标稳定性，倒序则不影响已遍历部分。
		for (int32 HandleIndex = InOutArray.Num() - 1; HandleIndex >= 0; --HandleIndex)
		{
			bool bIsAlreadyInSet = false;
			ExistingElements.FindOrAdd(InOutArray[HandleIndex], &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				InOutArray.RemoveAtSwap(HandleIndex);
			}
		}

		if (InOutArray.Num())
		{
			// 按需扩容到目标深度——TArray<TArray<>> 的中间空层用 default-constructed 空数组填充。
			if (static_cast<uint32>(ContainerPerLevel.Num()) <= Depth)
			{
				ContainerPerLevel.AddDefaulted(Depth - ContainerPerLevel.Num() + 1);
			}
			ContainerPerLevel[Depth].Append(InOutArray);
		}
	}

	void FRelationManager::FHierarchyEntitiesContainer::StoreUnique(const uint32 Depth, const FMassEntityHandle Handle)
	{
		// 单元素版本：直接 FindOrAdd 检查是否已存在，未存在才加入对应深度层。
		bool bIsAlreadyInSet = false;
		ExistingElements.FindOrAdd(Handle, &bIsAlreadyInSet);
		if (bIsAlreadyInSet == false)
		{
			if (static_cast<uint32>(ContainerPerLevel.Num()) <= Depth)
			{
				ContainerPerLevel.AddDefaulted(Depth - ContainerPerLevel.Num() + 1);
			}
			ContainerPerLevel[Depth].Add(Handle);
		}
	}

	//-----------------------------------------------------------------------------
	// FRelationManager
	//-----------------------------------------------------------------------------
	FRelationManager::FRelationManager(FMassEntityManager& InEntityManager)
		: EntityManager(InEntityManager)
		, TypeManager(InEntityManager.GetTypeManager())
	{
	}

	TArray<FMassEntityHandle> FRelationManager::CreateRelationInstances(const FTypeHandle RelationTypeHandle, TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects)
	{
		// =====================================================================
		// 【批量创建关系实例的完整流程】
		//
		// 阶段 1：参数与权限校验。
		// 阶段 2：就地剔除无效配对（swap 到末尾，缩短有效区间）。
		// 阶段 3：用 EntityBuilder 创建一批关系实体（每个挂 RelationFragment + RelationTagType）。
		// 阶段 4：更新 RoleMap，把每个新关系记入 Subject/Object 两端的索引。
		// 阶段 5：把待销毁的旧关系（独占冲突）批量提交销毁。
		// 阶段 6：给两端的角色 entity 加上 Element fragment（如 traits 配置）。
		// 阶段 7：若是层级关系，按深度更新 archetype group。
		// =====================================================================

		if (!ensureMsgf(RelationTypeHandle.IsValid(), TEXT("Invalid RelationTypeHandle passed to %hs"), __FUNCTION__))
		{
			return {};
		}
		// 至少要各 1 个端点；且数量必须匹配。
		// 注意：注释指出"允许某一端只有 1 个"，但 ensure 的判断是 Subjects.Num()==Objects.Num()——
		// 后续的 FMath::Min 才真正实现了 1-to-many 广播。
		if (!ensureMsgf(Subjects.Num(), TEXT("Relation needs a valid Subjects entity")) 
			|| !ensureMsgf(Objects.Num(), TEXT("Relation needs a valid Objects entity"))
			// we allow number mismatch if there's a single object or subject, then we create 1-to-many relation
			|| !testableEnsureMsgf(Objects.Num() == Subjects.Num(), TEXT("Relation Objects count need match Subjects")))
		{
			return {};
		}

		FRelationData& RelationData = GetRelationDataChecked(RelationTypeHandle);

		// ---------- 阶段 2：就地剔除无效配对 ----------
		// 算法：双指针——PairIndex 是当前考察位置；NumOfRelations 是有效区间右界。
		// 无效项：与末尾 swap 后 NumOfRelations--，PairIndex 不前进（再考察 swap 进来的元素）。
		int32 NumOfRelations = Subjects.Num();
		for (int32 PairIndex = 0; PairIndex < NumOfRelations; )
		{
			const FMassEntityHandle Object = Objects[PairIndex];
			const FMassEntityHandle Subject = Subjects[PairIndex];
			bool bPairValid = true;

			// 校验1：两端 entity 必须有效。
			if (!testableEnsureMsgf(Subject.IsValid(), TEXT("Relation needs a valid Subject entity")) 
				|| !testableEnsureMsgf(Object.IsValid(), TEXT("Relation needs a valid Object entity")))
			{
				bPairValid = false;
			}
			// 校验2：层级关系不能 self-loop（A 不能是自己的 parent）。
			if (!testableEnsureMsgf(RelationData.Traits.bHierarchical == false || Subject != Object
				, TEXT("Hierarchical relation requires the subject and object to be different")))
			{
				bPairValid = false;
			}
			// 校验3：相同 (Subject, Object) 不能再次创建——避免重复关系。
			if (!testableEnsureMsgf(IsSubjectOfRelation(RelationData, Subject, Object) == false
				, TEXT("Relation between the two entities already exists")))
			{
				bPairValid = false;
			}

			if (bPairValid)
			{
				++PairIndex;
			}
			else
			{
				// 把当前无效项 swap 到末尾，缩短有效区间。
				if (PairIndex + 1 < NumOfRelations)
				{
					Swap(Objects[PairIndex], Objects[NumOfRelations - 1]);
					Swap(Subjects[PairIndex], Subjects[NumOfRelations - 1]);
				}
				--NumOfRelations;
			}
		}

		if (NumOfRelations == 0)
		{
			return {};
		}

		// some pairs have been removed, we need to update the views and pretend the invalid pairs were never there
		// 把 ArrayView 收缩到只覆盖有效区间。
		if (NumOfRelations != Subjects.Num())
		{
			Objects = MakeArrayView(&Objects[0], NumOfRelations);
			Subjects = MakeArrayView(&Subjects[0], NumOfRelations);
		}

		// ---------- 阶段 3：创建关系实体 ----------
		// EntityBuilder 复用同一份 composition（fragment + tag）批量提交，效率比逐个 Create 高得多。
		FEntityBuilder EntityBuilder(EntityManager);
		// 添加 RelationFragment 并保留引用——稍后填 Subject/Object 字段后 Commit。
		FMassRelationFragment& RelationFragment = EntityBuilder.Add_GetRef<FMassRelationFragment>();
		// 添加该关系的 tag 类型（FMassRelation 子类）作为 archetype 区分标识。
		TNotNull<const UScriptStruct*> RelationTypeTag = RelationTypeHandle.GetScriptStruct();
		EntityBuilder.Add(RelationTypeTag);

		// 锁定 observer：本批操作期间 observer 不会立即触发，等本函数结束后统一 flush。
		// 这是为了避免在还没填好索引时 observer 就尝试读取造成不一致。
		TSharedRef<UE::Mass::ObserverManager::FObserverLock> ObserversLock = EntityManager.GetOrMakeObserversLock();

		TArray<FMassEntityHandle> CreatedRelationEntities;
		TArray<FMassEntityHandle> RelationEntitiesToDestroy;

		// ---------- 阶段 4：逐对创建关系实体 + 更新 RoleMap ----------
		for (int32 PairIndex = 0; PairIndex < NumOfRelations; ++PairIndex)
		{
			// FMath::Min 实现 1-to-many 广播：当 Objects.Num()==1 时所有 PairIndex 都用 Objects[0]。
			const FMassEntityHandle Object = Objects[FMath::Min(Objects.Num() - 1, PairIndex)];
			const FMassEntityHandle Subject = Subjects[FMath::Min(Subjects.Num() - 1, PairIndex)];

			RelationFragment.Subject = Subject;
			RelationFragment.Object = Object;

			// CommitAndReprepare：提交一次 entity 创建，并准备好下一次 Commit（保留 composition）。
			CreatedRelationEntities.Add(EntityBuilder.CommitAndReprepare());
			const FMassEntityHandle RelationEntityHandle = CreatedRelationEntities.Last();

			// 更新两端 RoleMap，独占冲突时记下要销毁的旧关系。
			Relations::Private::AddRoleInstance(EntityManager, RelationData, Subject, Object, RelationEntityHandle, RelationEntitiesToDestroy);
		}

		// ---------- 阶段 5：批量销毁因独占冲突而被替换的旧关系 ----------
		if (RelationEntitiesToDestroy.Num())
		{
			// 用 Defer().DestroyEntities 把销毁延迟到本帧 CommandBuffer flush 时执行——
			// 避免在 ObserverLock 期间立即销毁导致的回调嵌套。
			EntityManager.Defer().DestroyEntities(MoveTemp(RelationEntitiesToDestroy));
		}

		// ---------- 阶段 6：给角色端实体加 Element fragment ----------
		// 若 traits.RoleTraits[Subject].Element 非空，则 Subject 端实体会被加上该 Element；
		// Element 为空时退化使用 RelationTypeTag（仅有 tag 标识参与方）。
		TNotNull<const UScriptStruct*> SubjectElement = RelationData.Traits.RoleTraits[static_cast<int32>(ERelationRole::Subject)].Element
			? TNotNull<const UScriptStruct*>(RelationData.Traits.RoleTraits[static_cast<int32>(ERelationRole::Subject)].Element)
			: RelationTypeTag;
		TNotNull<const UScriptStruct*> ObjectElement = RelationData.Traits.RoleTraits[static_cast<int32>(ERelationRole::Object)].Element
			? TNotNull<const UScriptStruct*>(RelationData.Traits.RoleTraits[static_cast<int32>(ERelationRole::Object)].Element)
			: RelationTypeTag;

		if (EntityManager.IsProcessing())
		{
			// processing 中：必须走 deferred command 路径——直接修改 archetype 在 processing 期间是禁止的。
			TUniquePtr<FMassCommandAddElement> ElementAddingCommand = MakeUnique<FMassCommandAddElement>(SubjectElement);

			ElementAddingCommand->Add(Subjects);

			// 优化：若两端 element 不同则提交两条命令；相同则合并成一条减少命令数。
			if (ObjectElement != SubjectElement)
			{
				EntityManager.Defer().PushUniqueCommand(MoveTemp(ElementAddingCommand));
				ElementAddingCommand = MakeUnique<FMassCommandAddElement>(ObjectElement);
			}
			
			ElementAddingCommand->Add(Objects);

			EntityManager.Defer().PushUniqueCommand(MoveTemp(ElementAddingCommand));
		}
		else
		{
			// 非 processing 期：直接修改 archetype。
			EntityManager.AddElementToEntities(Objects, ObjectElement);
			EntityManager.AddElementToEntities(Subjects, SubjectElement);
		}

		// ---------- 阶段 7：层级关系的深度分组 ----------
		// 对于 bHierarchical 关系（如 ChildOf），关系实体需要按"在层级中的深度"打 archetype group，
		// 这样后续 query 可以按"父在前 / 子在后"的顺序遍历，做 transform 累乘等操作。
		if (RelationData.Traits.bHierarchical)
		{
			FHierarchyEntitiesContainer SubSubjects;
			uint32 MinDepth = static_cast<uint32>(-1);

			for (int32 PairIndex = 0; PairIndex < Subjects.Num(); ++PairIndex)
			{
				const FMassEntityHandle Object = Objects[FMath::Min(Objects.Num() - 1, PairIndex)];
				const FMassEntityHandle Subject = Subjects[FMath::Min(Subjects.Num() - 1, PairIndex)];

				// 找 Object（视为"父"）当前所在的 group——其 group ID 即为它的深度。
				FArchetypeGroupHandle ObjectsGroup = EntityManager.GetGroupForEntity(Object, RelationData.Traits.RegisteredGroupType);
				if (ObjectsGroup.IsValid() == false)
				{
					// Object 还没被分组——视为根（深度 0）。
					ObjectsGroup = FArchetypeGroupHandle(RelationData.Traits.RegisteredGroupType, 0);
				}

				const uint32 StartingDepth = ObjectsGroup.GetGroupID();
				MinDepth = FMath::Min(StartingDepth, MinDepth);

				// 把 Object 放在 StartingDepth 层、Subject 放在 StartingDepth+1 层。
				SubSubjects.StoreUnique(StartingDepth, Object);
				SubSubjects.StoreUnique(StartingDepth + 1, Subject);

				// 递归收集 Subject 下面的子树（注意 Subject 可能已经有自己的 children）。
				GatherHierarchy(RelationData, Subject, SubSubjects, StartingDepth + 2);
			}

			// 按深度逐层 push group 命令。
			if (EntityManager.IsProcessing())
			{
				for (uint32 Depth = MinDepth; Depth < SubSubjects.Num(); ++Depth)
				{
					FArchetypeGroupHandle GroupHandle(RelationData.Traits.RegisteredGroupType, Depth);

					// 用 deferred command 异步分组，避免在 processing 中改 archetype。
					EntityManager.Defer().PushCommand<UE::Mass::Command::FGroupEntities>(GroupHandle, MoveTemp(SubSubjects[Depth]));
				}
			}
			else
			{
				for (uint32 Depth = MinDepth; Depth < SubSubjects.Num(); ++Depth)
				{
					FArchetypeGroupHandle GroupHandle(RelationData.Traits.RegisteredGroupType, Depth);

					// 非 processing 期：构建 archetype collection 直接批量分组。
					TArray<FMassArchetypeEntityCollection> CollectionsAtLevel;
					UE::Mass::Utils::CreateEntityCollections(EntityManager, SubSubjects[Depth]
						, FMassArchetypeEntityCollection::EDuplicatesHandling::FoldDuplicates
						, CollectionsAtLevel);

					EntityManager.BatchGroupEntities(GroupHandle, CollectionsAtLevel);
				}
			}
		}

		return CreatedRelationEntities;
	}

	bool FRelationManager::DestroyRelationInstance(const FTypeHandle RelationTypeHandle, const FMassEntityHandle Subject, const FMassEntityHandle Object)
	{
		const FRelationData& RelationData = GetRelationDataChecked(RelationTypeHandle);
		// 至少有一端的 RoleMap 是被维护的，否则没法找到关系实体。
		ensure(RelationData.Traits.RoleTraits[0].RequiresExternalMapping != EExternalMappingRequired::No
			|| RelationData.Traits.RoleTraits[1].RequiresExternalMapping != EExternalMappingRequired::No);

		// 在 Subject 的 RoleMap 中查找 [Object] 列表里指向 Object 的那个 handle。
		// 注意：Subject's [Object] 列表里 GetRoleEntityIndex() 返回的是 Object 的 index——所以我们对比 Object.Index。
		if (const FRelationData::FRelationInstanceData* InstanceData = RelationData.RoleMap.Find(Subject))
		{
			for (const FMassRelationRoleInstanceHandle RelationInstanceHandle : (*InstanceData)[static_cast<uint32>(ERelationRole::Object)])
			{
				if (RelationInstanceHandle.GetRoleEntityIndex() == Object.Index)
				{
					const FMassEntityHandle RelationEntityHandle = RelationInstanceHandle.GetRelationEntityHandle(EntityManager);
					if (RelationEntityHandle.IsValid())
					{
						// 销毁关系实体——destruction observer 会负责清理 RoleMap 中的两端条目。
						EntityManager.DestroyEntity(RelationEntityHandle);
						return true;
					}
				}
			}
		}
		return false;
	}

	bool FRelationManager::DestroyRelationInstance(const FMassRelationRoleInstanceHandle RelationHandle) const
	{
		// 直接 handle 销毁路径——调用方已经知道关系实体，无需查 RoleMap。
		const FMassEntityHandle RelationEntityHandle = RelationHandle.GetRelationEntityHandle(EntityManager);
		if (RelationEntityHandle.IsValid())
		{
			EntityManager.DestroyEntity(RelationEntityHandle);
			return true;
		}
		return false;
	}

	TArray<FMassEntityHandle> FRelationManager::GetRelationSubjects(const FRelationData& RelationData, const FMassEntityHandle ObjectEntity) const
	{
		// "找以 ObjectEntity 为 Object 的所有 Subject"
		// = 从 RoleMap[ObjectEntity][Subject] 取出所有 handle 的 RoleEntity。
		return RelationData.GetParticipants(EntityManager, ObjectEntity, ERelationRole::Subject);
	}

	TArray<FMassEntityHandle> FRelationManager::GetRelationObjects(const FRelationData& RelationData, const FMassEntityHandle SubjectEntity) const
	{
		// "找 SubjectEntity 关联的所有 Object"
		// = 从 RoleMap[SubjectEntity][Object] 取出所有 handle 的 RoleEntity。
		return RelationData.GetParticipants(EntityManager, SubjectEntity, ERelationRole::Object);
	}

	void FRelationManager::GatherHierarchy(const FRelationData& RelationData, const FMassEntityHandle SubjectHandle, FHierarchyEntitiesContainer& SubSubjects, const uint32 Depth) const
	{
		// getting subject's subjects
		// 沿"作为 Object 找它的 Subject 们"方向递归——
		// 在 ChildOf 关系中：把 SubjectHandle 视为新的"父"，找它的所有 child（Subject 端）。
		TArray<FMassEntityHandle> LocalSubjects = GetRelationSubjects(RelationData, SubjectHandle);

		if (LocalSubjects.Num() > 0)
		{
			// this call will store the handles in LocalSubjects, but only the ones that have not been
			// stored earlier. This prevents the function getting stuck due to cycles.
			// 【环检测】StoreUnique 会从 LocalSubjects 中移除已经存在于 ExistingElements 的项——
			// 因此后续递归只对"新发现"的 entity 展开。即便关系图存在环，递归也会终止。
			SubSubjects.StoreUnique(Depth, LocalSubjects);
			for (const FMassEntityHandle& SubSubjectHandle : LocalSubjects)
			{
				GatherHierarchy(RelationData, SubSubjectHandle, SubSubjects, Depth + 1);
			}
		}
	}

	void FRelationManager::GetRelationEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer, TArray<FMassEntityHandle>& InOutEntityHandles) const
	{
		// 把一组 handle 的"关系实体"端解出。注意 InOutEntityHandles 是 append 模式，不清空。
		InOutEntityHandles.Reserve(RelationEntitiesContainer.Num() + InOutEntityHandles.Num());
		for (const FMassRelationRoleInstanceHandle& Handle : RelationEntitiesContainer)
		{
			InOutEntityHandles.Add(Handle.GetRelationEntityHandle(EntityManager));
		}
	}

	TArray<FMassEntityHandle> FRelationManager::GetRelationEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer) const
	{
		TArray<FMassEntityHandle> EntityHandles;
		GetRelationEntities(RelationEntitiesContainer, EntityHandles);
		return EntityHandles;
	}

	void FRelationManager::GetRoleEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer, TArray<FMassEntityHandle>& InOutEntityHandles) const
	{
		// 把一组 handle 的"角色端"（即对手实体）解出。
		InOutEntityHandles.Reserve(RelationEntitiesContainer.Num() + InOutEntityHandles.Num());
		for (const FMassRelationRoleInstanceHandle& Handle : RelationEntitiesContainer)
		{
			InOutEntityHandles.Add(Handle.GetRoleEntityHandle(EntityManager));
		}
	}

	TArray<FMassEntityHandle> FRelationManager::GetRoleEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer) const
	{
		TArray<FMassEntityHandle> EntityHandles;
		GetRoleEntities(RelationEntitiesContainer, EntityHandles);
		return EntityHandles;
	}

	FRelationData& FRelationManager::CreateRelationData(const FTypeHandle RelationTypeHandle)
	{
		// 在 RelationsDataMap 中新建一个 FRelationData 桶，附带从 TypeManager 拉取的 traits。
		FRelationData* RelationData = RelationsDataMap.Find(RelationTypeHandle);
		if (ensureMsgf(RelationData == nullptr
			, TEXT("%hs: relation of type %s already registered"), __FUNCTION__, *RelationTypeHandle.GetFName().ToString()))
		{
			FRelationTypeTraits Traits = TypeManager.GetRelationTypeChecked(RelationTypeHandle);

			// @todo we should check every role's traits and see if RequiresExternalMapping == EExternalMappingRequired::No,
			// and if so, make sure all the other required bits are provided (like "other role getter").
			// 【官方 todo】当 RequiresExternalMapping=No 时需要校验用户是否提供了"对面角色的查找器"——
			// 否则后续 GetRelationSubjects/Objects 等 API 会拿不到数据。

			RelationData = &RelationsDataMap.Add(RelationTypeHandle, Traits);
		}

		return *RelationData;
	}

	bool FRelationManager::IsSubjectOfRelation(const FTypeHandle RelationTypeHandle, const FMassEntityHandle Subject, const FMassEntityHandle Object) const
	{
		check(RelationTypeHandle.IsValid());
		// 类型未注册时直接 false（不 checkf——这是合法的"未知关系类型"查询）。
		if (const FRelationData* RelationData = GetRelationData(RelationTypeHandle))
		{
			return IsSubjectOfRelation(*RelationData, Subject, Object);
		}
		return false;
	}

	bool FRelationManager::IsSubjectOfRelation(const FRelationData& RelationData, const FMassEntityHandle Subject, const FMassEntityHandle Object) const
	{
		SCOPE_CYCLE_COUNTER(STAT_Mass_IsSubject);

		// 提前过滤：自反关系、无效 entity 直接返回 false。
		if (Subject != Object
			&& Subject.IsValid()
			&& Object.IsValid())
		{
			// 在 Subject 的 RoleMap 条目中扫"它的 Object 们"，看是否包含目标 Object。
			// 这是 O(K) 线性扫描，K = 该 Subject 的关系数；对于稀疏关系图很快。
			if (const FRelationData::FRelationInstanceData* SubjectData = RelationData.RoleMap.Find(Subject))
			{
				for (const FMassRelationRoleInstanceHandle& RelationInstanceHandle : (*SubjectData)[static_cast<int32>(ERelationRole::Object)])
				{
					if (RelationInstanceHandle.GetRoleEntityHandle(EntityManager) == Object)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	bool FRelationManager::IsSubjectOfRelationRecursive(const FTypeHandle RelationTypeHandle, const FMassEntityHandle Subject, const FMassEntityHandle Object) const
	{
		check(RelationTypeHandle.IsValid());
		if (const FRelationData* RelationData = GetRelationData(RelationTypeHandle))
		{
			return IsSubjectOfRelationRecursive(*RelationData, Subject, Object);
		}
		return false;
	}

	bool FRelationManager::IsSubjectOfRelationRecursive(const FRelationData& RelationData, const FMassEntityHandle Subject, const FMassEntityHandle Object) const
	{
		// @todo the function can be optimized based on information whether Subject or Object is exclusive. If both are, it can be optimized even further.
		// for now naive implementation
		// 【BFS 实现】沿"Subject 找它的 Object 们 → 视作新 Subject 继续找它们的 Object 们..."一路搜索。
		//   语义：在 ChildOf 关系下，等价于"Object 是否是 Subject 的祖先（沿 child→parent 链）"。
		SCOPE_CYCLE_COUNTER(STAT_Mass_IsSubjectRecursive);

		if (Subject != Object
			&& Subject.IsValid()
			&& Object.IsValid())
		{
			TArray<FMassEntityHandle> Subjects;	// BFS 队列（边遍历边追加）
			TArray<FMassEntityHandle> Objects;	// 临时缓冲：当前节点的 Object 们
			Subjects.Add(Subject);
			
			// 安全保护：最多迭代 64 步——防止环或异常深的层级把 CPU 烧穿。
			constexpr int32 IterationsLimit = 64;
			for (int32 SubjectIndex = 0; SubjectIndex < Subjects.Num() && ensure(SubjectIndex < IterationsLimit); ++SubjectIndex)
			{
				if (const FRelationData::FRelationInstanceData* SubjectData = RelationData.RoleMap.Find(Subjects[SubjectIndex]))
				{
					// 取出当前节点的所有 Object 端实体。
					GetRoleEntities((*SubjectData)[static_cast<int32>(ERelationRole::Object)], Objects);

					// 命中？立即返回。
					if (Objects.Find(Object) != INDEX_NONE)
					{
						return true;
					}

					// 否则把这些 Object 入队作为新的 Subject 继续向上扩展。
					// AddUnique 防止同一 entity 被反复入队（环检测）。
					for (const FMassEntityHandle& ObjectHandle : Objects)
					{
						Subjects.AddUnique(ObjectHandle);
					}
					Objects.Reset();
				}
			}
		}

		return false;
	}

	void FRelationManager::OnRelationTypeRegistered(const FTypeHandle InRegisteredTypeHandle, const FRelationTypeTraits& RelationTypeTraits)
	{
		// 步骤1：创建 FRelationData 桶。
		FRelationData& RelationData = CreateRelationData(InRegisteredTypeHandle);
		// 步骤2：层级关系必须有合法的 group type（在 TypeManager.RegisterType 中已分配）。
		ensureMsgf(RelationData.Traits.bHierarchical == false || RelationData.Traits.RegisteredGroupType.IsValid()
			, TEXT("Hierarchical relationships need a valid registered group size. Failed by relationship %s")
			, *RelationData.Traits.GetFName().ToString());

		// 步骤3：收集要创建的 observer class 列表。
		// 默认 3 个：关系实体创建/销毁 + GuardDog（监视关系实体的有效性）。
		TArray<TSubclassOf<UMassRelationObserver>> ObserversToCreate = 
		{
			RelationTypeTraits.RelationEntityCreationObserverClass.Get()
			, RelationTypeTraits.RelationEntityDestructionObserverClass.Get()
			, UMassRelationEntityGuardDog::StaticClass()
		};

		// 步骤4：Subject/Object 端的销毁 observer 处理。
		// 如果两端都使用默认的 UMassRelationRoleDestruction，走优化路径——共享 observer 实例。
		// 否则各自创建独立的 observer 处理（用户可能为两端配置了不同的 ERemovalPolicy）。
		if (RelationTypeTraits.SubjectEntityDestructionObserverClass.Get() == UMassRelationRoleDestruction::StaticClass()
			&& RelationTypeTraits.ObjectEntityDestructionObserverClass.Get() == UMassRelationRoleDestruction::StaticClass())
		{
			UMassRelationRoleDestruction::AddObserverInstances(EntityManager.GetObserverManager(), InRegisteredTypeHandle, RelationData.Traits);
		}
		else
		{
			ObserversToCreate.Add(RelationTypeTraits.SubjectEntityDestructionObserverClass.Get());
			ObserversToCreate.Add(RelationTypeTraits.ObjectEntityDestructionObserverClass.Get());
		}

		// 步骤5：实例化每个 observer 并注册到 ObserverManager。
		for (const TSubclassOf<UMassRelationObserver>& ObserverClass : ObserversToCreate)
		{
			if (ObserverClass)
			{
				// NewObject 时使用 EntityManager.GetOwner() 作为 outer——通常是 GameInstance/UWorld 子系统。
				UMassRelationObserver* ObserverProcessor = NewObject<UMassRelationObserver>(EntityManager.GetOwner(), ObserverClass);
				// ConfigureRelationObserver 让 observer 据 traits 决定监听哪种 fragment/tag、用何种 policy。
				// 返回 false 表示该 observer 不适用此关系（例如某些 traits 组合下不需要它），跳过注册。
				if (ObserverProcessor->ConfigureRelationObserver(InRegisteredTypeHandle, RelationData.Traits))
				{
					EntityManager.GetObserverManager().AddObserverInstance(ObserverProcessor);
				}
			}
		}
	}
} // namespace UE::Mass

#undef WITH_RELATIONSHIP_VALIDATION
