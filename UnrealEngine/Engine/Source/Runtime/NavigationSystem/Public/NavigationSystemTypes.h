// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   NavigationSystem 模块 Layer 0（基础层）的通用类型定义总入口。
//   放置与"寻路请求/Invoker/NavLink 后处理"相关的基础 POD 结构与辅助函数，
//   供上层 ANavigationData / UNavigationSystemV1 / Recast 生成器共用。
//
// 核心类型/命名空间：
//   - FPathFindingQueryData：一次寻路请求的基础参数（起点/终点/Filter/Cost 上限等）。
//   - FPathFindingQuery：在 QueryData 之上追加 NavData、可复用 Path 实例与 Agent 属性。
//   - EPathFindingMode：寻路模式枚举（Regular / Hierarchical）。
//   - FMoveRequestCustomData：供 PathFollowing 组件附带自定义数据的基类占位。
//   - FNavigationInvokerRaw / FNavigationInvoker：
//       "Navigation Invoker"（在其附近按需生成 Tile 的活跃点）的运行时描述。
//   - NavigationHelper 命名空间：从 BodySetup 收集碰撞、NavLink 后处理委托。
//   - FNavigationSystem::ECreateIfMissing：若目标对象不存在是否允许自动创建。
//
// 与其它文件的关系：
//   - 被 NavigationSystem.h / NavigationData.h / RecastNavMesh 大量使用。
//   - 通过 typedef 暴露两个线程安全共享指针：
//       FSharedConstNavQueryFilter  —— 寻路过滤器只读共享句柄
//       FNavPathSharedPtr           —— 路径对象共享句柄
//   - 配套实现：Private/NavigationSystemTypes.cpp（Gather 碰撞、NavLink 处理默认实现）。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "AI/NavigationSystemConfig.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "Math/GenericOctreePublic.h"
#include "AI/NavigationModifier.h"
#endif
#include "AI/Navigation/NavigationTypes.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "UObject/WeakInterfacePtr.h"

// 内部调试断言开关（仅 Debug 构建，且需要手工打开）。
#define NAVSYS_DEBUG (0 && UE_BUILD_DEBUG)

// 非 Shipping 构建默认开启：允许 Recast Tile 生成过程保留中间数据（Heightfield/Contour/PolyMesh 等）以供可视化。
#define RECAST_INTERNAL_DEBUG_DATA (!UE_BUILD_SHIPPING)

enum class ENavigationInvokerPriority : uint8;
class UBodySetup;
class UNavCollision;
struct FKAggregateGeom;
class FNavigationOctree;
class UNavigationPath;
class ANavigationData;
class INavigationInvokerInterface;
struct FCompositeNavModifier;
struct FNavigationLink;
struct FNavigationSegmentLink;
struct FNavigationQueryFilter;
// 只读的寻路 Filter 共享句柄（运行时在多线程查询中传递）。
typedef TSharedPtr<const FNavigationQueryFilter, ESPMode::ThreadSafe> FSharedConstNavQueryFilter;
// 路径对象（FNavigationPath）的线程安全共享指针，返回给调用方消费。
typedef TSharedPtr<struct FNavigationPath, ESPMode::ThreadSafe> FNavPathSharedPtr;

// 一次寻路请求的"基础数据部分"：不含 NavData/Agent 描述，只给出查询条件与选项。
struct FPathFindingQueryData
{
	// 发起寻路的业务对象（AActor/AIController 等），仅作追踪与日志用途。
	TWeakObjectPtr<const UObject> Owner;
	// 寻路起点（世界坐标）。
	FVector StartLocation;
	// 寻路终点（世界坐标）。
	FVector EndLocation;
	// 查询过滤器（线程安全共享），决定 Area Cost 覆写、Include/Exclude Flags 等。
	FSharedConstNavQueryFilter QueryFilter;

	/** cost limit of nodes allowed to be added to the open list */
	// 允许加入 A* Open List 的节点上限代价；超过则不扩展，可用于约束搜索规模。
	FVector::FReal CostLimit;
	
	/** additional flags passed to navigation data handling request */
	// 透传给 NavData 的附加标记位，具体语义由派生 NavData 解释。
	int32 NavDataFlags;

	/** if set, allow partial paths as a result */
	// 为 true 时允许返回"部分路径"（终点不可达也给出最近尝试的结果）。
	uint32 bAllowPartialPaths : 1;

	/** if set, require the end location to be linked to the navigation data */
	// 为 true 时要求终点必须落在导航数据上（否则直接失败）。
	uint32 bRequireNavigableEndLocation : 1;

	FPathFindingQueryData() : StartLocation(FNavigationSystem::InvalidLocation), EndLocation(FNavigationSystem::InvalidLocation), CostLimit(TNumericLimits<FVector::FReal>::Max()), NavDataFlags(0), bAllowPartialPaths(true), bRequireNavigableEndLocation(true) {}

	FPathFindingQueryData(const UObject* InOwner, const FVector& InStartLocation, const FVector& InEndLocation, FSharedConstNavQueryFilter InQueryFilter = nullptr, int32 InNavDataFlags = 0, bool bInAllowPartialPaths = true, const  FVector::FReal InCostLimit = TNumericLimits<FVector::FReal>::Max(), const bool bInRequireNavigableEndLocation = true) :
		Owner(InOwner), StartLocation(InStartLocation), EndLocation(InEndLocation), QueryFilter(InQueryFilter), CostLimit(InCostLimit), NavDataFlags(InNavDataFlags), bAllowPartialPaths(bInAllowPartialPaths), bRequireNavigableEndLocation(bInRequireNavigableEndLocation) {}
};

// 完整的寻路请求：在 QueryData 之上增加"在哪个 NavData 上查询、把结果填到谁、用什么 Agent 参数"。
struct FPathFindingQuery : public FPathFindingQueryData
{
	// 指定使用的 NavigationData（一般是 ARecastNavMesh）。若空则由 NavigationSystem 按 Agent 自动挑选。
	TWeakObjectPtr<const ANavigationData> NavData;
	// 可选：复用传入的路径对象而非新建一个（用于 repath）。
	FNavPathSharedPtr PathInstanceToFill;
	// 用于挑选 NavData 的 Agent 参数（半径/高度/类型）。
	FNavAgentProperties NavAgentProperties;

	FPathFindingQuery() : FPathFindingQueryData() {}
	NAVIGATIONSYSTEM_API FPathFindingQuery(const UObject* InOwner, const ANavigationData& InNavData, const FVector& Start, const FVector& End, FSharedConstNavQueryFilter SourceQueryFilter = NULL, FNavPathSharedPtr InPathInstanceToFill = NULL, const FVector::FReal CostLimit = TNumericLimits<FVector::FReal>::Max(), const bool bInRequireNavigableEndLocation = true);
	NAVIGATIONSYSTEM_API FPathFindingQuery(const INavAgentInterface& InNavAgent, const ANavigationData& InNavData, const FVector& Start, const FVector& End, FSharedConstNavQueryFilter SourceQueryFilter = NULL, FNavPathSharedPtr InPathInstanceToFill = NULL, const FVector::FReal CostLimit = TNumericLimits<FVector::FReal>::Max(), const bool bInRequireNavigableEndLocation = true);

	// 根据一条既有路径构造"重算请求"：保留原路径的配置，重新计算一次。
	NAVIGATIONSYSTEM_API explicit FPathFindingQuery(FNavPathSharedRef PathToRecalculate, const ANavigationData* NavDataOverride = NULL);

	// 以下为链式 Setter，便于表达式式构造查询。
	FPathFindingQuery& SetPathInstanceToUpdate(FNavPathSharedPtr InPathInstanceToFill) { PathInstanceToFill = InPathInstanceToFill; return *this; }
	FPathFindingQuery& SetAllowPartialPaths(const bool bAllow) { bAllowPartialPaths = bAllow; return *this; }
	FPathFindingQuery& SetRequireNavigableEndLocation(const bool bRequire) { bRequireNavigableEndLocation = bRequire; return *this; }
	FPathFindingQuery& SetNavAgentProperties(const FNavAgentProperties& InNavAgentProperties) { NavAgentProperties = InNavAgentProperties; return *this; }

	/** utility function to compute a cost limit using an Euclidean heuristic, an heuristic scale and a cost limit factor
	*	CostLimitFactor: multiplier used to compute the cost limit value from the initial heuristic
	*	MinimumCostLimit: minimum clamping value used to prevent low cost limit for short path query */
	// 基于欧氏启发式 * 启发尺度 * 因子计算 CostLimit；给短距离查询设最小下限，避免裁剪过严。
	static NAVIGATIONSYSTEM_API FVector::FReal ComputeCostLimitFromHeuristic(const FVector& StartPos, const FVector& EndPos, const FVector::FReal HeuristicScale, const FVector::FReal CostLimitFactor, const FVector::FReal MinimumCostLimit);
};

// 寻路模式：Regular 为标准 A*；Hierarchical 走多级（Cluster）加速但精度较低。
namespace EPathFindingMode
{
	enum Type
	{
		Regular,
		Hierarchical,
	};
};

////////////////////////////////////////////////////////////////////////////
//// Custom path following data
//
///** Custom data passed to movement requests. */
// 供 PathFollowing/MoveRequest 透传自定义扩展数据的基类占位（本身空实现，业务可派生）。
struct FMoveRequestCustomData
{
};

typedef TSharedPtr<FMoveRequestCustomData, ESPMode::ThreadSafe> FCustomMoveSharedPtr;
typedef TWeakPtr<FMoveRequestCustomData, ESPMode::ThreadSafe> FCustomMoveWeakPtr;

//----------------------------------------------------------------------//
// Active tiles 
//----------------------------------------------------------------------//
// Invoker 的"原始快照"：缓存位置与半径环以便在非主线程访问时避免访问 Actor 指针。
struct FNavigationInvokerRaw
{
	// Invoker 所在世界坐标。
	FVector Location;
	// 生成半径：此距离内 Tile 会被保留/生成。
	float RadiusMin;
	// 删除半径：超过此距离的 Tile 将被丢弃（需 >= RadiusMin）。
	float RadiusMax;
	// 限制该 Invoker 作用的 Agent 集合（按位）。
	FNavAgentSelector SupportedAgents;
	// Tile 脏化排序优先级。
	ENavigationInvokerPriority Priority;

	FNavigationInvokerRaw(const FVector& InLocation, float Min, float Max, const FNavAgentSelector& InSupportedAgents, ENavigationInvokerPriority InPriority);
};

class AActor;

// Navigation Invoker：注册进 NavigationSystem 的"活跃点"，仅其附近生成/保留 Tile（动态世界关键数据结构）。
struct FNavigationInvoker
{
	/** The Invoker source should be either an Actor or an Object. Thus only 1 of those member should be set. We'll use IsExplicitlyNull to know which one to use */
	// Invoker 载体：AActor（常见）或实现 INavigationInvokerInterface 的 UObject（二选一）。
	TWeakObjectPtr<AActor> Actor;
	TWeakInterfacePtr<INavigationInvokerInterface> Object;

	/** tiles GenerationRadius away or close will be generated if they're not already present */
	// 生成半径：距离 Invoker 小于该值的 Tile 若还未构建则排入生成队列。
	float GenerationRadius;

	/** tiles over RemovalRadius will get removed.
	*	@Note needs to be >= GenerationRadius or will get clamped */
	// 删除半径：距离大于该值的 Tile 被丢弃；必须 >= GenerationRadius，否则会被 Clamp。
	float RemovalRadius;

	/** restrict navigation generation to specific agents */
	// 仅对指定 Agent 生成导航（位集）。
	FNavAgentSelector SupportedAgents;

	/** invoker Priority used when dirtying tiles */
	// 标脏时的优先级（靠前 Invoker 相关的 Tile 更早重建）。
	ENavigationInvokerPriority Priority;

	FNavigationInvoker();
	FNavigationInvoker(AActor& InActor, float InGenerationRadius, float InRemovalRadius, const FNavAgentSelector& InSupportedAgents, ENavigationInvokerPriority InPriority);
	FNavigationInvoker(INavigationInvokerInterface& InObject, float InGenerationRadius, float InRemovalRadius, const FNavAgentSelector& InSupportedAgents, ENavigationInvokerPriority InPriority);

	// 返回 Invoker 可读名（日志/调试面板用）。
	FString GetName() const;
	// 取得当前 Invoker 的世界位置；Actor/Object 已失效则返回 false。
	bool GetLocation(FVector& OutLocation) const;
};

// NavigationSystem 内部使用的工具函数集合（碰撞收集、NavLink 后处理等）。
namespace NavigationHelper
{
	// 从 UBodySetup 中抽取碰撞几何（顶点+索引），输出到 Recast 可识别的缓冲区。
	void GatherCollision(UBodySetup* RigidBody, TNavStatArray<FVector>& OutVertexBuffer, TNavStatArray<int32>& OutIndexBuffer, const FTransform& ComponentToWorld = FTransform::Identity);
	// 同上，但把结果写入 UNavCollision（StaticMesh 走 DDC 缓存时用）。
	void GatherCollision(UBodySetup* RigidBody, UNavCollision* NavCollision);

	/** gather collisions from aggregated geom, convex and tri mesh elements are not supported - use override with full UBodySetup param instead */
	// 仅从 AggregateGeom 的简单形状（Box/Sphere/Capsule）抽取几何；Convex/TriMesh 需走上面的重载。
	void GatherCollision(const FKAggregateGeom& AggGeom, UNavCollision& NavCollision);
}

namespace FNavigationSystem
{
	// "目标对象不存在时是否自动创建" 的三态枚举。
	enum ECreateIfMissing
	{
		Invalid = -1,
		DontCreate = 0,
		Create = 1,
	};

	typedef ECreateIfMissing ECreateIfEmpty;
}

//----------------------------------------------------------------------//
// 
//----------------------------------------------------------------------//

namespace NavigationHelper
{
	// NavLink 所属对象的变换信息：供 NavLink 后处理把相对坐标变换为世界坐标。
	struct FNavLinkOwnerData
	{
		// 链接所属 Actor；若链接来自组件则取组件 Owner。
		const AActor* Actor;
		// 链接局部空间到世界空间的变换矩阵。
		FTransform LinkToWorld;

		FNavLinkOwnerData() : Actor(nullptr) {}
		NAVIGATIONSYSTEM_API FNavLinkOwnerData(const AActor& InActor);
		NAVIGATIONSYSTEM_API FNavLinkOwnerData(const USceneComponent& InComponent);
	};

	// NavLink 处理委托（旧接口，Actor 版）——把 NavLink 数组追加到 CompositeModifier。
	DECLARE_DELEGATE_ThreeParams(FNavLinkProcessorDelegate, FCompositeNavModifier*, const AActor*, const TArray<FNavigationLink>&);
	DECLARE_DELEGATE_ThreeParams(FNavLinkSegmentProcessorDelegate, FCompositeNavModifier*, const AActor*, const TArray<FNavigationSegmentLink>&);

	// NavLink 处理委托（新接口，使用 FNavLinkOwnerData，支持组件来源）。
	DECLARE_DELEGATE_ThreeParams(FNavLinkProcessorDataDelegate, FCompositeNavModifier*, const FNavLinkOwnerData&, const TArray<FNavigationLink>&);
	DECLARE_DELEGATE_ThreeParams(FNavLinkSegmentProcessorDataDelegate, FCompositeNavModifier*, const FNavLinkOwnerData&, const TArray<FNavigationSegmentLink>&);

	/** Set new implementation of nav link processor, a function that will be
	*	be used to process/transform links before adding them to CompositeModifier.
	*	This function is supposed to be called once during the engine/game
	*	setup phase. Not intended to be toggled at runtime */
	// 注入自定义 NavLink 处理器（只应在 StartupModule 阶段调用一次，不可运行时切换）。
	NAVIGATIONSYSTEM_API void SetNavLinkProcessorDelegate(const FNavLinkProcessorDataDelegate& NewDelegate);
	NAVIGATIONSYSTEM_API void SetNavLinkSegmentProcessorDelegate(const FNavLinkSegmentProcessorDataDelegate& NewDelegate);

	/** called to do any necessary processing on NavLinks and put results in CompositeModifier */
	// 将 NavLink 数组通过当前处理器写入 CompositeModifier（最终参与 Tile 生成）。
	NAVIGATIONSYSTEM_API void ProcessNavLinkAndAppend(FCompositeNavModifier* OUT CompositeModifier, const AActor* Actor, const TArray<FNavigationLink>& IN NavLinks);
	NAVIGATIONSYSTEM_API void ProcessNavLinkAndAppend(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationLink>& IN NavLinks);

	/** called to do any necessary processing on NavLinks and put results in CompositeModifier */
	// SegmentLink 版本（段对段连接，用于 WITH_NAVMESH_SEGMENT_LINKS 实验特性）。
	NAVIGATIONSYSTEM_API void ProcessNavLinkSegmentAndAppend(FCompositeNavModifier* OUT CompositeModifier, const AActor* Actor, const TArray<FNavigationSegmentLink>& IN NavLinks);
	NAVIGATIONSYSTEM_API void ProcessNavLinkSegmentAndAppend(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationSegmentLink>& IN NavLinks);

	// 默认处理器：根据 Direction 交换端点，并按 MaxFallDownLength/LeftProjectHeight 做地表投影。
	NAVIGATIONSYSTEM_API void DefaultNavLinkProcessorImpl(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationLink>& IN NavLinks);
	NAVIGATIONSYSTEM_API void DefaultNavLinkSegmentProcessorImpl(FCompositeNavModifier* OUT CompositeModifier, const FNavLinkOwnerData& OwnerData, const TArray<FNavigationSegmentLink>& IN NavLinks);

	// 判定一个 BodySetup 是否"对导航有贡献"——既要有几何也要阻挡 Pawn/Vehicle 且启用 QueryAndPhysics。
	NAVIGATIONSYSTEM_API bool IsBodyNavigationRelevant(const UBodySetup& IN BodySetup);
}
