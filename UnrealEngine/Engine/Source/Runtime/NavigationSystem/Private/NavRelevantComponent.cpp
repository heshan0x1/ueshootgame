// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavRelevantComponent.cpp
// -----------------------------------------------------------------------------
// UNavRelevantComponent 基类实现。
// 关键交互：
//   OnRegister → 可选缓存 Owner 根组件作为"导航父"（多 Modifier 聚合到一项 Octree 里）
//                → FNavigationSystem::OnComponentRegistered(*this) 走到
//                  UNavigationSystemV1::OnComponentRegistered → FNavigationDataHandler
//                  → NavOctree PendingUpdates
//   OnUnregister → FNavigationSystem::OnComponentUnregistered(*this)
//   RefreshNavigationModifiers → FNavigationSystem::UpdateComponentData
//
// 注意 Bounds 是"懒计算" + "首次 Owner 完全注册后才锁定"；
// GetNavigationParent 必须在缓存初始化完成后调，否则登记位置会错。
// =============================================================================

#include "NavRelevantComponent.h"
#include "NavigationSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavRelevantComponent)

// 构造：默认作为"附加到 Owner 根组件"的 Modifier；Bounds/Parent 均未初始化。
UNavRelevantComponent::UNavRelevantComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
, bAttachToOwnersRoot(true)
, bBoundsInitialized(false)
, bNavParentCacheInitialized(false)
{
	bCanEverAffectNavigation = true;
	bNavigationRelevant = true;
}

// OnRegister：解析"导航父"（root comp 或 owner actor）并登记到 NavigationSystem。
// 调用者：Actor 组件注册流程（引擎自动）。
// 副作用：会把自己加入 NavOctree 的 PendingUpdates。
void UNavRelevantComponent::OnRegister()
{
	Super::OnRegister();

	if (bAttachToOwnersRoot)
	{
		bool bUpdateCachedParent = true;
#if WITH_EDITOR
		// 编辑器场景下若 NavigationSystem 正被锁住（例如批量加载期间），
		// 跳过缓存刷新，避免读到不稳定状态。
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys && NavSys->IsNavigationRegisterLocked())
		{
			bUpdateCachedParent = false;
		}
#endif

		AActor* OwnerActor = GetOwner();
		if (OwnerActor && bUpdateCachedParent)
		{
			// attach to root component if it's relevant for navigation
			// 优先挂到 Owner 的根组件——若根组件本身参与导航，则把自己当作它的附加数据
			UActorComponent* ActorComp = OwnerActor->GetRootComponent();			
			INavRelevantInterface* NavInterface = ActorComp ? Cast<INavRelevantInterface>(ActorComp) : nullptr;
			if (NavInterface && NavInterface->IsNavigationRelevant() &&
				OwnerActor->IsComponentRelevantForNavigation(ActorComp))
			{
				CachedNavParent = ActorComp;
			}

			// otherwise try actor itself under the same condition
			// 若根组件不参与导航，则看 Owner Actor 本身是否实现 INavRelevantInterface
			if (CachedNavParent == nullptr)
			{
				NavInterface = Cast<INavRelevantInterface>(OwnerActor);
				if (NavInterface && NavInterface->IsNavigationRelevant())
				{
					CachedNavParent = OwnerActor;
				}
			}
		}
	}

	// Mark cache as initialized (even if null) from this point so calls to GetNavigationParent can be validated.
	// 无论有没有找到 Parent，都标记缓存已初始化。后续 GetNavigationParent 就不会报警。
	bNavParentCacheInitialized = true;

	// 真正把自己递交给 NavigationSystem，走 Octree 登记流程
	FNavigationSystem::OnComponentRegistered(*this);
}

// 反注册：从 NavigationSystem 中移除，对应 Octree 删除
void UNavRelevantComponent::OnUnregister()
{
	Super::OnUnregister();

	FNavigationSystem::OnComponentUnregistered(*this);
}

// INavRelevantInterface::GetNavigationBounds —— 惰性计算 + 初始化后锁定
FBox UNavRelevantComponent::GetNavigationBounds() const
{
	if (!bBoundsInitialized)
	{
		CalcAndCacheBounds();

		// Mark bounds as initialized after the actor has registered all its components
		// since some of them have dependencies.
		// (e.g., NavModifierComponent relies on registered primitive components)
		// 关键点：只有当 Owner 的全部组件都注册完毕后才标记 Bounds 已就绪
		// 否则某些依赖其他 Primitive 的 Modifier（如 NavModifierComponent）会拿到半截数据
		if (GetOwner() && GetOwner()->HasActorRegisteredAllComponents())
		{
			bBoundsInitialized = true;
		}
	}

	return Bounds;
}

bool UNavRelevantComponent::IsNavigationRelevant() const
{
	return bNavigationRelevant;
}

// 外部显式触发 Bounds 重新计算（例如 Area/几何参数变化）
void UNavRelevantComponent::UpdateNavigationBounds()
{
	CalcAndCacheBounds();
	bBoundsInitialized = true;
}

// 返回"导航父"；若缓存未初始化会打 Error，因为 Octree 会据此决定登记位置
UObject* UNavRelevantComponent::GetNavigationParent() const
{
	UE_CLOG(!bNavParentCacheInitialized, LogNavigation, Error, 
		TEXT("%s called before initialization of the navigation parent cache for [%s]. This might cause improper registration in the NavOctree and must be fixed."), 
		ANSI_TO_TCHAR(__FUNCTION__),
		*GetFullName());

	return CachedNavParent;
}

// 默认 Bounds 实现：Owner 位置为中心的 200×200×200 盒子。派生类应覆盖此函数。
void UNavRelevantComponent::CalcAndCacheBounds() const
{
	const AActor* OwnerActor = GetOwner();
	const FVector MyLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;

	Bounds = FBox::BuildAABB(MyLocation, FVector(100.0f, 100.0f, 100.0f));
}

// 设置"强制相关性"：true 时本组件独立登记不复用 Owner Parent。
void UNavRelevantComponent::ForceNavigationRelevancy(bool bForce)
{
	bAttachToOwnersRoot = !bForce;
	if (bForce)
	{
		bNavigationRelevant = true;
	}

	RefreshNavigationModifiers();
}

// 蓝图可调：切换参与导航状态
void UNavRelevantComponent::SetNavigationRelevancy(bool bRelevant)
{
	if (bNavigationRelevant != bRelevant)
	{
		bNavigationRelevant = bRelevant;
		RefreshNavigationModifiers();
	}
}

// 通知 NavigationSystem 重新拉取本组件数据；只有注册后才有意义。
void UNavRelevantComponent::RefreshNavigationModifiers()
{
	// Only update after component registration since some required informations are initialized at that time (i.e. Cached Navigation Parent)
	// 未注册之前 CachedNavParent 尚未计算，此时 Update 会登记到错误位置
	if (bRegistered)
	{
		FNavigationSystem::UpdateComponentData(*this);
	}
}

