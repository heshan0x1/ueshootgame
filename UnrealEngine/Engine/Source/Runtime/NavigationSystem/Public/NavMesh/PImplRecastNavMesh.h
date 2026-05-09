// Copyright Epic Games, Inc. All Rights Reserved.

//
// Private implementation for communication with Recast library
// 
// All functions should be called through RecastNavMesh actor to make them thread safe!
//
// ============================================================================
// 文件概览：PImplRecastNavMesh.h
// ----------------------------------------------------------------------------
// FPImplRecastNavMesh 使用 Pimpl 模式把 `dtNavMesh*` 与所有 Detour 交互集中封装，
// 让 ARecastNavMesh.h 不必直接暴露 Detour 头文件（加速编译 + 隔离依赖）。
//
// 本类是"引擎 → Detour"之间最核心的适配层，职责：
//   - 生命周期：SetRecastMesh / ReleaseDetourNavMesh，持有 dtNavMesh* 的所有权；
//   - 查询：FindPath / TestPath / Raycast / ProjectPointToNavMesh / FindNearestPoly
//           / GetRandomPoint / FindPolysAroundCircle / FindMoveAlongSurface；
//   - 寻路辅助：InitPathfinding（起止点投射 + 坐标 UE→Recast 转换）、
//               PostProcessPath（拉绳、Flags 应用、Link 标记）；
//   - 多边形访问：GetPolyCenter/Verts/Neighbors/Flags/WallSegments/Edges/Area；
//   - Tile 管理：Tile Cache 层增删查、RemoveTileCacheLayer；
//   - 序列化：Serialize、SerializeRecastMeshTile / SerializeCompressedTileCacheData
//             （供 URecastNavMeshDataChunk 复用）；
//   - 调试：GetDebugGeometryForTile、GetTilePolyEdges、DebugPathfinding 等。
//
// 线程约束：所有接口应由 ARecastNavMesh 通过 BeginBatchQuery/FinishBatchQuery
//   加读锁后调用；SharedNavQuery 是线程本地的 dtNavMeshQuery 对象缓存。
// ============================================================================

#pragma once 

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/WeakObjectPtr.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "NavFilters/NavigationQueryFilter.h"
#endif
#include "AI/Navigation/NavigationTypes.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastQueryFilter.h"
#include "AI/NavigationSystemBase.h"
#include "VisualLogger/VisualLogger.h"

#if RECAST_INTERNAL_DEBUG_DATA
#include "NavMesh/RecastInternalDebugData.h"
#endif

#if WITH_RECAST
#include "Detour/DetourNavMesh.h"
#include "Detour/DetourNavMeshQuery.h"
#endif

class FRecastNavMeshGenerator;
struct FNavLinkId;

#if WITH_RECAST

#define RECAST_VERY_SMALL_AGENT_RADIUS 0.0f

/** Engine Private! - Private Implementation details of ARecastNavMesh */
// ARecastNavMesh 的 Pimpl 实现；持有 dtNavMesh* 并提供所有 Detour 查询适配。
class FPImplRecastNavMesh
{
public:

	/** Constructor */
	NAVIGATIONSYSTEM_API FPImplRecastNavMesh(ARecastNavMesh* Owner);

	/** Dtor */
	// 析构会 ReleaseDetourNavMesh —— 释放持有的 dtNavMesh 与 TileCache
	NAVIGATIONSYSTEM_API ~FPImplRecastNavMesh();

	/**
	 * Serialization.
	 * @param Ar - The archive with which to serialize.
	 * @returns true if serialization was successful.
	 */
	// 序列化整个 dtNavMesh（Params + 所有 Tile 二进制 + TileCache 压缩层）。
	// 版本兼容：NavMeshVersion < NAVMESHVER_MIN_COMPATIBLE 会被上层跳过。
	NAVIGATIONSYSTEM_API void Serialize(FArchive& Ar, int32 NavMeshVersion);

	/* Gather debug geometry.
	 * @params OutGeometry Output geometry.
	 * @params TileIndex Used to collect geometry for a specific tile, INDEX_NONE will gather all tiles.
	 * @return True if done collecting.
	 */
	// 5.5 弃用：旧 int32 TileIndex 版本
	UE_DEPRECATED(5.5, "Use the version of this function that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API bool GetDebugGeometryForTile(FRecastDebugGeometry& OutGeometry, int32 TileIndex) const;

	/* Gather debug geometry.
	 * @params OutGeometry Output geometry.
	 * @params TileRef Used to collect geometry for a specific tile, an invalid FNavTileRef will gather all tiles.
	 * @return True if done collecting.
	 */
	// 收集调试几何（多边形顶点/索引/颜色/OffMeshLink 线等）；
	// TileRef 无效则收集全部 Tile（大 NavMesh 时开销显著）
	NAVIGATIONSYSTEM_API bool GetDebugGeometryForTile(FRecastDebugGeometry& OutGeometry, FNavTileRef TileRef) const;
	
	/** Returns bounding box for the whole navmesh. */
	// 所有 Tile AABB 的并集（UE 坐标，已 Recast2Unreal 翻回）
	NAVIGATIONSYSTEM_API FBox GetNavMeshBounds() const;

	/** Returns bounding box for a given navmesh tile. */
	UE_DEPRECATED(5.5, "Use the version of this function on ARecastNavMesh that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API FBox GetNavMeshTileBounds(int32 TileIndex) const;

	/** Retrieves XY and layer coordinates of tile specified by index */
	UE_DEPRECATED(5.5, "Use the version of this function on ARecastNavMesh that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API bool GetNavMeshTileXY(int32 TileIndex, int32& OutX, int32& OutY, int32& OutLayer) const;

	/** Retrieves XY coordinates of tile specified by position */
	// 世界位置 → Tile (X,Y)；内部 Unreal2Recast(Point) 后调 dtNavMesh::calcTileLoc
	NAVIGATIONSYSTEM_API bool GetNavMeshTileXY(const FVector& Point, int32& OutX, int32& OutY) const;

	/** Retrieves all tile indices at matching XY coordinates */
	UE_DEPRECATED(5.5, "Use the version of this function on ARecastNavMesh that takes an array of FNavTileRefs instead.")
	NAVIGATIONSYSTEM_API void GetNavMeshTilesAt(int32 TileX, int32 TileY, TArray<int32>& Indices) const;

	/** Retrieves list of tiles that intersect specified bounds */
	UE_DEPRECATED(5.5, "Use the version of the function that takes an array of FNavTileRefs instead")
	NAVIGATIONSYSTEM_API void GetNavMeshTilesIn(const TArray<FBox>& InclusionBounds, TArray<int32>& Indices) const;

	/** Retrieves list of tiles that intersect specified bounds */
	// 取所有 AABB 与 InclusionBounds 相交的 Tile；
	// 用途：Tile 级调试绘制、世界分区局部刷新
	NAVIGATIONSYSTEM_API void GetNavMeshTilesIn(const TArray<FBox>& InclusionBounds, TArray<FNavTileRef>& OutRefs) const;

	/** Retrieves number of tiles in this navmesh */
	inline int32 GetNavMeshTilesCount() const { return DetourNavMesh ? DetourNavMesh->getMaxTiles() : 0; }

	/** Supported queries */

	/** Generates path from the given query. Synchronous. */
	// 同步寻路：UE 坐标的 Start/End → Detour findPath → StringPull → PathPoints/PathCorridor。
	// 被调者：ARecastNavMesh::FindPath（静态方法，被挂到基类 ANavigationData::FindPathImplementation 函数指针）。
	// 详细链路：InitPathfinding → dtNavMeshQuery::findPath → PostProcessPath。
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type FindPath(const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, const bool bRequireNavigableEndLocation, FNavMeshPath& Path, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Check if path exists */
	// 仅判断可达，不生成路径数据；Detour 的 findPath 配合 cost limit，更便宜
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type TestPath(const FVector& StartLoc, const FVector& EndLoc, const bool bRequireNavigableEndLocation, const FNavigationQueryFilter& Filter, const UObject* Owner, int32* NumVisitedNodes = 0) const;

	// 自定义 A* 算法版寻路：由用户传入图 Wrapper / A* 算法 / 结果类型。
	// 用于研究/扩展：在 Detour 图上跑 UE 自己的 FGraphAStar 之类。
	template< typename TRecastAStar, typename TRecastAStartGraph, typename TRecastGraphAStarFilter, typename TRecastAStarResult >
	ENavigationQueryResult::Type FindPathCustomAStar(TRecastAStartGraph& RecastGraphWrapper, TRecastAStar& AStarAlgo, const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, FNavMeshPath& Path, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Checks if the whole segment is in navmesh */
	// UE 的 Raycast：与 dtNavMeshQuery::raycast 不同——这里用"起止点都在 NavMesh"的假设，
	// 沿线推进遍历多边形，遇到 Wall 或 Filter 拒绝即返回命中。
	// 与 Detour 版区别：UE 版可以跨 Tile 连续推进；返回结构含 HitLocation/PolyRef 序列。
	NAVIGATIONSYSTEM_API void Raycast(const FVector& StartLoc, const FVector& EndLoc, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner,
		ARecastNavMesh::FRaycastResult& RaycastResult, NavNodeRef StartNode = INVALID_NAVNODEREF) const;

	/** Generates path from given query and collect data for every step of A* algorithm */
	// 带步进数据的寻路：每一步的 Open/Closed List / 当前 best 节点等；用于可视化 A* 过程
	NAVIGATIONSYSTEM_API int32 DebugPathfinding(const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, const bool bRequireNavigableEndLocation, const FNavigationQueryFilter& Filter, const UObject* Owner, TArray<FRecastDebugPathfindingData>& Steps);

	/** Returns a random location on the navmesh. */
	// 全局均匀随机点；内部 dtNavMeshQuery::findRandomPoint
	NAVIGATIONSYSTEM_API FNavLocation GetRandomPoint(const FNavigationQueryFilter& Filter, const UObject* Owner) const;

#if WITH_NAVMESH_CLUSTER_LINKS
	/** Check if path exists using cluster graph */
	// 基于多边形簇的连通性测试（比多边形级别 A* 快，用于长距离可达性预筛）
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type TestClusterPath(const FVector& StartLoc, const FVector& EndLoc, int32* NumVisitedNodes = 0) const;

	/** Returns a random location on the navmesh within cluster */
	NAVIGATIONSYSTEM_API bool GetRandomPointInCluster(NavNodeRef ClusterRef, FNavLocation& OutLocation) const;
#endif // WITH_NAVMESH_CLUSTER_LINKS

	/**	Tries to move current nav location towards target constrained to navigable area. Faster than ProjectPointToNavmesh.
	 *	@param OutLocation if successful this variable will be filed with result
	 *	@return true if successful, false otherwise
	 */
	// 在 NavMesh 表面沿直线推进（不做 A*）；遇墙停住。被 AIController 做 Avoidance 等短距滑行
	NAVIGATIONSYSTEM_API bool FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	// 将点投影到 NavMesh；Extent 定义垂直/水平容差盒；返回命中的 FNavLocation（含 PolyRef）
	NAVIGATIONSYSTEM_API bool ProjectPointToNavMesh(const FVector& Point, FNavLocation& Result, const FVector& Extent, const FNavigationQueryFilter& Filter, const UObject* Owner) const;
	
	/** Project single point and grab all vertical intersections */
	// 多层投影：从 MinZ 到 MaxZ 搜索所有垂直可达的 Poly（适合多层建筑）
	NAVIGATIONSYSTEM_API bool ProjectPointMulti(const FVector& Point, TArray<FNavLocation>& OutLocations, const FVector& Extent,
		FVector::FReal MinZ, FVector::FReal MaxZ, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Returns nearest navmesh polygon to Loc, or INVALID_NAVNODEREF if Loc is not on the navmesh. */
	// Detour findNearestPoly 的 UE 封装；寻路前将起止点锁定到具体 Poly
	NAVIGATIONSYSTEM_API NavNodeRef FindNearestPoly(FVector const& Loc, FVector const& Extent, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Finds the polygons along the navigation graph that touch the specified circle. Return true if found any. */
	// 圆形范围内的 Poly 搜索（类似 flood fill）；可返回父节点+代价用于 AI 范围感知
	NAVIGATIONSYSTEM_API bool FindPolysAroundCircle(const FVector& CenterPos, const NavNodeRef CenterNodeRef, const FVector::FReal Radius, const FNavigationQueryFilter& Filter, const UObject* Owner, TArray<NavNodeRef>* OutPolys = nullptr, TArray<NavNodeRef>* OutPolysParent = nullptr, TArray<float>* OutPolysCost = nullptr, int32* OutPolysCount = nullptr) const;

	/** Retrieves all polys within given pathing distance from StartLocation.
	 *	@NOTE query is not using string-pulled path distance (for performance reasons),
	 *		it measured distance between middles of portal edges, do you might want to 
	 *		add an extra margin to PathingDistance */
	// 按"寻路代价距离"搜索所有可达 Poly；距离以 Portal Edge 中点近似（不做拉绳，速度优先）
	NAVIGATIONSYSTEM_API bool GetPolysWithinPathingDistance(FVector const& StartLoc, const FVector::FReal PathingDistance,
		const FNavigationQueryFilter& Filter, const UObject* Owner,
		TArray<NavNodeRef>& FoundPolys, FRecastDebugPathfindingData* DebugData) const;

	//@todo document
	// 走廊 → Portal Edge 序列；对每对相邻 Poly 调 dtNavMesh::getPortalPoints
	NAVIGATIONSYSTEM_API void GetEdgesForPathCorridor(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges) const;

	/** finds stringpulled path from given corridor */
	// dtNavMeshQuery::findStraightPath 的 UE 封装；把多边形走廊压成最短折线 PathPoints
	NAVIGATIONSYSTEM_API bool FindStraightPath(const FVector& StartLoc, const FVector& EndLoc, const TArray<NavNodeRef>& PathCorridor, TArray<FNavPathPoint>& PathPoints, TArray<FNavLinkId>* CustomLinks = NULL) const;

	/** Filters nav polys in PolyRefs with Filter */
	// 剔除 PolyRefs 中被 Filter 拒绝的多边形（原地修改）
	NAVIGATIONSYSTEM_API bool FilterPolys(TArray<NavNodeRef>& PolyRefs, const FRecastQueryFilter* Filter, const UObject* Owner) const;

	/** Get all polys from tile */
	UE_DEPRECATED(5.5, "Use the version of this function in ARecastNavMesh that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API bool GetPolysInTile(int32 TileIndex, TArray<FNavPoly>& Polys) const;

	/** Updates area on polygons creating point-to-point connection with given UserId */
	// 动态修改某条自定义 Link 所属的 Area + Flags（无需 Tile 重建）；
	// 用途：门锁/解锁时切换 AreaType 以改变代价或阻断。
	NAVIGATIONSYSTEM_API void UpdateNavigationLinkArea(FNavLinkId UserId, uint8 AreaType, uint16 PolyFlags) const;

#if WITH_NAVMESH_SEGMENT_LINKS
	/** Updates area on polygons creating segment-to-segment connection with given UserId */
	NAVIGATIONSYSTEM_API void UpdateSegmentLinkArea(int32 UserId, uint8 AreaType, uint16 PolyFlags) const;

	/** Creates segment link connections for the give tile */
	// Segment Link 依赖邻接 Tile，因此必须在所有 Tile Attach 完后逐 Tile 调这个
	NAVIGATIONSYSTEM_API void ProcessSegmentLinksForTile(FNavTileRef TileRef) const;

	/** Creates segment link connections for the give tile and stores the list of indices skipped to avoid generating duplicate links */
	NAVIGATIONSYSTEM_API void ProcessSegmentLinksForTile(FNavTileRef TileRef, uint32 MaxSkippedNeigborTiles, dtTileRef* OutSkippedNeigborTiles, uint32& OutNumSkippedNeigborTiles) const;
#endif // WITH_NAVMESH_SEGMENT_LINKS

	// ---------- 多边形级访问器 ----------
	/** Retrieves center of the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetPolyCenter(NavNodeRef PolyID, FVector& OutCenter) const;
	/** Retrieves the vertices for the specified polygon. Returns false on error. */
	// 读取 PolyID 对应多边形顶点（UE 坐标）；内部会做 Recast→UE 翻转
	NAVIGATIONSYSTEM_API bool GetPolyVerts(NavNodeRef PolyID, TArray<FVector>& OutVerts) const;
	/** Retrieves a random point inside the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetRandomPointInPoly(NavNodeRef PolyID, FVector& OutPoint) const;
	/** Retrieves the surface area of the specified polygon. Returns 0 on error. */
	NAVIGATIONSYSTEM_API FVector::FReal GetPolySurfaceArea(NavNodeRef PolyID) const;
	/** Retrieves the flags for the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetPolyData(NavNodeRef PolyID, uint16& Flags, uint8& AreaType) const;
	/** Retrieves area ID for the specified polygon. */
	NAVIGATIONSYSTEM_API uint32 GetPolyAreaID(NavNodeRef PolyID) const;
	/** Sets area ID for the specified polygon. */
	NAVIGATIONSYSTEM_API void SetPolyAreaID(NavNodeRef PolyID, uint8 AreaID);
	/** Finds all polys connected with specified one */
	NAVIGATIONSYSTEM_API bool GetPolyNeighbors(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Neighbors) const;
	/** Finds all polys connected with specified one, results expressed as array of NavNodeRefs */
	NAVIGATIONSYSTEM_API bool GetPolyNeighbors(NavNodeRef PolyID, TArray<NavNodeRef>& Neighbors) const;
	/** Finds all polys connected with specified one */
	NAVIGATIONSYSTEM_API bool GetPolyEdges(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Edges) const;
	/** Finds all wall segments for the specified polygon (walls or area borders) */
	// "墙边"：相邻 Poly 不存在 / 被 Filter 拒绝 / 跨 Area 边界 的边
	NAVIGATIONSYSTEM_API bool GetPolyWallSegments(NavNodeRef PolyID, const FNavigationQueryFilter& InQueryFilter, const UObject* QueryOwner, TArray<FNavigationPortalEdge>& OutNeighbors) const;
	/** Finds closest point constrained to given poly */
	NAVIGATIONSYSTEM_API bool GetClosestPointOnPoly(NavNodeRef PolyID, const FVector& TestPt, FVector& PointOnPoly) const;
	/** Decode poly ID into tile index and poly index */
	// PolyID 解码：Detour 将 (saltBits, tileIdx, polyIdx) 打包为 64bit
	NAVIGATIONSYSTEM_API bool GetPolyTileIndex(NavNodeRef PolyID, uint32& PolyIndex, uint32& TileIndex) const;
	/** Decode poly ID into FNavTileRef and poly index */
	NAVIGATIONSYSTEM_API bool GetPolyTileRef(NavNodeRef PolyId, uint32& OutPolyIndex, FNavTileRef& OutTileRef) const;
	/** Retrieves user ID for given offmesh link poly */
	// 5.3 起废弃：老 uint32 接口已无法表达 64bit FNavLinkId
	UE_DEPRECATED(5.3, "Please use GetNavLinkUserId() instead. This function only returns Invalid.")
	uint32 GetLinkUserId(NavNodeRef LinkPolyID) const
	{
		return static_cast<int32>(FNavLinkId::Invalid.GetId());
	}
	NAVIGATIONSYSTEM_API FNavLinkId GetNavLinkUserId(NavNodeRef LinkPolyID) const;
	/** Retrieves start and end point of offmesh link */
	NAVIGATIONSYSTEM_API bool GetLinkEndPoints(NavNodeRef LinkPolyID, FVector& PointA, FVector& PointB) const;
	/** Check if poly is a custom link */
	NAVIGATIONSYSTEM_API bool IsCustomLink(NavNodeRef PolyRef) const;

#if	WITH_NAVMESH_CLUSTER_LINKS
	/** Retrieves bounds of cluster. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetClusterBounds(NavNodeRef ClusterRef, FBox& OutBounds) const;
	NAVIGATIONSYSTEM_API NavNodeRef GetClusterRefFromPolyRef(const NavNodeRef PolyRef) const;
#endif // WITH_NAVMESH_CLUSTER_LINKS

	// 仅取 Tile 索引位（不做完整 decode）；用于快速比较 Poly 是否同 Tile
	uint32 GetTileIndexFromPolyRef(const NavNodeRef PolyRef) const { return DetourNavMesh != NULL ? DetourNavMesh->decodePolyIdTile(PolyRef) : uint32(INDEX_NONE); }

	// Filter 侧 forbidden flag 的读写（封装 dtQueryFilter 的 m_excludeFlags）
	static NAVIGATIONSYSTEM_API uint16 GetFilterForbiddenFlags(const FRecastQueryFilter* Filter);
	static NAVIGATIONSYSTEM_API void SetFilterForbiddenFlags(FRecastQueryFilter* Filter, uint16 ForbiddenFlags);

	// Area 代价变了之后：刷新 NavMeshOwner 上缓存的过滤器等（通常由 UNavArea 编辑时触发）
	NAVIGATIONSYSTEM_API void OnAreaCostChanged();

public:
	// 获取底层 dtNavMesh —— 极少数"真的需要直接调 Detour"的场景使用
	dtNavMesh const* GetRecastMesh() const { return DetourNavMesh; };
	dtNavMesh* GetRecastMesh() { return DetourNavMesh; };
	// 释放 dtNavMesh（含所有 Tile）与 CompressedTileCacheLayers
	NAVIGATIONSYSTEM_API void ReleaseDetourNavMesh();

	// ---------- Tile Cache 层管理（压缩态 Tile，用于 Dynamic 模式快速重建） ----------
	NAVIGATIONSYSTEM_API void RemoveTileCacheLayers(int32 TileX, int32 TileY);
	NAVIGATIONSYSTEM_API void RemoveTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx);
	NAVIGATIONSYSTEM_API void AddTileCacheLayers(int32 TileX, int32 TileY, const TArray<FNavMeshTileData>& Layers);
	NAVIGATIONSYSTEM_API void AddTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx, const FNavMeshTileData& LayerData);
	// "空 Tile 占位"：用于表达"这个 Tile 区域有障碍但没有可走的多边形"
	NAVIGATIONSYSTEM_API void MarkEmptyTileCacheLayers(int32 TileX, int32 TileY);
	NAVIGATIONSYSTEM_API FNavMeshTileData GetTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx) const;
	NAVIGATIONSYSTEM_API TArray<FNavMeshTileData> GetTileCacheLayers(int32 TileX, int32 TileY) const;
	NAVIGATIONSYSTEM_API bool HasTileCacheLayers(int32 TileX, int32 TileY) const;

	/** Assigns recast generated navmesh to this instance.
	 *	@param bOwnData if true from now on this FPImplRecastNavMesh instance will be responsible for this piece 
	 *		of memory
	 */
	// 将外部（通常是生成器）产出的 dtNavMesh 绑定到本 Pimpl；释放旧的并接管新指针
	NAVIGATIONSYSTEM_API void SetRecastMesh(dtNavMesh* NavMesh);

	// 估算整份 NavMesh 占用内存（含 TileCache）
	NAVIGATIONSYSTEM_API SIZE_T GetTotalDataSize() const;

	/** Gets the size of the compressed tile cache, this is slow */
#if !UE_BUILD_SHIPPING
	NAVIGATIONSYSTEM_API int32 GetCompressedTileCacheSize();
#endif

	/** Called on world origin changes */
	// 世界原点位移（大世界）：按 InOffset 修正所有 Tile 坐标；bWorldShift 是否来自 World Composition
	NAVIGATIONSYSTEM_API void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift);

	/** calculated cost of given segment if traversed on specified poly. Function measures distance between specified points
	 *	and returns cost of traversing this distance on given poly.
	 *	@note no check if segment is on poly is performed. */
	// 计算"在指定 Poly 上走 Start→End"的代价（距离 × Area cost + 进入代价）
	NAVIGATIONSYSTEM_API FVector::FReal CalcSegmentCostOnPoly(NavNodeRef PolyID, const dtQueryFilter* Filter, const FVector& StartLoc, const FVector& EndLoc) const;

	// ---------- 成员数据 ----------
	ARecastNavMesh* NavMeshOwner;           // 反向指针：本 Pimpl 绑定的 UE 侧 Actor
	
	/** Recast's runtime navmesh data that we can query against */
	dtNavMesh* DetourNavMesh;               // 底层 Detour NavMesh；所有查询都通过它

	/** Compressed layers data, can be reused for tiles generation */
	// Tile 坐标 → 压缩层列表；Dynamic/Obstacle 重建时复用，避免重新体素化
	TMap<FIntPoint, TArray<FNavMeshTileData> > CompressedTileCacheLayers;

#if RECAST_INTERNAL_DEBUG_DATA
	// Tile 坐标 → 生成中间数据（高度场/Contour 等），仅非 Shipping 构建保留
	TMap<FIntPoint, FRecastInternalDebugData> DebugDataMap;
#endif

	/** query used for searching data on game thread */
	// GameThread 共享的 dtNavMeshQuery；mutable 因为查询本身会改内部栈但概念上是 const 操作
	mutable dtNavMeshQuery SharedNavQuery;

	/** Helper function to serialize a single Recast tile. */
	// 被 URecastNavMeshDataChunk / Serialize 共用；按 NavMeshVersion 适配字段变更
	static NAVIGATIONSYSTEM_API void SerializeRecastMeshTile(FArchive& Ar, int32 NavMeshVersion, unsigned char*& TileData, int32& TileDataSize);

	/** Helper function to serialize a Recast tile compressed data. */
	static NAVIGATIONSYSTEM_API void SerializeCompressedTileCacheData(FArchive& Ar, int32 NavMeshVersion, unsigned char*& CompressedData, int32& CompressedDataSize);

	/** Initialize data for pathfinding
	* @param UnrealStart Start location for the desired path
	* @param UnrealEnd End location for the desired path
	* @param Query Query to use to project start and end position to the navmesh
	* @param Filter Filters which polygons are acceptable to project start and end locations
	* @param RecastStart Outputs the start location snapped to the navmesh
	* @param StartPoly Outputs the navmesh poly at the start location
	* @param RecastEnd Outputs the end location snapped to the navmesh. Equal to UnrealEnd when there's no navmesh at the end location.
	* @param EndPoly Outputs the navmesh poly at the end location. Invalid when there's no navmesh at the end location
	* @param OutPath [optional] When provided, Path will be used to store relevant error codes that this method can produce
	*/
	// 寻路前置：
	//   1) UE 坐标 → Recast 坐标（Unreal2Recast）
	//   2) 对起/止点各做一次 dtNavMeshQuery::findNearestPoly 把位置贴到具体多边形
	//   3) 失败原因可选写入 OutPath->ResultFlags 便于 VisLog
	NAVIGATIONSYSTEM_API bool InitPathfinding(const FVector& UnrealStart, const FVector& UnrealEnd, 
		const dtNavMeshQuery& Query, const dtQueryFilter* Filter,
		FVector& RecastStart, dtPolyRef& StartPoly,
		FVector& RecastEnd, dtPolyRef& EndPoly,
		FNavMeshPath* OutPath = nullptr) const;

	/** Marks path flags, perform string pulling if needed */
	// 寻路后置：
	//   1) 从 PathResult 把 PolyCorridor 拷进 Path.PathCorridor
	//   2) 调 Path.PerformStringPulling（若 bWantsStringPulling）
	//   3) ApplyFlags；填充 CustomNavLinkIds；OffsetFromCorners（若启用）
	NAVIGATIONSYSTEM_API void PostProcessPath(dtStatus PathfindResult, FNavMeshPath& Path,
		const dtNavMeshQuery& Query, const dtQueryFilter* Filter,
		NavNodeRef StartNode, NavNodeRef EndNode,
		FVector UnrealStart, FVector UnrealEnd,
		FVector RecastStart, FVector RecastEnd,
		dtQueryResult& PathResult) const;

	UE_DEPRECATED(5.5, "Use GetTilePolyEdges instead.")
	NAVIGATIONSYSTEM_API void GetDebugPolyEdges(const dtMeshTile& Tile, bool bInternalEdges, bool bNavMeshEdges, TArray<FVector>& InternalEdgeVerts, TArray<FVector>& NavMeshEdgeVerts) const;

	/**
	 * Traverses given tile's edges and detects the ones that are either internal poly (i.e. not triangle, but whole navmesh polygon)
	 * or external navmesh edge. Returns a pair of verts for each edge found.
	 * @param Tile The tile whose edges to traverse.
	 * @param bGatherInteriorPolyEdges If true, populates OutPolyEdgeVerts with the tile's internal poly edges.
	 * @param bGatherExternalNavMeshEdges If true, populates OutNavMeshEdgeVerts with the tile's external navmesh edges.
	 * @param OutInteriorPolyEdgeVerts Output poly edge vertex array. Contains a pair of verts for each edge.
	 * @param OutExteriorNavMeshEdgeVerts Output navmesh edge vertex array. Contains a pair of verts for each edge.
	 * @note This is really slow.
	 */
	// 遍历 Tile 所有边并分类为"多边形内部边"/"NavMesh 边界外边"；开销较高，仅调试用
	NAVIGATIONSYSTEM_API void GetTilePolyEdges(const dtMeshTile& Tile, bool bGatherInteriorPolyEdges, bool bGatherExteriorNavMeshEdges, TArray<FVector>& OutInteriorPolyEdgeVerts, TArray<FVector>& OutExteriorNavMeshEdgeVerts) const;

	/** workhorse function finding portal edges between corridor polys */
	// GetEdgesForPathCorridor 的内部实现；多一个 dtNavMeshQuery 参数便于复用查询栈
	NAVIGATIONSYSTEM_API void GetEdgesForPathCorridorImpl(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges, const dtNavMeshQuery& NavQuery) const;

	/** 
	 * Traverse a tile and returns a list of all the edges around the passed in filter.
	 * @param Tile The tile whose edges to traverse.
	 * @param InQueryFilter the filter used to determine if an edge should be considered a border
	 * @param OutEdges Output edges array
	*/
	// 以 Filter 为界的"墙边"提取——相邻 Poly 若被 Filter 拒绝，此边视作边界。
	// 用途：AIPerception/遮挡规避计算边界 + 可视化"AI 视角下的墙"。
	NAVIGATIONSYSTEM_API void GetTilePolyEdgesForFilter(const dtMeshTile& Tile, const FNavigationQueryFilter& InQueryFilter, TArray<FNavigationWallEdge>& OutEdges) const;

protected:
	/** 
	 *	@param ForbiddenFlags polys with flags matching the filter will get added to 
	 */
	// 被 GetDebugGeometryForTile 调用的底层 Tile→几何提取器
	NAVIGATIONSYSTEM_API int32 GetTilesDebugGeometry(const FRecastNavMeshGenerator* Generator, const dtMeshTile& Tile, int32 VertBase, FRecastDebugGeometry& OutGeometry, int32 TileIdx = INDEX_NONE, uint16 ForbiddenFlags = 0) const;

	// PostProcessPath 的下层实现；把 dtStatus 映射成 ENavigationQueryResult
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type PostProcessPathInternal(dtStatus FindPathStatus, FNavMeshPath& Path, 
		const dtNavMeshQuery& NavQuery, const dtQueryFilter* QueryFilter, 
		NavNodeRef StartPolyID, NavNodeRef EndPolyID, 
		const FVector& RecastStartPos, const FVector& RecastEndPos, 
		dtQueryResult& PathResult) const;
};

// 自定义 A* 实现：把 Detour NavMesh 的邻接图 wrap 为 RecastGraphWrapper，
// 配合任意满足签名的 AStarAlgo 跑寻路；结果仍通过 PostProcessPathInternal 归一成 UE 结果。
template< typename TRecastAStar, typename TRecastAStartGraph, typename TRecastGraphAStarFilter, typename TRecastAStarResult >
ENavigationQueryResult::Type FPImplRecastNavMesh::FindPathCustomAStar(TRecastAStartGraph& RecastGraphWrapper, TRecastAStar& AStarAlgo, const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, FNavMeshPath& Path, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner) const
{
	// UE 过滤器 → Recast 过滤器双身份检查
	const FRecastQueryFilter* FilterImplementation = (const FRecastQueryFilter*)(InQueryFilter.GetImplementation());
	if (FilterImplementation == nullptr)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("FPImplRecastNavMesh::FindPath failed due to passed filter having NULL implementation!"));
		return ENavigationQueryResult::Error;
	}

	const dtQueryFilter* QueryFilter = FilterImplementation->GetAsDetourQueryFilter();
	if (QueryFilter == nullptr)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::FindPath failing due to QueryFilter == NULL"));
		return ENavigationQueryResult::Error;
	}

	// initialize recast wrapper with the NavMeshOwner, not multithread safe!
	// 注意：RecastGraphWrapper.Initialize 非线程安全，须保证独占本 NavMesh
	RecastGraphWrapper.Initialize(NavMeshOwner);
	TRecastGraphAStarFilter AStarFilter(RecastGraphWrapper, *FilterImplementation, InQueryFilter.GetMaxSearchNodes(), CostLimit, Owner);

	FVector RecastStartPos, RecastEndPos;
	NavNodeRef StartPolyID, EndPolyID;
	const bool bCanSearch = InitPathfinding(StartLoc, EndLoc, RecastGraphWrapper.GetRecastQuery(), QueryFilter, RecastStartPos, StartPolyID, RecastEndPos, EndPolyID, &Path);
	if (!bCanSearch)
	{
		return ENavigationQueryResult::Error;
	}

	// 构造 A* 起止节点，运行用户算法
	typename TRecastAStar::FSearchNode StartNode(StartPolyID, RecastStartPos);
	typename TRecastAStar::FSearchNode EndNode(EndPolyID, RecastEndPos);
	StartNode.Initialize(RecastGraphWrapper);
	EndNode.Initialize(RecastGraphWrapper);
	TRecastAStarResult PathResult;
	auto AStarResult = AStarAlgo.FindPath(StartNode, EndNode, AStarFilter, PathResult);

	// 将用户算法结果映射回 dtStatus 并走统一后置处理
	dtStatus FindPathStatus = RecastGraphWrapper.ConvertToRecastStatus(AStarAlgo, AStarFilter, AStarResult);
	return PostProcessPathInternal(FindPathStatus, Path, RecastGraphWrapper.GetRecastQuery(), FilterImplementation, StartNode.NodeRef, EndNode.NodeRef, 
		FVector(StartNode.Position[0], StartNode.Position[1], StartNode.Position[2]), 
		FVector(EndNode.Position[0], EndNode.Position[1], EndNode.Position[2]),
		PathResult);
}

#endif	// WITH_RECAST

#if RECAST_INTERNAL_DEBUG_DATA
#include "NavMesh/RecastInternalDebugData.h"
#endif

#if WITH_RECAST
#include "Detour/DetourNavMesh.h"
#include "Detour/DetourNavMeshQuery.h"
#endif

class FRecastNavMeshGenerator;
struct FNavLinkId;

#if WITH_RECAST

#define RECAST_VERY_SMALL_AGENT_RADIUS 0.0f

/** Engine Private! - Private Implementation details of ARecastNavMesh */
class FPImplRecastNavMesh
{
public:

	/** Constructor */
	NAVIGATIONSYSTEM_API FPImplRecastNavMesh(ARecastNavMesh* Owner);

	/** Dtor */
	NAVIGATIONSYSTEM_API ~FPImplRecastNavMesh();

	/**
	 * Serialization.
	 * @param Ar - The archive with which to serialize.
	 * @returns true if serialization was successful.
	 */
	NAVIGATIONSYSTEM_API void Serialize(FArchive& Ar, int32 NavMeshVersion);

	/* Gather debug geometry.
	 * @params OutGeometry Output geometry.
	 * @params TileIndex Used to collect geometry for a specific tile, INDEX_NONE will gather all tiles.
	 * @return True if done collecting.
	 */
	UE_DEPRECATED(5.5, "Use the version of this function that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API bool GetDebugGeometryForTile(FRecastDebugGeometry& OutGeometry, int32 TileIndex) const;

	/* Gather debug geometry.
	 * @params OutGeometry Output geometry.
	 * @params TileRef Used to collect geometry for a specific tile, an invalid FNavTileRef will gather all tiles.
	 * @return True if done collecting.
	 */
	NAVIGATIONSYSTEM_API bool GetDebugGeometryForTile(FRecastDebugGeometry& OutGeometry, FNavTileRef TileRef) const;
	
	/** Returns bounding box for the whole navmesh. */
	NAVIGATIONSYSTEM_API FBox GetNavMeshBounds() const;

	/** Returns bounding box for a given navmesh tile. */
	UE_DEPRECATED(5.5, "Use the version of this function on ARecastNavMesh that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API FBox GetNavMeshTileBounds(int32 TileIndex) const;

	/** Retrieves XY and layer coordinates of tile specified by index */
	UE_DEPRECATED(5.5, "Use the version of this function on ARecastNavMesh that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API bool GetNavMeshTileXY(int32 TileIndex, int32& OutX, int32& OutY, int32& OutLayer) const;

	/** Retrieves XY coordinates of tile specified by position */
	NAVIGATIONSYSTEM_API bool GetNavMeshTileXY(const FVector& Point, int32& OutX, int32& OutY) const;

	/** Retrieves all tile indices at matching XY coordinates */
	UE_DEPRECATED(5.5, "Use the version of this function on ARecastNavMesh that takes an array of FNavTileRefs instead.")
	NAVIGATIONSYSTEM_API void GetNavMeshTilesAt(int32 TileX, int32 TileY, TArray<int32>& Indices) const;

	/** Retrieves list of tiles that intersect specified bounds */
	UE_DEPRECATED(5.5, "Use the version of the function that takes an array of FNavTileRefs instead")
	NAVIGATIONSYSTEM_API void GetNavMeshTilesIn(const TArray<FBox>& InclusionBounds, TArray<int32>& Indices) const;

	/** Retrieves list of tiles that intersect specified bounds */
	NAVIGATIONSYSTEM_API void GetNavMeshTilesIn(const TArray<FBox>& InclusionBounds, TArray<FNavTileRef>& OutRefs) const;

	/** Retrieves number of tiles in this navmesh */
	inline int32 GetNavMeshTilesCount() const { return DetourNavMesh ? DetourNavMesh->getMaxTiles() : 0; }

	/** Supported queries */

	/** Generates path from the given query. Synchronous. */
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type FindPath(const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, const bool bRequireNavigableEndLocation, FNavMeshPath& Path, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Check if path exists */
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type TestPath(const FVector& StartLoc, const FVector& EndLoc, const bool bRequireNavigableEndLocation, const FNavigationQueryFilter& Filter, const UObject* Owner, int32* NumVisitedNodes = 0) const;

	template< typename TRecastAStar, typename TRecastAStartGraph, typename TRecastGraphAStarFilter, typename TRecastAStarResult >
	ENavigationQueryResult::Type FindPathCustomAStar(TRecastAStartGraph& RecastGraphWrapper, TRecastAStar& AStarAlgo, const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, FNavMeshPath& Path, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Checks if the whole segment is in navmesh */
	NAVIGATIONSYSTEM_API void Raycast(const FVector& StartLoc, const FVector& EndLoc, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner,
		ARecastNavMesh::FRaycastResult& RaycastResult, NavNodeRef StartNode = INVALID_NAVNODEREF) const;

	/** Generates path from given query and collect data for every step of A* algorithm */
	NAVIGATIONSYSTEM_API int32 DebugPathfinding(const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, const bool bRequireNavigableEndLocation, const FNavigationQueryFilter& Filter, const UObject* Owner, TArray<FRecastDebugPathfindingData>& Steps);

	/** Returns a random location on the navmesh. */
	NAVIGATIONSYSTEM_API FNavLocation GetRandomPoint(const FNavigationQueryFilter& Filter, const UObject* Owner) const;

#if WITH_NAVMESH_CLUSTER_LINKS
	/** Check if path exists using cluster graph */
	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type TestClusterPath(const FVector& StartLoc, const FVector& EndLoc, int32* NumVisitedNodes = 0) const;

	/** Returns a random location on the navmesh within cluster */
	NAVIGATIONSYSTEM_API bool GetRandomPointInCluster(NavNodeRef ClusterRef, FNavLocation& OutLocation) const;
#endif // WITH_NAVMESH_CLUSTER_LINKS

	/**	Tries to move current nav location towards target constrained to navigable area. Faster than ProjectPointToNavmesh.
	 *	@param OutLocation if successful this variable will be filed with result
	 *	@return true if successful, false otherwise
	 */
	NAVIGATIONSYSTEM_API bool FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	NAVIGATIONSYSTEM_API bool ProjectPointToNavMesh(const FVector& Point, FNavLocation& Result, const FVector& Extent, const FNavigationQueryFilter& Filter, const UObject* Owner) const;
	
	/** Project single point and grab all vertical intersections */
	NAVIGATIONSYSTEM_API bool ProjectPointMulti(const FVector& Point, TArray<FNavLocation>& OutLocations, const FVector& Extent,
		FVector::FReal MinZ, FVector::FReal MaxZ, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Returns nearest navmesh polygon to Loc, or INVALID_NAVNODEREF if Loc is not on the navmesh. */
	NAVIGATIONSYSTEM_API NavNodeRef FindNearestPoly(FVector const& Loc, FVector const& Extent, const FNavigationQueryFilter& Filter, const UObject* Owner) const;

	/** Finds the polygons along the navigation graph that touch the specified circle. Return true if found any. */
	NAVIGATIONSYSTEM_API bool FindPolysAroundCircle(const FVector& CenterPos, const NavNodeRef CenterNodeRef, const FVector::FReal Radius, const FNavigationQueryFilter& Filter, const UObject* Owner, TArray<NavNodeRef>* OutPolys = nullptr, TArray<NavNodeRef>* OutPolysParent = nullptr, TArray<float>* OutPolysCost = nullptr, int32* OutPolysCount = nullptr) const;

	/** Retrieves all polys within given pathing distance from StartLocation.
	 *	@NOTE query is not using string-pulled path distance (for performance reasons),
	 *		it measured distance between middles of portal edges, do you might want to 
	 *		add an extra margin to PathingDistance */
	NAVIGATIONSYSTEM_API bool GetPolysWithinPathingDistance(FVector const& StartLoc, const FVector::FReal PathingDistance,
		const FNavigationQueryFilter& Filter, const UObject* Owner,
		TArray<NavNodeRef>& FoundPolys, FRecastDebugPathfindingData* DebugData) const;

	//@todo document
	NAVIGATIONSYSTEM_API void GetEdgesForPathCorridor(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges) const;

	/** finds stringpulled path from given corridor */
	NAVIGATIONSYSTEM_API bool FindStraightPath(const FVector& StartLoc, const FVector& EndLoc, const TArray<NavNodeRef>& PathCorridor, TArray<FNavPathPoint>& PathPoints, TArray<FNavLinkId>* CustomLinks = NULL) const;

	/** Filters nav polys in PolyRefs with Filter */
	NAVIGATIONSYSTEM_API bool FilterPolys(TArray<NavNodeRef>& PolyRefs, const FRecastQueryFilter* Filter, const UObject* Owner) const;

	/** Get all polys from tile */
	UE_DEPRECATED(5.5, "Use the version of this function in ARecastNavMesh that takes a FNavTileRef instead.")
	NAVIGATIONSYSTEM_API bool GetPolysInTile(int32 TileIndex, TArray<FNavPoly>& Polys) const;

	/** Updates area on polygons creating point-to-point connection with given UserId */
	NAVIGATIONSYSTEM_API void UpdateNavigationLinkArea(FNavLinkId UserId, uint8 AreaType, uint16 PolyFlags) const;

#if WITH_NAVMESH_SEGMENT_LINKS
	/** Updates area on polygons creating segment-to-segment connection with given UserId */
	NAVIGATIONSYSTEM_API void UpdateSegmentLinkArea(int32 UserId, uint8 AreaType, uint16 PolyFlags) const;

	/** Creates segment link connections for the give tile */
	NAVIGATIONSYSTEM_API void ProcessSegmentLinksForTile(FNavTileRef TileRef) const;

	/** Creates segment link connections for the give tile and stores the list of indices skipped to avoid generating duplicate links */
	NAVIGATIONSYSTEM_API void ProcessSegmentLinksForTile(FNavTileRef TileRef, uint32 MaxSkippedNeigborTiles, dtTileRef* OutSkippedNeigborTiles, uint32& OutNumSkippedNeigborTiles) const;
#endif // WITH_NAVMESH_SEGMENT_LINKS

	/** Retrieves center of the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetPolyCenter(NavNodeRef PolyID, FVector& OutCenter) const;
	/** Retrieves the vertices for the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetPolyVerts(NavNodeRef PolyID, TArray<FVector>& OutVerts) const;
	/** Retrieves a random point inside the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetRandomPointInPoly(NavNodeRef PolyID, FVector& OutPoint) const;
	/** Retrieves the surface area of the specified polygon. Returns 0 on error. */
	NAVIGATIONSYSTEM_API FVector::FReal GetPolySurfaceArea(NavNodeRef PolyID) const;
	/** Retrieves the flags for the specified polygon. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetPolyData(NavNodeRef PolyID, uint16& Flags, uint8& AreaType) const;
	/** Retrieves area ID for the specified polygon. */
	NAVIGATIONSYSTEM_API uint32 GetPolyAreaID(NavNodeRef PolyID) const;
	/** Sets area ID for the specified polygon. */
	NAVIGATIONSYSTEM_API void SetPolyAreaID(NavNodeRef PolyID, uint8 AreaID);
	/** Finds all polys connected with specified one */
	NAVIGATIONSYSTEM_API bool GetPolyNeighbors(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Neighbors) const;
	/** Finds all polys connected with specified one, results expressed as array of NavNodeRefs */
	NAVIGATIONSYSTEM_API bool GetPolyNeighbors(NavNodeRef PolyID, TArray<NavNodeRef>& Neighbors) const;
	/** Finds all polys connected with specified one */
	NAVIGATIONSYSTEM_API bool GetPolyEdges(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Edges) const;
	/** Finds all wall segments for the specified polygon (walls or area borders) */
	NAVIGATIONSYSTEM_API bool GetPolyWallSegments(NavNodeRef PolyID, const FNavigationQueryFilter& InQueryFilter, const UObject* QueryOwner, TArray<FNavigationPortalEdge>& OutNeighbors) const;
	/** Finds closest point constrained to given poly */
	NAVIGATIONSYSTEM_API bool GetClosestPointOnPoly(NavNodeRef PolyID, const FVector& TestPt, FVector& PointOnPoly) const;
	/** Decode poly ID into tile index and poly index */
	NAVIGATIONSYSTEM_API bool GetPolyTileIndex(NavNodeRef PolyID, uint32& PolyIndex, uint32& TileIndex) const;
	/** Decode poly ID into FNavTileRef and poly index */
	NAVIGATIONSYSTEM_API bool GetPolyTileRef(NavNodeRef PolyId, uint32& OutPolyIndex, FNavTileRef& OutTileRef) const;
	/** Retrieves user ID for given offmesh link poly */
	UE_DEPRECATED(5.3, "Please use GetNavLinkUserId() instead. This function only returns Invalid.")
	uint32 GetLinkUserId(NavNodeRef LinkPolyID) const
	{
		return static_cast<int32>(FNavLinkId::Invalid.GetId());
	}
	NAVIGATIONSYSTEM_API FNavLinkId GetNavLinkUserId(NavNodeRef LinkPolyID) const;
	/** Retrieves start and end point of offmesh link */
	NAVIGATIONSYSTEM_API bool GetLinkEndPoints(NavNodeRef LinkPolyID, FVector& PointA, FVector& PointB) const;
	/** Check if poly is a custom link */
	NAVIGATIONSYSTEM_API bool IsCustomLink(NavNodeRef PolyRef) const;

#if	WITH_NAVMESH_CLUSTER_LINKS
	/** Retrieves bounds of cluster. Returns false on error. */
	NAVIGATIONSYSTEM_API bool GetClusterBounds(NavNodeRef ClusterRef, FBox& OutBounds) const;
	NAVIGATIONSYSTEM_API NavNodeRef GetClusterRefFromPolyRef(const NavNodeRef PolyRef) const;
#endif // WITH_NAVMESH_CLUSTER_LINKS

	uint32 GetTileIndexFromPolyRef(const NavNodeRef PolyRef) const { return DetourNavMesh != NULL ? DetourNavMesh->decodePolyIdTile(PolyRef) : uint32(INDEX_NONE); }

	static NAVIGATIONSYSTEM_API uint16 GetFilterForbiddenFlags(const FRecastQueryFilter* Filter);
	static NAVIGATIONSYSTEM_API void SetFilterForbiddenFlags(FRecastQueryFilter* Filter, uint16 ForbiddenFlags);

	NAVIGATIONSYSTEM_API void OnAreaCostChanged();

public:
	dtNavMesh const* GetRecastMesh() const { return DetourNavMesh; };
	dtNavMesh* GetRecastMesh() { return DetourNavMesh; };
	NAVIGATIONSYSTEM_API void ReleaseDetourNavMesh();

	NAVIGATIONSYSTEM_API void RemoveTileCacheLayers(int32 TileX, int32 TileY);
	NAVIGATIONSYSTEM_API void RemoveTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx);
	NAVIGATIONSYSTEM_API void AddTileCacheLayers(int32 TileX, int32 TileY, const TArray<FNavMeshTileData>& Layers);
	NAVIGATIONSYSTEM_API void AddTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx, const FNavMeshTileData& LayerData);
	NAVIGATIONSYSTEM_API void MarkEmptyTileCacheLayers(int32 TileX, int32 TileY);
	NAVIGATIONSYSTEM_API FNavMeshTileData GetTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx) const;
	NAVIGATIONSYSTEM_API TArray<FNavMeshTileData> GetTileCacheLayers(int32 TileX, int32 TileY) const;
	NAVIGATIONSYSTEM_API bool HasTileCacheLayers(int32 TileX, int32 TileY) const;

	/** Assigns recast generated navmesh to this instance.
	 *	@param bOwnData if true from now on this FPImplRecastNavMesh instance will be responsible for this piece 
	 *		of memory
	 */
	NAVIGATIONSYSTEM_API void SetRecastMesh(dtNavMesh* NavMesh);

	NAVIGATIONSYSTEM_API SIZE_T GetTotalDataSize() const;

	/** Gets the size of the compressed tile cache, this is slow */
#if !UE_BUILD_SHIPPING
	NAVIGATIONSYSTEM_API int32 GetCompressedTileCacheSize();
#endif

	/** Called on world origin changes */
	NAVIGATIONSYSTEM_API void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift);

	/** calculated cost of given segment if traversed on specified poly. Function measures distance between specified points
	 *	and returns cost of traversing this distance on given poly.
	 *	@note no check if segment is on poly is performed. */
	NAVIGATIONSYSTEM_API FVector::FReal CalcSegmentCostOnPoly(NavNodeRef PolyID, const dtQueryFilter* Filter, const FVector& StartLoc, const FVector& EndLoc) const;

	ARecastNavMesh* NavMeshOwner;
	
	/** Recast's runtime navmesh data that we can query against */
	dtNavMesh* DetourNavMesh;

	/** Compressed layers data, can be reused for tiles generation */
	TMap<FIntPoint, TArray<FNavMeshTileData> > CompressedTileCacheLayers;

#if RECAST_INTERNAL_DEBUG_DATA
	TMap<FIntPoint, FRecastInternalDebugData> DebugDataMap;
#endif

	/** query used for searching data on game thread */
	mutable dtNavMeshQuery SharedNavQuery;

	/** Helper function to serialize a single Recast tile. */
	static NAVIGATIONSYSTEM_API void SerializeRecastMeshTile(FArchive& Ar, int32 NavMeshVersion, unsigned char*& TileData, int32& TileDataSize);

	/** Helper function to serialize a Recast tile compressed data. */
	static NAVIGATIONSYSTEM_API void SerializeCompressedTileCacheData(FArchive& Ar, int32 NavMeshVersion, unsigned char*& CompressedData, int32& CompressedDataSize);

	/** Initialize data for pathfinding
	* @param UnrealStart Start location for the desired path
	* @param UnrealEnd End location for the desired path
	* @param Query Query to use to project start and end position to the navmesh
	* @param Filter Filters which polygons are acceptable to project start and end locations
	* @param RecastStart Outputs the start location snapped to the navmesh
	* @param StartPoly Outputs the navmesh poly at the start location
	* @param RecastEnd Outputs the end location snapped to the navmesh. Equal to UnrealEnd when there's no navmesh at the end location.
	* @param EndPoly Outputs the navmesh poly at the end location. Invalid when there's no navmesh at the end location
	* @param OutPath [optional] When provided, Path will be used to store relevant error codes that this method can produce
	*/
	NAVIGATIONSYSTEM_API bool InitPathfinding(const FVector& UnrealStart, const FVector& UnrealEnd, 
		const dtNavMeshQuery& Query, const dtQueryFilter* Filter,
		FVector& RecastStart, dtPolyRef& StartPoly,
		FVector& RecastEnd, dtPolyRef& EndPoly,
		FNavMeshPath* OutPath = nullptr) const;

	/** Marks path flags, perform string pulling if needed */
	NAVIGATIONSYSTEM_API void PostProcessPath(dtStatus PathfindResult, FNavMeshPath& Path,
		const dtNavMeshQuery& Query, const dtQueryFilter* Filter,
		NavNodeRef StartNode, NavNodeRef EndNode,
		FVector UnrealStart, FVector UnrealEnd,
		FVector RecastStart, FVector RecastEnd,
		dtQueryResult& PathResult) const;

	UE_DEPRECATED(5.5, "Use GetTilePolyEdges instead.")
	NAVIGATIONSYSTEM_API void GetDebugPolyEdges(const dtMeshTile& Tile, bool bInternalEdges, bool bNavMeshEdges, TArray<FVector>& InternalEdgeVerts, TArray<FVector>& NavMeshEdgeVerts) const;

	/**
	 * Traverses given tile's edges and detects the ones that are either internal poly (i.e. not triangle, but whole navmesh polygon)
	 * or external navmesh edge. Returns a pair of verts for each edge found.
	 * @param Tile The tile whose edges to traverse.
	 * @param bGatherInteriorPolyEdges If true, populates OutPolyEdgeVerts with the tile's internal poly edges.
	 * @param bGatherExternalNavMeshEdges If true, populates OutNavMeshEdgeVerts with the tile's external navmesh edges.
	 * @param OutInteriorPolyEdgeVerts Output poly edge vertex array. Contains a pair of verts for each edge.
	 * @param OutExteriorNavMeshEdgeVerts Output navmesh edge vertex array. Contains a pair of verts for each edge.
	 * @note This is really slow.
	 */
	NAVIGATIONSYSTEM_API void GetTilePolyEdges(const dtMeshTile& Tile, bool bGatherInteriorPolyEdges, bool bGatherExteriorNavMeshEdges, TArray<FVector>& OutInteriorPolyEdgeVerts, TArray<FVector>& OutExteriorNavMeshEdgeVerts) const;

	/** workhorse function finding portal edges between corridor polys */
	NAVIGATIONSYSTEM_API void GetEdgesForPathCorridorImpl(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges, const dtNavMeshQuery& NavQuery) const;

	/** 
	 * Traverse a tile and returns a list of all the edges around the passed in filter.
	 * @param Tile The tile whose edges to traverse.
	 * @param InQueryFilter the filter used to determine if an edge should be considered a border
	 * @param OutEdges Output edges array
	*/
	NAVIGATIONSYSTEM_API void GetTilePolyEdgesForFilter(const dtMeshTile& Tile, const FNavigationQueryFilter& InQueryFilter, TArray<FNavigationWallEdge>& OutEdges) const;

protected:
	/** 
	 *	@param ForbiddenFlags polys with flags matching the filter will get added to 
	 */
	NAVIGATIONSYSTEM_API int32 GetTilesDebugGeometry(const FRecastNavMeshGenerator* Generator, const dtMeshTile& Tile, int32 VertBase, FRecastDebugGeometry& OutGeometry, int32 TileIdx = INDEX_NONE, uint16 ForbiddenFlags = 0) const;

	NAVIGATIONSYSTEM_API ENavigationQueryResult::Type PostProcessPathInternal(dtStatus FindPathStatus, FNavMeshPath& Path, 
		const dtNavMeshQuery& NavQuery, const dtQueryFilter* QueryFilter, 
		NavNodeRef StartPolyID, NavNodeRef EndPolyID, 
		const FVector& RecastStartPos, const FVector& RecastEndPos, 
		dtQueryResult& PathResult) const;
};

template< typename TRecastAStar, typename TRecastAStartGraph, typename TRecastGraphAStarFilter, typename TRecastAStarResult >
ENavigationQueryResult::Type FPImplRecastNavMesh::FindPathCustomAStar(TRecastAStartGraph& RecastGraphWrapper, TRecastAStar& AStarAlgo, const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, FNavMeshPath& Path, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner) const
{
	const FRecastQueryFilter* FilterImplementation = (const FRecastQueryFilter*)(InQueryFilter.GetImplementation());
	if (FilterImplementation == nullptr)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("FPImplRecastNavMesh::FindPath failed due to passed filter having NULL implementation!"));
		return ENavigationQueryResult::Error;
	}

	const dtQueryFilter* QueryFilter = FilterImplementation->GetAsDetourQueryFilter();
	if (QueryFilter == nullptr)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::FindPath failing due to QueryFilter == NULL"));
		return ENavigationQueryResult::Error;
	}

	// initialize recast wrapper with the NavMeshOwner, not multithread safe!
	RecastGraphWrapper.Initialize(NavMeshOwner);
	TRecastGraphAStarFilter AStarFilter(RecastGraphWrapper, *FilterImplementation, InQueryFilter.GetMaxSearchNodes(), CostLimit, Owner);

	FVector RecastStartPos, RecastEndPos;
	NavNodeRef StartPolyID, EndPolyID;
	const bool bCanSearch = InitPathfinding(StartLoc, EndLoc, RecastGraphWrapper.GetRecastQuery(), QueryFilter, RecastStartPos, StartPolyID, RecastEndPos, EndPolyID, &Path);
	if (!bCanSearch)
	{
		return ENavigationQueryResult::Error;
	}

	typename TRecastAStar::FSearchNode StartNode(StartPolyID, RecastStartPos);
	typename TRecastAStar::FSearchNode EndNode(EndPolyID, RecastEndPos);
	StartNode.Initialize(RecastGraphWrapper);
	EndNode.Initialize(RecastGraphWrapper);
	TRecastAStarResult PathResult;
	auto AStarResult = AStarAlgo.FindPath(StartNode, EndNode, AStarFilter, PathResult);

	dtStatus FindPathStatus = RecastGraphWrapper.ConvertToRecastStatus(AStarAlgo, AStarFilter, AStarResult);
	return PostProcessPathInternal(FindPathStatus, Path, RecastGraphWrapper.GetRecastQuery(), FilterImplementation, StartNode.NodeRef, EndNode.NodeRef, 
		FVector(StartNode.Position[0], StartNode.Position[1], StartNode.Position[2]), 
		FVector(EndNode.Position[0], EndNode.Position[1], EndNode.Position[2]),
		PathResult);
}

#endif	// WITH_RECAST
