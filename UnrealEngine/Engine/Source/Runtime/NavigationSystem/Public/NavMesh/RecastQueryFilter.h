// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

// ============================================================================
// 文件概览：RecastQueryFilter.h
// ----------------------------------------------------------------------------
// FRecastQueryFilter —— UE 侧过滤器到 Detour 侧过滤器的"双继承适配器"：
//   - 对上：实现 INavigationQueryFilterInterface（引擎抽象，FNavigationQueryFilter
//           会通过此接口设置 Area 代价、Flag、启发系数等）。
//   - 对下：同时继承 Detour 的 dtQueryFilter，以便直接被
//           dtNavMeshQuery::findPath / raycast / findNearestPoly 使用。
//   - UNavigationQueryFilter（UObject 描述）→ FNavigationQueryFilter（运行时）
//     → 内部持有 FRecastQueryFilter 的方式，使蓝图配置能无缝传入 Detour 调用。
//
// FRecastSpeciaLinkFilter —— Detour 在遍历到"自定义 OffMeshLink"时，会回调
//   isLinkAllowed 来询问"这条 Link 对当前 Owner 可不可走"。典型使用场景：
//   被锁住的门、需要某条件才能走的梯子等。NavSystem 借助这个 hook 把
//   INavLinkCustomInterface::IsLinkPathfindingAllowed 的语义接入 Detour。
// ============================================================================

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "AI/Navigation/NavigationTypes.h"
#include "AI/Navigation/NavQueryFilter.h"

#if WITH_RECAST

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "Detour/DetourNavMesh.h"
#endif
#include "Detour/DetourNavMeshQuery.h"

// Recast 允许的最小 Agent 半径；当代理半径小于此阈值时，
// 可以绕过部分基于半径的缩进（inset）判断，用于极小体积的代理（如飞行单位）。
#define RECAST_VERY_SMALL_AGENT_RADIUS 0.0f

class UNavigationSystemV1;

// Recast NavMesh 的查询过滤器实现。
// 双继承：
//   - INavigationQueryFilterInterface：UE 侧 API，供 FNavigationQueryFilter 调用。
//   - dtQueryFilter：Detour 侧 API，直接传给 dtNavMeshQuery 的各查询函数。
// 线程：当过滤器不被修改时，可跨线程复用（只读）；SetAreaCost 等修改操作不可重入。
class FRecastQueryFilter : public INavigationQueryFilterInterface, public dtQueryFilter
{
public:
	// 构造；bIsVirtual=true 时使用 dtQueryFilter 的虚函数版本（支持子类覆盖 passVirtualFilter/getVirtualCost），
	// false 时使用内联快速路径（常规 Area cost 查表）。
	NAVIGATIONSYSTEM_API FRecastQueryFilter(bool bIsVirtual = true);
	virtual ~FRecastQueryFilter(){}

	// 恢复到默认状态（所有 Area cost 清零、Include/Exclude flags 复位）
	NAVIGATIONSYSTEM_API virtual void Reset() override;

	// ----- 下列方法通过 INavigationQueryFilterInterface 转接到 dtQueryFilter 内部数组 -----
	// 设置某 Area 的单位距离代价（乘子）
	NAVIGATIONSYSTEM_API virtual void SetAreaCost(uint8 AreaType, float Cost) override;
	// 设置进入某 Area 需要支付的固定额外代价（与距离无关）
	NAVIGATIONSYSTEM_API virtual void SetFixedAreaEnteringCost(uint8 AreaType, float Cost) override;
	// 将某 Area 标记为"完全不可通行"
	NAVIGATIONSYSTEM_API virtual void SetExcludedArea(uint8 AreaType) override;
	// 批量写入所有 Area 的代价（CostArray 长度为 Count）
	NAVIGATIONSYSTEM_API virtual void SetAllAreaCosts(const float* CostArray, const int32 Count) override;
	// 批量读出所有 Area 的代价与固定进入代价
	NAVIGATIONSYSTEM_API virtual void GetAllAreaCosts(float* CostArray, float* FixedCostArray, const int32 Count) const override;
	// 设置是否开启"反向搜索"（从目标向起点展开，用于反向/双向搜索算法）
	NAVIGATIONSYSTEM_API virtual void SetBacktrackingEnabled(const bool bBacktracking) override;
	NAVIGATIONSYSTEM_API virtual bool IsBacktrackingEnabled() const override;
	// 启发函数缩放（H * Scale），Scale>1 偏向贪心，Scale<1 更接近 Dijkstra
	NAVIGATIONSYSTEM_API virtual float GetHeuristicScale() const override;
	// 深度比较（用于路径观察时判断过滤器是否变化，决定是否要 Repath）
	NAVIGATIONSYSTEM_API virtual bool IsEqual(const INavigationQueryFilterInterface* Other) const override;
	// 包含位（只遍历带有这些 flag 的多边形）
	NAVIGATIONSYSTEM_API virtual void SetIncludeFlags(uint16 Flags) override;
	NAVIGATIONSYSTEM_API virtual uint16 GetIncludeFlags() const override;
	// 排除位（跳过带有这些 flag 的多边形；优先级高于 Include）
	NAVIGATIONSYSTEM_API virtual void SetExcludeFlags(uint16 Flags) override;
	NAVIGATIONSYSTEM_API virtual uint16 GetExcludeFlags() const override;
	// 终点调整钩子；Recast 过滤器里默认透传，派生类可用来把终点吸附到可达位置
	virtual FVector GetAdjustedEndLocation(const FVector& EndLocation) const override { return EndLocation; }
	// 拷贝自身以便线程安全传递给查询器
	NAVIGATIONSYSTEM_API virtual INavigationQueryFilterInterface* CreateCopy() const override;

	// 向 Detour 暴露自身指针；所有 dtNavMeshQuery::xxx 都要求一个 const dtQueryFilter*。
	const dtQueryFilter* GetAsDetourQueryFilter() const { return this; }

	/** Changes whether the filter will use virtual set of filtering functions (getVirtualCost and passVirtualFilter)
	 *	or the inlined ones (getInlineCost and passInlineFilter) */
	// 切换"虚函数路径"或"内联快路径"。
	// 虚函数版本可以被子类重载以加入自定义 Cost 计算，代价是每个多边形一次间接调用。
	NAVIGATIONSYSTEM_API void SetIsVirtual(bool bIsVirtual);

	/** Instruct filter whether it can reopen nodes already on closed list */
	// 是否允许重开已在 Closed List 中的节点；
	// 在某些特殊启发策略（例如时间切片寻路，过程中 cost 变化）下，关闭此选项能避免死循环。
	NAVIGATIONSYSTEM_API void SetShouldIgnoreClosedNodes(const bool bIgnoreClosed);

	//----------------------------------------------------------------------//
	// @note you might also want to override following functions from dtQueryFilter	
	// virtual bool passVirtualFilter(const dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const;
	// virtual FVector::FReal getVirtualCost(const FVector::FReal* pa, const FVector::FReal* pb, const dtPolyRef prevRef, const dtMeshTile* prevTile, const dtPoly* prevPoly, const dtPolyRef curRef, const dtMeshTile* curTile, const dtPoly* curPoly, const dtPolyRef nextRef, const dtMeshTile* nextTile, const dtPoly* nextPoly) const;
	// 【扩展点】派生类可重载以上两个虚函数接入自定义 Cost/过滤逻辑。
};

// Detour 回调过滤器：寻路过程中遇到自定义 OffMesh Link 时调 isLinkAllowed。
// 用于把"这条门锁着/禁用了"之类的游戏逻辑接入 Detour A* 搜索。
struct FRecastSpeciaLinkFilter : public dtQuerySpecialLinkFilter
{
	FRecastSpeciaLinkFilter(UNavigationSystemV1* NavSystem, const UObject* Owner) : NavSys(NavSystem), SearchOwner(Owner), CachedOwnerOb(nullptr) {}
	// 询问一条由 UserId 标识的 Link 是否允许被 SearchOwner 通行。
	// 内部会：NavSys->GetCustomLink(UserId) -> INavLinkCustomInterface::IsLinkPathfindingAllowed(CachedOwnerOb)。
	NAVIGATIONSYSTEM_API virtual bool isLinkAllowed(const uint64 UserId) const override;
	// 首次调用前解出 SearchOwner 的原始 UObject 指针到 CachedOwnerOb，避免后续每次 WeakPtr 解引用。
	NAVIGATIONSYSTEM_API virtual void initialize() override;

	UNavigationSystemV1* NavSys;     // 当前 World 的导航系统，负责 UserId → Link 查找
	FWeakObjectPtr SearchOwner;      // 发起寻路的 Actor/Controller，用来做 Link 权限检查
	UObject* CachedOwnerOb;          // initialize() 期间缓存的 Owner 裸指针，避免每次解引用 WeakPtr
};

#endif	// WITH_RECAST
