// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StaticArray.h"
#include "MassEntityElementTypes.h"
#include "MassEntityHandle.h"
#include "MassArchetypeGroup.h"
#include "MassEntityMacros.h"
#include "MassEntityRelations.generated.h"

#define UE_API MASSENTITY_API

// =============================================================================
// 【模块定位 - Mass Entity Relations（关系系统类型层）】
// -----------------------------------------------------------------------------
// 在传统 ECS 中，"实体之间的关系（A 是 B 的 child / A 拥有 B / A 在追踪 B）"
// 通常通过组件里直接存 EntityHandle 来表达。但这种方式有局限：
//   - 难以在不同关系类型间统一查询（"找 B 的所有 child"需要扫所有 entity）；
//   - 难以处理"双向"语义（child 知道 parent，但 parent 不知道 child）；
//   - 难以在 entity 销毁时正确级联清理。
//
// Mass 的核心思路：【把"关系"本身建模成一个独立实体】。
//
//   假如 EntityA 是 EntityB 的 child，则：
//     1) 创建一个"关系实体" RelEntity；
//     2) RelEntity 上挂载一个 FMassRelationFragment（含 Subject=A, Object=B）；
//     3) RelEntity 上挂载一个 FChildOf 这种 FMassRelation 子类作为 Tag；
//     4) RelationManager 中索引：A.Subject 列表里有 RelEntity；B.Object 列表里有 RelEntity。
//
// 这样：
//   - 同样的索引结构可服务任意 relation 类型；
//   - 关系本身是 entity，可挂额外 fragment（例如 ChildOf 上挂"骨骼挂点"）；
//   - 销毁 A 时，由 destruction observer 找到 A 参与的所有 relation entity 并按
//     ERemovalPolicy 决定怎么处理（CleanUp / Destroy / Splice / Custom）。
//
// 【角色 Role】
//   每个关系有两端：Subject（"谁"）和 Object（"对谁/什么"）。
//   语义示例：
//     - ChildOf：Subject = child（多个），Object = parent（唯一）
//     - HasWeapon：Subject = character，Object = weapon
//     - WorksFor：Subject = employee，Object = company
//   FRoleTraits.bExclusive 控制"该角色端是否唯一"，例如 ChildOf 的 Object 端是 exclusive。
//
// 【级联策略 ERemovalPolicy】
//   当一个角色实体被销毁时，框架要决定关系实体怎么处理：
//     - CleanUp：仅清账本；关系实体本身被销毁，另一端不受影响（默认）。
//     - Destroy：销毁另一端实体（典型：删 parent 时连带删 child—很少用）。
//     - Splice：调用用户 observer 把这个关系"接到"别的关系上（典型：删 parent 时把 child
//       重新挂到 grandparent）。
//     - Custom：完全交给用户 observer 处理。
// =============================================================================

struct FMassEntityManager;
class UMassObserverProcessor;
namespace UE::Mass
{
	struct FRelationManager;
}

namespace UE::Mass::Relations
{
	/**
	 * 【ERemovalPolicy】当某一角色端的 entity 被销毁时，关系实体的级联处理策略。
	 * 该值定义在 FRoleTraits 上——同一关系两端可以有不同的策略。
	 */
	enum class ERemovalPolicy : uint8
	{
		/** only update the book-keeping
		 *  仅做账本清理：从 RelationManager.RoleMap 中移除条目；关系实体被销毁；
		 *  另一端的 entity 仍然存活，只是少了一条关系。
		 */
		CleanUp,
		/** when a given relation gets removed destroy the source entity (eg destroy the child when ChildOf relation gets removed)
		 *  级联销毁：当关系被解除时，把"另一端"的 entity 也销毁掉。
		 *  典型场景：HasInventoryItem 关系——删除 character 时连带销毁其 item entity。
		 */
		Destroy,
		/** external code will be called to patch up what remains off the relation
		 *  Splice 拼接：调用外部 observer 把关系"重新接驳"，例如删除中间节点时
		 *  把 child 接到 grandparent。
		 */
		Splice,
		/** the user will provide the observer processor
		 *  完全自定义：用户提供 SubjectEntityDestructionObserverClass / ObjectEntityDestructionObserverClass。
		 */
		Custom,

		MAX,
		Default = CleanUp,
	};

	/**
	 * 【EExternalMappingRequired】是否需要 RelationManager 自动维护"该角色端 → 关系实体"的索引。
	 *   默认 Yes：通用场景下 RelationManager 在 RoleMap 中维护 TArray<FMassRelationRoleInstanceHandle>；
	 *   设为 No：调用方有自己的索引方式（例如已经把 EntityHandle 存在 fragment 里），可以省内存。
	 *   @see FRoleTraits::RequiresExternalMapping
	 */
	enum class EExternalMappingRequired : uint8
	{
		No,
		Yes,
		Default = Yes, 
	};

	/**
	 * 【ERelationRole】关系的两端身份。
	 *   在数组索引中：Subject=0, Object=1，因此 TStaticArray 大小为 ERelationRole::MAX=2。
	 */
	enum class ERelationRole : uint8
	{
		Subject,	///< "谁"——发起或拥有关系的一方
		Object,		///< "对谁/什么"——关系指向的一方
		MAX
	};

	/** 调试用：把 ERelationRole 转字符串。 */
	inline FString LexToString(const ERelationRole Value)
	{
		return Value == ERelationRole::MAX
			? TEXT("INVALID")
			: (Value == ERelationRole::Subject
				? TEXT("Subject")
				: TEXT("Object"));
	}

	/**
	 * 【FRoleTraits】描述关系一个角色端的行为配置。
	 *   每个 FRelationTypeTraits 持有 2 个 FRoleTraits（Subject 端和 Object 端），
	 *   两端可以独立配置 element 类型、销毁策略、是否独占、是否需要 RelationManager 自动映射。
	 *
	 * 【"配置即代码"理念】
	 *   关系的语义（销毁谁、能否多对多、销毁后怎么修复）完全由 traits 数据决定，
	 *   而不需要为每种关系写一个新类——通用的 observer processor 根据 traits 分支即可。
	 */
	struct FRoleTraits
	{
		/**
		 * The element type that will be added to the participating entity. Can remain empty.
		 * Every entity will get the relation tag as well, the FRelationTypeTraits.RelationTagType
		 * 【可选 element】参与关系的"角色端实体"会被自动添加这个 element（fragment 或 tag）。
		 *   例如：HasWeapon 关系的 Subject 端可指定一个 FCanWieldWeapon tag；
		 *        Object 端可指定一个 FBeingWielded tag。
		 *   留空（nullptr）则只添加关系自身的 RelationTagType。
		 */
		const UScriptStruct* Element = nullptr;

		/**
		 * What to do when entities serving this role are destroyed. This value affects the whole relation, including other participants.
		 * 【销毁策略】当扮演此角色的实体被销毁时整个关系实体怎么处理。
		 *   注意"affects the whole relation"——会影响关系的另一端（如 Destroy 会连带销毁另一端）。
		 */
		ERemovalPolicy DestructionPolicy = ERemovalPolicy::Default;

		/**
		 * "Exclusive" means there can be only one participant like this in a relation.
		 * For example, there can be only one parent in our ChildOf relation (bExclusive = true, on the "parent" participant, i.e. the `Object`)
		 * while there can be multiple children (bExclusive = false for the `Subject`)
		 *
		 * 【独占性】此角色端在一个关系中是否唯一。
		 *   ChildOf 例子：parent 端独占（一个 child 只能有一个 parent），child 端非独占（一个 parent 可有多个 child）。
		 *   当 bExclusive=true 且为该 entity 创建新关系实例时，旧的关系实例会被自动销毁（见 RelationManager::HandleRole）。
		 */
		bool bExclusive = true;

		/**
		 * Declares whether this relation-specific implementation details provide dedicated
		 * mechanism for mapping other roles for entities serving as this role, the role these traits affect.
		 * It essentially answers whether this role needs the system to find other relation participants (`Yes`)
		 * or it can handle the task with custom code (like via the Element, option `No`).
		 * 
		 * Defaulting to `Yes` to provide reliable, out-of-the-box functionality required for
		 * fetching participants of relation instances. Set to `No` if you want to save memory by
		 * not automatically populating and utilizing FRelationData.RoleMap.
		 *
		 * @todo this functionality is not plugged in yet.
		 *
		 * 【是否需要框架的通用映射】Yes 时 RelationManager 会在 RoleMap 中自动维护索引；
		 *   No 时调用方自行通过 Element fragment 维护，可省一份 TMap。当前默认 Yes。
		 *   官方 todo：彻底支持 No 路径还在 plug-in 中。
		 */
		EExternalMappingRequired RequiresExternalMapping = EExternalMappingRequired::Default;

		bool operator==(const FRoleTraits& Other) const
		{
			return Element == Other.Element
				&& DestructionPolicy == Other.DestructionPolicy
				&& bExclusive == Other.bExclusive
				&& RequiresExternalMapping == Other.RequiresExternalMapping;
		}
	};

	/**
	 * 【FRelationTypeTraits】整个关系类型的总配置。
	 *   存放在 FTypeInfo::Traits 的 variant alternate 中，由 TypeManager 统一管理。
	 *
	 * 【主要字段】
	 *   - RelationTagType：关系的 tag 类型（FMassRelation 子类），同时充当类型 ID。
	 *   - RelationFragmentType：每个关系实体携带的 fragment 类型（FMassRelationFragment 或子类）。
	 *   - RoleTraits[Subject], RoleTraits[Object]：两端各自的角色行为配置。
	 *   - RegisteredGroupType：用于层级化的 archetype group type（仅 bHierarchical 时使用）。
	 *   - 4 个 ObserverClass：关系实体创建/销毁、Subject/Object 端销毁时挂的 observer。
	 */
	struct FRelationTypeTraits
	{
		/** 主构造：从 RelationTagType 出发，RelationFragmentType 默认为 FMassRelationFragment。 */
		UE_API FRelationTypeTraits(TNotNull<const UScriptStruct*> InRelationTagType);
		/** 拷贝并替换 tag 类型——用于"派生关系类型"复用现有配置。 */
		UE_API FRelationTypeTraits(const FRelationTypeTraits& Other, TNotNull<const UScriptStruct*> NewRelationTagType);
		FRelationTypeTraits(const FRelationTypeTraits& Other) = default;

		/** 获取 tag 类型——也是该关系在 TypeManager 中的注册键。 */
		TNotNull<const UScriptStruct*> GetRelationTagType() const
		{
			return RelationTagType;
		}

		FName GetFName() const
		{
			return RelationName;
		}

		/** 关系名（默认取自 RelationTagType.GetFName()，可在注册前覆盖）。用于日志/group type/archetype 命名。 */
		FName RelationName;

		/** Checks whether the traits are configured properly. 校验 traits 配置完整性（实现见 .cpp）。 */
		bool IsValid() const;

	private:
		/** 
		 * can only be set during creation since we use the same type to create UE::Mass::FTypeHandle for the relation type 
		 * 关系 tag 类型是关系的"身份" ——一旦构造就不能改，因为它充当 FTypeHandle 的索引键。
		 * 设 private 防止外部直接修改；只能通过"复制+替换"构造函数生成派生 traits。
		 */
		TNotNull<const UScriptStruct*> RelationTagType;

	public:
		/**
		 * The fragment type that will be automatically added to each relation entity created to represent relation instances
		 * @todo we can switch this to an array of fragment types or FInstancedStructs, bitsets or even FMassArchetypeCompositionDescriptor,
		 *		but first we need to see how our use-cases shape up.
		 *
		 * 【关系实体的 fragment 类型】每个关系实体会被自动加上这个 fragment（含 Subject/Object 字段）。
		 *   默认是 FMassRelationFragment；用户可派生子类来添加额外字段（例如 ChildOf 上的"附加偏移"）。
		 *   官方 todo：未来可能改成 fragment 数组。
		 */
		TNotNull<const UScriptStruct*> RelationFragmentType;

		/**
		 * Whether to use hierarchical archetype groups for the relation entities
		 * Set this to true of you want to do any data calculations with data stored in
		 * the relation entities, and them being processed in hierarchy order being a requirements
		 *
		 * 【层级 group】是否给关系实体打深度 tag，使得后续 query 能按"父在前 / 子在后"顺序处理。
		 *   典型场景：transform 计算 —— 先算父再算子。
		 *   实现：RegisteredGroupType + group_id=深度，在 CreateRelationInstances 时按深度分桶。
		 */
		bool bCreateRelationEntitiesInHierarchy = false;

		/** 两端的 FRoleTraits，索引为 ERelationRole 的整数值（[0]=Subject, [1]=Object）。 */
		TStaticArray<FRoleTraits, static_cast<uint8>(ERelationRole::MAX)> RoleTraits;

		/** 是否为层级关系（用于 ChildOf 这类）；为 true 时 CreateRelationInstances 会做"深度分组"工作。 */
		bool bHierarchical = false;

		/** 该关系专属的 archetype group type，由 TypeManager.RegisterType 自动申请。 */
		FArchetypeGroupType RegisteredGroupType;

		/**
		 * Is set, gets called upon type's registration to register appropriate observers.
		 * Return value indicates whether the entity manager should register default observes as well.
		 * 【自定义 observer 注册回调】
		 *   返回 true：默认 observer 仍然会被创建（这是"附加自定义"模式）；
		 *   返回 false：完全用户负责，框架不创建 default observer。
		 *   留空（empty TFunction）：完全使用下面 4 个 ObserverClass 的默认逻辑。
		 */
		TFunction<bool(FMassEntityManager&)> RegisterObserversDelegate;

		/**
		 * Processor classes to be instantiated when the relation type gets registered.
		 * These will get auto-created only if RegisterObserversDelegate is empty or returns `true`
		 * The default values point to generic observer classes that implement the generic policies and behavior.
		 * Nulling-out any of these member variables will result in the given functionality not being implemented
		 * by the system and makes the user responsible for supplying it. 
		 *
		 * 【4 个默认 observer class】
		 *   RelationEntityCreation*：关系实体创建时触发（当前未启用，see .cpp ctor）；
		 *   RelationEntityDestruction*：关系实体销毁时触发（清理 RoleMap、移除 element）；
		 *   SubjectEntityDestruction*：Subject 端 entity 被销毁时触发；
		 *   ObjectEntityDestruction*：Object 端 entity 被销毁时触发。
		 *   置空（nullptr）即"该 hook 不要默认 observer，让用户自行处理"。
		 */
		TWeakObjectPtr<UClass> RelationEntityCreationObserverClass;
		TWeakObjectPtr<UClass> RelationEntityDestructionObserverClass;
		TWeakObjectPtr<UClass> SubjectEntityDestructionObserverClass;
		TWeakObjectPtr<UClass> ObjectEntityDestructionObserverClass;

		/** 设置调试用的关系名称中缀（用于 DebugDescribeRelation 输出 [A] -RelationName- [B]）。 */
		void SetDebugInFix(FString&& InFix);
#if WITH_MASSENTITY_DEBUG
		/** 调试输出："[A.Description] <DebugInFix> [B.Description]"，便于日志追踪关系。 */
		UE_API FString DebugDescribeRelation(FMassEntityHandle A, FMassEntityHandle B) const;
	private:
		/** 调试输出中夹在两个 entity 中间的字符串（默认 = RelationName）。 */
		FString DebugInFix;
#endif // WITH_MASSENTITY_DEBUG
	};
}

//-----------------------------------------------------------------------------
// Relation types
//-----------------------------------------------------------------------------
/**
 * Structs extending FMassRelation represent a "concept" or a relation. These structs are
 * not intended to be stored in Mass. @see FMassRelationFragment for ways of storing relation-specific data
 *
 * 【FMassRelation】所有具体关系 tag 的基类。
 *   - 它本身派生自 FMassTag——所以是 zero-size 的标记类型，不存数据；
 *   - 子类（如 FChildOf, FHasWeapon）作为不同关系的"类型 ID"，被加到关系实体上做 archetype 区分；
 *   - 真正存数据的是 FMassRelationFragment（和它的子类）。
 */
USTRUCT()
struct FMassRelation : public FMassTag
{
	GENERATED_BODY()
};

//-----------------------------------------------------------------------------
// Relation data
//-----------------------------------------------------------------------------
/**
 * Relation fragment base. Every relation entity will get an instance of this type
 * or a type derived from it (as configured via FRelationTypeTraits.RelationFragmentType
 *
 * 【FMassRelationFragment】关系实体的"数据载荷"——存 Subject 和 Object 两端的 EntityHandle。
 *   每个关系实体都会有这个 fragment 实例（或它的子类，由 traits 配置决定）。
 *   注意：这两个 EntityHandle 指向"角色端实体"，而 fragment 自身所在的实体才是"关系实体"。
 */
USTRUCT()
struct FMassRelationFragment : public FMassFragment
{
	GENERATED_BODY()

	/**
	 * This is the "who" part of the relation. Examples:
	 * - the Child in a ChildOf relation
	 * - the Character in a HasWeapon relation
	 * - the Employee in a WorksFor relation
	 * 关系的"谁"端 / 主语方。
	 */
	FMassEntityHandle Subject;

	/**
	 * This is the "what" or "target" part of the relation. Examples:
	 * - the Parent in a ChildOf relation
	 * - the Weapon in a HasWeapon relation
	 * - the Company in a WorksFor relation
	 * 关系的"对象"端 / 宾语方。
	 */
	FMassEntityHandle Object;

	/** 通过整数 0/1 索引获取角色端 EntityHandle。 */
	FMassEntityHandle GetRole(const int32 Index) const
	{
		return Index == 0 ? Subject : Object;
	}

	/** 通过 ERelationRole 枚举获取角色端 EntityHandle。 */
	FMassEntityHandle GetRole(const UE::Mass::Relations::ERelationRole Role) const
	{
		check(Role != UE::Mass::Relations::ERelationRole::MAX);
		return GetRole(static_cast<int32>(Role));
	}
};

/**
 * 【FMassRelationMappingFragment】关系映射 fragment（占位/标记 fragment）。
 *   预留给"需要在 entity 上挂关系映射元数据"的扩展场景。当前结构为空。
 */
USTRUCT()
struct FMassRelationMappingFragment : public FMassFragment
{
	GENERATED_BODY()
};

/**
 * @todo we might want to promote the internals to FEntityIdentifier and have FMassEntityHandle derive from that.
 *
 * 【FMassRelationRoleInstanceHandle】把"角色端实体索引 + 角色 role + 关系实体索引"压缩到 8 字节的紧凑句柄。
 *
 * 【位打包布局】
 *   ┌────────────────────────────────────────┐
 *   │ uint32 RelationEntity:                 │ 关系实体的 EntityIndex（30 bit），高 2 bit 未用
 *   │   bits[0..29] = RelationEntity.Index    │
 *   │   bits[30..31] = unused (0)             │
 *   ├────────────────────────────────────────┤
 *   │ uint32 RoleEntity:                      │ 角色端实体索引 + 该角色 role
 *   │   bits[0..29] = RoleEntity.Index        │ （30 bit 足够 ~10 亿 entity）
 *   │   bits[30..31] = ERelationRole (Subject/Object) │
 *   └────────────────────────────────────────┘
 *
 * 【为什么要打包】
 *   RelationManager.RoleMap 中每个角色 entity 的"参与关系列表"是 TArray<FMassRelationRoleInstanceHandle>，
 *   8 字节/项比 16 字节（两个 FMassEntityHandle）省一半内存——对于"一个 parent 有上千 child"
 *   的层级关系尤其有效。
 *
 * 【为什么不存 SerialNumber】
 *   IsValid()=delete 故意标记不可调用：单凭 handle 无法判断该 RelationEntity 是否仍然存活，
 *   必须查 RelationManager 的 RoleMap 才能确定。SerialNumber 在解 handle 时由 EntityManager
 *   补回（CreateEntityIndexHandle 会查 EntityToData 拿到当前 SerialNumber）。
 */
struct alignas(8) FMassRelationRoleInstanceHandle
{
	/** EntityIndex 占低 30 位，给 ERelationRole 留 2 位（足够 4 个 role）。 */
	static constexpr int32 EntityIndexBits = 30;
	/** 低 30 位掩码，用来取出 EntityIndex。 */
	static constexpr int32 EntityIndexMask = (1 << 30) - 1;
	/** 高 2 位掩码（按位取反），用来取出 role 编码。 */
	static constexpr int32 TypeMask = ~EntityIndexMask;

	FMassRelationRoleInstanceHandle() = default;

	/** 
	 * 构造一个完整 handle。
	 * @param Role           本 handle 所代表的角色端在关系中的身份（Subject/Object）
	 * @param RoleHandle     角色端 entity（即 Subject 或 Object 实体）
	 * @param RelationEntityHandle 关系实体本身
	 * 注意：Index 范围必须 < 2^30，否则会被截断（实际上 Mass 的 EntityIndex 远不到此上限）。
	 */
	static FMassRelationRoleInstanceHandle Create(UE::Mass::Relations::ERelationRole Role, const FMassEntityHandle RoleHandle, const FMassEntityHandle RelationEntityHandle)
	{
		check(Role != UE::Mass::Relations::ERelationRole::MAX);

		FMassRelationRoleInstanceHandle ReturnHandle;
		ReturnHandle.SetRoleEntityIndex(RoleHandle.Index);
		ReturnHandle.SetRelationEntityIndex(RelationEntityHandle.Index);
		ReturnHandle.SetRole(Role);
		// 校验：取出来的索引应等于设进去的（防止 Index 超过 30 bit 被截断）。
		check(ReturnHandle.GetRoleEntityIndex() == RoleHandle.Index)
		check(ReturnHandle.GetRelationEntityIndex() == RelationEntityHandle.Index)

		return ReturnHandle;
	}

	/** 角色端实体索引（30 bit）。注意只是 Index，需要配合 EntityManager.CreateEntityIndexHandle 才能恢复完整 EntityHandle。 */
	int32 GetRoleEntityIndex() const 
	{
		return (RoleEntity & EntityIndexMask);
	}
	/** 通过 EntityManager 解出当前 SerialNumber 还原完整 EntityHandle。 */
	MASSENTITY_API FMassEntityHandle GetRoleEntityHandle(const FMassEntityManager& EntityManager) const;

	/** 关系实体索引（30 bit）。 */
	int32 GetRelationEntityIndex() const 
	{
		return (RelationEntity & EntityIndexMask);
	}
	/** 还原完整的关系实体 EntityHandle。 */
	MASSENTITY_API FMassEntityHandle GetRelationEntityHandle(const FMassEntityManager& EntityManager) const;

	/** 解出本 handle 中编码的角色身份。 */
	UE::Mass::Relations::ERelationRole GetRole() const
	{
		return static_cast<UE::Mass::Relations::ERelationRole>((RoleEntity & TypeMask) >> EntityIndexBits);
	}

	friend FString LexToString(const FMassRelationRoleInstanceHandle Handle)
	{
		return Handle.DebugGetDescription();
	}

	FString DebugGetDescription() const
	{
		return FString::Printf(TEXT("Relation %d role %s %d"), GetRelationEntityIndex(), *LexToString(GetRole()), GetRoleEntityIndex());
	}

	/** 
	 * We're unable to tell if a given relation instance handle is valid just by looking at a handle. Only the RelationManager can answer this question. Use IsSet as first filter 
	 * 【为什么 delete】仅凭 handle 自身（30+30+2 bit）无法判断目标 entity 是否仍存活——
	 *   没有存 SerialNumber，必须查 RelationManager 的 RoleMap 或 EntityManager。
	 *   故意 = delete 防止误用。
	 */
	bool IsValid() const = delete;

	/**
	 * 【FMassRelationRoleInstanceHandleFinder】函数对象——用于 TArray::FindByPredicate 等算法
	 *   按"角色端实体索引"匹配。
	 *   构造时存下要找的 EntityIndex，operator() 拿来比对每个 handle 的 RoleEntityIndex。
	 */
	struct FMassRelationRoleInstanceHandleFinder
	{
		FMassRelationRoleInstanceHandleFinder(const FMassEntityHandle EntityHandle)
			: EntityIndex(EntityHandle.Index)
		{	
		}

		bool operator()(const FMassRelationRoleInstanceHandle& Element) const
		{
			return Element.GetRoleEntityIndex() == EntityIndex;
		}

		const int32 EntityIndex;
	};

	bool operator==(const FMassRelationRoleInstanceHandle& Other) const
	{
		return RelationEntity == Other.RelationEntity && RoleEntity == Other.RoleEntity;
	}

private:
	/** 关系实体的 30-bit 索引（高 2 bit 未用）。 */
	uint32 RelationEntity = 0;
	/** 角色端实体的 30-bit 索引 + 高 2 bit 的 ERelationRole 编码。 */
	uint32 RoleEntity = 0;

	/** 写入角色端 EntityIndex（保留 RoleEntity 高 2 bit 的 role 信息）。 */
	void SetRoleEntityIndex(const int32 InIndex)
	{
		RoleEntity = (InIndex & EntityIndexMask) | (RoleEntity & TypeMask);
	}

	/** 写入关系实体 EntityIndex（保留高 2 bit 不变，目前未用）。 */
	void SetRelationEntityIndex(const int32 InIndex)
	{
		RelationEntity = (InIndex & EntityIndexMask) | (RelationEntity & TypeMask);
	}

	/** 写入 ERelationRole 到 RoleEntity 的高 2 bit。 */
	void SetRole(UE::Mass::Relations::ERelationRole InRole)
	{
		RoleEntity = (RoleEntity & EntityIndexMask) | (static_cast<int32>(InRole) << EntityIndexBits);
	}
};

namespace UE::Mass
{
	/** 
	 * UE::Mass::IsA 模板的 FMassRelation 特化——检查 Struct 是否派生自 FMassRelation。
	 * 用于 TypeManager 注册和 query 时校验"这是不是一个 relation 类型"。
	 */
	template<>
	inline bool IsA<FMassRelation>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassRelation::StaticStruct());
	}

	/**
	 * 【concept CRelation】C++20 concept：限定模板形参 T 必须派生自 FMassRelation。
	 *   用法：template<CRelation T> FMassEntityHandle CreateRelationInstance(...)
	 *   编译期立即拒绝非 relation 类型，比 SFINAE/static_assert 更易读。
	 */
	template<typename T>
	concept CRelation = TIsDerivedFrom<typename TRemoveReference<T>::Type, FMassRelation>::Value;
};

#undef UE_API
