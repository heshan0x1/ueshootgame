// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverNotificationTypes.h】
//
// 本文件定义 Mass Observer（观察者）系统的"通知缓冲"相关类型。
//
// ## Observer 是 ECS 的"反应式"机制
// Mass 中的 Processor 是"主动"的：每帧定期遍历所有匹配的 entity 并执行逻辑。
// 而 Observer 是"被动"的：它绑定到某种 fragment/tag 类型 + 某种操作（Add/Remove/
// CreateEntity/DestroyEntity），当 EntityManager 上发生匹配的"组合变更事件"时
// 自动触发对应的 ObserverProcessor。
//
// 这与传统的"事件钩子"（hook）类似，但完全数据驱动：observer 不订阅"某个对象"，
// 而是订阅"某种数据类型 + 某种生命周期事件"。
//
// ## 为什么需要"缓冲"通知 —— FObserverLock
// 在批量操作（比如循环往同一个 entity 上加 N 个 fragment、或一次性创建上千个 entity）
// 中，如果每次小变更都立刻触发 observer，会有两个问题：
//   1. 性能损失：observer 多次执行；
//   2. 语义不正确：observer 看到的是"半成品"实体（只有部分 fragment 已加上）。
//
// FObserverLock 把所有变更通知"缓冲"起来，等 lock 释放（析构）时再统一触发，
// 让 observer 一次性看到最终的状态。
//
// ## FCreationContext 的特殊性
// FCreationContext 是 FObserverLock 的"特殊场景包装"：当一段代码在创建实体时，
// 我们希望"创建型 observer"（CreateEntity 类型）只在所有初始化都做完后才触发，
// 而不是创建瞬间——因为创建瞬间实体可能还没把所有 fragment 都加上。
// =============================================================================

#pragma once

#include "MassEntityHandle.h"
#include "Misc/TVariant.h"
#include "MassEntityCollection.h"
#include "MassEntityTypes.h"

#define UE_API MASSENTITY_API

// 【中文注释】线程绑定校验宏：FObserverLock 的所有操作必须在创建它的同一个线程中执行。
// 当前 lock 设计为单线程，多线程支持留作未来扩展。
#define UE_CHECK_OWNER_THREADID() checkf(OwnerThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("%hs: all FObserverLock operations are expected to be run in a single thread"), __FUNCTION__)

struct FMassObserverManager;
namespace UE::Mass
{
	struct FProcessingContext;
	namespace ObserverManager
	{
		// 【中文注释】旧版"被观察操作"枚举，5.7 已废弃。被通用的 EMassObservedOperation 替代。
		// 旧枚举只区分 Add/Remove/Create，新枚举增加了 AddElement/RemoveElement/CreateEntity/DestroyEntity 等更精细的操作。
		enum class
		UE_DEPRECATED(5.7, "The type is no longer being used, replaced by EMassObservedOperation")
		EObservedOperationNotification : uint8
		{
			Add = static_cast<uint8>(EMassObservedOperation::Add),
			Remove = static_cast<uint8>(EMassObservedOperation::Remove),
			Create
		};

		/**
		 * The type represents a single "operation", as observed by the registered observers, an operation that has been performed
		 * while the FObserverLock was active. Instances of FBufferedNotification contain all the information needed
		 * to send out the necessary notification once the observers lock gets released.
		 *
		 * Note that the type contains information necessary to sent our notification. In case of "Remove" notifications the
		 * operations has already been performed, and the data being removed is no longer available to the observers, and
		 * instances of FBufferedNotification do not host this information either. 
		 */
		// =============================================================================
		// 【FBufferedNotification - 缓冲通知项】
		//
		// 一个 FBufferedNotification 实例代表"在 FObserverLock 锁定期间发生的一次组合变更事件"。
		// lock 释放时，FMassObserverManager::ResumeExecution 会遍历所有缓冲项并依次触发 observer。
		//
		// 重要语义警告：
		//   对于 "Remove" 类型操作，组合变更"已经发生"了 —— 即数据已经从 entity 上移除。
		//   FBufferedNotification 内部并不保存被移除的数据，所以 lock 释放后被触发的
		//   "Pre-Remove" observer 实际上看不到被移除的旧数据（这是 buffering 的副作用）。
		//   非 lock 模式下，PreRemove observer 会在数据真正被删除前触发，可以读取旧数据。
		//
		// 数据布局：
		//   - Type：操作类型（AddElement / RemoveElement / CreateEntity / DestroyEntity）
		//   - CompositionChange：变更涉及的 fragment/tag 组合（用 TVariant 保存为四种之一）
		//   - AffectedEntities：受影响的实体（单个 handle 或 collection，TVariant 包装）
		// =============================================================================
		struct FBufferedNotification
		{
			// 【中文注释】"空组合"占位类型：用于 CreateEntity 通知，因为创建事件不需要"变更哪些 fragment"信息
			// （创建是一次性把整个 archetype 的所有 fragment 都"加上"，没有 delta 概念）。
			struct FEmptyComposition
			{
			};
			// 【中文注释】操作类型：决定执行 lock 释放时调用哪些 observer map（FragmentObservers[Type] / TagObservers[Type]）。
			EMassObservedOperation Type;
			// 【中文注释】变更内容的多态描述：四选一
			//   - FEmptyComposition：创建/销毁实体时使用（变更 = 整个 archetype）
			//   - FMassArchetypeCompositionDescriptor：同时涉及 fragment 和 tag 的混合变更
			//   - FMassFragmentBitSet：仅 fragment 变更
			//   - FMassTagBitSet：仅 tag 变更
			// 用 TVariant 而非全部字段，节省内存并明确单一分支语义。
			using FCompositionDescription = TVariant<FEmptyComposition, FMassArchetypeCompositionDescriptor, FMassFragmentBitSet, FMassTagBitSet>;
			FCompositionDescription CompositionChange;
			// 【中文注释】受影响实体的容器：单个 handle 或一个 FEntityCollection（多实体）。
			// 单 handle 是常见的优化路径（避免创建集合），如果后续合并了更多 entity 才会升级为 FEntityCollection。
			using FEntitiesContainer = TVariant<FEntityCollection, FMassEntityHandle>;
			FEntitiesContainer AffectedEntities;

			// 【中文注释】构造函数（重载1）：通用版本
			// 参数 Composition 通过 std::forward 转发，TInPlaceType 直接在 variant 内构造，零拷贝。
			template<typename TComposition>
			FBufferedNotification(const EMassObservedOperation InType, TComposition&& Composition, FEntitiesContainer&& Entities)
				: Type(InType)
				, CompositionChange(TInPlaceType<typename TDecay<TComposition>::Type>(), Forward<TComposition>(Composition))
				, AffectedEntities(MoveTemp(Entities))
			{
			}

			// 【中文注释】构造函数（重载2）：分别接收 composition 与 entities 的转发引用
			template<typename TComposition, typename TEntities>
			FBufferedNotification(const EMassObservedOperation InType, TComposition&& Composition, TEntities&& Entities)
				: Type(InType)
				, CompositionChange(TInPlaceType<typename TDecay<TComposition>::Type>(), Forward<TComposition>(Composition))
				, AffectedEntities(TInPlaceType<typename TDecay<TEntities>::Type>(), Forward<TEntities>(Entities))
			{
			}

			// 【中文注释】构造函数（重载3）：composition 已是 variant 形式时使用
			template<typename TEntities>
			FBufferedNotification(const EMassObservedOperation InType, FCompositionDescription&& Composition, TEntities&& Entities)
				: Type(InType)
				, CompositionChange(MoveTemp(Composition))
				, AffectedEntities(TInPlaceType<typename TDecay<TEntities>::Type>(), Forward<TEntities>(Entities))
			{
			}

			// 【中文注释】构造函数（重载4）：从 archetype collection 拷贝构造受影响实体集合（不获取所有权）
			FBufferedNotification(const EMassObservedOperation InType, FCompositionDescription&& Composition, const FMassArchetypeEntityCollection& Entities)
				: Type(InType)
				, CompositionChange(MoveTemp(Composition))
				, AffectedEntities(TInPlaceType<typename TDecay<FEntityCollection>::Type>(), Entities)
			{
			}

			// 【中文注释】判断本通知是否为"创建实体"事件 —— 用于 ResumeExecution 时分派到不同的执行器。
			bool IsCreationNotification() const
			{
				return Type == EMassObservedOperation::CreateEntity;
			}

			// 【中文注释】向当前通知追加一个 entity handle。
			// 如果当前 AffectedEntities 还是单 handle 形态，则升级为 FEntityCollection 以容纳多个。
			void AddHandle(const FMassEntityHandle EntityHandle)
			{
				FEntityCollection* StoredCollection = AffectedEntities.TryGet<FEntityCollection>();
				if (StoredCollection == nullptr)
				{
					// 单 handle → 升级为 collection
					StoredCollection = &ConvertStoredHandleToCollection(TConstArrayView<FMassEntityHandle>());
				}
				StoredCollection->AddHandle(EntityHandle);
			}

			// 【中文注释】AppendEntities 的单 handle 重载：直接转发到 AddHandle。
			inline void AppendEntities(const FMassEntityHandle EntityHandle)
			{
				AddHandle(EntityHandle);
			}

			// 【中文注释】AppendEntities：批量追加 handle 数组。
			// 如果当前是单 handle 形态会先升级为 collection，再追加新 handle 数组。
			void AppendEntities(const TConstArrayView<FMassEntityHandle> InEntityHandles)
			{
				if (FEntityCollection* StoredCollection = AffectedEntities.TryGet<FEntityCollection>())
				{
					StoredCollection->AppendHandles(InEntityHandles);
				}
				else
				{
					ConvertStoredHandleToCollection(InEntityHandles);
				}
			}

			// 【中文注释】AppendEntities 重载：除了 handle 数组还提供一个预先构建的 archetype collection（性能加速用）。
			// 注意：如果当前已经存了集合，传进来的 archetype collection 会被丢弃（需要重建），见下方注释。
			void AppendEntities(const TConstArrayView<FMassEntityHandle> InEntityHandles, FMassArchetypeEntityCollection&& EntityCollection)
			{
				if (FEntityCollection* StoredCollection = AffectedEntities.TryGet<FEntityCollection>())
				{
					StoredCollection->AppendHandles(InEntityHandles, Forward<FMassArchetypeEntityCollection>(EntityCollection));
				}
				else
				{
					ConvertStoredHandleToCollection(InEntityHandles);
					// we're ignoring EntityCollection since the collections will need to be rebuilt anyway,
					// due to AffectedEntities already containing some data before this call
					// 【中文注释】此处忽略外部传入的 EntityCollection：因为已有数据就位后必须重建集合，
					// 直接用传入的预构建集合反而会"少算"已有的 entity，所以走 ConvertStoredHandleToCollection 路径。
				}
			}

			// 【中文注释】AppendEntities 模板重载：直接接收一个完整的 FMassArchetypeEntityCollection（按值或按右值引用）。
			// requires 子句保证只匹配 FMassArchetypeEntityCollection 类型。
			template<typename T> requires std::is_same_v<typename TDecay<T>::Type, FMassArchetypeEntityCollection>
			void AppendEntities(T&& InEntityCollection)
			{
				if (FEntityCollection* StoredCollection = AffectedEntities.TryGet<FEntityCollection>())
				{
					StoredCollection->AppendCollection(Forward<T>(InEntityCollection));
				}
				else
				{
					ConvertStoredHandleToCollection(Forward<T>(InEntityCollection));
				}
			}

			// 【中文注释】将存储的 entity collection 标记为"过期"。
			// 当组合变更操作发生时（lock 期间），entity 在哪个 archetype 可能改变了，
			// 之前缓存的"per-archetype 子集合"必须重新计算（lazy 机制）。
			void DirtyAffectedEntities()
			{
				if (FEntityCollection* StoredCollection = AffectedEntities.TryGet<FEntityCollection>())
				{
					StoredCollection->MarkDirty();
				}
			}

			// 【中文注释】静态工具：比较两个 CompositionDescription 是否"等价"。
			// 用于 coalesce（合并）相邻通知 —— 如果新缓冲项与上一项的操作类型 + 组合都相同，
			// 就把 entity 列表追加到上一项里，避免缓冲项数量爆炸。
			// variant 的 GetIndex 不同时直接判 false。
			static bool AreCompositionsEqual(const FCompositionDescription& A, const FCompositionDescription& B)
			{
				if (A.GetIndex() == B.GetIndex())
				{
					switch (A.GetIndex())
					{
						case FCompositionDescription::IndexOfType<FEmptyComposition>():
							return true;
						case FCompositionDescription::IndexOfType<FMassArchetypeCompositionDescriptor>():
							return A.Get<FMassArchetypeCompositionDescriptor>().IsIdentical(B.Get<FMassArchetypeCompositionDescriptor>());
						case FCompositionDescription::IndexOfType<FMassFragmentBitSet>():
							return A.Get<FMassFragmentBitSet>() == B.Get<FMassFragmentBitSet>();
						case FCompositionDescription::IndexOfType<FMassTagBitSet>():
							return A.Get<FMassTagBitSet>() == B.Get<FMassTagBitSet>();
						default:
							return false;
					}
				}
				return false;
			}

		private:
			// 【中文注释】私有工具：把 AffectedEntities 从 "单 handle" 形态转换为 "FEntityCollection" 形态。
			// 操作步骤：
			//   1. 把已存的单 handle 暂存到 StoredHandle；
			//   2. 用 InEntities 构造一个新的 FEntityCollection，原地替换到 variant 中；
			//   3. 把暂存的 handle 也加进去（保留原有数据）；
			// 返回新构造的 collection 引用，便于调用方继续追加。
			template<typename TEntities> 
			FEntityCollection& ConvertStoredHandleToCollection(TEntities&& InEntities)
			{
				// AffectedEntities holds a single handle. We need to extract it and emplace a FEntityCollection instance 
				const FMassEntityHandle StoredHandle = AffectedEntities.Get<FMassEntityHandle>();
				AffectedEntities.Emplace<FEntityCollection>(Forward<TEntities>(InEntities));
				FEntityCollection& StoredEntities = AffectedEntities.Get<FEntityCollection>();
				StoredEntities.AddHandle(StoredHandle);
				return StoredEntities;
			}
		};

		/** Simple handle type representing a entity creation notification as stored by FObserverLock */
		// =============================================================================
		// 【FCreationNotificationHandle - 创建通知句柄】
		//
		// 简单的"句柄"类型：FObserverLock 内部为 CreateEntity 类型的通知保留一个特殊位置，
		// FCreationNotificationHandle 就是用于"指向"这个位置的句柄。
		//
		// 为什么需要单独的 handle？
		// 创建实体的过程中，常常会接着对刚创建的实体做组合变更（加 fragment、加 tag 等），
		// 这些后续变更应该归属于"同一次创建事件"，而不是产生独立的 Add 通知。
		// 句柄用于在批量创建期间标识"我对应的那条创建通知"。
		// =============================================================================
		struct FCreationNotificationHandle
		{
			// 【中文注释】句柄是否已被分配（是否对应一个实际的缓冲通知项）。
			bool IsSet() const
			{
				return OpIndex != INDEX_NONE;
			}

			// 【中文注释】隐式转 int：直接拿到 BufferedNotifications 数组中的索引。
			operator int() const
			{
				return OpIndex;
			}

		private:
			friend FMassObserverManager;

			/**
			 * set upon creation to the value of LockedNotificationSerialNumber.
			 * This property's value is checked when the creation handle gets "released" via
			 * FMassObserverManager::ReleaseCreationHandle
			 */ 
			// 【中文注释】序列号：句柄创建时记录 ObserverManager 的当前序列号。
			// 每次 ResumeExecution 后序列号递增，因此过期的 handle 不会错误匹配新一轮的 lock 数据。
			// 这是一个轻量的 ABA 防御机制。
			uint32 SerialNumber = 0;

			// 【中文注释】句柄指向的 BufferedNotifications 数组下标。INDEX_NONE 表示句柄未初始化。
			int32 OpIndex = INDEX_NONE;
		};

		/**
		 * Once created with MassObserverManager.GetOrMakeObserverLock will prevent triggering
		 * observers and instead buffer all the notifications to be sent.
		 * Once the FObserverLock gets released it will call MassObserverManager.ResumeExecution
		 * that will send out all the buffered notifications.
		 * 
		 * @note that due to the buffering, all the "Remove" operation observers will be sent out later
		 * than usually - without locking those observers get triggered before the removal operation is
		 * performed, and as such have access to the data "about to be removed". Removal observers sent out
		 * after lock release won't have access to that information.
		 * 
		 * There's a special path for freshly created entities, see FCreationContext for more details.
		 */
		// =============================================================================
		// 【FObserverLock - 观察者锁】
		//
		// 当通过 FMassObserverManager::GetOrMakeObserverLock() 获取一个 lock 后，
		// 在 lock 存活期间，所有的 observer 触发都会被"缓冲"而不是立即执行。
		// lock 析构时会调用 FMassObserverManager::ResumeExecution，将缓冲的所有通知一次性派发。
		//
		// ## 典型使用场景
		// ```
		// {
		//     auto Lock = ObserverManager.GetOrMakeObserverLock();
		//     for (auto Entity : Entities) {
		//         EntityManager.AddFragment(Entity, MyFragment);
		//     }
		// }   // ← lock 在此析构，所有缓冲的 Add 通知统一触发
		// ```
		//
		// ## 为什么要 buffer？
		//   1. 性能：避免一千个 entity 触发一千次 observer，合并成一次 batch；
		//   2. 一致性：observer 看到的是"操作完成后"的最终状态，而不是中间半成品。
		//
		// ## "Remove" 观察者的语义损失
		// 没有 lock 时：PreRemove observer 在数据被删除前触发，可以读到"将要被删的旧数据"。
		// 有 lock 时：通知被 buffer，观察者直到 lock 释放才触发——而此时数据早已删除。
		// 因此 "Remove" 类型的 observer 在 lock 期间会"看不到旧数据"，
		// ResumeExecution 中会通过 VLog 记录这种情况。
		//
		// ## 嵌套通知 / 重入避免
		// 当 observer 在执行时又对 EntityManager 做组合变更，会再次进入此 lock。
		// 由于 ResumeExecution 只在 LocksCount == 0 时才执行（通过引用计数 LocksCount 管理），
		// 嵌套的变更通知会被新的缓冲项收集，避免无限递归。
		//
		// ## 创建实体的特殊路径
		// 创建实体（CreateEntity）走单独的 FCreationContext 流程 —— 见 FCreationContext。
		// =============================================================================
		struct FObserverLock
		{
			// 【中文注释】默认构造：用于 dummy lock（GetDummyObserverLock 单例），
			// 避免空 FCreationContext 解引用时崩溃。
			FObserverLock() = default;
			UE_API ~FObserverLock();

			// 【中文注释】返回所属 EntityManager 的弱引用。析构期间用于回调 ResumeExecution。
			TWeakPtr<FMassEntityManager> GetWeakEntityManager() const
			{
				return WeakEntityManager;
			}

			// 【中文注释】把指定创建通知的 entity collection 标记为脏。
			// 当 CreationContext 期间发生组合变更（add/remove fragment）时调用，
			// 强制 collection 在下次访问时重新生成 per-archetype 子视图。
			void MarkCreationNotificationDirty(FCreationNotificationHandle CreationHandle)
			{
				ensureMsgf(CreationHandle == CreationNotificationIndex, TEXT("Given creation handle doesn't match this Lock's data"));
				checkf(BufferedNotifications.IsValidIndex(CreationHandle), TEXT("Given CreationHandle doesn't match stored notifications"));
				BufferedNotifications[CreationHandle].DirtyAffectedEntities();
			}

			// 【中文注释】只读访问指定句柄对应的缓冲通知项。供 FCreationContext 读取已创建实体集合用。
			const FBufferedNotification& GetCreationNotification(FCreationNotificationHandle CreationHandle) const
			{
				ensureMsgf(CreationHandle == CreationNotificationIndex, TEXT("Given creation handle doesn't match this Lock's data"));
				checkf(BufferedNotifications.IsValidIndex(CreationHandle), TEXT("Given CreationHandle doesn't match stored notifications"));

				return BufferedNotifications[CreationHandle];
			}

		private:
			// 【中文注释】真正的私有构造：FMassObserverManager 通过它创建 lock；
			// 同时递增 ObserverManager 的 LocksCount 引用计数。
			UE_API explicit FObserverLock(FMassObserverManager& ObserverManager);

			// 【中文注释】内部工具：获取或创建"创建通知"的索引。
			// 一个 lock 内最多只有一条 CreateEntity 类型的缓冲项（所有创建合并到这一项），
			// 所以这里用 CreationNotificationIndex 作为单例标记。
			// 注释中保留了原作者尝试 atomic 路径的痕迹（注释里的 compare_exchange_weak），
			// 当前是单线程实现，可见 UE_CHECK_OWNER_THREADID。
			int32 GetOrCreateCreationNotification()
			{
				UE_CHECK_OWNER_THREADID();
				//if (CreationNotificationIndex.compare_exchange_weak(LocalCreationNotificationIndex, BufferedNotifications.Num()))
				if (CreationNotificationIndex == INDEX_NONE)
				{
					CreationNotificationIndex = BufferedNotifications.Num();
					BufferedNotifications.Emplace(EMassObservedOperation::CreateEntity
						, FBufferedNotification::FEmptyComposition{}
						, UE::Mass::FEntityCollection());
				}
				return CreationNotificationIndex;
			}

			// 【中文注释】释放创建通知"占位"。
			// 注意：这里"释放"只是把 CreationNotificationIndex 重置为 INDEX_NONE，
			// 这样后续如果还在 lock 期间又有创建实体，可以再开一条新的 CreateEntity 缓冲项；
			// 而 BufferedNotifications 中已存的项不会被删除，等 ResumeExecution 时一起触发。
			// 返回值：true 表示成功释放（句柄匹配当前 index），false 表示句柄已失配（异常情况）。
			bool ReleaseCreationNotification(FCreationNotificationHandle CreationHandle)
			{
				UE_CHECK_OWNER_THREADID();
				checkf(BufferedNotifications.IsValidIndex(CreationHandle), TEXT("Given CreationHandle doesn't match stored notifications"));

				int32 CreationHandleOpIndex = CreationHandle;
				if (CreationNotificationIndex == CreationHandleOpIndex)
				{
					CreationNotificationIndex = INDEX_NONE;
					return true;
				}
				// else 
				ensureMsgf(CreationHandle == CreationNotificationIndex, TEXT("Given creation handle doesn't match this Lock's data"));
				return false;
			}

			// 【中文注释】添加一个新创建的 entity 到当前 lock 的"创建通知"中。
			// 第一次调用会新建一条 CreateEntity 缓冲项；后续调用会追加到同一项里。
			// 返回 BufferedNotifications 中的索引（即句柄的 OpIndex 值）。
			int32 AddCreatedEntity(FMassEntityHandle CreatedEntity)
			{
				UE_CHECK_OWNER_THREADID();
				if (CreationNotificationIndex == INDEX_NONE)
				{
					CreationNotificationIndex = BufferedNotifications.Num();
					BufferedNotifications.Emplace(EMassObservedOperation::CreateEntity
						, FBufferedNotification::FEmptyComposition{}
						, CreatedEntity);
				}
				else
				{
					BufferedNotifications[CreationNotificationIndex].AddHandle(CreatedEntity);
				}

				return CreationNotificationIndex;
			}

			// 【中文注释】批量添加新创建的 entity（数组形式）。语义同 AddCreatedEntity。
			int32 AddCreatedEntities(const TConstArrayView<FMassEntityHandle> InCreatedEntities)
			{
				UE_CHECK_OWNER_THREADID();
				if (CreationNotificationIndex == INDEX_NONE)
				{
					CreationNotificationIndex = BufferedNotifications.Num();
					BufferedNotifications.Emplace(EMassObservedOperation::CreateEntity
						, FBufferedNotification::FEmptyComposition{}
						, UE::Mass::FEntityCollection(InCreatedEntities));
				}
				else
				{
					BufferedNotifications[CreationNotificationIndex].AppendEntities(InCreatedEntities);
				}

				return CreationNotificationIndex;
			}

			// 【中文注释】批量添加 + 提供预构建的 archetype collection（避免 ResumeExecution 时再算一次）。
			int32 AddCreatedEntities(const TConstArrayView<FMassEntityHandle> InCreatedEntities, FMassArchetypeEntityCollection&& InEntityCollection)
			{
				UE_CHECK_OWNER_THREADID();
				if (CreationNotificationIndex == INDEX_NONE)
				{
					CreationNotificationIndex = BufferedNotifications.Num();
					BufferedNotifications.Emplace(EMassObservedOperation::CreateEntity, FBufferedNotification::FEmptyComposition{}
						, UE::Mass::FEntityCollection(InCreatedEntities, Forward<FMassArchetypeEntityCollection>(InEntityCollection)));
				}
				else
				{
					BufferedNotifications[CreationNotificationIndex].AppendEntities(InCreatedEntities, Forward<FMassArchetypeEntityCollection>(InEntityCollection));
				}

				return CreationNotificationIndex;
			}

			// 【中文注释】仅传入 archetype collection（无独立 handle 数组）的版本。
			// requires 子句限制 T 为 FMassArchetypeEntityCollection。
			template<typename T> requires std::is_same_v<typename TDecay<T>::Type, FMassArchetypeEntityCollection>
			int32 AddCreatedEntitiesCollection(T&& InEntityCollection)
			{
				UE_CHECK_OWNER_THREADID();
				if (CreationNotificationIndex == INDEX_NONE)
				{
					CreationNotificationIndex = BufferedNotifications.Num();
					BufferedNotifications.Emplace(EMassObservedOperation::CreateEntity
						, FBufferedNotification::FEmptyComposition{}
						, UE::Mass::FEntityCollection(Forward<T>(InEntityCollection)));
				}
				else
				{
					BufferedNotifications[CreationNotificationIndex].AppendEntities<T>(InEntityCollection);
				}

				return CreationNotificationIndex;
			}

			// 【中文注释】添加一条非创建型通知（AddElement / RemoveElement / DestroyEntity）。
			// 实现"通知合并"（coalesce）：如果上一条缓冲项是相同操作类型 + 相同组合变更，
			// 则把新的 entity 追加进去；否则新建一条缓冲项。
			//
			// CompositionChange 的构造逻辑：
			//   - 仅 fragment 重叠：用 FMassFragmentBitSet
			//   - 仅 tag 重叠：用 FMassTagBitSet
			//   - 同时有 fragment 和 tag：用 FMassArchetypeCompositionDescriptor 包裹
			// 这种分层是为了节省内存：单一类型的变更不需要构造完整的 archetype descriptor。
			template<typename TEntities>
			void AddNotification(const EMassObservedOperation OperationType
				, TEntities&& Entities
				, const bool bHasFragmentsOverlap, FMassFragmentBitSet&& FragmentOverlap
				, const bool bHasTagsOverlap, FMassTagBitSet&& TagOverlap)
			{
				checkSlow(bHasFragmentsOverlap || bHasTagsOverlap);
				FBufferedNotification::FCompositionDescription CompositionChange = (bHasFragmentsOverlap != bHasTagsOverlap)
					? (bHasFragmentsOverlap
						? FBufferedNotification::FCompositionDescription(TInPlaceType<FMassFragmentBitSet>(), MoveTemp(FragmentOverlap))
						: FBufferedNotification::FCompositionDescription(TInPlaceType<FMassTagBitSet>(), MoveTemp(TagOverlap)))
					: FBufferedNotification::FCompositionDescription(TInPlaceType<FMassArchetypeCompositionDescriptor>(), FMassArchetypeCompositionDescriptor(MoveTemp(FragmentOverlap), MoveTemp(TagOverlap), {}, {}, {}));

				// 【中文注释】合并优化：若上一条与当前完全等价，则就地追加 entity，避免 BufferedNotifications 数组膨胀。
				if (BufferedNotifications.Num() 
					&& BufferedNotifications.Last().Type == OperationType
					&& FBufferedNotification::AreCompositionsEqual(BufferedNotifications.Last().CompositionChange, CompositionChange))
				{
					BufferedNotifications.Last().AppendEntities(Forward<TEntities>(Entities));
				}
				else
				{
					BufferedNotifications.Emplace(OperationType, MoveTemp(CompositionChange), Forward<TEntities>(Entities));
				}
			}

			friend FMassObserverManager;
			/** To be called in case of processor forking. */
			// 【中文注释】processor fork 后调用：把 OwnerThreadId 更新为当前线程。
			// fork 场景下 lock 跟随 processor 迁移到新线程，必须显式更新线程绑定。
			UE_API void ForceUpdateCurrentThreadID();

			/**
			 * Identifies the thread where given FObserverLock instance was created. All subsequent operations are 
			 * expected to be run in the same thread.
			 */
			// 【中文注释】lock 创建时记录的线程 ID，用于 UE_CHECK_OWNER_THREADID 校验。
			uint32 OwnerThreadId;

			// 【中文注释】当前 lock 中"创建通知"在 BufferedNotifications 中的索引。INDEX_NONE 表示未创建。
			int32 CreationNotificationIndex = INDEX_NONE;

			// 【中文注释】缓冲通知数组：lock 期间收集的所有变更事件，lock 析构时由 ResumeExecution 处理。
			TArray<FBufferedNotification> BufferedNotifications;

			// 【中文注释】构造 entity collection 时的"重复处理策略"配置：
			// 默认 NoDuplicates 假定输入 handle 数组中没有重复，省去了去重检查。
			FMassArchetypeEntityCollection::EDuplicatesHandling CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::EDuplicatesHandling::NoDuplicates;

			/** Point to outer EntityManager. Used to obtain ObserverManager in type's destructor*/
			// 【中文注释】outer EntityManager 的弱引用：析构期间用于反查 ObserverManager 并触发 ResumeExecution。
			// 用 weak 是为了应对 EntityManager 已销毁的情况（避免悬挂指针）。
			TWeakPtr<FMassEntityManager> WeakEntityManager;

	#if WITH_MASSENTITY_DEBUG
			// 【中文注释】调试用序列号：lock 创建时记录 ObserverManager 的当前序列号；
			// ResumeExecution 时校验 lock 与 ObserverManager 的序列号匹配，防止"跨轮"使用。
			uint32 LockSerialNumber = 0;
	#endif // WITH_MASSENTITY_DEBUG
		};

		/**
		 * A dedicated structure for ensuring the "on entities creation" observers get notified only once all other 
		 * initialization operations are done and this creation context instance gets released. 
		 */
		// =============================================================================
		// 【FCreationContext - 实体创建作用域】
		//
		// 这是一个"创建实体"的专用作用域类，与 FObserverLock 协作使用。它的语义比 lock 更细：
		// 在 CreationContext 存活期间创建的所有实体，会归集到一条 CreateEntity 缓冲通知，
		// 且后续的初始化变更（添加 fragment/tag 等）不会被当作独立的 Add 事件 —— 
		// 而是被当作"创建过程的一部分"，直到 context 析构时统一触发 CreateEntity observer。
		//
		// 这避免了一个常见问题：
		//   ```
		//   auto Entity = EntityManager.CreateEntity();          // 触发 Create observer
		//   EntityManager.AddFragment(Entity, MyFragment);       // 触发 Add observer
		//   ```
		// 在没有 CreationContext 时，Create observer 会在 fragment 还没加上时就被触发，
		// 导致 observer 看到的是"未初始化完毕"的实体；有了 CreationContext，
		// Create observer 会等到所有初始化完成后才触发，看到的就是完整状态。
		//
		// 内部实现：
		//   FCreationContext 持有一个 FObserverLock 引用 + 一个 FCreationNotificationHandle，
		//   handle 指向 lock 的 BufferedNotifications 中的"创建通知"项。
		//   context 析构时 → ReleaseCreationHandle → lock 析构 → ResumeExecution 触发 observer。
		// =============================================================================
		struct FCreationContext
		{
			// 【中文注释】返回创建过程产生的所有 entity 集合，按 archetype 分组。
			// 调用此函数会触发 collection 的"按 archetype 分桶"计算（如果之前被 MarkDirty 过）。
			UE_API TArray<FMassArchetypeEntityCollection> GetEntityCollections(const FMassEntityManager& EntityManager) const;

			/** Function for debugging/testing purposes. We don't expect users to ever call it, always get collections via GetEntityCollections */
			// 【中文注释】调试用：检查内部缓存的 collection 是否与最新数据一致。
			// 业务代码不应使用此函数，始终用 GetEntityCollections 获取最新数据。
			UE_API bool DebugAreEntityCollectionsUpToDate() const;

			// 【中文注释】析构：如果 CreationHandle 仍有效，会通知 ObserverManager 释放 handle。
			// 真正触发 CreateEntity observer 的时机是 lock 也析构时（lock 引用计数归零）。
			UE_API ~FCreationContext();

			// 【中文注释】调试用：构造一个 dummy CreationContext（不绑定真实 EntityManager），用于单测。
			static UE_API TSharedRef<FCreationContext> DebugCreateDummyCreationContext();

			// 【中文注释】---- 已废弃 API（5.5 / 5.6）----
			UE_DEPRECATED(5.6, "Use the other GetEntityCollections flavor insteand")
			UE_API TConstArrayView<FMassArchetypeEntityCollection> GetEntityCollections() const;
			UE_DEPRECATED(5.6, "Functionality no longer available")
			UE_API int32 GetSpawnedNum() const;
			UE_DEPRECATED(5.6, "Do not use, internal use only")
			UE_API bool IsDirty() const;
			UE_DEPRECATED(5.6, "Manually adding entities directly to the creation context is not longer supported and is not taking place automatically")
			UE_API void AppendEntities(const TConstArrayView<FMassEntityHandle>);
			UE_DEPRECATED(5.6, "Manually adding entities directly to the creation context is not longer supported and is not taking place automatically")
			UE_API void AppendEntities(const TConstArrayView<FMassEntityHandle>, FMassArchetypeEntityCollection&&);
			UE_DEPRECATED(5.5, "This constructor is now deprecated and defunct. Use one of the others instead.")
			UE_API explicit FCreationContext(const int32);
			UE_DEPRECATED(5.5, "This function is now deprecated since FEntityCreationContext can contain more than a single collection now. Use GetEntityCollections instead.")
			UE_API const FMassArchetypeEntityCollection& GetEntityCollection() const;
			
			/**
			 *	Called in response to composition mutating operation - these operations invalidate stored collections
			 */
			// 【中文注释】当创建过程中实体的组合发生变更（添加 fragment/tag），调用此函数把缓存的 collection 标记过期。
			// 因为 entity 可能从一个 archetype 迁移到另一个 archetype，原有的 per-archetype 分桶不再有效。
			UE_DEPRECATED_FORGAME(5.6, "Do not use, internal use only")
			void MarkDirty()
			{
				Lock->MarkCreationNotificationDirty(CreationHandle);
			}

		private:
			// 【中文注释】默认构造：用 dummy lock 构造一个"空" context，调试/测试用。
			UE_API FCreationContext();

			// 【中文注释】真正使用的构造：包装一个已存在的 FObserverLock。
			// 由 FMassObserverManager::GetOrMakeCreationContext 调用。
			FCreationContext(TSharedRef<FObserverLock>&& InLock)
				: Lock(Forward<TSharedRef<FObserverLock>>(InLock))
			{	
			}
			FCreationContext(const TSharedRef<FObserverLock>& InLock)
				: Lock(InLock)
			{
			}

			// 【中文注释】返回内部持有的 lock。CreationContext 借用 lock 的缓冲机制，多个 context 可共享同一个 lock。
			TSharedRef<FObserverLock> GetObserverLock() const
			{
				return Lock;
			}

			// 【中文注释】context 是否绑定了有效的 creation 通知项。
			bool IsValid() const
			{
				return CreationHandle.IsSet();
			}

			friend FMassObserverManager;
			// 【中文注释】持有 lock 的强引用：context 析构时 lock 仍在生效（共享所有权），
			// 等到 lock 引用计数归零时才会释放并触发 ResumeExecution。
			TSharedRef<FObserverLock> Lock;
			// 【中文注释】指向 lock 内 BufferedNotifications 中"创建通知"项的句柄。
			FCreationNotificationHandle CreationHandle;
		};
	} // namespace UE::Mass::ObserverManager
} // namespace UE::Mass

#undef UE_CHECK_OWNER_THREADID

#undef UE_API
