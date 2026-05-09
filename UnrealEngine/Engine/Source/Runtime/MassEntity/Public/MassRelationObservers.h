// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a，Mass Relations 系统的 Observer 实现）
// -----------------------------------------------------------------------------
// 关系系统（Wave M12）的核心思想：
//   * 一段"关系"被 reify 为一个 *关系实体*（relation entity），同时挂上一个
//     FMassRelationFragment{Subject, Object, ...}，描述参与关系的两端。
//   * 角色实体（subject / object）也分别被 tag 成"我参与了某关系"。
//   * 所有"关系→实例"的查询（如 ParentOf=A 的所有 B）通过 FRelationManager 内部维护
//     的 TMap 索引（RoleMap）来加速。
//
// 那么"什么时候维护这个索引"？答：通过本文件定义的 4 种 Observer：
//
//   1. UMassRelationObserver        ── 共同基类。封装 ConfigureQueries（订阅哪些
//                                       fragment/tag）+ ConfigureRelationObserver
//                                       （把 trait 数据吃进来）。
//
//   2. UMassRelationEntityCreation  ── 关系实体被创建（CreateEntity 操作）时触发。
//                                       默认实现不做事（注释掉的 sample 代码展示思路），
//                                       具体的索引维护由具体关系类型的子类完成（如
//                                       UMassChildOfRelationEntityCreation）。
//                                       优先级 = 1024，确保它早于其他 observer 跑，
//                                       从而后续 observer 看到的 RelationManager 索引
//                                       是完整的。
//
//   3. UMassRelationEntityGuardDog  ── 调试用。监测关系实体上的 RelationFragment 被
//                                       违法地"通过 RemoveElement 移除"时报警——关系
//                                       实体的 fragment 是私有 implementation detail，
//                                       不应被外部直接 Remove。
//
//   4. UMassRelationEntityDestruction
//                                  ── 关系实体被销毁时触发。从 RelationManager.RoleMap
//                                       中把对应条目移除（双向：Subject ↔ Object）。
//
//   5. UMassRelationRoleDestruction
//                                  ── 角色实体（Subject/Object）被销毁时触发。根据
//                                       FRelationTypeTraits::RoleTraits[Role].DestructionPolicy
//                                       走不同的 lambda：CleanUp / Destroy / Splice / Custom。
//
// 注册流程：
//   * FRelationTypeTraits 中预留了 ObserverClass* 字段（ChildOfRelation 等示范见
//     Relations/MassChildOf.cpp）。
//   * EntityManager 在注册关系类型时，根据这些字段自动 instantiate observer 并通过
//     FMassObserverManager 挂上去。
// =============================================================================

#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "MassRelationObservers.generated.h"

#define UE_API MASSENTITY_API

namespace UE::Mass::Relations
{
	struct FRelationTypeTraits;
	enum class ERelationRole : uint8;
}

/**
 * 中文说明（关系 Observer 通用基类）：
 *   * 派生自 UMassObserverProcessor（Mass 观察者基础设施，Wave M6/M9）。
 *   * 封装两个 ConfigureXxx 接口：
 *       - ConfigureRelationObserver(...)：把外部传入的 RelationTypeHandle / Traits
 *         缓存到成员变量，并设置 ObservedType（默认 = Traits.RelationFragmentType）。
 *       - ConfigureQueries(...)：根据 ObservedType 是 Tag 还是 Fragment，往内部 Query
 *         里加相应的 Requirement；并按需自动加上"关系 Tag/Fragment 必须存在"的额外要求。
 *   * 自身不实现 Execute——具体逻辑由各子类在 Execute 中实现。
 *   * 该类不通过 UCLASS 的自动 ObserverRegistry 机制注册（构造里把
 *     bAutoRegisterWithObserverRegistry 设成 false），改为 EntityManager 在注册关系
 *     类型时手动 Instance + AddObserverInstance。
 *
 * 反射宏：UCLASS(MinimalAPI) —— 仅导出 UClass 反射符号。
 */
UCLASS(MinimalAPI)
class UMassRelationObserver : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UE_API UMassRelationObserver();

	/**
	 * @return whether the configuration was successful. Failed observers won't get added to the observer manager and will get destroyed promptly.
	 *
	 * 中文说明：
	 *   * 由 EntityManager 在关系类型注册流程中调用，把"关系类型句柄"+"完整 traits"传进来。
	 *   * 默认实现：把 ObservedType 设为 Traits.RelationFragmentType，再把 RelationTypeHandle
	 *     缓存下来。
	 *   * 子类可 override 来调整 ObservedType 或做更多的初始化（如
	 *     UMassRelationRoleDestruction 会根据 RoleTraits 动态选择 ExecuteFunction lambda）。
	 *   * 返回 false 表示"该 observer 不需要被实际注册"——例如某些角色没有提供
	 *     DestructionPolicy 时直接放弃。
	 *
	 * @param InRegisteredTypeHandle  关系类型在 FTypeManager 中的句柄
	 * @param Traits                  关系类型的完整 traits 数据
	 */
	UE_API virtual bool ConfigureRelationObserver(UE::Mass::FTypeHandle InRegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& Traits);

protected:
	/** 中文：与父类 UMassObserverProcessor 同名钩子，仅做 Super 转发——预留扩展点。 */
	UE_API virtual void InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager) override;

	/**
	 * 中文：构建内部 EntityQuery 的过滤要求。
	 *   * 必须先调用过 ConfigureRelationObserver（让 ObservedType / RelationTypeHandle 已就绪）。
	 *   * 步骤：
	 *       1) check ObservedType 必须是 Tag 或 Fragment（其他类型不合法）。
	 *       2) 按 Tag/Fragment 不同往 EntityQuery 加 Requirement。
	 *       3) 若 bAutoAddRelationFragmentRequirement / bAutoAddRelationTagRequirement
	 *          为真，再追加"该关系实体必须含 Relation Tag / Relation Fragment"的限制——
	 *          这能让 query 只 match 真正的关系实体，而非碰巧也持有 ObservedType 的角色实体。
	 *       4) 拼一个 DebugDescription 字符串，方便日志/profiling。
	 */
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

	/** 中文：Observer 内部使用的实体查询。在 ConfigureQueries 中构建。 */
	FMassEntityQuery EntityQuery;

	/** 中文：本 observer 监听的关系类型句柄；ConfigureRelationObserver 时缓存。 */
	UE::Mass::FTypeHandle RelationTypeHandle;

	/** Relevant only when ObservedType is a fragment
	 *  中文：当 ObservedType 是 Fragment（不是 Tag）时使用的访问权限——读 / 写 / 只读。 */
	EMassFragmentAccess ObservedTypeAccess = EMassFragmentAccess::ReadWrite;

	/** 中文：关系本身的 RelationFragment（如 FMassRelationFragment{Subject,Object}）的访问权限。
	 *       仅当 bAutoAddRelationFragmentRequirement=true 时生效。 */
	EMassFragmentAccess RelationFragmentAccessType = EMassFragmentAccess::ReadWrite;

	/** 中文：是否在 ConfigureQueries 中自动追加"实体必须有 RelationFragment"的要求。
	 *       默认 true；某些 observer（如 UMassRelationRoleDestruction）会关掉，因为它们关心
	 *       的是"角色实体"，不是"关系实体"。 */
	bool bAutoAddRelationFragmentRequirement = true;

	/** 中文：是否在 ConfigureQueries 中自动追加"实体必须有 Relation Tag"的要求。同上。 */
	bool bAutoAddRelationTagRequirement = true;

	/** 中文：仅日志/调试用的字符串描述，Profile/Log 中可识别 observer 实例。 */
	FString DebugDescription;
};

/**
 * 中文说明（关系实体创建 observer）：
 *   * 当 *新的关系实体* 被 CreateEntity 时触发——这是关系系统给"关系实例"建立索引的标准时机。
 *   * 默认实现（base class）什么都不做，注释里展示了如何收集 (Subject, Object, RelationEntity) 三元组的 sample 代码。
 *   * 子类（如 UMassChildOfRelationEntityCreation）通过 override Execute 在三元组上做关系
 *     专属的逻辑（如把 Parent 写到子的 fragment 里）。
 *   * 关键：ExecutionPriority = RelationCreationObserverExecutionPriority = 1024 —— 高于
 *     绝大多数普通 observer（默认 0）。这样可以确保：在其他 observer 看到这批关系实体时，
 *     RelationManager 的索引已经被写入。
 *
 * 反射宏：UCLASS(MinimalAPI) —— 仅导出 UClass 反射符号。
 */
UCLASS(MinimalAPI)
class UMassRelationEntityCreation : public UMassRelationObserver
{
	GENERATED_BODY()
public:
	/** 中文：高优先级常量。Mass 调度器把数值大的 observer 排在前面执行。 */
	static constexpr int32 RelationCreationObserverExecutionPriority = 1024;

	UE_API UMassRelationEntityCreation();

protected:
	/** 中文：默认 Execute 是空实现（见 .cpp 中的 sample 注释）。子类 override 实现关系特定逻辑。 */
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

/** Debug-time, detects relation elements being removed from the relation entities, which is not supposed to be done
 *
 *  中文说明（关系实体守门狗 / Guard Dog）：
 *    * 监听 RemoveElement 操作；如果有人尝试从一个"关系实体"上移除其私有 fragment/tag，
 *      就触发 ensure 报警。
 *    * 关系实体上的 RelationFragment / RelationTag 是 Mass 的实现细节，业务代码不应该直接 Remove。
 *      正确的做法是销毁整个关系实体（DestroyEntity）。
 *    * 仅在 WITH_MASSENTITY_DEBUG 编译开关下做实质检查（cpp 中带 #if）。
 */
UCLASS(MinimalAPI)
class UMassRelationEntityGuardDog : public UMassRelationObserver
{
	GENERATED_BODY()
public:
	UE_API UMassRelationEntityGuardDog();

protected:
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

/** cleans up data, removes enties in RoleMap if Entry.IsEmpty
 *
 *  中文说明（关系实体销毁 observer）：
 *    * 当 *关系实体* 被 DestroyEntity 时触发。
 *    * 算法：从关系实体上读出 (Subject, Object) 配对，去 RelationManager.RoleMap 中
 *      把对应的反向引用条目 RemoveAllSwap：
 *        - Subject 的 [Object 角色组] 中移除 Object
 *        - Object  的 [Subject 角色组] 中移除 Subject
 *    * 这与 UMassRelationEntityCreation 形成生命周期闭环。
 */
UCLASS(MinimalAPI)
class UMassRelationEntityDestruction : public UMassRelationObserver
{
	GENERATED_BODY()
public:
	UE_API UMassRelationEntityDestruction();

protected:
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

/**
 * 中文说明（关系角色销毁 observer）：
 *   * 当一个 *角色实体*（subject 或 object）被销毁时触发——而非关系实体本身。
 *   * 它的行为高度依赖 FRelationTypeTraits::RoleTraits[Role].DestructionPolicy：
 *       - CleanUp ：仅销毁与之相连的关系实体；其他端的角色实体保持存活。
 *       - Destroy ：连同对端角色实体一起销毁（级联）。
 *       - Splice  ：让本角色"摘除"——把它的 Subject 端和 Object 端互相重新连起来
 *                    （类似链表节点的删除：A→B→C 中删除 B 后变成 A→C）。
 *       - Custom  ：业务方自行处理，本 observer 不接管。
 *   * 这种"按 policy 选 lambda"的设计避免了为每种 policy 写一个独立 UCLASS，但代价是
 *     每个 observer 实例携带一个 FMassExecuteFunction（lambda + 捕获）。
 *   * 静态工厂 AddObserverInstances 负责根据 Traits 创建 1 个或 2 个 observer 实例：
 *       - 若双方角色的 element + policy 都相同 → 只创建 1 个，RelationRole=MAX 表示"双向"。
 *       - 否则为每个角色单独创建，提高效率。
 */
UCLASS(MinimalAPI)
class UMassRelationRoleDestruction : public UMassRelationObserver
{
	GENERATED_BODY()

public:
	UE_API UMassRelationRoleDestruction();

	/**
	 * 中文：override 父类的 ConfigureRelationObserver。
	 *   * 不调用 Super::ConfigureRelationObserver——因为这里 ObservedType 不是 RelationFragment，
	 *     而是 RoleTraits[RoleAsIndex].Element（或 RelationTypeHandle 的 tag），目的是匹配
	 *     角色实体而非关系实体。
	 *   * 同时根据 RoleTraits[RoleAsIndex].DestructionPolicy 选定 ExecuteFunction lambda。
	 *   * 返回 false 表示该 policy 不需要 observer（如 Custom），调用方会丢弃实例。
	 */
	UE_API virtual bool ConfigureRelationObserver(UE::Mass::FTypeHandle InRegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& Traits) override;

	/**
	 * 中文：静态工厂——为给定关系类型批量创建 UMassRelationRoleDestruction 实例并挂载到 ObserverManager。
	 *   * 如果两个角色具有完全相同的 element + DestructionPolicy → 仅创建 1 个 observer 处理双方。
	 *   * 否则为每个 Role 单独创建 1 个 observer（最多 2 个）。
	 *   * Custom policy 直接跳过，由业务方自己注册 observer。
	 */
	static UE_API void AddObserverInstances(FMassObserverManager& GetObserverManager, UE::Mass::FTypeHandle InRegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& Traits);

protected:
	/** 中文：执行入口——直接把 ExecuteFunction lambda 传给 ForEachEntityChunk。 */
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	/** 中文：在 Super::ConfigureQueries 之上，把 ExcludedRelationFragmentType 加成 None/None 要求，
	 *       这样 query 只 match 角色实体（不持有 RelationFragment 的实体），不会误抓关系实体。 */
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

	/** 中文：实际执行逻辑的 lambda。在 ConfigureRelationObserver 中根据 DestructionPolicy 赋值。 */
	FMassExecuteFunction ExecuteFunction;

	/**
	 * indicates which side of the relation the given destructor handles. MAX indicates both sides.
	 *
	 * 中文：本 observer 处理关系的哪一端（Subject / Object / 双方=MAX）。
	 *      MAX 表示由一个 observer 同时处理两端——仅当两端 element + policy 相同时使用，提升效率。
	 */
	UE::Mass::Relations::ERelationRole RelationRole = UE::Mass::Relations::ERelationRole::MAX;

	/**
	 * indicates the relation-specific fragment, that characterizes relation-entities. We use this
	 * information to filer these entities out - this observer is meant to only handle "role" entities
	 *
	 * 中文：关系实体上独有的 RelationFragment 类型（用于在 ConfigureQueries 中以 None/None
	 *      要求"排除关系实体"——本 observer 只处理角色实体）。
	 */
	const UScriptStruct* ExcludedRelationFragmentType = nullptr;
};

#undef UE_API
