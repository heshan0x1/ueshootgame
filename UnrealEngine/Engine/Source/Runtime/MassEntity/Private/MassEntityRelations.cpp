// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityRelations.h"
#include "MassEntityManager.h"
#include "MassRelationObservers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntityRelations)

// =============================================================================
// 【实现说明】FRelationTypeTraits 的实现 + FMassRelationRoleInstanceHandle 的解码辅助。
//   主要内容很轻：
//     - 默认 traits 的 observer class 配置；
//     - debug 描述输出；
//     - handle 反查 EntityManager 还原完整 EntityHandle。
// =============================================================================

namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// FRelationTypeTraits
	//-----------------------------------------------------------------------------
	FRelationTypeTraits::FRelationTypeTraits(const TNotNull<const UScriptStruct*> InRelationTagType)
		: RelationTagType(InRelationTagType)
		, RelationFragmentType(FMassRelationFragment::StaticStruct())
	{
		// at the moment UMassRelationEntityCreation doesn't do anything, so we don't plug it in
		// RelationEntityCreationObserverClass = UMassRelationEntityCreation::StaticClass();
		// 【默认 observer 配置】
		//   - RelationEntityCreationObserverClass：留空（创建关系实体时无需特殊处理）。
		//   - RelationEntityDestructionObserverClass：销毁关系实体时清理 RoleMap、移除 element 等。
		//   - Subject/Object 端销毁时使用同一个 UMassRelationRoleDestruction observer——
		//     该 observer 会根据 ERemovalPolicy 分支（CleanUp/Destroy/Splice）。
		RelationEntityDestructionObserverClass = UMassRelationEntityDestruction::StaticClass();

		SubjectEntityDestructionObserverClass
			= ObjectEntityDestructionObserverClass = UMassRelationRoleDestruction::StaticClass();
	}

	FRelationTypeTraits::FRelationTypeTraits(const FRelationTypeTraits& Other, const TNotNull<const UScriptStruct*> NewRelationTagType)
		: FRelationTypeTraits(Other)
	{
		// 派生构造：复用 Other 的所有 traits，仅替换 tag 类型。
		// 用例：项目侧定义 FFriendOf : FMassRelation，复用 ChildOf 的 traits 配置但用新的 tag。
		RelationTagType = NewRelationTagType;
	}

	void FRelationTypeTraits::SetDebugInFix(FString&& InFix)
	{
#if WITH_MASSENTITY_DEBUG
		// 仅在调试构建中保留——release 下没有 DebugInFix 字段。
		DebugInFix = MoveTemp(InFix);
#endif // WITH_MASSENTITY_DEBUG
	}

#if WITH_MASSENTITY_DEBUG
	FString FRelationTypeTraits::DebugDescribeRelation(FMassEntityHandle A, FMassEntityHandle B) const
	{
		// 输出格式："[A的描述] <关系名> [B的描述]"，例如 "[Entity_42] ChildOf [Entity_7]"。
		return FString::Printf(TEXT("[%s] %s [%s]"), *A.DebugGetDescription(), *DebugInFix, *B.DebugGetDescription());
	}
#endif // WITH_MASSENTITY_DEBUG
}

//-----------------------------------------------------------------------------
// FMassRelationRoleInstanceHandle
//-----------------------------------------------------------------------------
// handle 内部只存了 30-bit Index，所以"还原完整 EntityHandle"必须问 EntityManager
// 拿到当前的 SerialNumber（处理 entity 销毁后 index 复用的情况）。
FMassEntityHandle FMassRelationRoleInstanceHandle::GetRoleEntityHandle(const FMassEntityManager& EntityManager) const
{
	const int32 EntityIndex = GetRoleEntityIndex();
	// CreateEntityIndexHandle 会查 EntityManager.EntityToData[Index]，
	// 把当前 SerialNumber 拼回成完整的 (Index, SerialNumber) handle。
	// 若该 slot 已被回收复用，返回的 handle 会指向新的 entity（调用方需自己校验）。
	return EntityManager.CreateEntityIndexHandle(EntityIndex);
}

FMassEntityHandle FMassRelationRoleInstanceHandle::GetRelationEntityHandle(const FMassEntityManager& EntityManager) const
{
	const int32 EntityIndex = GetRelationEntityIndex();
	return EntityManager.CreateEntityIndexHandle(EntityIndex);
}
