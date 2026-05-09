// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTypeManager.h"
#include "MassEntityManager.h"
#include "MassTestableEnsures.h"
#include "MassEntityElementTypes.h"
#include "MassEntityRelations.h"
#include "Misc/CoreDelegates.h"

// =============================================================================
// 【实现说明】FTypeManager 的实现
//   - 不持有 UObject 强引用：所有"代表类型"的 UStruct/UClass 由全局 reflection 系统持有；
//     TypeManager 只通过 TObjectKey 索引。
//   - 注册路径都收敛到 RegisterTypeInternal，统一处理"重复注册"和"二级索引"。
//   - relation 类型注册有特殊回调：通知 EntityManager 创建 destruction observer
//     和 archetype group type。
// =============================================================================

namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// FTypeHandle
	//-----------------------------------------------------------------------------
	// 私有构造函数：只能由 friend FTypeManager 或 MakeTypeHandle 调用。
	FTypeHandle::FTypeHandle(TObjectKey<const UStruct> InTypeKey)
		: TypeKey(InTypeKey)
	{
		
	}

	//-----------------------------------------------------------------------------
	// FTypeManager
	//-----------------------------------------------------------------------------
	// 静态多播委托的定义。所有 FTypeManager 实例共享。
	FTypeManager::FOnRegisterBuiltInTypes FTypeManager::OnRegisterBuiltInTypes;

	FTypeManager::FTypeManager(FMassEntityManager& InEntityManager)
		: OuterEntityManager(InEntityManager)
	{
	}

	void FTypeManager::RegisterBuiltInTypes()
	{
		// 广播：让所有监听者注册自己的内置类型。
		// 典型监听者：MassActor 模块、MassRelationManager 内置 relation 类型、
		// 项目侧的 Startup 模块。
		OnRegisterBuiltInTypes.Broadcast(*this);
		// 标记已完成内置阶段；之后再注册 relation 类型会立即触发 observer 创建。
		bBuiltInTypesRegistered = true;
	}

	FTypeHandle FTypeManager::RegisterTypeInternal(TNotNull<const UStruct*> InType, FTypeInfo&& TypeInfo)
	{
		// 步骤1：用 InType 构造 FTypeHandle（基于 TObjectKey）。
		FTypeHandle TypeHandle(InType);
		// 步骤2：检查是否已有该类型记录。
		FTypeInfo* ExistingData = TypeDataMap.Find(FTypeHandle(TypeHandle.TypeKey));
		if (LIKELY(ExistingData == nullptr))
		{
			// 走"全新注册"路径——绝大多数情形。
			if (TypeInfo.Traits.IsType<FSubsystemTypeTraits>())
			{
				// 维护二级索引：subsystem 类型放进 SubsystemTypes 集合。
				SubsystemTypes.Add(FTypeHandle(TypeHandle.TypeKey));
			}

			TypeDataMap.Add(FTypeHandle(TypeHandle.TypeKey), MoveTemp(TypeInfo));
		}
		else if (testableEnsureMsgf(bBuiltInTypesRegistered == false, TEXT("Registered type overriding is supported only as part of built-in types registration.")))
		{
			// we're overriding the existing data with the new data in assumption it's more up-to-date.
			// The most common occurence of this will be with already registered subsystems' subclasses.
			// The subclasses can change the data registered on their behalf by the super class,
			// but most of the time that won't be necessary.
			// 【覆写场景】内置注册阶段，子类可覆盖父类已经注册的 traits（典型：subsystem 子类
			//   想要不同的 thread-safety 标记）。运行时阶段不允许覆写——会 ensure 失败。
			*ExistingData = MoveTemp(TypeInfo);
		}
		
		return TypeHandle;
	}

	FTypeHandle FTypeManager::RegisterType(TNotNull<const UStruct*> InType, FSubsystemTypeTraits&& TypeTraits)
	{
		// 把 traits 包进 FTypeInfo 并 set 到 variant 的 FSubsystemTypeTraits 槽位。
		FTypeInfo TypeInfo;
		TypeInfo.TypeName = InType->GetFName();
		TypeInfo.Traits.Set<FSubsystemTypeTraits>(MoveTemp(TypeTraits));
		return RegisterTypeInternal(InType, MoveTemp(TypeInfo));
	}

	FTypeHandle FTypeManager::RegisterType(TNotNull<const UStruct*> InType, FSharedFragmentTypeTraits&& TypeTraits)
	{
		// 校验：必须派生自 FMassSharedFragment。否则返回空句柄并 ensure 失败。
		testableCheckfReturn(UE::Mass::IsA<FMassSharedFragment>(InType), {}
			, TEXT("%s is not a valid shared fragment type"), *InType->GetName());

		FTypeInfo TypeInfo;
		TypeInfo.TypeName = InType->GetFName();
		TypeInfo.Traits.Set<FSharedFragmentTypeTraits>(MoveTemp(TypeTraits));
		return RegisterTypeInternal(InType, MoveTemp(TypeInfo));
	}

	FTypeHandle FTypeManager::RegisterType(FRelationTypeTraits&& TypeTraits)
	{
		// 【relation 注册路径】比前两个重载复杂得多：
		// 1) RelationTagType 必须是 FMassRelation 子类；
		// 2) RelationFragmentType 必须是 FMassRelationFragment 子类（容纳 Subject/Object）；
		// 3) 自动派生 RelationName 和调试前缀；
		// 4) 创建专属的 archetype group type（用于层级化 relation entity 排序）；
		// 5) 注册成功后，若已过内置阶段，通知 EntityManager 创建 destruction observer。

		// 校验1：RelationTagType 必须派生自 FMassRelation。
		TNotNull<const UScriptStruct*> InType = TypeTraits.GetRelationTagType();
		testableCheckfReturn(UE::Mass::IsA<FMassRelation>(InType), return {}
			, TEXT("%s is not a valid relation type"), *InType->GetName());

		// 校验2：RelationFragmentType 必须派生自 FMassRelationFragment（必含 Subject/Object）。
		testableCheckfReturn(TypeTraits.RelationFragmentType->IsChildOf(FMassRelationFragment::StaticStruct()), return {}
			, TEXT("%s is not a valid TypeTraits.RelationFragmentType, needs to derive from FMassRelationFragment")
			, *TypeTraits.RelationFragmentType->GetName());

		// 步骤3：若调用方未提供 RelationName，则使用 RelationTagType 的名称作为默认值，
		// 同时把"调试中缀"也设为该名（日志中体现 [SubjectEntity] <RelationName> [ObjectEntity]）。
		if (TypeTraits.RelationName.IsNone())
		{
			TypeTraits.RelationName = InType->GetFName();
			TypeTraits.SetDebugInFix(TypeTraits.RelationName.ToString());
		}

		// 步骤4：检查是否已注册过相同 relation tag。
		FTypeInfo* ExistingData = TypeDataMap.Find(FTypeHandle(InType));
		
		// 关系类型的 traits 一旦 built-in 阶段结束就锁定——不允许在运行期再次"修改"已有的 relation。
		// 这是因为 destruction observer 已经创建，traits 改了之后行为会与 observer 不一致。
		if (!testableEnsureMsgf(bBuiltInTypesRegistered == false || ExistingData == nullptr
			, TEXT("Modifying relationship after registration done is not supported")))
		{
			return {};
		}

		FTypeInfo TypeInfo;
		TypeInfo.TypeName = InType->GetFName();

		// 步骤5：为 relation 申请专属 archetype group type，用于"层级关系"中按深度分组实体。
		// （例如 ChildOf 关系下，根节点 group=0，第一层 child group=1 等）
		if (TypeTraits.RegisteredGroupType.IsValid() == false)
		{
			TypeTraits.RegisteredGroupType = OuterEntityManager.FindOrAddArchetypeGroupType(TypeTraits.RelationName);
		}
		TypeInfo.Traits.Set<FRelationTypeTraits>(MoveTemp(TypeTraits));

		// 步骤6：写入 TypeDataMap。
		const FTypeHandle RegisteredTypeHandle = RegisterTypeInternal(InType, MoveTemp(TypeInfo));
		if (RegisteredTypeHandle.IsValid())
		{
			if (bBuiltInTypesRegistered)
			{
				// if the built-in types are already registered we need to notify the entity manager that there's a new
				// relation type, that might require additional handling (like creation of appropriate entity destruction observers)
				// Note that we don't call this during built-in types registration to give project-specific code a chance
				// to override type traits before the entity manager handles the registered types.
				// 【设计要点】只有在 built-in 阶段结束后才通知 EntityManager。
				//   原因：built-in 阶段，多个模块按顺序广播 OnRegisterBuiltInTypes，
				//   后注册的子类可能覆写父类的 traits。如果在每次内置注册时都通知 EntityManager
				//   立即创建 observer，那覆写后这些 observer 就和最新 traits 不一致了。
				//   等所有内置注册都完成后由 EntityManager 集中遍历 TypeDataMap 创建 observer。
				OuterEntityManager.OnNewTypeRegistered(RegisteredTypeHandle);
			}
		}

		return RegisteredTypeHandle;
	}

	FTypeHandle FTypeManager::GetRelationTypeHandle(const TNotNull<const UScriptStruct*> RelationOrElementType) const
	{
		// 注意：此函数只是"把 UScriptStruct* 包成 FTypeHandle"，不验证是否真的注册过——
		// 配合 IsValidRelationType 才能两步校验。
		const bool bValidRelationshipType = UE::Mass::IsA<FMassRelation>(RelationOrElementType);
		ensureMsgf(bValidRelationshipType, TEXT("%s is not a valid relationship type"), *RelationOrElementType->GetName());

		return UE::Mass::IsA<FMassRelation>(RelationOrElementType) 
			? FTypeHandle(RelationOrElementType)
			: FTypeHandle();
	}

	bool FTypeManager::IsValidRelationType(const TNotNull<const UScriptStruct*> RelationOrElementType) const
	{
		// 双重校验：
		// 1) 是 FMassRelation 子类（编译期/反射保证）；
		// 2) 已通过 RegisterType 写入 TypeDataMap（运行期注册保证）。
		return UE::Mass::IsA<FMassRelation>(RelationOrElementType) && TypeDataMap.Contains(FTypeHandle(RelationOrElementType));
	}
}