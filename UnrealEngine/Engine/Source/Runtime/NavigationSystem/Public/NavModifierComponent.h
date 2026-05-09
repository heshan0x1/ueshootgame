// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavModifierComponent.h
// -----------------------------------------------------------------------------
// "组件版 Modifier"：附在 Actor 身上、沿用所在 Actor 的碰撞几何来"盖一层 NavArea"。
// 与 ANavModifierVolume 的区别：Volume 用 AVolume 自己的 Brush；本组件则扫描
// Actor 上所有 Primitive 的 BodySetup，把球/盒/胶囊/凸包统统变成 FRotatedBox 缓存到
// ComponentBounds，再整体作为 FAreaNavModifier 推给 Tile 生成（见 cpp 里 CalculateBounds）。
//
// 关键点：
//   - 若 Actor 没有任何 Primitive 可用，则使用 FailsafeExtent 形成一个 Box 作为兜底。
//   - 若根组件不参与导航（CanEverAffectNavigation=false），则通过 OnTransformUpdated
//     手动打 Refresh —— 否则默认机制不会触发导航 Octree 更新。
//   - bIncludeAgentHeight 会把下 Bounds 按 Agent 高度抬升（用于 "平台抬高一层"之类场景）。
//   - 编辑器下监听 UNavArea 类注册变化，以保持 AreaClass 引用合法。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "NavAreas/NavArea.h"
#include "NavRelevantComponent.h"
#include "NavModifierComponent.generated.h"

struct FNavigationRelevantData;
class UBodySetup;
enum class ENavigationDataResolution : uint8;

// 组件版 Nav Modifier —— 根据 Actor 碰撞几何生成 Area 覆盖。
UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent), hidecategories = (Activation), config = Engine, defaultconfig, MinimalAPI)
class UNavModifierComponent : public UNavRelevantComponent
{
	GENERATED_UCLASS_BODY()

	/** NavArea to apply inside the defined volume. */
	// 要施加的 NavArea。默认 UNavArea_Null（禁入）。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation)
	TSubclassOf<UNavArea> AreaClass;

	/** When setting this value, the modifier behavior changes : it will now replace any surface marked by AreaClassToReplace in the volume and replace it with AreaClass. */ 
	// 仅替换目标：只把原 Area == AreaClassToReplace 的表面换成 AreaClass
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation)
	TSubclassOf<UNavArea> AreaClassToReplace;

	/** box extent used ONLY when owning actor doesn't have collision component */
	// 兜底包围盒：Actor 没有任何可用 Primitive 时使用此 Extent 建一个 Box
	UPROPERTY(EditAnywhere, Category = Navigation)
	FVector FailsafeExtent;

	/** Experimental: Indicates which navmesh resolution should be used around the actor. */
	// 实验性：触及 Tile 强制使用指定分辨率
	UPROPERTY(EditAnywhere, Category = Navigation, AdvancedDisplay)
	ENavigationDataResolution NavMeshResolution;

	/** Setting to 'true' will result in expanding lower bounding box of the nav 
	 *	modifier by agent's height, before applying to navmesh */
	// 为 true 时在应用前把 Box 下沿按 Agent 高度向下扩展，避免抬升的平台顶上被标错区域
	UPROPERTY(config, EditAnywhere, Category = Navigation)
	uint8 bIncludeAgentHeight : 1;


	// Does the actual calculating and caching of the bounds when called by CalcAndCacheBounds
	// 真正的包围盒计算：遍历 Owner Primitive + BodySetup → 填充 ComponentBounds
	NAVIGATIONSYSTEM_API virtual void CalculateBounds() const;
	// @Note We might make this function non-virtual in the future in favor of child classes overriding CalculateBounds, see #jira UE-202451
	// 覆盖基类：同时绑定 RootComponent TransformUpdated，用于处理"根组件非导航相关"的路径
	NAVIGATIONSYSTEM_API virtual void CalcAndCacheBounds() const override;
	// 把 ComponentBounds 逐条转为 FAreaNavModifier 塞入 Data
	NAVIGATIONSYSTEM_API virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

	// 蓝图：运行时切 AreaClass，自动触发 Refresh
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API void SetAreaClass(TSubclassOf<UNavArea> NewAreaClass);
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API void SetAreaClassToReplace(TSubclassOf<UNavArea> NewAreaClassToReplace);

protected:
	// Root Transform 变化时的回调——将 bBoundsInitialized 置 false；若 Root 本身不参与导航还要手动 Refresh
	NAVIGATIONSYSTEM_API void OnTransformUpdated(USceneComponent* RootComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

#if WITH_EDITOR
	// 编辑器下 NavArea 类注册/反注册时的回调（保持 AreaClass 引用正确）
	NAVIGATIONSYSTEM_API void OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass);
	NAVIGATIONSYSTEM_API void OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass);
#endif // WITH_EDITOR 

	//~ Begin UActorComponent Interface
	NAVIGATIONSYSTEM_API virtual void OnRegister() override;
	NAVIGATIONSYSTEM_API virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	// 把一个 BodySetup 里的球/盒/胶囊/凸包全部转成 FRotatedBox 追加到 ComponentBounds
	NAVIGATIONSYSTEM_API void PopulateComponentBounds(FTransform InParentTransform, const UBodySetup& InBodySetup) const;
	
	// 旋转过的 Box —— 因为 NavMesh 的 FAreaNavModifier 需要 "Box + Rotation" 成对存放
	struct FRotatedBox
	{
		FBox Box;
		FQuat Quat;

		FRotatedBox() {}
		FRotatedBox(const FBox& InBox, const FQuat& InQuat) : Box(InBox), Quat(InQuat) {}
	};

	// 缓存：CalculateBounds 产物；mutable 因为通过 const getter 间接刷新
	mutable TArray<FRotatedBox> ComponentBounds;
	// Root Transform 监听的句柄（用于反绑定）
	mutable FDelegateHandle TransformUpdateHandle;
	/** cached in CalcAndCacheBounds and tested in GetNavigationData to see if
	 *	cached data is still valid */
	// 上次计算 Bounds 时的 Owner Transform；用于判断 ComponentBounds 是否还有效
	mutable FTransform CachedTransform;

#if WITH_EDITOR
	FDelegateHandle OnNavAreaRegisteredDelegateHandle;
	FDelegateHandle OnNavAreaUnregisteredDelegateHandle;
#endif // WITH_EDITOR 
};
