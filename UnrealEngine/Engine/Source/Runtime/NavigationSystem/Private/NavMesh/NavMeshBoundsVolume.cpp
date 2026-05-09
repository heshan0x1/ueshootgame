// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// 文件概览：NavMeshBoundsVolume.cpp
// ----------------------------------------------------------------------------
// ANavMeshBoundsVolume 的实现：把 Volume 的创建/变更/销毁事件转发到
// UNavigationSystemV1 的三大接口：
//   - OnNavigationBoundsAdded     → 新增边界
//   - OnNavigationBoundsRemoved   → 移除边界
//   - OnNavigationBoundsUpdated   → 变换或 SupportedAgents 改变
// NavSys 会据此维护 RegisteredNavBounds 与 PendingNavBoundsUpdates；
// 真正的 Tile 生成/丢弃由 FRecastNavMeshGenerator 在下一次 Tick 触发。
// 只有 Authority 端才会通知，避免客户端重复生成导航。
// ============================================================================

#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "Engine/CollisionProfile.h"
#include "Components/BrushComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavMeshBoundsVolume)

#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Editor.h"
#endif // WITH_EDITOR

// 构造：关闭物理碰撞、强制 Static Mobility，使该 Volume 不影响 gameplay，仅提供一块 AABB。
ANavMeshBoundsVolume::ANavMeshBoundsVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// NavMeshBoundsVolume 只作为"导航范围"使用，不参与任何物理碰撞
	GetBrushComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	// 导航范围一经放置应保持静态，避免运行时变动导致 Tile 重建抖动
	GetBrushComponent()->Mobility = EComponentMobility::Static;

	BrushColor = FColor(200, 200, 200, 255);
	// 默认所有 Agent 都被本 Volume 覆盖
	SupportedAgents.MarkInitialized();

	bColored = true;

#if WITH_EDITORONLY_DATA
	// World Partition：NavMeshBoundsVolume 需要在任意 Cell 加载前被可见，禁用空间分流
	bIsSpatiallyLoaded = false;
#endif
}

#if WITH_EDITOR

// 属性编辑回调：当 Brush 形状 / SupportedAgents / Transform 改变时，
// 把 Volume 当作"边界已更新"推送到 NavSys 让其做增量重建。
void ANavMeshBoundsVolume::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (GIsEditor && NavSys)
	{
		const FName PropName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : FName();
		const FName MemberName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : FName();

		// 只对"真正影响边界或 Agent 集合"的属性响应，忽略纯外观属性
		if (PropName == GET_MEMBER_NAME_CHECKED(ABrush, BrushBuilder)
			|| MemberName == GET_MEMBER_NAME_CHECKED(ANavMeshBoundsVolume, SupportedAgents)
			|| MemberName == USceneComponent::GetRelativeLocationPropertyName()
			|| MemberName == USceneComponent::GetRelativeRotationPropertyName()
			|| MemberName == USceneComponent::GetRelativeScale3DPropertyName())
		{
			NavSys->OnNavigationBoundsUpdated(this);
		}
	}
}

// Undo/Redo 后重新同步边界到 NavSys
void ANavMeshBoundsVolume::PostEditUndo()
{
	Super::PostEditUndo();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (GIsEditor && NavSys)
	{
		NavSys->OnNavigationBoundsUpdated(this);
	}
}

// 引擎后初始化钩子：关闭编辑器放置时的偏移（bUsePlacementExtent），
// 保证拖拽 NavMeshBoundsVolume 到地面时能"嵌入"表面。
// 若悬浮在表面之上，Volume 的 AABB 可能不会覆盖地面碰撞，从而漏生成 NavMesh。
void ANavMeshBoundsVolume::OnPostEngineInit()
{
	if (GEditor)
	{
		const TArray<UActorFactory*>& ActorFactories = GEditor->ActorFactories;
		// 遍历所有 ActorFactory，找出生产 NavMeshBoundsVolume 的那些并关闭 PlacementExtent
		for (UActorFactory* Factory : ActorFactories)
		{
			// For ANavMeshBoundsVolume, do not use placement extent so that the volume embeds into the surface.
			// When flush with the surface, the collision might not be in the volume and the navmesh might not generate.
			const TSubclassOf<AActor> ActorClass = Factory->NewActorClass;
			if (ActorClass != nullptr && ActorClass->IsChildOf(ANavMeshBoundsVolume::StaticClass()))
			{
				Factory->bUsePlacementExtent = false;
			}
		}
	}
}

#endif // WITH_EDITOR

// Volume 注册完毕 → 通知 NavSys 新增一块边界。
// 仅在 Authority 端执行：客户端不负责生成 NavMesh（NavMesh 会通过复制/同步获取）。
void ANavMeshBoundsVolume::PostRegisterAllComponents() 
{
	Super::PostRegisterAllComponents();
	
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys && GetLocalRole() == ROLE_Authority)
	{
		NavSys->OnNavigationBoundsAdded(this);
	}
}

// Volume 反注册（关卡卸载 / 销毁）→ 通知 NavSys 移除边界，
// 相应 Tile 若不再被任何 Bounds 覆盖将被丢弃。
void ANavMeshBoundsVolume::PostUnregisterAllComponents() 
{
	Super::PostUnregisterAllComponents();
	
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys && GetLocalRole() == ROLE_Authority)
	{
		NavSys->OnNavigationBoundsRemoved(this);
	}
}

