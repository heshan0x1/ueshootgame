// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverNotificationTypes.cpp】
// FObserverLock 与 FCreationContext 的实现：
//   - FObserverLock 构造时递增 ObserverManager.LocksCount，析构时递减并触发 ResumeExecution
//   - FCreationContext 借助 FObserverLock 收集创建通知，析构时释放 handle
//   - GetEntityCollections 把内部 FEntityCollection 按 archetype 分组返回
// =============================================================================

#include "MassObserverNotificationTypes.h"
#include "MassObserverManager.h"
#include "MassEntityManager.h"
#include "MassEntityUtils.h"

namespace UE::Mass::ObserverManager
{
	namespace Private
	{
		// 【中文注释】返回一个全局单例的 dummy lock。
		// 用途：FCreationContext 默认构造（DebugCreateDummyCreationContext）时需要持有一个 lock 引用，
		// 但又不希望真正影响任何 EntityManager。dummy lock 没绑定 EntityManager，析构时 WeakEntityManager 为空，
		// ResumeExecution 路径会因 weak ptr.Pin 失败而早退。
		TSharedRef<FObserverLock> GetDummyObserverLock()
		{
			static TSharedRef<FObserverLock> DummyObserverLock = MakeShareable(new FObserverLock());
			return DummyObserverLock;
		}
	} // Private

	//-----------------------------------------------------------------------------
	// FObserverLock
	//-----------------------------------------------------------------------------
	// 【中文注释】构造：绑定 ObserverManager 所属的 EntityManager（弱引用），
	// 记录当前线程 ID 用于后续校验，并递增 LocksCount（重入计数）。
	// 调试构建额外保存创建时的 LockedNotificationSerialNumber。
	FObserverLock::FObserverLock(FMassObserverManager& ObserverManager)
		: OwnerThreadId(FPlatformTLS::GetCurrentThreadId())
		, WeakEntityManager(ObserverManager.EntityManager.AsWeak())
	#if WITH_MASSENTITY_DEBUG
		, LockSerialNumber(ObserverManager.LockedNotificationSerialNumber)
	#endif // WITH_MASSENTITY_DEBUG
	{
		++ObserverManager.LocksCount;
	}

	// 【中文注释】析构：lock 释放时执行收尾。
	// 流程：
	//   1. Pin EntityManager 弱引用；若已销毁（如 dummy lock 或 EntityManager 已 GC），直接返回；
	//   2. 递减 LocksCount，断言不可为负（防止重入失衡）；
	//   3. 调用 ObserverManager.ResumeExecution(*this) 触发缓冲通知派发。
	// 注意：如果 LocksCount 没归零（嵌套 lock），ResumeExecution 内部会断言失败 —— 
	// 实际由 GetOrMakeObserverLock 复用同一个 lock（共享指针）来保证嵌套时不会创建多个 lock 实例。
	FObserverLock::~FObserverLock()
	{
		TSharedPtr<FMassEntityManager> SharedEntityManager = WeakEntityManager.Pin();
		if (UNLIKELY(!SharedEntityManager))
		{
			return;
		}

		--SharedEntityManager->GetObserverManager().LocksCount;
		checkf(SharedEntityManager->GetObserverManager().LocksCount >= 0
			, TEXT("%hs: the lock count has become unbalanced."), __FUNCTION__);
		SharedEntityManager->GetObserverManager().ResumeExecution(*this);
	}

	// 【中文注释】processor fork 后重新绑定线程 ID。
	// fork 场景：主线程 fork 出 worker 后，原有 lock 的 OwnerThreadId 还是主线程，
	// 在 worker 中操作会触发 UE_CHECK_OWNER_THREADID 断言，因此需要主动更新。
	void FObserverLock::ForceUpdateCurrentThreadID()
	{
		OwnerThreadId = FPlatformTLS::GetCurrentThreadId();
	}

	//-----------------------------------------------------------------------------
	// FCreationContext
	//-----------------------------------------------------------------------------
	// 【中文注释】默认构造：使用 dummy lock，CreationHandle 未设置。
	// 仅用于 DebugCreateDummyCreationContext 的测试场景。
	FCreationContext::FCreationContext()
		: Lock(Private::GetDummyObserverLock())
	{	
	}

	// 【中文注释】析构：如果 CreationHandle 有效，通过 ObserverManager.ReleaseCreationHandle 释放。
	// 注意：此处只是把 CreationNotificationIndex 重置（避免后续创建挂到老条目），
	// 缓冲通知本身仍在 lock 中，等到 lock 析构时才真正触发 observer。
	// 多 context 共享同一 lock 时，每个 context 析构都会 ReleaseCreationHandle，但只有最后一个有效。
	FCreationContext::~FCreationContext()
	{
		if (CreationHandle.IsSet())
		{
			if (TSharedPtr<FMassEntityManager> SharedEntityManager = Lock->GetWeakEntityManager().Pin())
			{
				SharedEntityManager->GetObserverManager().ReleaseCreationHandle(CreationHandle);
			}
		}
	}

	// 【中文注释】调试用：构造一个 dummy CreationContext（不绑定真实数据），用于单测/调试 dump。
	TSharedRef<FCreationContext> FCreationContext::DebugCreateDummyCreationContext()
	{
		return MakeShareable(new FCreationContext());
	}

	// 【中文注释】返回创建过程中产生的所有 entity，按 archetype 分组。
	// 流程：
	//   1. 若 CreationHandle 未设置，返回空数组；
	//   2. 取出对应的缓冲通知项；
	//   3. 若 AffectedEntities 是 FEntityCollection，调用 GetUpToDatePerArchetypeCollections 触发懒重算；
	//   4. 若 AffectedEntities 是单个 handle，临时构造一个 collection 数组返回。
	TArray<FMassArchetypeEntityCollection> FCreationContext::GetEntityCollections(const FMassEntityManager& InEntityManager) const
	{
		TArray<FMassArchetypeEntityCollection> OutCollections;

		// if the creation handle isn't set there are no creation ops we know about
		if (CreationHandle.IsSet())
		{
			const FBufferedNotification& Notification = Lock->GetCreationNotification(CreationHandle);

			if (const UE::Mass::FEntityCollection* CreatedEntities = Notification.AffectedEntities.TryGet<FEntityCollection>())
			{
				OutCollections.Append(CreatedEntities->GetUpToDatePerArchetypeCollections(InEntityManager));
			}
			else
			{
				// 【中文注释】单 handle 路径：从 EntityManager 查 archetype 并构造一个临时 collection。
				const FMassEntityHandle EntityHandle = Notification.AffectedEntities.Get<FMassEntityHandle>();

				UE::Mass::Utils::CreateEntityCollections(InEntityManager, MakeArrayView(&EntityHandle, 1)
					, FMassArchetypeEntityCollection::NoDuplicates
					, OutCollections);
			}
		}

		return OutCollections;
	}

	// 【中文注释】调试用：检查内部 collection 是否还是最新的（未被 MarkDirty）。
	// 单 handle 形态没有"过期"概念（永远是最新），直接返回 true。
	bool FCreationContext::DebugAreEntityCollectionsUpToDate() const 
	{ 
		if (CreationHandle.IsSet())
		{
			const FBufferedNotification& Notification = Lock->GetCreationNotification(CreationHandle);

			// collections can be not up to date only if we're storing multiple entities (i.e. not a single handle)
			if (const UE::Mass::FEntityCollection* CreatedEntities = Notification.AffectedEntities.TryGet<FEntityCollection>())
			{
				return CreatedEntities->IsUpToDate();
			}
		}

		return true;
	}
} // UE::Mass::ObserverManager
