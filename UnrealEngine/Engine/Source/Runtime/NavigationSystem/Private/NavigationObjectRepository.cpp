// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// NavigationObjectRepository.cpp —— UNavigationObjectRepository 的实现。
// 关键点：
//   - AddNavigationElement 直接接收 FNavigationElement rvalue，MakeShared 后
//     放进 NavRelevantElements；广播 OnNavigationElementAddedDelegate。
//   - RegisterNavRelevantObject 是"传统 UObject/Interface"路径：内部转调
//     RegisterNavRelevantObjectInternal，若已注册则原地更新 FNavigationElement；
//     否则首次走 AddNavigationElement + 写 ObjectsToHandleMap。
//   - UpdateNavigationElementForUObject 供 NavigationSystem 自己触发"同步元素"
//     时使用，带 ENotifyOnSuccess::No 避免事件回环（调用者已经在处理变更了）。
//   - 控制台命令 'ai.debug.nav.DumpRepositoryElements' 可打印所有登记条目。
// ============================================================================

#include "NavigationObjectRepository.h"
#include "Misc/OutputDevice.h"
#include "NavigationSystem.h"
#include "NavLinkCustomInterface.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationElement.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "UObject/ObjectKey.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationObjectRepository)

namespace UE::Navigation::Private
{
static FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdDumpRepositoryElements(
	TEXT("ai.debug.nav.DumpRepositoryElements"),
	TEXT("Logs details about each element stored in the navigation repository to the output device."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World, FOutputDevice& OutputDevice)
		{
			if (const UNavigationObjectRepository* Repository = World->GetSubsystem<UNavigationObjectRepository>())
			{
				int32 NumElements = 0;

				Repository->ForEachNavigationElement([&OutputDevice, &NumElements](const TSharedRef<const FNavigationElement>& Element)
					{
						NumElements++;
						OutputDevice.Logf(ELogVerbosity::Log, TEXT("%s bounds: [%s] parent:'%s'"),
							*Element->GetPathName(),
							*Element->GetBounds().ToString(),
							*GetNameSafe(Element->GetNavigationParent().Get()));
					});

				OutputDevice.Logf(ELogVerbosity::Log, TEXT("Total: %d elements"), NumElements);
			}
			else
			{
				OutputDevice.Log(ELogVerbosity::Error, TEXT("Command failed since it was unable to find the navigation repository"));
			}
		})
	);
} // UE::Navigation::Private

// AddNavigationElement：新式注册入口（已有 FNavigationElement 右值）。
//  1) 非 Shipping 下校验重复注册；
//  2) MakeShared 包装 → 存进 NavRelevantElements；
//  3) 按需广播 OnNavigationElementAddedDelegate（NavigationSystem 监听后做 Octree 登记）。
// 线程：写锁 scope 覆盖到 Emplace，确保 NavRelevantElements 写入安全。
TSharedPtr<const FNavigationElement> UNavigationObjectRepository::AddNavigationElement(FNavigationElement&& Element, const ENotifyOnSuccess NotifyOnSuccess /*= ENotifyOnSuccess::Yes*/)
{
#if DO_ENSURE // We don't want to execute the Find at all for targets where ensures are disabled
	{
		UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

		if (!ensureMsgf(NavRelevantElements.Find(Element.GetHandle()) == nullptr, TEXT("Same element can't be registered twice.")))
		{
			return nullptr;
		}
	}
#endif

	const TSharedRef SharedElement(MakeShared<FNavigationElement>(MoveTemp(Element)));
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		NavRelevantElements.Emplace(Element.GetHandle(), SharedElement);
	}

	if (NotifyOnSuccess == ENotifyOnSuccess::Yes)
	{
		(void)OnNavigationElementAddedDelegate.ExecuteIfBound(SharedElement);
	}

	return SharedElement.ToSharedPtr();
}

// RemoveNavigationElement：从主存储移除；RemoveAndCopyValue 保留元素指针以便触发
// OnNavigationElementRemovedDelegate（广播到 NavigationSystem 做 Octree 反注册）。
void UNavigationObjectRepository::RemoveNavigationElement(const FNavigationElementHandle Handle)
{
	UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);

	TSharedPtr<FNavigationElement> Element;
	if (ensureMsgf(NavRelevantElements.RemoveAndCopyValue(Handle, Element),
		TEXT("Navigation element can't be removed since it was not registered or already unregistered)")))
	{
		(void)OnNavigationElementRemovedDelegate.ExecuteIfBound(Element.ToSharedRef());
	}
}

// 只读遍历全部登记元素；使用 shared 指针可以避免迭代期被其他线程移除后空悬。
void UNavigationObjectRepository::ForEachNavigationElement(TFunctionRef<void(const TSharedRef<const FNavigationElement>&)> PerElementCallback) const
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	// 循环不变量：遍历 NavRelevantElements 内所有 (Handle → SharedPtr) 项，对每个有效项回调
	for (auto It = NavRelevantElements.CreateConstIterator(); It; ++It)
	{
		if (const TSharedPtr<const FNavigationElement>& Element = It.Value())
		{
			PerElementCallback(Element.ToSharedRef());
		}
	}
}

// 基于 INavRelevantInterface 的外部入口。转成 UObject 后走内部统一实现。
TSharedPtr<const FNavigationElement> UNavigationObjectRepository::RegisterNavRelevantObject(const INavRelevantInterface& NavRelevantObject)
{
	return RegisterNavRelevantObjectInternal(NavRelevantObject, *Cast<UObject>(&NavRelevantObject), ENotifyOnSuccess::Yes);
}

// 只在项目设置要求创建 NavigationSystem 时才创建本 Subsystem，避免无导航的关卡浪费。
bool UNavigationObjectRepository::ShouldCreateSubsystem(UObject* Outer) const
{
	return (Super::ShouldCreateSubsystem(Outer))
		&& GetDefault<UNavigationSystemV1>()->ShouldCreateNavigationSystemInstance(Cast<UWorld>(Outer));
}

// 传统路径：UObject + INavRelevantInterface。
// 分支 1：ExistingElement 存在 → 原地覆盖 FNavigationElement（典型于 Actor 先注册
//         了 Component、随后 Component OnRegister 又来了一次——走更新而不是重复插入）。
// 分支 2：不存在但 IsNavigationRelevant → 新建 + 写 ObjectsToHandleMap。
// 分支 3：不 Relevant → 跳过并打 VeryVerbose。
TSharedPtr<const FNavigationElement> UNavigationObjectRepository::RegisterNavRelevantObjectInternal(
	const INavRelevantInterface& NavRelevantInterface,
	const UObject& NavRelevantObject,
	const ENotifyOnSuccess NotifyOnSuccess)
{
	// In AActor/UActorComponent code paths it is possible that a component registration is performed more than once
	// (i.e., Actor registering its component, then individual component registers too)
	// In such case we update with the latest.
	if (const TSharedPtr<FNavigationElement> ExistingElement = GetMutableNavigationElementForUObject(&NavRelevantObject))
	{
		const FBox PreviousBounds = ExistingElement->GetBounds();
		{
			UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
			// in-place 覆盖：保留 Handle 不变，Bounds / Delegate / Parent 等重新采集
			*ExistingElement = FNavigationElement(NavRelevantInterface);
		}

		if (NotifyOnSuccess == ENotifyOnSuccess::Yes)
		{
			(void)OnNavigationElementAddedDelegate.ExecuteIfBound(ExistingElement.ToSharedRef());
		}

		UE_LOG(LogNavigation, Verbose, TEXT("%hs [already registered - updating] (%s:%s) Bounds: [%s]->[%s]"), __FUNCTION__,
			*GetNameSafe(NavRelevantObject.GetOuter()), *GetNameSafe(&NavRelevantObject),
			*PreviousBounds.ToString(), *ExistingElement->GetBounds().ToString());

		return ExistingElement;
	}

	if (NavRelevantInterface.IsNavigationRelevant())
	{
		if (const TSharedPtr<const FNavigationElement> SharedElement = AddNavigationElement(FNavigationElement(NavRelevantInterface), NotifyOnSuccess))
		{
			UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
			// 反查表：UObject → Handle，让后续 UnregisterNavRelevantObject(UObject*) 能够找回元素
			ObjectsToHandleMap.Emplace(FObjectKey(&NavRelevantObject), SharedElement->GetHandle());

			UE_LOG(LogNavigation, Verbose, TEXT("%hs [registered] (%s:%s) Bounds: [%s]"), __FUNCTION__,
				*GetNameSafe(NavRelevantObject.GetOuter()), *GetNameSafe(&NavRelevantObject),
				*NavRelevantInterface.GetNavigationBounds().ToString());

			return SharedElement;
		}

		return nullptr;
	}

	UE_LOG(LogNavigation, VeryVerbose, TEXT("%hs [skipped: not relevant] (%s:%s)"), __FUNCTION__,
		*GetNameSafe(NavRelevantObject.GetOuter()), *GetNameSafe(&NavRelevantObject));
	return nullptr;
}

void UNavigationObjectRepository::UnregisterNavRelevantObject(const INavRelevantInterface& NavRelevantObject)
{
	UnregisterNavRelevantObject(Cast<UObject>(&NavRelevantObject));
}

// 反注册：从 ObjectsToHandleMap 拿 Handle 再走 RemoveNavigationElement。
// 即使 UObject 已经析构走空指针也可以调用（FObjectKey 允许）。
void UNavigationObjectRepository::UnregisterNavRelevantObject(const UObject* NavRelevantObject)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%hs (%s:%s)"), __FUNCTION__,
		NavRelevantObject ? *GetNameSafe(NavRelevantObject->GetOuter()) : TEXT("null outer"),
		*GetNameSafe(NavRelevantObject));

	FNavigationElementHandle Handle;
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		ObjectsToHandleMap.RemoveAndCopyValue(FObjectKey(NavRelevantObject), Handle);
	}

	if (Handle)
	{
		RemoveNavigationElement(Handle);
	}
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::GetNavigationElementForHandle(const FNavigationElementHandle Handle) const
{
	return GetMutableNavigationElementForHandle(Handle);
}

TSharedPtr<FNavigationElement> UNavigationObjectRepository::GetMutableNavigationElementForHandle(const FNavigationElementHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	if (const TSharedPtr<FNavigationElement>* Element = NavRelevantElements.Find(Handle))
	{
		return *Element;
	}

	return nullptr;
}

FNavigationElementHandle UNavigationObjectRepository::GetNavigationElementHandleForUObject(const UObject* NavRelevantObject) const
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	if (const FNavigationElementHandle* Handle = ObjectsToHandleMap.Find(FObjectKey(Cast<UObject>(NavRelevantObject))))
	{
		return *Handle;
	}

	return FNavigationElementHandle::Invalid;
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::GetNavigationElementForUObject(const UObject* NavRelevantObject) const
{
	return GetMutableNavigationElementForUObject(NavRelevantObject);
}

TSharedPtr<FNavigationElement> UNavigationObjectRepository::GetMutableNavigationElementForUObject(const UObject* NavRelevantObject) const
{
	UE_MT_SCOPED_READ_ACCESS(NavElementAccessDetector);

	if (const FNavigationElementHandle* Handle = ObjectsToHandleMap.Find(FObjectKey(NavRelevantObject)))
	{
		if (const TSharedPtr<FNavigationElement>* Element = NavRelevantElements.Find(*Handle))
		{
			return *Element;
		}
	}

	return nullptr;
}

TSharedPtr<const FNavigationElement> UNavigationObjectRepository::UpdateNavigationElementForUObject(
	const INavRelevantInterface& NavRelevantInterface,
	const UObject& NavRelevantObject)
{
	// This method is called by the navigation system to make sure an up-to-date navigation element exists for a
	// given navigation relevant UObject.
	// In this case we only need to create, or update, the navigation element without sending
	// notification (i.e. ENotifyOnSuccess::No) since the caller (NavigationSystem) is already in the process of updating.
	// 由 NavigationSystem 自身调用：确保该 UObject 对应的 FNavigationElement 是最新的；
	// 带 ENotifyOnSuccess::No 避免触发 OnNavigationElementAddedDelegate → NavigationSystem 的回环，
	// 因为调用方自己已经在处理"更新"这件事。
	return RegisterNavRelevantObjectInternal(NavRelevantInterface, NavRelevantObject, ENotifyOnSuccess::No);
}

// 自定义链接注册：保存 weak 接口指针 + 广播给 NavigationSystem。
// 在 tile 生成时 NavigationSystem 会把这些链接转成 OffMeshConnection 喂给 Recast。
void UNavigationObjectRepository::RegisterCustomNavLinkObject(INavLinkCustomInterface& CustomNavLinkObject)
{
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);

#if DO_ENSURE // We don't want to execute the Find at all for targets where ensures are disabled
		if (!ensureMsgf(CustomLinkObjects.Find(&CustomNavLinkObject) == INDEX_NONE, TEXT("Same interface pointer can't be registered twice.")))
		{
			return;
		}
#endif

		CustomLinkObjects.Emplace(&CustomNavLinkObject);
	}

	OnCustomNavLinkObjectRegistered.ExecuteIfBound(CustomNavLinkObject);
}

void UNavigationObjectRepository::UnregisterCustomNavLinkObject(INavLinkCustomInterface& CustomNavLinkObject)
{
	{
		UE_MT_SCOPED_WRITE_ACCESS(NavElementAccessDetector);
		ensureMsgf(CustomLinkObjects.Remove(&CustomNavLinkObject) > 0, TEXT("Interface can't be removed since it was not registered or already unregistered)"));
	}

	OnCustomNavLinkObjectUnregistered.ExecuteIfBound(CustomNavLinkObject);
}
