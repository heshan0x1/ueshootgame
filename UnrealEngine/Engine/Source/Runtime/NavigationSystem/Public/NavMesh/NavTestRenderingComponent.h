// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NavTestRenderingComponent.h —— 中文总览
// ---------------------------------------------------------------------------
// 本文件声明与 `ANavigationTestingActor` 配套的"调试渲染组件"及其 SceneProxy：
//   - UNavTestRenderingComponent：挂在 ANavigationTestingActor 上的 DebugDrawComponent，
//     负责构造 FNavTestSceneProxy 并把它推到渲染线程。
//   - FNavTestSceneProxy：渲染线程侧数据——寻路路径点、Open/Closed Set、
//     A* 搜索节点调试数据（NodeDebug）、最近墙点等可视化几何。
//   - FNavTestDebugDrawDelegateHelper：把 Canvas 调试文本（节点说明 / 路径点旗标）
//     画到 HUD 上的委托辅助类。
//
// 数据流（与架构文档 4.x 节 NavigationTestingActor 部分相关）：
//   ANavigationTestingActor → UpdatePathfinding/UpdateStep 时生成路径与节点数据
//     → 通过 UNavTestRenderingComponent 标记脏 → CreateDebugSceneProxy
//     → 只读拷贝到 FNavTestSceneProxy（线程安全）→ RenderThread 绘制。
//
// 仅在 UE_ENABLE_DEBUG_DRAWING 开启时编译 SceneProxy 相关类。
// =====================================================================================

#pragma once

#include "Debug/DebugDrawComponent.h"
#include "AI/Navigation/NavigationTypes.h" // NavNodeRef

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "PrimitiveViewRelevance.h"
#include "DynamicMeshBuilder.h"
#include "DebugRenderSceneProxy.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4

#include "NavTestRenderingComponent.generated.h"

#if UE_ENABLE_DEBUG_DRAWING

class FPrimitiveSceneProxy;
class ANavigationTestingActor;
class APlayerController;
class FMeshElementCollector;
class UCanvas;
class UNavTestRenderingComponent;
struct FNodeDebugData;

// -------------------------------------------------------------------------------------
// FNavTestSceneProxy —— 渲染线程侧的调试场景代理
// 职责：把 ANavigationTestingActor 在主线程上算好的路径 / 搜索节点拷贝到渲染侧绘制。
// 所有字段在构造时一次性从主线程数据快照出来，进入渲染线程后只读，符合 UE 的渲染线程
// 安全规约（参考架构文档 6.x 注释规约第 7 点）。
// -------------------------------------------------------------------------------------
class FNavTestSceneProxy final : public FDebugRenderSceneProxy
{
	// 允许 DrawDelegateHelper 直接读 private 字段，方便文本标签的组装
	friend class FNavTestDebugDrawDelegateHelper;

public:
	// 返回 RTTI 类型哈希，UE 渲染模块用它做 SceneProxy 类型识别
	virtual SIZE_T GetTypeHash() const override;

	// 单个 A* / Detour 搜索节点的调试信息
	// 由 Proxy 从 NavTestActor->DebugSteps 拷贝出来用于可视化
	struct FNodeDebugData
	{
		NavNodeRef PolyRef;        // 该节点所在多边形引用（Detour polyRef）
		FVector Position;          // 节点中心世界坐标
		FString Desc;              // 显示在节点上方的文本（代价/启发等）
		FSetElementId ParentId;    // 父节点在 NodeDebug 集合里的位置，用来连线
		uint32 bClosedSet : 1;     // 是否在 Closed Set（已确定最优代价）
		uint32 bBestPath : 1;      // 是否属于当前最优路径
		uint32 bModified : 1;      // 相对上一步是否被修改过（diff 模式用）
		uint32 bOffMeshLink : 1;   // 是否通过 OffMesh 链接到达（跳跃/门等）

		inline bool operator==(const FNodeDebugData& Other) const
		{
			return PolyRef == Other.PolyRef;
		}
		// TSet 用 PolyRef 去重：一个多边形只保留一个调试节点
		inline friend uint32 GetTypeHash(const FNodeDebugData& Other)
		{
			return ::GetTypeHash(Other.PolyRef);
		}
	};

	// 构造时读 Component/Actor 上的数据快照（主线程）
	explicit FNavTestSceneProxy(const UNavTestRenderingComponent* InComponent);

	// 渲染线程回调：按帧把 PathPoints/OpenSet/ClosedSet/NodeDebug 转成线段 / 三角形 / 点
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	// 从 NavTestActor 的完整路径数据提取可视化路径点
	void GatherPathPoints();

	// 从"单步调试"中取出当前步的 Open/Closed 集合
	void GatherPathStep();

	// 快速视锥剔除辅助：点是否落在视锥内（把位置当作零尺寸 AABB）
	inline static bool LocationInView(const FVector& Location, const FSceneView* View)
	{
		return View->ViewFrustum.IntersectBox(Location, FVector::ZeroVector);
	}

	// 告诉渲染系统本 Proxy 参与哪类通道（一般是 Editor/Debug）
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSizeInternal(); }

private:
	// 统计所有 TArray/TSet 的自分配尺寸，便于统计场景代理内存
	uint32 GetAllocatedSizeInternal() const;

	// 调试绘制时整体向上偏移，避免与地面 Z-fighting；来自 ARecastNavMesh::DrawOffset
	FVector3f NavMeshDrawOffset;

	uint32 bShowBestPath : 1;  // 是否高亮"最优路径"上的节点
	uint32 bShowNodePool : 1;  // 是否绘制全部搜索过节点（Open+Closed 集合）
	uint32 bShowDiff : 1;      // 是否以 diff 方式对比上一步（高亮被修改节点）

	ANavigationTestingActor* NavTestActor;  // 主线程对应 Actor（弱引用，仅在主线程读取）
	TArray<FVector> PathPoints;             // 最终路径的折线顶点
	TArray<FString> PathPointFlags;         // 每个路径点旁边要显示的标志/说明（HUD 文本）

	TArray<FDynamicMeshVertex> OpenSetVerts;    // Open Set 节点的三角形网格顶点
	TArray<uint32>             OpenSetIndices;  // Open Set 顶点索引
	TArray<FDynamicMeshVertex> ClosedSetVerts;  // Closed Set 节点网格顶点
	TArray<uint32>             ClosedSetIndices;// Closed Set 顶点索引
	TSet<FNodeDebugData>       NodeDebug;       // 所有参与可视化的搜索节点
	FSetElementId              BestNodeId;      // NodeDebug 里指向"最优路径末端"的 id

	FVector ClosestWallLocation;  // FindDistanceToWall 查询结果（若启用）
};

// -------------------------------------------------------------------------------------
// FNavTestDebugDrawDelegateHelper —— 在 HUD 上绘制调试文字
// 它保存一份 Proxy 数据的拷贝，在 OnPostRenderViews 回调里把文字（如节点 id、代价）
// 投影到屏幕并画到 UCanvas 上。属于游戏线程侧辅助（对应 FDebugRenderSceneProxy 的
// DrawDebugLabels 机制）。
// -------------------------------------------------------------------------------------
class FNavTestDebugDrawDelegateHelper : public FDebugDrawDelegateHelper
{
	typedef FDebugDrawDelegateHelper Super;

public:
	FNavTestDebugDrawDelegateHelper(): bShowBestPath(false), bShowDiff(false)
	{
	}

	// 从 SceneProxy 拷贝一份文字所需的数据（NodeDebug/PathPoints 等）
	// 在主线程调用，避免渲染线程访问 UObject
	void SetupFromProxy(const FNavTestSceneProxy* InSceneProxy);

protected:
	// 主绘制入口：按视角投影，逐个节点/路径点 DrawText
	virtual void DrawDebugLabels(UCanvas* Canvas, APlayerController*) override;

private:
	TSet<FNavTestSceneProxy::FNodeDebugData> NodeDebug;  // Proxy 数据的副本
	ANavigationTestingActor* NavTestActor = nullptr;     // 仅用于读取偏移/名称
	TArray<FVector> PathPoints;
	TArray<FString> PathPointFlags;
	FSetElementId BestNodeId;
	uint32 bShowBestPath : 1;
	uint32 bShowDiff : 1;
};

#endif // UE_ENABLE_DEBUG_DRAWING

// -------------------------------------------------------------------------------------
// UNavTestRenderingComponent —— 挂在 NavigationTestingActor 上的调试绘制组件
// 核心行为：
//   - CalcBounds 返回足够大的 AABB 保证 SceneProxy 不被视锥剔除掉
//   - CreateDebugSceneProxy 生成本次帧的 FNavTestSceneProxy 快照
//   - GetDebugDrawDelegateHelper 返回用于画 HUD 文本的委托实例
// -------------------------------------------------------------------------------------
UCLASS(ClassGroup = Debug)
class UNavTestRenderingComponent: public UDebugDrawComponent
{
	GENERATED_BODY()

protected:

	// 组件包围盒，结合 Actor 位置用于剔除/视口聚焦
	virtual FBoxSphereBounds CalcBounds(const FTransform &LocalToWorld) const override;

#if UE_ENABLE_DEBUG_DRAWING
	// 主线程调用，构造本次帧的渲染线程代理；会立即把 Actor 数据快照进 Proxy
	virtual FDebugRenderSceneProxy* CreateDebugSceneProxy() override;
	virtual FDebugDrawDelegateHelper& GetDebugDrawDelegateHelper() override { return NavTestDebugDrawDelegateHelper; }
private:
	FNavTestDebugDrawDelegateHelper NavTestDebugDrawDelegateHelper; // HUD 文本绘制辅助
#endif // UE_ENABLE_DEBUG_DRAWING
};

