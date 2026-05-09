// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationTestingActor.h
// -----------------------------------------------------------------------------
// 编辑器调试工具：在关卡里放两个 ANavigationTestingActor，一个是 Start（bSearchStart=true）
// 另一个是 Goal（OtherActor 指向它）。编辑器里拖动即可看到：
//   - 寻路路径（bShowBestPath）及 A* 内部步骤（bShowNodePool / bShowDiffWithPreviousStep）
//   - 各种查询：离墙最近点 / NavData 是否覆盖 / Raycast 到目标 / ...
//   - 总代价、寻路耗时、Node 数等统计
//
// 关键字段：
//   - NavAgentProps：模拟寻路代理的物理参数（类型/半径/高度）
//   - FilterClass：寻路用的 UNavigationQueryFilter
//   - CostLimitFactor / MinimumCostLimit：FindPath 的代价截断
//   - ShowStepIndex：-1 不画步骤；>=0 显示 A* 第 N 步
//
// 执行流：
//   PostEditMove / PostEditChangeProperty / TickHelper → UpdatePathfinding
//     → BuildPathFindingQuery + MyNavData->FindPath
//     → 把结果写到 LastPath，DebugSteps 保存 A* 中间态供 NavTestRenderingComponent 绘制
//
// 同时实现 INavAgentInterface 让它被当作一个寻路代理，
// INavPathObserverInterface 用于接收 LastPath 的事件（当前实现为空）。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "GameFramework/Actor.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "NavigationData.h"
#include "Tickable.h"
#include "AI/Navigation/NavPathObserverInterface.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationTestingActor.generated.h"

class ANavigationTestingActor;
class UNavigationInvokerComponent;

// 编辑器中对 ANavigationTestingActor Tick 的辅助代理。
// 原因：Actor 本身在编辑器下不 Tick，需要借 FTickableGameObject 渲染步骤动画。
struct FNavTestTickHelper : FTickableGameObject
{
	TWeakObjectPtr<ANavigationTestingActor> Owner;

	FNavTestTickHelper() : Owner(NULL) {}
	virtual void Tick(float DeltaTime);
	virtual bool IsTickable() const { return Owner.IsValid(); }
	// 编辑器模式下也 Tick——这是它存在的根本原因
	virtual bool IsTickableInEditor() const { return true; }
	virtual TStatId GetStatId() const ;
};

// 调试面板上"显示代价"的几种模式
UENUM()
namespace ENavCostDisplay
{
	enum Type : int
	{
		TotalCost,     // g + h
		HeuristicOnly, // h
		RealCostOnly,  // g
	};
}

// 导航调试 Actor：关卡内可视化工具，不参与游戏逻辑。
UCLASS(hidecategories=(Object, Actor, Input, Rendering, Replication, HLOD, Cooking), showcategories=("Input|MouseInput", "Input|TouchInput"), Blueprintable, MinimalAPI)
class ANavigationTestingActor : public AActor, public INavAgentInterface, public INavPathObserverInterface
{
	GENERATED_UCLASS_BODY()

private:
	// 根组件：用于可视化的胶囊（大小与 NavAgentProps 一致）
	UPROPERTY()
	TObjectPtr<class UCapsuleComponent> CapsuleComponent;

#if WITH_EDITORONLY_DATA
	/** Editor Preview */
	// 调试渲染组件：从本 Actor 拉数据，绘制路径/步骤/投影点等
	UPROPERTY()
	TObjectPtr<class UNavTestRenderingComponent> EdRenderComp;
#endif // WITH_EDITORONLY_DATA

	// 可选：把本 Actor 也当作 Invoker（用于局部生成场景调试）
	UPROPERTY(EditAnywhere, Category = Navigation, meta=(EditCondition="bActAsNavigationInvoker"))
	TObjectPtr<UNavigationInvokerComponent> InvokerComponent;

	// 上面字段的开关
	UPROPERTY(EditAnywhere, Category = Navigation, meta=(InlineEditConditionToggle))
	uint32 bActAsNavigationInvoker : 1;

public:

	/** @todo document */
	// 代理参数：决定用哪份 NavData（按 NavAgentProps 匹配 FNavDataConfig）
	UPROPERTY(EditAnywhere, Category=Agent)
	FNavAgentProperties NavAgentProps;

	// 投影/查询时的 Extent 容差
	UPROPERTY(EditAnywhere, Category=Agent)
	FVector QueryingExtent;

	// 当前使用的 NavData；由 UpdateNavData 依据 NavAgentProps 选择
	UPROPERTY(transient)
	TObjectPtr<ANavigationData> MyNavData;

	// 在 NavData 上投影 Actor 位置得到的点
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=AgentStatus)
	FVector ProjectedLocation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=AgentStatus)
	uint32 bProjectedLocationValid : 1;

	/** if set, start the search from this actor, else start the search from the other actor */
	// 决定 FindPath 从哪头开始：true = 从本 Actor 起算
	UPROPERTY(EditAnywhere, Category=Pathfinding)
	uint32 bSearchStart : 1;

	/** this multiplier is used to compute a max node cost allowed to the open list
	 *	(cost limit = CostLimitFactor*InitialHeuristicEstimate) */
	// A* open list 代价上限倍率：cost_limit = CostLimitFactor * 初始启发估计
	UPROPERTY(EditAnywhere, Category=Pathfinding, meta = (ClampMin = "0", UIMin = "0"))
	float CostLimitFactor;

	/** minimum cost limit clamping value (in cost units)
	 *	used to allow large deviation in short paths */
	// 代价上限下界：短路径允许更大的偏离，避免被截断
	UPROPERTY(EditAnywhere, Category = Pathfinding, meta = (ClampMin = "0", UIMin = "0"))
	float MinimumCostLimit;

	/** Instead of regular pathfinding from source to target location do
	 *	a 'backwards' search that searches from the source, but as if the allowed
	 *	movement direction was coming from the target. Meaningful only for paths
	 *	containing one-direction nav links. */
	// 反向搜索：对含单向链接的路径有意义，从源点模拟"从终点反向允许方向"的搜索
	UPROPERTY(EditAnywhere, Category = Pathfinding)
	uint32 bBacktracking : 1;

	// 分层寻路：启用后走 Cluster 层级的 A*（见 WITH_NAVMESH_CLUSTER_LINKS）
	UPROPERTY(EditAnywhere, Category=Pathfinding)
	uint32 bUseHierarchicalPathfinding : 1;

	/** if set, all steps of A* algorithm will be accessible for debugging */
	// 采集 A* 每一步的中间态，填 DebugSteps 供调试绘制
	UPROPERTY(EditAnywhere, Category=Pathfinding)
	uint32 bGatherDetailedInfo : 1;

	/** if set, require the end location to be close to the navigation data. The tolerance is controlled by QueryingExtent */
	// 要求终点投影到 NavData 成功，否则视为失败
	UPROPERTY(EditAnywhere, Category=Pathfinding)
	uint32 bRequireNavigableEndLocation : 1;

	// 画离墙最近点
	UPROPERTY(EditAnywhere, Category=Query)
	uint32 bDrawDistanceToWall : 1;

	/** If set, a cylinder is drawn to indicate if the navigation data is ready (has been generated) for the given radius (green when ready, red otherwise). */
	// 绘制"给定半径内 NavData 是否就绪"的圆柱
	UPROPERTY(EditAnywhere, Category=Query)
	uint32 bDrawIfNavDataIsReadyInRadius : 1;

	/** If set, a capsule is drawn to indicate if the navigation data is ready (has been generated) for the given radius from the current actor to the query target (green when ready, red otherwise). */
	// 绘制"到 QueryTargetActor 路径上 NavData 是否就绪"的胶囊
	UPROPERTY(EditAnywhere, Category=Query)
	uint32 bDrawIfNavDataIsReadyToQueryTargetActor : 1;

	/** If set, a line is drawn to indicate to result of a ray cast on the navigation data between the current actor and the QueryTargetActor location
	 * (red when there is a hit, green when there is no hit and the ray end is on the explored corridor, orange otherwise). */
	// 画 NavMesh Raycast 结果到目标
	UPROPERTY(EditAnywhere, Category=Query)
	uint32 bDrawRaycastToQueryTargetActor : 1;

	/** Actor to use as a target for navigation data queries */
	UPROPERTY(EditAnywhere, Category=Query)
	TObjectPtr<AActor> QueryTargetActor;

	/** show polys from open (orange) and closed (yellow) sets */
	// 调试绘制：Open（橙）/ Closed（黄）多边形集合
	UPROPERTY(EditAnywhere, Category=Debug)
	uint32 bShowNodePool : 1;

	/** show current best path */
	UPROPERTY(EditAnywhere, Category=Debug)
	uint32 bShowBestPath : 1;

	/** show which nodes were modified in current A* step */
	// 显示本步 A* 修改的节点（与上一步的 diff）
	UPROPERTY(EditAnywhere, Category=Debug)
	uint32 bShowDiffWithPreviousStep : 1;

	// 运行时也可见（默认仅编辑器）
	UPROPERTY(EditAnywhere, Category=Debug)
	uint32 bShouldBeVisibleInGame : 1;

	/** NavData must be ready for all tiles within radius. When using 0, NavData must be ready at the actor location. */
	// 0 = 仅检查本点；>0 = 整个半径覆盖的 Tile 都要就绪
	UPROPERTY(EditAnywhere, Category=Query)
	float RadiusUsedToValidateNavData = 0;
	
	/** determines which cost will be shown*/
	UPROPERTY(EditAnywhere, Category=Debug)
	TEnumAsByte<ENavCostDisplay::Type> CostDisplayMode;

	/** text canvas offset to apply */
	// 屏幕文字绘制偏移（避免文字挤作一团）
	UPROPERTY(EditAnywhere, Category=Debug)
	FVector2D TextCanvasOffset;

	// 寻路结果状态
	UPROPERTY(transient, VisibleAnywhere, BlueprintReadOnly, Category=PathfindingStatus)
	uint32 bPathExist : 1;

	UPROPERTY(transient, VisibleAnywhere, BlueprintReadOnly, Category=PathfindingStatus)
	uint32 bPathIsPartial : 1;
	
	UPROPERTY(transient, VisibleAnywhere, BlueprintReadOnly, Category=PathfindingStatus)
	uint32 bPathSearchOutOfNodes : 1;

	/** Time in micro seconds */
	UPROPERTY(transient, VisibleAnywhere, BlueprintReadOnly, Category=PathfindingStatus)
	float PathfindingTime;

	UPROPERTY(transient, VisibleAnywhere, BlueprintReadOnly, Category=PathfindingStatus)
	double PathCost;

	UPROPERTY(transient, VisibleAnywhere, BlueprintReadOnly, Category=PathfindingStatus)
	int32 PathfindingSteps;

	// 另一头 Actor —— 与本 Actor 配对进行 FindPath
	UPROPERTY(EditAnywhere, Category=Pathfinding)
	TObjectPtr<ANavigationTestingActor> OtherActor;

	/** "None" will result in default filter being used. This filter is used by the PathFind and Raycast queries. */
	// None 时用 NavData 默认过滤器
	UPROPERTY(EditAnywhere, Category=Query)
	TSubclassOf<class UNavigationQueryFilter> FilterClass;

	/** Show debug steps up to this index. Use -1 to disable. */
	// 步骤播放索引；-1 关闭
	UPROPERTY(EditInstanceOnly, Category=Debug, meta=(ClampMin="-1", UIMin="-1"))
	int32 ShowStepIndex;

	// 拉拐角偏移：用于路径平滑
	UPROPERTY(EditAnywhere, Category=Pathfinding)
	float OffsetFromCornersDistance;

	// 查询结果缓存（transient，只在编辑器绘制过程中使用）
	FVector ClosestWallLocation;
	FVector RaycastHitLocation;

	bool bNavDataIsReadyInRadius;
	bool bNavDataIsReadyToQueryTargetActor;
	bool bRaycastToQueryTargetActorResult;
	bool bRaycastToQueryTargetEndsInCorridor;

#if WITH_RECAST && WITH_EDITORONLY_DATA
	/** detail data gathered from each step of regular A* algorithm */
	// A* 每步保留的详细数据——ShowNodePool/DiffWithPreviousStep/步骤播放用
	TArray<struct FRecastDebugPathfindingData> DebugSteps;

	// 编辑器 Tick 用的辅助对象
	FNavTestTickHelper* TickHelper;
#endif

	// 上次寻路得到的路径（含 GoalActor 观察用途）
	FNavPathSharedPtr LastPath;
	// 观察者委托：路径失效时的回调
	FNavigationPath::FPathObserverDelegate::FDelegate PathObserver;

	/** Dtor */
	NAVIGATIONSYSTEM_API virtual ~ANavigationTestingActor();

	NAVIGATIONSYSTEM_API virtual void BeginDestroy() override;

#if WITH_EDITOR
	NAVIGATIONSYSTEM_API virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	NAVIGATIONSYSTEM_API virtual void PostEditMove(bool bFinished) override;
	
	NAVIGATIONSYSTEM_API virtual void PostLoad() override;
	// 编辑器 Tick：推进 ShowStepIndex 动画 / 重新跑查询
	NAVIGATIONSYSTEM_API void TickMe();
#endif // WITH_EDITOR

	//~ Begin INavAgentInterface Interface
	virtual const FNavAgentProperties& GetNavAgentPropertiesRef() const override { return NavAgentProps; }
	NAVIGATIONSYSTEM_API virtual FVector GetNavAgentLocation() const override;
	virtual void GetMoveGoalReachTest(const AActor* MovingActor, const FVector& MoveOffset, FVector& GoalOffset, float& GoalRadius, float& GoalHalfHeight) const override {}
	//~ End INavAgentInterface Interface

	//~ Begin INavPathObserverInterface Interface
	// 路径事件回调都是空实现——本类不需要响应路径事件
	virtual void OnPathUpdated(class INavigationPathGenerator* PathGenerator) override {};
	virtual void OnPathInvalid(class INavigationPathGenerator* PathGenerator) override {};
	virtual void OnPathFailed(class INavigationPathGenerator* PathGenerator) override {};
	//~ End INavPathObserverInterface Interface	

	// 根据 NavAgentProps 从 NavigationSystem 选出对应的 ANavigationData 存到 MyNavData
	NAVIGATIONSYSTEM_API void UpdateNavData();
	// 重跑一次寻路，同时更新 ProjectedLocation / LastPath / 统计字段
	NAVIGATIONSYSTEM_API void UpdatePathfinding();
	// 主寻路函数：构造 Query → MyNavData->FindPath
	NAVIGATIONSYSTEM_API virtual void SearchPathTo(ANavigationTestingActor* Goal);

	/*	Called when given path becomes invalid (via @see PathObserverDelegate)
	 *	NOTE: InvalidatedPath doesn't have to be instance's current Path
	 */
	// 路径事件回调：对应 FNavigationPath::FPathObserverDelegate
	NAVIGATIONSYSTEM_API void OnPathEvent(FNavigationPath* InvalidatedPath, ENavPathEvent::Type Event);

	// Virtual method to override if you want to customize the query being 
	// constructed for the path find (e.g. change the filter or add 
	// constraints/goal evaluators).
	// 构造 FPathFindingQuery 的地方，派生类可覆盖以定制过滤/约束
	NAVIGATIONSYSTEM_API virtual FPathFindingQuery BuildPathFindingQuery(const ANavigationTestingActor* Goal) const;

	/** Returns CapsuleComponent subobject **/
	class UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
#if WITH_EDITORONLY_DATA
	/** Returns EdRenderComp subobject **/
	class UNavTestRenderingComponent* GetEdRenderComp() const { return EdRenderComp; }
#endif

protected:
	// 更新本 Actor 自身的查询（投影点 / 离墙距离 / NavData Ready）
	void UpdateLocalQueries();
	// 更新与 QueryTargetActor 相关的查询（Raycast / 覆盖检测）
	void UpdateTargetActorQueries();

	// 离墙最近点：调用 NavData.FindDistanceToWall
	NAVIGATIONSYSTEM_API FVector FindClosestWallLocation() const;
	// 检查半径内所有 Tile 是否都已生成
	bool CheckIfNavDataIsReadyInRadius();
	// 检查从本 Actor 到目标 Actor 路径上 Tile 是否都已生成
	bool CheckIfNavDataIsReadyToActor(const AActor* TargetActor);
	// NavMesh 上的 Raycast：命中 + 是否落在已探索走廊上
	bool CheckRaycastToActor(const AActor* TargetActor, FVector& OutHitLocation, bool& bOutIsRaycastEndInCorridor);
	// 目标 Actor 变换变化时重新跑 TargetActorQueries
	void OnQueryTargetActorTransformUpdated(USceneComponent* InRootComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
};

