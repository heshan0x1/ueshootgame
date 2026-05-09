// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavModifierVolume.cpp
// -----------------------------------------------------------------------------
// ANavModifierVolume 实现。运行原理：
//   1) 构造时禁用 BrushComponent 的 Overlap/碰撞 —— 体积仅用作"导航标记区"。
//   2) GetNavigationData：把 Brush 的几何（简单情况下把 Brush AABB）封装为
//      FAreaNavModifier 写入 Data.Modifiers，Tile 生成阶段会据此给多边形标 Area。
//   3) RebuildNavigationData / SetAreaClass 等改动 → FNavigationSystem::UpdateActorData
//      → Octree 更新 → Dirty Area → 相关 Tile 重建。
//   4) 编辑器下监听 UNavArea 的 register/unregister，以保持 AreaClass 引用正确。
// =============================================================================

#include "NavModifierVolume.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"
#include "AI/NavigationModifier.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationOctree.h"
#include "Components/BrushComponent.h"
#include "AI/NavigationSystemHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Model.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavModifierVolume)

#if WITH_EDITOR
namespace UE::Navigation::ModVolume::Private
{
	// NavArea 类注册变更时的通用处理：若变化涉及本体积使用的 Area，则 UpdateActorData。
	// 触发条件：本 Actor 全部组件已完成初次注册；只有真正 live 的 Actor 才更新。
	void OnNavAreaRegistrationChanged(ANavModifierVolume& ModifierVolume, const UWorld& World, const UClass* NavAreaClass)
	{
		if (NavAreaClass
			&& (NavAreaClass == ModifierVolume.GetAreaClass() || NavAreaClass == ModifierVolume.GetAreaClassToReplace())
			&& &World == ModifierVolume.GetWorld()
			&& ModifierVolume.HasActorRegisteredAllComponents()) // Update only required after initial registration was completed
		{
			FNavigationSystem::UpdateActorData(ModifierVolume);
		}
	}
} // UE::Navigation::ModVolume::Private
#endif // WITH_EDITOR

//----------------------------------------------------------------------//
// ANavModifierVolume
//----------------------------------------------------------------------//
// 构造：默认 AreaClass=Null（禁入区），Brush 关闭 Overlap + 碰撞。
ANavModifierVolume::ANavModifierVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AreaClass(UNavArea_Null::StaticClass())
	, AreaClassToReplace(nullptr)
	, NavMeshResolution(ENavigationDataResolution::Invalid)
{
	if (GetBrushComponent())
	{
		GetBrushComponent()->SetGenerateOverlapEvents(false);
		GetBrushComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	}
}

// 初始化属性完毕后（编辑器下）挂 NavArea 注册/反注册监听
void ANavModifierVolume::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		OnNavAreaRegisteredDelegateHandle = UNavigationSystemBase::OnNavAreaRegisteredDelegate().AddUObject(this, &ANavModifierVolume::OnNavAreaRegistered);
		OnNavAreaUnregisteredDelegateHandle = UNavigationSystemBase::OnNavAreaUnregisteredDelegate().AddUObject(this, &ANavModifierVolume::OnNavAreaUnregistered);
	}
#endif // WITH_EDITOR
}

// 销毁前反注册监听
void ANavModifierVolume::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		UNavigationSystemBase::OnNavAreaRegisteredDelegate().Remove(OnNavAreaRegisteredDelegateHandle);
		UNavigationSystemBase::OnNavAreaUnregisteredDelegate().Remove(OnNavAreaUnregisteredDelegateHandle);
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR

// 编辑器下：组件注册完成后绑定 TransformUpdated，用户拖动 Actor 即刻刷新 NavData。
void ANavModifierVolume::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (RootComponent)
	{
		RootComponent->TransformUpdated.AddLambda([this](USceneComponent*, EUpdateTransformFlags, ETeleportType)
			{
				FNavigationSystem::UpdateActorData(*this);
			});
	}
}

// 反注册时清理 Transform 回调，避免悬空 lambda
void ANavModifierVolume::PostUnregisterAllComponents()
{
	if (RootComponent)
	{
		RootComponent->TransformUpdated.RemoveAll(this);
	}

	Super::PostUnregisterAllComponents();
}

// This function is only called if GIsEditor == true
// NavArea 类被注册（例如插件加载）——若影响本体积则 UpdateActorData
void ANavModifierVolume::OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass)
{
	UE::Navigation::ModVolume::Private::OnNavAreaRegistrationChanged(*this, World, NavAreaClass);
}

// This function is only called if GIsEditor == true
// NavArea 类被反注册——同样检查是否影响本体积
void ANavModifierVolume::OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass)
{
	UE::Navigation::ModVolume::Private::OnNavAreaRegistrationChanged(*this, World, NavAreaClass);
}
#endif // WITH_EDITOR

// 把 Brush 几何 + AreaClass 塞入 FNavigationRelevantData。
// Tile 生成时 Recast 会按 Modifier 应用"Area 标记"到多边形上。
void ANavModifierVolume::GetNavigationData(FNavigationRelevantData& Data) const
{
	if (Brush && AreaClass)
	{
		// No need to create modifiers if the AreaClass we want to set is the default one unless we want to replace a NavArea to default.
		// 优化：若仅是把面标为 Default Walkable，且不是替换模式，则什么也不用做——默认就是 Default Walkable。
		if (AreaClass != FNavigationSystem::GetDefaultWalkableArea() || AreaClassToReplace)
		{
			Data.Modifiers.CreateAreaModifiers(GetBrushComponent(), AreaClass, AreaClassToReplace);
		}
	}

	if (GetBrushComponent()->Brush != nullptr)
	{
		// bMaskFillCollisionUnderneathForNavmesh 启用时，额外生成一条 Box Modifier，
		// 这样 Recast 在"填充下方碰撞"阶段会跳过本体积的投影区域。
		if (bMaskFillCollisionUnderneathForNavmesh)
		{
			const FBox& Box = GetBrushComponent()->Brush->Bounds.GetBox();
			FAreaNavModifier AreaMod(Box, GetBrushComponent()->GetComponentTransform(), AreaClass);
			if (AreaClassToReplace)
			{
				AreaMod.SetAreaClassToReplace(AreaClassToReplace);
			}
			Data.Modifiers.SetMaskFillCollisionUnderneathForNavmesh(true);
			Data.Modifiers.Add(AreaMod);
		}

		// 指定分辨率时把信息传到 Modifier —— 影响生成阶段的 TileLayout 选择。
		if (NavMeshResolution != ENavigationDataResolution::Invalid)
		{
			Data.Modifiers.SetNavMeshResolution(NavMeshResolution);
		}
	}
}

// Bounds：非碰撞组件也纳入计算
FBox ANavModifierVolume::GetNavigationBounds() const
{
	return GetComponentsBoundingBox(/*bNonColliding=*/ true);
}

// 运行时切换 AreaClass：仅在变化时才 UpdateActorData，避免无谓重建
void ANavModifierVolume::SetAreaClass(TSubclassOf<UNavArea> NewAreaClass)
{
	if (NewAreaClass != AreaClass)
	{
		AreaClass = NewAreaClass;

		FNavigationSystem::UpdateActorData(*this);
	}
}

void ANavModifierVolume::SetAreaClassToReplace(TSubclassOf<UNavArea> NewAreaClassToReplace)
{
	if (NewAreaClassToReplace != AreaClassToReplace)
	{
		AreaClassToReplace = NewAreaClassToReplace;

		FNavigationSystem::UpdateActorData(*this);
	}
}

// 外部通用 Rebuild 入口：直接重新推到 NavSystem
void ANavModifierVolume::RebuildNavigationData()
{
	FNavigationSystem::UpdateActorData(*this);
}

#if WITH_EDITOR

// Undo/Redo：可能恢复了新 Brush 数据，重建简单碰撞并同步 NavData
void ANavModifierVolume::PostEditUndo()
{
	Super::PostEditUndo();

	if (GetBrushComponent())
	{
		GetBrushComponent()->BuildSimpleBrushCollision();
	}
	FNavigationSystem::UpdateActorData(*this);
}

// 编辑面板属性改动：只有当改了 Area / Brush 时才刷新 NavData
void ANavModifierVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_AreaClass = GET_MEMBER_NAME_CHECKED(ANavModifierVolume, AreaClass);
	static const FName NAME_AreaClassToReplace = GET_MEMBER_NAME_CHECKED(ANavModifierVolume, AreaClassToReplace);
	static const FName NAME_BrushComponent = TEXT("BrushComponent");

	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropName == NAME_AreaClass || PropName == NAME_AreaClassToReplace)
	{
		FNavigationSystem::UpdateActorData(*this);
	}
	else if (PropName == NAME_BrushComponent)
	{
		// Brush 变更：若新 BodySetup 有导航意义则 Update，否则 Unregister（去掉旧条目）
		if (GetBrushComponent())
		{
			if (GetBrushComponent()->GetBodySetup() && NavigationHelper::IsBodyNavigationRelevant(*GetBrushComponent()->GetBodySetup()))
			{
				FNavigationSystem::UpdateActorData(*this);
			}
			else
			{
				FNavigationSystem::OnActorUnregistered(*this);
			}
		}
	}
}

#endif

