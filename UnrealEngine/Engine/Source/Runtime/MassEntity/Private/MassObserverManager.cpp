// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverManager.cpp】
//
// 实现 FMassObserverManager 的全部行为。文件结构（按顺序）：
//
// 1. 匿名命名空间 - 内部辅助类型：
//    - FDeprecationHelper：旧 API 的桥接逻辑
//    - FNotificationContext：执行 buffered 通知时传递的上下文
//    - FBufferedNotificationExecutioner：执行非创建型 buffered 通知的 visitor（用 Visit 派发）
//    - FBufferedCreationNotificationExecutioner：执行创建型 buffered 通知的 visitor
//    - FObserverContextIterator：迭代 ObserverExecutionContext 的 CurrentTypeIndex
//    - AddRegisteredObserverProcessorInstances：从 Registry 实例化 observer processor
//
// 2. FMassObserversMap 实现
//
// 3. FMassObserverManager 实现：
//    - Initialize / DeInitialize：从 Registry 加载 / 卸载
//    - OnPostEntitiesCreated 等：变更事件入口
//    - OnCompositionChanged：核心实现（fast path 检查 + 缓冲 OR 立即触发）
//    - HandleElementsImpl：底层 observer 执行
//    - AddObserverInstance / RemoveObserverInstance：动态注册
//    - GetOrMakeObserverLock / GetOrMakeCreationContext：lock 与 context 工厂
//    - ResumeExecution：lock 析构时的派发主循环
//
// 4. 已废弃 API 的兼容实现
// =============================================================================

#include "MassObserverManager.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingTypes.h"
#include "MassObserverRegistry.h"
#include "MassDebugger.h"
#include "MassEntityCollection.h"
#include "MassProcessingContext.h"
#include "MassObserverNotificationTypes.h"
#include "MassObserverProcessor.h"
#include "VisualLogger/VisualLogger.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(MassObserverManager)

namespace UE::Mass::ObserverManager
{
	namespace Tweakables
	{
		// Used as a template parameter for TInlineAllocator that we use when gathering UScriptStruct* of the observed types to process.
		// 【中文注释】栈上 inline allocator 元素数：当从位集导出 UScriptStruct* 列表时，
		// 多数情况下涉及的 type 数量较少（< 8），用 inline allocator 避免堆分配。
		constexpr int InlineAllocatorElementsForOverlapTypes = 8;
	} // Tweakables

	// 【中文注释】调试 / 优化开关：是否合并相邻同类型的缓冲通知。
	// 启用后 AddNotification 会检查上一条是否同类型 + 同 composition，是则就地追加，否则新建。
	// 默认关闭（false），因为合并需要额外的 composition 比较成本，且在常见场景下作用不大。
	bool bCoalesceBufferedNotifications = false;

	namespace Private
	{
		// 【中文注释】控制台变量注册：mass.observers.CoalesceBufferedNotifications
		FAutoConsoleVariableRef ConsoleVariables[] =
		{
			FAutoConsoleVariableRef(TEXT("mass.observers.CoalesceBufferedNotifications"), bCoalesceBufferedNotifications
				, TEXT("If enabled, when buffering new notification we'll check if it's the same type as the previously stored one, and if so then merge the two."), ECVF_Default)
		};

		// a helper function to reduce code duplication in FMassObserverManager::Initialize
		// 【中文注释】Initialize 的核心辅助函数：把 Registry 中的 SoftClassPath 解析成实际 processor 实例并组装 pipeline。
		// 模板参数 TBitSet：FMassFragmentBitSet 或 FMassTagBitSet。
		// 流程：
		//   1. 遍历 Registry.RegisteredObserverTypes（type → [processor classes]）；
		//   2. 对每个 type 解析所有 SoftClassPath：
		//      - 已实例化过的复用（InOutExistingProcessorsMap），避免重复 NewObject；
		//      - 否则用 ShouldExecute 检查执行环境（编辑器/游戏/dedicated 等）后 NewObject；
		//   3. 把 processor 列表追加到 pipeline，按执行优先级排序，调用 Initialize；
		//   4. 把 type 加入镜像位集 InOutObservedBitSet，让 fast path 能识别。
		template<typename TBitSet>
		void AddRegisteredObserverProcessorInstances(FMassEntityManager& EntityManager, const EProcessorExecutionFlags WorldExecutionFlags, UObject& Owner
			, const UMassObserverRegistry::FObserverClassesMap& RegisteredObserverTypes, TBitSet& InOutObservedBitSet, FMassObserversMap& Observers
			, TMap<const TSubclassOf<UMassProcessor>, UMassProcessor*>& InOutExistingProcessorsMap)
		{
			for (auto It : RegisteredObserverTypes)
			{
				const UObject* Type = It.Key.ResolveObjectPtr();
				
				// 【中文注释】type 可能因 module 卸载而失效；空 processor 列表也跳过。
				if (Type == nullptr || It.Value.Num() == 0)
				{
					continue;
				}

				const UScriptStruct* StructType = CastChecked<const UScriptStruct>(Type);
				TArray<TObjectPtr<UMassProcessor>> ObserverProcessors;

				for (const FSoftClassPath& SoftProcessorClass : It.Value)
				{
					if (TSubclassOf<UMassProcessor> ProcessorClass = SoftProcessorClass.ResolveClass())
					{
						// 【中文注释】跨 type 共享 processor 实例：同一个 observer class 可能监听多个 type，
						// 但应只实例化一次。InOutExistingProcessorsMap 缓存已创建的实例供复用。
						if (UMassProcessor** ExistingProcessor = InOutExistingProcessorsMap.Find(ProcessorClass))
						{
							ObserverProcessors.AddUnique(*ExistingProcessor);
						}
						// 【中文注释】CDO.ShouldExecute 检查：observer 是否应在当前世界类型（client/server/dedicated）下执行。
						else if (ProcessorClass->GetDefaultObject<UMassProcessor>()->ShouldExecute(WorldExecutionFlags))
						{
							UMassProcessor* ObserverInstance = NewObject<UMassProcessor>(&Owner, ProcessorClass);							
							ObserverProcessors.Add(ObserverInstance);
							InOutExistingProcessorsMap.Add(ProcessorClass, ObserverInstance);
						}
					}
				}

				if (ObserverProcessors.Num() > 0)
				{
					FMassRuntimePipeline& Pipeline = (*Observers).FindOrAdd(StructType);
					Pipeline.AppendProcessors(MoveTemp(ObserverProcessors));
					Pipeline.SortByExecutionPriority();
					Pipeline.Initialize(Owner, EntityManager.AsShared());
					
					// 【中文注释】关键：把 type 写入镜像位集，OnCompositionChanged 的 fast path 据此筛选。
					InOutObservedBitSet.Add(*StructType);
				}
			}
		};
	} // Private

	// 【中文注释】FDeprecationHelper：旧 API（5.6 / 5.7 之前）的桥接实现。
	// 旧 API 没有显式传 EMassObservedOperation，需要通过对比 HandlersContainer 指针归属反推操作类型。
	struct FDeprecationHelper
	{
		/** determines which observed operation type the given HandlersContainerPtr implements */
		// 【中文注释】DetermineOperationType：通过指针比较反推 HandlersContainer 是哪种 op。
		// 旧 API 只传一个 map 引用，不带 op 信息；新 API 显式传 EMassObservedOperation。
		// 这里只支持 AddElement/RemoveElement（旧 API 的两种基本操作）。
		static EMassObservedOperation DetermineOperationType(const FMassObserversMap* HandlersContainerPtr, const FMassObserversMap* MapArray)
		{
			if (HandlersContainerPtr == &MapArray[static_cast<uint8>(EMassObservedOperation::AddElement)])
			{
				return EMassObservedOperation::AddElement;
			}
			if (HandlersContainerPtr == &MapArray[static_cast<uint8>(EMassObservedOperation::RemoveElement)])
			{
				return EMassObservedOperation::RemoveElement;
			}
			return EMassObservedOperation::MAX;
		};

		// 【中文注释】旧 API 入口：根据 HandlersContainer 归属反推 op，再走新 API 路径。
		static void HandleSingleElement(TNotNull<FMassObserverManager*> ObserverManager, const UScriptStruct& ElementType, const FMassArchetypeEntityCollection& EntityCollection, FMassObserversMap& HandlersContainer)
		{
			const bool bIsFragment = UE::Mass::IsA<FMassFragment>(&ElementType);
			check(bIsFragment || UE::Mass::IsA<FMassTag>(&ElementType));

			const EMassObservedOperation Operation = bIsFragment
				? DetermineOperationType(&HandlersContainer, ObserverManager->EntityManager.GetObserverManager().FragmentObservers)
				: DetermineOperationType(&HandlersContainer, ObserverManager->EntityManager.GetObserverManager().TagObservers);

			HandleSingleElement(ObserverManager, ElementType, EntityCollection, Operation);
		}

		// 【中文注释】HandleSingleElement 的真正实现：构造 ProcessingContext 并直接调用 HandleElementsImpl。
		static void HandleSingleElement(TNotNull<FMassObserverManager*> ObserverManager, const UScriptStruct& ElementType, const FMassArchetypeEntityCollection& EntityCollection, const EMassObservedOperation Operation)
		{
			const int32 OperationIndex = static_cast<int32>(Operation);
			const bool bIsFragment = UE::Mass::IsA<FMassFragment>(&ElementType);
			check(bIsFragment || UE::Mass::IsA<FMassTag>(&ElementType));

			const UScriptStruct* ElementTypePtr = &ElementType;

			UE::Mass::FProcessingContext ProcessingContext(ObserverManager->EntityManager);
			FMassObserverManager::HandleElementsImpl(ProcessingContext, MakeArrayView(&EntityCollection, 1)
				, {Operation, MakeArrayView(&ElementTypePtr, 1)}
				, bIsFragment ? ObserverManager->FragmentObservers[OperationIndex] : ObserverManager->TagObservers[OperationIndex]);
		}
	};

	// 【中文注释】FNotificationContext：派发 buffered 通知时传递的上下文束。
	// 把 ObserverManager 和 ProcessingContext 一起打包，方便 visitor 函数捕获。
	struct FNotificationContext
	{
		FMassObserverManager& ObserverManager;
		FProcessingContext& ProcessingContext;
	};

	// =============================================================================
	// 【FBufferedNotificationExecutioner - 非创建型缓冲通知执行器】
	//
	// 用于 ResumeExecution 时通过 Visit 派发 BufferedNotification.CompositionChange + AffectedEntities。
	// 工作方式：TVariant 的 Visit 函数会按 (CompositionChange, AffectedEntities) 的运行时类型组合，
	// 调用对应的 operator() 重载。本类提供了所有 4 种 composition × 2 种 entity 容器的组合。
	//
	// 重载矩阵：
	//   - (FEmptyComposition, *) → no-op（创建型走另一执行器，这里不应到达）
	//   - (FMassArchetypeCompositionDescriptor, *) → 拆成 fragment + tag 两次调用
	//   - (FMassFragmentBitSet, *) → 仅 fragment observer
	//   - (FMassTagBitSet, *) → 仅 tag observer
	//   - 第二维：FEntityCollection 直接用，FMassEntityHandle 临时构造一个 collection
	//
	// OpType 由调用方在每条通知前设置（见 ResumeExecution 主循环）。
	// =============================================================================
	struct FBufferedNotificationExecutioner 
	{
		FBufferedNotificationExecutioner(FNotificationContext& InNotificationContext)
			: NotificationContext(InNotificationContext)
		{}

		// 【中文注释】(FEmptyComposition, *) 重载：no-op。
		// FEmptyComposition 仅用于 CreateEntity 通知（走 FBufferedCreationNotificationExecutioner），
		// 此处不应被命中；保留作为 visitor 的完备性兜底。
		template<typename TEntities>
		void operator()(const FBufferedNotification::FEmptyComposition&, TEntities)
		{
			// no-op
		}

		// 【中文注释】(ArchetypeComposition, FEntityCollection)：拆成 fragment + tag 各调一次。
		// fragment 与 tag 分属不同的 observer map，需分别派发。
		void operator()(const FMassArchetypeCompositionDescriptor& Change, const FEntityCollection& Entities)
		{
			if (Change.GetFragments().IsEmpty() == false)
			{
				(*this)(Change.GetFragments(), Entities);
			}
			if (Change.GetTags().IsEmpty() == false)
			{
				(*this)(Change.GetTags(), Entities);
			}
		}

		// 【中文注释】(ArchetypeComposition, single handle)：同上但 entity 是单 handle。
		void operator()(const FMassArchetypeCompositionDescriptor& Change, const FMassEntityHandle EntityHandle)
		{
			if (Change.GetFragments().IsEmpty() == false)
			{
				(*this)(Change.GetFragments(), EntityHandle);
			}
			if (Change.GetTags().IsEmpty() == false)
			{
				(*this)(Change.GetTags(), EntityHandle);
			}
		}

		// 【中文注释】(FragmentBitSet, FEntityCollection)：触发 fragment observer。
		// ExportTypes 把位集展开成 UScriptStruct* 数组，作为 ObserverExecutionContext 的 TypesInOperation。
		// HandleElementsImpl 内部会按这些 type 依次执行 pipeline。
		void operator()(const FMassFragmentBitSet& Change, const FEntityCollection& Entities)
		{
			ObservedTypesOverlap.Reset();
			Change.ExportTypes(ObservedTypesOverlap);
			FMassObserverManager::HandleElementsImpl(NotificationContext.ProcessingContext
				, Entities.GetUpToDatePerArchetypeCollections(NotificationContext.ObserverManager.EntityManager)
				, {OpType, ObservedTypesOverlap}
				, NotificationContext.ObserverManager.FragmentObservers[static_cast<uint8>(OpType)]);
		}

		// 【中文注释】(FragmentBitSet, single handle)：临时构造一个 archetype collection，再走相同逻辑。
		void operator()(const FMassFragmentBitSet& Change, const FMassEntityHandle EntityHandle)
		{
			ObservedTypesOverlap.Reset();
			Change.ExportTypes(ObservedTypesOverlap);
			
			FMassArchetypeHandle ArchetypeHandle = NotificationContext.ObserverManager.GetEntityManager().GetArchetypeForEntity(EntityHandle);
			FMassArchetypeEntityCollection AsEntityCollection(MoveTemp(ArchetypeHandle), EntityHandle);

			NotificationContext.ObserverManager.HandleElementsImpl(NotificationContext.ProcessingContext
				, MakeArrayView(&AsEntityCollection, 1)
				, {OpType, ObservedTypesOverlap}
				, NotificationContext.ObserverManager.FragmentObservers[static_cast<uint8>(OpType)]);
		}

		// 【中文注释】(TagBitSet, FEntityCollection)：触发 tag observer。
		void operator()(const FMassTagBitSet& Change, const FEntityCollection& Entities)
		{
			ObservedTypesOverlap.Reset();
			Change.ExportTypes(ObservedTypesOverlap);
			FMassObserverManager::HandleElementsImpl(NotificationContext.ProcessingContext
				, Entities.GetUpToDatePerArchetypeCollections(NotificationContext.ObserverManager.EntityManager)
				, {OpType, ObservedTypesOverlap}
				, NotificationContext.ObserverManager.TagObservers[static_cast<uint8>(OpType)]);
		}

		// 【中文注释】(TagBitSet, single handle)：tag observer + 单 handle。
		void operator()(const FMassTagBitSet& Change, const FMassEntityHandle EntityHandle)
		{
			ObservedTypesOverlap.Reset();
			Change.ExportTypes(ObservedTypesOverlap);

			FMassArchetypeHandle ArchetypeHandle = NotificationContext.ObserverManager.GetEntityManager().GetArchetypeForEntity(EntityHandle);
			FMassArchetypeEntityCollection AsEntityCollection(MoveTemp(ArchetypeHandle), EntityHandle);

			NotificationContext.ObserverManager.HandleElementsImpl(NotificationContext.ProcessingContext
				, MakeArrayView(&AsEntityCollection, 1)
				, {OpType, ObservedTypesOverlap}
				, NotificationContext.ObserverManager.TagObservers[static_cast<uint8>(OpType)]);
		}

		// 【中文注释】栈上缓冲：用 inline allocator 避免每次 ExportTypes 都堆分配。
		TArray<const UScriptStruct*, TInlineAllocator<ObserverManager::Tweakables::InlineAllocatorElementsForOverlapTypes>> ObservedTypesOverlap;
		FNotificationContext& NotificationContext;
		// 【中文注释】当前正在派发的操作类型，由 ResumeExecution 主循环每次更新。
		EMassObservedOperation OpType;
	};

	// =============================================================================
	// 【FBufferedCreationNotificationExecutioner - 创建型缓冲通知执行器】
	//
	// 专门处理 CreateEntity 类型的缓冲通知。
	// 与普通 executor 不同，创建通知没有 CompositionChange（FEmptyComposition），
	// 因为创建事件不是"变更某些 fragment"而是"出现一批新 entity"。
	// =============================================================================
	struct FBufferedCreationNotificationExecutioner
	{
		explicit FBufferedCreationNotificationExecutioner(FNotificationContext& InNotificationContext)
			: NotificationContext(InNotificationContext)
		{}

		// 【中文注释】FEntityCollection 路径：所有创建实体已分桶，调用 OnCollectionsCreatedImpl 一次性执行。
		void operator()(const FEntityCollection& Entities) const
		{
			NotificationContext.ObserverManager.OnCollectionsCreatedImpl(NotificationContext.ProcessingContext
				, Entities.GetUpToDatePerArchetypeCollections(NotificationContext.ObserverManager.EntityManager));
		}

		// 【中文注释】单 handle 路径：取出 archetype 的完整 composition，按 CreateEntity 调用 OnCompositionChanged。
		// 注意：这里 composition 是 archetype 的完整组成（不是 delta），因为创建时所有 fragment 都"加上"了。
		void operator()(const FMassEntityHandle EntityHandle) const
		{
			const FMassArchetypeHandle ArchetypeHandle = NotificationContext.ObserverManager.GetEntityManager().GetArchetypeForEntity(EntityHandle);
			const FMassArchetypeCompositionDescriptor& ArchetypeComposition = NotificationContext.ProcessingContext.GetEntityManager()->GetArchetypeComposition(ArchetypeHandle);
			NotificationContext.ObserverManager.OnCompositionChanged(EntityHandle, ArchetypeComposition, EMassObservedOperation::CreateEntity, &NotificationContext.ProcessingContext);
		}
		FNotificationContext& NotificationContext;
	};

	/** Trivial type with a sole responsibility of initializing and incrementing FMassObserverExecutionContext.CurrentTypeIndex */
	// =============================================================================
	// 【FObserverContextIterator - observer 执行上下文迭代器】
	//
	// 让 HandleElementsImpl 中的循环结构（for ContextIterator; ContextIterator; ++ContextIterator）
	// 自动维护 RuntimeContext.CurrentTypeIndex。
	//
	// 用法：
	//   for (FObserverContextIterator It(Ctx); It; ++It) { ... use Ctx.GetCurrentType() ... }
	//
	// 设计目的：observer 在执行时通过 ExecutionContext.GetAuxData<FMassObserverExecutionContext>().GetCurrentType()
	// 知道"我现在被触发是因为哪个 fragment/tag"。一次组合变更可能涉及多个 type，迭代器顺次推进。
	// =============================================================================
	struct FObserverContextIterator
	{
		FObserverContextIterator(FMassObserverExecutionContext& InRuntimeContext)
			: RuntimeContext(InRuntimeContext)
		{
			// 【中文注释】初始化为 0：第一次执行时 GetCurrentType() 返回 TypesInOperation[0]。
			RuntimeContext.CurrentTypeIndex = 0;
		}

		int32 operator++()
		{
			return ++RuntimeContext.CurrentTypeIndex;
		}

		// 【中文注释】循环条件：CurrentTypeIndex 是否在 TypesInOperation 范围内（IsValid 检查）。
		operator bool() const
		{
			return RuntimeContext.IsValid();
		}

	private:
		FMassObserverExecutionContext& RuntimeContext;
	};
} // UE::Mass::ObserverManager

//----------------------------------------------------------------------//
// FMassObserversMap
//----------------------------------------------------------------------//
// 【中文注释】调试用：把 map 中所有 pipeline 的 processor 实例去重后追加到输出数组。
// 仅在 WITH_MASSENTITY_DEBUG 下编译实体逻辑，发布构建为空函数。
void FMassObserversMap::DebugAddUniqueProcessors(TArray<const UMassProcessor*>& OutProcessors) const
{
#if WITH_MASSENTITY_DEBUG
	for (const auto& MapElement : Container)
	{
		for (const UMassProcessor* Processor : MapElement.Value.GetProcessorsView())
		{
			ensure(Processor);
			OutProcessors.AddUnique(Processor);
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

//----------------------------------------------------------------------//
// FMassObserverManager
//----------------------------------------------------------------------//
// 【中文注释】FCollectionRefOrHandle::DummyCollection 静态成员定义：仅用作占位的引用目标。
FMassArchetypeEntityCollection FMassObserverManager::FCollectionRefOrHandle::DummyCollection;

// 【中文注释】默认构造：仅用于 USTRUCT 反射默认实例化路径。
// 通过 MassEntitySubsystem 的 CDO 拿到 EntityManager 引用 —— 这一步在引擎启动早期可能失败，
// 所以正常使用应通过 FMassObserverManager(FMassEntityManager&) 构造。
FMassObserverManager::FMassObserverManager()
	: EntityManager(GetMutableDefault<UMassEntitySubsystem>()->GetMutableEntityManager())
{

}

// 【中文注释】真正的构造：由 FMassEntityManager 创建自己时调用。
FMassObserverManager::FMassObserverManager(FMassEntityManager& Owner)
	: EntityManager(Owner)
{

}

// 【中文注释】调试：收集所有 op × {fragment, tag} 维度下的 processor 实例（去重后）。
void FMassObserverManager::DebugGatherUniqueProcessors(TArray<const UMassProcessor*>& OutProcessors) const
{
#if WITH_MASSENTITY_DEBUG
	for (int32 OperationIndex = 0; OperationIndex < static_cast<uint32>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		FragmentObservers[OperationIndex].DebugAddUniqueProcessors(OutProcessors);
		TagObservers[OperationIndex].DebugAddUniqueProcessors(OutProcessors);
	}
#endif // WITH_MASSENTITY_DEBUG
}

// 【中文注释】Initialize：从 UMassObserverRegistry 加载所有 observer 配置并实例化 processor。
// 流程：
//   1. 拿到 Registry CDO（全局单例）；
//   2. 通过 Owner（EntityManager 的 outer，通常是 UMassEntitySubsystem）反查 World，
//      推断当前世界的 EProcessorExecutionFlags（client/server/dedicated）；
//   3. TransientProcessorsMap 跨调用复用 processor 实例（同一 class 监听多个 type 时只 NewObject 一次）；
//   4. 对每个 op 索引，分别处理 fragment 和 tag 的 ObserverMap；
//   5. 注册 debugger 的 processor 数据 provider；
//   6. 订阅 module 卸载事件，热更新时清理失效 observer。
void FMassObserverManager::Initialize()
{
	// instantiate initializers
	const UMassObserverRegistry& Registry = UMassObserverRegistry::Get();

	UObject* Owner = EntityManager.GetOwner();
	check(Owner);
	const UWorld* World = Owner->GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = UE::Mass::Utils::DetermineProcessorExecutionFlags(World);

	TMap<const TSubclassOf<UMassProcessor>, UMassProcessor*> TransientProcessorsMap;
	using UE::Mass::ObserverManager::Private::AddRegisteredObserverProcessorInstances;
	for (int i = 0; i < (int)EMassObservedOperation::MAX; ++i)
	{
		// 【中文注释】fragment 维度：Registry.FragmentObserverMaps[i] → FragmentObservers[i]，同时填充 ObservedFragments[i]。
		AddRegisteredObserverProcessorInstances(EntityManager, WorldExecutionFlags, *Owner, Registry.FragmentObserverMaps[i], ObservedFragments[i], FragmentObservers[i], TransientProcessorsMap);
		// 【中文注释】tag 维度：Registry.TagObserverMaps[i] → TagObservers[i]，同时填充 ObservedTags[i]。
		AddRegisteredObserverProcessorInstances(EntityManager, WorldExecutionFlags, *Owner, Registry.TagObserverMaps[i], ObservedTags[i], TagObservers[i], TransientProcessorsMap);
	}

#if WITH_MASSENTITY_DEBUG
	// 【中文注释】调试：注册一个数据提供器到 FMassDebugger，让外部工具能查询当前 observer 列表。
	FMassDebugger::RegisterProcessorDataProvider(TEXT("Observers"), EntityManager.AsShared()
		, [WeakManager = EntityManager.AsWeak()](TArray<const UMassProcessor*>& OutProcessors)
	{
		if (TSharedPtr<FMassEntityManager> SharedEntityManager = WeakManager.Pin())
		{
			FMassObserverManager& ObserverManager = SharedEntityManager->GetObserverManager();
			ObserverManager.DebugGatherUniqueProcessors(OutProcessors);
		}
	});
#endif // WITH_MASSENTITY_DEBUG

	// 【中文注释】订阅"模块卸载完成"事件：当 game module hot-reload 时清理引用了被卸载 UScriptStruct 的 observer 项。
	if (!ModulesUnloadedHandle.IsValid())
	{
		ModulesUnloadedHandle = FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate.AddRaw(this, &FMassObserverManager::OnModulePackagesUnloaded);
	}
}

// 【中文注释】DeInitialize：反注册回调，清空所有 pipeline。EntityManager 销毁时调用。
// 注意：UPROPERTY FMassObserversMap 中的 processor 引用一旦清空，下一轮 GC 会回收 processor 实例。
void FMassObserverManager::DeInitialize()
{
	if (ModulesUnloadedHandle.IsValid())
	{
		FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate.Remove(ModulesUnloadedHandle);
		ModulesUnloadedHandle.Reset();
	}

	for (int32 i = 0; i < (int32)EMassObservedOperation::MAX; ++i)
	{
		(*FragmentObservers[i]).Empty();
		(*TagObservers[i]).Empty();
	}
}

// 【中文注释】OnPostEntitiesCreated：实体创建后的 observer 入口（collection 版本）。
// 流程：
//   1. 若 lock 中（LocksCount > 0），把整批 collection 缓冲到 lock 的"创建通知"项，立即返回 false；
//   2. 否则取出 archetype 完整 composition，按 CreateEntity 触发 observer。
// 返回值：true 表示有 observer 立即执行；false 表示无 observer 或被缓冲。
bool FMassObserverManager::OnPostEntitiesCreated(const FMassArchetypeEntityCollection& EntityCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_OnPostEntitiesCreated_Collection);

	if (LocksCount > 0)
	{
		TSharedRef<FObserverLock> ObserverLock = ActiveObserverLock.Pin().ToSharedRef();
		ObserverLock->AddCreatedEntitiesCollection(EntityCollection);
		return false;
	}

	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager.GetArchetypeComposition(EntityCollection.GetArchetype());
	return OnCompositionChanged(EntityCollection, ArchetypeComposition, EMassObservedOperation::CreateEntity);
}

// 【中文注释】OnPostEntityCreated：单实体创建版本。
// Composition 为空时从 archetype 反查（兼容某些上层只传 entity handle 的路径）。
bool FMassObserverManager::OnPostEntityCreated(FMassEntityHandle EntityHandle, const FMassArchetypeCompositionDescriptor& Composition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_OnPostEntitiesCreated_Collection);

	if (LocksCount > 0)
	{
		TSharedRef<FObserverLock> ObserverLock = ActiveObserverLock.Pin().ToSharedRef();
		ObserverLock->AddCreatedEntity(EntityHandle);
		return false;
	}

	if (Composition.IsEmpty())
	{
		const FMassArchetypeHandle ArchetypeHandle = EntityManager.GetArchetypeForEntity(EntityHandle);
		const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager.GetArchetypeComposition(ArchetypeHandle);
		return OnCompositionChanged(EntityHandle, ArchetypeComposition, EMassObservedOperation::CreateEntity);
	}

	return OnCompositionChanged(EntityHandle, Composition, EMassObservedOperation::CreateEntity);
}

// 【中文注释】OnPreEntitiesDestroyed：实体销毁前 observer 入口。
// "Pre" 语义：实体还活着，observer 可以读取所有数据；这是 buffer 不友好的路径
// （buffer 后数据已删除，PreDestroy observer 看不到旧数据）。
bool FMassObserverManager::OnPreEntitiesDestroyed(const FMassArchetypeEntityCollection& EntityCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPreEntitiesDestroyed")
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager.GetArchetypeComposition(EntityCollection.GetArchetype());
	return OnCompositionChanged(EntityCollection, ArchetypeComposition, EMassObservedOperation::DestroyEntity);
}

// 【中文注释】带 ProcessingContext 的 OnPreEntitiesDestroyed：复用上层 processing 上下文。
bool FMassObserverManager::OnPreEntitiesDestroyed(UE::Mass::FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPreEntitiesDestroyed")
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager.GetArchetypeComposition(EntityCollection.GetArchetype());	
	return OnCompositionChanged(EntityCollection, ArchetypeComposition, EMassObservedOperation::DestroyEntity, &ProcessingContext);
}

// 【中文注释】单实体销毁前版本。
bool FMassObserverManager::OnPreEntityDestroyed(const FMassArchetypeCompositionDescriptor& ArchetypeComposition, const FMassEntityHandle Entity)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPreEntityDestroyed")
	return OnCompositionChanged(Entity, ArchetypeComposition, EMassObservedOperation::DestroyEntity);
}

// =============================================================================
// 【OnCompositionChanged - 核心实现】
//
// 整个 observer 系统的中枢函数。所有 OnPost*/OnPre* 入口最终都汇聚到这里。
//
// 流程总览：
//   1. 校验 collection 是否最新；空 delta 直接返回 false（fast path）；
//   2. 处理 lock 期间的创建上下文标脏（CreationContext 的 collection 失效）；
//   3. 计算 delta 与 ObservedFragments/ObservedTags 的交集（第二级 fast path）；
//      - 无交集 → return false，整个 observer 流程跳过；
//      - 有交集 → 进入派发分支：
//          a. 若 lock 中：把通知缓冲到 lock；
//          b. 否则：立即执行 fragment + tag observer 各一轮。
//
// 关键性能点：第 1 步和第 3 步的 fast path 都是 O(1)（位集 IsEmpty / HasAny），
// 让"绝大多数变更不涉及 observer"的常见情况几乎无开销。
// =============================================================================
bool FMassObserverManager::OnCompositionChanged(FCollectionRefOrHandle&& EntityCollection, const FMassArchetypeCompositionDescriptor& CompositionDelta
	, const EMassObservedOperation Operation, UE::Mass::FProcessingContext* ProcessingContext)
{
	using UE::Mass::ObserverManager::Tweakables::InlineAllocatorElementsForOverlapTypes;
	ensureMsgf(EntityCollection.EntityHandle.IsValid() || EntityCollection.EntityCollection.IsUpToDate()
		, TEXT("Out-of-date FMassArchetypeEntityCollection used. Stored information is unreliable."));

	if (CompositionDelta.IsEmpty())
	{
		// nothing to do here.
		// @todo calling this function just to bail out would be a lot cheaper if we didn't have to create
		// FMassArchetypeCompositionDescriptor instances first - we usually just pass in tag or fragment bitsets.
		// like in FMassEntityManager::BatchChangeTagsForEntities
		// 【中文注释】TODO 提示：上层有时为了"调用一次"会先构造完整 descriptor，但实际只用其中一部分；
		// 优化方向是直接接受 bitset 重载，跳过 descriptor 构造。
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_OnCompositionChanged);

	if (LocksCount > 0)
	{
		// 【中文注释】lock 期间出现组合变更：如果当前还有活跃 CreationContext，必须把它的 collection 标脏。
		// 因为 entity 的 archetype 变了，CreationContext 缓存的 per-archetype 集合不再准确。
		// 同时这里直接 return false，把"加进缓冲"的工作交给 CreationContext 的处理路径
		// （注：只在 CreationContext 活跃时这样做，普通 lock 仍走下面的缓冲分支）。
		if (TSharedPtr<FCreationContext> CreationContext = GetCreationContext())
		{
			// a composition mutating operation is taking place, while creation lock is active - this operation invalidates the stored collections
			CreationContext->MarkDirty();
			return false;
		}
		// 【中文注释】lock 期间执行 Remove/DestroyEntity：警告 observer 看不到被删数据。
		UE_CVLOG_UELOG(Operation == EMassObservedOperation::RemoveElement || Operation == EMassObservedOperation::DestroyEntity
			, EntityManager.GetOwner(), LogMass, Log
			, TEXT("%hs: Remove operation while observers are locked - the remove-observer will be executed after the data has already been removed."), __FUNCTION__);
	}

	const int32 OperationIndex = static_cast<int32>(Operation);

	// 【中文注释】---- 第二级 fast path：求 delta 与"被观察"位集的交集 ----
	// 仅当交集非空才需要触发 observer。交集结果 FragmentOverlap/TagOverlap 即"实际要触发的 type 集合"。
	FMassFragmentBitSet FragmentOverlap = ObservedFragments[OperationIndex].GetOverlap(CompositionDelta.GetFragments());
	FMassTagBitSet TagOverlap = ObservedTags[OperationIndex].GetOverlap(CompositionDelta.GetTags());
	const bool bHasFragmentsOverlap = !FragmentOverlap.IsEmpty();
	const bool bHasTagsOverlap = !TagOverlap.IsEmpty();

	if (bHasFragmentsOverlap || bHasTagsOverlap)
	{
		if (LocksCount > 0)
		{
			// =============================================================================
			// 【缓冲分支：lock 中】
			// =============================================================================
			TSharedRef<FObserverLock> ObserverLockRef = ActiveObserverLock.Pin().ToSharedRef();

			// UE::Mass::FEntityCollection(EntityCollection) OR EntityHandle
			if (UE::Mass::ObserverManager::bCoalesceBufferedNotifications)
			{
				// 【中文注释】合并模式：调用 AddNotification 让 lock 内部尝试与上一条相同类型的通知合并。
				TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_Notify_Coalesced);

				if (EntityCollection.EntityHandle.IsSet())
				{
					ObserverLockRef->AddNotification(Operation, EntityCollection.EntityHandle
						, bHasFragmentsOverlap, MoveTemp(FragmentOverlap)
						, bHasTagsOverlap, MoveTemp(TagOverlap));
				}
				else
				{
					ObserverLockRef->AddNotification(Operation, EntityCollection.EntityCollection
						, bHasFragmentsOverlap, MoveTemp(FragmentOverlap) 
						, bHasTagsOverlap, MoveTemp(TagOverlap));
				}
			}
			else
			{
				// 【中文注释】非合并模式：每次都新建一条 BufferedNotification。
				// 根据 fragment/tag 重叠情况选择最紧凑的 composition 形态：
				//   - 都有 → FMassArchetypeCompositionDescriptor（包含两个位集）
				//   - 仅 fragment → FMassFragmentBitSet
				//   - 仅 tag → FMassTagBitSet
				// 这种紧凑表达减少内存占用。
				TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_Notify_Emplace);

				FBufferedNotification::FEntitiesContainer Entities;
				if (EntityCollection.EntityHandle.IsSet())
				{
					Entities.Emplace<FMassEntityHandle>(EntityCollection.EntityHandle);
				}
				else
				{
					Entities.Emplace<UE::Mass::FEntityCollection>(EntityCollection.EntityCollection);
				}

				if (bHasFragmentsOverlap && bHasTagsOverlap)
				{
					FMassArchetypeCompositionDescriptor ChangedComposition(MoveTemp(FragmentOverlap), MoveTemp(TagOverlap), {}, {}, {});
					ObserverLockRef->BufferedNotifications.Emplace(Operation, MoveTemp(ChangedComposition), MoveTemp(Entities));
				}
				else if (bHasFragmentsOverlap)
				{
					ObserverLockRef->BufferedNotifications.Emplace(Operation, MoveTemp(FragmentOverlap), MoveTemp(Entities));
				}
				else // bHasTagsOverlap
				{
					ObserverLockRef->BufferedNotifications.Emplace(Operation, MoveTemp(TagOverlap), MoveTemp(Entities));
				}
			}
		}
		else
		{
			// =============================================================================
			// 【立即执行分支：无 lock】
			// =============================================================================
			// 【中文注释】Lambda HandleElements：把"具体怎么执行"的逻辑封装一次，
			// 然后根据 EntityCollection.EntityHandle 是否有效决定走 handle 路径还是 collection 路径。
			auto HandleElements = [&](const FMassArchetypeEntityCollection& Collection)
			{
				TArray<const UScriptStruct*, TInlineAllocator<InlineAllocatorElementsForOverlapTypes>> ObservedTypesOverlap;

				// 【中文注释】LocalProcessingContext 处理：调用方可能没传 ProcessingContext，
				// 此处用 placement new 在栈缓冲区上"伪堆分配"一个临时 context，避免堆分配开销。
				// 退出时手动调用析构（见下方 ~FProcessingContext()）。
				alignas(UE::Mass::FProcessingContext) uint8 LocalContextBuffer[sizeof(UE::Mass::FProcessingContext)];
				UE::Mass::FProcessingContext* LocalProcessingContext = (ProcessingContext == nullptr)
					? new(&LocalContextBuffer) UE::Mass::FProcessingContext(EntityManager, /*DeltaSeconds=*/0.f, /*bFlushCommandBuffer=*/false)
					: ProcessingContext;

				if (bHasFragmentsOverlap)
				{
					// 【中文注释】把 fragment 位集导出为 UScriptStruct* 列表，作为 ObserverExecutionContext 的 TypesInOperation。
					FragmentOverlap.ExportTypes(ObservedTypesOverlap);

					HandleElementsImpl(*LocalProcessingContext, {Collection}, {Operation, ObservedTypesOverlap}, FragmentObservers[OperationIndex]);
				}

				if (bHasTagsOverlap)
				{
					ObservedTypesOverlap.Reset();
					TagOverlap.ExportTypes(ObservedTypesOverlap);

					HandleElementsImpl(*LocalProcessingContext, {Collection}, {Operation, ObservedTypesOverlap}, TagObservers[OperationIndex]);
				}

				// 【中文注释】手动析构栈上构造的 FProcessingContext。
				if (ProcessingContext == nullptr)
				{
					LocalProcessingContext->~FProcessingContext();
				}
			};

			if (EntityCollection.EntityHandle.IsSet())
			{
				// 【中文注释】单 handle：临时构造一个只含此 handle 的 archetype collection。
				const FMassArchetypeHandle ArchetypeHandle = EntityManager.GetArchetypeForEntity(EntityCollection.EntityHandle);
				HandleElements(FMassArchetypeEntityCollection(ArchetypeHandle, EntityCollection.EntityHandle));
			}
			else
			{
				HandleElements(EntityCollection.EntityCollection);
			}

			return true;
		}
	}

	return false;
}

// =============================================================================
// 【OnCollectionsCreatedImpl - 多 collection 的 CreateEntity observer 入口】
//
// 用途：lock 释放时（FBufferedCreationNotificationExecutioner）批量处理已收集的创建实体集合。
// 与 OnCompositionChanged 不同，这里"composition" = "所有 archetype 的并集" —— 因为创建事件涉及整个 archetype。
//
// 流程：
//   1. 断言 LocksCount == 0（必须在 ResumeExecution 后才能调用）；
//   2. 把所有 collection 的 archetype composition 累加成总位集；
//   3. 与 ObservedFragments[Create]/ObservedTags[Create] 求交集；
//   4. 有交集则一次性触发所有 fragment + tag observer。
// =============================================================================
bool FMassObserverManager::OnCollectionsCreatedImpl(UE::Mass::FProcessingContext& ProcessingContext, const TConstArrayView<FMassArchetypeEntityCollection> EntityCollections)
{
	using UE::Mass::ObserverManager::Tweakables::InlineAllocatorElementsForOverlapTypes;

	TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_OnCollectionsCreatedImpl_Collection);

	check(LocksCount == 0);

	constexpr EMassObservedOperation Operation = EMassObservedOperation::CreateEntity;
	constexpr int32 OperationIndex = static_cast<int32>(Operation);

	FMassFragmentBitSet FragmentOverlap;
	FMassTagBitSet TagOverlap;

	// 【中文注释】累加所有 collection 的 archetype composition：因为同一批创建可能跨多个 archetype，
	// 这里把所有 fragment/tag 都汇总成单一位集，再统一与"被观察"位集求交。
	for (FMassArchetypeEntityCollection Collection : EntityCollections)
	{
		checkfSlow(Collection.IsUpToDate(), TEXT("Out-of-date FMassArchetypeEntityCollection used. Stored information is unreliable."));

		const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager.GetArchetypeComposition(Collection.GetArchetype());
		FragmentOverlap += ArchetypeComposition.GetFragments();
		TagOverlap += ArchetypeComposition.GetTags();
	}
	FragmentOverlap = ObservedFragments[OperationIndex].GetOverlap(FragmentOverlap);
	TagOverlap = ObservedTags[OperationIndex].GetOverlap(TagOverlap);

	const bool bHasFragmentsOverlap = !FragmentOverlap.IsEmpty();
	const bool bHasTagsOverlap = !TagOverlap.IsEmpty();
	if (bHasFragmentsOverlap || bHasTagsOverlap)
	{
		TArray<const UScriptStruct*, TInlineAllocator<InlineAllocatorElementsForOverlapTypes>> ObservedTypesOverlap;

		if (bHasFragmentsOverlap)
		{
			FragmentOverlap.ExportTypes(ObservedTypesOverlap);
			HandleElementsImpl(ProcessingContext, EntityCollections, {Operation, ObservedTypesOverlap}, FragmentObservers[OperationIndex]);
		}

		if (bHasTagsOverlap)
		{
			ObservedTypesOverlap.Reset();
			TagOverlap.ExportTypes(ObservedTypesOverlap);
			HandleElementsImpl(ProcessingContext, EntityCollections, {Operation, ObservedTypesOverlap}, TagObservers[OperationIndex]);
		}

		return true;
	}
	return false;
}

// =============================================================================
// 【HandleElementsImpl - observer 执行的最底层】
//
// 由所有路径汇聚于此。职责：
//   1. 进入 EntityManager 的 ProcessingScope（保证嵌套调用安全）；
//   2. 把 ObserverContext 安装到 ProcessingContext.AuxData，让 observer 能通过 GetAuxData 访问；
//   3. 依次执行 ObserverContext.TypesInOperation 中每个 type 对应的 pipeline。
//
// 关键循环：FObserverContextIterator 维护 CurrentTypeIndex，每次推进让 GetCurrentType() 返回下一个 type。
//   - 一次组合变更如果同时加 5 个 fragment（且都被观察），则循环 5 次，
//     每次执行该 fragment 对应的 pipeline；
//   - observer processor 在执行时可通过 ExecCtx.GetAuxData<FMassObserverExecutionContext>().GetCurrentType()
//     获知"我现在被触发是因为哪个具体类型"。
// =============================================================================
void FMassObserverManager::HandleElementsImpl(UE::Mass::FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, FMassObserverExecutionContext&& ObserverContext, FMassObserversMap& HandlersContainer)
{	
	TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_HandleElementsImpl);

	check(ObserverContext.GetTypesInOperation().Num());
	ensureMsgf(EntityCollections.Num(), TEXT("Empty collections array is unexpected at this point. Nothing bad will happen, but it's a waste of perf."));

	// 【中文注释】NewProcessingScope：进入 EntityManager 的"正在处理"状态，
	// 期间会延迟所有结构变更，避免 observer 修改的数据被立即应用而引发逻辑错乱。
	FMassEntityManager::FScopedProcessing ProcessingScope = ProcessingContext.EntityManager->NewProcessingScope();

	// @todo maybe we could make this type configurable, so that project-specific code can extend it? 
	// 【中文注释】把 ObserverContext 安装到 AuxData。observer processor 通过：
	//   ExecutionContext.GetAuxData<FMassObserverExecutionContext>()
	// 获取此对象，从而知道 GetOperationType / GetCurrentType / GetTypesInOperation。
	FMassObserverExecutionContext& RuntimeObserverContext = ProcessingContext.AuxData.InitializeAs<FMassObserverExecutionContext>(MoveTemp(ObserverContext));

	// 【中文注释】主循环：每次迭代 RuntimeObserverContext.CurrentTypeIndex 自动 +1，
	// 找到对应 type 的 pipeline 并执行。FindChecked 假定 type 在 map 中存在
	// （由 fast path 保证：ObservedFragments 中有的 type 一定在 FragmentObservers map 中有 pipeline）。
	for (UE::Mass::ObserverManager::FObserverContextIterator ContextIterator(RuntimeObserverContext); ContextIterator; ++ContextIterator)
	{
		FMassRuntimePipeline& Pipeline = (*HandlersContainer).FindChecked(RuntimeObserverContext.GetCurrentType());

		// 【中文注释】真正执行 pipeline 中所有 processor，作用于给定的 EntityCollections。
		UE::Mass::Executor::RunProcessorsView(Pipeline.GetMutableProcessors(), ProcessingContext, EntityCollections);
	}
}

// 【中文注释】AddObserverInstance：动态注册一个 observer processor。
// 流程：
//   1. 判断 ElementType 是 fragment 还是 tag，选择对应的 ObserversMap 数组；
//   2. 遍历每个 op 位（Add/Remove/Create/Destroy），如果在 OperationFlags 中：
//      a. 把 type 加入对应的镜像位集 Observed*；
//      b. 在对应的 pipeline 中追加 processor（去重）；
//      c. 仅在第一次成功追加时调用 ObserverProcessor->CallInitialize（避免重复初始化）；
//      d. 重新按优先级排序 pipeline。
void FMassObserverManager::AddObserverInstance(TNotNull<const UScriptStruct*> ElementType, const EMassObservedOperationFlags OperationFlags, TNotNull<UMassProcessor*> ObserverProcessor)
{
	const bool bIsFragment = UE::Mass::IsA<FMassFragment>(ElementType);
	checkSlow(bIsFragment || UE::Mass::IsA<FMassTag>(ElementType));

	FMassObserversMap* ObserversMap = bIsFragment
		? FragmentObservers
		: TagObservers;

	bool bInitializeCalled = false;

	for (uint8 OperationIndex = 0; OperationIndex < static_cast<uint8>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		if (!!(OperationFlags & static_cast<EMassObservedOperationFlags>(1 << OperationIndex)))
		{
			if (bIsFragment)
			{
				ObservedFragments[OperationIndex].Add(*ElementType);
			}
			else
			{
				ObservedTags[OperationIndex].Add(*ElementType);
			}

			FMassRuntimePipeline& Pipeline = (*ObserversMap[OperationIndex]).FindOrAdd(ElementType);
			if (Pipeline.AppendUniqueProcessor(*ObserverProcessor) && !bInitializeCalled)
			{
				if (UObject* Owner = EntityManager.GetOwner())
				{	
					ObserverProcessor->CallInitialize(Owner, EntityManager.AsShared());
					bInitializeCalled = true;
				}

				Pipeline.SortByExecutionPriority();
			}
		}
	}
}

// 【中文注释】单一 op 的便捷重载：把 EMassObservedOperation 转成 flag bit 后转发。
// 注：函数体里有兜底逻辑应对 Operation == MAX（理论上不该发生，被 ensureAlways 拦下了）。
void FMassObserverManager::AddObserverInstance(const UScriptStruct& ElementType, const EMassObservedOperation Operation, UMassProcessor& ObserverProcessor)
{
	ensureAlways(Operation < EMassObservedOperation::MAX);
	check(Operation != EMassObservedOperation::MAX);

	EMassObservedOperationFlags OperationFlag = (Operation < EMassObservedOperation::MAX)
		? static_cast<EMassObservedOperationFlags>(1 << static_cast<uint8>(Operation))
		: (Operation == EMassObservedOperation::Add
			? EMassObservedOperationFlags::Add
			: EMassObservedOperationFlags::Remove);

	AddObserverInstance(&ElementType, OperationFlag, &ObserverProcessor);
}

// 【中文注释】最高层入口：根据 UMassObserverProcessor 自身的配置自动注册到所有声明的 op + type。
void FMassObserverManager::AddObserverInstance(TNotNull<UMassObserverProcessor*> ObserverProcessor)
{
	AddObserverInstance(ObserverProcessor->GetObservedTypeChecked(), ObserverProcessor->GetObservedOperations(), ObserverProcessor);
}

// 【中文注释】RemoveObserverInstance：注销 observer。
// 关键点：当某个 (type, op) 上的 pipeline 变空时，必须从 map 中删除整个条目，
// 并从镜像位集 Observed* 中移除对应位 —— 这样 fast path 才不会把"已无 observer"的 type 当作"被观察"。
void FMassObserverManager::RemoveObserverInstance(TNotNull<const UScriptStruct*> ElementType, const EMassObservedOperationFlags OperationFlags, TNotNull<UMassProcessor*> ObserverProcessor)
{
	const bool bIsFragment = UE::Mass::IsA<FMassFragment>(ElementType);

	if (!ensure(bIsFragment || UE::Mass::IsA<FMassTag>(ElementType)))
	{
		return;
	}

	FMassObserversMap* ObserversMap = bIsFragment
		? FragmentObservers
		: TagObservers;

	for (uint8 OperationIndex = 0; OperationIndex < static_cast<uint8>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		if (!!(OperationFlags & static_cast<EMassObservedOperationFlags>(1 << OperationIndex)))
		{
			FMassRuntimePipeline* Pipeline = (*ObserversMap[OperationIndex]).Find(ElementType);
			if (ensureMsgf(Pipeline, TEXT("Trying to remove an observer for a fragment/tag that does not seem to be observed.")))
			{
				Pipeline->RemoveProcessor(*ObserverProcessor);
				if (Pipeline->Num() == 0)
				{
					(*ObserversMap[OperationIndex]).Remove(ElementType);
					if (bIsFragment)
					{
						ObservedFragments[OperationIndex].Remove(*ElementType);
					}
					else
					{
						ObservedTags[OperationIndex].Remove(*ElementType);
					}
				}
			}
		}
	}
}

// 【中文注释】单一 op 的便捷重载，参考 AddObserverInstance 的注释。
void FMassObserverManager::RemoveObserverInstance(const UScriptStruct& ElementType, const EMassObservedOperation Operation, UMassProcessor& ObserverProcessor)
{
	ensureAlways(Operation < EMassObservedOperation::MAX);
	check(Operation != EMassObservedOperation::MAX);

	EMassObservedOperationFlags OperationFlag = (Operation < EMassObservedOperation::MAX)
		? static_cast<EMassObservedOperationFlags>(1 << static_cast<uint8>(Operation))
		: (Operation == EMassObservedOperation::Add
			? EMassObservedOperationFlags::Add
			: EMassObservedOperationFlags::Remove);

	RemoveObserverInstance(&ElementType, OperationFlag, &ObserverProcessor);
}

// 【中文注释】GetOrMakeObserverLock：返回当前活跃 lock，或新建一个。
// 关键：把新建的 shared_ref 同时记到 ActiveObserverLock 弱引用中，
// 这样后续调用 IsLocked() 或 GetObserverLock() 能找到同一个 lock。
TSharedRef<FMassObserverManager::FObserverLock> FMassObserverManager::GetOrMakeObserverLock()
{
	if (ActiveObserverLock.IsValid())
	{
		return ActiveObserverLock.Pin().ToSharedRef();
	}
	else
	{
		FObserverLock* ObserverLock = new FObserverLock(*this);
		TSharedRef<FObserverLock> SharedContext = MakeShareable(ObserverLock);
		ActiveObserverLock = SharedContext;
		return SharedContext;
	}
}

// 【中文注释】GetOrMakeCreationContext：返回活跃 creation context 或新建。
// 细节：
//   1. 如果已有 context，直接复用（共享 shared_ptr）；
//   2. 否则新建：先 GetOrMakeObserverLock 拿到 lock，再 new 一个 FCreationContext 包装；
//   3. 通过 AddCreatedEntities({}) 创建一条空的 CreateEntity 缓冲项作为占位，
//      handle.OpIndex 即指向这条占位；后续真正的创建会追加到这条项里。
//   4. 调试构建额外记录 SerialNumber，防止跨轮使用。
TSharedRef<FMassObserverManager::FCreationContext> FMassObserverManager::GetOrMakeCreationContext()
{
	if (ActiveCreationContext.IsValid())
	{
		return ActiveCreationContext.Pin().ToSharedRef();
	}
	else
	{
		FCreationContext* ObserverLock = new FCreationContext(GetOrMakeObserverLock());
#if WITH_MASSENTITY_DEBUG
		ObserverLock->CreationHandle.SerialNumber = LockedNotificationSerialNumber;
#endif // WITH_MASSENTITY_DEBUG
		ObserverLock->CreationHandle.OpIndex = ObserverLock->Lock->AddCreatedEntities({});
		TSharedRef<FCreationContext> SharedContext = MakeShareable(ObserverLock);
		ActiveCreationContext = SharedContext;
		return SharedContext;
	}
}

// 【中文注释】带预留 entity 的版本：创建 context 时把已知的 reserved entity 列表 + 预构建的 collection 一并塞入 lock。
// 用于"先批量预留 ID，后填充数据"的批量创建路径，节省后续重复构造 collection 的开销。
TSharedRef<FMassObserverManager::FCreationContext> FMassObserverManager::GetOrMakeCreationContext(TConstArrayView<FMassEntityHandle> ReservedEntities
	, FMassArchetypeEntityCollection&& EntityCollection)
{
	if (TSharedPtr<FCreationContext> CreationContext = ActiveCreationContext.Pin())
	{
		// 【中文注释】已有 context：把新预留实体追加进去（与原 collection 合并）。
		CreationContext->GetObserverLock()->AddCreatedEntities(ReservedEntities, Forward<FMassArchetypeEntityCollection>(EntityCollection));
		return CreationContext.ToSharedRef();
	}
	else
	{
		FCreationContext* ObserverLock = new FCreationContext(GetOrMakeObserverLock());
#if WITH_MASSENTITY_DEBUG
		ObserverLock->CreationHandle.SerialNumber = LockedNotificationSerialNumber;
#endif // WITH_MASSENTITY_DEBUG
		ObserverLock->CreationHandle.OpIndex = ObserverLock->Lock->AddCreatedEntities(ReservedEntities, Forward<FMassArchetypeEntityCollection>(EntityCollection));
		TSharedRef<FCreationContext> SharedContext = MakeShareable(ObserverLock);
		ActiveCreationContext = SharedContext;
		return SharedContext;
	}
}

// 【中文注释】OnPostFork：processor fork 后通知活跃 lock 更新线程 ID。
// fork 场景下，原线程的 lock 被复制到 worker 线程；如果不更新 OwnerThreadId，
// worker 中的 UE_CHECK_OWNER_THREADID 会断言失败。
void FMassObserverManager::OnPostFork(EForkProcessRole)
{
	if (TSharedPtr<FObserverLock> ActiveContext = ActiveObserverLock.Pin())
	{
		ActiveContext->ForceUpdateCurrentThreadID();
	}
}

// 【中文注释】OnModulePackagesUnloaded：游戏模块热更新时清理失效 observer。
// 流程：
//   1. 对每个 op × {fragment, tag} 维度遍历 map；
//   2. 检查每个 type（UScriptStruct）的归属包是否在卸载列表中；
//   3. 在则连同整个 type→pipeline 条目一起移除。
// 注意：只清理 type 维度的失效，pipeline 中的 processor 实例由 GC 自然回收。
void FMassObserverManager::OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMassObserverManager::OnModulePackagesUnloaded);

	const auto ProcessObserversMap = [&Packages](FMassObserversMap& ObserversMap)
	{
		for (auto MapIt = (*ObserversMap).CreateIterator(); MapIt; ++MapIt)
		{
			TObjectPtr<const UScriptStruct> ObserverClass = MapIt->Key;
			const UPackage* ObserverPackage = ObserverClass->GetPackage();

			if (Packages.Contains(ObserverPackage))
			{
				UE_LOG(LogMass, Verbose, TEXT("%hs: removed observer %s (%s)"), __FUNCTION__, *ObserverClass->GetName(), *ObserverPackage->GetName());
				MapIt.RemoveCurrent();
			}
		}
	};

	for (int32 OperationIndex = 0; OperationIndex < static_cast<uint32>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		ProcessObserversMap(FragmentObservers[OperationIndex]);
		ProcessObserversMap(TagObservers[OperationIndex]);
	}
}

// =============================================================================
// 【ResumeExecution - lock 析构时的派发主循环】
//
// 由 ~FObserverLock 自动调用。负责把 lock 收集的所有 BufferedNotification 派发给 observer。
//
// 流程：
//   1. 断言 LocksCount == 0：必须所有 lock 都释放才能开始派发；
//   2. 调试：序列号校验 + 递增（防止 lock 跨轮被误用）；
//   3. 若缓冲为空，直接返回（hot path：很多 lock 可能根本没产生通知）；
//   4. 构造一个全新的 ProcessingContext + NotificationContext；
//   5. 主循环遍历 BufferedNotifications：
//      - CreateEntity 类型 → Visit(CreationOpExecutioner, AffectedEntities)
//          仅对 entity 容器做 visit（CompositionChange 是 FEmptyComposition 不参与）；
//      - 其他类型 → 设置 OpType，再 Visit(RegularOperationNotifier, CompositionChange, AffectedEntities)
//          双 variant visit：根据 (composition 类型, entity 容器类型) 组合派发。
//
// 注意：派发期间如果 observer 又调用了 EntityManager 的变更 API，
//   - 此时 LocksCount == 0，所以会走立即执行分支（递归触发 observer）；
//   - 但因为 ProcessingScope 已建立，新变更会被延迟到 scope 退出，避免递归冲突。
// =============================================================================
void FMassObserverManager::ResumeExecution(FObserverLock& LockBeingReleased)
{
	using namespace UE::Mass::ObserverManager;

	ensureMsgf(LocksCount == 0, TEXT("We only expect this function to be called if all locks are released."));
#if WITH_MASSENTITY_DEBUG
	ensureMsgf(LockBeingReleased.LockSerialNumber == LockedNotificationSerialNumber
		, TEXT("Lock's and ObserverManager's lock serial numbers are expected to match."));
	++LockedNotificationSerialNumber;
#endif // WITH_MASSENTITY_DEBUG

	if (LockBeingReleased.BufferedNotifications.IsEmpty() == false)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_ResumeExecution);

		UE::Mass::FProcessingContext ProcessingContext(EntityManager);

		FNotificationContext NotificationContext{*this, ProcessingContext};

		FBufferedNotificationExecutioner RegularOperationNotifier(NotificationContext);
		FBufferedCreationNotificationExecutioner CreationOpExecutioner(NotificationContext);

		for (FBufferedNotification& Op : LockBeingReleased.BufferedNotifications)
		{
			if (Op.Type == EMassObservedOperation::CreateEntity)
			{
				// 【中文注释】创建型通知：CompositionChange 必为 FEmptyComposition，不参与 visit；
				// 仅对 AffectedEntities 派发到 OnCollectionsCreatedImpl 或 OnCompositionChanged（按 entity 形态）。
				Visit(CreationOpExecutioner, Op.AffectedEntities);
			}
			else
			{
				// 【中文注释】非创建型通知：双 variant visit。
				// RegularOperationNotifier 内提供了 (composition, entities) 的所有重载。
				RegularOperationNotifier.OpType = Op.Type;
				Visit(RegularOperationNotifier, Op.CompositionChange, Op.AffectedEntities);
			}
		}
#if WITH_MASSENTITY_DEBUG
		++DebugNonTrivialResumeExecutionCount;
#endif // WITH_MASSENTITY_DEBUG
	}
}

// 【中文注释】ReleaseCreationHandle：由 ~FCreationContext 调用。
// 仅把 lock 内部的 CreationNotificationIndex 重置（让后续创建可以新开一条），
// 不会立刻触发 observer —— observer 触发时机是 lock 析构时的 ResumeExecution。
// 也会校验 ActiveCreationContext 已失效（共享指针计数归零），断言前后状态一致。
void FMassObserverManager::ReleaseCreationHandle(FCreationNotificationHandle InCreationNotificationHandle)
{
	ensureMsgf(InCreationNotificationHandle.IsSet(), TEXT("Invalid creation handle passed to %hs"), __FUNCTION__);
#if WITH_MASSENTITY_DEBUG
	ensureMsgf(InCreationNotificationHandle.SerialNumber == LockedNotificationSerialNumber
		, TEXT("Creation handle's serial number doesn't match the ObserverManager's data"));
#endif // WITH_MASSENTITY_DEBUG

	TSharedPtr<FObserverLock> LockInstance = ActiveObserverLock.Pin();
	if (ensure(LockInstance))
	{
		ensure(LockInstance->ReleaseCreationNotification(InCreationNotificationHandle));
		ensure(ActiveCreationContext.IsValid() == false);
	}
}

//----------------------------------------------------------------------//
// DEPRECATED
//----------------------------------------------------------------------//
// 【中文注释】---- 以下为兼容旧 API 的桥接实现，全部转发到新 API ----
// 保留是为了二进制 / 源码兼容；新代码不应再调用这些。
bool FMassObserverManager::OnPostEntitiesCreated(UE::Mass::FProcessingContext&, const FMassArchetypeEntityCollection& EntityCollection)
{
	return OnPostEntitiesCreated(EntityCollection);
}

bool FMassObserverManager::OnPostEntitiesCreated(UE::Mass::FProcessingContext&, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPostEntitiesCreated")

	bool bReturnValue = false;

	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		const FMassArchetypeCompositionDescriptor& ArchetypeComposition = EntityManager.GetArchetypeComposition(Collection.GetArchetype());
		bReturnValue |= OnCompositionChanged(Collection, ArchetypeComposition, EMassObservedOperation::CreateEntity);
	}

	return bReturnValue;
}

bool FMassObserverManager::OnCompositionChanged(UE::Mass::FProcessingContext&, const FMassArchetypeEntityCollection& EntityCollection
	, const FMassArchetypeCompositionDescriptor& CompositionDelta, const EMassObservedOperation InOperation)
{
	return OnCompositionChanged(EntityCollection, CompositionDelta, InOperation);
}

void FMassObserverManager::HandleSingleEntityImpl(const UScriptStruct& FragmentType, const FMassArchetypeEntityCollection& EntityCollection, FMassObserversMap& HandlersContainer)
{
	UE::Mass::ObserverManager::FDeprecationHelper::HandleSingleElement(this, FragmentType, EntityCollection, HandlersContainer);
}

void FMassObserverManager::OnPostFragmentOrTagAdded(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection)
{
	UE::Mass::ObserverManager::FDeprecationHelper::HandleSingleElement(this, FragmentOrTagType, EntityCollection, EMassObservedOperation::AddElement);
}

void FMassObserverManager::OnPreFragmentOrTagRemoved(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection)
{
	UE::Mass::ObserverManager::FDeprecationHelper::HandleSingleElement(this, FragmentOrTagType, EntityCollection, EMassObservedOperation::RemoveElement);
}

void FMassObserverManager::OnFragmentOrTagOperation(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection, const EMassObservedOperation Operation)
{
	UE::Mass::ObserverManager::FDeprecationHelper::HandleSingleElement(this, FragmentOrTagType, EntityCollection, Operation);
}

// 【中文注释】下面这组 FCreationContext 的旧 API 一律返回空 / 假值，仅为保持二进制兼容。
TConstArrayView<FMassArchetypeEntityCollection> FMassObserverManager::FCreationContext::GetEntityCollections() const
{
	return {};
}

int32 FMassObserverManager::FCreationContext::GetSpawnedNum() const
{
	return 0;
}

bool FMassObserverManager::FCreationContext::IsDirty() const
{
	return true;
}

void FMassObserverManager::FCreationContext::AppendEntities(const TConstArrayView<FMassEntityHandle>)
{
}

void FMassObserverManager::FCreationContext::AppendEntities(const TConstArrayView<FMassEntityHandle>, FMassArchetypeEntityCollection&&)
{
}

FMassObserverManager::FCreationContext::FCreationContext(const int32)
	: FCreationContext()
{}

const FMassArchetypeEntityCollection& FMassObserverManager::FCreationContext::GetEntityCollection() const
{
	static FMassArchetypeEntityCollection DummyInstance;
	return DummyInstance;
}

// 【中文注释】旧 HandleElementsImpl：通过 DetermineOperationType 反推 op 后转发到新版本。
void FMassObserverManager::HandleElementsImpl(UE::Mass::FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, TArrayView<const UScriptStruct*> ObservedTypes, FMassObserversMap& HandlersContainer)
{
	if (ObservedTypes.Num() == 0 || ObservedTypes[0] == nullptr)
	{
		return;
	}

	// Legacy support: we need to figure out which operation this is
	// and to do that we need to know whether we're handling fragments or tags
	const bool bIsFragment = UE::Mass::IsA<FMassFragment>(ObservedTypes[0]);
	check(bIsFragment || UE::Mass::IsA<FMassTag>(ObservedTypes[0]));

	using UE::Mass::ObserverManager::FDeprecationHelper;
	const EMassObservedOperation Operation = bIsFragment
		? FDeprecationHelper::DetermineOperationType(&HandlersContainer, ProcessingContext.GetEntityManager()->GetObserverManager().FragmentObservers)
		: FDeprecationHelper::DetermineOperationType(&HandlersContainer, ProcessingContext.GetEntityManager()->GetObserverManager().TagObservers);

	checkf(Operation < EMassObservedOperation::MAX, TEXT("Unable to determine the legacy operation type"));

	HandleElementsImpl(ProcessingContext, EntityCollections, {Operation, ObservedTypes}, HandlersContainer);
}

void FMassObserverManager::HandleFragmentsImpl(UE::Mass::FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection
		, TArrayView<const UScriptStruct*> ObservedTypes, FMassObserversMap& HandlersContainer)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	HandleElementsImpl(ProcessingContext, {EntityCollection}, ObservedTypes, HandlersContainer);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FMassObserverManager::OnCollectionsCreatedImpl(UE::Mass::FProcessingContext& ProcessingContext, TArray<FMassArchetypeEntityCollection>&& EntityCollections)
{
	return OnCollectionsCreatedImpl(ProcessingContext, MakeArrayView(EntityCollections));
}
