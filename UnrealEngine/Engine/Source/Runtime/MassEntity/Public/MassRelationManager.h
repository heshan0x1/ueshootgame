// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "MassTypeManager.h"
#include "MassEntityHandle.h"
#include "MassEntityRelations.h"

#define UE_API MASSENTITY_API

// =============================================================================
// 【模块定位 - FRelationManager（关系管理器）】
// -----------------------------------------------------------------------------
// 这是 Mass 关系系统的"运行时执行层"——把"关系即实体"思想变成可用的查询接口。
//
// 【数据结构层级】
//
//   FRelationManager
//   ├─ TMap<FTypeHandle, FRelationData> RelationsDataMap     ← 每种 relation 类型一份数据
//   │
//   └─ FRelationData （per-relation-type）
//      ├─ const FRelationTypeTraits Traits                    ← 该关系的配置（来自 TypeManager）
//      └─ TMap<FMassEntityHandle, FRelationInstanceData> RoleMap ← 角色端实体 → 它参与的关系列表
//         │
//         └─ FRelationInstanceData
//            └─ TStaticArray<TArray<FMassRelationRoleInstanceHandle>, MAX_ROLE>
//                 [Subject] = [关系1, 关系3, ...]   // 我作为 Subject 参与的关系
//                 [Object]  = [关系2, ...]          // 我作为 Object 参与的关系
//
// 【为什么这样设计】
//   - 按 relation type 分桶：避免不同关系类型混在一起，查询时不需要扫描；
//   - 按 entity 分桶：给定一个 entity 找它参与的所有该类型关系是 O(1)；
//   - 按 role 分桶：区分"我作为 parent 的关系"vs"我作为 child 的关系"——
//     这是层级遍历（GetRelationSubjectsRecursive 找所有 ancestors）的关键。
//
// 【典型使用流程】
//   1. EntityManager.GetTypeManager().RegisterType(FRelationTypeTraits(FChildOf::StaticStruct()))
//      → TypeManager 在 built-in 阶段结束后调用 FRelationManager::OnRelationTypeRegistered
//      → RelationManager 在 RelationsDataMap 中创建 FRelationData 桶
//      → 同时创建 destruction observer 监听 entity 销毁。
//
//   2. 业务侧调用 RelationManager.CreateRelationInstance<FChildOf>(ChildEntity, ParentEntity)：
//      → 创建一个新的 RelationEntity，挂上 FMassRelationFragment{Subject=Child, Object=Parent}；
//      → 在 ChildEntity 的 RoleMap[ChildEntity][Object] 列表里加上这个关系（"我以 Subject 身份关联 Parent"）；
//      → 在 ParentEntity 的 RoleMap[ParentEntity][Subject] 列表里也加上（"我以 Object 身份被 Child 关联"）；
//      → 在两个角色端 entity 上加 Element fragment（如有配置）。
//
//   3. 销毁某个 entity 时：
//      → destruction observer 检查它在所有 relation 类型的 RoleMap 中的条目；
//      → 按 ERemovalPolicy 处理（CleanUp / Destroy / Splice / Custom）。
//
// 【关键操作复杂度】
//   - 创建关系：O(1)（不算 entity 创建本身）
//   - 查询直接 subjects/objects：O(K) K=该 entity 参与的关系数
//   - 递归向上遍历层级：O(N) N=祖先链长度，有 64 深度上限
// =============================================================================

struct FMassEntityHandle;
struct FMassEntityManager;
struct FMassExecutionContext;

namespace UE::Mass
{
	struct FTypeRegistry;
	struct FRelationManager;

	/**
	 * 【FRelationData】单个 relation 类型的所有运行时数据。
	 *   生命周期：在 FRelationManager::OnRelationTypeRegistered 中创建，与 RelationManager 同生共死。
	 */
	struct FRelationData
	{
		/** 构造：把 traits 拷一份保存。Traits 是 const 的——一旦关系类型注册就不能修改。 */
		FRelationData(const FRelationTypeTraits& InTraits);

		/** 该关系的 traits 快照（来自 TypeManager）。const 防止运行时被改。 */
		const FRelationTypeTraits Traits;

		/**
		 * 【FRelationInstanceData】描述"某个 entity 参与的所有该类型关系"。
		 *   按 ERelationRole 分两个数组：
		 *     [Subject] = 我作为 Subject 端时参与的 RelationRoleInstanceHandle 列表；
		 *     [Object]  = 我作为 Object  端时参与的 RelationRoleInstanceHandle 列表。
		 *
		 *   注意命名细节：handle.GetRoleEntityIndex() 返回的不是"我"，而是"另一端"！
		 *   即：在 RoleMap[me][Subject] 中的每个 handle，GetRoleEntityIndex() 是把"我"当 Object 的那个 Subject。
		 *   这样 GetRoleEntities() 就能直接返回"对面"的实体列表。
		 */
		struct FRelationInstanceData
		{
			/** 大小固定为 ERelationRole::MAX (=2) 的 TArray 数组：每个 role 对应一个动态列表。 */
			TStaticArray<TArray<FMassRelationRoleInstanceHandle>, static_cast<uint32>(ERelationRole::MAX)> RelationEntityContainers;

			/** 是否两个 role 的列表都为空——RelationManager 销毁时用作"是否可清理本条目"的判据。 */
			bool IsEmpty() const
			{
				bool bIsEmpty = true;
				for (const TArray<FMassRelationRoleInstanceHandle>& Container : RelationEntityContainers)
				{
					bIsEmpty = bIsEmpty && Container.IsEmpty();
				}
				return bIsEmpty;
			}

			/** 整数索引访问（0=Subject,1=Object）。 */
			TArray<FMassRelationRoleInstanceHandle>& operator[](const int32 Index)
			{
				return RelationEntityContainers[Index];
			}

			const TArray<FMassRelationRoleInstanceHandle>& operator[](const int32 Index) const
			{
				return RelationEntityContainers[Index];
			}

			/** 枚举索引访问——更具语义。 */
			TArray<FMassRelationRoleInstanceHandle>& operator[](const ERelationRole Index)
			{
				return (*this)[static_cast<int32>(Index)];
			}

			const TArray<FMassRelationRoleInstanceHandle>& operator[](const ERelationRole Index) const
			{
				return (*this)[static_cast<int32>(Index)];
			}
		};

		/** 
		 * 【主索引】角色端 entity → 它参与的所有该类型关系（按 role 分组）。
		 *   插入时机：每次 CreateRelationInstance 同时给 Subject、Object 两端分别 FindOrAdd。
		 *   删除时机：destruction observer 在关系实体销毁时清理。
		 */
		TMap<FMassEntityHandle, FRelationInstanceData> RoleMap;

		/**
		 * 查询 RoleEntity 在该关系类型中"扮演 QueriedRole 的所有对手"。
		 * @param EntityManager 用于把 30-bit Index 还原成完整 EntityHandle
		 * @param RoleEntity    被查询的实体
		 * @param QueriedRole   想要找"扮演此 role"的对手列表（Subject/Object）
		 * @return 所有对手 entity handle 数组
		 *
		 * 例：A.ChildOf B（Subject=A, Object=B）
		 *     - GetParticipants(A, Object) → [B]   （A 的"Object 们" = A 的 parent）
		 *     - GetParticipants(B, Subject) → [A]  （B 的"Subject 们" = B 的 children）
		 *
		 * 注意：若 RoleTraits[QueriedRole].RequiresExternalMapping == No，则 RoleMap 不维护该 role，
		 *       此函数会 ensure 失败（提示用户该 role 走的是外部映射路径）。
		 */
		TArray<FMassEntityHandle> GetParticipants(const FMassEntityManager& EntityManager, const FMassEntityHandle RoleEntity, ERelationRole QueriedRole) const;
	};


	/**
	 * 【FRelationManager】关系系统的"门面"——业务侧大多通过它操作关系。
	 *   一个 EntityManager 持有一个 FRelationManager，反向持 EntityManager 引用 + TypeManager 常引用。
	 */
	struct FRelationManager
	{
		/** 构造：从 EntityManager 拿到 TypeManager 引用。RelationManager 与 EntityManager 同生命周期。 */
		UE_API explicit FRelationManager(FMassEntityManager& EntityManager);

		/**
		 * 模板糖：创建 1 对 1 的关系实例。
		 * @tparam T  必须派生自 FMassRelation 的 tag 类型
		 * @return    新创建的关系实体 handle，失败时为空 handle
		 */
		template<UE::Mass::CRelation T>
		FMassEntityHandle CreateRelationInstance(FMassEntityHandle Subject, FMassEntityHandle Object);
		/** 同上，但接受运行时已知的 RelationTypeHandle。 */
		FMassEntityHandle CreateRelationInstance(const FTypeHandle RelationTypeHandle, FMassEntityHandle Subject, FMassEntityHandle Object);

		/** 
		 * Creates a relation type handle with RelationType, and calls the other CreateRelationInstances implementation 
		 * 批量创建关系；通过 UScriptStruct* 自动转 FTypeHandle 后委托给底层版本。
		 */
		TArray<FMassEntityHandle> CreateRelationInstances(TNotNull<const UScriptStruct*> RelationType, TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects);

		/**
		 * Creates valid relation instances of type RelationTypeHandle, binding Subjects and Objects
		 * Note that the input arrays can have their order modified by the function, all the relation pairs that are not valid, are moved to the back of the arrays
		 * The number of elements in Subjects and Objects must match.
		 *
		 * 批量创建关系实例。本函数会就地修改 Subjects/Objects 数组顺序：
		 *   把无效配对（subject==object、entity 无效、关系已存在等）swap 到末尾再忽略。
		 * 配对规则：Subjects[i] 与 Objects[i] 一一对应；若一边大小为 1，会自动广播为 1-to-many。
		 */
		UE_API TArray<FMassEntityHandle> CreateRelationInstances(const FTypeHandle RelationTypeHandle, TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects);

		/** 
		 * 销毁 (Subject, Object) 之间的关系实例（如果存在）。
		 * @return 是否找到并销毁了关系
		 */
		UE_API bool DestroyRelationInstance(const FTypeHandle RelationTypeHandle, const FMassEntityHandle Subject, const FMassEntityHandle Object);
		/** 通过 handle 直接销毁关系实体；不需要先查 RoleMap。 */
		UE_API bool DestroyRelationInstance(FMassRelationRoleInstanceHandle RelationHandle) const;

		/** 
		 * Fetch all the entities that are "subjects" in instances of the given relation type, where ObjectEntity is the "object" of the relation 
		 * 给定 ObjectEntity，返回所有以它为 Object 的关系中那些 Subject 实体。
		 * 例：ObjectEntity = parent → 返回它所有的 children。
		 */
		[[nodiscard]] TArray<FMassEntityHandle> GetRelationSubjects(TNotNull<const UScriptStruct*> RelationType, const FMassEntityHandle ObjectEntity) const;
		[[nodiscard]] TArray<FMassEntityHandle> GetRelationSubjects(const FTypeHandle RelationTypeHandle, const FMassEntityHandle ObjectEntity) const;

		/** 
		 * Fetch all the entities that are "objects" in instances of the given relation type, where SubjectEntity is the "subject" of the relation 
		 * 给定 SubjectEntity，返回所有它作为 Subject 的关系中那些 Object 实体。
		 * 例：SubjectEntity = child → 返回它所有的 parent（一般是 1 个）。
		 */
		[[nodiscard]] TArray<FMassEntityHandle> GetRelationObjects(TNotNull<const UScriptStruct*> RelationType, const FMassEntityHandle SubjectEntity) const;
		[[nodiscard]] TArray<FMassEntityHandle> GetRelationObjects(const FTypeHandle RelationTypeHandle, const FMassEntityHandle SubjectEntity) const;

		/** 把一组 RoleInstanceHandle 的"关系实体"端 EntityHandle 都取出来。 */
		[[nodiscard]] TArray<FMassEntityHandle> GetRelationEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer) const;
		void GetRelationEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer, TArray<FMassEntityHandle>& InOutEntityHandles) const;
		/** 把一组 RoleInstanceHandle 的"角色端"（即对面）EntityHandle 都取出来。 */
		[[nodiscard]] TArray<FMassEntityHandle> GetRoleEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer) const;
		void GetRoleEntities(TConstArrayView<FMassRelationRoleInstanceHandle> RelationEntitiesContainer, TArray<FMassEntityHandle>& InOutEntityHandles) const;

		/** 把 UScriptStruct* 转成 FTypeHandle；委托给 TypeManager。 */
		FTypeHandle GetRelationTypeHandle(TNotNull<const UScriptStruct*> RelationType) const;

		/** 
		 * 直接判断 (Subject, Object) 是否存在该类型关系实例。
		 * 注意"语义方向"：检查的是"Subject 是否以 Subject 身份关联到 Object"，不是反向。
		 */
		UE_API bool IsSubjectOfRelation(const FTypeHandle RelationTypeHandle, const FMassEntityHandle Subject, const FMassEntityHandle Object) const;
		UE_API bool IsSubjectOfRelation(const FRelationData& RelationDataInstance, const FMassEntityHandle Subject, const FMassEntityHandle Object) const;
		/** 
		 * 递归版：是否能从 Subject 沿"作为 Subject → 找其 Object"链一路走到 Object。
		 * 用例：检测 ChildOf 链中 A 是否是 B 的 ancestor 的 ancestor 的...
		 * 内部 BFS 实现，有 64 步迭代上限保护防止环。
		 */
		UE_API bool IsSubjectOfRelationRecursive(const FTypeHandle RelationTypeHandle, const FMassEntityHandle Subject, const FMassEntityHandle Object) const;
		UE_API bool IsSubjectOfRelationRecursive(const FRelationData& RelationDataInstance, const FMassEntityHandle Subject, const FMassEntityHandle Object) const;


		/**
		 * 【关键 hook】当 TypeManager 注册了一个新 relation 类型时被调用。
		 *   - 在 RelationsDataMap 中创建 FRelationData 桶；
		 *   - 创建 4 个 destruction observer（关系实体销毁、Subject 销毁、Object 销毁、guard dog）
		 *     并注册到 EntityManager 的 ObserverManager。
		 */
		void OnRelationTypeRegistered(const FTypeHandle RegisteredTypeHandle, const FRelationTypeTraits& RelationTypeTraits);

		const FRelationData& GetRelationDataChecked(const FTypeHandle RelationTypeHandle) const;
		FRelationData& GetRelationDataChecked(const FTypeHandle RelationTypeHandle);

	protected:

		/** OnRelationTypeRegistered 的实现细节：在 RelationsDataMap 中 Add 一个新桶。 */
		FRelationData& CreateRelationData(const FTypeHandle RelationTypeHandle);

		/** 实际的 GetRelationObjects 实现——按"我作为 Subject 时关联的 Object"查询。 */
		UE_API TArray<FMassEntityHandle> GetRelationObjects(const FRelationData& RelationData, const FMassEntityHandle SubjectEntity) const;
		/** 实际的 GetRelationSubjects 实现——按"我作为 Object 时关联的 Subject"查询。 */
		UE_API TArray<FMassEntityHandle> GetRelationSubjects(const FRelationData& RelationData, const FMassEntityHandle ObjectEntity) const;

		/** 安全版：未注册的 relation 类型返回 null。 */
		const FRelationData* GetRelationData(const FTypeHandle RelationTypeHandle) const;

		/**
		 * 【FHierarchyEntitiesContainer】层级遍历的辅助容器。
		 *   关键作用：
		 *     1. 按"深度"分桶：ContainerPerLevel[depth] = 该深度的实体列表；
		 *     2. 全局去重：ExistingElements 集合保证同一 entity 不重复存储——
		 *        即使关系图中存在环或菱形继承，遍历也能终止。
		 *
		 *   用于 CreateRelationInstances 在 bHierarchical 关系下计算每层 archetype group。
		 */
		struct FHierarchyEntitiesContainer
		{
			/**
			 * 把一批 handle 加入指定深度层；自动去重（已存在的会从 InOutArray 中 RemoveAtSwap）。
			 * @param Depth 目标深度
			 * @param InOutArray 输入数组——会被就地修改（去掉重复的）
			 */
			void StoreUnique(const uint32 Depth, TArray<FMassEntityHandle>& InOutArray);
			/** 单元素版本。 */
			void StoreUnique(const uint32 Depth, FMassEntityHandle Handle);

			TArray<FMassEntityHandle>& operator[](const uint32 Depth)
			{
				return ContainerPerLevel[Depth];
			}
			const TArray<FMassEntityHandle>& operator[](const uint32 Depth) const
			{
				return ContainerPerLevel[Depth];
			}

			uint32 Num() const
			{
				return static_cast<uint32>(ContainerPerLevel.Num());
			}
		protected:
			/** 按深度索引的实体数组：ContainerPerLevel[d] 是深度 d 的实体列表。 */
			TArray<TArray<FMassEntityHandle>> ContainerPerLevel;
			/** 全局去重集合——保证一个 entity 只出现在一个深度层（防环）。 */
			TSet<FMassEntityHandle> ExistingElements;
		};

		/**
		 * 【递归层级收集】从 SubjectHandle 出发，沿"作为 Subject 找它的 Object 们"
		 *   的方向递归向下（注意：在 ChildOf 关系里这意味着向上爬到 ancestors）。
		 *   每一层把找到的实体放进 SubSubjects 的 [Depth] 槽。
		 *   去重由 FHierarchyEntitiesContainer 处理，因此即使有环也能终止。
		 *
		 *   命名解释：函数叫 "GatherHierarchy" 但内部"对手"用 GetRelationSubjects——
		 *   这是因为在层级关系中，调用方传入的"SubjectHandle"实际上是当前要扩展的节点，
		 *   而它的"Subjects"（以它为 Object 的那些 entity）才是要展开的子树。
		 */
		void GatherHierarchy(const FRelationData& RelationData, const FMassEntityHandle SubjectHandle, FHierarchyEntitiesContainer& SubSubjects, uint32 Depth = 0) const;
		
		/** 所属 EntityManager（构造时绑定）。RelationManager 是 EntityManager 的内嵌成员。 */
		FMassEntityManager& EntityManager;
		/** TypeManager 的 const 引用——RelationManager 只读，不能注册新类型。 */
		const FTypeManager& TypeManager;

		/** 主存储：每个 relation 类型一份数据。 */
		TMap<FTypeHandle, FRelationData> RelationsDataMap;
	};

	//-----------------------------------------------------------------------------
	// INLINES
	//-----------------------------------------------------------------------------
	template<UE::Mass::CRelation T>
	FMassEntityHandle FRelationManager::CreateRelationInstance(FMassEntityHandle Subject, FMassEntityHandle Object)
	{
		// 1-对-1 创建：把单个 handle 包成 ArrayView 走批量路径，再取首元素。
		TArray<FMassEntityHandle> CreatedRelationshipEntities = CreateRelationInstances(T::StaticStruct(), TArrayView<FMassEntityHandle>(&Subject, 1), TArrayView<FMassEntityHandle>(&Object, 1));
		return CreatedRelationshipEntities.IsEmpty() ? FMassEntityHandle() : CreatedRelationshipEntities[0];
	}

	inline FMassEntityHandle FRelationManager::CreateRelationInstance(const FTypeHandle RelationTypeHandle, FMassEntityHandle Subject, FMassEntityHandle Object)
	{
		// 同上，handle 版。
		TArray<FMassEntityHandle> CreatedRelationshipEntities = CreateRelationInstances(RelationTypeHandle, TArrayView<FMassEntityHandle>(&Subject, 1), TArrayView<FMassEntityHandle>(&Object, 1));
		return CreatedRelationshipEntities.IsEmpty() ? FMassEntityHandle() : CreatedRelationshipEntities[0];
	}

	inline TArray<FMassEntityHandle> FRelationManager::CreateRelationInstances(TNotNull<const UScriptStruct*> RelationType, TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects)
	{
		// 把 UScriptStruct* 通过 TypeManager 转成 FTypeHandle；未注册时早返回。
		const FTypeHandle RelationTypeHandle = TypeManager.GetRelationTypeHandle(RelationType);
		if (!ensureMsgf(RelationTypeHandle.IsValid(), TEXT("%hs: Unknown relation type %s, make sure to register it first"), __FUNCTION__, *RelationType->GetName()))
		{
			return {};
		}

		return CreateRelationInstances(RelationTypeHandle, Subjects, Objects);
	}

	inline FTypeHandle FRelationManager::GetRelationTypeHandle(TNotNull<const UScriptStruct*> RelationType) const
	{
		// 校验是 FMassRelation 子类（编译期+反射），然后委托给 TypeManager。
		checkf(UE::Mass::IsA<FMassRelation>(RelationType)
			, TEXT("Provided RelationType, %s, is not a relation type")
			, *RelationType->GetName());

		return TypeManager.GetRelationTypeHandle(RelationType);
	}
	
	inline const FRelationData* FRelationManager::GetRelationData(const FTypeHandle RelationTypeHandle) const
	{
		return RelationsDataMap.Find(RelationTypeHandle);
	}

	inline const FRelationData& FRelationManager::GetRelationDataChecked(const FTypeHandle RelationTypeHandle) const
	{
		// FindChecked：未注册时直接 check 失败——调用方必须先确认类型已注册。
		return RelationsDataMap.FindChecked(RelationTypeHandle);
	}

	inline FRelationData& FRelationManager::GetRelationDataChecked(const FTypeHandle RelationTypeHandle)
	{
		return RelationsDataMap.FindChecked(RelationTypeHandle);
	}

	inline TArray<FMassEntityHandle> FRelationManager::GetRelationSubjects(TNotNull<const UScriptStruct*> RelationType, const FMassEntityHandle ObjectEntity) const
	{
		const FTypeHandle RelationTypeHandle = TypeManager.GetRelationTypeHandle(RelationType);
		return GetRelationSubjects(RelationTypeHandle, ObjectEntity);
	}

	inline TArray<FMassEntityHandle> FRelationManager::GetRelationSubjects(const FTypeHandle RelationTypeHandle, const FMassEntityHandle ObjectEntity) const
	{
		const FRelationData& RelationData = GetRelationDataChecked(RelationTypeHandle);
		return GetRelationSubjects(RelationData, ObjectEntity);
	}

	inline TArray<FMassEntityHandle> FRelationManager::GetRelationObjects(TNotNull<const UScriptStruct*> RelationType, const FMassEntityHandle SubjectEntity) const
	{
		const FTypeHandle RelationTypeHandle = TypeManager.GetRelationTypeHandle(RelationType);
		return GetRelationObjects(RelationTypeHandle, SubjectEntity);
	}

	inline TArray<FMassEntityHandle> FRelationManager::GetRelationObjects(const FTypeHandle RelationTypeHandle, const FMassEntityHandle SubjectEntity) const
	{
		const FRelationData& RelationData = GetRelationDataChecked(RelationTypeHandle);
		return GetRelationObjects(RelationData, SubjectEntity);
	}
} // namespace UE::Mass

#undef UE_API
