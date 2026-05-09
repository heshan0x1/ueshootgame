// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ============================================================================
// 文件概览：NavMeshPath.h
// ----------------------------------------------------------------------------
// FNavMeshPath 是 FNavigationPath 针对 Recast NavMesh 的派生实现，承载两类信息：
//   1) "多边形走廊" PathCorridor：dtNavMeshQuery::findPath 直接产出的 PolyRef 序列，
//      它是一条无阻"多边形链"，尚未细化到具体行走点。
//   2) "拉绳径" (String-pulled Path)：PerformStringPulling 调用 Detour
//      findStraightPath 沿 Portal Edge 做漏斗算法，压缩为几何最短折线，
//      存入基类的 PathPoints。
// bWantsStringPulling / bStringPulled 控制是否以及已否完成拉绳；
// bWantsPathCorridor 决定寻路阶段要不要保留 PathCorridor（AI 流程里有些
// 只需要最终路径点，省一份走廊可以节省内存）。
// FNavMeshNodeFlags 是对 dtNavMeshQuery 返回的 32bit Flags 的位拆解工具：
//   低 8bit = StraightPath 标志（含 OFFMESH_CONNECTION 位）
//   中 8bit = Poly 所属 Area 类型
//   高 16bit = Area Flags（INCLUDE/EXCLUDE 掩码参与寻路筛选）
// ============================================================================

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "NavigationPath.h"
//#include "NavigationPath.generated.h"

// StraightPath 返回的标志位之一：本顶点是 OffMesh Link 的起止端。
// 与 Detour 的 DT_STRAIGHTPATH_OFFMESH_CONNECTION 数值一致。
#define RECAST_STRAIGHTPATH_OFFMESH_CONNECTION 0x04

/** Helper to translate FNavPathPoint.Flags. */
// FNavPathPoint.Flags 是 uint32 压缩存储，此工具把 3 段语义展开为字段。
struct FNavMeshNodeFlags
{
	/** Extra node information (like "path start", "off-mesh connection"). */
	uint8 PathFlags;   // Detour StraightPath 标志位（BEGIN/END/OFFMESH_CONNECTION 等）
	/** Area type after this node. */
	uint8 Area;        // 该节点之后进入的 Area 类型索引
	/** Area flags for this node. */
	uint16 AreaFlags;  // 该节点 Area 的 Include/Exclude 掩码

	FNavMeshNodeFlags() : PathFlags(0), Area(0), AreaFlags(0) {}
	// 从 packed uint32 解包
	FNavMeshNodeFlags(const uint32 Flags) : PathFlags((uint8)Flags), Area((uint8)(Flags >> 8)), AreaFlags((uint16)(Flags >> 16)) {}
	// 打包回 uint32 以存入 FNavPathPoint.Flags
	uint32 Pack() const { return PathFlags | ((uint32)Area << 8) | ((uint32)AreaFlags << 16); }
	// 是否为 NavLink（OffMeshConnection）端点
	bool IsNavLink() const { return (PathFlags & RECAST_STRAIGHTPATH_OFFMESH_CONNECTION) != 0; }

	// 追加 AreaFlags（或运算，便于叠加多源标志）
	FNavMeshNodeFlags& AddAreaFlags(const uint16 InAreaFlags)
	{
		AreaFlags = (AreaFlags | InAreaFlags);
		return *this;
	}
};


// Recast NavMesh 专用的路径对象。
// 数据双模型：
//   - PathCorridor (+ PathCorridorCost)：A* 搜索输出的多边形 ID 序列。
//   - PathPoints (基类持有) + bStringPulled：拉绳算法得到的几何行走路径。
// 任一模型都可以单独用（通过 bWantsPathCorridor / bWantsStringPulling 选择）。
struct FNavMeshPath : public FNavigationPath
{
	typedef FNavigationPath Super;

	NAVIGATIONSYSTEM_API FNavMeshPath();
	NAVIGATIONSYSTEM_API ~FNavMeshPath();

	NAVIGATIONSYSTEM_API FNavMeshPath(const FNavMeshPath&);
	NAVIGATIONSYSTEM_API FNavMeshPath(FNavMeshPath&& Other);
	NAVIGATIONSYSTEM_API FNavMeshPath& operator=(const FNavMeshPath& Other);
	NAVIGATIONSYSTEM_API FNavMeshPath& operator=(FNavMeshPath&& Other);

	// 是否希望在 FindPath 结束后自动做 String-Pulling（默认 true）
	inline void SetWantsStringPulling(const bool bNewWantsStringPulling) { bWantsStringPulling = bNewWantsStringPulling; }
	inline bool WantsStringPulling() const { return bWantsStringPulling; }
	// 是否已经完成拉绳；true 时 PathPoints 为拉绳结果，false 时只填了起止点
	inline bool IsStringPulled() const { return bStringPulled; }

	/** find string pulled path from PathCorridor */
	// 依据当前 PathCorridor 调用 Detour findStraightPath 得到拉绳径，
	// 写入基类 PathPoints 并置 bStringPulled=true。
	// 典型由 FPImplRecastNavMesh::FindPath 在寻路末尾调用。
	NAVIGATIONSYSTEM_API void PerformStringPulling(const FVector& StartLoc, const FVector& EndLoc);

	// 是否保留 PathCorridor（开销：每条路径额外一份 Poly 序列）
	inline void SetWantsPathCorridor(const bool bNewWantsPathCorridor) { bWantsPathCorridor = bNewWantsPathCorridor; }
	inline bool WantsPathCorridor() const { return bWantsPathCorridor; }

	// 按需生成/返回 Portal Edge 列表（走廊多边形相邻边）；
	// 懒计算：GeneratePathCorridorEdges() 首次访问时从 PathCorridor 推出。
	inline const TArray<FNavigationPortalEdge>& GetPathCorridorEdges() const { return bCorridorEdgesGenerated ? PathCorridorEdges : GeneratePathCorridorEdges(); }
	inline void SetPathCorridorEdges(const TArray<FNavigationPortalEdge>& InPathCorridorEdges) { PathCorridorEdges = InPathCorridorEdges; bCorridorEdgesGenerated = true; }

	// 通知外部："PathCorridor 改过了，下次 GetPathCorridorEdges 重新生成"
	inline void OnPathCorridorUpdated() { bCorridorEdgesGenerated = false; }

	// 在 Canvas 上绘制调试可视化；NextPathPointIndex 指出当前跟随到第几段
	NAVIGATIONSYSTEM_API virtual void DebugDraw(const ANavigationData* NavData, const FColor PathColor, UCanvas* Canvas, const bool bPersistent, const float LifeTime, const uint32 NextPathPointIndex = 0) const override;

	// 判断两条路径是否在"末尾相同 PolyRef 区段"相连（用于 Repath 平滑拼接）
	NAVIGATIONSYSTEM_API bool ContainsWithSameEnd(const FNavMeshPath* Other) const;

	// 让拐角点沿 Portal Edge 内移 Distance，避免 Agent 贴边穿墙
	NAVIGATIONSYSTEM_API void OffsetFromCorners(FVector::FReal Distance);

	// 将 NavDataFlags 应用到所有 PathPoint.Flags（例如整条路径打 "Jump" 标签）
	NAVIGATIONSYSTEM_API void ApplyFlags(int32 NavDataFlags);

	// Repath 准备：清空拉绳数据与 Corridor 边缓存；保留部分头部用于无缝接续
	NAVIGATIONSYSTEM_API virtual void ResetForRepath() override;

	/** get flags of path point or corridor poly (depends on bStringPulled flag) */
	// NodeIdx 的含义随 bStringPulled 而变：
	//   - true  → 基类 PathPoints 的下标
	//   - false → PathCorridor 的下标
	NAVIGATIONSYSTEM_API bool GetNodeFlags(int32 NodeIdx, FNavMeshNodeFlags& Flags) const;

	/** get cost of path, starting from next poly in corridor */
	// 从"紧跟在 PathNode 之后的那块多边形"起累加 PathCorridorCost
	virtual FVector::FReal GetCostFromNode(NavNodeRef PathNode) const override { return GetCostFromIndex(PathCorridor.Find(PathNode) + 1); }

	/** get cost of path, starting from given point */
	// 从 PathPointIndex（对应 PathCorridor 下标）起累加剩余 cost
	virtual FVector::FReal GetCostFromIndex(int32 PathPointIndex) const override
	{
		FVector::FReal TotalCost = 0.f;
		const FVector::FReal* Cost = PathCorridorCost.GetData();
		// 遍历剩余所有 Poly，累加进入代价
		for (int32 PolyIndex = PathPointIndex; PolyIndex < PathCorridorCost.Num(); ++PolyIndex, ++Cost)
		{
			TotalCost += *Cost;
		}

		return TotalCost;
	}

	// 总长度：拉绳版算折线总长，未拉绳则估算走廊长度（Edge 中点距离累加）
	inline FVector::FReal GetTotalPathLength() const
	{
		return bStringPulled ? GetStringPulledLength(0) : GetPathCorridorLength(0);
	}

	inline int32 GetNodeRefIndex(const NavNodeRef NodeRef) const { return PathCorridor.Find(NodeRef); }

	/** check if path (all polys in corridor) contains given node */
	virtual bool ContainsNode(NavNodeRef NodeRef) const override { return PathCorridor.Contains(NodeRef); }

	virtual bool ContainsCustomLink(FNavLinkId UniqueLinkId) const override { return CustomNavLinkIds.Contains(UniqueLinkId); }
	virtual bool ContainsAnyCustomLink() const override { return CustomNavLinkIds.Num() > 0; }

	// 判定 PathSegmentStartIndex 开始的那段是否是一条 NavLink（即 OFFMESH_CONNECTION 段）
	NAVIGATIONSYSTEM_API bool IsPathSegmentANavLink(const int32 PathSegmentStartIndex) const;

	// 路径与 Box 相交检测，用于障碍突然插入时触发 Repath
	NAVIGATIONSYSTEM_API virtual bool DoesIntersectBox(const FBox& Box, uint32 StartingIndex = 0, int32* IntersectingSegmentIndex = NULL, FVector* AgentExtent = NULL) const override;
	NAVIGATIONSYSTEM_API virtual bool DoesIntersectBox(const FBox& Box, const FVector& AgentLocation, uint32 StartingIndex = 0, int32* IntersectingSegmentIndex = NULL, FVector* AgentExtent = NULL) const override;
	/** retrieves normalized direction vector to given path segment. If path is not string pulled navigation corridor is being used */
	NAVIGATIONSYSTEM_API virtual FVector GetSegmentDirection(uint32 SegmentEndIndex) const override;

	// 反转路径方向（起终点互换）；同时反转 PathCorridor / PathCorridorCost / PathPoints
	NAVIGATIONSYSTEM_API void Invert();

private:
	// DoesIntersectBox 的实现细节；分派到两份重载的公共实现
	NAVIGATIONSYSTEM_API bool DoesPathIntersectBoxImplementation(const FBox& Box, const FVector& StartLocation, uint32 StartingIndex, int32* IntersectingSegmentIndex, FVector* AgentExtent) const;
	// 清空 NavMesh 路径所有字段（PathCorridor / 边缓存 / Link Ids）
	NAVIGATIONSYSTEM_API void InternalResetNavMeshPath();

public:

#if ENABLE_VISUAL_LOG
	// VisLog 快照（可在 VLog 里回放路径）
	NAVIGATIONSYSTEM_API virtual void DescribeSelfToVisLog(struct FVisualLogEntry* Snapshot) const override;
	NAVIGATIONSYSTEM_API virtual FString GetDescription() const override;
#endif // ENABLE_VISUAL_LOG

protected:
	/** calculates total length of string pulled path. Does not generate string pulled
	*	path if it's not already generated (see bWantsStringPulling and bStrigPulled)
	*	Internal use only */
	NAVIGATIONSYSTEM_API FVector::FReal GetStringPulledLength(const int32 StartingPoint) const;

	/** calculates estimated length of path expressed as sequence of navmesh edges.
	*	It basically sums up distances between every subsequent nav edge pair edge middles.
	*	Internal use only */
	// 走廊估算长度：相邻 Portal Edge 中点之间的距离之和（粗略，但不需要拉绳即可用）
	NAVIGATIONSYSTEM_API FVector::FReal GetPathCorridorLength(const int32 StartingEdge) const;

	/** it's only const to be callable in const environment. It's not supposed to be called directly externally anyway,
	*	just as part of retrieving corridor on demand or generating it in internal processes. It fills a mutable
	*	array. */
	// 懒计算 PathCorridorEdges，被 GetPathCorridorEdges() 首次访问时触发；
	// 由于需要写 mutable 成员，故 const 仅是名义上的（便于在 const 上下文调用）。
	NAVIGATIONSYSTEM_API const TArray<FNavigationPortalEdge>& GeneratePathCorridorEdges() const;

public:

	/** sequence of navigation mesh poly ids representing an obstacle-free navigation corridor */
	// Detour findPath 输出的多边形 ID 序列；可视作"无障走廊"。
	TArray<NavNodeRef> PathCorridor;

	/** for every poly in PathCorridor stores traversal cost from previous navpoly */
	// 与 PathCorridor 等长；每项是从上一块多边形进入本多边形的代价
	TArray<FVector::FReal> PathCorridorCost;

	/** set of unique link Ids */
	// 历史 32 位链接 ID 列表；5.3 起已废弃，保留仅为旧存档兼容；不再填充。
	UE_DEPRECATED(5.3, "LinkIds are now based on FNavLinkId. Use CustomNavLinkIds instead. CustomLinkIds array is no longer populated or used in the engine")
	TArray<uint32> CustomLinkIds;

	// 当前路径经过的所有自定义 NavLink 的稳定 64 位 ID 集合，用于 Repath 时比对
	TArray<FNavLinkId> CustomNavLinkIds;

private:
	/** sequence of FVector pairs where each pair represents navmesh portal edge between two polygons navigation corridor.
	*	Note, that it should always be accessed via GetPathCorridorEdges() since PathCorridorEdges content is generated
	*	on first access */
	// 走廊多边形之间的 Portal Edge（左右端点对）；懒计算，mutable 以便 const 方法填充
	mutable TArray<FNavigationPortalEdge> PathCorridorEdges;

	/** transient variable indicating whether PathCorridorEdges contains up to date information */
	// PathCorridorEdges 是否为最新；OnPathCorridorUpdated / InternalResetNavMeshPath 会清零
	mutable uint32 bCorridorEdgesGenerated : 1;

public:
	/** is this path generated on dynamic navmesh (i.e. one attached to moving surface) */
	// 是否生成在"动态 NavMesh"（附着到移动表面）上；影响位置重投影策略
	uint32 bDynamic : 1;

protected:
	/** does this path contain string pulled path? If true then NumPathVerts > 0 and
	*	OutPathVerts contains valid data. If false there's only navigation corridor data
	*	available.*/
	// 是否已经执行过 PerformStringPulling；决定 PathPoints 是否有效
	uint32 bStringPulled : 1;

	/** If set to true path instance will contain a string pulled version. Otherwise only
	*	navigation corridor will be available. Defaults to true */
	uint32 bWantsStringPulling : 1;

	/** If set to true path instance will contain path corridor generated as a part
	*	pathfinding call (i.e. without the need to generate it with GeneratePathCorridorEdges */
	uint32 bWantsPathCorridor : 1;

public:
	// 路径类型 RTTI（用于 FNavigationPath::IsA<FNavMeshPath>() 之类的多态判断）
	static NAVIGATIONSYSTEM_API const FNavPathType Type;
};
