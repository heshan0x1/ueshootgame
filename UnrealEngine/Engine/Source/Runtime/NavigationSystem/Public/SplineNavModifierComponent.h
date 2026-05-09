// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// SplineNavModifierComponent.h
// -----------------------------------------------------------------------------
// 沿 Spline 采样的 NavModifier。
// 数据流：
//   1) 用户在 Actor 上配一个 SplineComponent 并通过 AttachedSpline 引用它。
//   2) CalculateBounds：把 Spline AABB 按 StrokeWidth/Height 膨胀得到 Bounds。
//   3) GetNavigationData：把 Spline 切成若干段 → 每段用一个矩形截面 "扫出"一个
//      凸体（棱柱 tube）→ 转成 FAreaNavModifier 送入 Tile 生成。
//   4) SubdivisionLOD 越高，曲率大的段切得越细，越贴合曲线。
// 注意：Tick 只在编辑器世界开启（非游戏），每帧检查 Spline version/transform 变化。
// =============================================================================
#pragma once

#include "NavModifierComponent.h"

#include "SplineNavModifierComponent.generated.h"

// 用于标记"Spline 尚未被赋值"的哨兵值
#define INVALID_SPLINE_VERSION MIN_int32

struct FNavigationRelevantData;

// Spline 采样分辨率档位。越高越贴合但越耗。
UENUM()
enum class ESubdivisionLOD
{
	Low,      // 阈值 500（粗糙）
	Medium,   // 阈值 250
	High,     // 阈值 100
	Ultra,    // 阈值 10（非常精细）
};

/**
 *	Used to assign a chosen NavArea to the nav mesh in the vicinity of a chosen spline.
 *	A tube is constructed around the spline and intersected with the nav mesh. Set its dimensions with StrokeWidth and StrokeHeight.
 */
// Spline 走向的 NavArea 标记组件。典型用途：沿公路中线刷一条"路面"Area，
// 或沿管道刷一条"禁入"区域。
UCLASS(Blueprintable, MinimalAPI, Meta = (BlueprintSpawnableComponent), hidecategories = (Variable, Tags, Cooking, Collision))
class USplineNavModifierComponent : public UNavModifierComponent
{
	GENERATED_BODY()

	/**
	 * If true, any changes to Spline Components on this actor will cause this component to update the nav mesh.
	 * This will be slow if the spline has many points, or the nav mesh is sufficiently large.
	 */ 
	// 编辑器下：Spline 改动后是否立即更新 NavData。开销大时可关掉，改用手动 UpdateNavigationData 按钮。
	UPROPERTY(EditAnywhere, Category = Navigation)
	bool bUpdateNavDataOnSplineChange = true;

	/** The SplineComponent which will modify the nav mesh; it must also be attached to this component's owner actor */ 
	// 被引用的 Spline 组件（必须和本 Actor 在同一 Actor 下）
	UPROPERTY(EditAnywhere, Category = Navigation, Meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SplineComponent", DisplayName = "Nav Modifier Spline"))
	FComponentReference AttachedSpline;

	/** Cross-sectional width of the tube enclosing the spline */
	// 覆盖宽度（Y 方向）——沿 Spline 两侧各一半
	UPROPERTY(EditAnywhere, Category = Navigation, Meta=(UIMin="10", ClampMin="10"))
	double StrokeWidth = 500.0f;

	/** Cross-sectional height of the tube enclosing the spline */
	// 覆盖高度（Z 方向）——沿 Spline 上下各一半
	UPROPERTY(EditAnywhere, Category = Navigation, Meta=(UIMin="10", ClampMin="10"))
	double StrokeHeight = 500.0f;

	/** Higher LOD will capture finer details in the spline */
	// 曲线采样精度：Ultra/High/Medium/Low 对应不同曲率阈值
	UPROPERTY(EditAnywhere, Category = Navigation)
	ESubdivisionLOD SubdivisionLOD = ESubdivisionLOD::Medium;

	USplineNavModifierComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * Recalculates bounds, then re-computes the NavModifierVolumes and re-marks the nav mesh.
	 * Disable UpdateNavDataOnSplineChange and use this to manually update when either the spline or nav mesh is too large to handle rapid updates.
	 *
	 * Does nothing in non-editor builds
	 */
	// 编辑器按钮：手动更新 NavData。当 Spline 很复杂/NavMesh 很大时推荐配合 bUpdateNavDataOnSplineChange=false 使用。
	UFUNCTION(CallInEditor, Category = Navigation, Meta=(DisplayName="UpdateNavigationData"))
	void UpdateNavigationWithComponentData();

protected:
#if WITH_EDITORONLY_DATA
	// 编辑器 Tick：监测 Spline version / transform 是否变化，按需刷新 NavData
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// 仅在非 Game World 下 Tick；Runtime 不跑这段
	virtual bool IsComponentTickEnabled() const override;
#endif // WITH_EDITORONLY_DATA

	// 计算 Bounds：以 Spline AABB 为基，按 StrokeWidth/Height 的一半外扩
	virtual void CalculateBounds() const override;
	// 把沿 Spline 的一系列棱柱作为 FAreaNavModifier 写入 Data
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

	// 返回 CubicBezier::Tessellate 使用的曲率阈值（LOD → 数值 的映射）
	float GetSubdivisionThreshold() const;

private:
#if WITH_EDITORONLY_DATA
	// Used to check against attached spline's version each tick for changes
	// 上次 Tick 时看到的 Spline 版本号，用于变化检测
	uint32 SplineVersion = INVALID_SPLINE_VERSION;
#endif // WITH_EDITORONLY_DATA

	// Used for bounds calculation and to check against attached spline's transform each tick for changes
	// 缓存的 Spline 变换，用于 Bounds 计算 + 变化检测
	FTransform SplineTransform = FTransform();
};
