// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【中文文件总览】MassEntityBuilder.cpp
// -----------------------------------------------------------------------------
// 实现 FEntityBuilder（单实体构造的"流式 API"）。
// 三个核心实现要点：
//   1) 状态机维护：构造 / 析构 / 拷贝 / 移动 / Reset 等都要正确转换 EState 并管理 reserved handle 资源；
//   2) Lazy 缓存：CacheArchetypeHandle / CacheSharedFragmentValue 只在 Commit 路径上调用一次；
//   3) 与 EntityManager.IsProcessing() 的兼容：Mass 处理过程中 Commit 必须走 deferred command。
// =============================================================================
#include "MassEntityBuilder.h"
#include "MassCommandBuffer.h"
#include "MassCommands.h"
#include "MassEntityManager.h"
#include "VisualLogger/VisualLogger.h"
#if WITH_MASSENTITY_DEBUG
#include "HAL/IConsoleManager.h"
#endif // WITH_MASSENTITY_DEBUG

namespace UE::Mass
{
#if WITH_MASSENTITY_DEBUG
	namespace Debug
	{
		// 中文：开关：FEntityBuilder::Make 时是否对入参 InstancedStruct 与 Composition 一致性做校验。
		// 默认开。可通过控制台 mass.debug.ValidateEntityBuilderMakeInput 关闭以提升性能。
		bool bValidateEntityBuilderMakeInput = true;
		namespace
		{
			// 中文：FAutoConsoleVariableRef 自动注册控制台变量到 ECVF_Cheat 类别（仅作弊/调试可用）。
			FAutoConsoleVariableRef AnonymousCVars[] = {
				{TEXT("mass.debug.ValidateEntityBuilderMakeInput"), bValidateEntityBuilderMakeInput
					, TEXT("When set, every call to FEntityBuilder::Make will verify if the struct values provided match declared entity composition.")
					, ECVF_Cheat}
			};
		}
	}
#endif // WITH_MASSENTITY_DEBUG

	namespace Private
	{
		// =============================================================================
		// 【中文】FEntityBuilderHelper：把"按 element 类型批量从源实体抽数据"的模板代码集中起来。
		// 通过 friend 访问 FEntityBuilder 的私有 GetInstancedStructContainerInternal<T>。
		// =============================================================================
		struct FEntityBuilderHelper
		{
			// 中文：Append 语义 — 删除 builder 中已存在的同类型 element，再 Copy 一遍。
			// 用于 AppendDataFromEntity，避免与已有数据重复。
			template<typename T>
			static void AppendFromEntity(FEntityBuilder& Builder, const FMassEntityHandle SourceEntityHandle, const FMassArchetypeCompositionDescriptor& ArchetypeComposition)
			{
				const auto& SourceContainer = ArchetypeComposition.GetContainer<T>();
				TArray<FInstancedStruct>& ElementInstanceContainer = Builder.GetInstancedStructContainerInternal<T>();

				// remove all the existing entries that match SourceContainer, and then just copy.

				for (auto Iterator = SourceContainer.GetIndexIterator(); Iterator; ++Iterator)
				{
					// @todo we could use an iterator that can fetch the type by simply calling Iterator.GetType()
					TNotNull<const UScriptStruct*> ElementType = SourceContainer.GetTypeAtIndex(*Iterator);
					const int32 FoundIndex = ElementInstanceContainer.IndexOfByPredicate([&ElementType](const FInstancedStruct& ExistingElement)
					{
						return ExistingElement.GetScriptStruct() == ElementType;
					});
					if (FoundIndex != INDEX_NONE)
					{
						// 中文：用 RemoveAtSwap（不缩容）以 O(1) 删除；后续 CopyFromEntity 会重新 Reserve+Add。
						ElementInstanceContainer.RemoveAtSwap(FoundIndex, EAllowShrinking::No);
					}
				}
				CopyFromEntity<T>(Builder, SourceEntityHandle, ArchetypeComposition);
			}

			// 中文：Copy 语义 — 直接遍历源 archetype 的 element 类型位集，从 EntityManager 中获取该 element 的 struct view，
			// 然后 Emplace 一份到 builder 容器。不做查重（因为调用者已 Reset 或 Append 已先做删除）。
			template<typename T>
			static void CopyFromEntity(FEntityBuilder& Builder, const FMassEntityHandle SourceEntityHandle, const FMassArchetypeCompositionDescriptor& ArchetypeComposition)
			{
				const auto& SourceContainer = ArchetypeComposition.GetContainer<T>();
				TArray<FInstancedStruct>& ElementInstanceContainer = Builder.GetInstancedStructContainerInternal<T>();

				ElementInstanceContainer.Reserve(ElementInstanceContainer.Num() + SourceContainer.CountStoredTypes());

				for (auto Iterator = SourceContainer.GetIndexIterator(); Iterator; ++Iterator)
				{
					// @todo we could use an iterator that can fetch the type by simply calling Iterator.GetType()
					TNotNull<const UScriptStruct*> Type = SourceContainer.GetTypeAtIndex(*Iterator);
					FConstStructView SourceElementView = Builder.EntityManager->GetElementDataStruct<T>(SourceEntityHandle, Type);

					// this happening is practically impossible, so testing only in debug
					checkSlow(SourceElementView.IsValid());
					ElementInstanceContainer.Emplace(SourceElementView);
				}
			}
		};

#if WITH_MASSENTITY_DEBUG
		// 中文：调试校验 — 检查 Container（FInstancedStruct/FConstSharedStruct/FSharedStruct 数组）中每个元素：
		//   1) 类型属于 TElement（FMassFragment/FMassConstSharedFragment/FMassSharedFragment）
		//   2) 在 Bitset 中确实被标记
		// 任一项失败都通过 UE_VLOG_UELOG 输出错误，并返回 true（"发现问题"）。
		template<typename TElement, typename TBitset, typename TWrapper>
		bool CheckStructContainer(TConstArrayView<TWrapper> Container, const TBitset& Bitset, const UObject* LogOwner)
		{
			bool bIssuesFound = false;

			for (const TWrapper& Element : Container)
			{
				if (Mass::IsA<TElement>(Element.GetScriptStruct()))
				{
					if (Bitset.Contains(*Element.GetScriptStruct()) == false)
					{
						bIssuesFound = true;
						UE_VLOG_UELOG(LogOwner, LogMass, Error, TEXT("%hs: input Composition doesn't contain %s"), __FUNCTION__, *GetNameSafe(Element.GetScriptStruct()));
					}
				}
				else
				{
					bIssuesFound = true;
					UE_VLOG_UELOG(LogOwner, LogMass, Error, TEXT("%hs: %s is not a valid %s type")
						, __FUNCTION__, *GetNameSafe(Element.GetScriptStruct()), *TElement::StaticStruct()->GetName());
				}
			}

			return bIssuesFound;
		}

		// 中文：FEntityBuilder::Make(...) 的入参 sanity check —— 三种类别（fragment / const shared / shared）都过一遍。
		// 返回值为"是否通过校验"（与 CheckStructContainer 的"是否发现问题"语义相反）。
		bool ValidateMakeInput(const TSharedRef<FMassEntityManager>& InEntityManager, const FMassArchetypeCompositionDescriptor& Composition
			, TConstArrayView<FInstancedStruct> InitialFragmentValues, TConstArrayView<FConstSharedStruct> ConstSharedFragments, TConstArrayView<FSharedStruct> SharedFragments)
		{
			const UObject* LogOwner = InEntityManager->GetOwner();
			bool bIssuesFound = CheckStructContainer<FMassFragment>(InitialFragmentValues, Composition.GetFragments(), LogOwner);
			bIssuesFound |= CheckStructContainer<FMassConstSharedFragment>(ConstSharedFragments, Composition.GetConstSharedFragments(), LogOwner);
			bIssuesFound |= CheckStructContainer<FMassSharedFragment>(SharedFragments, Composition.GetSharedFragments(), LogOwner);
			
			return !bIssuesFound;
		}
#endif // WITH_MASSENTITY_DEBUG
	}

//-----------------------------------------------------------------------------
// FEntityBuilder
//-----------------------------------------------------------------------------
// 中文：构造（raw 引用）—— 通过 AsShared() 升级为 TSharedRef。前提：EntityManager 已被 TSharedFromThis 管理。
FEntityBuilder::FEntityBuilder(FMassEntityManager& InEntityManager)
	: EntityManager(InEntityManager.AsShared())
{	
}

// 中文：构造（共享引用）—— 推荐用法。
FEntityBuilder::FEntityBuilder(const TSharedRef<FMassEntityManager>& InEntityManager)
	: EntityManager(InEntityManager)
{
	
}

// 中文：拷贝构造 —— 委托给 operator=。注意 EntityManager 字段必须先初始化（TSharedRef 非空要求）。
FEntityBuilder::FEntityBuilder(const FEntityBuilder& Other)
	: EntityManager(Other.EntityManager)
{
	*this = Other;
}

// 中文：拷贝赋值 —— 拷贝 *配方* 不拷贝 *实体身份*。
// 关键决策：
//   - Other 必须 IsValid()（不能从 Invalid 状态拷贝过来）；
//   - 若 EntityManager 不同，先释放 this 当前的 reserved handle，再切到 Other 的 manager；
//   - 若 EntityManager 相同且 this 已 Committed，重置 EntityHandle（以便后续重新 Commit 创造新实体）；
//   - 不复制 EntityHandle —— 拷贝出来的 builder 是"独立的另一个实体"。
//   - State 根据 Composition 是否为空决定 Empty / ReadyToCommit。
FEntityBuilder& FEntityBuilder::operator=(const FEntityBuilder& Other)
{
	if (testableEnsureMsgf(Other.IsValid(), TEXT("Copying invalid entity builder instances is not supported")))
	{
		// if we already have an EntityHandle reserved we might want to keep it - why reserve a handle again
		// soon, the reserved handle doesn't have an archetype associated with it?
		// We do need to release the handle if we're dealing with a different entity manager (unexpected in practice, but possible [for now])
		if (EntityManager != Other.EntityManager)
		{
			ConditionallyReleaseEntityHandle();
			EntityManager = Other.EntityManager;
		}
		// we also reset the handle if this builder has already committed its entity - the entity needs to 
		// be forgotten by this builder, it's "out in the wild" now and should be safe from accidental destruction.
		else if (State == EState::Committed)
		{
			EntityHandle.Reset();
		}
			
		Composition = Other.Composition;
		Fragments = Other.Fragments;
		SharedFragments = Other.SharedFragments;
		ConstSharedFragments = Other.ConstSharedFragments;

		State = Composition.IsEmpty() ? EState::Empty : EState::ReadyToCommit;
	}

	return *this;
}

// 中文：移动赋值 —— 与拷贝不同，会接管 Other 的 EntityHandle 和 State，避免无谓 reserve/release。
// 详细决策树：
//   - 若 EntityManager 不同：先释放 this 的 reserved handle，再 move EntityManager。
//   - 若 this 已有 reserved handle：
//       * Other 也有 reserved handle → 释放 this 的，接管 Other 的。
//       * 否则保留 this 的 handle。
//       * 若 Other 已 Committed，本 builder 仍需"造另一个实体"，所以新 State = ReadyToCommit。
//   - 若 this 没有 reserved handle：直接接管 Other 的 EntityHandle 和 State。
//   - 最后把 Other 置为 Invalid（move-from sentinel）。
FEntityBuilder& FEntityBuilder::operator=(FEntityBuilder&& Other)
{
	if (testableEnsureMsgf(Other.IsValid(), TEXT("Copying invalid entity builder instances is not supported")))
	{
		// if we already have an EntityHandle reserved we might want to keep it - why reserve a handle again
		// soon, the reserved handle doesn't have an archetype associated with it?
		// We do need to release the handle if we're dealing with a different entity manager (unexpected in practice, but possible [for now])
		if (EntityManager != Other.EntityManager)
		{
			ConditionallyReleaseEntityHandle();
			EntityManager = MoveTemp(Other.EntityManager);
		}
		Fragments = MoveTemp(Other.Fragments);
		SharedFragments = MoveTemp(Other.SharedFragments);
		ConstSharedFragments = MoveTemp(Other.ConstSharedFragments);

		// the main point of the elaborated logic below is to avoid needlessly releasing reserved entities.
		if (HasReservedEntityHandle())
		{
			if (Other.HasReservedEntityHandle())
			{
				ConditionallyReleaseEntityHandle();
				EntityHandle = Other.EntityHandle;
			}
			State = (Other.State == EState::Committed)
				? EState::ReadyToCommit // we have a reserved entity at hand, we can Commit again
				: Other.State;
		}
		else
		{
			// we just take everything as is
			EntityHandle = Other.EntityHandle;
			State = Other.State;
		}

		Other.EntityHandle.Reset();
		Other.State = EState::Invalid;
	}

	return *this;
}

// 中文：析构 —— 仅做资源回收（释放 reserved handle）。不会自动 Commit。
// 自动 Commit 是 FScopedEntityBuilder 的语义。
FEntityBuilder::~FEntityBuilder()
{
	ConditionallyReleaseEntityHandle();
}

// 中文：Make 的"const-view"版本 —— 适合从已知 Composition + 默认值数组造 builder 的场景。
// 在 debug 模式下做入参一致性校验。
FEntityBuilder FEntityBuilder::Make(const TSharedRef<FMassEntityManager>& InEntityManager, const FMassArchetypeCompositionDescriptor& Composition
	, TConstArrayView<FInstancedStruct> InitialFragmentValues, TConstArrayView<FConstSharedStruct> ConstSharedFragments, TConstArrayView<FSharedStruct> SharedFragments)
{
	FEntityBuilder Builder(InEntityManager);

#if WITH_MASSENTITY_DEBUG
	if (Debug::bValidateEntityBuilderMakeInput)
	{
		ensureMsgf(Private::ValidateMakeInput(InEntityManager, Composition, InitialFragmentValues, ConstSharedFragments, SharedFragments)
			, TEXT("%hs: failed input validation. See log for details."), __FUNCTION__);
	}
#endif // WITH_MASSENTITY_DEBUG

	Builder.Composition = Composition;
	Builder.Fragments = InitialFragmentValues;
	Builder.SharedFragments = SharedFragments;
	Builder.ConstSharedFragments = ConstSharedFragments;

	// 中文：注意 —— 这里没有显式设 State，依赖 NRVO/return 时刚初始化默认 EState::Empty。
	// 但由于 Composition 非空，按本类约定其实应是 ReadyToCommit。看上去是依赖后续操作设置 State。
	// （潜在小问题：如果 caller 直接对返回的 builder 调 Commit() 而不调任何 Add，State 仍是 Empty 不会进 Commit 主流程 ——
	//  但 Commit 内部检查的是 Composition.IsEmpty()，所以不会真的报错。需在 ARCHITECTURE.md 中提一下。）
	return Builder;
}

// 中文：Make 的 move 版本 —— 性能更好。
// 注意：SharedFragments 和 ConstSharedFragments 用 Append+Forward 而不是直接 = MoveTemp，
// 这是因为入参类型是 TArray<FSharedStruct> / TArray<FConstSharedStruct>，而成员是 TArray<FInstancedStruct>，
// 类型不同需要逐元素转换。
FEntityBuilder FEntityBuilder::Make(const TSharedRef<FMassEntityManager>& InEntityManager
		, const FMassArchetypeCompositionDescriptor& Composition
		, TArray<FInstancedStruct>&& InitialFragmentValues
		, TArray<FConstSharedStruct>&& ConstSharedFragments
		, TArray<FSharedStruct>&& SharedFragments)
{
	FEntityBuilder Builder(InEntityManager);

#if WITH_MASSENTITY_DEBUG
	if (Debug::bValidateEntityBuilderMakeInput)
	{
		ensureMsgf(Private::ValidateMakeInput(InEntityManager, Composition, InitialFragmentValues, ConstSharedFragments, SharedFragments)
			, TEXT("%hs: failed input validation. See log for details."), __FUNCTION__);
	}
#endif // WITH_MASSENTITY_DEBUG

	Builder.Composition = Composition;
	Builder.Fragments = InitialFragmentValues;
	Builder.SharedFragments.Append(Forward<TArray<FSharedStruct>>(SharedFragments));
	Builder.ConstSharedFragments.Append(Forward<TArray<FConstSharedStruct>>(ConstSharedFragments));

	return Builder;
}

// =============================================================================
// 中文：Commit —— 把 builder 配方落地为真实实体。流程在 .h 注释中已展开，这里逐步说明。
// =============================================================================
FMassEntityHandle FEntityBuilder::Commit()
{
	// @todo consider locking every builder instance to a single thread to prevent concurrent add/flush

	// 中文：步骤 0：状态保护。重复 Commit 不允许（避免错误地把同一个 handle 落地两次）。
	if (!testableEnsureMsgf(State != EState::Committed, TEXT("Trying to commit an already committed EntityBuilder. This request will be ignored.")))
	{
		return EntityHandle;
	}
	// 中文：步骤 1：空 composition 早期返回。也警告若 handle 已 reserve（说明用户 GetEntityHandle 后又忘了 Add）。
	if (Composition.IsEmpty())
	{
		UE_VLOG_UELOG(EntityManager->GetOwner(), LogMass, Warning, TEXT("%hs: Attempting to commit while no composition has been configured."), __FUNCTION__);
		UE_CVLOG_UELOG(EntityHandle.IsValid(), EntityManager->GetOwner(), LogMass, Error, TEXT("Failing to commit while the entity handle has already been reserved."));
		return FMassEntityHandle();
	}

	// 中文：步骤 2-4：三个 lazy cache 顺序填充。
	CacheEntityHandle();		// 拿/复用 reserved handle
	CacheSharedFragmentValue();	// 把 SharedFragments 落到全局 shared 池并排序
	CacheArchetypeHandle();		// 取/创建对应 archetype

	// 中文：步骤 5：分两条路径
	//   A) Mass 当前正在 processing → 必须 defer。EntityManager 暂不可变 (archetype 切换会破坏正在执行的 query)。
	//   B) 否则 → 立即同步建实体。
	if (EntityManager->IsProcessing())
	{
		// we need to issue commands in this case
		// 中文：lambda 通过值捕获所有必要数据；CreateEntityImpl 是 static 的，不依赖此 builder 实例的生命周期。
		EntityManager->Defer().PushCommand<FMassDeferredCreateCommand>(
					[ReservedEntityHandle = EntityHandle, SharedFragmentValues = CachedSharedFragmentValues
					, ArchetypeHandle = CachedArchetypeHandle, FragmentsCopy = Fragments
					, RelationsParams = RelationsParams](FMassEntityManager& Manager)
					{
						FEntityBuilder::CreateEntityImpl(Manager, ReservedEntityHandle, ArchetypeHandle
							, SharedFragmentValues, FragmentsCopy, RelationsParams);
					});
	}
	else
	{
		// directly create the entity, right now
		CreateEntityImpl(*EntityManager, EntityHandle, CachedArchetypeHandle, CachedSharedFragmentValues, Fragments, RelationsParams);
	}

	// 中文：步骤 6：状态切到 Committed。注意 EntityHandle 不重置（可继续被读取）。
	State = EState::Committed;

	return EntityHandle;
}

// =============================================================================
// 中文：CreateEntityImpl —— 实际"建实体"的静态工人函数。
// 它独立于具体 builder 实例，所以可以被 deferred command lambda 安全捕获。
//
// 重要副作用：
//   1) GetOrMakeCreationContext 会"挂起"observer 触发，直到 SetEntityFragmentValues 完成（避免 observer 看到半构造的实体）。
//   2) BuildEntity 把 reserved handle 关联到 archetype（实际"激活"实体）。
//   3) SetEntityFragmentValues 把 InstancedStruct 数组中的值写入对应 chunk 内存。
//   4) 遍历 RelationsParams 创建关系实例。Subject/Object 角色决定调用顺序：本 builder 是 Object 还是 Subject。
// =============================================================================
void FEntityBuilder::CreateEntityImpl(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const TConstArrayView<FInstancedStruct> Fragments, TConstArrayView<FPendingRelationParams> RelationsParams)
{
	// creating creation context to block observers from triggering until the
	// values are set with SetEntityFragmentValues. The lock gets auto-released at the end of the scope
	// or persists, if anyone else has a shared ptr to it
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.GetOrMakeCreationContext();

	EntityManager.BuildEntity(EntityHandle, ArchetypeHandle, SharedFragmentValues);
	EntityManager.SetEntityFragmentValues(EntityHandle, Fragments);
	for (const FPendingRelationParams& Relation : RelationsParams)
	{
		switch (Relation.OtherEntityRole)
		{
		case ERelationRole::Subject:
			// 中文：另一端是 Subject ⇒ 该 builder 实体是 Object ⇒ 调用约定 (RelationType, Subject=OtherEntity, Object=ThisEntity)
			EntityManager.GetRelationManager().CreateRelationInstance(Relation.RelationTypeHandle, Relation.OtherEntity, EntityHandle);
			break;
		case ERelationRole::Object:
			// 中文：另一端是 Object ⇒ 该 builder 实体是 Subject ⇒ 调用约定 (RelationType, Subject=ThisEntity, Object=OtherEntity)
			EntityManager.GetRelationManager().CreateRelationInstance(Relation.RelationTypeHandle, EntityHandle, Relation.OtherEntity);
			break;
		default:
			ensureMsgf(false, TEXT("Unhandled value %s"), *LexToString(Relation.OtherEntityRole));
		}
	}
}

// 中文：CommitAndReprepare = "Commit + 立刻为下一个实体准备好"，常用于循环造同种实体。
FMassEntityHandle FEntityBuilder::CommitAndReprepare()
{
	FMassEntityHandle CreatedEntity = Commit();
	Reprepare();
	return CreatedEntity;
}

// 中文：Reprepare —— 仅在 Committed 状态下生效。把 EntityHandle 清掉、回到 ReadyToCommit。
// Composition / Fragments / RelationsParams / Cached* 全部保留，从而下次 Commit 复用同一份配方。
void FEntityBuilder::Reprepare()
{
	if (ensureMsgf(State == EState::Committed, TEXT("Expected to be called only on Committed builders")))
	{
		EntityHandle.Reset();
		State = EState::ReadyToCommit;
	}
}

// 中文：Reset —— 完全重置回 Empty。可选释放 reserved handle。
// CopyDataFromEntity 内部会传 false 以保留 handle —— 因为它接下来要把 handle 关联到拷贝来的 composition。
void FEntityBuilder::Reset(const bool bReleaseEntityHandleIfReserved)
{
	if (bReleaseEntityHandleIfReserved)
	{
		ConditionallyReleaseEntityHandle();
	}

	if (State != EState::Empty)
	{
		InvalidateCachedData();

		State = EState::Empty;

		Composition.Reset();
		Fragments.Reset();
		SharedFragments.Reset();
		ConstSharedFragments.Reset();
		RelationsParams.Reset();
	}
}

// 中文：把"外部已 reserve 的 handle"绑到 builder。会先释放 builder 自带的 reserved handle（避免泄漏）。
// 必须在 Commit 之前调，否则会 checkf 失败。
bool FEntityBuilder::SetReservedEntityHandle(const FMassEntityHandle ReservedEntityHandle)
{
	if (!ensureMsgf(ReservedEntityHandle.IsValid() && EntityManager->IsEntityReserved(ReservedEntityHandle), TEXT("Input ReservedEntityHandle is expected to be valid and represent a reserved entity")))
	{
		return false;
	}

	if (EntityHandle.IsValid() && EntityManager->IsEntityReserved(EntityHandle))
	{
		checkf(IsCommitted() == false, TEXT("We only expect to be here when the entity builder has not been `Committed` yet"));
		EntityManager->ReleaseReservedEntity(EntityHandle);
	}

	EntityHandle = ReservedEntityHandle;
	return true;
}

// 中文：从源实体 *追加* 数据到当前 builder。
// 关键路径：
//   1) 校验源实体 IsEntityActive（必须已 build）；
//   2) 如果当前是 Empty 状态，直接走 CopyDataFromEntity（更高效，无需查重）；
//   3) 否则失效 cache，逐类别 AppendFromEntity（删旧再添新），最后 Composition.Append 合并位集。
bool FEntityBuilder::AppendDataFromEntity(const FMassEntityHandle SourceEntityHandle)
{
	if (!testableEnsureMsgf(EntityManager->IsEntityActive(SourceEntityHandle)
		, TEXT("%hs expecting a valid, built entity as input"), __FUNCTION__))
	{
		return false;
	}
	if (State == EState::Empty)
	{
		// copying is significantly more efficient (no lookups for existing data) 
		return CopyDataFromEntity(SourceEntityHandle);
	}

	InvalidateCachedData();

	const FMassArchetypeHandle ArchetypeHandle = EntityManager->GetArchetypeForEntity(SourceEntityHandle);
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager->GetArchetypeComposition(ArchetypeHandle);

	Private::FEntityBuilderHelper::AppendFromEntity<FMassFragment>(*this, SourceEntityHandle, ArchetypeComposition);
	Private::FEntityBuilderHelper::AppendFromEntity<FMassSharedFragment>(*this, SourceEntityHandle, ArchetypeComposition);
	Private::FEntityBuilderHelper::AppendFromEntity<FMassConstSharedFragment>(*this, SourceEntityHandle, ArchetypeComposition);

	Composition.Append(ArchetypeComposition);

	State = Composition.IsEmpty() ? EState::Empty : EState::ReadyToCommit;

	return true;
}

// 中文：从源实体 *拷贝* 数据 —— 先 Reset(false)，再逐类别 CopyFromEntity，最后 Composition = ArchetypeComposition。
// 注意：Reset(false) 保留 reserved handle，因为这个 builder 仍要 Commit 这份新配方。
bool FEntityBuilder::CopyDataFromEntity(const FMassEntityHandle SourceEntityHandle)
{
	if (!testableEnsureMsgf(EntityManager->IsEntityActive(SourceEntityHandle)
		, TEXT("%hs expecting a valid, built entity as input"), __FUNCTION__))
	{
		return false;
	}

	Reset(/*bReleaseEntityHandleIfReserved=*/false);
	
	const FMassArchetypeHandle ArchetypeHandle = EntityManager->GetArchetypeForEntity(SourceEntityHandle);
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager->GetArchetypeComposition(ArchetypeHandle);

	Private::FEntityBuilderHelper::CopyFromEntity<FMassFragment>(*this, SourceEntityHandle, ArchetypeComposition);
	Private::FEntityBuilderHelper::CopyFromEntity<FMassSharedFragment>(*this, SourceEntityHandle, ArchetypeComposition);
	Private::FEntityBuilderHelper::CopyFromEntity<FMassConstSharedFragment>(*this, SourceEntityHandle, ArchetypeComposition);

	Composition = ArchetypeComposition;

	State = Composition.IsEmpty() ? EState::Empty : EState::ReadyToCommit;

	return true;
}

// 中文：取实体 handle，必要时 reserve。const 但有副作用 —— 这是允许 Builder 隐式转 FMassEntityHandle 的关键。
FMassEntityHandle FEntityBuilder::GetEntityHandle() const
{
	CacheEntityHandle();
	return EntityHandle;
}

// 中文：仅当 reserved 但未 committed 时归还 handle 给 EntityManager。
// 调用点：~FEntityBuilder、Reset(true)、operator= 跨 manager 切换、SetReservedEntityHandle。
void FEntityBuilder::ConditionallyReleaseEntityHandle()
{
	if ((EntityHandle.IsValid() == true) && (State != EState::Committed))
	{
		EntityManager->ReleaseReservedEntity(EntityHandle);
	}

	EntityHandle.Reset();
}

// 中文：lazy 取 reserved handle。const 但写 mutable EntityHandle。
// 重要不变量：Committed 状态下不应再 reserve（应该已经有 handle 了），否则 checkf 触发。
void FEntityBuilder::CacheEntityHandle() const
{
	if (EntityHandle.IsValid() == false)
	{
		checkf(State != EState::Committed, TEXT("Reserving an entity while the builder has already committed. This should not happen. Indicates an error during builder copying from another instance."))
		EntityHandle = EntityManager->ReserveEntity();
	}
}

// 中文：lazy 取/创建 archetype。EntityManager.CreateArchetype 内部按 composition hash 复用已有 archetype。
// ArchetypeCreationParams 仅在新创建 archetype 时生效（已存在时被忽略）。
void FEntityBuilder::CacheArchetypeHandle()
{
	if (CachedArchetypeHandle.IsValid() == false)
	{
		CachedArchetypeHandle = EntityManager->CreateArchetype(Composition, ArchetypeCreationParams);
	}
}

// 中文：清空两个 lazy cache。被 Reset / AppendDataFromEntity 等场景调用。
void FEntityBuilder::InvalidateCachedData()
{
	CachedArchetypeHandle = {};
	CachedSharedFragmentValues.Reset();
}

FMassArchetypeHandle FEntityBuilder::GetArchetypeHandle()
{
	CacheArchetypeHandle();
	return CachedArchetypeHandle;
}

// =============================================================================
// 中文：CacheSharedFragmentValue —— 把 SharedFragments / ConstSharedFragments 这两个 InstancedStruct 数组
// 落到 EntityManager 的全局 shared 池，得到对应的 FSharedStruct / FConstSharedStruct，
// 然后填充到 CachedSharedFragmentValues 中并 Sort（排序保证 archetype 内 chunk hash 一致性）。
//
// 幂等：如果 CachedSharedFragmentValues 已非空（说明已计算过），跳过。
// 任何修改 SharedFragments/ConstSharedFragments 的操作（GetInstancedStructContainerInternal）会清空 cache。
// =============================================================================
void FEntityBuilder::CacheSharedFragmentValue()
{
	if (CachedSharedFragmentValues.IsEmpty())
	{
		for (FInstancedStruct& SharedFragmentInstance : SharedFragments)
		{
			check(SharedFragmentInstance.IsValid());
			const FSharedStruct& SharedStruct = EntityManager->GetOrCreateSharedFragment(*SharedFragmentInstance.GetScriptStruct(), SharedFragmentInstance.GetMemory());
			CachedSharedFragmentValues.Add(SharedStruct);
		}
		for (FInstancedStruct& ConstSharedFragmentInstance : ConstSharedFragments)
		{
			check(ConstSharedFragmentInstance.IsValid());
			const FConstSharedStruct& ConstSharedStruct = EntityManager->GetOrCreateConstSharedFragment(*ConstSharedFragmentInstance.GetScriptStruct(), ConstSharedFragmentInstance.GetMemory());
			CachedSharedFragmentValues.Add(ConstSharedStruct);
		}

		CachedSharedFragmentValues.Sort();
	}
}

// =============================================================================
// 中文：HandleTypeInstance —— FInstancedStruct 路径的"类型路由"helper。
// 给定 Type，按 T (FMassFragment / FMassSharedFragment / FMassConstSharedFragment) 测试 IsA：
//   - 命中：在 Composition 对应位集中标位（若已标位则置 bAlreadyInComposition），并填 OutTargetContainer 指向该容器；
//   - 未命中：返回 false，让 caller 试下一个 T。
// 该函数被 AddInternal 用于"短路链"：只有第一个匹配的 T 会被处理。
// =============================================================================
template<typename T>
bool FEntityBuilder::HandleTypeInstance(const UScriptStruct* Type, TArray<FInstancedStruct>*& OutTargetContainer, bool& bOutAlreadyInComposition)
{
	if (UE::Mass::IsA<T>(Type))
	{
		auto& BitSet = Composition.GetContainer<T>();
		const int32 TypeIndex = BitSet.GetTypeIndex(*Type);
		OutTargetContainer = &GetInstancedStructContainerInternal<T>();
		if (BitSet.IsBitSet(TypeIndex) == false)
		{
			BitSet.AddAtIndex(TypeIndex);
		}
		else
		{
			bOutAlreadyInComposition = true;
		}
		return true;
	}
	return false;
}

// 中文：AddInternal —— Add(FInstancedStruct) 的实际实现。
// 注意：&& 链式短路 —— 第一个 HandleTypeInstance 返回 true 就停。这要求三类元素类型集合互斥（不能同时是 fragment + shared）。
//
// 后处理逻辑：
//   - 若 bAlreadyInComposition && 容器中找得到旧实例 → 用 forward 后的新实例覆盖；
//   - 否则 Add 一个新实例（也覆盖 bAlreadyInComposition==true 但容器中尚无 instance 的情况，
//     即先 Add(UScriptStruct*) 再 Add(FInstancedStruct) 的混合用法）。
template<typename TInstancedStruct>
FEntityBuilder& FEntityBuilder::AddInternal(TInstancedStruct&& ElementInstance)
{
	if (const UScriptStruct* Type = ElementInstance.GetScriptStruct())
	{
		TArray<FInstancedStruct>* TargetContainer = nullptr;

		bool bAlreadyInComposition = false;
		// the following chain will stop at the first successful HandleTypeInstance call.
		// This is by design, since we don't support there being any overlap between the types.
		HandleTypeInstance<FMassFragment>(Type, TargetContainer, bAlreadyInComposition)
		|| HandleTypeInstance<FMassSharedFragment>(Type, TargetContainer, bAlreadyInComposition)
		|| HandleTypeInstance<FMassConstSharedFragment>(Type, TargetContainer, bAlreadyInComposition);

		if (TargetContainer)
		{
			auto* FoundElement = bAlreadyInComposition
				? TargetContainer->FindByPredicate(FStructInstanceFindingPredicate{Type})
				: nullptr;

			// note that it's perfectly fine for FoundElement being null while bAlreadyInComposition == true.
			// This will happen if the element type has been added just as a type first (which only annotates the bitset)
			// and then an instance of the same type gets added - the bitset already contains the bit, but
			// the container doesn't hold an instance yet.
			if (FoundElement)
			{
				*FoundElement = Forward<TInstancedStruct>(ElementInstance);
			}
			else
			{
				TargetContainer->Add(Forward<TInstancedStruct>(ElementInstance));
			}
		}
	}

	return *this;
}

// 中文：Add(InstancedStruct) 的两个对外重载 —— const 引用 / 右值引用，统一转发到 AddInternal。
FEntityBuilder& FEntityBuilder::Add(const FInstancedStruct& ElementInstance)
{
	return AddInternal(ElementInstance);
}

FEntityBuilder& FEntityBuilder::Add(FInstancedStruct&& ElementInstance)
{
	return AddInternal(ElementInstance);
}

// 中文：Add(UScriptStruct*) —— 仅按"类型"添加到 Composition，不携带 instance 数据。
// 路径：按继承自 FMassTag / FMassFragment / FMassSharedFragment / FMassConstSharedFragment / FMassChunkFragment 派发。
// 最后强制进入 ReadyToCommit 并失效 archetype cache。
//
// 副作用：注意 Tag / ChunkFragment 永远没有 instance 数据，Fragment / SharedFragment 也可以"只 mark 不赋值"，
// 此时实体 build 后该 fragment 用默认构造值。
FEntityBuilder& FEntityBuilder::Add(TNotNull<const UScriptStruct*> ElementType)
{
	if (UE::Mass::IsA<FMassTag>(ElementType))
	{
		Composition.GetTags().Add(*ElementType);
	}
	else if (UE::Mass::IsA<FMassFragment>(ElementType))
	{
		Composition.GetFragments().Add(*ElementType);
	}
	else if (UE::Mass::IsA<FMassSharedFragment>(ElementType))
	{
		Composition.GetSharedFragments().Add(*ElementType);
	}
	else if (UE::Mass::IsA<FMassConstSharedFragment>(ElementType))
	{
		Composition.GetConstSharedFragments().Add(*ElementType);
	}
	else if (ensureMsgf(UE::Mass::IsA<FMassChunkFragment>(ElementType)
		, TEXT("Unhandled element type %s"), *ElementType->GetName()))
	{
		Composition.GetChunkFragments().Add(*ElementType);
	}

	State = EState::ReadyToCommit;
	CachedArchetypeHandle = FMassArchetypeHandle();
	
	return *this;
}

// 中文：登记一条 pending relation。无任何校验、无去重 —— Commit 时会按顺序创建。
FEntityBuilder& FEntityBuilder::AddRelation(const FTypeHandle RelationTypeHandle, const FMassEntityHandle OtherEntity, const ERelationRole InputEntityRole)
{
	RelationsParams.Add({RelationTypeHandle, OtherEntity, InputEntityRole});
	return *this;
}

// 中文：稳定的"反向遍历删除"模式 —— 倒序避免索引失效。Operator 返回 false ⇒ RemoveAt（不缩容）。
void FEntityBuilder::ForEachRelation(const TFunctionRef<bool(FPendingRelationParams&)>& Operator)
{
	for (int32 ElementIndex = RelationsParams.Num() - 1; ElementIndex >= 0; --ElementIndex)
	{
		if (Operator(RelationsParams[ElementIndex]) == false)
		{
			RelationsParams.RemoveAt(ElementIndex, EAllowShrinking::No);
		}
	}
}

} // namespace UE::Mass