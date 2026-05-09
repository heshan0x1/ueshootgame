// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavModifierComponent.cpp
// -----------------------------------------------------------------------------
// UNavModifierComponent 实现。
// 核心流程（响应架构文档 6.? 节 "NavModifierComponent::CalcAndCacheBounds"）：
//
//   CalcAndCacheBounds (基类 INavRelevant 调用路径)
//     ├── 首次执行时绑 RootComponent->TransformUpdated
//     └── CalculateBounds:
//           遍历 Owner 的所有 UPrimitiveComponent（含 GeometryCollection）
//           → 从 BodySetup.AggGeom 的球/盒/胶囊/凸包里取出局部几何
//           → 变换到世界空间、写入 ComponentBounds (FRotatedBox 列表)
//           → 若完全为空则用 FailsafeExtent 兜底
//           → 最后把每个 Box 中心反变换回局部空间（给后续 FAreaNavModifier 用）
//
//   GetNavigationData
//     把 ComponentBounds 逐条包成 FAreaNavModifier（带 AreaClass + Replace + IncludeAgentHeight）
//     + 设置 NavMeshResolution。
//
// 线程：一律 GameThread。Octree 登记时会拿 NavigationSystem 的写锁。
// =============================================================================

#include "NavModifierComponent.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "AI/NavigationModifier.h"
#include "NavAreas/NavArea_Null.h"
#include "PhysicsEngine/BodySetup.h"
#include "NavigationSystem.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Engine/StaticMesh.h"
#include "VisualLogger/VisualLogger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavModifierComponent)

namespace UE::Navigation::ModComponent::Private
{
#if WITH_EDITOR
	// 只要注册/反注册的 Area 恰好是本组件引用的（AreaClass 或 AreaClassToReplace），
	// 就通知 NavigationSystem 更新本组件数据。
	void OnNavAreaRegistrationChanged(UNavModifierComponent& ModifierComponent, const UWorld& World, const UClass* NavAreaClass)
	{
		if (NavAreaClass && (NavAreaClass == ModifierComponent.AreaClass || NavAreaClass == ModifierComponent.AreaClassToReplace) && &World == ModifierComponent.GetWorld())
		{
			ModifierComponent.RefreshNavigationModifiers();
		}
	}
#endif // WITH_EDITOR

	// 单次转换：把一个局部 FBox + 旋转 + Area 信息封装成 FAreaNavModifier。
	FAreaNavModifier CreateAreaModifier(const FBox& Box, const FQuat& Quat, const TSubclassOf<UNavArea>& AreaClass, const TSubclassOf<UNavArea>& AreaClassToReplace, const bool bIncludeAgentHeight)
	{
		FAreaNavModifier AreaNavModifier(Box, FTransform(Quat), AreaClass);
		AreaNavModifier.SetIncludeAgentHeight(bIncludeAgentHeight);
		if (AreaClassToReplace)
		{
			AreaNavModifier.SetAreaClassToReplace(AreaClassToReplace);
		}
		return AreaNavModifier;
	}
} // UE::Navigation::ModComponent::Private

// 构造：默认 Null Area（禁入）、Failsafe 100 立方、IncludeAgentHeight=true、分辨率 Invalid。
UNavModifierComponent::UNavModifierComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	AreaClass = UNavArea_Null::StaticClass();
	AreaClassToReplace = nullptr;
	FailsafeExtent = FVector(100, 100, 100);
	bIncludeAgentHeight = true;
	NavMeshResolution = ENavigationDataResolution::Invalid;
}

// 注册：编辑器下挂 NavArea 注册/反注册监听
void UNavModifierComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		OnNavAreaRegisteredDelegateHandle = UNavigationSystemBase::OnNavAreaRegisteredDelegate().AddUObject(this, &UNavModifierComponent::OnNavAreaRegistered);
		OnNavAreaUnregisteredDelegateHandle = UNavigationSystemBase::OnNavAreaUnregisteredDelegate().AddUObject(this, &UNavModifierComponent::OnNavAreaUnregistered);
	}
#endif // WITH_EDITOR 
}

// 反注册：反向解绑
void UNavModifierComponent::OnUnregister()
{
#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		UNavigationSystemBase::OnNavAreaRegisteredDelegate().Remove(OnNavAreaRegisteredDelegateHandle);
		UNavigationSystemBase::OnNavAreaUnregisteredDelegate().Remove(OnNavAreaUnregisteredDelegateHandle);
	}
#endif // WITH_EDITOR 

	Super::OnUnregister();
}

#if WITH_EDITOR
// This function is only called if GIsEditor == true for non default objects components that are registered.
void UNavModifierComponent::OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass)
{
	UE::Navigation::ModComponent::Private::OnNavAreaRegistrationChanged(*this, World, NavAreaClass);
}

// This function is only called if GIsEditor == true for non default objects components that are registered.
void UNavModifierComponent::OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass)
{
	UE::Navigation::ModComponent::Private::OnNavAreaRegistrationChanged(*this, World, NavAreaClass);
}
#endif // WITH_EDITOR 

// 把一个 BodySetup 内所有几何（球/盒/胶囊/凸包）转成 FRotatedBox 追加到 ComponentBounds。
// 处理要点：
//   - Scale 被从 ParentTransform 里剥离，单独应用到每个 Elem（AggGeom 与 Scale 的互动规则）
//   - Convex 因为本身是 Box 集合，直接用其 ElemBox + ParentTransform
void UNavModifierComponent::PopulateComponentBounds(FTransform InParentTransform, const UBodySetup& InBodySetup) const
{
	const FVector Scale3D = InParentTransform.GetScale3D();
	InParentTransform.RemoveScaling();
	
	// 球体：半径受 Scale3D 影响（按分量缩放得椭球 AABB）
	for (int32 SphereIdx = 0; SphereIdx < InBodySetup.AggGeom.SphereElems.Num(); SphereIdx++)
	{
		const FKSphereElem& ElemInfo = InBodySetup.AggGeom.SphereElems[SphereIdx];
		FTransform ElemTM = ElemInfo.GetTransform();
		ElemTM.ScaleTranslation(Scale3D);
		ElemTM *= InParentTransform;

		const FBox SphereBounds = FBox::BuildAABB(ElemTM.GetLocation(), ElemInfo.Radius * Scale3D);
		ComponentBounds.Add(FRotatedBox(SphereBounds, ElemTM.GetRotation()));
	}

	// 盒体：X/Y/Z 是全长，除以 2 得 Extent
	for (int32 BoxIdx = 0; BoxIdx < InBodySetup.AggGeom.BoxElems.Num(); BoxIdx++)
	{
		const FKBoxElem& ElemInfo = InBodySetup.AggGeom.BoxElems[BoxIdx];
		FTransform ElemTM = ElemInfo.GetTransform();
		ElemTM.ScaleTranslation(Scale3D);
		ElemTM *= InParentTransform;

		const FBox BoxBounds = FBox::BuildAABB(ElemTM.GetLocation(), FVector(ElemInfo.X, ElemInfo.Y, ElemInfo.Z) * Scale3D * 0.5f);
		ComponentBounds.Add(FRotatedBox(BoxBounds, ElemTM.GetRotation()));
	}

	// 胶囊体：简化为 Radius/Radius/Length 的 AABB
	for (int32 SphylIdx = 0; SphylIdx < InBodySetup.AggGeom.SphylElems.Num(); SphylIdx++)
	{
		const FKSphylElem& ElemInfo = InBodySetup.AggGeom.SphylElems[SphylIdx];
		FTransform ElemTM = ElemInfo.GetTransform();
		ElemTM.ScaleTranslation(Scale3D);
		ElemTM *= InParentTransform;

		const FBox SphylBounds = FBox::BuildAABB(ElemTM.GetLocation(), FVector(ElemInfo.Radius, ElemInfo.Radius, ElemInfo.Length) * Scale3D);
		ComponentBounds.Add(FRotatedBox(SphylBounds, ElemTM.GetRotation()));
	}

	// 凸包：使用 ElemBox（所有点的 AABB）+ ParentTransform 做空间变换
	for (int32 ConvexIdx = 0; ConvexIdx < InBodySetup.AggGeom.ConvexElems.Num(); ConvexIdx++)
	{
		const FKConvexElem& ElemInfo = InBodySetup.AggGeom.ConvexElems[ConvexIdx];
		FTransform ElemTM = ElemInfo.GetTransform();

		const FBox ConvexBounds = FBox::BuildAABB(InParentTransform.TransformPosition(ElemInfo.ElemBox.GetCenter() * Scale3D), ElemInfo.ElemBox.GetExtent() * Scale3D);
		ComponentBounds.Add(FRotatedBox(ConvexBounds, ElemTM.GetRotation() * InParentTransform.GetRotation()));
	}
}

// 主包围盒计算：收集 Owner 上所有可用 Primitive 的 BodySetup 几何，生成 FRotatedBox 列表。
// 特别处理：
//   - UGeometryCollectionComponent 没有直接 BodySetup，改为遍历 RestCollection->RootProxyData.ProxyMeshes
//   - 没有任何 Primitive 时使用 FailsafeExtent 兜底
// 末尾的循环：把每个 Box 的中心"反变换"回局部空间，因为 FAreaNavModifier 是"Box + Rotation"合成的，
//            中心必须处于旋转前坐标系。
void UNavModifierComponent::CalculateBounds() const
{
	const AActor* MyOwner = GetOwner();
	if (!MyOwner)
	{
		return;
	}

	Bounds = FBox(ForceInit);
    ComponentBounds.Reset();
    // 迭代目标：遍历每个 Primitive，若可参与导航则抽出其 BodySetup 几何
    for (UActorComponent* Component : MyOwner->GetComponents())
    {
    	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component);
    	// 筛选：必须已注册 + 开了碰撞 + 允许影响导航
    	if (PrimComp && PrimComp->IsRegistered() && PrimComp->IsCollisionEnabled() && PrimComp->CanEverAffectNavigation())
    	{
    		UBodySetup* BodySetup = PrimComp->GetBodySetup();
    		if (BodySetup)
    		{
    			Bounds += PrimComp->Bounds.GetBox();
    				
    			const FTransform& ParentTM = PrimComp->GetComponentTransform();
    			PopulateComponentBounds(ParentTM, *BodySetup);
    		}
    		else if (const UGeometryCollectionComponent* GeometryCollection = Cast<UGeometryCollectionComponent>(PrimComp))
    		{
    			// If it's a GC, use the bodySetups from the proxyMeshes.
    			// GeometryCollection 特殊路径：它没有 Component 级 BodySetup，
    			// 需要从 RestCollection 的 ProxyMeshes 拿每个 StaticMesh 的 BodySetup
    			if (const TObjectPtr<const UGeometryCollection> RestCollection = GeometryCollection->RestCollection)
    			{
    				Bounds += GeometryCollection->Bounds.GetBox();

					const FGeometryCollectionProxyMeshData& ProxyMeshData = RestCollection->RootProxyData;
					for (int32 MeshIdx = 0; MeshIdx < ProxyMeshData.ProxyMeshes.Num(); ++MeshIdx)
    				{
						const TObjectPtr<UStaticMesh>& ProxyMesh = ProxyMeshData.ProxyMeshes[MeshIdx];
    					if (ProxyMesh != nullptr)
    					{
    						const UBodySetup* Body = ProxyMesh->GetBodySetup();
    						if (Body)
    						{
    							const FTransform& ParentTM = PrimComp->GetComponentTransform();
								// ProxyMesh 相对 Collection 有自己的局部变换，先局部后世界
								const FTransform LocalMeshTransform(ProxyMeshData.GetMeshTransform(MeshIdx));
    							PopulateComponentBounds(LocalMeshTransform* ParentTM, *Body);
    						}
    					}
    				}
    			}
    		}	
    	}
    }

    // 兜底：Actor 完全没有可用 Primitive，用 FailsafeExtent 建一个 Box 避免 Modifier 为空
    if (ComponentBounds.Num() == 0)
    {
    	Bounds = FBox::BuildAABB(MyOwner->GetActorLocation(), FailsafeExtent);
    	ComponentBounds.Add(FRotatedBox(Bounds, MyOwner->GetActorQuat()));
    }

    // 最后一轮：把每个 Box 的中心反变换到其自身旋转的局部空间。
    // 之后 FAreaNavModifier 构造时会用 FTransform(Quat) 变换局部 Box，
    // 恰好"抵消"这一步反变换，还原到世界空间原位置。
    for (int32 Idx = 0; Idx < ComponentBounds.Num(); Idx++)
    {
    	const FVector BoxOrigin = ComponentBounds[Idx].Box.GetCenter();
    	const FVector BoxExtent = ComponentBounds[Idx].Box.GetExtent();

    	const FVector NavModBoxOrigin = FTransform(ComponentBounds[Idx].Quat).InverseTransformPosition(BoxOrigin);
    	ComponentBounds[Idx].Box = FBox::BuildAABB(NavModBoxOrigin, BoxExtent);
    }
}

// 基类 hook：缓存 Transform + 首次绑 TransformUpdated + 跑 CalculateBounds
// 绑定 TransformUpdated 的理由：当 Owner 根组件本身"不导航"时默认机制不会触发刷新，
// 我们需要手动在 OnTransformUpdated 里把 Bounds 打旧并 RefreshNavigationModifiers。
void UNavModifierComponent::CalcAndCacheBounds() const
{
	const AActor* MyOwner = GetOwner();
	if (MyOwner)
	{
		CachedTransform = MyOwner->GetActorTransform();
		
		if (TransformUpdateHandle.IsValid() == false && MyOwner->GetRootComponent())
		{
			// binding to get notifies when the root component moves. We need
			// this only when the rootcomp is nav-irrelevant (since the default 
			// mechanisms won't kick in) but we're binding without checking it since
			// this property can change without re-running CalcAndCacheBounds.
			// We're filtering for nav relevancy in OnTransformUpdated.
			TransformUpdateHandle = MyOwner->GetRootComponent()->TransformUpdated.AddUObject(const_cast<UNavModifierComponent*>(this), &UNavModifierComponent::OnTransformUpdated);
		}
	}

	CalculateBounds();

	// VeryVerbose VLog：把每个最终生成的 Modifier Box 画出来，便于调试。
	UE_SUPPRESS(LogNavigation, VeryVerbose,
	{
		TArray<FAreaNavModifier> Areas;
		for (int32 Idx = 0; Idx < ComponentBounds.Num(); Idx++)
		{
			Areas.Add(UE::Navigation::ModComponent::Private::CreateAreaModifier(ComponentBounds[Idx].Box, ComponentBounds[Idx].Quat, AreaClass, AreaClassToReplace, bIncludeAgentHeight));
		}

		for(const FAreaNavModifier& Modifier : Areas)
		{
			UE_VLOG_BOX(this, LogNavigation, VeryVerbose, Modifier.GetBounds(), FColor::Yellow, TEXT(""));	
		}
	});
}

// 供 NavigationOctree 查询时使用——把缓存的 ComponentBounds 转成 FAreaNavModifier 列表。
void UNavModifierComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	for (int32 Idx = 0; Idx < ComponentBounds.Num(); Idx++)
	{
		Data.Modifiers.Add(UE::Navigation::ModComponent::Private::CreateAreaModifier(ComponentBounds[Idx].Box, ComponentBounds[Idx].Quat, AreaClass, AreaClassToReplace, bIncludeAgentHeight));
	}

	Data.Modifiers.SetNavMeshResolution(NavMeshResolution);
}

// 蓝图 Setter：仅在真正变化时 Refresh，避免冗余重建
void UNavModifierComponent::SetAreaClass(TSubclassOf<UNavArea> NewAreaClass)
{
	if (AreaClass != NewAreaClass)
	{
		AreaClass = NewAreaClass;
		RefreshNavigationModifiers();
	}
}

void UNavModifierComponent::SetAreaClassToReplace(TSubclassOf<UNavArea> NewAreaClassToReplace)
{
	if (AreaClassToReplace != NewAreaClassToReplace)
	{
		AreaClassToReplace = NewAreaClassToReplace;
		RefreshNavigationModifiers();
	}
}

// Root 位姿改变：无条件标脏 Bounds；若 Root 本身不参与导航还要手动 Refresh。
void UNavModifierComponent::OnTransformUpdated(USceneComponent* RootComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	// force bounds recaching next time GetNavigationBounds gets called.
	bBoundsInitialized = false;

	// otherwise the update will be handled by the default path
	// Root 可参与导航时，Octree 自己会收到 Primitive 通知，这里不需要重复触发
	if (RootComponent && RootComponent->CanEverAffectNavigation() == false)
	{
		// since the parent is not nav-relevant we need to manually tell nav sys to update
		RefreshNavigationModifiers();
	}
}

