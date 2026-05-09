// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkComponent.h
// -----------------------------------------------------------------------------
// 简单 NavLink 组件：把一组 FNavigationLink（点到点/段到段的偏离网格连接）
// 暴露给 NavigationSystem，让 Recast 生成时将其作为 OffMeshConnection 打进 Tile。
// 与 NavLinkCustomComponent 区别：
//   - 本组件只是"静态跳跃点"声明，不支持动态启用/禁用；
//   - 不实现 INavLinkCustomInterface，无法拦截 OnLinkMoveStarted 等回调。
// 如果需要"SmartLink（可开关/可拦截）"请使用 UNavLinkCustomComponent。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Components/PrimitiveComponent.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "NavLinkHostInterface.h"
#include "NavLinkComponent.generated.h"

class FPrimitiveSceneProxy;
struct FNavigationRelevantData;

// 简单链接组件：PrimitiveComponent 派生，既负责导航数据贡献，也负责编辑器可视化（SceneProxy）。
UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent), hidecategories = (Activation), MinimalAPI)
class UNavLinkComponent : public UPrimitiveComponent, public INavLinkHostInterface
{
	GENERATED_UCLASS_BODY()

	// 挂在该组件上的一组链接定义（每条带 Left/Right 端点 + Direction + AreaClass 等）。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation)
	TArray<FNavigationLink> Links;

	// 将 Links 写入 Data.OffMeshLinks，供 Tile 生成添加 OffMeshConnection
	NAVIGATIONSYSTEM_API virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	// 至少存在一条启用的链接才算"相关"
	NAVIGATIONSYSTEM_API virtual bool IsNavigationRelevant() const override;

	// 本组件不通过 NavLinkDefinition 类提供链接，所以返回 false。
	virtual bool GetNavigationLinksClasses(TArray<TSubclassOf<class UNavLinkDefinition> >& OutClasses) const override { return false; }
	// 返回直接内嵌的 Links 数组（点链接）；段链接数组保持为空。
	NAVIGATIONSYSTEM_API virtual bool GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const override;

	// 计算组件可视化 Bounds（包含所有 link 端点的最小 AABB）。
	NAVIGATIONSYSTEM_API virtual FBoxSphereBounds CalcBounds(const FTransform &LocalToWorld) const override;
	// 创建 FNavLinkRenderingProxy 用于调试绘制箭头。
	NAVIGATIONSYSTEM_API virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	// 变换改变时重建 SceneProxy，确保调试几何更新。
	virtual bool ShouldRecreateProxyOnUpdateTransform() const override { return true; }

#if WITH_EDITOR
	// 编辑器修改 Links 属性时重新初始化 AreaClass（把 None 替换为默认 Area）
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	NAVIGATIONSYSTEM_API virtual void PostEditUndo() override;
	NAVIGATIONSYSTEM_API virtual void PostEditImport() override;
#endif // WITH_EDITOR

	// OnRegister：走一次 InitializeLinksAreaClasses 保证 AreaClass 非空
	NAVIGATIONSYSTEM_API virtual void OnRegister() override;

protected:
	// 把每条 Link 里尚未指定的 AreaClass 替换为默认 Area 类。
	NAVIGATIONSYSTEM_API void InitializeLinksAreaClasses();
};
