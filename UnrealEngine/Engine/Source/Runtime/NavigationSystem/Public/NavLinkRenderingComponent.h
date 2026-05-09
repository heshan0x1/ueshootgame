// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkRenderingComponent.h
// -----------------------------------------------------------------------------
// 专供 SmartLink (UNavLinkCustomComponent) 使用的"可视化 Primitive 组件"。
// 没有导航数据贡献，仅用于在编辑器视口里绘制端点/箭头；
// 其 CreateSceneProxy 内部构造 FNavLinkRenderingProxy。
// 与 UNavLinkComponent 区别：
//   - UNavLinkComponent 自己就是 Primitive，渲染 + 导航一体；
//   - UNavLinkCustomComponent 不是 Primitive，所以额外挂一个本组件做可视化。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Components/PrimitiveComponent.h"
#include "NavLinkRenderingComponent.generated.h"

class FPrimitiveSceneProxy;
struct FConvexVolume;
struct FEngineShowFlags;

// SmartLink 的渲染辅助组件。
UCLASS(hidecategories=Object, editinlinenew, MinimalAPI)
class UNavLinkRenderingComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()
		
	//~ Begin UPrimitiveComponent Interface
	// 创建 FNavLinkRenderingProxy：proxy 会向 Owner（UNavLinkCustomComponent）反查数据。
	NAVIGATIONSYSTEM_API virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	/** Should recreate proxy one very update */
	// 每次 Transform 变化都重建 Proxy（因为端点位置依赖 Owner 变换）。
	virtual bool ShouldRecreateProxyOnUpdateTransform() const override { return true; }
	// 编辑器视口聚焦时忽略本组件的 Bounds —— 否则聚焦到一个半无限大的调试 Bounds 很糟糕。
	virtual bool GetIgnoreBoundsForEditorFocus() const override { return true; }
#if WITH_EDITOR
	// 编辑器拖选/框选检测：本组件参与检测但不把鼠标命中判定纳入 BSP。
	NAVIGATIONSYSTEM_API virtual bool ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	NAVIGATIONSYSTEM_API virtual bool ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
#endif
	//~ End UPrimitiveComponent Interface

	//~ Begin USceneComponent Interface
	// Bounds 需覆盖两个端点 + snap 半径，确保剔除正确。
	NAVIGATIONSYSTEM_API virtual FBoxSphereBounds CalcBounds(const FTransform &LocalToWorld) const override;
	//~ End USceneComponent Interface
};

