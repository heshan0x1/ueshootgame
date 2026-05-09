// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkRenderingProxy.h
// -----------------------------------------------------------------------------
// 导航链接的调试渲染代理（FPrimitiveSceneProxy 派生）。
// 由 UNavLinkComponent / UNavLinkRenderingComponent 创建，
// 用于在编辑器里把 NavLink 画成箭头 / 圆盘 / 段落 / snap 范围等。
// 数据流：
//   Component::CreateSceneProxy → new FNavLinkRenderingProxy →
//   StorePointLinks / StoreSegmentLinks 把 Link 数据转成绘制用的 FNavLinkDrawing →
//   GetDynamicMeshElements 里调静态 DrawLinks / GetLinkMeshes 出图。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "NavAreas/NavArea.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "AI/Navigation/NavLinkDefinition.h"

class FMaterialRenderProxy;
class FMeshElementCollector;
class FPrimitiveDrawInterface;
class UPrimitiveComponent;

// Link 调试绘制代理：只在编辑器/开发时使用。
class FNavLinkRenderingProxy : public FPrimitiveSceneProxy
{
private:
	// 拥有此 Proxy 的 Actor（用于选中判断等）
	AActor* LinkOwnerActor;
	// 拥有的 Host 接口（用于拉取 Links / Segments）
	class INavLinkHostInterface* LinkOwnerHost;

public:
	NAVIGATIONSYSTEM_API SIZE_T GetTypeHash() const override;

	// 点对点链接的绘制数据：端点已变换到世界空间 + 展示用的颜色 / snap 参数。
	struct FNavLinkDrawing
	{
		FNavLinkDrawing() {}
		FNavLinkDrawing(const FTransform& InLocalToWorld, const FNavigationLink& Link)
			: Left(InLocalToWorld.TransformPosition(Link.Left))
			, Right(InLocalToWorld.TransformPosition(Link.Right))
			, Direction(Link.Direction)
			, Color(UNavArea::GetColor(Link.GetAreaClass()))
			, SnapRadius(Link.SnapRadius)
			, SnapHeight(Link.bUseSnapHeight ? Link.SnapHeight : -1.0f)
			, SupportedAgentsBits(Link.SupportedAgents.PackedBits)
		{}

		FVector Left;             // 世界空间左端点
		FVector Right;            // 世界空间右端点
		ENavLinkDirection::Type Direction; // 方向（双向 / 左到右 / 右到左）
		FColor Color;             // 取自 AreaClass 的颜色
		float SnapRadius;         // 端点吸附半径
		float SnapHeight;         // 高度限制（<0 表示未使用）
		uint32 SupportedAgentsBits; // 允许该链接的代理位掩码
	};
	// 段到段链接的绘制数据（类似上面，但是两端各是一条线段）。
	struct FNavLinkSegmentDrawing
	{
		FNavLinkSegmentDrawing() {}
		FNavLinkSegmentDrawing(const FTransform& InLocalToWorld, const FNavigationSegmentLink& Link)
			: LeftStart(InLocalToWorld.TransformPosition(Link.LeftStart))
			, LeftEnd(InLocalToWorld.TransformPosition(Link.LeftEnd))
			, RightStart(InLocalToWorld.TransformPosition(Link.RightStart))
			, RightEnd(InLocalToWorld.TransformPosition(Link.RightEnd))
			, Direction(Link.Direction)
			, Color(UNavArea::GetColor(Link.GetAreaClass()))
			, SnapRadius(Link.SnapRadius)
			, SnapHeight(Link.bUseSnapHeight ? Link.SnapHeight : -1.0f)
			, SupportedAgentsBits(Link.SupportedAgents.PackedBits)
		{}

		FVector LeftStart, LeftEnd;    // 左侧线段
		FVector RightStart, RightEnd;  // 右侧线段
		ENavLinkDirection::Type Direction;
		FColor Color;
		float SnapRadius;
		float SnapHeight;
		uint32 SupportedAgentsBits;
	};

private:
	// 渲染线程使用的 Link 数据快照（主线程在 Store* 中填入）
	TArray<FNavLinkDrawing> OffMeshPointLinks;
	TArray<FNavLinkSegmentDrawing> OffMeshSegmentLinks;

public:
	/** Initialization constructor. */
	// 构造：从 Component 推出 Owner/Host，但此时还没数据；需要调用方后续 Store*。
	NAVIGATIONSYSTEM_API FNavLinkRenderingProxy(const UPrimitiveComponent* InComponent);
	// 渲染主入口：收集可见 View，把 Link 数据转为 Mesh/Line 调试绘制指令。
	NAVIGATIONSYSTEM_API virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	NAVIGATIONSYSTEM_API virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	NAVIGATIONSYSTEM_API virtual uint32 GetMemoryFootprint( void ) const override;
	NAVIGATIONSYSTEM_API uint32 GetAllocatedSize( void ) const;
	// 把 FNavigationLink 数组转换并存储为 FNavLinkDrawing，供渲染线程使用。
	NAVIGATIONSYSTEM_API void StorePointLinks(const FTransform& LocalToWorld, const TArray<FNavigationLink>& LinksArray);
	// 同上，段链接版本。
	NAVIGATIONSYSTEM_API void StoreSegmentLinks(const FTransform& LocalToWorld, const TArray<FNavigationSegmentLink>& LinksArray);

	// 生成调试 Mesh（3D 圆盘 / 箭头 / 跳跃抛物线等），填入 Collector。
	static NAVIGATIONSYSTEM_API void GetLinkMeshes(const TArray<FNavLinkDrawing>& OffMeshPointLinks, const TArray<FNavLinkSegmentDrawing>& OffMeshSegmentLinks, TArray<float>& StepHeights, FMaterialRenderProxy* const MeshColorInstance, int32 ViewIndex, FMeshElementCollector& Collector, uint32 AgentMask);

	/** made static to allow consistent navlinks drawing even if something is drawing links without FNavLinkRenderingProxy */
	// 绘制线段箭头 / 端点圆盘。做成静态是为了让 NavMeshRenderingComponent 之类的也能复用绘制样式。
	static NAVIGATIONSYSTEM_API void DrawLinks(FPrimitiveDrawInterface* PDI, TArray<FNavLinkDrawing>& OffMeshPointLinks, TArray<FNavLinkSegmentDrawing>& OffMeshSegmentLinks, TArray<float>& StepHeights, FMaterialRenderProxy* const MeshColorInstance, uint32 AgentMask);
};
