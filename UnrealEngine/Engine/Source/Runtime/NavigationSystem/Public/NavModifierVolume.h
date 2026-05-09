// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavModifierVolume.h
// -----------------------------------------------------------------------------
// "体积版 Modifier"：派生 AVolume，把某个 UNavArea 应用到体积内部（或替换某个指定的 Area）。
// 使用者放一个 ANavModifierVolume 到关卡里，画出一块 AVolume；
// Tile 生成时会被 Octree 查询到，进而把 AreaClass 盖到体积重合的多边形上。
//
// 支持两种模式：
//   1) 单独指定 AreaClass：直接覆盖体积内所有面的 Area。
//   2) 同时指定 AreaClassToReplace：仅替换"原来就是 AreaClassToReplace"的面。
//
// 高级实验性字段：
//   - bMaskFillCollisionUnderneathForNavmesh：体积覆盖区域内跳过"填充下方碰撞"的处理。
//   - NavMeshResolution：把被体积覆盖到的 Tile 提升为更高分辨率重建。
// =============================================================================

#pragma once 

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavAreas/NavArea.h"
#include "GameFramework/Volume.h"
#include "NavModifierVolume.generated.h"

enum class ENavigationDataResolution : uint8;

struct FNavigationRelevantData;

/** 
 *	Allows applying selected AreaClass to navmesh, using Volume's shape
 */
// 体积 Modifier：将 AreaClass 应用到 AVolume 几何内部的 NavMesh 面上。
UCLASS(hidecategories=(Navigation), MinimalAPI)
class ANavModifierVolume : public AVolume, public INavRelevantInterface
{
	GENERATED_BODY()

protected:
	/** NavArea to apply inside the defined volume. */
	// 将被"应用"到体积内部的 NavArea 类（典型：UNavArea_Null 禁入 / UNavArea_Obstacle 高代价）。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Default)
	TSubclassOf<UNavArea> AreaClass;

	/** When setting this value, the modifier volume behavior changes : it will now replace any surface marked by AreaClassToReplace in the volume and replace it with AreaClass. */ 
	// 替换模式：若非空，则仅把体积内"原 Area == AreaClassToReplace"的表面替换为 AreaClass。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Default)
	TSubclassOf<UNavArea> AreaClassToReplace;

	/** Experimental: if set, the 2D space occupied by the volume box will ignore FillCollisionUnderneathForNavmesh */
	// 实验：体积在 2D 投影范围内跳过 "FillCollisionUnderneathForNavmesh" 处理（用于复杂悬空场景）。
	UPROPERTY(EditAnywhere, Category = Default, AdvancedDisplay)
	bool bMaskFillCollisionUnderneathForNavmesh;

	/** Experimental: When not set to None, the navmesh tiles touched by the navigation modifier volume will be built
	 * using the highest resolution found. */
	// 实验：指定分辨率时，体积触及的所有 Tile 强制以更高分辨率重建（仅与体素尺寸相关）。
	UPROPERTY(EditAnywhere, Category = Default, AdvancedDisplay)
	ENavigationDataResolution NavMeshResolution;

#if WITH_EDITOR
	// 监听 UNavArea 类的注册/反注册，以便编辑器下 hot-reload 时刷新本体积
	FDelegateHandle OnNavAreaRegisteredDelegateHandle;
	FDelegateHandle OnNavAreaUnregisteredDelegateHandle;
#endif

public:
	NAVIGATIONSYSTEM_API ANavModifierVolume(const FObjectInitializer& ObjectInitializer);

	// 运行时改 AreaClass，会触发 RebuildNavigationData 标脏对应 Tile。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API void SetAreaClass(TSubclassOf<UNavArea> NewAreaClass = {});
	// 运行时改被替换的目标 Area。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API void SetAreaClassToReplace(TSubclassOf<UNavArea> NewAreaClassToReplace = {});

	TSubclassOf<UNavArea> GetAreaClass() const { return AreaClass; }
	TSubclassOf<UNavArea> GetAreaClassToReplace() const { return AreaClassToReplace; }

	// 把 AreaClass / Brush 几何写入 FNavigationRelevantData，供 Recast 生成时 Area Marking 使用。
	NAVIGATIONSYSTEM_API virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	// 返回 Brush 的世界 AABB；用于 Octree 查询
	NAVIGATIONSYSTEM_API virtual FBox GetNavigationBounds() const override;
	// 通知 NavigationSystem：体积属性变了，相关 Tile 打脏并重建
	NAVIGATIONSYSTEM_API virtual void RebuildNavigationData() override;

#if WITH_EDITOR
	NAVIGATIONSYSTEM_API virtual void PostEditUndo() override;
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	NAVIGATIONSYSTEM_API virtual void PostInitProperties() override;
	NAVIGATIONSYSTEM_API virtual void BeginDestroy() override;

#if WITH_EDITOR
	NAVIGATIONSYSTEM_API virtual void PostRegisterAllComponents() override;
	NAVIGATIONSYSTEM_API virtual void PostUnregisterAllComponents() override;

	// NavArea 类注册/反注册回调：在编辑器下 Area 类热重载时保持 AreaClass 引用正确。
	NAVIGATIONSYSTEM_API void OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass);
	NAVIGATIONSYSTEM_API void OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass);
#endif
};
