// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a，ChildOf 关系实现）：
//   * ChildOfHandle 全局变量定义
//   * FChildOfRelationTypeInitializer：单例 RAII 类，负责把 ChildOf 关系挂到 TypeManager 注册委托上
//   * RegisterChildOfRelation()：模块启动时调用一次，触发上述单例的构造
//   * UMassChildOfRelationEntityCreation::Execute：关系实体创建时把 Parent 写入子的 fragment
// =============================================================================

#include "Relations/MassChildOf.h"
#include "MassTypeManager.h"
#include "MassEntityManager.h"
#include "Misc/CoreDelegates.h"
#include "MassExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassChildOf)

namespace UE::Mass
{
	namespace Relations
	{
		/** 中文：ChildOf 关系类型的全局句柄。在 FChildOfRelationTypeInitializer 构造函数中初始化。 */
		FTypeHandle ChildOfHandle;

		/**
		 * 中文：RAII 风格的关系类型初始化器。
		 *   * 构造时：
		 *       1) 把 RegisterType 静态方法挂到 FTypeManager::OnRegisterBuiltInTypes 委托。
		 *          每当 FTypeManager 完成初始化时，会广播此委托——给所有"内置类型"一次注册机会。
		 *       2) 立刻通过 FTypeManager::MakeTypeHandle 把 FMassChildOfRelation 反射结构映射成 ChildOfHandle。
		 *          注意：MakeTypeHandle 不需要 EntityManager 实例，仅基于 StaticStruct 计算句柄。
		 *   * 析构时：从委托上移除自身——保证模块卸载时不会留悬挂指针。
		 *   * 单例使用：通过 RegisterChildOfRelation() 中的 static 局部变量保证全局唯一。
		 */
		struct FChildOfRelationTypeInitializer
		{
			FChildOfRelationTypeInitializer()
				: RegisterBuiltInTypesHandle(FTypeManager::OnRegisterBuiltInTypes.AddStatic(&FChildOfRelationTypeInitializer::RegisterType))
			{
				UE::Mass::Relations::ChildOfHandle = FTypeManager::MakeTypeHandle(FMassChildOfRelation::StaticStruct());
			}

			~FChildOfRelationTypeInitializer()
			{
				FTypeManager::OnRegisterBuiltInTypes.Remove(RegisterBuiltInTypesHandle);
			}

			/**
			 * 中文：实际向 TypeManager 注册 ChildOf 关系类型的回调。
			 *   * 由 FTypeManager 在 EntityManager 初始化时回调（通过 OnRegisterBuiltInTypes 委托）。
			 *   * 步骤：
			 *       1) 用 FMassChildOfRelation::StaticStruct() 构造一个 FRelationTypeTraits（关系类型 traits）。
			 *       2) 配置 Object 端（=父端）：
			 *           - bExclusive = true ── 一个父实体可以有多个子，但每个子-父关系是独立的；
			 *             这里 Exclusive 是从"Object 角度"看，含义参考 FRelationTypeTraits 的设计文档。
			 *           - DestructionPolicy = Destroy ── 父被销毁时连带销毁所有子（级联）。
			 *       3) 配置 Subject 端（=子端）：
			 *           - bExclusive = false ── 子可以被多个父引用？
			 *             ⚠️ 这看起来与"父子树"的常识不一致。可能本字段在 Subject 端的语义为
			 *             "一个 child 是否对其他父唯一"——实际由 RelationManager 解释。@todo 验证语义。
			 *           - DestructionPolicy = CleanUp ── 子被销毁时仅清理与之相连的 ChildOf 关系实体，不影响父。
			 *           - Element = FMassChildOfFragment::StaticStruct() ── 子端独有的 fragment（带 Parent 字段）。
			 *       4) bHierarchical = true ── 标记为层级关系，让 RelationManager 启用层级专属优化（如 archetype 分组）。
			 *       5) RelationName = "ChildOf" ── 用于调试日志、UI 展示。
			 *       6) RegisteredGroupType = FindOrAddArchetypeGroupType("ChildOf") ── 申请一个 ArchetypeGroupType
			 *          作为 archetype 分组键；后续按父分组遍历子时使用此键。
			 *       7) RelationEntityCreationObserverClass = UMassChildOfRelationEntityCreation::StaticClass()
			 *          ── 关系实体创建时使用的 observer（其他 observer 走默认）。
			 *       8) InOutTypeManager.RegisterType(MoveTemp(traits)) ── 完成注册。
			 */
			static void RegisterType(FTypeManager& InOutTypeManager)
			{
				FRelationTypeTraits ChildOfRelation(FMassChildOfRelation::StaticStruct());
				ChildOfRelation.RoleTraits[static_cast<int32>(ERelationRole::Object)].bExclusive = true;
				ChildOfRelation.RoleTraits[static_cast<int32>(ERelationRole::Object)].DestructionPolicy = ERemovalPolicy::Destroy;

				ChildOfRelation.RoleTraits[static_cast<int32>(ERelationRole::Subject)].bExclusive = false;
				ChildOfRelation.RoleTraits[static_cast<int32>(ERelationRole::Subject)].DestructionPolicy = ERemovalPolicy::CleanUp;
				// @todo unused for now.
				// 中文备注：当前 Element 字段在 Subject 端登记了 FMassChildOfFragment，但 todo 标注"暂未使用"——
				//          意味着 Mass 当前实现可能还没把"角色端独有 fragment"的自动 add/remove 机制完整实现。
				ChildOfRelation.RoleTraits[static_cast<int32>(ERelationRole::Subject)].Element = FMassChildOfFragment::StaticStruct();

				ChildOfRelation.bHierarchical = true;
				ChildOfRelation.RelationName = "ChildOf";
				
				// 中文：取得或新建一个 archetype 分组类型——用于按"父"对 archetype 做高效分组。
				ChildOfRelation.RegisteredGroupType = InOutTypeManager.GetEntityManager().FindOrAddArchetypeGroupType(ChildOfRelation.RelationName);

				// 中文：绑定本关系的"创建 observer"为 UMassChildOfRelationEntityCreation。
				ChildOfRelation.RelationEntityCreationObserverClass = UMassChildOfRelationEntityCreation::StaticClass();

				InOutTypeManager.RegisterType(MoveTemp(ChildOfRelation));
			}

			/** 中文：保存委托句柄，析构时用于精确移除自己。 */
			const FDelegateHandle RegisterBuiltInTypesHandle;
		};

		/**
		 * 中文：模块启动接口。
		 *   * static 局部变量保证 FChildOfRelationTypeInitializer 仅构造一次（C++11 起线程安全）。
		 *   * 构造副作用：注册到 OnRegisterBuiltInTypes 委托 + 计算 ChildOfHandle。
		 *   * 多次调用是幂等的（除了第一次外，无任何副作用）。
		 *   * 调用时机：使用 ChildOf 关系的模块在 StartupModule 中调用一次。
		 */
		void RegisterChildOfRelation()
		{
			static FChildOfRelationTypeInitializer BuildInRelationHandlesInitializer;
		}
	}
}

//-----------------------------------------------------------------------------
// UMassRelationEntityCreation
// 中文备注：注释行写的是 UMassRelationEntityCreation，但下面实现的是
//          UMassChildOfRelationEntityCreation——疑似复制注释时的笔误。
//-----------------------------------------------------------------------------

/**
 * 中文：ChildOf 关系实体被创建时的回调——把 Parent 写入子实体的 FMassChildOfFragment。
 *
 * 算法：
 *   1) 第一阶段：遍历本批次新建的关系实体，从其 RelationFragment 中读出 (Subject=child, Object=parent)
 *      与关系实体本身的 EntityHandle，存入临时数组 RelationInstances。
 *      — 之所以分两阶段，是因为接下来要 GetFragmentDataStruct(child, ...) 这是对 *其他* archetype 的
 *        随机访问，不能在 ForEachEntityChunk 的迭代过程中做（迭代器在锁定 archetype 上）。
 *   2) 第二阶段：对每个三元组，用 EntityManager.GetFragmentDataStruct<FMassChildOfFragment>(child) 拿到
 *      子实体的 fragment，把 Parent 字段填上。ensure 校验 fragment 必须存在——子端没有 ChildOfFragment 是 bug。
 *
 * 设计注释（@todo）：
 *   * "if we don't do mapping how do we 'remove relation', how do we find the relation entity?"
 *     —— 当前 ChildOfFragment.Parent 只存了"父 handle"，没存"关系实体 handle"。如果业务想"主动断绝
 *        某个父子关系"，得先按 Subject/Object 反查 RelationManager.RoleMap，再 DestroyEntity 关系实体。
 *        这是设计权衡：fragment 越小越好（cache 友好），代价是反向查询走 RoleMap。
 *
 *   * "we need 'external requirement' meaning 'things we touch on other entities'"
 *     —— 当前 query 系统不能优雅地表达"我会读 child 上的 fragment，但 child 不在我的 query archetype 里"。
 *        这种"跨实体写"目前用 GetFragmentDataStruct 直接做，但缺乏调度器层面的依赖求解保护。
 *
 *   * "use the fragment type from traits, not hardcoded FMassChildOfFragment"
 *     —— 这里硬编码了 FMassChildOfFragment::StaticStruct()。如果业务方派生了 FMyChildOfFragment 并复用
 *        本 observer，会取错 fragment。建议从 RelationData.Traits.RoleTraits[Subject].Element 拿。
 */
void UMassChildOfRelationEntityCreation::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	struct FRelationInstanceRegistration
	{
		FMassEntityHandle ChildHandle;
		FMassEntityHandle ParentHandle;
		FMassEntityHandle RelationEntityHandle;
	};

	TArray<FRelationInstanceRegistration> RelationInstances;
	// 中文：阶段 1 —— 收集本批次新建关系的 (child, parent, relation_entity) 三元组。
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
	// @todo if we don't do mapping how do we "remove relation", how do we find the relation entity?
	// @todo, again, we need "external requirement" meaning "things we touch on other entities, so needs
	// to be accounted for in dependencies, but we're not binding or expecting them to be in the very archetypes we process"
	// 中文：阶段 2 —— 把 Parent 字段填到每个子实体的 ChildOfFragment 上。
	for (const FRelationInstanceRegistration& RelationInstance : RelationInstances)
	{
		// using GetFragmentDataStruct to support this observer processor being used by relations that extend the ChildOf
		// relation by using a fragment deriving from FMassChildOfFragment
		// @todo use the fragment type from traits, not hardcoded FMassChildOfFragment
		// 中文：用 FStructView 形式访问，便于支持派生 fragment（继承 FMassChildOfFragment 的子类）。
		//      仍然存在硬编码 StaticStruct() 的限制——见上面的 @todo。
		FStructView FragmentView = EntityManager.GetFragmentDataStruct(RelationInstance.ChildHandle, FMassChildOfFragment::StaticStruct());
		FMassChildOfFragment* ChildOfFragment = FragmentView.GetPtr<FMassChildOfFragment>();
		if (ensure(ChildOfFragment))
		{
			ChildOfFragment->Parent = RelationInstance.ParentHandle;
		}
	}
}
