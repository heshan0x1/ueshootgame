// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a，Mass 内置 ChildOf 关系——也是用户实现自定义关系的"参考实现"）
// -----------------------------------------------------------------------------
// ChildOf 是 Mass 框架自带的"父子关系"演示。它展示了实现一个新关系类型需要的最少零件：
//
//   1) 关系 Tag         ── struct FMassChildOfRelation : FMassRelation
//        * 仅作类型标识，不带数据；ChildOf 关系实体上必带此 tag。
//
//   2) 角色端 Fragment   ── struct FMassChildOfFragment : FMassFragment { FMassEntityHandle Parent; }
//        * 挂在"子实体"上，记录其父引用，便于 O(1) 反向查询父。
//        * 这是 Subject 端的 Element（在 traits 注册时填到 RoleTraits[Subject].Element）。
//
//   3) Creation Observer ── class UMassChildOfRelationEntityCreation : UMassRelationEntityCreation
//        * 关系实体被创建时跑，把 Parent 写入子实体的 FMassChildOfFragment::Parent。
//
//   4) 关系类型句柄      ── extern UE_API FTypeHandle ChildOfHandle;
//        * 模块加载期间通过 RegisterChildOfRelation() 完成初始化。
//        * 业务代码通过 ChildOfHandle 调 RelationManager.CreateRelationInstances 来建立父子关系。
//
//   5) 注册流程          ── RegisterChildOfRelation()（见 .cpp）
//        * 内部用单例 FChildOfRelationTypeInitializer 挂到 FTypeManager::OnRegisterBuiltInTypes
//          委托上；EntityManager 初始化时回调，把 ChildOf 的 traits 注册进 TypeManager。
//
// 业务代码使用方式：
//   * 在 Module 启动时调用一次 UE::Mass::Relations::RegisterChildOfRelation()。
//   * 之后通过 RelationManager.CreateRelationInstances(ChildOfHandle, parents, children) 建立关系。
//   * 通过 child 上的 FMassChildOfFragment::Parent 直接访问父；通过 FRelationManager 的
//     archetype-group 查询接口可以高效地"按父分组遍历子"。
// =============================================================================

#pragma once

#include "MassEntityRelations.h"
#include "MassTypeManager.h"
#include "MassRelationObservers.h"
#include "MassChildOf.generated.h"

#define UE_API MASSENTITY_API
 
namespace UE::Mass::Relations
{
	/**
	 * 中文：ChildOf 关系类型的运行时句柄。
	 *   * 模块加载期间被赋值（由 FChildOfRelationTypeInitializer 单例初始化）。
	 *   * 业务代码通过它调用 RelationManager 的关系操作接口。
	 *   * extern + UE_API：跨翻译单元共享，且需要从 MASSENTITY 模块导出。
	 */
	extern UE_API FTypeHandle ChildOfHandle;

	/**
	 * 中文：注册 ChildOf 关系类型的入口函数。
	 *   * 应在使用 Mass 关系系统的模块启动时调用一次（典型位置：StartupModule）。
	 *   * 内部使用 static 单例确保只注册一次——多次调用是幂等的。
	 *   * 注册步骤：
	 *       1) 把 FChildOfRelationTypeInitializer 单例挂到 FTypeManager::OnRegisterBuiltInTypes 委托。
	 *       2) 当 EntityManager 初始化 TypeManager 时，回调 RegisterType——填入 ChildOf 的完整 traits。
	 *       3) 同时把 FMassChildOfRelation::StaticStruct() 反向解析成 ChildOfHandle。
	 */
	UE_API void RegisterChildOfRelation();
}

/**
 * 中文：ChildOf 关系的 Tag。
 *   * 继承 FMassRelation —— Mass 关系系统的 base tag struct（见 MassEntityRelations.h）。
 *   * 反射宏：USTRUCT() —— 让 UE 反射系统能识别此 struct（StaticStruct() 才能拿到）。
 *   * 关系实体上会自动添加此 tag；用 EntityQuery::AddTagRequirement 即可只匹配 ChildOf 关系实体。
 */
USTRUCT()
struct FMassChildOfRelation : public FMassRelation
{
	GENERATED_BODY()
};

/**
 * 中文：挂在子实体上的 fragment。
 *   * 继承 FMassFragment —— Mass 数据组件的 base struct。
 *   * 反射宏：USTRUCT() —— 反射可发现，可序列化。
 *   * 业务代码可以通过 EntityManager.GetFragmentDataStruct<FMassChildOfFragment>(ChildHandle)
 *     直接拿到 Parent，O(1) 反向查询。
 *   * 由 UMassChildOfRelationEntityCreation::Execute 在关系实体创建时填充 Parent。
 *
 * 注意：本 fragment 是"子端"的 element；不是 subject/object 之分而是"被指向的一端"——
 *      在 ChildOf 中：Subject=child（拥有 ChildOf 关系的发起方），Object=parent（被指向的对象）。
 *      命名上"ChildOfFragment.Parent"也印证此约定：本 fragment 在子上，Parent 字段指向父。
 */
USTRUCT()
struct FMassChildOfFragment : public FMassFragment
{
	GENERATED_BODY()

	/**
	 * 中文：父实体的 handle。
	 *   * 反射宏：UPROPERTY(VisibleAnywhere, Category = Mass)
	 *       - VisibleAnywhere：在 details 面板中只读可见（调试时方便观察父引用）。
	 *       - Category = Mass：归类于 "Mass" 类目。
	 *   * 仅在 ChildOf 关系存在时有效；关系销毁时通过 observer 清理（subject 端 policy=CleanUp）。
	 */
	UPROPERTY(VisibleAnywhere, Category = Mass)
	FMassEntityHandle Parent;
};

/**
 * 中文：ChildOf 专属的"关系实体创建"observer。
 *   * 继承 UMassRelationEntityCreation —— 自动获得高优先级（1024）的 CreateEntity 监听。
 *   * 仅 override Execute：把每个新建关系实体上读出的 (Subject=child, Object=parent) 写入
 *     子实体的 FMassChildOfFragment::Parent。
 *   * 反射宏：UCLASS(MinimalAPI) —— 仅导出 UClass 反射符号。
 *   * 注册路径：在 .cpp 的 ChildOfRelation.RelationEntityCreationObserverClass = ... 中声明，
 *     由 EntityManager 在关系类型注册时自动 instance 并挂到 ObserverManager。
 */
UCLASS(MinimalAPI)
class UMassChildOfRelationEntityCreation : public UMassRelationEntityCreation
{
	GENERATED_BODY()

protected:
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

#undef UE_API
