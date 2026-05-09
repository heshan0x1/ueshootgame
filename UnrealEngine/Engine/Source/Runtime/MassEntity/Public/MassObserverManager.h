// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverManager.h】
//
// 本文件定义 Mass 反应式系统的核心：FMassObserverManager。
//
// ## 角色
// FMassObserverManager 是一个 USTRUCT（非 UObject），由 FMassEntityManager **内嵌持有**
// （而不是作为子对象指针）。它是"运行时层"：
//   - 配置层：UMassObserverRegistry（CDO 单例）—— "哪个 observer class 关心哪个 fragment 类型"
//   - 运行时层：FMassObserverManager（每个 EntityManager 一个）—— 维护实际的 processor 实例
//
// Initialize 阶段读取 Registry 的配置，把每个 observer class 实例化成 UMassProcessor，
// 按 (操作类型, fragment/tag 类型) 维度组织成查找表（FMassObserversMap）。
//
// ## 数据流（变更事件 → observer 触发）
// 1. EntityManager 发起组合变更（如 AddFragment）；
// 2. 调用 ObserverManager.OnCompositionChanged(entity, delta, op)；
// 3. 用镜像位集 ObservedFragments[op]/ObservedTags[op] 与 delta 求交，O(1) 判断"是否需触发"；
// 4. 若有重叠，从 FragmentObservers[op] / TagObservers[op] 找到对应 type → pipeline；
// 5. 通过 Executor::RunProcessorsView 执行 pipeline 中的所有 observer processor。
//
// ## 关键 API 速查
//   - OnPostEntitiesCreated / OnPostEntityCreated：实体创建后触发 CreateEntity observer
//   - OnPreEntitiesDestroyed / OnPreEntityDestroyed：实体销毁前触发 DestroyEntity observer
//   - OnPostCompositionAdded：fragment/tag 加上后触发 AddElement observer（数据已就位）
//   - OnPreCompositionRemoved：fragment/tag 移除前触发 RemoveElement observer（数据将被销毁，observer 抢救）
//
// 注意 "Pre/Post" 的语义对应：
//   - 销毁/移除：Pre = 操作之前，observer 还能看到旧数据
//   - 创建/添加：Post = 操作之后，observer 能看到新数据
//
// ## 锁与作用域
//   - GetOrMakeObserverLock：返回当前的 lock，若无则新建。多次调用会复用同一个 lock（共享指针）；
//   - GetOrMakeCreationContext：返回当前的 creation context，若无则新建（包装 lock）；
//   - LocksCount：lock 的引用计数，>0 时所有变更都会被缓冲；
//   - ActiveObserverLock / ActiveCreationContext：弱引用，外部持有强引用决定生命周期。
// =============================================================================

#pragma once

#include "MassEntityHandle.h"
#include "MassProcessingTypes.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "Misc/Fork.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
// the following is a new header, but it contains some types moved over from other places, and including
// said header ties everything together. Engine code has been updated to not require it
// 【中文注释】兼容老代码：5.6 之前 FObserverLock 等类型直接定义在本头文件，现在拆到 MassObserverNotificationTypes.h，
// 通过此条件 include 保持向后兼容（老代码只 include 本头文件即可获得全部类型）。
#include "MassObserverNotificationTypes.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassObserverManager.generated.h"


struct FMassObserverExecutionContext;
struct FMassEntityManager;
struct FMassArchetypeEntityCollection;
class UMassProcessor;
class UMassObserverProcessor;
namespace UE::Mass
{
	struct FProcessingContext;
	namespace ObserverManager
	{
		struct FDeprecationHelper;
		struct FBufferedNotificationExecutioner;
		struct FBufferedCreationNotificationExecutioner;

		struct FBufferedNotification;
		struct FCreationNotificationHandle;
		struct FObserverLock;
		struct FCreationContext;
	}
}

/** 
 * A wrapper type for a TMap to support having array-of-maps UPROPERTY members in FMassObserverManager
 */
// 【中文注释】FMassObserversMap：TMap<UScriptStruct*, FMassRuntimePipeline> 的 USTRUCT 包装。
// 为什么需要包装？UPROPERTY 不支持"数组的元素是模板 TMap"这种嵌套（无法直接声明 TMap[MAX_Op]），
// 通过把 TMap 包成一个 USTRUCT，外部才能写 UPROPERTY FMassObserversMap[MAX_Op]。
//
// 语义：每个 element type（fragment/tag 的 UScriptStruct）映射到一个 pipeline（一组按优先级排好的 observer processor）。
USTRUCT()
struct FMassObserversMap
{
	GENERATED_BODY()

	// a helper accessor simplifying access while still keeping Container private
	// 【中文注释】operator* 返回内部 TMap 的引用。设计为 operator* 而非 GetContainer() 方法，
	// 让调用点写起来像"解引用容器"那样自然，例如 (*ObserversMap[i]).FindOrAdd(StructType)。
	TMap<TObjectPtr<const UScriptStruct>, FMassRuntimePipeline>& operator*()
	{
		return Container;
	}

	// 【中文注释】调试用：把所有 pipeline 中的 processor 收集到 OutProcessors（去重）。
	void DebugAddUniqueProcessors(TArray<const UMassProcessor*>& OutProcessors) const;

private:
	// 【中文注释】真正的存储：UPROPERTY 保证 GC 不会回收 processor 实例。
	UPROPERTY()
	TMap<TObjectPtr<const UScriptStruct>, FMassRuntimePipeline> Container;
};

/** 
 * A type that encapsulates logic related to notifying interested parties of entity composition changes. Upon creation it
 * reads information from UMassObserverRegistry and instantiates processors interested in handling given fragment
 * type addition or removal.
 */
// =============================================================================
// 【FMassObserverManager - 观察者管理器】
//
// 见文件总览。这是一个 USTRUCT（非 UObject），由 FMassEntityManager 直接内嵌：
//   FMassEntityManager
//     └── FMassObserverManager ObserverManager;   ← 直接成员
//
// 因此其生命周期与 EntityManager 绑定，不需要单独 GC 管理。
// 但其内部持有的 ObserverProcessor 实例是 UObject，由 FMassObserversMap 中的 UPROPERTY 引用。
//
// ## 核心数据
//   - ObservedFragments[Op] / ObservedTags[Op]：镜像位集，O(1) 检查变更是否需要触发 observer
//   - FragmentObservers[Op] / TagObservers[Op]：实际的 type → pipeline 映射表
//   - ActiveObserverLock / ActiveCreationContext：当前活跃的 lock / context（弱引用）
//   - LocksCount：lock 引用计数，>0 时变更被缓冲
// =============================================================================
USTRUCT()
struct FMassObserverManager
{
	GENERATED_BODY()

	/** convenience aliases */
	// 【中文注释】别名简化 —— 把 UE::Mass::ObserverManager 命名空间下的类型暴露为本结构体的成员类型。
	// 调用方写 FMassObserverManager::FObserverLock 即可，无需写完整命名空间。
	using FObserverLock = UE::Mass::ObserverManager::FObserverLock;
	using FBufferedNotification = UE::Mass::ObserverManager::FBufferedNotification;
	using FCreationNotificationHandle = UE::Mass::ObserverManager::FCreationNotificationHandle;
	using FCreationContext = UE::Mass::ObserverManager::FCreationContext;

	// 【中文注释】默认构造：通过 GetMutableDefault<UMassEntitySubsystem>() 获取全局 EntityManager。
	// 此构造仅用于 USTRUCT 的反射默认实例化（Mass 不期望用户直接 new 这个结构）。
	MASSENTITY_API FMassObserverManager();

	FMassEntityManager& GetEntityManager();

	// 【中文注释】返回所有"被观察的 fragment 位集"数组（按 EMassObservedOperation 索引）。
	// 用途：EntityManager 在做组合变更前可以批量比对。
	const FMassFragmentBitSet* GetObservedFragmentBitSets() const { return ObservedFragments; }
	// 【中文注释】返回指定操作类型下的"被观察 fragment 位集"。
	const FMassFragmentBitSet& GetObservedFragmentsBitSet(const EMassObservedOperation Operation) const 
	{ 
		return ObservedFragments[(uint8)Operation]; 
	}

	// 【中文注释】tag 版本，语义同上。
	const FMassTagBitSet* GetObservedTagBitSets() const { return ObservedTags; }
	const FMassTagBitSet& GetObservedTagsBitSet(const EMassObservedOperation Operation) const 
	{ 
		return ObservedTags[(uint8)Operation]; 
	}
	
	// 【中文注释】快速检查：传入的 fragment 位集是否与"被观察"位集有交集。
	// 这是反应式系统的关键 fast path：调用方在变更前可以 O(1) 判定是否要继续走完整的 observer 流程。
	bool HasObserversForBitSet(const FMassFragmentBitSet& InQueriedBitSet, const EMassObservedOperation Operation) const
	{
		return ObservedFragments[(uint8)Operation].HasAny(InQueriedBitSet);
	}

	// 【中文注释】tag 版本。
	bool HasObserversForBitSet(const FMassTagBitSet& InQueriedBitSet, const EMassObservedOperation Operation) const
	{
		return ObservedTags[(uint8)Operation].HasAny(InQueriedBitSet);
	}

	// 【中文注释】组合（fragment + tag）版本：任一种类型有重叠即返回 true。
	bool HasObserversForComposition(const FMassArchetypeCompositionDescriptor& Composition, const EMassObservedOperation Operation) const
	{
		return HasObserversForBitSet(Composition.GetFragments(), Operation) || HasObserversForBitSet(Composition.GetTags(), Operation);
	}

	/** @return whether there are observers watching affected elements */
	// 【中文注释】实体创建后调用：触发 CreateEntity 类型的 observer。
	// 如果当前有 lock，会缓冲到 lock；否则立即执行。
	// 返回 true 表示有 observer 被触发（或被缓冲），调用方据此决定是否做后续处理。
	MASSENTITY_API bool OnPostEntitiesCreated(const FMassArchetypeEntityCollection& EntityCollection);

	/** @return whether there are observers watching affected elements */
	// 【中文注释】单实体创建版本。Composition 可为空（此时会从 archetype 反查）。
	MASSENTITY_API bool OnPostEntityCreated(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition);

	/** @return whether there are observers watching affected elements */
	// 【中文注释】实体销毁前调用：触发 DestroyEntity 类型的 observer。
	// "Pre" 语义：实体还活着，observer 可以读取所有 fragment 数据做收尾。
	MASSENTITY_API bool OnPreEntitiesDestroyed(const FMassArchetypeEntityCollection& EntityCollection);

	/** @return whether there are observers watching affected elements */
	// 【中文注释】带 ProcessingContext 参数的版本：用于复用上层 processing 上下文，避免内部重新构造。
	MASSENTITY_API bool OnPreEntitiesDestroyed(UE::Mass::FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection);

	/** @return whether there are observers watching affected elements */
	// 【中文注释】单实体销毁前版本。
	MASSENTITY_API bool OnPreEntityDestroyed(const FMassArchetypeCompositionDescriptor& ArchetypeComposition, const FMassEntityHandle Entity);

	/** @return whether there are observers watching affected elements */
	// 【中文注释】组合添加后调用：触发 AddElement 类型的 observer。
	// "Post" 语义：fragment 已经加到 entity 上了，observer 可以直接读取新数据。
	// 这是最常用的反应式入口（"我加了 X 组件，自动启动相关逻辑"）。
	bool OnPostCompositionAdded(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
	{
		return OnCompositionChanged(Entity, Composition, EMassObservedOperation::AddElement);
	}
	/** @return whether there are observers watching affected elements */
	// 【中文注释】组合移除前调用：触发 RemoveElement 类型的 observer。
	// "Pre" 语义：fragment 还在 entity 上，observer 可以做最后的"抢救"读取。
	// 注意：lock 期间被 buffer 后，PreRemove 会变成"PostRemove"，看不到旧数据（见 FObserverLock 文档）。
	bool OnPreCompositionRemoved(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
	{
		return OnCompositionChanged(Entity, Composition, EMassObservedOperation::RemoveElement);
	}

protected:
	/**
	 * helper struct for holding either a single enity handle or an archetype collection reference.
	 * The type is highly transient, do not store instances of it. Its only function is to 
	 * allow us to have a single OnCompositionChange implementation rather than having two separate
	 * implementations, one for entity handle, the other for archetype collection.
	 */
	// 【中文注释】FCollectionRefOrHandle - "二选一" 持有 handle 或 collection 引用。
	// 用途：让 OnCompositionChanged 只需写一份实现，同时支持单 handle 和 collection 两种入口。
	// 是高度临时性的辅助类型 —— 通过 EntityHandle.IsValid() 判定走哪条分支。
	// UE_NONCOPYABLE 防止误用（含引用成员，拷贝语义有歧义）。
	struct FCollectionRefOrHandle
	{
		UE_NONCOPYABLE(FCollectionRefOrHandle);

		// 【中文注释】静态 dummy collection：当只用 handle 路径时，EntityCollection 引用必须指向某个有效对象，
		// 这里用一个全局 dummy 占位（避免悬空引用）。访问前会先看 EntityHandle.IsSet()。
		static FMassArchetypeEntityCollection DummyCollection;
		// 【中文注释】handle 形态：EntityHandle 设值，EntityCollection 指向 dummy。
		explicit FCollectionRefOrHandle(FMassEntityHandle InEntityHandle)
			: EntityHandle(InEntityHandle)
			, EntityCollection(DummyCollection)
		{	
		}

		// 【中文注释】collection 形态：EntityHandle 默认为无效（IsSet 返回 false），EntityCollection 引用真实 collection。
		explicit FCollectionRefOrHandle(const FMassArchetypeEntityCollection& InEntityCollection)
			: EntityCollection(InEntityCollection)
		{	
		}

		FMassEntityHandle EntityHandle;
		const FMassArchetypeEntityCollection& EntityCollection;
	};
	// 【中文注释】所有 OnCompositionChanged 重载的统一实现入口。
	// 参数：
	//   - EntityCollection：受影响的实体（单 handle 或 collection）
	//   - Composition：变更内容（fragment + tag 的 delta）
	//   - Operation：操作类型（Add/Remove/Create/Destroy）
	//   - ProcessingContext：可选，复用上层 processing 上下文
	// 返回值：true 表示有 observer 被触发或缓冲。
	// 实现细节见 .cpp。
	MASSENTITY_API bool OnCompositionChanged(FCollectionRefOrHandle&& EntityCollection, const FMassArchetypeCompositionDescriptor& Composition
		, const EMassObservedOperation Operation, UE::Mass::FProcessingContext* ProcessingContext = nullptr);

public:
	// 【中文注释】OnCompositionChanged 的对外入口（collection 版本）。包了一层 trace event。
	bool OnCompositionChanged(const FMassArchetypeEntityCollection& EntityCollection, const FMassArchetypeCompositionDescriptor& Composition
		, const EMassObservedOperation Operation, UE::Mass::FProcessingContext* ProcessingContext = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_OnCompositionChanged_Collection);
		return OnCompositionChanged(FCollectionRefOrHandle(EntityCollection), Composition, Operation, ProcessingContext);
	}

	// 【中文注释】OnCompositionChanged 的对外入口（单 handle 版本）。
	bool OnCompositionChanged(FMassEntityHandle EntityHandle, const FMassArchetypeCompositionDescriptor& Composition
		, const EMassObservedOperation Operation, UE::Mass::FProcessingContext* ProcessingContext = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassObserver_OnCompositionChanged_Entity);
		return OnCompositionChanged(FCollectionRefOrHandle(EntityHandle), Composition, Operation, ProcessingContext);
	}

	// 【中文注释】便捷重载：直接传 fragment / tag 位集（不必显式构造 CompositionDescriptor）。
	// requires 子句限制 T 仅能是 FMassFragmentBitSet 或 FMassTagBitSet 之一。
	template<typename T, typename U = std::decay_t<T>> requires (std::is_same_v<U, FMassFragmentBitSet> || std::is_same_v<U, FMassTagBitSet>)
	bool OnCompositionChanged(const FMassArchetypeEntityCollection& EntityCollection, T&& BitSet
		, const EMassObservedOperation Operation, UE::Mass::FProcessingContext* ProcessingContext = nullptr)
	{
		return OnCompositionChanged(EntityCollection, FMassArchetypeCompositionDescriptor(MoveTemp(BitSet)), Operation, ProcessingContext);
	}

	// 【中文注释】单 handle + 位集版本。
	template<typename T, typename U = std::decay_t<T>> requires (std::is_same_v<U, FMassFragmentBitSet> || std::is_same_v<U, FMassTagBitSet>)
	bool OnCompositionChanged(const FMassEntityHandle Entity, T&& BitSet
		, const EMassObservedOperation Operation, UE::Mass::FProcessingContext* ProcessingContext = nullptr)
	{
		return OnCompositionChanged(Entity, FMassArchetypeCompositionDescriptor(MoveTemp(BitSet)), Operation, ProcessingContext);
	}

	// 【中文注释】注册一个 observer 实例：把 ObserverProcessor 加入到指定 ElementType 的指定 OperationFlags 的 pipeline。
	// OperationFlags 是位组合（可一次注册多个操作类型，例如 Add | Remove）。
	// 同时更新 ObservedFragments/ObservedTags 镜像位集，让 fast path 能识别新加入的类型。
	MASSENTITY_API void AddObserverInstance(TNotNull<const UScriptStruct*> ElementType, EMassObservedOperationFlags OperationFlags, TNotNull<UMassProcessor*> ObserverProcessor);
	// 【中文注释】单一操作类型的便捷重载（参数是 EMassObservedOperation 枚举而不是 flags）。
	MASSENTITY_API void AddObserverInstance(const UScriptStruct& ElementType, EMassObservedOperation Operation, UMassProcessor& ObserverProcessor);
	// 【中文注释】根据 UMassObserverProcessor 自身的 ObservedType + ObservedOperations 自动注册（最常用入口）。
	MASSENTITY_API void AddObserverInstance(TNotNull<UMassObserverProcessor*> ObserverProcessor);
	// 【中文注释】注销 observer 实例。如果某个 (type, op) 上的 pipeline 变空，会从 map 中移除并清理镜像位集。
	MASSENTITY_API void RemoveObserverInstance(TNotNull<const UScriptStruct*> ElementType, EMassObservedOperationFlags OperationFlags, TNotNull<UMassProcessor*> ObserverProcessor);
	MASSENTITY_API void RemoveObserverInstance(const UScriptStruct& ElementType, EMassObservedOperation Operation, UMassProcessor& ObserverProcessor);

	// 【中文注释】释放 CreationContext 持有的句柄。由 ~FCreationContext 调用。
	MASSENTITY_API void ReleaseCreationHandle(UE::Mass::ObserverManager::FCreationNotificationHandle InCreationNotificationHandle);

	// 【中文注释】调试用：收集所有去重后的 observer processor 实例。
	MASSENTITY_API void DebugGatherUniqueProcessors(TArray<const UMassProcessor*>& OutProcessors) const;

protected:
	friend FMassEntityManager;
	// 【中文注释】真正的构造函数：由 FMassEntityManager 通过 friend 关系调用，传入 owning manager 的引用。
	MASSENTITY_API explicit FMassObserverManager(FMassEntityManager& Owner);

	// 【中文注释】Initialize：从 UMassObserverRegistry 读取所有注册过的 observer class，
	// 实例化为 UMassProcessor 并组装成 pipeline，填充 FragmentObservers / TagObservers / Observed* 位集。
	// 由 EntityManager 在初始化阶段调用。
	MASSENTITY_API void Initialize();
	// 【中文注释】DeInitialize：清空所有 pipeline，注销 module 卸载回调。EntityManager 销毁时调用。
	MASSENTITY_API void DeInitialize();

	friend UE::Mass::ObserverManager::FBufferedNotificationExecutioner;
	friend UE::Mass::ObserverManager::FBufferedCreationNotificationExecutioner;

	// 【中文注释】HandleElementsImpl：observer 触发的"最底层"统一实现。
	// 流程：
	//   1. 获取 EntityManager 的 NewProcessingScope（建立 processing 范围）；
	//   2. 把 ObserverContext 写入 ProcessingContext.AuxData，让 observer 通过 GetAuxData 访问 Operation/Types 信息；
	//   3. 遍历 ObserverContext.TypesInOperation：每种 type 对应一个 pipeline，依次执行；
	//   4. FObserverContextIterator 在迭代过程中递增 CurrentTypeIndex，让 GetCurrentType() 始终返回当前 type。
	// 关键：一次组合变更可能涉及多种 fragment/tag，此循环依次执行每个 type 的 observer。
	static MASSENTITY_API void HandleElementsImpl(UE::Mass::FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, FMassObserverExecutionContext&& ObserverContext, FMassObserversMap& HandlersContainer);

	/** Coalesces all the elements observed in all the collections and executes all the observers at once */
	// 【中文注释】OnCollectionsCreatedImpl：处理多个 archetype collection 的 CreateEntity 事件。
	// 把所有 collection 的 archetype composition 合并成一份 fragment/tag 总集，
	// 与 ObservedFragments[Create] 求交后，一次性触发所有 fragment observer + tag observer。
	// 这是 lock 释放时 FBufferedCreationNotificationExecutioner 的实现路径。
	MASSENTITY_API bool OnCollectionsCreatedImpl(UE::Mass::FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections);

	// 【中文注释】fork 后回调：更新当前活跃 lock 的 OwnerThreadId。
	MASSENTITY_API void OnPostFork(EForkProcessRole);

	// 【中文注释】模块卸载回调：当 game module hot-reload 时，移除归属于卸载包的 observer pipeline。
	// 防止卸载后留下悬空的 UScriptStruct 指针。
	MASSENTITY_API void OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages);

	//TSharedRef<FObserverLock> GetOrMakeObserverLock(TConstArrayView<FMassEntityHandle> ReservedEntities, FMassArchetypeEntityCollection&& EntityCollection);
	// 【中文注释】GetOrMakeObserverLock：返回当前活跃 lock，或新建一个。
	// 多次调用会复用同一个 lock（共享指针），实现"嵌套 lock"等效于"单一 lock"的语义。
	MASSENTITY_API TSharedRef<FObserverLock> GetOrMakeObserverLock();
	// 【中文注释】只读获取当前 lock；若无活跃 lock 返回空 ptr。
	TSharedPtr<FObserverLock> GetObserverLock() const;
	// 【中文注释】是否处于 lock 状态。注意这里只判断 weak ptr 有效性，并未检查 LocksCount，
	// 因为 lock 的"活跃"完全由其 shared_ptr 生命周期决定。
	bool IsLocked() const { return ActiveObserverLock.IsValid(); }
	// 【中文注释】GetOrMakeCreationContext：返回当前 creation context，或新建一个（同时确保 lock 存在）。
	MASSENTITY_API TSharedRef<FCreationContext> GetOrMakeCreationContext();
	// 【中文注释】带预留实体的版本：把 ReservedEntities 直接放入 lock 的 BufferedNotifications。
	// 用于"先保留 ID 再延迟构造"的批量创建路径。
	MASSENTITY_API TSharedRef<FCreationContext> GetOrMakeCreationContext(TConstArrayView<FMassEntityHandle> ReservedEntities, FMassArchetypeEntityCollection&& EntityCollection);
	TSharedPtr<FCreationContext> GetCreationContext() const;
	
	//-----------------------------------------------------------------------------
	// Observers locking logic
	//-----------------------------------------------------------------------------
	friend FObserverLock;

	/**
	 * Resumes observer triggering. All notifications collected in lock's BufferedNotifications will be processed at this point.
	 *
	 * Note that due to all the notifications being sent our are being sent post-factum the "OnPreRemove" 
	 * observers won't be able to access the data being removed, since the remove operation has already been performed.
	 * All the instances of removal-observers being triggered will be logged.
	 * 
	 * Intended to be called automatically by ~FObserverLock
	 */
	// 【中文注释】ResumeExecution：lock 析构时自动调用，遍历 BufferedNotifications 执行所有缓冲通知。
	// CreateEntity 类型走 FBufferedCreationNotificationExecutioner（合并到 OnCollectionsCreatedImpl）；
	// 其他类型走 FBufferedNotificationExecutioner（按 fragment/tag 分别派发）。
	// "Pre-Remove" 语义损失：buffer 期间数据已被删除，observer 无法读取旧数据。
	MASSENTITY_API void ResumeExecution(FObserverLock& LockBeingReleased);

	/**
	 * Never access directly, use GetOrMakeObserverLock or GetOrMakeCreationContext instead.
	 * Note: current lock is single-threaded. There's a path towards making it multithreaded, we'll work on it once we have a use-case
	 */
	// 【中文注释】当前活跃 lock 的弱引用。强引用由调用方持有（局部变量或共享指针）。
	// 弱引用 + LocksCount 引用计数共同决定 lock 何时析构。
	TWeakPtr<FObserverLock> ActiveObserverLock;
	// 【中文注释】lock 引用计数：每个 FObserverLock 构造 +1，析构 -1。
	// 与 weak ptr 不同，这是显式计数：用来在嵌套或多 context 共享时跟踪是否仍有 lock 活跃。
	int32 LocksCount = 0;
	// 【中文注释】当前活跃 creation context 的弱引用。
	TWeakPtr<FCreationContext> ActiveCreationContext;

	// 【中文注释】镜像位集：记录每种 op 下所有"被观察"的 fragment/tag 类型。
	// 用法：在做组合变更前，与 delta 求交集 —— 若交集为空，直接跳过整个 observer 流程（fast path）。
	// 这是反应式 ECS 的关键性能优化：99% 的变更可能不涉及任何 observer，O(1) bitset 比对极快。
	FMassFragmentBitSet ObservedFragments[(uint8)EMassObservedOperation::MAX];
	FMassTagBitSet ObservedTags[(uint8)EMassObservedOperation::MAX];

	// 【中文注释】fragment 维度的 observer 表：每种 op 一个 map，map 中 key 是 fragment 类型，value 是 pipeline。
	// 数组下标对应 EMassObservedOperation：
	//   [Add]/[AddElement]：fragment 加上后触发的 observer
	//   [Remove]/[RemoveElement]：fragment 移除前触发的 observer
	//   [CreateEntity]：实体创建时（按 fragment 分别）触发的 observer
	//   [DestroyEntity]：实体销毁时触发的 observer
	UPROPERTY()
	FMassObserversMap FragmentObservers[(uint8)EMassObservedOperation::MAX];

	// 【中文注释】tag 维度的 observer 表，结构同 FragmentObservers。
	UPROPERTY()
	FMassObserversMap TagObservers[(uint8)EMassObservedOperation::MAX];

	// 【中文注释】模块卸载回调的 delegate handle，用于 DeInitialize 时反注册。
	FDelegateHandle ModulesUnloadedHandle;

	/** 
	 * The owning EntityManager. No need for it to be a UPROPERTY since by design we don't support creation of 
	 * FMassObserverManager outside a FMassEntityManager instance 
	 */
	// 【中文注释】指向 owning EntityManager 的引用。
	// 不需要 UPROPERTY，因为 EntityManager 不是 UObject，且 ObserverManager 本身就是 EntityManager 的成员，
	// 不可能"独立活着"或被 GC 回收。
	FMassEntityManager& EntityManager;

#if WITH_MASSENTITY_DEBUG
	// 【中文注释】调试用：每次 ResumeExecution 后递增。lock 创建时记录此值，
	// 防止 lock 跨"轮次"被误用（如序列号不匹配会触发 ensure）。
	uint32 LockedNotificationSerialNumber = 1;
	// 【中文注释】调试统计：实际执行了非空缓冲通知的 ResumeExecution 次数。
	uint32 DebugNonTrivialResumeExecutionCount = 0;
#endif // WITH_MASSENTITY_DEBUG

public:
	// 【中文注释】---- 已废弃 API（5.5 / 5.6 / 5.7）----
	// 这些函数保留是为了二进制兼容；底层都转发到新的 OnCompositionChanged / OnPostEntitiesCreated。
	UE_DEPRECATED(5.5, "This flavor of OnPostEntitiesCreated is deprecated. Please use the one taking a TConstArrayView<FMassArchetypeEntityCollection> parameter instead")
	MASSENTITY_API bool OnPostEntitiesCreated(UE::Mass::FProcessingContext& InProcessingContext, const FMassArchetypeEntityCollection& EntityCollection);
	UE_DEPRECATED(5.6, "*FragmentOrTag* functions have been deprecated. Use OnCompositionChanged instead.")
	MASSENTITY_API void OnPostFragmentOrTagAdded(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection);
	UE_DEPRECATED(5.6, "*FragmentOrTag* functions have been deprecated. Use OnCompositionChanged instead.")
	MASSENTITY_API void OnPreFragmentOrTagRemoved(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection);
	UE_DEPRECATED(5.6, "*FragmentOrTag* functions have been deprecated. Use OnCompositionChanged instead.")
	MASSENTITY_API void OnFragmentOrTagOperation(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection, const EMassObservedOperation Operation);
	UE_DEPRECATED(5.6, "Use the other OnPostEntitiesCreated implementation.")
	MASSENTITY_API bool OnPostEntitiesCreated(UE::Mass::FProcessingContext& InProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections);
	UE_DEPRECATED(5.6, "Use the other OnCompositionChanged implementation.")
	MASSENTITY_API bool OnCompositionChanged(UE::Mass::FProcessingContext& InProcessingContext, const FMassArchetypeEntityCollection& EntityCollection
		, const FMassArchetypeCompositionDescriptor& Composition, const EMassObservedOperation Operation);
protected:
	UE_DEPRECATED(5.6, "Use HandleElementsImpl instead.")
	MASSENTITY_API void HandleSingleEntityImpl(const UScriptStruct& FragmentType, const FMassArchetypeEntityCollection& EntityCollection, FMassObserversMap& HandlersContainer);
	UE_DEPRECATED(5.7, "Use the implementation with the FMassObserverExecutionContext parameter")
	static void HandleElementsImpl(UE::Mass::FProcessingContext& ProcessingContext, TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
		, TArrayView<const UScriptStruct*> ObservedTypes, FMassObserversMap& HandlersContainer);
	UE_DEPRECATED(5.7, "Use the HandleElementsImpl implementation with the FMassObserverExecutionContext parameter")
	static void HandleFragmentsImpl(UE::Mass::FProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection
		, TArrayView<const UScriptStruct*> ObservedTypes, FMassObserversMap& HandlersContainer);

	UE_DEPRECATED(5.7, "Rvalue ref implementation is no longer available, it didn't do anything special. Please use the other OnCollectionsCreatedImpl implementation instead.")
	MASSENTITY_API bool OnCollectionsCreatedImpl(UE::Mass::FProcessingContext& ProcessingContext, TArray<FMassArchetypeEntityCollection>&& EntityCollections);

	friend UE::Mass::ObserverManager::FDeprecationHelper;
};

// 【中文注释】禁止拷贝：FMassObserverManager 含引用成员（EntityManager），且与 EntityManager 一一绑定，拷贝无意义。
template<>
struct TStructOpsTypeTraits<FMassObserverManager> : public TStructOpsTypeTraitsBase2<FMassObserverManager>
{
	enum
	{
		WithCopy = false,
	};
};

//----------------------------------------------------------------------//
// inlines
//----------------------------------------------------------------------//
inline FMassEntityManager& FMassObserverManager::GetEntityManager()
{
	return EntityManager;
}

// 【中文注释】Pin 弱引用获取活跃 lock。如果 lock 已析构，返回空 ptr。
inline TSharedPtr<FMassObserverManager::FObserverLock> FMassObserverManager::GetObserverLock() const
{
	return ActiveObserverLock.Pin();
}

// 【中文注释】Pin 弱引用获取活跃 creation context。
inline TSharedPtr<FMassObserverManager::FCreationContext> FMassObserverManager::GetCreationContext() const
{
	return ActiveCreationContext.Pin();
}
