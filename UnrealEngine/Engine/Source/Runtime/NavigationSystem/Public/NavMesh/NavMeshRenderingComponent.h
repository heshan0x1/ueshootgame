// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NavMeshRenderingComponent.h —— 中文总览
// ---------------------------------------------------------------------------
// 本文件是 ARecastNavMesh 可视化（编辑器 + 运行时调试）的核心声明，负责把
// Recast 生成完成的 NavMesh Tile / Cluster / Link 等数据画在场景中。主要类：
//   - ENavMeshDetailFlags：21 种调试绘制选项的枚举（与 ARecastNavMesh 上的
//     bDraw* 属性一一对应，控制显示哪些元素）。
//   - FNavMeshSceneProxyData：数据收集容器（可被 GameplayDebugger 复用）。
//       收集自 ARecastNavMesh → FRecastDebugGeometry → Proxy 数据的中间层。
//       存有三角面、线条、链接、文字、Octree 框等所有可视化要素。
//   - FNavMeshSceneProxy：派生自 FDebugRenderSceneProxy，进入渲染线程的具体
//     GeometryProxy；持有静态 VB/IB、MeshBatch、材质代理。
//   - UNavMeshRenderingComponent：挂在 ARecastNavMesh Actor 上的 DebugDrawComponent。
//       OnRegister 时订阅 NavSystem 的更新，Tick 时触发 MarkRenderStateDirty。
//   - FNavMeshDebugDrawDelegateHelper：HUD 文本（Tile 标签、PolyLabels 等）绘制助手。
//
// 数据流（对应架构文档 4.3 节末端）：
//   ARecastNavMesh 调用 BeginBatchQuery → GetDebugGeometry → FRecastDebugGeometry
//     → FNavMeshSceneProxyData::GatherData → FNavMeshSceneProxy 构造（拷贝一次）
//     → 渲染线程 GetDynamicMeshElements 提交 MeshBatch。
//
// 线程约束：Proxy 在游戏线程构造时一次性拷贝全部数据，进入渲染线程后只读，
// 避免渲染线程触碰 UObject（参考架构文档 6.x 注释规约）。
// =====================================================================================

#pragma once

#include "Engine/EngineTypes.h"
#include "PrimitiveViewRelevance.h"
#include "MaterialShared.h"
#include "DynamicMeshBuilder.h"
#include "DebugRenderSceneProxy.h"
#include "Debug/DebugDrawComponent.h"
#include "MeshBatch.h"
#include "LocalVertexFactory.h"
#include "Math/GenericOctree.h"
#include "StaticMeshResources.h"
#include "NavigationSystemTypes.h"
#include "Templates/UnrealTemplate.h"
#include "NavMeshRenderingComponent.generated.h"

class APlayerController;
class ARecastNavMesh;
class FColoredMaterialRenderProxy;
class FMeshElementCollector;
class FPrimitiveDrawInterface;
class UCanvas;
class UNavMeshRenderingComponent;

// NavMesh 调试绘制的详细开关位
// ARecastNavMesh 会根据用户勾选的 bDraw* 属性把该 enum 合成一个 int32 位图，
// 再透传给 FNavMeshSceneProxyData::NavDetailFlags，Proxy 据此决定收集/绘制哪些内容
enum class ENavMeshDetailFlags : uint8
{
	TriangleEdges,         // NavMesh 内部 DetailMesh 的三角形边
	PolyEdges,             // PolyMesh 多边形边（Recast 合并后的)
	BoundaryEdges,         // NavMesh 边界（与 NullArea 的分界线）
	FilledPolys,           // 把多边形以半透明填充色画出来
	TileBounds,            // 每个 Tile 的 AABB
	PathCollidingGeometry, // 参与 NavMesh 构建的原始几何（可能性能沉重）
	TileLabels,            // 每个 Tile 的坐标/层编号文本
	PolygonLabels,         // 每个多边形的 ID/Ref 文本
	PolygonCost,           // 多边形的 Area 代价值
	PolygonFlags,          // 多边形 Flag 位（bit 文本）
	PolygonAreaIDs,        // Area 枚举 ID
	PathLabels,            // 路径走廊上的额外标签
	NavLinks,              // OffMeshConnection 连线
	FailedNavLinks,        // 生成失败或未连通的 NavLink
	Clusters,              // Cluster 分组（WITH_NAVMESH_CLUSTER_LINKS）
	NavOctree,             // 导航 Octree 节点包围盒
	NavOctreeDetails,      // Octree 每个 Element 的详细 Bounds
	MarkForbiddenPolys,    // 禁行（flag=Forbidden）的多边形特殊高亮
	TileBuildTimes,        // 各 Tile 的生成耗时文本
	TileBuildTimesHeatMap, // 用热力色显示耗时
	TileResolutions        // Tile 的分辨率等级（Low/Default/High）
};

// exported to API for GameplayDebugger module
// -------------------------------------------------------------------------------------
// FNavMeshSceneProxyData —— 调试数据的聚合容器（线程无关）
// GameplayDebugger 也可以直接 Serialize 这个结构通过网络传输，因此被导出。
// GatherData() 会从 ARecastNavMesh 中一次性采集各类几何、线条、文字，供 Proxy 拷贝。
// -------------------------------------------------------------------------------------
struct FNavMeshSceneProxyData : public TSharedFromThis<FNavMeshSceneProxyData, ESPMode::ThreadSafe>
{
	// 填充多边形使用的三角网格数据；每个 Cluster 一组，颜色随 Area 或 Cluster
	struct FDebugMeshData
	{
		TArray<FDynamicMeshVertex> Vertices; // 顶点（位置+颜色+法线）
		TArray<uint32> Indices;              // 三角形索引
		FColor ClusterColor;                 // 本组使用的颜色
	};
	TArray<FDebugMeshData> MeshBuilders; // 所有填充色多边形按 Cluster/Area 分组

	// 一个调试用的点 sprite
	struct FDebugPoint
	{
		FDebugPoint(const FVector& InPosition, const FColor& InColor, const float InSize) : Position(InPosition), Color(InColor), Size(InSize) {}
		FVector Position;
		FColor Color;
		float Size = 0.f;
	};

	TArray<FDebugRenderSceneProxy::FDebugLine> ThickLineItems;   // 粗线（主要用于高亮）
	TArray<FDebugRenderSceneProxy::FDebugLine> TileEdgeLines;    // Tile 之间的接缝（邻接边）
	TArray<FDebugRenderSceneProxy::FDebugLine> NavMeshEdgeLines; // NavMesh 整体外轮廓
	TArray<FDebugRenderSceneProxy::FDebugLine> NavLinkLines;     // OffMeshConnection 两端连线
	TArray<FDebugRenderSceneProxy::FDebugLine> ClusterLinkLines; // Cluster 之间的连线
	TArray<FDebugRenderSceneProxy::FDebugLine> AuxLines;         // 其他杂项线段
	TArray<FDebugPoint> AuxPoints;                               // 杂项点
	TArray<FDebugRenderSceneProxy::FDebugBox> AuxBoxes;          // 杂项盒（Octree/Tile）
	TArray<FDebugRenderSceneProxy::FMesh> Meshes;                // 低级别 Mesh（Recast 中间产物可视化）

	// 需要贴到屏幕上的文字（PolyId / Cost / Tile Coord 等）
	struct FDebugText
	{
		FVector Location;   // 世界坐标
		FString Text;

		FDebugText() {}
		FDebugText(const FVector& InLocation, const FString& InText) : Location(InLocation), Text(InText) {}
		FDebugText(const FString& InText) : Location(FNavigationSystem::InvalidLocation), Text(InText) {}
	};
	TArray<FDebugText> DebugLabels; // 被 DrawDebugLabels 使用

	TArray<FBoxCenterAndExtent>	OctreeBounds; // 导航 Octree 节点盒；在 NavOctree 调试位开启时填充

	FBox Bounds;                  // 整体包围盒，作为 SceneProxy 的 bounds
	FVector NavMeshDrawOffset;    // 整体 Z 偏移，避免和地面 Z-fighting（默认 (0,0,10)）
	uint32 bDataGathered : 1;     // 是否已经完成 GatherData（空数据不重复收集）
	uint32 bNeedsNewData : 1;     // 外部标记"需要重新收集"（NavMesh 变脏）
	int32 NavDetailFlags;         // ENavMeshDetailFlags 组合位图

	FNavMeshSceneProxyData() : NavMeshDrawOffset(0, 0, 10.f),
		bDataGathered(false), bNeedsNewData(true), NavDetailFlags(0) {}

	NAVIGATIONSYSTEM_API void Reset();              // 清空所有数组，准备复用
	NAVIGATIONSYSTEM_API void Serialize(FArchive& Ar); // 供 GameplayDebugger 网络/磁盘序列化
	NAVIGATIONSYSTEM_API uint32 GetAllocatedSize() const; // 统计自分配内存

#if WITH_RECAST
	UE_DEPRECATED(5.5, "Use the version of this function that takes an array of FNavTileRefs instead")
	NAVIGATIONSYSTEM_API void GatherData(const ARecastNavMesh* NavMesh, int32 InNavDetailFlags, const TArray<int32>& TileSet);
	// 核心采集函数：从 ARecastNavMesh 的 FRecastDebugGeometry 汇总本结构体里的所有字段
	// 只读 NavMesh 数据；供游戏线程调用（Proxy 构造前）
	NAVIGATIONSYSTEM_API void GatherData(const ARecastNavMesh* NavMesh, int32 InNavDetailFlags, const TArray<struct FNavTileRef>& TileSet);

#if RECAST_INTERNAL_DEBUG_DATA
	// 当保留 Tile 生成中间数据（Heightfield / Contour / PolyMesh）时，把它们转成 FMesh
	NAVIGATIONSYSTEM_API void AddMeshForInternalData(const struct FRecastInternalDebugData& InInternalData);
#endif //RECAST_INTERNAL_DEBUG_DATA

#endif
};

// exported to API for GameplayDebugger module
// -------------------------------------------------------------------------------------
// FNavMeshSceneProxy —— 进入渲染线程的 SceneProxy
// 在构造函数里一次性把 ProxyData 的三角形 / 线条 / 点写入 VB / IB，避免 RenderThread
// 运行时分配。每个 Cluster/Area 组共用一个 FColoredMaterialRenderProxy 做颜色着色。
// -------------------------------------------------------------------------------------
class FNavMeshSceneProxy final : public FDebugRenderSceneProxy, public FNoncopyable
{
	friend class FNavMeshDebugDrawDelegateHelper;
public:
	NAVIGATIONSYSTEM_API virtual SIZE_T GetTypeHash() const override;

	// 构造：拷贝并吃下 ProxyData；ForceToRender=true 会忽略 ShowFlag 强制画出来
	NAVIGATIONSYSTEM_API FNavMeshSceneProxy(const UPrimitiveComponent* InComponent, FNavMeshSceneProxyData* InProxyData, bool ForceToRender = false);
	NAVIGATIONSYSTEM_API virtual ~FNavMeshSceneProxy() override;

	// 渲染线程入口：根据 NavDetailFlags 决定组装哪些 FMeshBatch
	NAVIGATIONSYSTEM_API virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

protected:
	// 只在 ShowFlag Navigation 开启 / 或 bForceRendering 时返回 Dynamic/Translucent 通道
	NAVIGATIONSYSTEM_API virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual uint32 GetMemoryFootprint(void) const override { return sizeof(*this) + GetAllocatedSizeInternal(); }
	NAVIGATIONSYSTEM_API uint32 GetAllocatedSizeInternal(void) const;

private:
	FNavMeshSceneProxyData ProxyData; // Proxy 私有拷贝，渲染线程只读

	FDynamicMeshIndexBuffer32 IndexBuffer; // 所有 FilledPolys 合一张 IB
	FStaticMeshVertexBuffers VertexBuffers; // 顶点位置/颜色/法线/UV 缓冲
	FLocalVertexFactory VertexFactory;      // 绑定上面 VB 的顶点工厂

	// 每个 Cluster 颜色一个材质代理；着色器路径=SolidColor
	TArray<TUniquePtr<FColoredMaterialRenderProxy>> MeshColors;
	TArray<FMeshBatchElement> MeshBatchElements; // 每个 Cluster 一个 MeshBatchElement

	FDebugDrawDelegate DebugTextDrawingDelegate; // HUD 文字绘制委托
	FDelegateHandle    DebugTextDrawingDelegateHandle;
	TWeakObjectPtr<UNavMeshRenderingComponent> RenderingComponent; // 反向弱引用，供相关查询
	uint32 bForceRendering : 1;    // 即使 ShowFlag 关也画
	uint32 bSkipDistanceCheck : 1; // 跳过与相机距离的剔除
	uint32 bUseThickLines : 1;     // 线宽加粗（方便截图）
};

#if WITH_RECAST && UE_ENABLE_DEBUG_DRAWING
// -------------------------------------------------------------------------------------
// FNavMeshDebugDrawDelegateHelper —— HUD 文本绘制助手
// 把 ProxyData.DebugLabels 复制一份，在 UCanvas 上逐个绘制
// -------------------------------------------------------------------------------------
class FNavMeshDebugDrawDelegateHelper : public FDebugDrawDelegateHelper
{
	typedef FDebugDrawDelegateHelper Super;

public:
	FNavMeshDebugDrawDelegateHelper()
		: bForceRendering(false)
		, bNeedsNewData(false)
	{
	}

	// 从 SceneProxy 抽取调试文字列表（主线程）
	void SetupFromProxy(const FNavMeshSceneProxy* InSceneProxy)
	{
		DebugLabels.Reset();
		DebugLabels.Append(InSceneProxy->ProxyData.DebugLabels);
		bForceRendering = InSceneProxy->bForceRendering;
		bNeedsNewData = InSceneProxy->ProxyData.bNeedsNewData;
	}

	// 清空标签，等下一次刷新
	void Reset()
	{
		DebugLabels.Reset();
		bNeedsNewData = true;
	}

protected:
	// 实际绘制：把世界坐标 Project 到屏幕再 DrawText
	NAVIGATIONSYSTEM_API virtual void DrawDebugLabels(UCanvas* Canvas, APlayerController*) override;

private:
	TArray<FNavMeshSceneProxyData::FDebugText> DebugLabels;
	uint32 bForceRendering : 1;
	uint32 bNeedsNewData : 1;
};
#endif

// -------------------------------------------------------------------------------------
// UNavMeshRenderingComponent —— 挂在 ARecastNavMesh 上的调试绘制组件
// 在 OnRegister 时启动定时更新；Tick/Timer 回调里根据 bForceUpdate / ShowFlag
// 决定是否 MarkRenderStateDirty 触发 CreateSceneProxy。
// -------------------------------------------------------------------------------------
UCLASS(editinlinenew, ClassGroup = Debug, MinimalAPI)
class UNavMeshRenderingComponent : public UDebugDrawComponent
{
	GENERATED_UCLASS_BODY()

public:
	void ForceUpdate() { bForceUpdate = true; }
	bool IsForcingUpdate() const { return bForceUpdate; }

	// 工具函数：判断当前 World 主视口是否打开了 Navigation ShowFlag
	static NAVIGATIONSYSTEM_API bool IsNavigationShowFlagSet(const UWorld* World);

protected:
	// 注册/反注册时绑定定时器，用于延迟刷新
	NAVIGATIONSYSTEM_API virtual void OnRegister()  override;
	NAVIGATIONSYSTEM_API virtual void OnUnregister()  override;

#if UE_ENABLE_DEBUG_DRAWING
	// 主入口：收集 ProxyData → new FNavMeshSceneProxy
	NAVIGATIONSYSTEM_API virtual FDebugRenderSceneProxy* CreateDebugSceneProxy() override;
#if WITH_RECAST
	virtual FDebugDrawDelegateHelper& GetDebugDrawDelegateHelper() override { return NavMeshDebugDrawDelegateManager; }
#endif // WITH_RECAST
#endif // UE_ENABLE_DEBUG_DRAWING

	// 用 NavMesh 的 AllTilesBBox 作为组件包围盒，确保不被视锥裁剪
	NAVIGATIONSYSTEM_API virtual FBoxSphereBounds CalcBounds(const FTransform &LocalToWorld) const override;

	/** Gathers drawable information from NavMesh and puts it in OutProxyData. 
	 *	Override to add additional information to OutProxyData.*/
	// 子类可重写以追加自定义可视化元素（例如 AI 专用信息）
	NAVIGATIONSYSTEM_API virtual void GatherData(const ARecastNavMesh& NavMesh, FNavMeshSceneProxyData& OutProxyData) const;

	// 周期回调，用于节流式 MarkRenderStateDirty；避免每帧都重建 Proxy
	NAVIGATIONSYSTEM_API void TimerFunction();

protected:
	uint32 bCollectNavigationData : 1; // 是否每次刷新都收集（true=OnRegister 后激活）
	uint32 bForceUpdate : 1;           // 强制下次刷新
	FTimerHandle TimerHandle;          // TimerFunction 的计时器

protected:
#if WITH_RECAST && UE_ENABLE_DEBUG_DRAWING
	FNavMeshDebugDrawDelegateHelper NavMeshDebugDrawDelegateManager; // HUD 文本辅助
#endif
};

// 通用几何构建小工具（在 cpp 使用很多）
namespace FNavMeshRenderingHelpers
{
	// 把一个顶点追加到 FDebugMeshData（自动设置法线/颜色）
	NAVIGATIONSYSTEM_API void AddVertex(FNavMeshSceneProxyData::FDebugMeshData& MeshData, const FVector& Pos, const FColor Color = FColor::White);

	// 追加一个三角形的索引（逆时针）
	NAVIGATIONSYSTEM_API void AddTriangleIndices(FNavMeshSceneProxyData::FDebugMeshData& MeshData, int32 V0, int32 V1, int32 V2);
}
