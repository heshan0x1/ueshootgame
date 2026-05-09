// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// 【文件总览 / File Overview】
// MassEntityManager.cpp —— FMassEntityManager 的完整实现，是整个 MassEntity 框架最大、最核心
// 的源文件之一（约 2300 行）。
//
// 本文件实现了：
//   1. 生命周期：构造/Initialize/PostInitialize/Deinitialize/OnPostFork/析构；
//   2. FGCObject：AddReferencedObjects（让 GC 看到 SharedFragment 内的 UObject 引用）；
//   3. Archetype 创建/查找/相似派生：CreateArchetype 五个重载、InternalCreateSimilarArchetype 多个重载；
//   4. Entity 单实例 API：Reserve/Build/Create/Destroy/Release/IsActive/IsValid/IsBuilt/IsReserved；
//   5. Entity 批量 API：BatchReserve/BatchBuild/BatchCreate/BatchDestroy/BatchGroup/BatchChange*；
//   6. 单实体修改：AddFragment / RemoveFragment / AddTag / RemoveTag / SwapTagsForEntity /
//      AddElementToEntity / Add[Const]SharedFragment / RemoveSharedFragment / AddCompositionToEntity_GetDelta /
//      RemoveCompositionFromEntity / MoveEntityToAnotherArchetype / SetEntityFragmentValues；
//   7. ArchetypeGroup 管理：FindOrAddArchetypeGroupType / GetGroupsForArchetype / RemoveEntityFromGroupType；
//   8. Defer/FlushCommands 双缓冲：FlushCommands(buffer)、FlushCommands()、AppendCommands；
//   9. Type/Relation：OnNewTypeRegistered / OnRelationTypeRegistered / BatchCreateRelations；
//  10. Debug：DebugPrintArchetypes / DebugRemoveAllEntities / DebugForceArchetypeDataVersionBump 等。
//
// 【约定的内部宏】
//   CHECK_SYNC_API() —— 在同步 API 入口断言 IsProcessing()==false（processor 期间禁用同步 API）。
//   CHECK_SYNC_API_RETURN(v) —— 同上，但失败时 return v（用于有返回值函数）。
//   CHECK_ELEMENT(t) —— 断言传入的 UScriptStruct* 是 fragment 或 tag（用于 *Element* 系列）。
//
// 【常见私有路径】
//   - InternalBuildEntity：单 entity 的"绑 archetype + 加 chunk + 通知 observer"私有公共底层。
//   - InternalReleaseEntity：ForceReleaseOne，跳过 serial 校验。
//   - InternalAddFragmentListToEntity[Checked]：单实体加 fragment 的私有路径，区别在于是否过滤已存在。
//   - InternalCreateSimilarArchetype：基于源 archetype + 一个维度的 override 派生新 archetype。
//   - InternalBatchCreateReservedEntities：批量 build 的私有公共底层。
//
// 【关键设计点说明】
//   - 大量同步 API 都以 CHECK_SYNC_API 开头：强制开发者在 processor 内部用 CommandBuffer。
//   - "Move 一个 entity 到新 archetype" 是几乎所有结构性变化的核心原语：所有 add/remove fragment/tag/
//     shared fragment 都最终归到 MoveEntityToAnotherArchetype（archetype 层）+ SetArchetypeFromShared
//     （storage 层）+ ObserverManager 通知（observer 层）这三步。
//   - Observer 通知有"前置（Pre）"和"后置（Post）"两类：
//       · Remove 通知用 Pre（数据还在），Add 通知用 Post（数据已写入）；
//       · MoveEntityToAnotherArchetype 同时触发 Remove(Pre) + Add(Post)。
// =============================================================================================

#include "MassEntityManager.h"
#include "MassEntityManagerConstants.h"
#include "MassArchetypeData.h"
#include "MassCommandBuffer.h"
#include "MassEntityManagerStorage.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "VisualLogger/VisualLogger.h"
#include "MassExecutionContext.h"
#include "MassDebugger.h"
#include "Misc/Fork.h"
#include "Misc/CoreDelegates.h"
#include "Algo/Find.h"
#include "MassEntityUtils.h"
#include "MassEntityBuilder.h"
#include "MassTypeManager.h"
#include "MassProcessingContext.h"
#include "MassObserverNotificationTypes.h"

#define CHECK_SYNC_API() testableCheckf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__)
#define CHECK_SYNC_API_RETURN(ReturnValue) testableCheckfReturn(IsProcessing() == false, ReturnValue , TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__)
#define CHECK_ELEMENT(ElementType) checkf(UE::Mass::Private::IsElement(ElementType), TEXT("%hs: Only tags and fragments are considered 'elements', type provided: %s") \
		, __FUNCTION__ , *ElementType->GetName())

// 中文：全局静态"无效 entity"哨兵，等同于默认构造的 FMassEntityHandle()。
const FMassEntityHandle FMassEntityManager::InvalidEntity;

namespace UE::Mass::Private
{
	// note: this function doesn't set EntityHandle.SerialNumber
	// 中文：把"archetype-less subchunks"（仅有 entity index range 信息）展开成 entity handle 数组。
	//      不填 SerialNumber——caller 须在后续步骤补齐（典型用法见 BatchBuildEntities）。
	void ConvertArchetypelessSubchunksIntoEntityHandles(FMassArchetypeEntityCollection::FConstEntityRangeArrayView Subchunks, TArray<FMassEntityHandle>& OutEntityHandles)
	{
		int32 TotalCount = 0;
		for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Subchunk : Subchunks)
		{
			TotalCount += Subchunk.Length;
		}

		int32 Index = OutEntityHandles.Num();
		OutEntityHandles.AddDefaulted(TotalCount);

		for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Subchunk : Subchunks)
		{
			for (int i = Subchunk.SubchunkStart; i < Subchunk.SubchunkStart + Subchunk.Length; ++i)
			{
				OutEntityHandles[Index++].Index = i;
			}
		}
	}

	bool IsElement(TNotNull<const UScriptStruct*> ElementType)
	{
		// 中文：判定 ElementType 是否属于 *Element* 系列 API 接受的类型——只接受 fragment 和 tag。
		//      ChunkFragment / SharedFragment 都有专门的 API，不算 element。
		return UE::Mass::IsA<FMassFragment>(ElementType)
			|| UE::Mass::IsA<FMassTag>(ElementType);
	}
}

//-----------------------------------------------------------------------------
// FMassEntityManager
//-----------------------------------------------------------------------------
// 中文：构造器仅做最轻量的初始化——创建子模块（ObserverManager / TypeManager / RelationManager）
//      并把它们绑定到 *this。真正的"重活"留到 Initialize() 调用。
//      这样设计是因为 EntityManager 通常在 Subsystem 构造时一并构造，但实际可用要等 Subsystem
//      被 Initialize 时才调用 EntityManager.Initialize。
FMassEntityManager::FMassEntityManager(UObject* InOwner)
	: ObserverManager(*this)
	, TypeManager(new UE::Mass::FTypeManager(*this))
	, RelationManager(*this)
	, Owner(InOwner)
{
#if WITH_MASSENTITY_DEBUG
	DebugName = InOwner ? (InOwner->GetName() + TEXT("_EntityManager")) : TEXT("Unset");
#endif
}

FMassEntityManager::~FMassEntityManager()
{
	// 中文：保险措施——如果用户忘了显式 Deinitialize，析构时兜底清理。
	if (InitializationState == EInitializationState::Initialized)
	{
		Deinitialize();
	}
}

void FMassEntityManager::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	// 中文：内存统计：EntityStorage（仅 Initialized 时有效）+ 两张 archetype 索引 map +
	//      所有 deferred command buffer + 每个 archetype 自身的内存（chunks/fragment 数据）。
	//      被 .Stat 命令汇总展示。
	SIZE_T MyExtraSize = (InitializationState == EInitializationState::Initialized ? GetEntityStorageInterface().GetAllocatedSize() : 0)
		+ FragmentHashToArchetypeMap.GetAllocatedSize()
		+ FragmentTypeToArchetypeMap.GetAllocatedSize();

	for (const TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
	{
		MyExtraSize += (CommandBuffer ? CommandBuffer->GetAllocatedSize() : 0);
	}
	
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MyExtraSize);

	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ArchetypePtr->GetAllocatedSize());
		}
	}
}

void FMassEntityManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	// 中文：FGCObject 接口实现。EntityManager 自身不是 UObject，但内部
	//      SharedFragment / ConstSharedFragment 可能持有 UObject*（FInstancedStruct 内 UPROPERTY），
	//      且 ObserverManager 也可能引用 processor instance。这些都需要在 GC 时上报，避免被错误回收。
	if (InitializationState == EInitializationState::Uninitialized)
	{
		// 中文：初始化前 GC 跑了——直接早退，没有引用要上报。
		UE_VLOG_UELOG(GetOwner(), LogMass, Log, TEXT("AddReferencedObjects called before Initialize call"));
		return;
	}

	if (InitializationState == EInitializationState::Deinitialized)
	{
		// this is AddReferencedObjects called after Deinitialize call, which means we don't want to retain any object refs
		// since this FMassEntityManager instance is going away even if it's kept alive by some stored shared refs at the moment.
		// 中文：Deinitialize 之后还被 GC 调用（说明还有 shared ref 持有本 manager），但内部数据已被清空，
		//      此时不再上报引用——让 UObject 该被回收的就回收。
		return;
	}

	// 中文：上报 const shared fragment 池中所有实例的 UObject 引用。
	for (FConstSharedStruct& Struct : ConstSharedFragmentsContainer.GetAllInstances())
	{
		Struct.AddStructReferencedObjects(Collector);
	}

	// 中文：上报非 const shared fragment 池中所有实例的 UObject 引用。
	for (FSharedStruct& Struct : SharedFragmentsContainer.GetAllInstances())
	{
		Struct.AddStructReferencedObjects(Collector);
	}
 
	// 中文：把 ObserverManager 自身当作"通过 reflection 收集引用的 struct"上报。
	//      FMassObserverManager 内部含 UObject* 引用（如 processor instance）。
	const class UScriptStruct* ScriptStruct = FMassObserverManager::StaticStruct();
	TWeakObjectPtr<const UScriptStruct> ScriptStructPtr{ScriptStruct};
	Collector.AddReferencedObjects(ScriptStructPtr, &ObserverManager);
}

void FMassEntityManager::Initialize()
{
	// 中文：无参 Initialize 默认走单线程 entity 存储。绝大多数游戏代码用这个。
	FMassEntityManagerStorageInitParams InitializationParams;
	InitializationParams.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
	Initialize(InitializationParams);
}

namespace UE::Mass::Private
{
	// 中文：访问者模式——根据 InitializationParams 的 variant 类型选择合适的 entity 存储后端实例化。
	struct FEntityStorageInitializer
	{
		void operator()(const FMassEntityManager_InitParams_SingleThreaded& Params)
		{
			EntityStorage->Emplace<UE::Mass::FSingleThreadedEntityStorage>();
			EntityStorage->Get<FSingleThreadedEntityStorage>().Initialize(Params);
		}
		void operator()(const FMassEntityManager_InitParams_Concurrent& Params)
		{
#if WITH_MASS_CONCURRENT_RESERVE
			EntityStorage->Emplace<UE::Mass::FConcurrentEntityStorage>();
			EntityStorage->Get<UE::Mass::FConcurrentEntityStorage>().Initialize(Params);
#else
			// 中文：项目关闭并发存储但仍尝试用 Concurrent 后端——立即 check fail。
			checkf(false, TEXT("Mass does not support this storage backend"));
#endif
		}
		
		FMassEntityManager::FEntityStorageContainerType* EntityStorage = nullptr;
	};
}

void FMassEntityManager::Initialize(const FMassEntityManagerStorageInitParams& InitializationParams)
{
	// 中文：【Initialize 完整流程】
	if (InitializationState == EInitializationState::Initialized)
	{
		// 中文：重复调用——打 log 并直接返回（不重置状态）。
		UE_VLOG_UELOG(GetOwner(), LogMass, Log, TEXT("Calling %hs on already initialized entity manager owned by %s")
			, __FUNCTION__, *GetNameSafe(Owner.Get()));
		return;
	}

	LLM_SCOPE_BYNAME(TEXT("Mass/EntityManager"));

	// 中文：1) 根据 InitializationParams variant 选择 entity 存储后端并 Initialize 之。
	Visit(UE::Mass::Private::FEntityStorageInitializer{&EntityStorage}, InitializationParams);
#if WITH_MASSENTITY_DEBUG
	DebugEntityStoragePtr = &DebugGetEntityStorageInterface();
#endif // WITH_MASSENTITY_DEBUG

	// 中文：2) 创建 NumCommandBuffers 个 deferred command buffer（默认 2 个，双缓冲）。
	for (TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
	{
		CommandBuffer = MakeShareable(new FMassCommandBuffer());
	}

	// if we get forked we need to update the CurrentThreadID of things like command buffers
	// and potentially active creation context
	// 中文：3) 注册 OnPostFork 回调（若进程模式启用 fork）——子进程会复制本对象，但 thread ID 失效，
	//      需要在 Child 进程里把 command buffer 的 cached thread id 重置。
	if (FForkProcessHelper::IsForkRequested())
	{
		OnPostForkHandle = FCoreDelegates::OnPostFork.AddSP(AsShared(), &FMassEntityManager::OnPostFork);
	}

	// creating these bitset instances to populate respective bitset types' StructTrackers
	// 中文：4) 通过创建一次每种 BitSet 触发它们的 StructTracker 注册流程。
	//      接着遍历所有 UScriptStruct，把每种 fragment/tag/chunk fragment/[const]shared fragment
	//      预先 .Add 到对应 BitSet 中，从而保证 type → bit index 的全局一致映射在使用前就建立完毕。
	//      这是 Mass BitSet 性能的关键——避免 query 第一次创建 archetype 时才注册类型导致 stall。
	FMassFragmentBitSet Fragments;
	FMassTagBitSet Tags;
	FMassChunkFragmentBitSet ChunkFragments;
	FMassSharedFragmentBitSet LocalSharedFragments;
	FMassConstSharedFragmentBitSet LocalConstSharedFragments;

	for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
	{
		if (UE::Mass::IsA<FMassFragment>(*StructIt))
		{
			if (*StructIt != FMassFragment::StaticStruct())
			{
				Fragments.Add(**StructIt);
			}
		}
		else if (UE::Mass::IsA<FMassTag>(*StructIt))
		{
			if (*StructIt != FMassTag::StaticStruct())
			{
				Tags.Add(**StructIt);
			}
		}
		else if (UE::Mass::IsA<FMassChunkFragment>(*StructIt))
		{
			if (*StructIt != FMassChunkFragment::StaticStruct())
			{
				ChunkFragments.Add(**StructIt);
			}
		}
		else if (UE::Mass::IsA<FMassSharedFragment>(*StructIt))
		{
			if (*StructIt != FMassSharedFragment::StaticStruct())
			{
				LocalSharedFragments.Add(**StructIt);
			}
		}
		else if (UE::Mass::IsA<FMassConstSharedFragment>(*StructIt))
		{
			if (*StructIt != FMassConstSharedFragment::StaticStruct())
			{
				LocalConstSharedFragments.Add(**StructIt);
			}
		}
	}

	InitializationState = EInitializationState::Initialized;
	bFirstCommandFlush = true;

#if WITH_MASSENTITY_DEBUG
	// 中文：5) 调试期初始化——访问检测器（防止 processor 越权读 fragment）+ 注册到 MassDebugger。
	RequirementAccessDetector.Initialize();
	FMassDebugger::RegisterEntityManager(*this);
#endif // WITH_MASSENTITY_DEBUG
}

void FMassEntityManager::PostInitialize()
{
	// 中文：必须在 Initialize 之后再调用。分两步是因为某些 processor 在初始化期间会访问
	//      EntityManager（比如查 archetype），所以 EntityManager 自己得先准备好基础设施再
	//      做"依赖外部 type/observer 注册"的事。
	ensureMsgf(InitializationState == EInitializationState::Initialized,
		TEXT("This needs to be done after all the subsystems have been initialized since some processors might want to access"
			" them during processors' initialization"));

	// 中文：注册 Mass 内置类型（如 RelationManager 需要的元数据类型）。
	TypeManager->RegisterBuiltInTypes();
	// now hook-in all relation observers
	// note that we're doing it only after RegisterBuiltInTypes is done, as opposed to doing it on
	// every type as it gets added, because there are ways to override the traits of built-in types.
	// Once RegisterBuiltInTypes is done the traits are set in stone, and we can handle the types.
	// 中文：遍历所有已注册类型，逐一触发 OnNewTypeRegistered。
	//      为啥不在 RegisterBuiltInTypes 内部边注册边 hook？因为内置类型可能被项目代码 override
	//      其 traits，必须等所有类型注册完、traits 定型后再 hook observer，避免基于过期 traits hook。
	for (UE::Mass::FTypeManager::FTypeInfoConstIterator It = TypeManager->MakeIterator(); It; ++It)
	{
		OnNewTypeRegistered(It->Key);
	}

	// 中文：启动 ObserverManager（实际是触发其内部 fragment/tag observer 收集与排序）。
	ObserverManager.Initialize();
}

void FMassEntityManager::Deinitialize()
{
	if (InitializationState == EInitializationState::Initialized)
	{
		// 中文：移除 OnPostFork 回调避免悬挂引用。
		FCoreDelegates::OnPostFork.Remove(OnPostForkHandle);

		// closing down so no point in actually flushing commands, but need to clean them up to avoid warnings on destruction
		// 中文：关闭流程，未 flush 的命令直接清掉（CleanUp 会丢弃命令并打 log，避免析构期打 warning）。
		for (TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
		{
			if (CommandBuffer)
			{
				CommandBuffer->CleanUp();
			}
		}

#if WITH_MASSENTITY_DEBUG
		FMassDebugger::UnregisterEntityManager(*this);
#endif // WITH_MASSENTITY_DEBUG

		// 中文：释放 entity storage——Emplace<Empty> 销毁原 variant 内的存储实例。
		EntityStorage.Emplace<FEmptyVariantState>();

		// 中文：关闭 ObserverManager（清空 observer / context）。
		ObserverManager.DeInitialize();
		
		InitializationState = EInitializationState::Deinitialized;
	}
	else
	{
		UE_VLOG_UELOG(GetOwner(), LogMass, Log, TEXT("Calling %hs on already deinitialized entity manager owned by %s")
			, __FUNCTION__, *GetNameSafe(Owner.Get()));
	}
}

void FMassEntityManager::OnPostFork(EForkProcessRole Role)
{
	// 中文：【fork 后清理】
	//   OS 级 fork 把整个进程内存复制一份给子进程，但：
	//     · 子进程的线程 ID 和父进程不同（cached thread id 失效）；
	//     · ObserverManager 内部某些状态也需要重置。
	//   父进程（Parent role）一般不需要做什么；本函数只在 Child 上动手。
	if (Role == EForkProcessRole::Child)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/EntityManager"));
		for (TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
		{
			if (CommandBuffer)
			{
				// 中文：让 buffer 接受当前（child）线程 push。
				CommandBuffer->ForceUpdateCurrentThreadID();
			}
			else
			{
				// 中文：保险——如果某个 slot 不知怎的为空，重建之。
				CommandBuffer = MakeShareable(new FMassCommandBuffer());
			}
		}

		ObserverManager.OnPostFork(Role);
	}
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, const FMassArchetypeCreationParams& CreationParams)
{
	FMassArchetypeCompositionDescriptor Composition;
	InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(Composition, FragmentsAndTagsList);
	return CreateArchetype(Composition, CreationParams);
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(FMassArchetypeHandle SourceArchetype, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(SourceArchetype); 
	return CreateArchetype(SourceArchetype, FragmentsAndTagsList, FMassArchetypeCreationParams(ArchetypeData));
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(FMassArchetypeHandle SourceArchetype,
	TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, const FMassArchetypeCreationParams& CreationParams)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(SourceArchetype);
	FMassArchetypeCompositionDescriptor Composition = ArchetypeData.GetCompositionDescriptor();
	InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(Composition, FragmentsAndTagsList);
	return CreateArchetype(Composition, CreationParams);
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& AddedFragments)
{
	return CreateArchetype(SourceArchetype, AddedFragments, FMassArchetypeCreationParams(*SourceArchetype));
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& AddedFragments, const FMassArchetypeCreationParams& CreationParams)
{
	check(SourceArchetype.IsValid());
	checkf(AddedFragments.IsEmpty() == false, TEXT("%hs Adding an empty fragment list to an archetype is not supported."), __FUNCTION__);

	const FMassArchetypeCompositionDescriptor Composition(AddedFragments + SourceArchetype->GetFragmentBitSet()
		, SourceArchetype->GetTagBitSet()
		, SourceArchetype->GetChunkFragmentBitSet()
		, SourceArchetype->GetSharedFragmentBitSet()
		, SourceArchetype->GetConstSharedFragmentBitSet());
	return CreateArchetype(Composition, CreationParams);
}

FMassArchetypeHandle FMassEntityManager::GetOrCreateSuitableArchetype(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassSharedFragmentBitSet& SharedFragmentBitSet
	, const FMassConstSharedFragmentBitSet& ConstSharedFragmentBitSet
	, const FMassArchetypeCreationParams& CreationParams)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	if (SharedFragmentBitSet != ArchetypeData.GetSharedFragmentBitSet()
		|| ConstSharedFragmentBitSet != ArchetypeData.GetConstSharedFragmentBitSet())
	{
		FMassArchetypeCompositionDescriptor NewDescriptor = ArchetypeData.GetCompositionDescriptor();
		NewDescriptor.SetSharedFragments(SharedFragmentBitSet);
		NewDescriptor.SetConstSharedFragments(ConstSharedFragmentBitSet);
		return CreateArchetype(NewDescriptor, CreationParams);
	}
	return ArchetypeHandle;
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(const FMassArchetypeCompositionDescriptor& Composition, const FMassArchetypeCreationParams& CreationParams)
{
	// 中文：【CreateArchetype 核心实现 —— archetype 自动去重的关键算法】
	LLM_SCOPE_BYNAME(TEXT("Mass/EntityManager"));
	// 中文：1) 计算 type hash —— composition 的 hash 与 GroupHandle 的 hash 合并。
	//      默认 GroupHandle 是空的（不属于任何 group），但同名 composition + 不同 group 也算不同 archetype。
	const uint32 TypeHash = HashCombine(Composition.CalculateHash(), GetTypeHash(UE::Mass::FArchetypeGroups()));

	// 中文：2) 在 hash 桶中查找已存在的 archetype（hash 冲突时桶内可能多于一项，需 IsEquivalent 二次比较）。
	TArray<TSharedPtr<FMassArchetypeData>>& HashRow = FragmentHashToArchetypeMap.FindOrAdd(TypeHash);

	TSharedPtr<FMassArchetypeData> ArchetypeDataPtr;
	for (const TSharedPtr<FMassArchetypeData>& Ptr : HashRow)
	{
		// 中文：IsEquivalent 同时比较 composition + groups，确保完全相等才复用。
		if (Ptr->IsEquivalent(Composition, /*Groups=*/{}))
		{
#if WITH_MASSENTITY_DEBUG
			// Keep track of all names for this archetype.
			// 中文：调试期把多个调用方提供的 DebugName 都附加到同一 archetype，便于诊断。
			if (!CreationParams.DebugName.IsNone())
			{
				Ptr->AddUniqueDebugName(CreationParams.DebugName);
			}
#endif // WITH_MASSENTITY_DEBUG
			if (CreationParams.ChunkMemorySize > 0 && CreationParams.ChunkMemorySize != Ptr->GetChunkAllocSize())
			{
				// 中文：⚠️ 复用现有 archetype 但 caller 期望的 ChunkMemorySize 与现存的不一致——
				//      Mass 不支持 archetype 创建后再调整 chunk 大小，仅打 warning 复用现有大小。
				UE_LOG(LogMass, Warning, TEXT("Reusing existing Archetype, but the requested ChunkMemorySize is different. Requested %d, existing: %llu")
					, CreationParams.ChunkMemorySize, Ptr->GetChunkAllocSize());
			}
			ArchetypeDataPtr = Ptr;
			break;
		}
	}

	if (!ArchetypeDataPtr.IsValid())
	{
		// 中文：3) 没找到匹配——新建。
		// Important to pre-increment the version as the queries will use this value to do incremental updates
		// 中文：先递增 ArchetypeDataVersion，让新 archetype 的 CreatedArchetypeDataVersion 与 AllArchetypes 索引对齐。
		++ArchetypeDataVersion;

		// Create a new archetype
		FMassArchetypeData* NewArchetype = new FMassArchetypeData(CreationParams);
		// 中文：archetype 内部 Initialize 会根据 composition 计算 chunk 布局（fragment size、对齐等）。
		NewArchetype->Initialize(*this, Composition, ArchetypeDataVersion);
		ArchetypeDataPtr = HashRow.Add_GetRef(MakeShareable(NewArchetype));
		AllArchetypes.Add(ArchetypeDataPtr);
		// 中文：不变量——AllArchetypes.Num() == ArchetypeDataVersion。
		ensure(AllArchetypes.Num() == ArchetypeDataVersion);

		// 中文：4) 把新 archetype 注册到"按 fragment 类型反查"的索引（Query 用）。
		for (const FMassArchetypeFragmentConfig& FragmentConfig : NewArchetype->GetFragmentConfigs())
		{
			checkSlow(FragmentConfig.FragmentType)
			FragmentTypeToArchetypeMap.FindOrAdd(FragmentConfig.FragmentType).Add(ArchetypeDataPtr);
		}

		// 中文：5) 广播事件 + Trace 标记，让 query 系统、Insights、调试器都得到通知。
		OnNewArchetypeEvent.Broadcast(FMassArchetypeHandle(ArchetypeDataPtr));
		UE_TRACE_MASS_ARCHETYPE_CREATED(ArchetypeDataPtr)
	}

	return FMassArchetypeHelper::ArchetypeHandleFromData(ArchetypeDataPtr);
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassTagBitSet& OverrideTags)
{
	checkSlow(SourceArchetype.IsValid());
	const FMassArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	FMassArchetypeCompositionDescriptor NewComposition(SourceArchetypeRef.GetCompositionDescriptor());
	NewComposition.SetTags(OverrideTags);

	return InternalCreateSimilarArchetype(SourceArchetypeRef, MoveTemp(NewComposition), SourceArchetypeRef.GetGroups());
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& OverrideFragments)
{
	checkSlow(SourceArchetype.IsValid());
	const FMassArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	FMassArchetypeCompositionDescriptor NewComposition(SourceArchetypeRef.GetCompositionDescriptor());
	NewComposition.SetFragments(OverrideFragments);

	return InternalCreateSimilarArchetype(SourceArchetypeRef, MoveTemp(NewComposition), SourceArchetypeRef.GetGroups());
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const UE::Mass::FArchetypeGroups& GroupsOverride)
{
	checkSlow(SourceArchetype.IsValid());
	const FMassArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	FMassArchetypeCompositionDescriptor NewComposition = SourceArchetype->GetCompositionDescriptor();
	return InternalCreateSimilarArchetype(SourceArchetypeRef, MoveTemp(NewComposition), GroupsOverride);
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const FMassArchetypeData& SourceArchetypeRef, FMassArchetypeCompositionDescriptor&& NewComposition, const UE::Mass::FArchetypeGroups& Groups)
{
	// 中文：【Similar Archetype 路径】—— Add/Remove tag 等增量操作的高效底层。
	//      与 CreateArchetype 的区别：直接传入修改后的 composition 与 groups，少了"从 caller 入参再算 composition"的中间步骤；
	//      新 archetype 的初始化用 InitializeWithSimilar，可以从源 archetype 拷贝若干元数据（fragment 配置布局缓存）。
	LLM_SCOPE_BYNAME(TEXT("Mass/EntityManager"));
	// we require Groups to be already shrunk. Shrinking is required to remove any trailing, invalid group IDs that would
	// be there if IDs were added and removed to this specific Groups container instance
	// 中文：Groups 必须已 Shrunk——FArchetypeGroups 内部以稀疏 array 存 group id，删除会留空位，
	//      Shrink 才能保证 hash 稳定（带空位的 hash 值不同）。否则去重逻辑会失败。
	checkf(Groups.IsShrunk(), TEXT("A group container with invalid trailing IDs has been passed to archetype creation - this is not expected and will cause issues. Make sure to Shrink your Groups before passing to %hs"), __FUNCTION__);

	const uint32 TypeHash = HashCombine(NewComposition.CalculateHash(), GetTypeHash(Groups));

	TArray<TSharedPtr<FMassArchetypeData>>& HashRow = FragmentHashToArchetypeMap.FindOrAdd(TypeHash);

	TSharedPtr<FMassArchetypeData> ArchetypeDataPtr;
	for (const TSharedPtr<FMassArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(NewComposition, Groups))
		{
			ArchetypeDataPtr = Ptr;
			break;
		}
	}

	if (!ArchetypeDataPtr.IsValid())
	{
		// Important to pre-increment the version as the queries will use this value to do incremental updates
		++ArchetypeDataVersion;

		// Create a new archetype
		FMassArchetypeData* NewArchetype = new FMassArchetypeData(FMassArchetypeCreationParams(SourceArchetypeRef));
		// 中文：与 CreateArchetype 的不同点——InitializeWithSimilar 让新 archetype 从源 archetype 复用
		//      尽可能多的元数据（fragment 偏移表等），降低初始化成本。
		NewArchetype->InitializeWithSimilar(*this, SourceArchetypeRef, MoveTemp(NewComposition), Groups, ArchetypeDataVersion);
		// 中文：把源 archetype 的 DebugName 也带过来，方便在 archetype browser 中追溯。
		NewArchetype->CopyDebugNamesFrom(SourceArchetypeRef);

		ArchetypeDataPtr = HashRow.Add_GetRef(MakeShareable(NewArchetype));
		AllArchetypes.Add(ArchetypeDataPtr);
		ensure(AllArchetypes.Num() == ArchetypeDataVersion);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : NewArchetype->GetFragmentConfigs())
		{
			checkSlow(FragmentConfig.FragmentType)
			FragmentTypeToArchetypeMap.FindOrAdd(FragmentConfig.FragmentType).Add(ArchetypeDataPtr);
		}

		OnNewArchetypeEvent.Broadcast(FMassArchetypeHandle(ArchetypeDataPtr));
		UE_TRACE_MASS_ARCHETYPE_CREATED(ArchetypeDataPtr)
	}

	return FMassArchetypeHelper::ArchetypeHandleFromData(ArchetypeDataPtr);
}

void FMassEntityManager::InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(
	FMassArchetypeCompositionDescriptor& InOutComposition, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList) const
{
	for (const UScriptStruct* Type : FragmentsAndTagsList)
	{
		if (UE::Mass::IsA<FMassFragment>(Type))
		{
			InOutComposition.GetFragments().Add(*Type);
		}
		else if (UE::Mass::IsA<FMassTag>(Type))
		{
			InOutComposition.GetTags().Add(*Type);
		}
		else if (UE::Mass::IsA<FMassChunkFragment>(Type))
		{
			InOutComposition.GetChunkFragments().Add(*Type);
		}
		else
		{
			UE_LOG(LogMass, Warning, TEXT("%hs: %s is not a valid fragment nor tag type. Ignoring.")
				, __FUNCTION__, *GetNameSafe(Type));
		}
	}
}

FMassArchetypeHandle FMassEntityManager::GetArchetypeForEntity(FMassEntityHandle Entity) const
{
	if (IsEntityValid(Entity))
	{
		return FMassArchetypeHelper::ArchetypeHandleFromData(GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index));
	}
	return FMassArchetypeHandle();
}

FMassArchetypeHandle FMassEntityManager::GetArchetypeForEntityUnsafe(FMassEntityHandle Entity) const
{
	check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
	return FMassArchetypeHelper::ArchetypeHandleFromData(GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index));
}

void FMassEntityManager::GetMatchingArchetypes(const FMassFragmentRequirements& Requirements, TArray<FMassArchetypeHandle>& OutValidArchetypes) const
{
	GetMatchingArchetypes(Requirements, OutValidArchetypes, 0);
}

void FMassEntityManager::ForEachArchetypeFragmentType(const FMassArchetypeHandle& ArchetypeHandle, TFunction< void(const UScriptStruct* /*FragmentType*/)> Function)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	ArchetypeData.ForEachFragmentType(Function);
}

void FMassEntityManager::DoEntityCompaction(const double TimeAllowed)
{
	int32 TotalEntitiesMoved = 0;
	const double TimeAllowedEnd = FPlatformTime::Seconds() + TimeAllowed;

	bool bReachedTimeLimit = false;
	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			const double TimeAllowedLeft = TimeAllowedEnd - FPlatformTime::Seconds();
			bReachedTimeLimit = TimeAllowedLeft <= 0.0;
			if (bReachedTimeLimit)
			{
 				break;
			}
			TotalEntitiesMoved += ArchetypePtr->CompactEntities(TimeAllowedLeft);
		}
		if (bReachedTimeLimit)
		{
			break;
		}
	}

	UE_CVLOG(TotalEntitiesMoved, GetOwner(), LogMass, Verbose, TEXT("Entity Compaction: moved %d entities"), TotalEntitiesMoved);
}

FMassEntityHandle FMassEntityManager::CreateEntity(const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	// 中文：【单 entity 创建完整流程】
	//   1) 检查同步限制（CHECK_SYNC_API）；
	//   2) ReserveEntity 取一个新的 (Index, SerialNumber)；
	//   3) GetOrCreateSuitableArchetype 把 SharedFragmentValues 同步到 archetype（必要时新建）；
	//   4) InternalBuildEntity → SetArchetypeFromShared + archetype.AddEntity + Observer.OnPostEntityCreated。
	CHECK_SYNC_API_RETURN(return {});
	check(ArchetypeHandle.IsValid());
	
	// 中文：调试断点钩子：可在 MassDebugger 中针对特定 archetype/entity 设置 break-on-create。
	MASS_BREAKPOINT(UE::Mass::Debug::FBreakpoint::CheckCreateEntityBreakpoints(ArchetypeHandle));

	const FMassEntityHandle Entity = ReserveEntity();
	InternalBuildEntity(Entity
		, GetOrCreateSuitableArchetype(ArchetypeHandle, SharedFragmentValues.GetSharedFragmentBitSet(), SharedFragmentValues.GetConstSharedFragmentBitSet())
		, SharedFragmentValues);

	return Entity;
}

FMassEntityHandle FMassEntityManager::CreateEntity(TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassArchetypeCreationParams& CreationParams)
{
	// 中文：与上面的差别：根据 FragmentInstanceList 自动推导 archetype（无 tag/chunk fragment），
	//      并把每个 instance 的初值写入。
	CHECK_SYNC_API_RETURN(return {});
	check(FragmentInstanceList.Num() > 0);

	const FMassArchetypeHandle& ArchetypeHandle = CreateArchetype(FMassArchetypeCompositionDescriptor(FragmentInstanceList,
		FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet()), CreationParams);
	check(ArchetypeHandle.IsValid());

	const FMassEntityHandle Entity = ReserveEntity();

	// Using a creation context to prevent InternalBuildEntity from notifying observers before we set fragments data
	// 中文：⚠️ 关键技巧：先建 CreationContext 再 BuildEntity——这样 OnPostEntityCreated 通知会被延迟，
	//      等到 SetFragmentsData 写完数据、context 析构时才统一触发。否则 observer 拿到的就是默认值。
	const TSharedRef<FEntityCreationContext> CreationContext = ObserverManager.GetOrMakeCreationContext();

	InternalBuildEntity(Entity, ArchetypeHandle, SharedFragmentValues);

	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);

	return Entity;
}

FMassEntityHandle FMassEntityManager::ReserveEntity()
{
	// 中文：仅向 EntityStorage 拿一个空槽位（Reserved 状态），不绑 archetype。
	//      WITH_MASS_CONCURRENT_RESERVE 启用时是线程安全的（FConcurrentEntityStorage 用 mpsc queue）。
	FMassEntityHandle Result = GetEntityStorageInterface().AcquireOne();

	return Result;
}

void FMassEntityManager::ReleaseReservedEntity(FMassEntityHandle Entity)
{
	// 中文：仅释放 Reserved 但未 Build 的 entity——若已 Build 必须用 DestroyEntity。
	checkf(!IsEntityBuilt(Entity), TEXT("Entity is already built, use DestroyEntity() instead"));

	InternalReleaseEntity(Entity);
}

void FMassEntityManager::BuildEntity(FMassEntityHandle Entity, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	CHECK_SYNC_API();
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));
	check(ArchetypeHandle.IsValid());

	InternalBuildEntity(Entity, ArchetypeHandle, SharedFragmentValues);
}

void FMassEntityManager::BuildEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	CHECK_SYNC_API();
	check(FragmentInstanceList.Num() > 0);
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));

	checkf(SharedFragmentValues.IsSorted(), TEXT("Expecting shared fragment values to be previously sorted"));
	FMassArchetypeCompositionDescriptor Composition(FragmentInstanceList, FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet());
	for (const FConstSharedStruct& SharedFragment : SharedFragmentValues.GetConstSharedFragments())
	{
		Composition.GetConstSharedFragments().Add(*SharedFragment.GetScriptStruct());
	}
	for (const FSharedStruct& SharedFragment : SharedFragmentValues.GetSharedFragments())
	{
		Composition.GetSharedFragments().Add(*SharedFragment.GetScriptStruct());
	}

	const FMassArchetypeHandle& ArchetypeHandle = CreateArchetype(Composition);
	check(ArchetypeHandle.IsValid());

	// Using a creation context to prevent InternalBuildEntity from notifying observers before we set fragments data
	const TSharedRef<FEntityCreationContext> CreationContext = ObserverManager.GetOrMakeCreationContext();

	InternalBuildEntity(Entity, ArchetypeHandle, SharedFragmentValues);

	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

TConstArrayView<FMassEntityHandle> FMassEntityManager::BatchReserveEntities(const int32 Count, TArray<FMassEntityHandle>& InOutEntities)
{
	const int32 Index = InOutEntities.Num();
	const int32 NumAdded = GetEntityStorageInterface().Acquire(Count, InOutEntities);
	ensureMsgf(NumAdded == Count, TEXT("Failed to reserve %d entities, was able to only reserve %d"), Count, NumAdded);

	return MakeArrayView(InOutEntities.GetData() + Index, NumAdded);
}

int32 FMassEntityManager::BatchReserveEntities(TArrayView<FMassEntityHandle> InOutEntities)
{
	return GetEntityStorageInterface().Acquire(InOutEntities);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload
	, const FMassFragmentBitSet& FragmentsAffected, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassArchetypeCreationParams& CreationParams)
{
	CHECK_SYNC_API_RETURN(return FMassObserverManager::FCreationContext::DebugCreateDummyCreationContext());
	check(SharedFragmentValues.IsSorted());

	FMassArchetypeCompositionDescriptor Composition(FragmentsAffected, FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet());
	for (const FConstSharedStruct& SharedFragment : SharedFragmentValues.GetConstSharedFragments())
	{
		Composition.GetConstSharedFragments().Add(*SharedFragment.GetScriptStruct());
	}
	for (const FSharedStruct& SharedFragment : SharedFragmentValues.GetSharedFragments())
	{
		Composition.GetSharedFragments().Add(*SharedFragment.GetScriptStruct());
	}

	return BatchBuildEntities(EncodedEntitiesWithPayload, MoveTemp(Composition), SharedFragmentValues, CreationParams);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload
	, const FMassArchetypeCompositionDescriptor& Composition
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassArchetypeCreationParams& CreationParams)
{
	// 中文：【BatchBuildEntities 完整批量构建实现】见头文件相应函数详细注释。
	CHECK_SYNC_API_RETURN(return FMassObserverManager::FCreationContext::DebugCreateDummyCreationContext());

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchBuildEntities);

	FMassArchetypeEntityCollection::FEntityRangeArray TargetArchetypeEntityRanges;

	// "built" entities case, this is verified during FMassArchetypeEntityCollectionWithPayload construction
	// 中文：1) 拿/建目标 archetype。FMassArchetypeEntityCollectionWithPayload 构造时已验证 entity
	//      处于 reserved 状态，未与 archetype 绑定；这里我们决定 archetype 后统一绑定。
	FMassArchetypeHandle TargetArchetypeHandle = CreateArchetype(Composition, CreationParams);
	check(TargetArchetypeHandle.IsValid());

	// there are some extra steps in creating EncodedEntities from the original given entity handles and then back
	// to handles here, but this way we're consistent in how stuff is handled, and there are some slight benefits 
	// to having entities ordered by their index (like accessing the Entities data below).
	// 中文：2) 把 archetype-less subchunks 还原成 entity handle 数组。这样多绕一道（原本 caller 也是
	//      传 entity handle 进来）的好处：encoded 形式按 index 排序，缓存局部性更好。
	TArray<FMassEntityHandle> EntityHandles;
	UE::Mass::Private::ConvertArchetypelessSubchunksIntoEntityHandles(EncodedEntitiesWithPayload.GetEntityCollection().GetRanges(), EntityHandles);

	// since the handles encoded via FMassArchetypeEntityCollectionWithPayload miss the SerialNumber we need to update it
	// before passing over the new archetype. Thankfully we need to iterate over all the entity handles anyway
	// to update the manager's information on these entities (stored in FMassEntityManager::Entities)
	// 中文：3) 给每个 handle 补 SerialNumber，并验证 entity 状态为 Reserved，
	//      然后 SetArchetypeFromShared 把它们都绑到目标 archetype。
	for (FMassEntityHandle& Entity : EntityHandles)
	{
		check(GetEntityStorageInterface().IsValidIndex(Entity.Index));

		const UE::Mass::IEntityStorageInterface::EEntityState EntityState = GetEntityStorageInterface().GetEntityState(Entity.Index);
		checkf(EntityState == UE::Mass::IEntityStorageInterface::EEntityState::Reserved, TEXT("Trying to build entities that are not reserved. Check all handles are reserved or consider using BatchCreateEntities"));

		const int32 SerialNumber = GetEntityStorageInterface().GetSerialNumber(Entity.Index);
		Entity.SerialNumber = SerialNumber;
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, TargetArchetypeHandle.DataPtr);
	}

	// 中文：4) archetype 一次性批量分配 chunk 槽位。TargetArchetypeEntityRanges 输出每个 entity
	//      最终落在 archetype 内的位置（按 chunk:offset 编码），后续设置 fragment 值要用。
	TargetArchetypeHandle.DataPtr->BatchAddEntities(EntityHandles, SharedFragmentValues, TargetArchetypeEntityRanges);
	UE_TRACE_MASS_ENTITIES_CREATED(EntityHandles, *TargetArchetypeHandle.DataPtr.Get())

	if (EncodedEntitiesWithPayload.GetPayload().IsEmpty() == false)
	{
		// at this point all the entities are in the target archetype, we can set the values
		// note that even though the "subchunk" information could have changed the order of entities is the same and 
		// corresponds to the order in FMassArchetypeEntityCollectionWithPayload's payload
		// 中文：5) 写入 fragment 初值。注意：BatchAddEntities 后 chunk 中 entity 的物理位置变了，
		//      但*顺序*与 EncodedEntitiesWithPayload 的 payload 顺序保持一致——这是 BatchAddEntities 的契约。
		//      因此可以按相同 index 把 payload[i] 写到 ranges 中第 i 个槽位。
		TargetArchetypeHandle.DataPtr->BatchSetFragmentValues(TargetArchetypeEntityRanges, EncodedEntitiesWithPayload.GetPayload());
	}

	// With this call we're either creating a fresh context populated with EntityHandles, or it will append 
	// EntityHandles to active context.
	// Not creating the context sooner since we want to reuse TargetArchetypeEntityRanges by moving it over to the context.
	// Note that we can afford to create this context so late since all previous operations were on the archetype level
	// and as such won't cause observers triggering (which usually is prevented by context's existence), and that we 
	// strongly assume the all entity creation/building (not to be mistaken with "reserving") takes place in a single thread
	// @todo add checks/ensures enforcing the assumption mentioned above.
	// 中文：6) 把 entity 集合记录到 CreationContext（新建或追加到当前活跃的）。
	//      为何 context 这么晚建？因为前面所有操作都是 archetype-level 的（不会触发 observer），
	//      而 context 的目的是延迟 observer 触发。等需要"通过 context 跟踪新增 entity"时再建即可，
	//      还能直接 Move TargetArchetypeEntityRanges 进 context 避免拷贝。
	return ObserverManager.GetOrMakeCreationContext(EntityHandles, FMassArchetypeEntityCollection(TargetArchetypeHandle, MoveTemp(TargetArchetypeEntityRanges)));
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities)
{
	CHECK_SYNC_API_RETURN(return FMassObserverManager::FCreationContext::DebugCreateDummyCreationContext());
	checkf(!ReservedEntities.IsEmpty(), TEXT("No reserved entities given to batch create."));

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchCreateReservedEntities);

	return InternalBatchCreateReservedEntities(
		GetOrCreateSuitableArchetype(ArchetypeHandle, SharedFragmentValues.GetSharedFragmentBitSet(), SharedFragmentValues.GetConstSharedFragmentBitSet())
		, SharedFragmentValues, ReservedEntities);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchCreateEntities(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const int32 Count, TArray<FMassEntityHandle>& InOutEntities)
{
	CHECK_SYNC_API_RETURN(return FMassObserverManager::FCreationContext::DebugCreateDummyCreationContext());
	testableCheckfReturn(ArchetypeHandle.IsValid(), return FMassObserverManager::FCreationContext::DebugCreateDummyCreationContext()
		, TEXT("%hs expecting a valid ArchetypeHandle"), __FUNCTION__);

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchCreateEntities);

	MASS_BREAKPOINT(UE::Mass::Debug::FBreakpoint::CheckCreateEntityBreakpoints(ArchetypeHandle));

	TConstArrayView<FMassEntityHandle> ReservedEntities = BatchReserveEntities(Count, InOutEntities);
	
	return InternalBatchCreateReservedEntities(
		GetOrCreateSuitableArchetype(ArchetypeHandle, SharedFragmentValues.GetSharedFragmentBitSet(), SharedFragmentValues.GetConstSharedFragmentBitSet())
		, SharedFragmentValues, ReservedEntities);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::InternalBatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities)
{
	// Functions calling into this one are required to verify that the archetype handle is valid
	FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
	checkf(ArchetypeData, TEXT("Functions calling into this one are required to verify that the archetype handle is valid"));

	MASS_BREAKPOINT(UE::Mass::Debug::FBreakpoint::CheckCreateEntityBreakpoints(ArchetypeHandle));

	for (FMassEntityHandle Entity : ReservedEntities)
	{
		check(IsEntityValid(Entity));
		const UE::Mass::IEntityStorageInterface::EEntityState EntityState = GetEntityStorageInterface().GetEntityState(Entity.Index);
		checkf(EntityState == UE::Mass::IEntityStorageInterface::EEntityState::Reserved, TEXT("Trying to build entities that are not reserved. Check all handles are reserved or consider using BatchCreateEntities"));
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, ArchetypeHandle.DataPtr);
	}

	FMassArchetypeEntityCollection::FEntityRangeArray TargetArchetypeEntityRanges;
	ArchetypeData->BatchAddEntities(ReservedEntities, SharedFragmentValues, TargetArchetypeEntityRanges);

	UE_TRACE_MASS_ENTITIES_CREATED(ReservedEntities, *ArchetypeData)

	return ObserverManager.GetOrMakeCreationContext(ReservedEntities, FMassArchetypeEntityCollection(ArchetypeHandle, MoveTemp(TargetArchetypeEntityRanges)));
}

void FMassEntityManager::DestroyEntity(FMassEntityHandle Entity)
{
	// 中文：【单 entity 销毁完整流程】
	//   1) CHECK_SYNC_API + 必须已 Built；
	//   2) Pre 通知 observer（observer 仍可读老数据）；
	//   3) Archetype.RemoveEntity："swap to last"从 chunk 中拔出，析构 fragment 数据；
	//   4) ForceReleaseOne 释放 entity index（serial number 自动 +1，老 handle 失效）。
	CHECK_SYNC_API();
	
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* Archetype = GetEntityStorageInterface().GetArchetype(Entity.Index);

	MASS_BREAKPOINT(UE::Mass::Debug::FBreakpoint::CheckDestroyEntityBreakpoints(Entity));

	if (Archetype)
	{
		// 中文：在数据真正被销毁前先通知 observer——observer 此时还能从 archetype 读到 fragment 老值。
		ObserverManager.OnPreEntityDestroyed(Archetype->GetCompositionDescriptor(), Entity);
		Archetype->RemoveEntity(Entity);
	}
	
	UE_TRACE_MASS_ENTITY_DESTROYED(Entity)

	InternalReleaseEntity(Entity);
}

void FMassEntityManager::BatchDestroyEntities(TConstArrayView<FMassEntityHandle> InEntities)
{
	// 中文：批量销毁未按 archetype 分组的 entity 列表。逐个走 OnPreEntityDestroyed + RemoveEntity，
	//      最后用 EntityStorage.Release 一次批量释放 index——比 N 次 ForceReleaseOne 高效。
	//      ⚠️ ObserverManager 不允许处于 lock 状态——锁住时 Remove 通知拿不到老数据，违反契约。
	CHECK_SYNC_API();
	checkf(ObserverManager.IsLocked() == false, TEXT("%hs: Trying to destroy entities while observers are locked - remove-observers won't get triggered in time to read fragments being removed."), __FUNCTION__);

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchDestroyEntities);

	MASS_BREAKPOINT(UE::Mass::Debug::FBreakpoint::CheckDestroyEntityBreakpoints(InEntities));

	for (const FMassEntityHandle Entity : InEntities)
	{
		// 中文：宽容处理无效 handle —— 不会崩溃，跳过即可（单 entity 销毁会 check fail）。
		//      这样 caller 可以传混合的"有效 + 已销毁"列表（罕见但能防御）。
		if (GetEntityStorageInterface().IsValidIndex(Entity.Index) == false)
		{
			continue;
		}

		const int32 SerialNumber = GetEntityStorageInterface().GetSerialNumber(Entity.Index);
		// 中文：serial number 不匹配——entity 已被销毁过（同 index 被复用），跳过。
		if (SerialNumber != Entity.SerialNumber)
		{
			continue;
		}

		if (FMassArchetypeData* Archetype = GetEntityStorageInterface().GetArchetype(Entity.Index))
		{
			ObserverManager.OnPreEntityDestroyed(Archetype->GetCompositionDescriptor(), Entity);
			Archetype->RemoveEntity(Entity);
		}
		// else it's a "reserved" entity so it has not been assigned to an archetype yet, no archetype nor observers to notify
		// 中文：仅 Reserved 未 Build 的 entity 没有 archetype，无 observer 可触发，直接走下面的 Release。
	}
	
	UE_TRACE_MASS_ENTITIES_DESTROYED(InEntities)

	GetEntityStorageInterface().Release(InEntities);
}

void FMassEntityManager::BatchDestroyEntityChunks(const FMassArchetypeEntityCollection& EntityCollection)
{
	BatchDestroyEntityChunks(MakeArrayView(&EntityCollection, 1));
}

void FMassEntityManager::BatchDestroyEntityChunks(TConstArrayView<FMassArchetypeEntityCollection> Collections)
{
	CHECK_SYNC_API();
	checkf(ObserverManager.IsLocked() == false, TEXT("%hs: Trying to destroy entities while observers are locked - remove-observers won't get triggered in time to read fragments being removed."), __FUNCTION__);

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchDestroyEntityChunks);

	TArray<FMassEntityHandle> EntitiesRemoved;
	FMassProcessingContext ProcessingContext(*this);

	for (const FMassArchetypeEntityCollection& EntityCollection : Collections)
	{
		EntitiesRemoved.Reset();
		if (ensureMsgf(EntityCollection.GetArchetype().IsValid() && EntityCollection.IsUpToDate(), TEXT("Provided collection is out of data")))
		{
			ObserverManager.OnPreEntitiesDestroyed(ProcessingContext, EntityCollection);

			checkf(EntityCollection.IsUpToDate(), TEXT("Remove-type observers resulted in additional mutating of entity composition. This is not allowed."))

			FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(EntityCollection.GetArchetype());
			ArchetypeData.BatchDestroyEntityChunks(EntityCollection.GetRanges(), EntitiesRemoved);
		
			GetEntityStorageInterface().Release(EntitiesRemoved);
		}
		else
		{
			UE::Mass::Private::ConvertArchetypelessSubchunksIntoEntityHandles(EntityCollection.GetRanges(), EntitiesRemoved);
			GetEntityStorageInterface().ForceRelease(EntitiesRemoved);
		}
	}
}

void FMassEntityManager::BatchGroupEntities(const UE::Mass::FArchetypeGroupHandle GroupHandle, TConstArrayView<FMassArchetypeEntityCollection> Collections)
{
	// 中文：【批量分组算法】
	//   把每个 collection 的 entity 加到指定 GroupHandle 所属的 group：
	//     1) 跳过已在该 group 的 archetype（无操作）；
	//     2) 否则计算 NewGroups = 旧 + GroupHandle，调用 InternalCreateSimilarArchetype 拿目标 archetype（仅 group 不同）；
	//     3) BatchMoveEntitiesToAnotherArchetype 整 chunk 一次性迁移；
	//     4) 更新每个 entity 的 archetype 指针。
	//   ⚠️ 当前实现注释中提到"need something like the following to support observers"——
	//      group 变更目前不触发 observer。这是 5.6 引入 ArchetypeGroup 时未完成的 TODO。
	CHECK_SYNC_API();

	if (GroupHandle.IsValid() == false)
	{
		// 中文：无效 GroupHandle 直接打 warning 返回，不 check fail——容错性更好。
		UE_LOG(LogMass, Warning, TEXT("%hs called with an invalid GroupHandle"), __FUNCTION__);
		return;
	}

	TArray<FMassEntityHandle> EntitiesBeingMoved;

	for (const FMassArchetypeEntityCollection& EntityCollection : Collections)
	{
		if (EntityCollection.GetArchetype().IsValid())
		{
			FMassArchetypeData& CurrentArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(EntityCollection.GetArchetype());
			if (CurrentArchetype.IsInGroup(GroupHandle) == false)
			{
				UE::Mass::FArchetypeGroups NewGroups = CurrentArchetype.GetGroups();
				NewGroups.Add(GroupHandle);

				const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(EntityCollection.GetArchetype().DataPtr, MoveTemp(NewGroups));

				EntitiesBeingMoved.Reset();
				CurrentArchetype.BatchMoveEntitiesToAnotherArchetype(EntityCollection, *NewArchetypeHandle.DataPtr.Get(), EntitiesBeingMoved
				// we need something like the following to support observers
				//, bTagsAddedAreObserved ? &NewArchetypeEntityRanges : nullptr
				// 中文：TODO：group 变化目前没接入 observer 通知机制。如果项目希望 group 变化也能
				//      被 observer 监听，需要在此处类似 BatchChangeTags 那样输出 NewArchetypeEntityRanges
				//      并触发 OnCompositionChanged。当前没做。
				);

				for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
				{
					check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
					GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
				}
			}
		}
	}
}

void FMassEntityManager::BatchGroupEntities(const UE::Mass::FArchetypeGroupHandle GroupHandle, TConstArrayView<FMassEntityHandle> InEntities)
{
	TArray<FMassArchetypeEntityCollection> Collections;
	UE::Mass::Utils::CreateEntityCollections(*this, InEntities, FMassArchetypeEntityCollection::FoldDuplicates, Collections);
	BatchGroupEntities(GroupHandle, Collections);
}

void FMassEntityManager::RemoveEntityFromGroupType(FMassEntityHandle EntityHandle, UE::Mass::FArchetypeGroupType GroupType)
{
	CHECK_SYNC_API();

	const FMassArchetypeHandle CurrentArchetypeHandle = GetArchetypeForEntity(EntityHandle);
	if (FMassArchetypeData* CurrentArchetype = CurrentArchetypeHandle.DataPtr.Get())
	{
		if (CurrentArchetype->IsInGroupOfType(GroupType))
		{
			const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetypeHandle.DataPtr, CurrentArchetype->GetGroups().Remove(GroupType));

			CurrentArchetype->MoveEntityToAnotherArchetype(EntityHandle, *NewArchetypeHandle.DataPtr.Get());
			
			GetEntityStorageInterface().SetArchetypeFromShared(EntityHandle.Index, NewArchetypeHandle.DataPtr);
		}
	}
}

UE::Mass::FArchetypeGroupHandle FMassEntityManager::GetGroupForEntity(FMassEntityHandle EntityHandle, UE::Mass::FArchetypeGroupType GroupType) const
{
	if (FMassArchetypeData* CurrentArchetype = GetArchetypeForEntity(EntityHandle).DataPtr.Get())
	{
		return UE::Mass::FArchetypeGroupHandle(GroupType, CurrentArchetype->GetGroups().GetID(GroupType));
	}

	return UE::Mass::FArchetypeGroupHandle();
}

UE::Mass::FArchetypeGroupType FMassEntityManager::FindOrAddArchetypeGroupType(const FName GroupName)
{
	// 中文：把 group name（FName）映射为 group type index。同名再次调用返回同一 index。
	//      内部 GroupNameToTypeIndex 是 TMap<FName,int32>。GroupTypes 是反向数组（index → name）。
	//      ⚠️ 不会 lock —— 假定仅 GameThread 调用。
	LLM_SCOPE_BYNAME(TEXT("Mass/EntityManager"));
	const int32* FoundGroupIndex = GroupNameToTypeIndex.Find(GroupName);
	if (LIKELY(FoundGroupIndex))
	{
		return UE::Mass::FArchetypeGroupType(*FoundGroupIndex);
	}

	const int32 NewGroupIndex = GroupTypes.Add(GroupName);
	GroupNameToTypeIndex.Add(GroupName, NewGroupIndex);
	return UE::Mass::FArchetypeGroupType(NewGroupIndex);
}

const UE::Mass::FArchetypeGroups& FMassEntityManager::GetGroupsForArchetype(const FMassArchetypeHandle& ArchetypeHandle) const
{
	if (ArchetypeHandle.IsValid() == false)
	{
		static UE::Mass::FArchetypeGroups DummyGroups;
		return DummyGroups;
	}

	return ArchetypeHandle.DataPtr->GetGroups();
}

void FMassEntityManager::AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	CHECK_SYNC_API();
	checkf(FragmentType, TEXT("Null fragment type passed in to %hs"), __FUNCTION__);

	CheckIfEntityIsActive(Entity);

	const FMassArchetypeCompositionDescriptor Descriptor(InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(*FragmentType)));

	ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
}

void FMassEntityManager::AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType, const FStructInitializationCallback& Initializer)
{
	CHECK_SYNC_API();
	checkf(FragmentType, TEXT("Null fragment type passed in to %hs"), __FUNCTION__);

	CheckIfEntityIsActive(Entity);

	FMassFragmentBitSet Fragments = InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(*FragmentType));
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	void* FragmentData = CurrentArchetype->GetFragmentDataForEntity(FragmentType, Entity.Index);
	Initializer(FragmentData, *FragmentType);

	const FMassArchetypeCompositionDescriptor Descriptor(MoveTemp(Fragments));
	
	ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
}

void FMassEntityManager::AddFragmentListToEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList)
{
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);

	const FMassArchetypeCompositionDescriptor Descriptor(InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(FragmentList)));
	
	ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
}

void FMassEntityManager::AddCompositionToEntity_GetDelta(FMassEntityHandle Entity, FMassArchetypeCompositionDescriptor& InOutDescriptor, const FMassArchetypeSharedFragmentValues* AddedSharedFragmentValues)
{
	// 中文：【AddCompositionToEntity_GetDelta 算法】
	//   入参 InOutDescriptor 是"打算添加的 composition 集合"（可能含 fragment + tag + shared 等）。
	//   首先 InOutDescriptor.Remove(OldComp) 算出真正缺失的部分（in-place 修改），
	//   然后基于 NewComp = OldComp + InOutDescriptor 创建/复用目标 archetype，
	//   并把 entity 迁移过去（公共 fragment 拷贝，新增 fragment 默认初始化）。
	//   返回时 InOutDescriptor 仅含"实际新增的 element 集合"，caller 可据此判断"具体变了什么"。
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* OldArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(OldArchetype);

	// 中文：先做差集，把"已经在 entity 上的 element"从 InOutDescriptor 中剔除。
	InOutDescriptor.Remove(OldArchetype->GetCompositionDescriptor());

	// 中文：⚠️ 不支持新增 ChunkFragment（chunk fragment 是 archetype 范围的，运行期增删受限）。
	ensureMsgf(InOutDescriptor.GetChunkFragments().IsEmpty(), TEXT("Adding new chunk fragments is not supported"));
	// 中文：⚠️ 若新增包含 shared fragment，必须同时提供匹配的 SharedFragmentValues。
	ensureMsgf(InOutDescriptor.GetSharedFragments().IsEmpty() 
		|| (AddedSharedFragmentValues && AddedSharedFragmentValues->DoesMatchComposition(InOutDescriptor))
		, TEXT("When adding new shared fragments it's required to provide values for said fragments"));

	if (InOutDescriptor.IsEmpty() == false)
	{
		FMassArchetypeCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Append(InOutDescriptor);

		const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor, FMassArchetypeCreationParams(*OldArchetype));

		if (ensure(NewArchetypeHandle.DataPtr.Get() != OldArchetype))
		{
			// Move the entity over
			FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
			NewArchetype.CopyDebugNamesFrom(*OldArchetype);
			if (AddedSharedFragmentValues)
			{
				// we need to merge AddedSharedFragmentValues with OldArchetype's shared fragments
				// 中文：合并 shared fragment 引用：旧的 + 新增的，再排序传入 Move。
				FMassArchetypeSharedFragmentValues CurrentSharedFragment = OldArchetype->GetSharedFragmentValues(Entity);
				CurrentSharedFragment.Append(*AddedSharedFragmentValues);
				CurrentSharedFragment.Sort();
				OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype, &CurrentSharedFragment);
			}
			else
			{
				OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);
			}

			GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

			// 中文：注意：这里只触发 Add 通知（Post），没触发 Remove 通知——因为本函数语义只 add，不 remove。
			ObserverManager.OnPostCompositionAdded(Entity, InOutDescriptor);
		}
	}
}

void FMassEntityManager::RemoveCompositionFromEntity(FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& InDescriptor)
{
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);

	if(InDescriptor.IsEmpty() == false)
	{
		FMassArchetypeData* OldArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
		check(OldArchetype);

		FMassArchetypeCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Remove(InDescriptor);

		ensureMsgf(InDescriptor.GetChunkFragments().IsEmpty(), TEXT("Removing chunk fragments is not supported"));

		if (NewDescriptor.IsEquivalent(OldArchetype->GetCompositionDescriptor()) == false)
		{
			ObserverManager.OnPreCompositionRemoved(Entity, InDescriptor);

			const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor, FMassArchetypeCreationParams(*OldArchetype));

			if (ensure(NewArchetypeHandle.DataPtr.Get() != OldArchetype))
			{
				// Move the entity over
				FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
				NewArchetype.CopyDebugNamesFrom(*OldArchetype);
				if (InDescriptor.GetSharedFragments().IsEmpty() && InDescriptor.GetConstSharedFragments().IsEmpty())
				{
					OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);
				}
				else
				{
					// we need to remove the shared fragment values to match the new composition
					FMassArchetypeSharedFragmentValues CurrentSharedFragment = OldArchetype->GetSharedFragmentValues(Entity);
					CurrentSharedFragment.Remove(InDescriptor);
					CurrentSharedFragment.Sort();
					OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype, &CurrentSharedFragment);
				}
				GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
			}
		}
	}
}

const FMassArchetypeCompositionDescriptor& FMassEntityManager::GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle) const
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.GetCompositionDescriptor();
}

void FMassEntityManager::InternalBuildEntity(FMassEntityHandle Entity, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	// 中文：【InternalBuildEntity 单 entity 构建私有底层】
	//   被 BuildEntity / CreateEntity / 各 BuildEntity 重载共同调用。三步走：
	//     1) EntityStorage 把 Index → archetype 的映射建立；
	//     2) Archetype.AddEntity 在物理 chunk 中分配槽位（可能新建 chunk），写入 SharedFragmentValues；
	//     3) ObserverManager 触发 OnPostEntityCreated（若有 CreationContext 则会被合并延迟触发）。
	const TSharedPtr<FMassArchetypeData>& NewArchetype = ArchetypeHandle.DataPtr;
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, ArchetypeHandle.DataPtr);
	NewArchetype->AddEntity(Entity, SharedFragmentValues);
	
	UE_TRACE_MASS_ENTITY_CREATED(Entity, *NewArchetype)

	ObserverManager.OnPostEntityCreated(Entity, NewArchetype->GetCompositionDescriptor());
}

void FMassEntityManager::InternalReleaseEntity(FMassEntityHandle Entity)
{
	// Using force release by bypass serial number check since we have verified the validity of the handle earlier.
	// 中文：跳过 serial number 校验直接释放——caller 已经在更上层验证过 handle 有效性。
	GetEntityStorageInterface().ForceReleaseOne(Entity);
}

FMassFragmentBitSet FMassEntityManager::InternalAddFragmentListToEntityChecked(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments)
{
	TSharedPtr<FMassArchetypeData>& OldArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(OldArchetype);

	UE_CLOG(OldArchetype->GetFragmentBitSet().HasAny(InFragments), LogMass, Log
		, TEXT("Trying to add a new fragment type to an entity, but it already has some of them. (%s)")
		, *InFragments.GetOverlap(OldArchetype->GetFragmentBitSet()).DebugGetStringDesc());

	FMassFragmentBitSet NewFragments = InFragments - OldArchetype->GetFragmentBitSet();
	if (NewFragments.IsEmpty() == false)
	{
		InternalAddFragmentListToEntity(Entity, NewFragments);
	}
	return MoveTemp(NewFragments);
}

void FMassEntityManager::InternalAddFragmentListToEntity(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments)
{
	checkf(InFragments.IsEmpty() == false, TEXT("%hs is intended for internal calls with non empty NewFragments parameter"), __FUNCTION__);
	check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
	TSharedPtr<FMassArchetypeData>& OldArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(OldArchetype.IsValid());

	// fetch or create the new archetype
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(OldArchetype, InFragments);
	checkf(NewArchetypeHandle.DataPtr != OldArchetype, TEXT("%hs is intended for internal calls with non overlapping fragment list."), __FUNCTION__);

	// Move the entity over
	FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
	NewArchetype.CopyDebugNamesFrom(*OldArchetype);
	OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);

	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
}

void FMassEntityManager::AddFragmentInstanceListToEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList)
{
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);
	checkf(FragmentInstanceList.Num() > 0, TEXT("Need to specify at least one fragment instances for this operation"));

	const FMassArchetypeCompositionDescriptor Descriptor(InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(FragmentInstanceList)));
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);

	ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
}

void FMassEntityManager::RemoveFragmentFromEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	RemoveFragmentListFromEntity(Entity, MakeArrayView(&FragmentType, 1));
}

void FMassEntityManager::RemoveFragmentListFromEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList)
{
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* OldArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(OldArchetype);

	const FMassFragmentBitSet FragmentsToRemove(FragmentList);

	if (OldArchetype->GetFragmentBitSet().HasAny(FragmentsToRemove))
	{
		// If all the fragments got removed this will result in fetching of the empty archetype
		const FMassArchetypeCompositionDescriptor NewComposition(OldArchetype->GetFragmentBitSet() - FragmentsToRemove
			, OldArchetype->GetTagBitSet()
			, OldArchetype->GetChunkFragmentBitSet()
			, OldArchetype->GetSharedFragmentBitSet()
			, OldArchetype->GetConstSharedFragmentBitSet());
		const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*OldArchetype));

		// Find overlap.  It isn't guaranteed that the old archetype has all of the fragments being removed.
		FMassArchetypeCompositionDescriptor CompositionDelta(OldArchetype->GetFragmentBitSet().GetOverlap(FragmentsToRemove));
		ObserverManager.OnPreCompositionRemoved(Entity, CompositionDelta);

		// Move the entity over
		FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
		NewArchetype.CopyDebugNamesFrom(*OldArchetype);
		OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	}
}

void FMassEntityManager::SwapTagsForEntity(FMassEntityHandle Entity, const UScriptStruct* OldTagType, const UScriptStruct* NewTagType)
{
	// 中文：原子化"换 tag"——同时去掉 OldTag、添加 NewTag，仅一次 archetype 迁移。
	//      用法典型：状态机切换（FStateA → FStateB），比"先 Remove 再 Add"快约一倍。
	//
	//      ⚠️ 注意（潜在不一致）：本函数当前实现并不调用 ObserverManager.OnPreCompositionRemoved
	//      与 OnPostCompositionAdded —— 与 AddTagToEntity / RemoveTagFromEntity 的行为不一致。
	//      如果项目代码在 NewTag 上注册了 add observer 或在 OldTag 上注册了 remove observer，
	//      使用 SwapTagsForEntity 不会触发它们。这是有意为之还是 bug 需进一步确认。
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);

	checkf(UE::Mass::IsA<FMassTag>(OldTagType), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(OldTagType));
	checkf(UE::Mass::IsA<FMassTag>(NewTagType), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(NewTagType));
	
	TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);

	FMassTagBitSet NewTagBitSet = CurrentArchetype->GetTagBitSet();
	NewTagBitSet.Remove(*OldTagType);
	NewTagBitSet.Add(*NewTagType);
	
	if (NewTagBitSet != CurrentArchetype->GetTagBitSet())
	{
		const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetype, NewTagBitSet);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	}
}

void FMassEntityManager::AddTagToEntity(FMassEntityHandle Entity, const UScriptStruct* TagType)
{
	CHECK_SYNC_API();
	checkf(UE::Mass::IsA<FMassTag>(TagType), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);
	
	TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType) == false)
	{
		FMassTagBitSet NewTags = CurrentArchetype->GetTagBitSet();
		NewTags.Add(*TagType);
		const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetype, NewTags);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
		
		ObserverManager.OnPostCompositionAdded(Entity, FMassArchetypeCompositionDescriptor(FMassTagBitSet(*TagType)));
	}
}
	
void FMassEntityManager::RemoveTagFromEntity(FMassEntityHandle Entity, const UScriptStruct* TagType)
{
	CHECK_SYNC_API();
	checkf(UE::Mass::IsA<FMassTag>(TagType), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);

	TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType))
	{
		FMassTagBitSet TagDelta(*TagType);
		const FMassTagBitSet NewTagComposition = CurrentArchetype->GetTagBitSet() - TagDelta;
		ObserverManager.OnPreCompositionRemoved(Entity, FMassArchetypeCompositionDescriptor(MoveTemp(TagDelta)));

		const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetype, NewTagComposition);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	}
}

bool FMassEntityManager::DoesEntityHaveElement(FMassEntityHandle Entity, TNotNull<const UScriptStruct*> ElementType) const
{
	CHECK_ELEMENT(ElementType);

	CheckIfEntityIsActive(Entity);
	const TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);
	if (UE::Mass::IsA<FMassTag>(ElementType))
	{
		return CurrentArchetype->GetCompositionDescriptor().GetTags().Contains(*ElementType);
	}
	
	return CurrentArchetype->GetCompositionDescriptor().GetFragments().Contains(*ElementType);
}

void FMassEntityManager::AddElementToEntities(TConstArrayView<FMassEntityHandle> Entities, TNotNull<const UScriptStruct*> ElementType)
{
	CHECK_ELEMENT(ElementType);

	TArray<FMassArchetypeEntityCollection> EntityCollections;
	UE::Mass::Utils::CreateEntityCollections(*this, Entities, FMassArchetypeEntityCollection::EDuplicatesHandling::FoldDuplicates, EntityCollections);

	if (UE::Mass::IsA<FMassTag>(ElementType))
	{
		BatchChangeTagsForEntities(EntityCollections, FMassTagBitSet(*ElementType), {});
	}
	else
	{
		BatchChangeFragmentCompositionForEntities(EntityCollections, FMassFragmentBitSet(*ElementType), {});
	}
}

void FMassEntityManager::AddElementToEntity(FMassEntityHandle Entity, TNotNull<const UScriptStruct*> ElementType)
{
	CHECK_ELEMENT(ElementType);

	if (UE::Mass::IsA<FMassTag>(ElementType))
	{
		AddTagToEntity(Entity, ElementType);
	}
	else
	{
		AddFragmentToEntity(Entity, ElementType);
	}
}

void FMassEntityManager::RemoveElementFromEntities(TConstArrayView<FMassEntityHandle> Entities, TNotNull<const UScriptStruct*> ElementType)
{
	CHECK_ELEMENT(ElementType);

	TArray<FMassArchetypeEntityCollection> EntityCollections;
	UE::Mass::Utils::CreateEntityCollections(*this, Entities, FMassArchetypeEntityCollection::EDuplicatesHandling::FoldDuplicates, EntityCollections);

	if (UE::Mass::IsA<FMassTag>(ElementType))
	{
		BatchChangeTagsForEntities(EntityCollections, {}, FMassTagBitSet(*ElementType));
	}
	else
	{
		BatchChangeFragmentCompositionForEntities(EntityCollections, {}, FMassFragmentBitSet(*ElementType));
	}
}

void FMassEntityManager::RemoveElementFromEntity(FMassEntityHandle Entity, TNotNull<const UScriptStruct*> ElementType)
{
	CHECK_ELEMENT(ElementType);

	if (UE::Mass::IsA<FMassTag>(ElementType))
	{
		RemoveTagFromEntity(Entity, ElementType);
	}
	else
	{
		RemoveFragmentFromEntity(Entity, ElementType);
	}
}

bool FMassEntityManager::AddConstSharedFragmentToEntity(const FMassEntityHandle Entity, const FConstSharedStruct& InConstSharedFragment)
{
	// 中文：【AddConstSharedFragment 算法 —— 不支持"修改值"，只支持"加 / 软通过"】
	//   1) 若 entity 已有此类型的 const shared fragment：
	//      - 值相同（==或字段比较）→ 软通过返回 true（幂等）；
	//      - 值不同 → 不支持，打 warning 返回 false。
	//   2) 若 entity 没有此 fragment：
	//      - 新 composition = 旧 + 此类型；
	//      - CreateArchetype 拿目标 archetype；
	//      - 构造 NewSharedFragmentValues = 旧引用 + 新引用并排序；
	//      - MoveEntityToAnotherArchetype 迁移。
	//   ⚠️ 设计选择：shared fragment 一旦添加就不能改值——因为 entity 持有的只是"指向池中条目"的引用，
	//      改值会影响所有共享同一引用的 entity，这通常不是 caller 意图。要改值需先 Remove 再 Add 不同值。
	CHECK_SYNC_API_RETURN(return false);

	if (!ensureMsgf(InConstSharedFragment.IsValid(), TEXT("%hs parameter Fragment is expected to be valid"), __FUNCTION__))
	{
		return false;
	}

	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	check(CurrentArchetype);

	const UScriptStruct* StructType = InConstSharedFragment.GetScriptStruct();
	CA_ASSUME(StructType);
	if (CurrentArchetype->GetCompositionDescriptor().GetConstSharedFragments().Contains(*StructType))
	{
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity);
		FConstSharedStruct ExistingConstSharedStruct = SharedFragmentValues.GetConstSharedFragmentStruct(StructType);
		if (ExistingConstSharedStruct == InConstSharedFragment || ExistingConstSharedStruct.CompareStructValues(InConstSharedFragment))
		{
			// nothing to do
			return true;
		}
		// 中文：⚠️ 关键限制：不支持改值。caller 必须先 Remove 再 Add 新值。
		UE_LOG(LogMass, Warning, TEXT("Changing shared fragment value of entities is not supported"));
		return false;
	}
	
	FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
	NewComposition.GetConstSharedFragments().Add(*StructType);
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*CurrentArchetype));
	check(NewArchetypeHandle.IsValid());
	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);

	const FMassArchetypeSharedFragmentValues& OldSharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity.Index);
	check(!OldSharedFragmentValues.ContainsType(StructType));
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldSharedFragmentValues);
	NewSharedFragmentValues.Add(InConstSharedFragment);
	// 中文：Sort 是关键——SharedFragmentValues 内部按 type 排序后才能用 binary search 高效查找。
	NewSharedFragmentValues.Sort();

	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype, &NewSharedFragmentValues);

	// Change the entity archetype
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

	return true;
}

bool FMassEntityManager::RemoveConstSharedFragmentFromEntity(const FMassEntityHandle Entity, const UScriptStruct& ConstSharedFragmentType)
{
	CHECK_SYNC_API_RETURN(return false);

	if (!ensureMsgf(UE::Mass::IsA<FMassConstSharedFragment>(&ConstSharedFragmentType), TEXT("%hs parameter ConstSharedFragmentType is expected to be a FMassConstSharedFragment"), __FUNCTION__))
	{
		return false;
	}
	
	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	check(CurrentArchetype);
	
	if (!CurrentArchetype->GetCompositionDescriptor().GetConstSharedFragments().Contains(ConstSharedFragmentType))
	{
		// Nothing to do. Returning false to indicate nothing has been removed, as per function's documentation 
		return false;
	}

	FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
	NewComposition.GetConstSharedFragments().Remove(ConstSharedFragmentType);
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition);
	check(NewArchetypeHandle.IsValid());
	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);
	
	const FMassArchetypeSharedFragmentValues& OldSharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity.Index);
	check(OldSharedFragmentValues.ContainsType(&ConstSharedFragmentType));
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldSharedFragmentValues);
	
	const FMassConstSharedFragmentBitSet ToRemove(ConstSharedFragmentType);
	NewSharedFragmentValues.Remove(ToRemove);
	NewSharedFragmentValues.Sort();
	
	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype, &NewSharedFragmentValues);

	// Change the entity archetype
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	
	return true;
}

bool FMassEntityManager::AddSharedFragmentToEntity(const FMassEntityHandle Entity, const FSharedStruct& InSharedFragment)
{
	CHECK_SYNC_API_RETURN(return false);

	if (!ensureMsgf(InSharedFragment.IsValid(), TEXT("%hs parameter Fragment is expected to be valid"), __FUNCTION__))
	{
		return false;
	}

	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	check(CurrentArchetype);

	const UScriptStruct* StructType = InSharedFragment.GetScriptStruct();
	CA_ASSUME(StructType);
	if (CurrentArchetype->GetCompositionDescriptor().GetSharedFragments().Contains(*StructType))
	{
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity);
		FConstSharedStruct ExistingSharedStruct = SharedFragmentValues.GetSharedFragmentStruct(StructType);
		if (ExistingSharedStruct == InSharedFragment || ExistingSharedStruct.CompareStructValues(InSharedFragment))
		{
			// nothing to do
			return true;
		}
		UE_LOG(LogMass, Warning, TEXT("Changing shared fragment value of entities is not supported"));
		return false;
	}
	
	FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
	NewComposition.GetSharedFragments().Add(*StructType);
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*CurrentArchetype));
	check(NewArchetypeHandle.IsValid());
	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);

	const FMassArchetypeSharedFragmentValues& OldSharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity.Index);
	check(!OldSharedFragmentValues.ContainsType(StructType));
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldSharedFragmentValues);
	NewSharedFragmentValues.Add(InSharedFragment);
	NewSharedFragmentValues.Sort();

	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype, &NewSharedFragmentValues);

	// Change the entity archetype
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

	return true;
}

bool FMassEntityManager::RemoveSharedFragmentFromEntity(const FMassEntityHandle Entity, const UScriptStruct& SharedFragmentType)
{
	CHECK_SYNC_API_RETURN(return false);

	if (!ensureMsgf(UE::Mass::IsA<FMassSharedFragment>(&SharedFragmentType), TEXT("%hs parameter SharedFragmentType is expected to be a FMassSharedFragment"), __FUNCTION__))
	{
		return false;
	}
	
	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	check(CurrentArchetype);
	
	if (!CurrentArchetype->GetCompositionDescriptor().GetSharedFragments().Contains(SharedFragmentType))
	{
		// Nothing to do. Returning false to indicate nothing has been removed, as per function's documentation 
		return false;
	}

	FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
	NewComposition.GetSharedFragments().Remove(SharedFragmentType);
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition);
	check(NewArchetypeHandle.IsValid());
	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);
	
	const FMassArchetypeSharedFragmentValues& OldSharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity.Index);
	check(OldSharedFragmentValues.ContainsType(&SharedFragmentType));
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldSharedFragmentValues);
	
	const FMassSharedFragmentBitSet ToRemove(SharedFragmentType);
	NewSharedFragmentValues.Remove(ToRemove);
	NewSharedFragmentValues.Sort();
	
	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype, &NewSharedFragmentValues);

	// Change the entity archetype
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	
	return true;
}

void FMassEntityManager::BatchChangeTagsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassTagBitSet& TagsToAdd, const FMassTagBitSet& TagsToRemove)
{
	// 中文：【批量改 tag 算法】
	//   对每个 collection（已按源 archetype 分组）：
	//     1. NewTagComposition = OldTagBitSet + Add - Remove；
	//     2. 若与原 tag composition 相同则跳过（无需迁移）；
	//     3. 计算真正新增的 TagsAdded（去掉原本就有的）和真正移除的 TagsRemoved（取交集）；
	//     4. 触发 Pre Remove 通知（observer 仍能读老数据）；
	//     5. InternalCreateSimilarArchetype 拿目标 archetype（仅 tag 不同）；
	//     6. BatchMoveEntitiesToAnotherArchetype 整 chunk-range 一次性迁移；
	//     7. 仅当 TagsAdded 有 observer 监听时才输出 NewArchetypeEntityRanges 并触发 Post Add 通知
	//        （小优化：无观察者时省去 ranges 分配）。
	CHECK_SYNC_API();

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchChangeTagsForEntities);
	
	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		FMassArchetypeData* CurrentArchetype = Collection.GetArchetype().DataPtr.Get();
		const FMassTagBitSet NewTagComposition = CurrentArchetype
			? (CurrentArchetype->GetTagBitSet() + TagsToAdd - TagsToRemove)
			: (TagsToAdd - TagsToRemove);

		if (ensure(CurrentArchetype) && CurrentArchetype->GetTagBitSet() != NewTagComposition)
		{
			FMassTagBitSet TagsAdded = TagsToAdd - CurrentArchetype->GetTagBitSet();
			// 中文：⭐ 只有 TagsAdded 真有 observer 监听 Add 时才需要追踪迁移后位置——
			//      省去无意义的 NewArchetypeEntityRanges 分配（在大多数 tag 没观察者的项目里收益明显）。
			const bool bTagsAddedAreObserved = ObserverManager.HasObserversForBitSet(TagsAdded, EMassObservedOperation::AddElement);
			FMassTagBitSet TagsRemoved = TagsToRemove.GetOverlap(CurrentArchetype->GetTagBitSet());
			if (TagsRemoved.IsEmpty() == false)
			{
				ObserverManager.OnCompositionChanged(Collection, MoveTemp(TagsRemoved), EMassObservedOperation::RemoveElement);
			}
			
			FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(Collection.GetArchetype().DataPtr, NewTagComposition);
			checkSlow(NewArchetypeHandle.IsValid());

			// Move the entity over
			FMassArchetypeEntityCollection::FEntityRangeArray NewArchetypeEntityRanges;
			TArray<FMassEntityHandle> EntitiesBeingMoved;
			CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(Collection, *NewArchetypeHandle.DataPtr.Get(), EntitiesBeingMoved
				, bTagsAddedAreObserved ? &NewArchetypeEntityRanges : nullptr);

			for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
			{
				check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
				
				GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
			}

			if (TagsAdded.IsEmpty() == false)
			{
				// 中文：注意：这里检查的是 TagsAdded.IsEmpty() 而非 bTagsAddedAreObserved。
				//       即便没观察者，TagsAdded 不空也会进来——但 OnCompositionChanged 内部会再次过滤
				//       无观察者的情况，整体仍是高效的（且 NewArchetypeEntityRanges 此时为空）。
				ObserverManager.OnCompositionChanged(
					FMassArchetypeEntityCollection(NewArchetypeHandle, MoveTemp(NewArchetypeEntityRanges))
					, MoveTemp(TagsAdded)
					, EMassObservedOperation::AddElement);
			}
		}
	}
}

void FMassEntityManager::BatchChangeFragmentCompositionForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassFragmentBitSet& FragmentsToAdd, const FMassFragmentBitSet& FragmentsToRemove)
{
	CHECK_SYNC_API();

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchChangeFragmentCompositionForEntities);

	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		FMassArchetypeData* CurrentArchetype = Collection.GetArchetype().DataPtr.Get();
		const FMassFragmentBitSet NewFragmentComposition = CurrentArchetype
			? (CurrentArchetype->GetFragmentBitSet() + FragmentsToAdd - FragmentsToRemove)
			: (FragmentsToAdd - FragmentsToRemove);

		if (CurrentArchetype)
		{
			if (CurrentArchetype->GetFragmentBitSet() != NewFragmentComposition)
			{
				FMassFragmentBitSet FragmentsAdded = FragmentsToAdd - CurrentArchetype->GetFragmentBitSet();
				const bool bFragmentsAddedAreObserved = ObserverManager.HasObserversForBitSet(FragmentsAdded, EMassObservedOperation::AddElement);
				FMassFragmentBitSet FragmentsRemoved = FragmentsToRemove.GetOverlap(CurrentArchetype->GetFragmentBitSet());
				
				if (FragmentsRemoved.IsEmpty() == false)
				{
					ObserverManager.OnCompositionChanged(Collection, MoveTemp(FragmentsRemoved), EMassObservedOperation::RemoveElement);
				}

				FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(Collection.GetArchetype().DataPtr, NewFragmentComposition);
				checkSlow(NewArchetypeHandle.IsValid());

				// Move the entity over
				FMassArchetypeEntityCollection::FEntityRangeArray NewArchetypeEntityRanges;
				TArray<FMassEntityHandle> EntitiesBeingMoved;
				CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(Collection, *NewArchetypeHandle.DataPtr.Get(), EntitiesBeingMoved
					, bFragmentsAddedAreObserved ? &NewArchetypeEntityRanges : nullptr);

				for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
				{
					check(GetEntityStorageInterface().IsValidIndex(Entity.Index));

					GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
				}

				if (bFragmentsAddedAreObserved)
				{
					ObserverManager.OnCompositionChanged(
						FMassArchetypeEntityCollection(NewArchetypeHandle, MoveTemp(NewArchetypeEntityRanges))
						, MoveTemp(FragmentsAdded)
						, EMassObservedOperation::AddElement);
				}
			}
		}
		else
		{
			BatchBuildEntities(FMassArchetypeEntityCollectionWithPayload(Collection), NewFragmentComposition, FMassArchetypeSharedFragmentValues());
		}
	}
}

void FMassEntityManager::BatchAddFragmentInstancesForEntities(TConstArrayView<FMassArchetypeEntityCollectionWithPayload> EntityCollections, const FMassFragmentBitSet& FragmentsAffected)
{
	CHECK_SYNC_API();

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchAddFragmentInstancesForEntities);

	// here's the scenario:
	// * we get entities from potentially different archetypes
	// * adding a fragment instance consists of two operations: A) add fragment type & B) set fragment value
	//		* some archetypes might already have the "added" fragments so no need for step A
	//		* there might be an "empty" archetype in the mix - then step A results in archetype creation and assigning
	//		* if step A is required then the initial FMassArchetypeEntityCollection instance is no longer valid
	// * setting value can be done uniformly for all entities, remembering some might be in different chunks already
	// * @todo note that after adding fragment type some entities originally in different archetypes end up in the same 
	//		archetype. This could be utilized as a basis for optimization. To be investigated.
	// 

	for (const FMassArchetypeEntityCollectionWithPayload& EntityRangesWithPayload : EntityCollections)
	{
		FMassArchetypeHandle TargetArchetypeHandle = EntityRangesWithPayload.GetEntityCollection().GetArchetype();
		FMassArchetypeData* CurrentArchetype = TargetArchetypeHandle.DataPtr.Get();

		if (CurrentArchetype)
		{
			FMassArchetypeEntityCollection::FEntityRangeArray TargetArchetypeEntityRanges;
			bool bFragmentsAddedAreObserved = false;
			FMassFragmentBitSet NewFragmentComposition = CurrentArchetype
				? (CurrentArchetype->GetFragmentBitSet() + FragmentsAffected)
				: FragmentsAffected;
			FMassFragmentBitSet FragmentsAdded;

			if (CurrentArchetype->GetFragmentBitSet() != NewFragmentComposition)
			{
				FragmentsAdded = FragmentsAffected - CurrentArchetype->GetFragmentBitSet();
				bFragmentsAddedAreObserved = ObserverManager.HasObserversForBitSet(FragmentsAdded, EMassObservedOperation::AddElement);

				FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(TargetArchetypeHandle.DataPtr, NewFragmentComposition);
				checkSlow(NewArchetypeHandle.IsValid());

				// Move the entity over
				TArray<FMassEntityHandle> EntitiesBeingMoved;
				CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(EntityRangesWithPayload.GetEntityCollection(), *NewArchetypeHandle.DataPtr.Get()
					, EntitiesBeingMoved, &TargetArchetypeEntityRanges);

				for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
				{
					check(GetEntityStorageInterface().IsValidIndex(Entity.Index));

					GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
				}

				TargetArchetypeHandle = NewArchetypeHandle;
			}
			else
			{
				TargetArchetypeEntityRanges = EntityRangesWithPayload.GetEntityCollection().GetRanges();
			}

			// at this point all the entities are in the target archetype, we can set the values
			// note that even though the "subchunk" information could have changed the order of entities is the same and 
			// corresponds to the order in FMassArchetypeEntityCollectionWithPayload's payload
			TargetArchetypeHandle.DataPtr->BatchSetFragmentValues(TargetArchetypeEntityRanges, EntityRangesWithPayload.GetPayload());
			
			if (bFragmentsAddedAreObserved)
			{
				ObserverManager.OnCompositionChanged(
					FMassArchetypeEntityCollection(TargetArchetypeHandle, MoveTemp(TargetArchetypeEntityRanges))
					, MoveTemp(FragmentsAdded)
					, EMassObservedOperation::AddElement);
			}
		}
		else 
		{
			BatchBuildEntities(EntityRangesWithPayload, FragmentsAffected, FMassArchetypeSharedFragmentValues());
		}
	}
}

void FMassEntityManager::BatchAddSharedFragmentsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
	, const FMassArchetypeSharedFragmentValues& AddedFragmentValues)
{
	CHECK_SYNC_API();

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchAddConstSharedFragmentForEntities);

	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		FMassArchetypeData* CurrentArchetype = Collection.GetArchetype().DataPtr.Get();
		testableCheckfReturn(CurrentArchetype, continue, TEXT("Adding shared fragments to archetype-less entities is not supported"));

		FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
		NewComposition.GetSharedFragments() += AddedFragmentValues.GetSharedFragmentBitSet();
		NewComposition.GetConstSharedFragments() += AddedFragmentValues.GetConstSharedFragmentBitSet();

		const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*CurrentArchetype));
		check(NewArchetypeHandle.IsValid());
		FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
		check(NewArchetype);
		if (!testableEnsureMsgf(CurrentArchetype != NewArchetype, TEXT("Setting shared fragment values without archetype change is not supported")))
		{
			UE_LOG(LogMass, Warning, TEXT("Trying to set shared fragment values, without adding new shared fragments, is not supported."));
			continue;
		}

		TArray<FMassEntityHandle> EntitiesBeingMoved;
		CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(Collection, *NewArchetype, EntitiesBeingMoved, /*OutNewChunks=*/nullptr, &AddedFragmentValues);

		for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
		{
			check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
			
			GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
		}
	}
}

void FMassEntityManager::MoveEntityToAnotherArchetype(FMassEntityHandle Entity, FMassArchetypeHandle NewArchetypeHandle,
	const FMassArchetypeSharedFragmentValues* SharedFragmentValuesOverride)
{
	// 中文：【跨 archetype 迁移 —— 几乎所有结构性变化的最终落脚点】
	//   1) 计算 Removed = OldComp - NewComp，触发 Pre Remove 通知（observer 可访问被删 fragment 的旧值）；
	//   2) Archetype 层 MoveEntityToAnotherArchetype：
	//      - 在新 archetype 找空位（最后未满 chunk 或新 chunk）；
	//      - 公共 fragment：memcpy 旧 chunk → 新 chunk；
	//      - 仅旧 archetype 的 fragment：调用其 destructor；
	//      - 仅新 archetype 的 fragment：用 fragment 默认值初始化；
	//      - 旧 chunk 空位 swap 末尾元素填补；
	//   3) EntityStorage 更新 entity → archetype 映射；
	//   4) 计算 Added = NewComp - OldComp，触发 Post Add 通知（observer 可读新 fragment 的值）。
	CHECK_SYNC_API();

	CheckIfEntityIsActive(Entity);

	FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);

	// Move the entity over
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);

	// 中文：CompositionRemoved = OldArchetype 比 NewArchetype 多出来的部分。
	FMassArchetypeCompositionDescriptor CompositionRemoved = CurrentArchetype->GetCompositionDescriptor().CalculateDifference(NewArchetype.GetCompositionDescriptor());
	ObserverManager.OnCompositionChanged(Entity, MoveTemp(CompositionRemoved), EMassObservedOperation::RemoveElement);
	
	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype, SharedFragmentValuesOverride);
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

	// 中文：CompositionAdded = NewArchetype 比 OldArchetype 多出来的部分。
	FMassArchetypeCompositionDescriptor CompositionAdded = NewArchetype.GetCompositionDescriptor().CalculateDifference(CurrentArchetype->GetCompositionDescriptor());
	ObserverManager.OnCompositionChanged(Entity, MoveTemp(CompositionAdded), EMassObservedOperation::AddElement);
}

void FMassEntityManager::SetEntityFragmentValues(FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

void FMassEntityManager::BatchSetEntityFragmentValues(const FMassArchetypeEntityCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	if (FragmentInstanceList.Num())
	{
		BatchSetEntityFragmentValues(MakeArrayView(&SparseEntities, 1), FragmentInstanceList);
	}
}

void FMassEntityManager::BatchSetEntityFragmentValues(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	CHECK_SYNC_API();

	if (FragmentInstanceList.IsEmpty())
	{
		return;
	}

	for (const FMassArchetypeEntityCollection& SparseEntities : EntityCollections)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchSetEntityFragmentValues);

		FMassArchetypeData* Archetype = SparseEntities.GetArchetype().DataPtr.Get();
		check(Archetype);

		for (const FInstancedStruct& FragmentTemplate : FragmentInstanceList)
		{
			Archetype->SetFragmentData(SparseEntities.GetRanges(), FragmentTemplate);
		}
	}
}

void* FMassEntityManager::InternalGetFragmentDataChecked(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	// note that FragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	CheckIfEntityIsActive(Entity);
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	return CurrentArchetype->GetFragmentDataForEntityChecked(FragmentType, Entity.Index);
}

void* FMassEntityManager::InternalGetFragmentDataPtr(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	// note that FragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	return CurrentArchetype->GetFragmentDataForEntity(FragmentType, Entity.Index);
}

const FConstSharedStruct* FMassEntityManager::InternalGetConstSharedFragmentPtr(FMassEntityHandle Entity, const UScriptStruct* ConstSharedFragmentType) const
{
	// note that ConstSharedFragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	const FConstSharedStruct* SharedFragment = CurrentArchetype->GetSharedFragmentValues(Entity).GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(ConstSharedFragmentType));
	return SharedFragment;
}

const FSharedStruct* FMassEntityManager::InternalGetSharedFragmentPtr(FMassEntityHandle Entity, const UScriptStruct* SharedFragmentType) const
{
	// note that SharedFragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	const FSharedStruct* SharedFragment = CurrentArchetype->GetSharedFragmentValues(Entity).GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(SharedFragmentType));
	return SharedFragment;
}

bool FMassEntityManager::IsEntityActive(FMassEntityHandle Entity) const
{
	// 中文：综合判定——index 非保留索引 + storage 认为 active（state==Created 且 serial 匹配）。
	if (Entity.Index != UE::Mass::Private::InvalidEntityIndex)
	{
		const UE::Mass::FStorageType& EntityStorageInterface = GetEntityStorageInterface();
		return EntityStorageInterface.IsEntityActive(Entity);
	}
	return false;
}

bool FMassEntityManager::IsEntityValid(FMassEntityHandle Entity) const
{
	// 中文：handle 有效 = 非 0 index + 在 storage 范围内 + serial number 匹配（防止旧 handle 复用 index）。
	//      "Valid" 不要求 entity 已 Build，可能仍处 Reserved 状态。
	return (Entity.Index != UE::Mass::Private::InvalidEntityIndex) 
		&& GetEntityStorageInterface().IsValidIndex(Entity.Index) 
		&& (GetEntityStorageInterface().GetSerialNumber(Entity.Index) == Entity.SerialNumber);
}

bool FMassEntityManager::IsEntityBuilt(FMassEntityHandle Entity) const
{
	// 中文：必须先 IsValid 再判 state——本函数会 check fail 在无效 handle 上。
	//      Built ≡ state == Created（已绑 archetype + 已分配 chunk 槽位 + observer 已通知）。
	CheckIfEntityIsValid(Entity);
	const UE::Mass::IEntityStorageInterface::EEntityState CurrentState = GetEntityStorageInterface().GetEntityState(Entity.Index);
	return CurrentState == UE::Mass::IEntityStorageInterface::EEntityState::Created;
}

bool FMassEntityManager::IsEntityReserved(FMassEntityHandle EntityHandle) const
{
	// 中文：Reserved ≡ 已 ReserveEntity 但尚未 Build。处于此状态的 entity 没有 archetype。
	CheckIfEntityIsValid(EntityHandle);
	return GetEntityStorageInterface().GetEntityState(EntityHandle.Index) == UE::Mass::IEntityStorageInterface::EEntityState::Reserved;
}

FMassEntityHandle FMassEntityManager::CreateEntityIndexHandle(const int32 EntityIndex) const
{
	// 中文：根据 index 重建一个完整 handle（带 serial number）。
	//      仅当该 index 当前是 Created 状态才有效，否则返回 invalid handle。
	//      用于 debugger / 调试工具——把"我看到一个 entity index"还原成完整 handle。
	return (GetEntityStorageInterface().IsValidIndex(EntityIndex)
		&& GetEntityStorageInterface().GetEntityState(EntityIndex) == UE::Mass::IEntityStorageInterface::EEntityState::Created)
		? FMassEntityHandle(EntityIndex, GetEntityStorageInterface().GetSerialNumber(EntityIndex))
		: FMassEntityHandle();
}

void FMassEntityManager::GetMatchingArchetypes(const FMassFragmentRequirements& Requirements, TArray<FMassArchetypeHandle>& OutValidArchetypes, const uint32 FromArchetypeDataVersion) const
{
	// 中文：【Query 增量匹配核心】
	//   只检查 [FromArchetypeDataVersion, AllArchetypes.Num()) 范围内的 archetype。
	//   Query 自身缓存上次扫到的 ArchetypeDataVersion，下次 query 时只看新增 archetype——
	//   绝大多数 frame 都不需要扫所有 archetype，性能 O(新增数) 而非 O(总数)。
	for (int32 ArchetypeIndex = FromArchetypeDataVersion; ArchetypeIndex < AllArchetypes.Num(); ++ArchetypeIndex)
	{
		checkf(AllArchetypes[ArchetypeIndex].IsValid(), TEXT("We never expect to get any invalid shared ptrs in AllArchetypes"));

		FMassArchetypeData& Archetype = *(AllArchetypes[ArchetypeIndex].Get());

		// Only return archetypes with a newer created version than the specified version, this is for incremental query updates
		// 中文：不变量校验——AllArchetypes 索引应等于 archetype 自身记录的 CreatedArchetypeDataVersion。
		ensureMsgf(Archetype.GetCreatedArchetypeDataVersion() > FromArchetypeDataVersion
			, TEXT("There's a stron assumption that archetype's data version corresponds to its index in AllArchetypes"));

		if (Requirements.DoesArchetypeMatchRequirements(Archetype.GetCompositionDescriptor()))
		{
			OutValidArchetypes.Add(AllArchetypes[ArchetypeIndex]);
		}
#if WITH_MASSENTITY_DEBUG
		else
		{
			// 中文：调试期记录"为什么没匹配"，便于定位 query requirement 写错的问题。
			UE_VLOG_UELOG(GetOwner(), LogMass, VeryVerbose, TEXT("%s")
				, *FMassDebugger::GetArchetypeRequirementCompatibilityDescription(Requirements, Archetype.GetCompositionDescriptor()));
		}
#endif // WITH_MASSENTITY_DEBUG
	}
}

FMassExecutionContext FMassEntityManager::CreateExecutionContext(const float DeltaSeconds)
{
	// 中文：构造一个绑定到当前 OpenedCommandBufferIndex 对应 buffer 的 execution context。
	//      processor 通过 context 拿 entity view + Defer command。注意 context 持有的是 buffer 的
	//      *当前*指针——若执行期间发生 FlushCommands 把 buffer 切换了，context 仍指向"原 buffer"。
	//      但其实 processor 运行期间 IsProcessing()>0 时 FlushCommands 是禁用的，所以 OK。
	FMassExecutionContext ExecutionContext(*this, DeltaSeconds);
	ExecutionContext.SetDeferredCommandBuffer(DeferredCommandBuffers[OpenedCommandBufferIndex]);
	return MoveTemp(ExecutionContext);
}

void FMassEntityManager::FlushCommands(const TSharedPtr<FMassCommandBuffer>& InCommandBuffer)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Calling %hs is supported only on the Game Tread"), __FUNCTION__))
	{
		return;
	}

	if (!ensureMsgf(IsProcessing() == false, TEXT("Calling %hs is not supported while Mass Processing is active. Call FMassEntityManager::AppendCommands instead."), __FUNCTION__))
	{
		return;
	}

	if (UNLIKELY(InitializationState != EInitializationState::Initialized))
	{
		UE_CVLOG_UELOG(InitializationState == EInitializationState::Uninitialized, GetOwner(), LogMass, Warning
			, TEXT("FlushCommands called before Initialize call, which means this FMassEntityManager instance is not ready to process commands and will cancel them."));
		UE_CVLOG_UELOG(InitializationState == EInitializationState::Deinitialized, GetOwner(), LogMass, Log
			, TEXT("FlushCommands called after Deinitialize call, which means this FMassEntityManager instance is going away, can't process commands and will cancel them."));
		InCommandBuffer->CancelCommands();
		return;
	}
	
	if (InCommandBuffer && InCommandBuffer->HasPendingCommands()
		&& (Algo::Find(DeferredCommandBuffers, InCommandBuffer) == nullptr))
	{
		AppendCommands(InCommandBuffer);
	}
	FlushCommands();
}

void FMassEntityManager::FlushCommands()
{
	// 中文：【FlushCommands 双缓冲流转核心实现】
	//   关键设计：observer 在 flush 期间可能 push 新命令。我们用双 buffer 让"正在 flush 的 buffer"
	//   与"接收新命令的 buffer"分离：进入 flush 立刻切换 OpenedCommandBufferIndex，新命令进 buffer B；
	//   主线程 flush buffer A；A 处理完后看 B 是否非空，非空则继续 flush B（再切换回 A），
	//   循环最多 5 次防死锁。
	constexpr int32 MaxIterations = 5;

	if (!ensureMsgf(IsInGameThread(), TEXT("Calling %hs is supported only on the Game Tread"), __FUNCTION__))
	{
		return;
	}
	if (!ensureMsgf(IsProcessing() == false, TEXT("Calling %hs is not supported while Mass Processing is active. Call FMassEntityManager::AppendCommands instead."), __FUNCTION__))
	{
		// 中文：在 processor 内部不能调用同步 flush——容易导致 archetype 布局变化干扰处理器迭代。
		//      使用 AppendCommands 把 buffer 入队，等当前处理完毕由调度器 flush。
		return;
	}

	if (bCommandBufferFlushingInProgress == false && IsProcessing() == false)
	{
		// 中文：ON_SCOPE_EXIT 兜底复位标志位，确保异常路径也能解锁。
		ON_SCOPE_EXIT
		{
			bCommandBufferFlushingInProgress = false;
		};
		bCommandBufferFlushingInProgress = true;

		int32 IterationCount = 0;
		do 
		{
			const int32 CommandBufferIndexToFlush = OpenedCommandBufferIndex;

			// buffer swap. Code instigated by observers can still use Defer() to push commands.
			// 中文：⭐ buffer swap —— 提前切换 OpenedCommandBufferIndex，让接下来 observer 等
			//      调用 Defer() 拿到的都是新 buffer。这是双缓冲设计的核心精髓。
			OpenedCommandBufferIndex = (OpenedCommandBufferIndex + 1) % DeferredCommandBuffers.Num();
			ensureMsgf(DeferredCommandBuffers[OpenedCommandBufferIndex]->HasPendingCommands() == false
				, TEXT("The freshly opened command buffer is expected to be empty upon switching"));

			// 中文：真正执行命令——逐个 apply，触发 archetype/observer 变更。
			//      Flush 内部命令执行可能再 push 新命令到当前打开 buffer（已切换到对面的）。
			DeferredCommandBuffers[CommandBufferIndexToFlush]->Flush(*this);

			// repeat if there were commands submitted while commands were being flushed (by observers for example)
			// 中文：检查"接收新命令的 buffer"是否非空；非空则下一轮继续 flush（再次切换）。
		} while (DeferredCommandBuffers[OpenedCommandBufferIndex]->HasPendingCommands() && ++IterationCount < MaxIterations);

		// 中文：超 5 轮仍有命令——可能 observer 间接生产命令导致永不收敛。报 Error 但不死锁。
		UE_CVLOG_UELOG(IterationCount >= MaxIterations, GetOwner(), LogMass, Error, TEXT("Reached loop count limit while flushing commands. Limiting the number of commands pushed during commands flushing could help."));
	}
}

void FMassEntityManager::AppendCommands(const TSharedPtr<FMassCommandBuffer>& InOutCommandBuffer)
{
	// 中文：把外部 buffer 的命令并入主 command buffer。Move 语义——执行后 InOutCommandBuffer 会被清空。
	//      不能用 EntityManager 自己的 buffer 作为输入（会自我吞噬，ensureFalse + 早退）。
	//      典型用例：processor 各自持 thread-local command buffer，在 phase 结束时合并。
	if (!ensureMsgf(Algo::Find(DeferredCommandBuffers, InOutCommandBuffer) == nullptr
		, TEXT("We don't expect AppendCommands to be called with EntityManager's command buffer as the input parameter")))
	{
		return;
	}
	LLM_SCOPE_BYNAME(TEXT("Mass/EntityManager"));
	Defer().MoveAppend(*InOutCommandBuffer.Get());
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::GetOrMakeCreationContext()
{
	return ObserverManager.GetOrMakeCreationContext();
}

UE::Mass::FEntityBuilder FMassEntityManager::MakeEntityBuilder()
{
	return UE::Mass::FEntityBuilder(AsShared());
}

void FMassEntityManager::OnNewTypeRegistered(UE::Mass::FTypeHandle RegisteredTypeHandle)
{
	const UE::Mass::FTypeInfo* NewTypeInfo = TypeManager->GetTypeInfo(RegisteredTypeHandle);
	if (testableEnsureMsgf(NewTypeInfo, TEXT("%hs input handle doesn't represent a registered type"), __FUNCTION__))
	{
		if (const UE::Mass::FRelationTypeTraits* RelationTypeTraits = NewTypeInfo->GetAsRelationTraits())
		{
			OnRelationTypeRegistered(RegisteredTypeHandle, *RelationTypeTraits);
		}
	}
}

void FMassEntityManager::OnRelationTypeRegistered(UE::Mass::FTypeHandle RegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& RelationTypeTraits)
{
	if (RelationTypeTraits.RegisterObserversDelegate.IsSet() == false
		|| RelationTypeTraits.RegisterObserversDelegate(*this) == true)
	{
		RelationManager.OnRelationTypeRegistered(RegisteredTypeHandle, RelationTypeTraits);
	}
}

bool FMassEntityManager::BatchCreateRelations(const UE::Mass::FTypeHandle RelationTypeHandle, TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects)
{
	return RelationManager.CreateRelationInstances(RelationTypeHandle, Subjects, Objects).Num() > 0;
}

//-----------------------------------------------------------------------------
// DEBUG stuff
//-----------------------------------------------------------------------------
bool FMassEntityManager::DebugDoCollectionsOverlapCreationContext(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections) const
{
	if (TSharedPtr<FMassObserverManager::FCreationContext> AsSharedPtr = ObserverManager.GetCreationContext())
	{
		TArray<FMassArchetypeEntityCollection> CreationCollections = AsSharedPtr->GetEntityCollections(*this);
		return CreationCollections.GetData() <= EntityCollections.GetData()
			&& EntityCollections.GetData() <= CreationCollections.GetData() + CreationCollections.Num();
	}

	return false;
}

void FMassEntityManager::SetDebugName(const FString& NewDebugGame) 
{ 
#if WITH_MASSENTITY_DEBUG
	DebugName = NewDebugGame; 
#endif // WITH_MASSENTITY_DEBUG
}

#if WITH_MASSENTITY_DEBUG
void FMassEntityManager::DebugPrintArchetypes(FOutputDevice& Ar, const bool bIncludeEmpty) const
{
	Ar.Logf(ELogVerbosity::Log, TEXT("Listing archetypes contained in EntityManager owned by %s"), *GetPathNameSafe(GetOwner()));

	int32 NumBuckets = 0;
	int32 NumArchetypes = 0;
	int32 LongestArchetypeBucket = 0;
	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			if (ArchetypePtr.IsValid() && (bIncludeEmpty == true || ArchetypePtr->GetChunkCount() > 0))
			{
				ArchetypePtr->DebugPrintArchetype(Ar);
			}
		}

		const int32 NumArchetypesInBucket = KVP.Value.Num();
		LongestArchetypeBucket = FMath::Max(LongestArchetypeBucket, NumArchetypesInBucket);
		NumArchetypes += NumArchetypesInBucket;
		++NumBuckets;
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("FragmentHashToArchetypeMap: %d archetypes across %d buckets, longest bucket is %d"),
		NumArchetypes, NumBuckets, LongestArchetypeBucket);
}

void FMassEntityManager::DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty) const
{
	Ar.SetAutoEmitLineTerminator(true);
	for (auto Pair : FragmentHashToArchetypeMap)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\n-----------------------------------\nHash: %u"), Pair.Key);
		for (TSharedPtr<FMassArchetypeData> Archetype : Pair.Value)
		{
			if (Archetype.IsValid() && (bIncludeEmpty == true || Archetype->GetChunkCount() > 0))
			{
				Archetype->DebugPrintArchetype(Ar);
				Ar.Logf(ELogVerbosity::Log, TEXT("+++++++++++++++++++++++++\n"));
			}
		}
	}
}

void FMassEntityManager::DebugGetArchetypeFragmentTypes(const FMassArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutFragmentList) const
{
	if (Archetype.IsValid())
	{
		const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype);
		ArchetypeData.GetCompositionDescriptor().GetFragments().ExportTypes(InOutFragmentList);
	}
}

int32 FMassEntityManager::DebugGetArchetypeEntitiesCount(const FMassArchetypeHandle& Archetype) const
{
	return Archetype.IsValid() ? FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype).GetNumEntities() : 0;
}

int32 FMassEntityManager::DebugGetArchetypeEntitiesCountPerChunk(const FMassArchetypeHandle& Archetype) const
{
	return Archetype.IsValid() ? FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype).GetNumEntitiesPerChunk() : 0;
}

int32 FMassEntityManager::DebugGetEntityCount() const
{
	return GetEntityStorageInterface().Num() - NumReservedEntities - GetEntityStorageInterface().ComputeFreeSize();
}

int32 FMassEntityManager::DebugGetArchetypesCount() const
{
	return AllArchetypes.Num();
}

void FMassEntityManager::DebugRemoveAllEntities()
{
	for (int EntityIndex = NumReservedEntities, EndIndex = GetEntityStorageInterface().Num(); EntityIndex < EndIndex; ++EntityIndex)
	{
		if (GetEntityStorageInterface().IsValid(EntityIndex) == false)
		{
			// already dead
			continue;
		}
		FMassArchetypeData* Archetype = GetEntityStorageInterface().GetArchetype(EntityIndex);
		check(Archetype);
		FMassEntityHandle Entity;
		Entity.Index = EntityIndex;
		Entity.SerialNumber = GetEntityStorageInterface().GetSerialNumber(EntityIndex);
		Archetype->RemoveEntity(Entity);

		GetEntityStorageInterface().ForceReleaseOne(Entity);
	}
}

void FMassEntityManager::DebugForceArchetypeDataVersionBump()
{
	++ArchetypeDataVersion;
}

void FMassEntityManager::DebugGetArchetypeStrings(const FMassArchetypeHandle& Archetype, TArray<FName>& OutFragmentNames, TArray<FName>& OutTagNames)
{
	if (Archetype.IsValid() == false)
	{
		return;
	}

	const FMassArchetypeData& ArchetypeRef = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype);
	
	OutFragmentNames.Reserve(ArchetypeRef.GetFragmentConfigs().Num());
	for (const FMassArchetypeFragmentConfig& FragmentConfig : ArchetypeRef.GetFragmentConfigs())
	{
		checkSlow(FragmentConfig.FragmentType);
		OutFragmentNames.Add(FragmentConfig.FragmentType->GetFName());
	}

	ArchetypeRef.GetTagBitSet().DebugGetIndividualNames(OutTagNames);
}

FMassEntityHandle FMassEntityManager::DebugGetEntityIndexHandle(const int32 EntityIndex) const
{
	return CreateEntityIndexHandle(EntityIndex);
}

const FString& FMassEntityManager::DebugGetName() const
{
	return DebugName;
}

void FMassEntityManager::DebugEnableDebugFeature(EDebugFeatures Features)
{
	EnumAddFlags(EnabledDebugFeatures, Features);
}

void FMassEntityManager::DebugDisableDebugFeature(EDebugFeatures Features)
{
	EnumRemoveFlags(EnabledDebugFeatures, Features);
}

bool FMassEntityManager::DebugHasAllDebugFeatures(EDebugFeatures Features) const
{
	return EnumHasAllFlags(EnabledDebugFeatures, Features);
}

FMassRequirementAccessDetector& FMassEntityManager::GetRequirementAccessDetector()
{
	return RequirementAccessDetector;
}

UE::Mass::FStorageType& FMassEntityManager::DebugGetEntityStorageInterface()
{
	return GetEntityStorageInterface();
}

const UE::Mass::FStorageType& FMassEntityManager::DebugGetEntityStorageInterface() const
{
	return GetEntityStorageInterface();
}

bool FMassEntityManager::DebugHasCommandsToFlush() const
{
	checkfSlow(NumCommandBuffers == 2, TEXT("This check relies on there being two command buffers."));
	return DeferredCommandBuffers[0]->HasPendingCommands() || DeferredCommandBuffers[1]->HasPendingCommands();
}
#endif // WITH_MASSENTITY_DEBUG

//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------
FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const FMassArchetypeData& SourceArchetypeRef, FMassArchetypeCompositionDescriptor&& NewComposition)
{
	return InternalCreateSimilarArchetype(SourceArchetypeRef, Forward<FMassArchetypeCompositionDescriptor>(NewComposition), {});
}

void FMassEntityManager::SetEntityFragmentsValues(FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	SetEntityFragmentValues(Entity, FragmentInstanceList);
}

void FMassEntityManager::BatchSetEntityFragmentsValues(const FMassArchetypeEntityCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	ensureMsgf(false, TEXT("The static BatchSetEntityFragmentsValues is not expected to be called anymore. There's no way to deduce the EntityManager instance related to the call"));
}

void FMassEntityManager::BatchSetEntityFragmentsValues(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	ensureMsgf(false, TEXT("The static BatchSetEntityFragmentsValues is not expected to be called anymore. There's no way to deduce the EntityManager instance related to the call"));
}

#undef CHECK_SYNC_API
#undef CHECK_SYNC_API_RETURN
#undef CHECK_ELEMENT