// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassArchetypeTypes.h"
#include "MassEntityConcepts.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "MassEntityRelations.h"
#include "MassTypeManager.h"

#define UE_API MASSENTITY_API

struct FMassEntityManager;

namespace UE::Mass
{
	struct FTypeHandle;
	namespace Private
	{
		// 内部 helper，封装从已有实体抽取数据用的模板函数（AppendFromEntity / CopyFromEntity）。
		// 之所以放在 Private 命名空间内，是因为它需要直接访问 FEntityBuilder 的私有容器接口，
		// 但又不希望污染 FEntityBuilder 的公开 API。
		struct FEntityBuilderHelper;
	}

/**
 * FEntityBuilder is a utility struct that provides a convenient way to create and configure entities
 * in the Mass framework. It bridges multiple APIs from FMassEntityManager, MassSpawnerSubsystem,
 * MassEntityTemplates, and other related components, allowing for streamlined entity creation and configuration.
 *
 * Key Features:
 * - Can be seamlessly used in place of FMassEntityHandle, allowing for consistent and intuitive usage.
 * - An entity only gets created once Commit() is called
 * - Copyable, but copied instances represent new entities without carrying over the reserved entity handle.
 *
 * Example Usage:
 * {
 * 		FEntityBuilder Builder(EntityManager);
 * 		Builder.Add<FTransformFragment>(FTransform(FVector(100, 200, 300)))
 * 			.Commit();	// the entity gets reserved and built by this call
 * } 
 *
 * {
 *     FEntityBuilder Builder(EntityManager);
 *     FMassEntityHandle ReservedEntity = Builder; // Entity handle reserved, can be used for commands.
 *     Builder.Add_GetRef<FTransformFragment>().GetMutableTransform().SetTranslation(FVector(100, 200, 300));
 *     Builder.Commit(); // Entity creation is finalized at this point.
 * }
 *
 * // Example of chaining with FMassEntityManager's MakeEntityBuilder() method:
 * FMassEntityHandle NewEntity = EntityManager.MakeEntityBuilder()
 *     .Add<FMassStaticRepresentationTag>()
 *     .Add<FTransformFragment>()
 *     .Add<FAgentRadiusFragment>(FAgentRadiusFragment{ .Radius = 35.f})
 *     .Add<FMassVelocityFragment>()
 *     .Commit();
 *
 * Current Limitations:
 * - Committing entities while Mass's processing is in progress is not yet supported; this functionality will be implemented in the near future.
 * - no support for entity grouping
 */
//===========================================================================
// 【中文整体说明】FEntityBuilder —— 单实体构造的"流式 API 入口"
//---------------------------------------------------------------------------
// 【为什么需要 Builder？】
//   直接通过 FMassEntityManager 创建实体需要分为以下三步（且步骤之间高度耦合）：
//     1) 准备 FMassArchetypeCompositionDescriptor（fragment / tag / shared / chunk fragment 的位集）
//     2) 调用 EntityManager.CreateArchetype(Composition) 得到 FMassArchetypeHandle
//     3) 准备 FMassArchetypeSharedFragmentValues（每个 shared fragment 的具体取值）
//        然后调用 EntityManager.CreateEntity(ArchetypeHandle, SharedValues, FragmentInstances)
//   FEntityBuilder 把这三步全部"流式化 + 延迟化"，使用者只需：
//     Builder.Add<FFragA>().Add<FFragB>(args).Commit();
//   即可。Archetype 与 SharedFragmentValues 的构造在 Commit() 时按需懒计算（lazy）。
//
// 【状态机（EState）】
//                 Add* / AddRelation / Configure*
//      Empty  ───────────────────────────────────►  ReadyToCommit
//        ▲                                             │
//        │ Reset(true)                                  │ Commit()
//        │                                              ▼
//        │                                          Committed
//        │                                              │
//        │                                              │ Reprepare()
//        │                                              ▼
//        │                                          ReadyToCommit
//        │
//      Invalid  ◄── 析构后被 move-assign 的源实例
//   不同状态可调用的方法：
//     - Empty       : Add / Make / CopyDataFromEntity / SetReservedEntityHandle
//     - ReadyToCommit: Add / Commit / CommitAndReprepare / AppendDataFromEntity / Reset
//     - Committed   : Reprepare / GetEntityHandle（只读）
//     - Invalid     : 仅可被 move-赋值复活（行为类似已 moved-from）
//
// 【关键设计：Lazy 缓存】
//   - CachedSharedFragmentValues：在 Commit() 时根据 SharedFragments / ConstSharedFragments
//     调 GetOrCreateSharedFragment(...) 实例化共享段并排序后填入。
//   - CachedArchetypeHandle：在 Commit() 时调 EntityManager.CreateArchetype(Composition) 得到。
//   - 只要再调任何 Add* 修改 Composition，缓存就会被清空（CachedArchetypeHandle = {}）。
//   - 这样保证：用户先 Add 再 Commit 的工作流中，Archetype 不会被错误命中已经过时的缓存。
//
// 【隐式转换为 FMassEntityHandle】
//   GetEntityHandle() 是 const，调用时会通过 CacheEntityHandle() 调 ReserveEntity 拿一个保留 handle。
//   因此可以把 Builder 直接当 FMassEntityHandle 使（用于异步/延迟命令绑定到该未来实体）。
//   未 Commit 直接析构时，析构函数会 ReleaseReservedEntity，避免 handle 泄漏。
//
// 【与 EntityManager.IsProcessing() 的关系】
//   Commit() 内部判断：
//     - 若 EntityManager 当前正在 processing（比如某个 Processor 正在 Execute），
//       则 push 一个 FMassDeferredCreateCommand，等下一次 FlushCommands 再真正建实体；
//     - 否则直接同步建实体（创建上下文锁定 observer 直到 SetEntityFragmentValues 完成）。
//===========================================================================
struct FEntityBuilder
{
	/** Constructs a FEntityBuilder using a reference to a FMassEntityManager. */
	// 中文：以 raw 引用形式持有 EntityManager 的入口；内部会自动调 AsShared() 升级为共享指针，
	// 因此 EntityManager 必须已经被某个 TSharedRef 拥有（典型情况：World subsystem 已构造）。
	UE_API explicit FEntityBuilder(FMassEntityManager& InEntityManager);

	/** Constructs a FEntityBuilder using a shared reference to a FMassEntityManager. */
	// 中文：推荐用法。直接以 TSharedRef 持有 EntityManager，强保证生命周期。
	UE_API explicit FEntityBuilder(const TSharedRef<FMassEntityManager>& InEntityManager);

	/** Copy constructor - copies-create a new instance that represents a new entity and does not carry over reserved handle. */
	// 中文：拷贝构造 = "再造一个一样的实体定义"，并不复制保留的 EntityHandle（每次都会去 reserve 新的）。
	// 这是有意为之的语义：Builder 是"实体配方"，不是"实体本身"。
	UE_API FEntityBuilder(const FEntityBuilder& Other);

	/** Assignment operator - copies represent new entities, with no carryover of reserved handle from the original. */
	// 中文：拷贝赋值同上 — 配方相同但实体不同。如果 this 已经 reserve 了 handle，会复用（避免反复 reserve/release）。
	UE_API FEntityBuilder& operator=(const FEntityBuilder& Other);

	/** Move assignment operator - moves over all the data from Other, including the internal state (like whether the entity handle has already been reserved) */
	// 中文：与拷贝不同，move 赋值会接管 Other 的状态，包括 reserved handle 与 EState。Other 移交后变为 Invalid。
	UE_API FEntityBuilder& operator=(FEntityBuilder&& Other);

	/** Destructor - automatically commits entity creation if not explicitly aborted or committed beforehand. */
	// 中文：析构会在 reserved 但未 commit 时调 ReleaseReservedEntity 把 handle 还回 manager（防泄漏）。
	// 注意：基础 FEntityBuilder 的析构 *不会* 自动 Commit；自动 Commit 是 FScopedEntityBuilder 的语义。
	UE_API ~FEntityBuilder();

	/** Creates an instance of FEntityBuilder and populates it with provided data */
	// 中文：从已有 Composition + 初值数组直接造一个 ReadyToCommit 状态的 Builder，常用于"按模板批量造实体"的场景。
	// debug 模式下（mass.debug.ValidateEntityBuilderMakeInput）会校验提供的 InstancedStruct 与 Composition bitset 一致。
	static UE_API FEntityBuilder Make(const TSharedRef<FMassEntityManager>& InEntityManager
		, const FMassArchetypeCompositionDescriptor& Composition
		, TConstArrayView<FInstancedStruct> InitialFragmentValues = {}
		, TConstArrayView<FConstSharedStruct> ConstSharedFragments = {}
		, TConstArrayView<FSharedStruct> SharedFragments = {});

	/** Creates an instance of FEntityBuilder and populates it with provided data, using move-semantics on said data */
	// 中文：移动语义版的 Make，避免拷贝大量 InstancedStruct。
	static UE_API FEntityBuilder Make(const TSharedRef<FMassEntityManager>& InEntityManager
		, const FMassArchetypeCompositionDescriptor& Composition
		, TArray<FInstancedStruct>&& InitialFragmentValues
		, TArray<FConstSharedStruct>&& ConstSharedFragments
		, TArray<FSharedStruct>&& SharedFragments);

	/**
	 * Finalizes the creation of the entity with the specified fragments and configurations.
	 * Note that this function needs to be called manually, no automated entity creation will take place upon builder's destruction. 
	 */
	// 中文：将"配方"落地为真实实体。流程：
	//   1) 状态校验：必须不是 Committed；Composition 不能为空。
	//   2) CacheEntityHandle()：若没 reserve 过 handle，调 EntityManager.ReserveEntity() 拿一个。
	//   3) CacheSharedFragmentValue()：把 SharedFragments / ConstSharedFragments 落到全局 shared-fragment 池。
	//   4) CacheArchetypeHandle()：调 CreateArchetype 拿 archetype。
	//   5) 若 IsProcessing()→ push FMassDeferredCreateCommand 延后；否则同步 BuildEntity + SetEntityFragmentValues + 创建 relations。
	//   6) State = Committed。
	// 返回值：刚创建（或保留待延迟创建）的实体 handle。
	UE_API FMassEntityHandle Commit();

	/**
	 * A wrapper for "Commit" call that, once that's done, prepares the builder for another commit, forgetting the
	 * handle for the entity just created, and reverting the state back to "ReadyToCommit"
	 * @see Commit
	 */
	// 中文：用于"按同一配方连续造多个实体"的便利方法。
	// 等价于 Commit() 后立刻 Reprepare()，Composition / Fragments / RelationsParams 全部保留。
	UE_API FMassEntityHandle CommitAndReprepare();

	/** if the builder is in "Committed" state it will roll back to ReadyToSubmit and reset the stored entity handle */
	// 中文：仅在 Committed 状态下生效；把 EntityHandle 清掉、状态回退到 ReadyToCommit，方便用同一份 Composition 再造一个。
	UE_API void Reprepare();

	/**
	 * Resets the builder to its initial state, discarding all previous entity configurations.
	 * @param bReleaseEntityHandleIfReserved configures what to do with the reserved entity handle, if it's valid.
	 */
	// 中文：重置回 Empty 状态。bReleaseEntityHandleIfReserved=true 时会归还 reserved handle；
	// 注意：内部 CopyDataFromEntity 调用本函数时会传 false，因为它要保留已 reserve 的 handle 用于后续 commit。
	UE_API void Reset(const bool bReleaseEntityHandleIfReserved = true);

	/**
	 * Stores ReservedEntityHandle as the cached EntityHandle. The ReservedEntityHandle is expected to be valid
	 * and represent a reserved entity. These expectations will be checked via ensures.
	 * If the existing EntityHandle also represents a valid, reserved entity, that handle will be released.
	 * @return whether the ReservedEntityHandle has been stored.
	 */
	// 中文：外部已经 ReserveEntity 拿到一个 handle 了，把它"绑定"到这个 builder 上。
	// 用法场景：先发出基于该 handle 的 deferred command，再用 builder 配置 fragments 并 Commit，使 command 落到正确实体上。
	UE_API bool SetReservedEntityHandle(const FMassEntityHandle ReservedEntityHandle);

	/**
	 * Appends all element types and values stored by the entity indicated by SourceEntityHandle.
	 * @param SourceEntityHandle valid handle for a fully constructed, built entity.
	 * @return whether the operation was successful
	 */
	// 中文：从已存在的源实体抽取 Fragment / Shared / ConstShared 数据"追加"到当前 builder。
	// "追加"语义：现有 builder 中已有的同类型元素会被源实体的值覆盖（先 RemoveAtSwap 再 Add）。
	// 当 builder 处于 Empty 时，自动转为更高效的 CopyDataFromEntity。
	UE_API bool AppendDataFromEntity(const FMassEntityHandle SourceEntityHandle);

	/**
	 * Copies all element types and values stored by the entity indicated by SourceEntityHandle. Any existing builder data will be overridden
	 * @param SourceEntityHandle valid handle for a fully constructed, built entity.
	 * @return whether the operation was successful
	 */
	// 中文：与 Append 不同，Copy 会先 Reset(false) 清空当前 builder（保留已 reserve 的 handle），
	// 然后把源实体的全部 element 直接拷贝过来。比 Append 高效（无需查重）。
	UE_API bool CopyDataFromEntity(const FMassEntityHandle SourceEntityHandle);

	/**
	 * Adds a fragment of type T to the entity and returns a reference to it, constructing it with the provided arguments.
	 * The function will assert if an element of type T already exists.
	 * @param T - The type of fragment to add.
	 * @param InArgs - Constructor arguments for initializing the fragment.
	 * @return A reference to the added fragment.
	 */
	// 中文：添加一个新 fragment 实例并返回可变引用，可链式做 .GetMutableTransform().Set... 之类操作。
	// 若 T 已存在则 ensure 失败（行为：返回已存在那份的引用，但日志会报错）。要"覆盖"语义请用 GetOrCreate。
	// 模板约束：T 必须是 fragment / shared / const shared，不允许 Tag / ChunkFragment（这两者无 instance 数据）。
	template<typename T, typename... TArgs>
	T& Add_GetRef(TArgs&&... InArgs) requires (CElement<T> && !(CTag<T> || CChunkFragment<T>));

	/**
	 * Adds a fragment of type T to the entity and returns a reference to it, constructing it with the provided arguments.
	 * If a fragment of the given type already exists then it will be overriden and its reference returned.
	 * @return A reference to the added fragment.
	 */
	// 中文：与 Add_GetRef 唯一差异：若 T 已存在，则用新构造的值"覆盖"旧值（不报错）。
	template<typename T, typename... TArgs>
	T& GetOrCreate(TArgs&&... InArgs) requires (CElement<T> && !(CTag<T> || CChunkFragment<T>));

	/**
	 * Adds a tag of type T to the entity.
	 * @return Reference to this FEntityBuilder for method chaining.
	 */
	// 中文：Tag 没有数据，只在 Composition.Tags bitset 设位即可。返回 *this 支持流式链式。
	template<CTag T>
	FEntityBuilder& Add();

	/**
	 * Adds a chunk fragment of type T to the entity.
	 * @return Reference to this FEntityBuilder for method chaining.
	 */
	// 中文：Chunk fragment 是 archetype-chunk 级别的元数据，每个 chunk 一份；与单实体无关，
	// 故无 instance 值，只需在 Composition 中 mark。
	template<CChunkFragment T>
	FEntityBuilder& Add();

	/**
	 * Adds a fragment of type T to the entity, constructing it with the provided arguments.
	 * @return Reference to this FEntityBuilder for method chaining.
	 */
	// 中文：Add 的"链式"版本，内部直接调 Add_GetRef<T>(InArgs...)，丢弃返回引用。
	template<typename T, typename... TArgs>
	FEntityBuilder& Add(TArgs&&... InArgs) requires (CElement<T> && !(CTag<T> || CChunkFragment<T>));

	/**
	 * Adds a fragment instance to the Entity Builder, treating the contents according to its type
	 */
	// 中文：基于 FInstancedStruct 的"运行时类型分派"添加方式 — 适合读 DataAsset / 反序列化场景。
	// 内部 AddInternal 会按 IsA<FMassFragment/Shared/ConstShared> 顺序找对应 container。
	UE_API FEntityBuilder& Add(const FInstancedStruct& ElementInstance);
	UE_API FEntityBuilder& Add(FInstancedStruct&& ElementInstance);

	/** Adds the ElementType to the target archetype's composition */
	// 中文：仅按"类型"添加到 Composition，不携带 instance 值（适合 Tag / ChunkFragment / 默认值就够用的 fragment）。
	UE_API FEntityBuilder& Add(TNotNull<const UScriptStruct*> ElementType);

	/** type used to store parameters for relations to be created once the target entity is created. */
	// 中文：Relations 系统的 pending 记录。
	// EntityBuilder 在 Commit 之前并不实际创建关系，只缓存参数；Commit 时一次性把这些 relation 通过
	// EntityManager.GetRelationManager().CreateRelationInstance 落地。
	struct FPendingRelationParams
	{
		UE::Mass::FTypeHandle RelationTypeHandle;	// 中文：关系类型句柄（来自 TypeManager）
		FMassEntityHandle OtherEntity;				// 中文：关系另一端的实体
		Relations::ERelationRole OtherEntityRole;	// 中文：另一端在该关系中的角色（Subject 或 Object）
	};

	/**
	 * Adds information about a specific relation instance to be added once the entity gets created.
	 * The function will simply store the information without any checks for validity or duplication.
	 * If you want to override existing relation data call ForEachRelation.
	 */
	// 中文：登记一条待创建关系。注意：不做去重 / 有效性校验，要修改/删除请用 ForEachRelation 遍历。
	UE_API FEntityBuilder& AddRelation(UE::Mass::FTypeHandle RelationTypeHandle, FMassEntityHandle OtherEntity, Relations::ERelationRole InputEntityRole = Relations::ERelationRole::Object);

	/** templated helper function for calling the other AddRelation function */
	// 中文：模板版本：根据 T (满足 CRelation 概念) 的 StaticStruct 自动取出 FTypeHandle，再调上面的 AddRelation。
	template<UE::Mass::CRelation T>
	FEntityBuilder& AddRelation(FMassEntityHandle OtherEntity, Relations::ERelationRole InputEntityRole = Relations::ERelationRole::Object);

	/**
	 * Calls the provided Operator function for every stored pending relation data instance.
	 * The return value of the Operator is used to determine whether the relation data instance
	 * should be kept (meaning: return `false` for each element you want to remove).
	 * The potential element removal is stable.
	 */
	// 中文：倒序遍历 RelationsParams，Operator 返回 false 即 RemoveAt（稳定）。可以同时修改 element。
	UE_API void ForEachRelation(const TFunctionRef<bool(FPendingRelationParams&)>& Operator);

	/**
	 * Finds and retrieves a pointer to a fragment of type T if it exists.
	 * @param T - The type of fragment to find.
	 * @return Pointer to the fragment, or nullptr if it does not exist.
	 */
	// 中文：在 builder 当前已添加的元素中查找 T 实例并返回可变指针。Composition 与 InstancedStruct 数组双向校验。
	template<typename T>
	T* Find() requires (CElement<T> && !(CTag<T> || CChunkFragment<T>));

	/**
	 * Advanced functionality. Can be used to provide additional parameters that will be used to
	 * create the entity's target archetype. Note that these parameters will take effect only if
	 * the target archetype doesn't exist yet.
	 */
	// 中文：高级用法：传 FMassArchetypeCreationParams（如 ChunkSize、debug 名等）。
	// 仅在 archetype 还不存在时生效（CreateArchetype 内部按 composition hash 查找）。
	void ConfigureArchetypeCreation(const FMassArchetypeCreationParams& InCreationParams);

	/** Converts the builder to a FMassEntityHandle, reserving the entity handle if not already committed. */
	// 中文：const 但有副作用！会延迟 reserve 一个实体 handle（mutable EntityHandle）。
	// 正是这个函数让 FEntityBuilder 可以隐式当作 FMassEntityHandle 用 — 触发的瞬间就拿到了 handle。
	[[nodiscard]] UE_API FMassEntityHandle GetEntityHandle() const;

	// 中文：取（必要时创建并缓存）目标 archetype 的 handle。一次调用后，CachedArchetypeHandle 被填充。
	[[nodiscard]] UE_API FMassArchetypeHandle GetArchetypeHandle();

	/** Checks whether the builder is in a valid, expected state */
	// 中文：状态机健康检查 — 仅当 EState != Invalid 时返回 true。move-from 后的实例为 Invalid。
	bool IsValid() const;

	/** @return whether the builder has an entity handle reserved and the data has not been committed yet */
	// 中文：是否处于"已 reserve handle 但未 Commit"中间态（既不是 Empty 也不是 Committed）。
	bool HasReservedEntityHandle() const;

	/** @return whether the builder has already committed the data */
	bool IsCommitted() const;

	/** @return the EntityManager instance this entity builder is working for */
	TSharedRef<FMassEntityManager> GetEntityManager();

protected:
	friend Private::FEntityBuilderHelper;

	// 中文：以下三个 Cache* 是状态机的"懒计算 + 失效"内部接口。详见 .cpp 注释。
	UE_API void CacheSharedFragmentValue();
	UE_API void CacheArchetypeHandle();
	UE_API void InvalidateCachedData();

	// 中文：在 InstancedStruct 数组中按 ScriptStruct 类型查找的 predicate（FindByPredicate 配合用）。
	struct FStructInstanceFindingPredicate
	{
		template<typename TInstancedStruct>
		bool operator()(const TInstancedStruct& Instance) const
		{
			return Instance.GetScriptStruct() == SearchedType;
		}
		const UScriptStruct* SearchedType = nullptr;
	};

private:
	template<typename TInstancedStruct>
	FEntityBuilder& AddInternal(TInstancedStruct&& ElementInstance);

	template<typename T>
	bool HandleTypeInstance(const UScriptStruct* Type, TArray<FInstancedStruct>*& OutTargetContainer, bool& bOutAlreadyInComposition);

	// 中文：持有的 EntityManager（共享所有权）。Builder 的所有操作都基于它。
	TSharedRef<FMassEntityManager> EntityManager;
	// 中文：mutable —— 允许 const GetEntityHandle() 触发 lazy ReserveEntity。
	mutable FMassEntityHandle EntityHandle;

	// 中文：实体 archetype 的 *逻辑* 描述（fragments / tags / shared / const shared / chunk fragments 的位集）。
	// 这是 builder 的核心状态。
	FMassArchetypeCompositionDescriptor Composition;

	// 中文：Lazy 缓存：CacheSharedFragmentValue 计算的实际共享段值集合（已排序）。
	FMassArchetypeSharedFragmentValues CachedSharedFragmentValues;
	// 中文：Lazy 缓存：CacheArchetypeHandle 拿到的 archetype handle。任何 Add 改 Composition 都会清空它。
	FMassArchetypeHandle CachedArchetypeHandle;

	/** stores optional FMassArchetypeCreationParams, that will be used if the target archetype doesn't exist yet */
	// 中文：见 ConfigureArchetypeCreation 注释。
	FMassArchetypeCreationParams ArchetypeCreationParams;

	// 中文：以下三个模板特化是"按元素类型路由到对应 InstancedStruct 容器"的派发表。
	// CFragment → Fragments；CSharedFragment → SharedFragments（同时清缓存）；CConstSharedFragment 同。
	// 之所以 SharedFragment 容器 getter 会清 CachedSharedFragmentValues，
	// 是因为这个 getter 总是被以"修改"意图调用（addElement），缓存必然脏。
	template<CFragment T> 
	TArray<FInstancedStruct>& GetInstancedStructContainerInternal() 
	{ 
		return Fragments; 
	}

	template<CSharedFragment T> 
	TArray<FInstancedStruct>& GetInstancedStructContainerInternal() 
	{
		// Resetting the cached shared values because this function is always called 
		// with the intent to modify the contents of SharedFragments, invalidating the
		// cached data anyway 
		CachedSharedFragmentValues.Reset();
		return SharedFragments; 
	}

	template<CConstSharedFragment T> 
	TArray<FInstancedStruct>& GetInstancedStructContainerInternal() 
	{
		CachedSharedFragmentValues.Reset();
		return ConstSharedFragments; 
	}

	// 中文：根据元素 trait T 取对应 bitset builder（Composition.Tags / Fragments / SharedFragments...）。
	template<typename T>
	auto& GetBitSetBuilder()
	{
		return Composition.GetContainer<T>();
	}

	/** Releases reserved handle if it has not been committed yet */
	// 中文：如果当前 handle 是 reserved 且 *未* committed，调 ReleaseReservedEntity 还回去，并清空 handle。
	// 析构、Reset(true)、跨 EntityManager 复制赋值时会用到。
	void ConditionallyReleaseEntityHandle();

	void CacheEntityHandle() const;

	// 中文：实际"建实体"的私有静态实现。被 Commit() 内联调用 *或* 被 deferred command lambda 捕获后调用。
	// 流程：取 CreationContext 锁住 observer → BuildEntity → SetEntityFragmentValues → 创建 relations。
	static void CreateEntityImpl(FMassEntityManager& EntityManager, FMassEntityHandle EntityHandle, const FMassArchetypeHandle& ArchetypeHandle
		, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FInstancedStruct> Fragments, TConstArrayView<FPendingRelationParams> RelationsParams);

	// 中文：三个不同语义的元素值容器。它们与 Composition bitset 是"双轨"——bitset 决定 archetype，instance 数组决定具体值。
	// 注意 Fragments 数组中的元素数量可能 ≤ Composition.Fragments 的 set bit 数（仅 Add(UScriptStruct*) 不带 instance 时不会进数组）。
	TArray<FInstancedStruct> Fragments;
	TArray<FInstancedStruct> SharedFragments;
	TArray<FInstancedStruct> ConstSharedFragments;
	TArray<FPendingRelationParams> RelationsParams;

	// 中文：见文件顶部的状态机说明。
	enum class EState : uint8
	{
		Empty,			// 中文：未配置，无 reserved handle（也可能有，因为 SetReservedEntityHandle 被先调）。
		ReadyToCommit,	// 中文：至少添加过一个元素，可以 Commit。
		Committed,		// 中文：已经成功 Commit，EntityHandle 指向真实实体；只能 Reprepare 或读。
		Invalid,		// 中文：被 move-from 或不可恢复错误，仅可被赋值复活。
	};
	EState State = EState::Empty;
};

//===========================================================================
// 【中文整体说明】FScopedEntityBuilder —— RAII 自动 Commit 包装
//---------------------------------------------------------------------------
// 用法：
//   {
//       UE::Mass::FScopedEntityBuilder Builder(EntityManager);
//       Builder.Add<FFragA>().Add<FFragB>(args);
//       // 此处可继续操作；析构时自动 Commit
//   }
// 与 FEntityBuilder 的唯一区别：析构时自动调用 Commit()（基类析构仅释放 reserved handle）。
// UE_NONCOPYABLE 阻止拷贝以避免双重 commit。
//===========================================================================
struct FScopedEntityBuilder : FEntityBuilder
{
	UE_NONCOPYABLE(FScopedEntityBuilder);

	template<typename... TArgs>
	FScopedEntityBuilder(TArgs&&... InArgs)
		: FEntityBuilder(Forward<TArgs>(InArgs)...)
	{	
	}

	~FScopedEntityBuilder()
	{
		// 中文：RAII 关键点 —— 离开 scope 时强制 Commit。如果 Composition 仍为空，Commit 内部会日志告警并返回空 handle。
		Commit();
	}
};

//-----------------------------------------------------------------------------
// Inlines and specializations
//-----------------------------------------------------------------------------
// 中文：Tag 特化 —— Tag 无 instance 数据，只在 Composition.Tags bitset 中标位即可。
// 注：状态被设为 ReadyToCommit；缓存 archetype handle 失效（因 composition 变了）。
template<CTag T>
FEntityBuilder& FEntityBuilder::Add()
{
	Composition.GetTags().Add<T>();
	State = EState::ReadyToCommit;
	CachedArchetypeHandle = FMassArchetypeHandle();
	return *this;
}

// 中文：ChunkFragment 特化 —— 同 Tag，只标位（chunk 级别数据，不携带单实体值）。
template<CChunkFragment T>
FEntityBuilder& FEntityBuilder::Add()
{
	Composition.GetChunkFragments().Add<T>();
	State = EState::ReadyToCommit;
	CachedArchetypeHandle = FMassArchetypeHandle();
	return *this;
}

// 中文：流式链式 Add(Args...) —— 内部转调 Add_GetRef，丢弃返回引用，让用户做 .Add().Add() 链式书写。
template<typename T, typename... TArgs>
FEntityBuilder& FEntityBuilder::Add(TArgs&&... InArgs) requires (CElement<T> && !(CTag<T> || CChunkFragment<T>))
{
	Add_GetRef<T>(InArgs...);
	return *this;
}

// 中文：核心实现 —— Add_GetRef
//   1) 先 ensure 没添加过该类型（防重复 Add）；
//   2) 在 Composition 对应位集中标位；
//   3) 失效 archetype 缓存；
//   4) 在对应 InstancedStruct 容器中 emplace 一份新实例并返回 GetMutable<T>。
// 失败路径（已存在）：返回已存在那份的引用（退化为 Find/GetMutable）。
template<typename T, typename... TArgs>
T& FEntityBuilder::Add_GetRef(TArgs&&... InArgs) requires (CElement<T> && !(CTag<T> || CChunkFragment<T>))
{
	using FElementType = UE::Mass::TElementType<T>;

	State = EState::ReadyToCommit;

	if (ensureMsgf(GetBitSetBuilder<FElementType>().template Contains<T>() == false, TEXT("Element of type %s has already been added"), *T::StaticStruct()->GetName()))
	{
		GetBitSetBuilder<FElementType>().template Add<T>();
		CachedArchetypeHandle = FMassArchetypeHandle();

		return GetInstancedStructContainerInternal<T>().Add_GetRef(FInstancedStruct::Make<T>(InArgs...)).template GetMutable<T>();
	}

	return GetInstancedStructContainerInternal<T>().FindByPredicate(FStructInstanceFindingPredicate{T::StaticStruct()})
		->template GetMutable<T>();
}

// 中文：GetOrCreate —— 与 Add_GetRef 的差异：
//   - 若已存在该 element：用新值赋值覆盖（*FoundElement = T(InArgs...)），不报错；
//   - 若不存在：与 Add_GetRef 一样标位 + 添加。
// 这是"幂等设置"语义。
template<typename T, typename... TArgs>
T& FEntityBuilder::GetOrCreate(TArgs&&... InArgs) requires (CElement<T> && !(CTag<T> || CChunkFragment<T>))
{
	using FElementType = UE::Mass::TElementType<T>;

	State = EState::ReadyToCommit;

	FInstancedStruct* ElementFound = nullptr;
	if (GetBitSetBuilder<FElementType>().template Contains<T>())
	{
		// replace
		ElementFound = GetInstancedStructContainerInternal<T>().FindByPredicate(FStructInstanceFindingPredicate{T::StaticStruct()});

		checkf(ElementFound, TEXT("We expect the element to be found since we already tested the Composition"));
		ElementFound->GetMutable<T>() = T(InArgs...);
	}
	else
	{
		GetBitSetBuilder<FElementType>().template Add<T>();
		CachedArchetypeHandle = FMassArchetypeHandle();

		ElementFound = &GetInstancedStructContainerInternal<T>().Add_GetRef(FInstancedStruct::Make<T>(InArgs...));
	}

	return ElementFound->GetMutable<T>();
}

// 中文：Find —— 仅查找已存在的元素实例。Composition 与 InstancedStruct 数组一致性由 checkf 保护。
template<typename T>
T* FEntityBuilder::Find() requires (CElement<T> && !(CTag<T> || CChunkFragment<T>))
{
	using FElementType = UE::Mass::TElementType<T>;

	if (GetBitSetBuilder<FElementType>().template Contains<T>())
	{
		FInstancedStruct* ElementFound = GetInstancedStructContainerInternal<T>().FindByPredicate(FStructInstanceFindingPredicate{T::StaticStruct()});

		checkf(ElementFound, TEXT("We expect the element to be found since we already tested the Composition"));
		return ElementFound->GetMutablePtr<T>();
	}

	return nullptr;
}

// 中文：模板版 AddRelation —— 用 T::StaticStruct() 找 FTypeHandle 后转调非模板版。
template<UE::Mass::CRelation T>
FEntityBuilder& FEntityBuilder::AddRelation(const FMassEntityHandle OtherEntity, const Relations::ERelationRole InputEntityRole)
{
	return AddRelation(FTypeManager::MakeTypeHandle(T::StaticStruct()), OtherEntity, InputEntityRole);
}

inline bool FEntityBuilder::IsValid() const
{
	return State != EState::Invalid;
}

// 中文：HasReservedEntityHandle 的语义 = "有 handle 但还没正式 build"。
// 一旦 Committed，即使 EntityHandle 仍 valid，也认为 handle 已"放出"，builder 不再"持有" reserve 关系。
inline bool FEntityBuilder::HasReservedEntityHandle() const
{
	return State != EState::Committed && EntityHandle.IsValid();
}

inline bool FEntityBuilder::IsCommitted() const
{
	return State == EState::Committed;
}

inline TSharedRef<FMassEntityManager> FEntityBuilder::GetEntityManager()
{
	return EntityManager;
}

inline void FEntityBuilder::ConfigureArchetypeCreation(const FMassArchetypeCreationParams& InCreationParams)
{
	ArchetypeCreationParams = InCreationParams;
}

} // namespace UE::Mass

#undef UE_API