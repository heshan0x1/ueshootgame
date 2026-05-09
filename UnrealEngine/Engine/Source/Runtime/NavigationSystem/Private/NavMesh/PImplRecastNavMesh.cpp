// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// 文件概览：PImplRecastNavMesh.cpp
// ----------------------------------------------------------------------------
// FPImplRecastNavMesh 的实现（~4000 行，本模块最密集的 Detour 胶水层）。
// 按功能分组：
//
//   1. 生命周期 / 序列化
//      - ReleaseDetourNavMesh：释放 dtNavMesh + TileCache
//      - Serialize：整块 NavMesh 序列化（Header + Params + 每 Tile 二进制）
//      - SerializeRecastMeshTile / SerializeCompressedTileCacheData：
//        单 Tile / Cache 层字节流的版本化读写，供 URecastNavMeshDataChunk 复用
//      - SetRecastMesh：替换底层 dtNavMesh，旧的先 ReleaseDetourNavMesh
//
//   2. 寻路核心（§4.4）
//      - InitPathfinding：UE 坐标→Recast 坐标，Start/End 贴到 Poly
//      - FindPath：InitPathfinding → dtNavMeshQuery::findPath → PostProcessPath
//      - TestPath：仅可达性，轻量版 FindPath
//      - PostProcessPath：拉绳、ApplyFlags、填 CustomNavLinkIds、OffsetFromCorners
//      - FindStraightPath：findStraightPath 的 UE 封装（§4.4 拉绳阶段）
//      - DebugPathfinding：每步记录 A* 节点的调试版
//
//   3. Raycast / Projection / Query
//      - Raycast：跨 Tile 沿线扫描，填 FRaycastResult.CorridorPolys
//      - ProjectPointToNavMesh / ProjectPointMulti / FindNearestPoly
//      - FindMoveAlongSurface / FindPolysAroundCircle / GetPolysWithinPathingDistance
//      - GetRandomPoint / GetRandomPointInPoly / GetRandomPointInCluster
//
//   4. 多边形级访问
//      - GetPolyCenter / Verts / Edges / Neighbors / WallSegments / ClosestPoint
//      - GetPolyAreaID / SetPolyAreaID / GetPolyData / GetPolyTileIndex
//      - GetLinkEndPoints / IsCustomLink / GetNavLinkUserId
//
//   5. Tile Cache 管理（动态 NavMesh 用）
//      - AddTileCacheLayer(s) / RemoveTileCacheLayer(s) / GetTileCacheLayer(s)
//      - MarkEmptyTileCacheLayers：标记"已重建但空"（防止再次重建无效 Tile）
//
//   6. 调试
//      - GetDebugGeometryForTile / GetTilesDebugGeometry
//      - GetTilePolyEdges / GetTilePolyEdgesForFilter
//
// 线程约束：INITIALIZE_NAVQUERY_* 系列宏统一处理 GameThread vs 其它线程——
//   GameThread 复用 SharedNavQuery 节省栈分配；后台线程每次用栈上新对象避免竞争。
// 坐标转换：所有进入 Detour 的 FVector 都要过 Unreal2Recast*，出来的要用
//   Recast2Unreal*。违反会得到"看起来位置正确但 Tile 找不到"的怪异 Bug。
// ============================================================================

#include "NavMesh/PImplRecastNavMesh.h"
#include "NavigationSystem.h"
#include "TransactionCommon.h"

#if WITH_RECAST

#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastVersion.h"
#include "Detour/DetourNode.h"
#include "Detour/DetourNavMesh.h"
#include "Recast/RecastAlloc.h"
#include "DetourTileCache/DetourTileCacheBuilder.h"
#include "NavAreas/NavArea.h"
#include "NavMesh/RecastNavMeshGenerator.h"
#include "NavMesh/RecastQueryFilter.h"
#include "NavLinkCustomInterface.h"
#include "VisualLogger/VisualLogger.h"

#include "Misc/LargeWorldCoordinates.h"
#include "DebugUtils/DebugDrawLargeWorldCoordinates.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

//----------------------------------------------------------------------//
// bunch of compile-time checks to assure types used by Recast and our
// mid-layer are the same size
//----------------------------------------------------------------------//
static_assert(sizeof(NavNodeRef) == sizeof(dtPolyRef), "NavNodeRef and dtPolyRef should be the same size.");
static_assert(RECAST_MAX_AREAS <= DT_MAX_AREAS, "Number of allowed areas cannot exceed DT_MAX_AREAS.");
static_assert(RECAST_STRAIGHTPATH_OFFMESH_CONNECTION == DT_STRAIGHTPATH_OFFMESH_CONNECTION, "Path flags values differ.");
static_assert(RECAST_UNWALKABLE_POLY_COST == DT_UNWALKABLE_POLY_COST, "Unwalkable poly cost differ.");
static_assert(std::is_same_v<FVector::FReal, dtReal>, "FReal and dtReal must be the same type!");
static_assert(std::is_same_v<FVector::FReal, rcReal>, "FReal and rcReal must be the same type!");
static_assert(std::is_same_v<FVector::FReal, duReal>, "FReal and duReal must be the same type!");

/// Helper for accessing navigation query from different threads
#define INITIALIZE_NAVQUERY_SIMPLE(NavQueryVariable, NumNodes)	\
	dtNavMeshQuery NavQueryVariable##Private;	\
	dtNavMeshQuery& NavQueryVariable = IsInGameThread() ? SharedNavQuery : NavQueryVariable##Private; \
	NavQueryVariable.init(DetourNavMesh, NumNodes);

#define INITIALIZE_NAVQUERY(NavQueryVariable, NumNodes, LinkFilter)	\
	dtNavMeshQuery NavQueryVariable##Private;	\
	dtNavMeshQuery& NavQueryVariable = IsInGameThread() ? SharedNavQuery : NavQueryVariable##Private; \
	NavQueryVariable.init(DetourNavMesh, NumNodes, &LinkFilter);

static void* DetourMalloc(int Size, dtAllocHint Hint)
{
	LLM_SCOPE(ELLMTag::NavigationRecast);
	void* Result = FMemory::Malloc(uint32(Size));
#if STATS
	const SIZE_T ActualSize = FMemory::GetAllocSize(Result);

	switch (Hint)
	{
	case DT_ALLOC_TEMP:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourTEMP, ActualSize);
		break;

	case DT_ALLOC_PERM_AVOIDANCE:				
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_AVOIDANCE, ActualSize);
		break;

	case DT_ALLOC_PERM_CROWD:					
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_CROWD, ActualSize);
		break;

	case DT_ALLOC_PERM_LOOKUP:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_LOOKUP, ActualSize);
		break;

	case DT_ALLOC_PERM_NAVQUERY:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_NAVQUERY, ActualSize);
		break;

	case DT_ALLOC_PERM_NAVMESH:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_NAVMESH, ActualSize);
		break;

	case DT_ALLOC_PERM_NODE_POOL:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_NODE_POOL, ActualSize);
		break;

	case DT_ALLOC_PERM_PATH_CORRIDOR:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_PATH_CORRIDOR, ActualSize);
		break;

	case DT_ALLOC_PERM_PATH_QUEUE:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_PATH_QUEUE, ActualSize);
		break;

	case DT_ALLOC_PERM_PROXIMITY_GRID:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_PROXY_GRID, ActualSize);
		break;

	case DT_ALLOC_PERM_TILE_DATA:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_DATA, ActualSize);
		break;

	case DT_ALLOC_PERM_TILE_DYNLINK_OFFMESH:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_DYNLINK_OFFMESH, ActualSize);
		break;

	case DT_ALLOC_PERM_TILE_DYNLINK_CLUSTER:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_DYNLINK_CLUSTER, ActualSize);
		break;

	case DT_ALLOC_PERM_TILES:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILES, ActualSize);
		break;

	case DT_ALLOC_PERM_TILE_LINK_BUILDER:
		INC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_LINK_BUILDER, ActualSize);
		break;
		
	default:
		ensureMsgf(false, TEXT("Unsupported allocation hint %d"), Hint);
		break;
	}

	INC_DWORD_STAT_BY(STAT_NavigationMemory, ActualSize);
	INC_MEMORY_STAT_BY(STAT_Navigation_RecastMemory, ActualSize);
#endif // STATS
	return Result;
}

static void* RecastMalloc(int Size, rcAllocHint)
{
	LLM_SCOPE(ELLMTag::NavigationRecast);
	void* Result = FMemory::Malloc(uint32(Size));
#if STATS
	const SIZE_T ActualSize = FMemory::GetAllocSize(Result);
	INC_DWORD_STAT_BY(STAT_NavigationMemory, ActualSize);
	INC_MEMORY_STAT_BY(STAT_Navigation_RecastMemory, ActualSize);
#endif // STATS
	return Result;
}

static void DetourFree(void* Original, dtAllocHint Hint)
{
#if STATS
	const SIZE_T Size = FMemory::GetAllocSize(Original);

	switch (Hint)
	{
	case DT_ALLOC_TEMP:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourTEMP, Size);
		break;

	case DT_ALLOC_PERM_AVOIDANCE:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_AVOIDANCE, Size);
		break;

	case DT_ALLOC_PERM_CROWD:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_CROWD, Size);
		break;

	case DT_ALLOC_PERM_LOOKUP:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_LOOKUP, Size);
		break;

	case DT_ALLOC_PERM_NAVQUERY:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_NAVQUERY, Size);
		break;

	case DT_ALLOC_PERM_NAVMESH:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_NAVMESH, Size);
		break;

	case DT_ALLOC_PERM_NODE_POOL:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_NODE_POOL, Size);
		break;

	case DT_ALLOC_PERM_PATH_CORRIDOR:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_PATH_CORRIDOR, Size);
		break;

	case DT_ALLOC_PERM_PATH_QUEUE:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_PATH_QUEUE, Size);
		break;

	case DT_ALLOC_PERM_PROXIMITY_GRID:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_PROXY_GRID, Size);
		break;
	
	case DT_ALLOC_PERM_TILE_DATA:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_DATA, Size);
		break;

	case DT_ALLOC_PERM_TILE_DYNLINK_OFFMESH:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_DYNLINK_OFFMESH, Size);
		break;

	case DT_ALLOC_PERM_TILE_DYNLINK_CLUSTER:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_DYNLINK_CLUSTER, Size);
		break;

	case DT_ALLOC_PERM_TILES:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILES, Size);
		break;

	case DT_ALLOC_PERM_TILE_LINK_BUILDER:
		DEC_MEMORY_STAT_BY(STAT_Navigation_DetourPERM_TILE_LINK_BUILDER, Size);
		break;
		
	default:
		ensureMsgf(false, TEXT("Unsupported allocation hint %d"), Hint);
		break;
	}

	DEC_DWORD_STAT_BY(STAT_NavigationMemory, Size);
	DEC_MEMORY_STAT_BY(STAT_Navigation_RecastMemory, Size);
#endif // STATS

	FMemory::Free(Original);
}

static void RecastFree(void* Original)
{
#if STATS
	const SIZE_T Size = FMemory::GetAllocSize(Original);
	DEC_DWORD_STAT_BY(STAT_NavigationMemory, Size);
	DEC_MEMORY_STAT_BY(STAT_Navigation_RecastMemory, Size);
#endif // STATS
	FMemory::Free(Original);
}

static void DetourStatsPostAddTile(const dtMeshTile& TileAdded)
{
	FDetourTileLayout TileLayout(TileAdded);

	INC_MEMORY_STAT_BY(STAT_DetourTileMemory, TileLayout.TileSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileMeshHeaderMemory, TileLayout.HeaderSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileNavVertsMemory, TileLayout.VertsSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileNavPolysMemory, TileLayout.PolysSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileLinksMemory, TileLayout.LinksSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileDetailMeshesMemory, TileLayout.DetailMeshesSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileDetailVertsMemory, TileLayout.DetailVertsSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileDetailTrisMemory, TileLayout.DetailTrisSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileBVTreeMemory, TileLayout.BvTreeSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileOffMeshConsMemory, TileLayout.OffMeshConsSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileOffMeshSegsMemory, TileLayout.OffMeshSegsSize);
	INC_MEMORY_STAT_BY(STAT_DetourTileClustersMemory, TileLayout.ClustersSize);
	INC_MEMORY_STAT_BY(STAT_DetourTilePolyClustersMemory, TileLayout.PolyClustersSize);
}

static void DetourStatsPreRemoveTile(const dtMeshTile& TileRemoving)
{
	FDetourTileLayout TileLayout(TileRemoving);

	DEC_MEMORY_STAT_BY(STAT_DetourTileMemory, TileLayout.TileSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileMeshHeaderMemory, TileLayout.HeaderSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileNavVertsMemory, TileLayout.VertsSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileNavPolysMemory, TileLayout.PolysSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileLinksMemory, TileLayout.LinksSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileDetailMeshesMemory, TileLayout.DetailMeshesSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileDetailVertsMemory, TileLayout.DetailVertsSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileDetailTrisMemory, TileLayout.DetailTrisSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileBVTreeMemory, TileLayout.BvTreeSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileOffMeshConsMemory, TileLayout.OffMeshConsSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileOffMeshSegsMemory, TileLayout.OffMeshSegsSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTileClustersMemory, TileLayout.ClustersSize);
	DEC_MEMORY_STAT_BY(STAT_DetourTilePolyClustersMemory, TileLayout.PolyClustersSize);
}

struct FRecastInitialSetup
{
	FRecastInitialSetup()
	{
		dtAllocSetCustom(DetourMalloc, DetourFree);
		rcAllocSetCustom(RecastMalloc, RecastFree);

		dtStatsSetCustom(DetourStatsPostAddTile, DetourStatsPreRemoveTile);
	}
};
static FRecastInitialSetup RecastSetup;



/****************************
 * helpers
 ****************************/

static void Unr2RecastVector(FVector const& V, FVector::FReal* R)
{
	// @todo: speed this up with axis swaps instead of a full transform?
	FVector const RecastV = Unreal2RecastPoint(V);
	R[0] = RecastV.X;
	R[1] = RecastV.Y;
	R[2] = RecastV.Z;
}

static void Unr2RecastSizeVector(FVector const& V, FVector::FReal* R)
{
	// @todo: speed this up with axis swaps instead of a full transform?
	FVector const RecastVAbs = Unreal2RecastPoint(V).GetAbs();
	R[0] = RecastVAbs.X;
	R[1] = RecastVAbs.Y;
	R[2] = RecastVAbs.Z;
}

static FVector Recast2UnrVector(FVector::FReal const* R)
{
	return Recast2UnrealPoint(R);
}

ENavigationQueryResult::Type DTStatusToNavQueryResult(dtStatus Status)
{
	// @todo look at possible dtStatus values (in DetourStatus.h), there's more data that can be retrieved from it

	// Partial paths are treated by Recast as Success while we treat as fail
	return dtStatusSucceed(Status) ? (dtStatusDetail(Status, DT_PARTIAL_RESULT) ? ENavigationQueryResult::Fail : ENavigationQueryResult::Success)
		: (dtStatusDetail(Status, DT_INVALID_PARAM) ? ENavigationQueryResult::Error : ENavigationQueryResult::Fail);
}

//----------------------------------------------------------------------//
// FRecastQueryFilter();
//----------------------------------------------------------------------//

FRecastQueryFilter::FRecastQueryFilter(bool bIsVirtual)
	: dtQueryFilter(bIsVirtual)
{
	SetExcludedArea(RECAST_NULL_AREA);
}

INavigationQueryFilterInterface* FRecastQueryFilter::CreateCopy() const 
{
	return new FRecastQueryFilter(*this);
}

void FRecastQueryFilter::SetIsVirtual(bool bIsVirtual)
{
	isVirtual = bIsVirtual;
}

void FRecastQueryFilter::Reset()
{
	// resetting just the cost data, we don't want to override the vf table like we did before (UE-95704)
	new(&data)dtQueryFilterData();
	SetExcludedArea(RECAST_NULL_AREA);
}

void FRecastQueryFilter::SetAreaCost(uint8 AreaType, float Cost)
{
	setAreaCost(AreaType, Cost);
}

void FRecastQueryFilter::SetFixedAreaEnteringCost(uint8 AreaType, float Cost) 
{
#if WITH_FIXED_AREA_ENTERING_COST
	setAreaFixedCost(AreaType, Cost);
#endif // WITH_FIXED_AREA_ENTERING_COST
}

void FRecastQueryFilter::SetExcludedArea(uint8 AreaType)
{
	setAreaCost(AreaType, DT_UNWALKABLE_POLY_COST);
}

void FRecastQueryFilter::SetAllAreaCosts(const float* CostArray, const int32 Count) 
{
	// @todo could get away with memcopying to m_areaCost, but it's private and would require a little hack
	// need to consider if it's wort a try (not sure there'll be any perf gain)
	if (Count > RECAST_MAX_AREAS)
	{
		UE_LOG(LogNavigation, Warning, TEXT("FRecastQueryFilter: Trying to set cost to more areas than allowed! Discarding redundant values."));
	}

	const int32 ElementsCount = FPlatformMath::Min(Count, RECAST_MAX_AREAS);
	for (int32 i = 0; i < ElementsCount; ++i)
	{
		setAreaCost(i, CostArray[i]);
	}
}

void FRecastQueryFilter::GetAllAreaCosts(float* CostArray, float* FixedCostArray, const int32 Count) const
{
	const FVector::FReal* DetourCosts = getAllAreaCosts();
	const FVector::FReal* DetourFixedCosts = getAllFixedAreaCosts();
	const int32 NumItems = FMath::Min(Count, RECAST_MAX_AREAS);

	for (int i = 0; i < NumItems; ++i)
	{
		CostArray[i] = UE_REAL_TO_FLOAT_CLAMPED(DetourCosts[i]);
		FixedCostArray[i] = UE_REAL_TO_FLOAT_CLAMPED(DetourFixedCosts[i]);
	}
}

void FRecastQueryFilter::SetBacktrackingEnabled(const bool bBacktracking)
{
	setIsBacktracking(bBacktracking);
}

void FRecastQueryFilter::SetShouldIgnoreClosedNodes(const bool bIgnoreClosed)
{
	setShouldIgnoreClosedNodes(bIgnoreClosed);
}

bool FRecastQueryFilter::IsBacktrackingEnabled() const
{
	return getIsBacktracking();
}

float FRecastQueryFilter::GetHeuristicScale() const
{
	return UE_REAL_TO_FLOAT(getHeuristicScale());
}

bool FRecastQueryFilter::IsEqual(const INavigationQueryFilterInterface* Other) const
{
	// @NOTE: not type safe, should be changed when another filter type is introduced
	return FMemory::Memcmp(this, Other, sizeof(FRecastQueryFilter)) == 0; //-V598
}

void FRecastQueryFilter::SetIncludeFlags(uint16 Flags)
{
	setIncludeFlags(Flags);
}

uint16 FRecastQueryFilter::GetIncludeFlags() const
{
	return getIncludeFlags();
}

void FRecastQueryFilter::SetExcludeFlags(uint16 Flags)
{
	setExcludeFlags(Flags);
}

uint16 FRecastQueryFilter::GetExcludeFlags() const
{
	return getExcludeFlags();
}

bool FRecastSpeciaLinkFilter::isLinkAllowed(const uint64 UserId) const
{
	const INavLinkCustomInterface* CustomLink = NavSys ? NavSys->GetCustomLink(FNavLinkId(UserId)) : nullptr;
	return (CustomLink != NULL) && CustomLink->IsLinkPathfindingAllowed(CachedOwnerOb);
}

void FRecastSpeciaLinkFilter::initialize()
{
	CachedOwnerOb = SearchOwner.Get();
}

//----------------------------------------------------------------------//
// FPImplRecastNavMesh
//----------------------------------------------------------------------//

FPImplRecastNavMesh::FPImplRecastNavMesh(ARecastNavMesh* Owner)
	: NavMeshOwner(Owner)
	, DetourNavMesh(NULL)
{
	check(Owner && "Owner must never be NULL");

	INC_DWORD_STAT_BY( STAT_NavigationMemory
		, Owner->HasAnyFlags(RF_ClassDefaultObject) == false ? sizeof(*this) : 0 );
};

FPImplRecastNavMesh::~FPImplRecastNavMesh()
{
	ReleaseDetourNavMesh();

	DEC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );
};

// 释放 Detour NavMesh 及其相关资源。
// 调用 dtFreeNavMesh 释放 DetourNavMesh 内存（前提是当前对象持有所有权），
// 同时清空压缩 Tile 缓存层与（可选的）调试数据映射。
void FPImplRecastNavMesh::ReleaseDetourNavMesh()
{
	// release navmesh only if we own it
	if (DetourNavMesh != nullptr)
	{
		dtFreeNavMesh(DetourNavMesh);
	}
	DetourNavMesh = nullptr;
	
	CompressedTileCacheLayers.Empty();

#if RECAST_INTERNAL_DEBUG_DATA
	DebugDataMap.Empty();
#endif
}

/**
 * Serialization.
 * @param Ar - The archive with which to serialize.
 * @returns true if serialization was successful.
 */
// Serialize：把 dtNavMesh 与其所有 Tile 写入/读出 FArchive。
// 过程：
//   - Loading：先 ReleaseDetourNavMesh 再 dtAllocNavMesh 分配空壳，接着读入
//     Params（tileWidth/Height/原点/Poly 位宽等），再读入 NumTiles 个 Tile 二进制
//     （交给 SerializeRecastMeshTile）；每读一个 Tile 就 dtNavMesh::addTile 挂上去。
//   - Saving：枚举 TilesToSave（通常限制在"当前 Level 对应 NavBounds 内的 Tile"，
//     子关卡流式时只写它负责的那些，避免重复），逐个写入。
// 特例：
//   - NavMeshOwner->bIsWorldPartitioned：所有 Tile 走 ANavigationDataChunkActor，
//     本函数不写 Tile，只写 Params。
//   - Ar.IsTransacting：Undo/Redo 不写 Tile（太大），只写结构性元数据。
//   - bCompileRecast=false 的版本：读到 Recast 数据应跳过；由上层版本号判断处理。
void FPImplRecastNavMesh::Serialize( FArchive& Ar, int32 NavMeshVersion )
{
	//@todo: How to handle loading nav meshes saved w/ recast when recast isn't present????

	if (!Ar.IsLoading() && DetourNavMesh == NULL)
	{
		// nothing to write
		return;
	}
	
	// All we really need to do is read/write the data blob for each tile

	if (Ar.IsLoading())
	{
		// allocate the navmesh object
		// 加载：重置底层 dtNavMesh 再重新分配空壳
		ReleaseDetourNavMesh();
		DetourNavMesh = dtAllocNavMesh();

		if (DetourNavMesh == NULL)
		{
			UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("Failed to allocate Recast navmesh"));
		}
	}

	int32 NumTiles = 0;
	TArray<FNavTileRef> TilesToSave;

	// 决定本次 Save 要写入哪些 Tile：World Partition / Transacting / 流关卡 / 全量
	if (Ar.IsSaving() && !Ar.IsTransacting() && !UE::Transaction::DiffUtil::IsGeneratingDiffableObject(Ar)) // Do not save tiles during transactions (i.e. undo/redo)
	{
		TilesToSave.Reserve(DetourNavMesh->getMaxTiles());
		
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<const UNavigationSystemV1>(NavMeshOwner->GetWorld());

		if (NavMeshOwner->bIsWorldPartitioned)
		{
			// Ignore (leave TilesToSave empty so no tiles are saved).
			// Navmesh data are stored in ANavigationDataChunkActor.
			// WP 模式：Tile 交由 ChunkActor 序列化，这里不写
			UE_LOG(LogNavigation, VeryVerbose, TEXT("%s Ar.IsSaving() no tiles are being saved because bIsWorldPartitioned=true in %s."), ANSI_TO_TCHAR(__FUNCTION__), *NavMeshOwner->GetFullName());
		}
		else
		{
			// Need to keep the check !IsRunningCommandlet() for the case where maps are cooked and saved from UCookCommandlet.
			// In that flow the nav bounds are not set (no bounds means no tiles to save and the navmesh would be saved without tiles).
			// This flow would benefit to be revisited since navmesh serialization should not be different whether it was run or not by a commandlet.
			// Fixes missing navmesh regression (UE-103604).
			if (NavMeshOwner->SupportsStreaming() && NavSys && !IsRunningCommandlet())
			{
				// We save only tiles that belongs to this level
				// 流式关卡：只写本 Level 的 NavBounds 范围内的 Tile
				GetNavMeshTilesIn(NavMeshOwner->GetNavigableBoundsInLevel(NavMeshOwner->GetLevel()), TilesToSave);
			}
			else
			{
				// Otherwise all valid tiles
				dtNavMesh const* ConstNavMesh = DetourNavMesh;
				for (int i = 0; i < ConstNavMesh->getMaxTiles(); ++i)
				{
					const dtMeshTile* Tile = ConstNavMesh->getTile(i);
					if (Tile != NULL && Tile->header != NULL && Tile->dataSize > 0)
					{
						FNavTileRef TileRef(ConstNavMesh->getTileRef(Tile));
						TilesToSave.Add(TileRef);
					}
				}
			}
		}
		
		NumTiles = TilesToSave.Num();
		UE_LOG(LogNavigation, VeryVerbose, TEXT("%hs Ar.IsSaving() %i tiles in %s."), __FUNCTION__, NumTiles, *NavMeshOwner->GetFullName());
	}

	Ar << NumTiles;

	dtNavMeshParams Params = *DetourNavMesh->getParams();
	Ar << Params.orig[0] << Params.orig[1] << Params.orig[2];
	Ar << Params.tileWidth;				///< The width of each tile. (Along the x-axis.)
	Ar << Params.tileHeight;			///< The height of each tile. (Along the z-axis.)
	Ar << Params.maxTiles;				///< The maximum number of tiles the navigation mesh can contain.
	Ar << Params.maxPolys;

	if (NavMeshOwner->NavMeshVersion >= NAVMESHVER_TILE_RESOLUTIONS)
	{
		Ar << Params.walkableHeight;
		Ar << Params.walkableRadius;
		Ar << Params.walkableClimb;
		Ar << Params.resolutionParams[(uint8)ENavigationDataResolution::Low].bvQuantFactor;
		Ar << Params.resolutionParams[(uint8)ENavigationDataResolution::Default].bvQuantFactor;
		Ar << Params.resolutionParams[(uint8)ENavigationDataResolution::High].bvQuantFactor;
	}
	else if (NavMeshOwner->NavMeshVersion >= NAVMESHVER_OPTIM_FIX_SERIALIZE_PARAMS)
	{
		// Load previous version navmesh data into new struct
		Ar << Params.walkableHeight;
		Ar << Params.walkableRadius;
		Ar << Params.walkableClimb;
		Ar << Params.resolutionParams[(uint8)ENavigationDataResolution::Default].bvQuantFactor;
		if (Ar.IsLoading())
		{
			const dtReal DefaultQuantFactor = Params.resolutionParams[(uint8)ENavigationDataResolution::Default].bvQuantFactor; 
			Params.resolutionParams[(uint8)ENavigationDataResolution::Low].bvQuantFactor = DefaultQuantFactor;
			Params.resolutionParams[(uint8)ENavigationDataResolution::High].bvQuantFactor = DefaultQuantFactor;
		}
	}
	else
	{
		Params.walkableHeight = NavMeshOwner->AgentHeight;
		Params.walkableRadius = NavMeshOwner->AgentRadius;
		Params.walkableClimb = NavMeshOwner->GetAgentMaxStepHeight(ENavigationDataResolution::Default);
		const float DefaultQuantFactor =  1.f / NavMeshOwner->GetCellSize(ENavigationDataResolution::Default);
		for(uint8 Index = 0; Index < (uint8)ENavigationDataResolution::MAX; Index++)
		{
			Params.resolutionParams[Index].bvQuantFactor = DefaultQuantFactor;	
		}
	}

	if (Ar.IsLoading())
	{
		// at this point we can tell whether navmesh being loaded is in line
		// ARecastNavMesh's params. If not, just skip it.
		// assumes tiles are rectangular
		
		float DefaultCellSize = NavMeshOwner->GetCellSize(ENavigationDataResolution::Default);

#if WITH_EDITORONLY_DATA
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (NavMeshVersion < NAVMESHVER_TILE_RESOLUTIONS)
		{
			// For backward compatibility, read original CellSize value.
			// In ARecastNavMesh::PostLoad(), cell sizes for the different resolutions are set to the old CellSize value but it occurs later (PostLoad).
			// This why we explicitly need to read CellSize for older versions here.
			DefaultCellSize = NavMeshOwner->CellSize;
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif // WITH_EDITORONLY_DATA

		const FVector::FReal ActorsTileSize = static_cast<FVector::FReal>(FMath::TruncToInt(NavMeshOwner->TileSizeUU / DefaultCellSize)) * DefaultCellSize;

		if (ActorsTileSize != Params.tileWidth)
		{
			// just move archive position
			ReleaseDetourNavMesh();

			for (int i = 0; i < NumTiles; ++i)
			{
				dtTileRef TileRef = MAX_uint64;
				int32 TileDataSize = 0;
				Ar << TileRef << TileDataSize;

				if (TileRef == MAX_uint64 || TileDataSize == 0)
				{
					continue;
				}

				unsigned char* TileData = NULL;
				TileDataSize = 0;
				SerializeRecastMeshTile(Ar, NavMeshVersion, TileData, TileDataSize);
				if (TileData != NULL)
				{
					dtMeshHeader* const TileHeader = (dtMeshHeader*)TileData;
					dtFree(TileHeader, DT_ALLOC_PERM_TILE_DATA);

					unsigned char* ComressedTileData = NULL;
					int32 CompressedTileDataSize = 0;
					SerializeCompressedTileCacheData(Ar, NavMeshVersion, ComressedTileData, CompressedTileDataSize);
					dtFree(ComressedTileData, DT_ALLOC_PERM_TILE_DATA);
				}
			}
		}
		else
		{
			// regular loading
			dtStatus Status = DetourNavMesh->init(&Params);
			if (dtStatusFailed(Status))
			{
				UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("Failed to initialize NavMesh"));
			}
			
			NavMeshOwner->bHasNoTileData = (NumTiles == 0);	

#if WITH_NAVMESH_SEGMENT_LINKS			
			// Store the tileRefs when iterating over the tile list.
			// These will be processed again to add the segment link connections.
			TArray<dtTileRef> AddedTiles;
			AddedTiles.Reserve(NumTiles);
#endif // WITH_NAVMESH_SEGMENT_LINKS			

			for (int i = 0; i < NumTiles; ++i)
			{
				dtTileRef TileRef = MAX_uint64;
				int32 TileDataSize = 0;
				Ar << TileRef << TileDataSize;

				if (TileRef == MAX_uint64 || TileDataSize == 0)
				{
					continue;
				}
				
				unsigned char* TileData = NULL;
				TileDataSize = 0;
				SerializeRecastMeshTile(Ar, NavMeshVersion, TileData, TileDataSize);

				if (TileData != NULL)
				{
#if WITH_NAVMESH_SEGMENT_LINKS					
					AddedTiles.Add(TileRef);
#endif

					dtMeshHeader* const TileHeader = (dtMeshHeader*)TileData;
					Status = DetourNavMesh->addTile(TileData, TileDataSize, DT_TILE_FREE_DATA, TileRef, NULL);
					if (dtStatusDetail(Status, DT_OUT_OF_MEMORY))
					{
						UE_LOG(LogNavigation, Warning, TEXT("%hs Failed to add tile (%d,%d:%d), %d tile limit reached in %s. If using FixedTilePoolSize, try increasing the TilePoolSize or using bigger tiles."),
							__FUNCTION__, TileHeader->x, TileHeader->y, TileHeader->layer, DetourNavMesh->getMaxTiles(), *NavMeshOwner->GetFullName());
					}

					// Serialize compressed tile cache layer
					uint8* ComressedTileData = nullptr;
					int32 CompressedTileDataSize = 0;
					SerializeCompressedTileCacheData(Ar, NavMeshVersion, ComressedTileData, CompressedTileDataSize);
					
					if (CompressedTileDataSize > 0)
					{
						AddTileCacheLayer(TileHeader->x, TileHeader->y, TileHeader->layer,
							FNavMeshTileData(ComressedTileData, CompressedTileDataSize, TileHeader->layer, Recast2UnrealBox(TileHeader->bmin, TileHeader->bmax)));
					}
				}
			}

#if WITH_NAVMESH_SEGMENT_LINKS
			// Create segment link connections now that all the tiles have been loaded.
			for (int i = 0; i < AddedTiles.Num(); ++i)
			{
				ProcessSegmentLinksForTile(FNavTileRef(AddedTiles[i]));
			}
#endif // WITH_NAVMESH_SEGMENT_LINKS
		}
	}
	else if (Ar.IsSaving())
	{
		const bool bSupportsRuntimeGeneration = NavMeshOwner->SupportsRuntimeGeneration();
		dtNavMesh const* ConstNavMesh = DetourNavMesh;
		
		for (FNavTileRef TileRefToSave : TilesToSave)
		{
			const dtMeshTile* Tile = ConstNavMesh->getTileByRef(static_cast<dtTileRef>(TileRefToSave));
			dtTileRef TileRef = ConstNavMesh->getTileRef(Tile);
			int32 TileDataSize = Tile->dataSize;
			Ar << TileRef << TileDataSize;

			unsigned char* TileData = Tile->data;
			SerializeRecastMeshTile(Ar, NavMeshVersion, TileData, TileDataSize);

			// Serialize compressed tile cache layer only if navmesh requires it
			{
				FNavMeshTileData TileCacheLayer;
				uint8* CompressedData = nullptr;
				int32 CompressedDataSize = 0;
				if (bSupportsRuntimeGeneration)
				{
					TileCacheLayer = GetTileCacheLayer(Tile->header->x, Tile->header->y, Tile->header->layer);
					CompressedData = TileCacheLayer.GetDataSafe();
					CompressedDataSize = TileCacheLayer.DataSize;
				}
				
				SerializeCompressedTileCacheData(Ar, NavMeshVersion, CompressedData, CompressedDataSize);
			}
		}
	}
}

// 序列化单个 Recast NavMesh Tile 的数据块（与 dtCreateNavMeshData 产出的内存布局保持一致）。
// 处理头部、顶点、Poly、Detail Mesh、BV 树、OffMeshConnection、SegmentLink、Cluster、PolyCluster 映射等所有子段。
// 加载时会调用 dtAlloc 分配 Tile 内存；不同的 NavMeshVersion 与 FortniteMainBranch CustomVer 用于版本兼容
// （如 NAVMESHVER_TILE_RESOLUTIONS、NAVMESHVER_OFFMESH_HEIGHT_BUG、NavigationLinkID32To64 升级 32->64 位 userId）。
void FPImplRecastNavMesh::SerializeRecastMeshTile(FArchive& Ar, int32 NavMeshVersion, unsigned char*& TileData, int32& TileDataSize)
{
	// The strategy here is to serialize the data blob that is passed into addTile()
	// @see dtCreateNavMeshData() for details on how this data is laid out

	FDetourTileSizeInfo SizeInfo;

	if (Ar.IsSaving())
	{
		// fill in data to write
		dtMeshHeader* const H = (dtMeshHeader*)TileData;
		SizeInfo.VertCount = H->vertCount;
		SizeInfo.PolyCount = H->polyCount;
		SizeInfo.MaxLinkCount = H->maxLinkCount;
		SizeInfo.DetailMeshCount = H->detailMeshCount;
		SizeInfo.DetailVertCount = H->detailVertCount;
		SizeInfo.DetailTriCount = H->detailTriCount;
		SizeInfo.BvNodeCount = H->bvNodeCount;
		SizeInfo.OffMeshConCount = H->offMeshConCount;
#if WITH_NAVMESH_SEGMENT_LINKS
		SizeInfo.OffMeshSegConCount = H->offMeshSegConCount;
#endif // WITH_NAVMESH_SEGMENT_LINKS

#if WITH_NAVMESH_CLUSTER_LINKS
		SizeInfo.ClusterCount = H->clusterCount;
#endif // WITH_NAVMESH_CLUSTER_LINKS
	}

	Ar << SizeInfo.VertCount << SizeInfo.PolyCount << SizeInfo.MaxLinkCount ;
	Ar << SizeInfo.DetailMeshCount << SizeInfo.DetailVertCount << SizeInfo.DetailTriCount;
	Ar << SizeInfo.BvNodeCount << SizeInfo.OffMeshConCount << SizeInfo.OffMeshSegConCount;
	Ar << SizeInfo.ClusterCount;
	SizeInfo.OffMeshBase = SizeInfo.DetailMeshCount;
	const int32 polyClusterCount = SizeInfo.OffMeshBase;

	// calc sizes for our data so we know how much to allocate and where to read/write stuff
	// note this may not match the on-disk size or the in-memory size on the machine that generated that data

	FDetourTileLayout TileLayout(SizeInfo);

	if (Ar.IsLoading())
	{
		check(TileData == NULL);

		TileDataSize = TileLayout.TileSize;

		TileData = (unsigned char*)dtAlloc(sizeof(unsigned char)*TileDataSize, DT_ALLOC_PERM_TILE_DATA);
		if (!TileData)
		{
			UE_LOG(LogNavigation, Error, TEXT("Failed to alloc navmesh tile"));
		}
		FMemory::Memset(TileData, 0, TileDataSize);
	}
	else if (Ar.IsSaving())
	{
		// TileData and TileDataSize should already be set, verify
		check(TileData != NULL);
		check(TileLayout.TileSize == TileDataSize);
	}

	if (TileData != NULL)
	{
		// sort out where various data types do/will live
		unsigned char* d = TileData;
		dtMeshHeader* Header = (dtMeshHeader*)d; d += TileLayout.HeaderSize;
		FVector::FReal* NavVerts = (FVector::FReal*)d; d += TileLayout.VertsSize;
		dtPoly* NavPolys = (dtPoly*)d; d += TileLayout.PolysSize;
		d += TileLayout.LinksSize;			// @fixme, are links autogenerated on addTile?
		dtPolyDetail* DetailMeshes = (dtPolyDetail*)d; d += TileLayout.DetailMeshesSize;
		FVector::FReal* DetailVerts = (FVector::FReal*)d; d += TileLayout.DetailVertsSize;
		unsigned char* DetailTris = (unsigned char*)d; d += TileLayout.DetailTrisSize;
		dtBVNode* BVTree = (dtBVNode*)d; d += TileLayout.BvTreeSize;
		dtOffMeshConnection* OffMeshCons = (dtOffMeshConnection*)d; d += TileLayout.OffMeshConsSize;

#if WITH_NAVMESH_SEGMENT_LINKS
		dtOffMeshSegmentConnection* OffMeshSegs = (dtOffMeshSegmentConnection*)d; d += TileLayout.OffMeshSegsSize;
#endif // WITH_NAVMESH_SEGMENT_LINKS

#if WITH_NAVMESH_CLUSTER_LINKS
		dtCluster* Clusters = (dtCluster*)d; d += TileLayout.ClustersSize;
		unsigned short* PolyClusters = (unsigned short*)d; d += TileLayout.PolyClustersSize;
#endif // WITH_NAVMESH_CLUSTER_LINKS

		check(d==(TileData + TileDataSize));

		// now serialize the data in the blob!

		// header
		Ar << Header->version << Header->x << Header->y;
		Ar << Header->layer << Header->polyCount << Header->vertCount;
		Ar << Header->maxLinkCount << Header->detailMeshCount << Header->detailVertCount << Header->detailTriCount;
		Ar << Header->bvNodeCount << Header->offMeshConCount<< Header->offMeshBase;
		
		if (NavMeshVersion >= NAVMESHVER_TILE_RESOLUTIONS)
		{
			Ar << Header->resolution;
		}
		
		Ar << Header->bmin[0] << Header->bmin[1] << Header->bmin[2];
		Ar << Header->bmax[0] << Header->bmax[1] << Header->bmax[2];
#if WITH_NAVMESH_CLUSTER_LINKS
		Ar << Header->clusterCount;
#else
		unsigned short DummyClusterCount = 0;
		Ar << DummyClusterCount;
#endif // WITH_NAVMESH_CLUSTER_LINKS

#if WITH_NAVMESH_SEGMENT_LINKS
		Ar << Header->offMeshSegConCount << Header->offMeshSegPolyBase << Header->offMeshSegVertBase;
#else
		unsigned short DummySegmentInt = 0;
		Ar << DummySegmentInt << DummySegmentInt << DummySegmentInt;
#endif // WITH_NAVMESH_SEGMENT_LINKS

		// mesh and offmesh connection vertices, just an array of reals (one real triplet per vert)
		{
			FVector::FReal* F = NavVerts;
			for (int32 VertIdx=0; VertIdx < SizeInfo.VertCount; VertIdx++)
			{
				Ar << *F; F++;
				Ar << *F; F++;
				Ar << *F; F++;
			}
		}

		// mesh and off-mesh connection polys
		for (int32 PolyIdx=0; PolyIdx < SizeInfo.PolyCount; ++PolyIdx)
		{
			dtPoly& P = NavPolys[PolyIdx];
			Ar << P.firstLink;

			for (uint32 VertIdx=0; VertIdx < DT_VERTS_PER_POLYGON; ++VertIdx)
			{
				Ar << P.verts[VertIdx];
			}
			for (uint32 NeiIdx=0; NeiIdx < DT_VERTS_PER_POLYGON; ++NeiIdx)
			{
				Ar << P.neis[NeiIdx];
			}
			Ar << P.flags << P.vertCount << P.areaAndtype;
		}

		// serialize detail meshes
		for (int32 MeshIdx=0; MeshIdx < SizeInfo.DetailMeshCount; ++MeshIdx)
		{
			dtPolyDetail& DM = DetailMeshes[MeshIdx];
			Ar << DM.vertBase << DM.triBase << DM.vertCount << DM.triCount;
		}

		// serialize detail verts (one real triplet per vert)
		{
			FVector::FReal* F = DetailVerts;
			for (int32 VertIdx=0; VertIdx < SizeInfo.DetailVertCount; ++VertIdx)
			{
				Ar << *F; F++;
				Ar << *F; F++;
				Ar << *F; F++;
			}
		}

		// serialize detail tris (4 one-byte indices per tri)
		{
			unsigned char* V = DetailTris;
			for (int32 TriIdx=0; TriIdx < SizeInfo.DetailTriCount; ++TriIdx)
			{
				Ar << *V; V++;
				Ar << *V; V++;
				Ar << *V; V++;
				Ar << *V; V++;
			}
		}

		// serialize BV tree
		for (int32 NodeIdx=0; NodeIdx < SizeInfo.BvNodeCount; ++NodeIdx)
		{
			dtBVNode& Node = BVTree[NodeIdx];
			Ar << Node.bmin[0] << Node.bmin[1] << Node.bmin[2];
			Ar << Node.bmax[0] << Node.bmax[1] << Node.bmax[2];
			Ar << Node.i;
		}

		// serialize off-mesh connections
		for (int32 ConnIdx=0; ConnIdx < SizeInfo.OffMeshConCount; ++ConnIdx)
		{
			dtOffMeshConnection& Conn = OffMeshCons[ConnIdx];
			Ar << Conn.pos[0] << Conn.pos[1] << Conn.pos[2] << Conn.pos[3] << Conn.pos[4] << Conn.pos[5];
			Ar << Conn.rad << Conn.poly << Conn.flags << Conn.side;

			if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::NavigationLinkID32To64)
			{
				uint32 Id;

				Ar << Id;
				Conn.userId = Id;
			}
			else
			{
				Ar << Conn.userId;
			}
		}

		if (NavMeshVersion >= NAVMESHVER_OFFMESH_HEIGHT_BUG)
		{
			for (int32 ConnIdx = 0; ConnIdx < SizeInfo.OffMeshConCount; ++ConnIdx)
			{
				dtOffMeshConnection& Conn = OffMeshCons[ConnIdx];
				Ar << Conn.height;
			}
		}

		for (int32 SegIdx=0; SegIdx < SizeInfo.OffMeshSegConCount; ++SegIdx)
		{
#if WITH_NAVMESH_SEGMENT_LINKS
			dtOffMeshSegmentConnection& Seg = OffMeshSegs[SegIdx];
			Ar << Seg.startA[0] << Seg.startA[1] << Seg.startA[2];
			Ar << Seg.startB[0] << Seg.startB[1] << Seg.startB[2];
			Ar << Seg.endA[0] << Seg.endA[1] << Seg.endA[2];
			Ar << Seg.endB[0] << Seg.endB[1] << Seg.endB[2];
			Ar << Seg.rad << Seg.firstPoly << Seg.npolys << Seg.flags << Seg.userId;
#else
			FVector::FReal DummySegmentConReal;
			unsigned int DummySegmentConInt;
			unsigned short DummySegmentConShort;
			unsigned char DummySegmentConChar;
			Ar << DummySegmentConReal << DummySegmentConReal << DummySegmentConReal; // real startA[3];	///< Start point of segment A
			Ar << DummySegmentConReal << DummySegmentConReal << DummySegmentConReal; // real endA[3];	///< End point of segment A
			Ar << DummySegmentConReal << DummySegmentConReal << DummySegmentConReal; // real startB[3];	///< Start point of segment B
			Ar << DummySegmentConReal << DummySegmentConReal << DummySegmentConReal; // real endB[3];	///< End point of segment B
			Ar << DummySegmentConReal << DummySegmentConShort << DummySegmentConChar << DummySegmentConChar << DummySegmentConInt; // real rad, short firstPoly, char npolys, char flags, int userId
#endif // WITH_NAVMESH_SEGMENT_LINKS
		}

		// serialize clusters
		for (int32 CIdx = 0; CIdx < SizeInfo.ClusterCount; ++CIdx)
		{
#if WITH_NAVMESH_CLUSTER_LINKS
			dtCluster& Cluster = Clusters[CIdx];
			Ar << Cluster.center[0] << Cluster.center[1] << Cluster.center[2];
#else
			FVector::FReal DummyCluster[3];
			Ar << DummyCluster[0] << DummyCluster[1] << DummyCluster[2];
#endif // WITH_NAVMESH_CLUSTER_LINKS
		}

		// serialize poly clusters map
		{
#if WITH_NAVMESH_CLUSTER_LINKS
			unsigned short* C = PolyClusters;
			for (int32 PolyClusterIdx = 0; PolyClusterIdx < polyClusterCount; ++PolyClusterIdx)
			{
				Ar << *C; C++;
			}
#else
			unsigned short DummyPolyCluster = 0;
			for (int32 PolyClusterIdx = 0; PolyClusterIdx < polyClusterCount; ++PolyClusterIdx)
			{
				Ar << DummyPolyCluster;
			}
#endif // WITH_NAVMESH_CLUSTER_LINKS
		}
	}
}

// 序列化单个 Tile 缓存层（dtTileCache 用于动态导航网格的压缩高度场层）的数据。
// 三种情形：无头无数据（EmptyDataValue=-1）、仅有头部、有头部+压缩字节流。
// 头部按 dtTileCacheLayerHeader 字段逐项序列化；Loading 时通过 dtAlloc 分配，且不同 NavMeshVersion
// 决定头部字段是否包含（例如旧版本可能缺少某些字段，需做兼容回填）。
void FPImplRecastNavMesh::SerializeCompressedTileCacheData(FArchive& Ar, int32 NavMeshVersion, unsigned char*& CompressedData, int32& CompressedDataSize)
{
	constexpr int32 EmptyDataValue = -1;

	// Note when saving the CompressedDataSize is either 0 or it must be big enough to include the size of the uncompressed dtTileCacheLayerHeader.
	checkf((Ar.IsSaving() == false) || CompressedDataSize == 0 || CompressedDataSize >= dtAlign(sizeof(dtTileCacheLayerHeader)), TEXT("When saving CompressedDataSize must either be zero or large enough to hold dtTileCacheLayerHeader!"));
	checkf((Ar.IsSaving() == false) || CompressedDataSize == 0 || CompressedData != nullptr, TEXT("When saving CompressedDataSize must either be zero or CompressedData must be != nullptr"));

	if (Ar.IsLoading())
	{
		// Initialize to 0 if we are loading as this is calculated and used duiring processing.
		CompressedDataSize = 0;
	}
	
	// There are 3 cases that need to be serialized here, no header no compresseed data, header only no compressed data or header and compressed data.
	// CompressedDataSizeNoHeader == NoHeaderValue, indicates we have no header and no compressed data.
	// CompressedDataSizeNoHeader == 0, indicates we have a header only no compressed data.
	// CompressedDataSizeNoHeader > 0, indicates we have a header and compressed data.
	int32 CompressedDataSizeNoHeader = 0;
	if (Ar.IsSaving())
	{
		// Handle invalid CompressedDataSize ( i.e. CompressedDataSize > 0 and CompressedDataSize < dtAlign(sizeof(dtTileCacheLayerHeader))
		// as well as valid CompressedDataSize == 0, to make this function atleast as robust as it was.
		if (CompressedDataSize < dtAlign(sizeof(dtTileCacheLayerHeader)))
		{
			CompressedDataSizeNoHeader = EmptyDataValue;
		}
		else
		{
			CompressedDataSizeNoHeader = CompressedDataSize - dtAlign(sizeof(dtTileCacheLayerHeader));
		}
	}

	Ar << CompressedDataSizeNoHeader;

	const bool bHasHeader = CompressedDataSizeNoHeader >= 0;

	if (!bHasHeader)
	{
		return;
	}

	if (Ar.IsLoading())
	{
		CompressedDataSize = CompressedDataSizeNoHeader + dtAlign(sizeof(dtTileCacheLayerHeader));
		CompressedData = (unsigned char*)dtAlloc(sizeof(unsigned char)*CompressedDataSize, DT_ALLOC_PERM_TILE_DATA);
		if (!CompressedData)
		{
			UE_LOG(LogNavigation, Error, TEXT("Failed to alloc tile compressed data"));
		}
		FMemory::Memset(CompressedData, 0, CompressedDataSize);
	}

	check(CompressedData != nullptr);

	// Serialize dtTileCacheLayerHeader by hand so we can account for the FReals always being serialized as doubles
	dtTileCacheLayerHeader* Header = (dtTileCacheLayerHeader*)CompressedData;
	Ar << Header->version;
	Ar << Header->tx;
	Ar << Header->ty;
	Ar << Header->tlayer;
	for (int i = 0; i < 3; ++i)
	{
		Ar << Header->bmin[i];
		Ar << Header->bmax[i];
	}
	Ar << Header->hmin;	// @todo: remove
	Ar << Header->hmax;
	Ar << Header->width;
	Ar << Header->height;
	Ar << Header->minx;
	Ar << Header->maxx;
	Ar << Header->miny;
	Ar << Header->maxy;

	if (CompressedDataSizeNoHeader > 0)
	{
		// @todo this does not appear to be accounting for potential endian differences!
		Ar.Serialize(CompressedData + dtAlign(sizeof(dtTileCacheLayerHeader)), CompressedDataSizeNoHeader);
	}
}

// 替换底层 dtNavMesh 实例。
// 若与现有相同则直接返回；否则先 ReleaseDetourNavMesh 释放旧的，赋值新指针，
// 通知 NavMeshOwner 更新内部对象，并重新应用 Area Cost 排序（OnAreaCostChanged）。
void FPImplRecastNavMesh::SetRecastMesh(dtNavMesh* NavMesh)
{
	if (NavMesh == DetourNavMesh)
	{
		return;
	}

	ReleaseDetourNavMesh();
	DetourNavMesh = NavMesh;

	if (NavMeshOwner)
	{
		NavMeshOwner->UpdateNavObject();
	}

	// reapply area sort order in new detour navmesh
	OnAreaCostChanged();
}

// -------------------- Raycast (UE 版 NavMesh Raycast) --------------------
// 与 dtNavMeshQuery::raycast 的区别：本函数在 Detour raycast 基础上补充了：
//   1) 若未提供 StartNode，先 findNearestContainingPoly 把起点吸附到 Poly
//   2) 专门查一次 EndNode，用于判断 bIsRaycastEndInCorridor（终点是否落在走廊末端 Poly 内）
//   3) HitNormal 从 Recast 空间翻回 UE 空间
// 调用者：ARecastNavMesh::NavMeshRaycast 多重载 → 本函数 → dtNavMeshQuery::raycast
// 线程：可在后台工作线程调用（INITIALIZE_NAVQUERY 会按线程分 Query 实例）
void FPImplRecastNavMesh::Raycast(const FVector& StartLoc, const FVector& EndLoc, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner, 
	ARecastNavMesh::FRaycastResult& RaycastResult, NavNodeRef StartNode) const
{
	if (DetourNavMesh == NULL || NavMeshOwner == NULL)
	{
		return;
	}

	// UE 过滤器 → Detour 过滤器
	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(InQueryFilter.GetImplementation()))->GetAsDetourQueryFilter();
	if (QueryFilter == NULL)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::Raycast failing due to QueryFilter == NULL"));
		return;
	}

	// SpecialLinkFilter 用于遍历到自定义 OffMeshLink 时询问是否可通行
	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, InQueryFilter.GetMaxSearchNodes(), LinkFilter);

	// 查询 Extent 含 VerticalDeviationFromGroundCompensation 的修正（处理 NavMesh 与几何的垂直偏差）
	const FVector NavExtent = NavMeshOwner->GetModifiedQueryExtent(NavMeshOwner->GetDefaultQueryExtent());
	// 注意：Detour 的 extent 顺序是 x/z/y（因为它的坐标 y 是向上的）
	const FVector::FReal Extent[3] = { NavExtent.X, NavExtent.Z, NavExtent.Y };

	// UE→Recast 坐标转换：Unreal(x,y,z) → Recast(-x,z,-y)
	const FVector RecastStart = Unreal2RecastPoint(StartLoc);
	const FVector RecastEnd = Unreal2RecastPoint(EndLoc);

	if (StartNode == INVALID_NAVNODEREF)
	{
		// 起点没预先贴到 Poly → 自动找"包含 RecastStart 的 Poly"
		NavQuery.findNearestContainingPoly(&RecastStart.X, Extent, QueryFilter, &StartNode, NULL);
	}

	NavNodeRef EndNode = INVALID_NAVNODEREF;
	NavQuery.findNearestContainingPoly(&RecastEnd.X, Extent, QueryFilter, &EndNode, NULL);

	if (StartNode != INVALID_NAVNODEREF)
	{
		FVector::FReal RecastHitNormal[3];

		// 真正的 Detour Raycast：沿 Start→End 推进，填 CorridorPolys + HitTime + HitNormal
		const dtStatus RaycastStatus = NavQuery.raycast(StartNode, &RecastStart.X, &RecastEnd.X
			, QueryFilter, &RaycastResult.HitTime, RecastHitNormal
			, RaycastResult.CorridorPolys, &RaycastResult.CorridorPolysCount, RaycastResult.GetMaxCorridorSize());

		RaycastResult.HitNormal = Recast2UnrVector(RecastHitNormal);
		// 是否走到终点：成功 + 走廊最后一块 Poly 正是 EndNode
		RaycastResult.bIsRaycastEndInCorridor = dtStatusSucceed(RaycastStatus) && (RaycastResult.GetLastNodeRef() == EndNode);
	}
	else
	{
		// 起点不在 NavMesh 上：直接视为"一开始就被挡住"
		RaycastResult.HitTime = 0.f;
		RaycastResult.HitNormal = (StartLoc - EndLoc).GetSafeNormal();
	}
}

// @TODONAV
// -------------------- FindPath：§4.4 寻路链路的关键函数 --------------------
// 完整链路（UE 架构文档 §4.4）：
//   Blueprint/AIController → UNavigationSystemV1::FindPathSync
//     → ANavigationData::FindPath (走 FindPathImplementation 函数指针)
//     → ARecastNavMesh::FindPath (静态回调)
//     → 此函数 FPImplRecastNavMesh::FindPath
//        ├─ InitPathfinding：UE 坐标→Recast 坐标，Start/End 贴到 Poly
//        ├─ dtNavMeshQuery::findPath：A* 在多边形图上搜索，产出 PathCorridor
//        └─ PostProcessPathInternal：dtStatus→UE 结果 + 拉绳 + ApplyFlags
// 坐标：StartLoc/EndLoc 为 UE 坐标，函数内部调 Unreal2Recast 转换为 Recast 坐标。
// 线程：可在 GameThread 或后台 AsyncPath 线程；INITIALIZE_NAVQUERY 已处理线程本地 Query。
// 返回：ENavigationQueryResult::Success/Partial/Fail/Error 等
ENavigationQueryResult::Type FPImplRecastNavMesh::FindPath(const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, const bool bRequireNavigableEndLocation, FNavMeshPath& Path, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner) const
{
	// temporarily disabling this check due to it causing too much "crashes"
	// @todo but it needs to be back at some point since it realy checks for a buggy setup
	//ensure(DetourNavMesh != NULL || NavMeshOwner->bRebuildAtRuntime == false);

	if (DetourNavMesh == NULL || NavMeshOwner == NULL)
	{
		return ENavigationQueryResult::Error;
	}

	// 1) 过滤器接口双重校验：必须是 FRecastQueryFilter 且能给出 dtQueryFilter
	const FRecastQueryFilter* FilterImplementation = (const FRecastQueryFilter*)(InQueryFilter.GetImplementation());
	if (FilterImplementation == NULL)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("FPImplRecastNavMesh::FindPath failed due to passed filter having NULL implementation!"));
		return ENavigationQueryResult::Error;
	}

	const dtQueryFilter* QueryFilter = FilterImplementation->GetAsDetourQueryFilter();
	if (QueryFilter == NULL)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::FindPath failing due to QueryFilter == NULL"));
		return ENavigationQueryResult::Error;
	}

	// 2) 构造 SpecialLinkFilter：遇到 OffMeshLink 时回调 INavLinkCustomInterface::IsLinkPathfindingAllowed
	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, InQueryFilter.GetMaxSearchNodes(), LinkFilter);
	// bRequireNavigableEndLocation=false 时，允许终点不在 NavMesh 上（返回最近 Partial 路径）
	NavQuery.setRequireNavigableEndLocation(bRequireNavigableEndLocation);

	// 3) 起止点投射：UE 坐标 → Recast 坐标，findNearestPoly 贴到具体多边形
	FVector RecastStartPos, RecastEndPos;
	NavNodeRef StartPolyID, EndPolyID;
	const bool bCanSearch = InitPathfinding(StartLoc, EndLoc, NavQuery, QueryFilter, RecastStartPos, StartPolyID, RecastEndPos, EndPolyID, &Path);
	if (!bCanSearch)
	{
		return ENavigationQueryResult::Error;
	}

	// 4) 核心 A*：dtNavMeshQuery::findPath 跑多边形图搜索，产出 PolyCorridor
	// get path corridor
	dtQueryResult PathResult;
	const dtStatus FindPathStatus = NavQuery.findPath(StartPolyID, EndPolyID, &RecastStartPos.X, &RecastEndPos.X, CostLimit, QueryFilter, PathResult, 0);

	// 5) 后处理：dtStatus → UE 结果；拉绳；Flags；Link Id 收集
	return PostProcessPathInternal(FindPathStatus, Path, NavQuery, QueryFilter, StartPolyID, EndPolyID, RecastStartPos, RecastEndPos, PathResult);
}


ENavigationQueryResult::Type FPImplRecastNavMesh::PostProcessPathInternal(dtStatus FindPathStatus, FNavMeshPath& Path, 
	const dtNavMeshQuery& NavQuery, const dtQueryFilter* QueryFilter, 
	NavNodeRef StartPolyID, NavNodeRef EndPolyID, 
	const FVector& RecastStartPos, const FVector& RecastEndPos,
	dtQueryResult& PathResult) const
{
	// check for special case, where path has not been found, and starting polygon
	// was the one closest to the target
	if (PathResult.size() == 1 && dtStatusDetail(FindPathStatus, DT_PARTIAL_RESULT))
	{
		// in this case we find a point on starting polygon, that's closest to destination
		// and store it as path end
		FVector RecastHandPlacedPathEnd;
		NavQuery.closestPointOnPolyBoundary(StartPolyID, &RecastEndPos.X, &RecastHandPlacedPathEnd.X);

		new(Path.GetPathPoints()) FNavPathPoint(Recast2UnrVector(&RecastStartPos.X), StartPolyID);
		new(Path.GetPathPoints()) FNavPathPoint(Recast2UnrVector(&RecastHandPlacedPathEnd.X), StartPolyID);

		Path.PathCorridor.Add(PathResult.getRef(0));
		Path.PathCorridorCost.Add(CalcSegmentCostOnPoly(StartPolyID, QueryFilter, RecastHandPlacedPathEnd, RecastStartPos));
	}
	else
	{
		PostProcessPath(FindPathStatus, Path, NavQuery, QueryFilter,
			StartPolyID, EndPolyID, Recast2UnrVector(&RecastStartPos.X), Recast2UnrVector(&RecastEndPos.X), RecastStartPos, RecastEndPos,
			PathResult);
	}

	if (dtStatusDetail(FindPathStatus, DT_PARTIAL_RESULT))
	{
		Path.SetIsPartial(true);
		// this means path finding algorithm reached the limit of InQueryFilter.GetMaxSearchNodes()
		// nodes in A* node pool. This can mean resulting path is way off.
		Path.SetSearchReachedLimit(dtStatusDetail(FindPathStatus, DT_OUT_OF_NODES));
	}

#if ENABLE_VISUAL_LOG
	if (dtStatusDetail(FindPathStatus, DT_INVALID_CYCLE_PATH))
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("FPImplRecastNavMesh::FindPath resulted in a cyclic path!"));
		FVisualLogger::Get().ExecuteOnLastEntryForObject(NavMeshOwner, [&Path](FVisualLogEntry& LogEntry)
		{
			Path.DescribeSelfToVisLog(&LogEntry);
		});
	}
#endif // ENABLE_VISUAL_LOG

	Path.MarkReady();

	return DTStatusToNavQueryResult(FindPathStatus);
}

ENavigationQueryResult::Type FPImplRecastNavMesh::TestPath(const FVector& StartLoc, const FVector& EndLoc, const bool bRequireNavigableEndLocation, const FNavigationQueryFilter& InQueryFilter, const UObject* Owner, int32* NumVisitedNodes) const
{
	// Same check as in FPImplRecastNavMesh::FindPath (ex: this can occur when tileWidth of loading navmesh mismatch).
	if (DetourNavMesh == NULL || NavMeshOwner == NULL)
	{
		return ENavigationQueryResult::Error;
	}
	
	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(InQueryFilter.GetImplementation()))->GetAsDetourQueryFilter();
	if (QueryFilter == NULL)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::FindPath failing due to QueryFilter == NULL"));
		return ENavigationQueryResult::Error;
	}

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, InQueryFilter.GetMaxSearchNodes(), LinkFilter);
	NavQuery.setRequireNavigableEndLocation(bRequireNavigableEndLocation);

	FVector RecastStartPos, RecastEndPos;
	NavNodeRef StartPolyID, EndPolyID;
	const bool bCanSearch = InitPathfinding(StartLoc, EndLoc, NavQuery, QueryFilter, RecastStartPos, StartPolyID, RecastEndPos, EndPolyID);
	if (!bCanSearch)
	{
		return ENavigationQueryResult::Error;
	}

	// get path corridor
	dtQueryResult PathResult;
	const FVector::FReal CostLimit = TNumericLimits<FVector::FReal>::Max();
	const dtStatus FindPathStatus = NavQuery.findPath(StartPolyID, EndPolyID,
		&RecastStartPos.X, &RecastEndPos.X, CostLimit, QueryFilter, PathResult, 0);

	if (NumVisitedNodes)
	{
		*NumVisitedNodes = NavQuery.getQueryNodes();
	}

	return DTStatusToNavQueryResult(FindPathStatus);
}

#if WITH_NAVMESH_CLUSTER_LINKS
ENavigationQueryResult::Type FPImplRecastNavMesh::TestClusterPath(const FVector& StartLoc, const FVector& EndLoc, int32* NumVisitedNodes) const
{
	FVector RecastStartPos, RecastEndPos;
	NavNodeRef StartPolyID, EndPolyID;
	const dtQueryFilter* ClusterFilter = ((const FRecastQueryFilter*)NavMeshOwner->GetDefaultQueryFilterImpl())->GetAsDetourQueryFilter();

	check(NavMeshOwner->DefaultMaxHierarchicalSearchNodes >= 0. && NavMeshOwner->DefaultMaxHierarchicalSearchNodes <= (float)TNumericLimits<int32>::Max());
	INITIALIZE_NAVQUERY_SIMPLE(ClusterQuery, static_cast<int32>(NavMeshOwner->DefaultMaxHierarchicalSearchNodes));
	ClusterQuery.setRequireNavigableEndLocation(true);

	const bool bCanSearch = InitPathfinding(StartLoc, EndLoc, ClusterQuery, ClusterFilter, RecastStartPos, StartPolyID, RecastEndPos, EndPolyID);
	if (!bCanSearch)
	{
		return ENavigationQueryResult::Error;
	}

	const dtStatus status = ClusterQuery.testClusterPath(StartPolyID, EndPolyID);
	if (NumVisitedNodes)
	{
		*NumVisitedNodes = ClusterQuery.getQueryNodes();
	}

	return DTStatusToNavQueryResult(status);
}
#endif // WITH_NAVMESH_CLUSTER_LINKS

// InitPathfinding：寻路前置三步曲
//   1) 计算 Extent（含 VerticalDeviationFromGroundCompensation 垂直容差扩展）
//   2) UE 坐标 → Recast 坐标：Unreal(x,y,z) → Recast(-x,z,-y)
//   3) findNearestPoly：把点吸附到具体 Poly（投影到 NavMesh 表面）
// 特殊处理：若 Query 不要求 EndLocation 可达，End 没贴到 Poly 时只复制原始投影坐标
//   用于启发计算，返回 Partial Path。
// 错误上报：失败时通过 UE_VLOG_* 输出 VisLog 可视化轨迹 + 设置 OutPath 错误标志。
bool FPImplRecastNavMesh::InitPathfinding(const FVector& UnrealStart, const FVector& UnrealEnd,
	const dtNavMeshQuery& Query, const dtQueryFilter* Filter,
	FVector& RecastStart, dtPolyRef& StartPoly,
	FVector& RecastEnd, dtPolyRef& EndPoly, FNavMeshPath* OutPath) const
{
	// 修正 Extent：在垂直方向加一点容差以补偿 NavMesh 相对碰撞几何的偏差
	const FVector NavExtent = NavMeshOwner->GetModifiedQueryExtent(NavMeshOwner->GetDefaultQueryExtent());
	// Detour 坐标顺序 (x, y_up, z)；对应 UE 的 (x, z, y)
	const FVector::FReal Extent[3] = { NavExtent.X, NavExtent.Z, NavExtent.Y };

	// 坐标翻转：UE→Recast（架构文档 §2 坐标约定）
	const FVector RecastStartToProject = Unreal2RecastPoint(UnrealStart);
	const FVector RecastEndToProject = Unreal2RecastPoint(UnrealEnd);

	// 起点：必须能投到 Poly，否则整次寻路失败
	StartPoly = INVALID_NAVNODEREF;
	Query.findNearestPoly(&RecastStartToProject.X, Extent, Filter, &StartPoly, &RecastStart.X);
	if (StartPoly == INVALID_NAVNODEREF)
	{
		UE_VLOG_UELOG(NavMeshOwner, LogNavigation, Log, TEXT("FPImplRecastNavMesh::InitPathfinding start point not on navmesh (%s)"), *UnrealStart.ToString());
		UE_VLOG_SEGMENT(NavMeshOwner, LogNavigation, Log, UnrealStart, UnrealEnd, FColor::Red, TEXT("Failed path"));
		UE_VLOG_LOCATION(NavMeshOwner, LogNavigation, Log, UnrealStart, 15, FColor::Red, TEXT("Start failed"));
		UE_VLOG_BOX(NavMeshOwner, LogNavigation, Log, FBox(UnrealStart - NavExtent, UnrealStart + NavExtent), FColor::Red, TEXT_EMPTY);

		if (OutPath)
		{
			OutPath->SetErrorStartLocationNonNavigable(true);
		}
		return false;
	}

	// 终点：视 Query.bRequireNavigableEndLocation 决定 "必须可达" vs "允许 Partial"
	EndPoly = INVALID_NAVNODEREF;
	Query.findNearestPoly(&RecastEndToProject.X, Extent, Filter, &EndPoly, &RecastEnd.X);
	if (EndPoly == INVALID_NAVNODEREF)
	{
		if (Query.isRequiringNavigableEndLocation())
		{
			// 严格模式：终点不可达直接失败
			UE_VLOG_UELOG(NavMeshOwner, LogNavigation, Log, TEXT("FPImplRecastNavMesh::InitPathfinding end point not on navmesh (%s)"), *UnrealEnd.ToString());
			UE_VLOG_SEGMENT(NavMeshOwner, LogNavigation, Log, UnrealEnd, UnrealEnd, FColor::Red, TEXT("Failed path"));
			UE_VLOG_LOCATION(NavMeshOwner, LogNavigation, Log, UnrealEnd, 15, FColor::Red, TEXT("End failed"));
			UE_VLOG_BOX(NavMeshOwner, LogNavigation, Log, FBox(UnrealEnd - NavExtent, UnrealEnd + NavExtent), FColor::Red, TEXT_EMPTY);

			if (OutPath)
			{
				OutPath->SetErrorEndLocationNonNavigable(true);
			}
			return false;
		}

		// we will use RecastEndToProject as the estimated end location since we didn't find a poly. It will be used to compute the heuristic mainly
		// 宽松模式：保留原投影位置供启发函数使用，A* 会产出 Partial 路径
		dtVcopy(&RecastEnd.X, &RecastEndToProject.X);
	}

	return true;
}

FVector::FReal FPImplRecastNavMesh::CalcSegmentCostOnPoly(NavNodeRef PolyID, const dtQueryFilter* Filter, const FVector& StartLoc, const FVector& EndLoc) const
{
	uint8 AreaID = RECAST_DEFAULT_AREA;
	DetourNavMesh->getPolyArea(PolyID, &AreaID);

	const FVector::FReal AreaTravelCost = Filter->getAreaCost(AreaID);
	return AreaTravelCost * (EndLoc - StartLoc).Size();
}

// PostProcessPath：寻路后处理的完整流程
// 输入：dtStatus + dtQueryResult（来自 dtNavMeshQuery::findPath 的 Poly 序列和代价）
// 步骤：
//   1) 若 bAllowNavLinkAsPathEnd=false 且末段是 NavLink，则剥离最后一个 Poly
//      （避免 Agent "停在半空中的 NavLink 上"）
//   2) 把 Poly 序列 + 代价拷进 Path.PathCorridor / Path.PathCorridorCost
//   3) Backtracking（反向搜索）时把路径反转再做拉绳
//   4) bWantsStringPulling → PerformStringPulling（调 findStraightPath）
//      对 Partial Path：用走廊最后一块 Poly 上"距离目标最近点"作为拉绳终点
//   5) 否则：至少添加起止点 + 收集 CustomNavLinkIds（供 AI 运动时判断特殊通行）
//   6) bWantsPathCorridor → GetEdgesForPathCorridorImpl 预生成 Portal Edges
void FPImplRecastNavMesh::PostProcessPath(dtStatus FindPathStatus, FNavMeshPath& Path,
	const dtNavMeshQuery& NavQuery, const dtQueryFilter* Filter,
	NavNodeRef StartPolyID, NavNodeRef EndPolyID,
	FVector StartLoc, FVector EndLoc,
	FVector RecastStartPos, FVector RecastEndPos,
	dtQueryResult& PathResult) const
{
	check(Filter);

	// note that for recast partial path is successful, while we treat it as failed, just marking it as partial
	// 注意：Recast 把 Partial 当"成功"；UE 也接受，只是标记为 Partial
	if (dtStatusSucceed(FindPathStatus))
	{
		// check if navlink poly at end of path is allowed
		// 若不允许 NavLink 作为路径终点，砍掉末尾的 NavLink Poly
		int32 PathSize = PathResult.size();
		if ((PathSize > 1) && NavMeshOwner && !NavMeshOwner->bAllowNavLinkAsPathEnd)
		{
			uint16 PolyFlags = 0;
			DetourNavMesh->getPolyFlags(PathResult.getRef(PathSize - 1), &PolyFlags);

			if (PolyFlags & ARecastNavMesh::GetNavLinkFlag())
			{
				PathSize--;
			}
		}

		// 拷 Corridor Cost
		Path.PathCorridorCost.AddUninitialized(PathSize);

		if (PathSize == 1)
		{
			// failsafe cost for single poly corridor
			// 单 Poly 特例：Recast 无法给出 cost，用 CalcSegmentCostOnPoly 估算
			Path.PathCorridorCost[0] = CalcSegmentCostOnPoly(StartPolyID, Filter, EndLoc, StartLoc);
		}
		else
		{
			// 迭代目标：把 PathResult 里每段 cost 拷进路径
			for (int32 i = 0; i < PathSize; i++)
			{
				Path.PathCorridorCost[i] = PathResult.getCost(i);
			}
		}
		
		// copy over corridor poly data
		// 拷 Corridor PolyRef 序列
		Path.PathCorridor.AddUninitialized(PathSize);
		NavNodeRef* DestCorridorPoly = Path.PathCorridor.GetData();
		for (int i = 0; i < PathSize; ++i, ++DestCorridorPoly)
		{
			*DestCorridorPoly = PathResult.getRef(i);
		}

		Path.OnPathCorridorUpdated();

		// if we're backtracking this is the time to reverse the path.
		// Backtracking 情形：Recast 是"从终点反向搜索"，产出路径也是反的；
		// 为了拉绳的几何方向与玩家一致，先反转再做拉绳
		if (Filter->getIsBacktracking())
		{
			// for a proper string-pulling of a backtracking path we need to
			// reverse the data right now.
			Path.Invert();
			Swap(StartPolyID, EndPolyID);
			Swap(StartLoc, EndLoc);
			Swap(RecastStartPos, RecastEndPos);
		}

#if STATS
		if (dtStatusDetail(FindPathStatus, DT_OUT_OF_NODES))
		{
			INC_DWORD_STAT(STAT_Navigation_OutOfNodesPath);
		}

		if (dtStatusDetail(FindPathStatus, DT_PARTIAL_RESULT))
		{
			INC_DWORD_STAT(STAT_Navigation_PartialPath);
		}
#endif

		if (Path.WantsStringPulling())
		{
			FVector UseEndLoc = EndLoc;
			
			// if path is partial (path corridor doesn't contain EndPolyID), find new RecastEndPos on last poly in corridor
			// Partial：真正的 End Poly 不在走廊里 → 用走廊末端 Poly 上的最近点作为拉绳终点
			if (dtStatusDetail(FindPathStatus, DT_PARTIAL_RESULT))
			{
				NavNodeRef LastPolyID = Path.PathCorridor.Last();
				FVector::FReal NewEndPoint[3];

				const dtStatus NewEndPointStatus = NavQuery.closestPointOnPoly(LastPolyID, &RecastEndPos.X, NewEndPoint);
				if (dtStatusSucceed(NewEndPointStatus))
				{
					UseEndLoc = Recast2UnrealPoint(NewEndPoint);
				}
			}

			// 调 FNavMeshPath::PerformStringPulling → FindStraightPath
			Path.PerformStringPulling(StartLoc, UseEndLoc);
		}
		else
		{
			// make sure at least beginning and end of path are added
			// 不拉绳：至少把首尾塞进去，供跟随者驱动
			new(Path.GetPathPoints()) FNavPathPoint(StartLoc, StartPolyID);
			new(Path.GetPathPoints()) FNavPathPoint(EndLoc, EndPolyID);

			// collect all custom links Ids
			// 迭代目标：走廊里每个 OffMeshConnection → 记录其 userId 以便 Pathfollow 时判断
			for (int32 Idx = 0; Idx < Path.PathCorridor.Num(); Idx++)
			{
				const dtOffMeshConnection* OffMeshCon = DetourNavMesh->getOffMeshConnectionByRef(Path.PathCorridor[Idx]);
				if (OffMeshCon)
				{
					Path.CustomNavLinkIds.Add(FNavLinkId(OffMeshCon->userId));
				}
			}
		}

		// 若需要走廊边，预先生成（替代将来懒计算，节省首访问开销）
		if (Path.WantsPathCorridor())
		{
			TArray<FNavigationPortalEdge> PathCorridorEdges;
			GetEdgesForPathCorridorImpl(&Path.PathCorridor, &PathCorridorEdges, NavQuery);
			Path.SetPathCorridorEdges(PathCorridorEdges);
		}
	}
}

// FindStraightPath：走廊 → 最短折线（拉绳/漏斗算法的 UE 封装）
// 输入：PathCorridor（Poly 序列）+ Start/End（UE 坐标）
// 流程：
//   1) UE → Recast 坐标翻转
//   2) dtNavMeshQuery::findStraightPath 跑漏斗算法，产出顶点 + Flags
//      （DT_STRAIGHTPATH_AREA_CROSSINGS 让其在 Area 变化处强制插点）
//   3) 逐个顶点：坐标翻回 UE；填 NodeRef / Flags / Area / AreaFlags；
//      若该顶点是 OFFMESH_CONNECTION 端，收集对应 userId 到 CustomLinks
bool FPImplRecastNavMesh::FindStraightPath(const FVector& StartLoc, const FVector& EndLoc, const TArray<NavNodeRef>& PathCorridor, TArray<FNavPathPoint>& PathPoints, TArray<FNavLinkId>* CustomLinks) const
{
	INITIALIZE_NAVQUERY_SIMPLE(NavQuery, RECAST_MAX_SEARCH_NODES);

	const FVector RecastStartPos = Unreal2RecastPoint(StartLoc);
	const FVector RecastEndPos = Unreal2RecastPoint(EndLoc);
	bool bResult = false;

	// DT_STRAIGHTPATH_AREA_CROSSINGS：当 Area 变化时必须插一个路径点，
	// 以便 Pathfollow 能察觉到 "进入了另一种 Area（水/泥地...）" 并切换行为
	dtQueryResult StringPullResult;
	const dtStatus StringPullStatus = NavQuery.findStraightPath(&RecastStartPos.X, &RecastEndPos.X,
		PathCorridor.GetData(), PathCorridor.Num(), StringPullResult, DT_STRAIGHTPATH_AREA_CROSSINGS);

	PathPoints.Reset();
	if (dtStatusSucceed(StringPullStatus))
	{
		PathPoints.AddZeroed(StringPullResult.size());

		// convert to desired format
		// 迭代目标：每个拉绳顶点做坐标翻转 + Flags/Area 打包，并登记 NavLink ID
		FNavPathPoint* CurVert = PathPoints.GetData();

		for (int32 VertIdx = 0; VertIdx < StringPullResult.size(); ++VertIdx)
		{
			const FVector::FReal* CurRecastVert = StringPullResult.getPos(VertIdx);
			CurVert->Location = Recast2UnrVector(CurRecastVert);
			CurVert->NodeRef = StringPullResult.getRef(VertIdx);

			FNavMeshNodeFlags CurNodeFlags(0);
			CurNodeFlags.PathFlags = IntCastChecked<uint8>(StringPullResult.getFlag(VertIdx));

			uint8 AreaID = RECAST_DEFAULT_AREA;
			DetourNavMesh->getPolyArea(CurVert->NodeRef, &AreaID);
			CurNodeFlags.Area = AreaID;

			const UClass* AreaClass = NavMeshOwner->GetAreaClass(AreaID);
			const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
			CurNodeFlags.AreaFlags = DefArea ? DefArea->GetAreaFlags() : 0;

			CurVert->Flags = CurNodeFlags.Pack();

			// include smart link data
			// if there will be more "edge types" we change this implementation to something more generic
			if (CustomLinks && (CurNodeFlags.PathFlags & DT_STRAIGHTPATH_OFFMESH_CONNECTION))
			{
				const dtOffMeshConnection* OffMeshCon = DetourNavMesh->getOffMeshConnectionByRef(CurVert->NodeRef);
				if (OffMeshCon)
				{
					CurVert->CustomNavLinkId.SetId(OffMeshCon->userId);
					CustomLinks->Add(FNavLinkId(OffMeshCon->userId));
				}
			}

			CurVert++;
		}

		// findStraightPath returns 0 for polyId of last point for some reason, even though it knows the poly id.  We will fill that in correctly with the last poly id of the corridor.
		// @TODO shouldn't it be the same as EndPolyID? (nope, it could be partial path)
		PathPoints.Last().NodeRef = PathCorridor.Last();
		bResult = true;
	}

	return bResult;
}

static bool IsDebugNodeModified(const FRecastDebugPathfindingNode& NodeData, const FRecastDebugPathfindingData& PreviousStep)
{
	const FRecastDebugPathfindingNode* PrevNodeData = PreviousStep.Nodes.Find(NodeData);
	if (PrevNodeData)
	{
		const bool bModified = PrevNodeData->bOpenSet != NodeData.bOpenSet ||
			PrevNodeData->TotalCost != NodeData.TotalCost ||
			PrevNodeData->Cost != NodeData.Cost ||
			PrevNodeData->ParentRef != NodeData.ParentRef ||
			!PrevNodeData->NodePos.Equals(NodeData.NodePos, SMALL_NUMBER);

		return bModified;
	}

	return true;
}

static void StorePathfindingDebugData(const dtNavMeshQuery& NavQuery, const dtNavMesh* NavMesh, FRecastDebugPathfindingData& Data)
{
	const dtNodePool* NodePool = NavQuery.getNodePool();
	check(NodePool);

	const int32 NodeCount = NodePool->getNodeCount();
	if (NodeCount <= 0)
	{
		return;
	}
	
	// cache path lengths for all nodes in pool, indexed by poolIdx (idx + 1)
	TArray<FVector::FReal> NodePathLength;
	if (Data.Flags & ERecastDebugPathfindingFlags::PathLength)
	{
		NodePathLength.AddZeroed(NodeCount + 1);
	}

	Data.Nodes.Reserve(NodeCount);
	for (int32 Idx = 0; Idx < NodeCount; Idx++)
	{
		const int32 NodePoolIdx = Idx + 1;
		const dtNode* Node = NodePool->getNodeAtIdx(NodePoolIdx);
		check(Node);

		const dtNode* ParentNode = Node->pidx ? NodePool->getNodeAtIdx(Node->pidx) : nullptr;

		FRecastDebugPathfindingNode NodeInfo;
		NodeInfo.PolyRef = Node->id;
		NodeInfo.ParentRef = ParentNode ? ParentNode->id : 0;
		NodeInfo.Cost = Node->cost; 
		NodeInfo.TotalCost = Node->total;
		NodeInfo.Length = 0.;
		NodeInfo.bOpenSet = (Node->flags & DT_NODE_OPEN) != 0;
		NodeInfo.bModified = true;
		NodeInfo.NodePos = Recast2UnrealPoint(&Node->pos[0]);

		const dtPoly* NavPoly = 0;
		const dtMeshTile* NavTile = 0;
		NavMesh->getTileAndPolyByRef(Node->id, &NavTile, &NavPoly);

		NodeInfo.bOffMeshLink = NavPoly ? (NavPoly->getType() != DT_POLYTYPE_GROUND) : false;
		if (Data.Flags & ERecastDebugPathfindingFlags::Vertices)
		{
			check(NavPoly);

			NodeInfo.NumVerts = NavPoly->vertCount;
			for (int32 VertIdx = 0; VertIdx < NavPoly->vertCount; VertIdx++)
			{
				NodeInfo.Verts.Add((FVector3f)Recast2UnrealPoint(&NavTile->verts[NavPoly->verts[VertIdx] * 3]));
			}
		}

		if ((Data.Flags & ERecastDebugPathfindingFlags::PathLength) && ParentNode)
		{
			const FVector ParentPos = Recast2UnrealPoint(&ParentNode->pos[0]);
			const FVector::FReal NodeLinkLen = FVector::Dist(NodeInfo.NodePos, ParentPos);

			// no point in validating, it would already crash on reading ParentNode (no validation in NodePool.getNodeAtIdx)
			const FVector::FReal ParentPathLength = NodePathLength[Node->pidx];

			const FVector::FReal LinkAndParentLength = NodeLinkLen + ParentPathLength;
			
			NodePathLength[NodePoolIdx] = LinkAndParentLength;

			NodeInfo.Length = LinkAndParentLength;
		}

		Data.Nodes.Add(NodeInfo);
	}

	if (Data.Flags & ERecastDebugPathfindingFlags::BestNode)
	{
		dtNode* BestNode = nullptr;
		FVector::FReal BestNodeCost = 0.0f;
		NavQuery.getCurrentBestResult(BestNode, BestNodeCost);

		if (BestNode)
		{
			const FRecastDebugPathfindingNode BestNodeKey(BestNode->id);
			Data.BestNode = Data.Nodes.FindId(BestNodeKey);
		}
	}
}

static void StorePathfindingDebugStep(const dtNavMeshQuery& NavQuery, const dtNavMesh* NavMesh, TArray<FRecastDebugPathfindingData>& Steps)
{
	const int StepIdx = Steps.AddZeroed(1);
	FRecastDebugPathfindingData& StepInfo = Steps[StepIdx];
	StepInfo.Flags = ERecastDebugPathfindingFlags::BestNode | ERecastDebugPathfindingFlags::Vertices;
	
	StorePathfindingDebugData(NavQuery, NavMesh, StepInfo);

	if (Steps.Num() > 1)
	{
		FRecastDebugPathfindingData& PrevStepInfo = Steps[StepIdx - 1];
		for (TSet<FRecastDebugPathfindingNode>::TIterator It(StepInfo.Nodes); It; ++It)
		{
			FRecastDebugPathfindingNode& NodeData = *It;
			NodeData.bModified = IsDebugNodeModified(NodeData, PrevStepInfo);
		}
	}
}

// 用 dtNavMeshQuery 的 Sliced（分步）寻路 API 一步步驱动 A*，并把每一步的搜索节点状态截存到 Steps。
// 用于 NavMesh 调试可视化（高亮被打开/关闭/修改的节点）。流程：initSlicedFindPath → 反复 updateSlicedFindPath(1)
// → 每步调用 StorePathfindingDebugStep → finalizeSlicedFindPath。返回执行的步数。
int32 FPImplRecastNavMesh::DebugPathfinding(const FVector& StartLoc, const FVector& EndLoc, const FVector::FReal CostLimit, const bool bRequireNavigableEndLocation, const FNavigationQueryFilter& Filter, const UObject* Owner, TArray<FRecastDebugPathfindingData>& Steps)
{
	int32 NumSteps = 0;

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	if (QueryFilter == NULL)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::DebugPathfinding failing due to QueryFilter == NULL"));
		return NumSteps;
	}

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes(), LinkFilter);
	NavQuery.setRequireNavigableEndLocation(bRequireNavigableEndLocation);

	FVector RecastStartPos, RecastEndPos;
	NavNodeRef StartPolyID, EndPolyID;
	const bool bCanSearch = InitPathfinding(StartLoc, EndLoc, NavQuery, QueryFilter, RecastStartPos, StartPolyID, RecastEndPos, EndPolyID);
	if (!bCanSearch)
	{
		return NumSteps;
	}

	dtStatus status = NavQuery.initSlicedFindPath(StartPolyID, EndPolyID, &RecastStartPos.X, &RecastEndPos.X, CostLimit, bRequireNavigableEndLocation, QueryFilter);
	while (dtStatusInProgress(status))
	{
		StorePathfindingDebugStep(NavQuery, DetourNavMesh, Steps);
		NumSteps++;

		status = NavQuery.updateSlicedFindPath(1, 0);
	}

	static const int32 MAX_TEMP_POLYS = 16;
	NavNodeRef TempPolys[MAX_TEMP_POLYS];
	int32 NumTempPolys;
	NavQuery.finalizeSlicedFindPath(TempPolys, &NumTempPolys, MAX_TEMP_POLYS);

	return NumSteps;
}

#if WITH_NAVMESH_CLUSTER_LINKS
// 由 PolyRef 反查所属 Cluster 的 NodeRef（仅 WITH_NAVMESH_CLUSTER_LINKS 时启用）。
// 通过 dtNavMesh::getTileByRef + decodePolyIdPoly 取出 Tile 与 Poly 索引，
// 再读 Tile->polyClusters 数组得到 Cluster Index，最后与 getClusterRefBase 合成 ClusterRef。
NavNodeRef FPImplRecastNavMesh::GetClusterRefFromPolyRef(const NavNodeRef PolyRef) const
{
	if (DetourNavMesh)
	{
		const dtMeshTile* Tile = DetourNavMesh->getTileByRef(PolyRef);
		uint32 PolyIdx = DetourNavMesh->decodePolyIdPoly(PolyRef);
		if (Tile && Tile->polyClusters && PolyIdx < (uint32)Tile->header->offMeshBase)
		{
			return DetourNavMesh->getClusterRefBase(Tile) | Tile->polyClusters[PolyIdx];
		}
	}

	return 0;
}
#endif // WITH_NAVMESH_CLUSTER_LINKS

// 在整个 NavMesh 上随机取一个可行点（按 QueryFilter 通过的 Poly 中均匀采样）。
// 转发到 dtNavMeshQuery::findRandomPoint；结果坐标从 Recast 翻回 UE 后填到 OutLocation。
FNavLocation FPImplRecastNavMesh::GetRandomPoint(const FNavigationQueryFilter& Filter, const UObject* Owner) const
{
	FNavLocation OutLocation;
	if (DetourNavMesh == NULL)
	{
		return OutLocation;
	}

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes(), LinkFilter);

	// inits to "pass all"
	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter)
	{
		dtPolyRef Poly;
		FVector::FReal RandPt[3];
		dtStatus Status = NavQuery.findRandomPoint(QueryFilter, FMath::FRand, &Poly, RandPt);
		if (dtStatusSucceed(Status))
		{
			// arrange output
			OutLocation.Location = Recast2UnrVector(RandPt);
			OutLocation.NodeRef = Poly;
		}
	}

	return OutLocation;
}

#if WITH_NAVMESH_CLUSTER_LINKS
// 在指定 Cluster 内部随机取一个点（仅 WITH_NAVMESH_CLUSTER_LINKS）。
// 转发到 dtNavMeshQuery::findRandomPointInCluster，输出 Recast→UE 翻转后的位置与所在 Poly。
bool FPImplRecastNavMesh::GetRandomPointInCluster(NavNodeRef ClusterRef, FNavLocation& OutLocation) const
{
	if (DetourNavMesh == NULL || ClusterRef == 0)
	{
		return false;
	}

	INITIALIZE_NAVQUERY_SIMPLE(NavQuery, RECAST_MAX_SEARCH_NODES);

	dtPolyRef Poly;
	FVector::FReal RandPt[3];
	dtStatus Status = NavQuery.findRandomPointInCluster(ClusterRef, FMath::FRand, &Poly, RandPt);

	if (dtStatusSucceed(Status))
	{
		OutLocation = FNavLocation(Recast2UnrVector(RandPt), Poly);
		return true;
	}

	return false;
}
#endif // WITH_NAVMESH_CLUSTER_LINKS

// 让一个点沿 NavMesh 表面"滑动"到目标位置（不做 A*，只在相邻 Poly 间步进）。
// 转发到 dtNavMeshQuery::moveAlongSurface，受 QueryFilter 限制；返回最终走到的位置与所在 Poly。
// 末尾用 getPolyHeight 把 Y(垂直) 方向贴到 NavMesh 表面，再翻回 UE 坐标。常用于 AI 微调位置 / 受击位移。
bool FPImplRecastNavMesh::FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, const FNavigationQueryFilter& Filter, const UObject* Owner) const
{
	// sanity check
	if (DetourNavMesh == NULL)
	{
		return false;
	}

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes(), LinkFilter);

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (!QueryFilter)
	{
		return false;
	}

	FVector RcStartPos = Unreal2RecastPoint(StartLocation.Location);
	FVector RcEndPos = Unreal2RecastPoint(TargetPosition);

	FVector::FReal Result[3];
	static const int MAX_VISITED = 16;
	dtPolyRef Visited[MAX_VISITED];
	int VisitedCount = 0;

	dtStatus status = NavQuery.moveAlongSurface(StartLocation.NodeRef, &RcStartPos.X, &RcEndPos.X, QueryFilter, Result, Visited, &VisitedCount, MAX_VISITED);
	if (dtStatusFailed(status))
	{
		return false;
	}
	dtPolyRef ResultPoly = Visited[VisitedCount - 1];

	// Adjust the position to stay on top of the navmesh.
	FVector::FReal h = RcStartPos.Y;
	NavQuery.getPolyHeight(ResultPoly, Result, &h);
	Result[1] = h;

	const FVector UnrealResult = Recast2UnrVector(Result);

	OutLocation = FNavLocation(UnrealResult, ResultPoly);

	return true;
}

// 把空间中一点投影到 NavMesh 上"最近的可行点"（单一最近点投影策略）。
// 转发到 dtNavMeshQuery::findNearestPoly2D（不使用节点池，NumNodes=0），取在 Extent 内的最近 Poly + 最近点。
// 由于 Recast BVTree 存在精度误差，命中后还会用 ModifiedExtent 做一次 AABB 校验剔除越界结果。
bool FPImplRecastNavMesh::ProjectPointToNavMesh(const FVector& Point, FNavLocation& Result, const FVector& Extent, const FNavigationQueryFilter& Filter, const UObject* Owner) const
{
	// sanity check
	if (DetourNavMesh == NULL)
	{
		return false;
	}

	bool bSuccess = false;

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	// using 0 as NumNodes since findNearestPoly2D, being the only dtNavMeshQuery
	// function we're using, is not utilizing m_nodePool
	INITIALIZE_NAVQUERY(NavQuery, /*NumNodes=*/0, LinkFilter);

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter)
	{
		FVector::FReal ClosestPoint[3];

		const FVector ModifiedExtent = NavMeshOwner->GetModifiedQueryExtent(Extent);
		FVector RcExtent = Unreal2RecastPoint(ModifiedExtent).GetAbs();
	
		FVector RcPoint = Unreal2RecastPoint(Point);
		dtPolyRef PolyRef;
		NavQuery.findNearestPoly2D(&RcPoint.X, &RcExtent.X, QueryFilter, &PolyRef, ClosestPoint);

		if( PolyRef > 0 )
		{
			// one last step required due to recast's BVTree imprecision
			const FVector& UnrealClosestPoint = Recast2UnrVector(ClosestPoint);			
			const FVector ClosestPointDelta = UnrealClosestPoint - Point;
			if (-ModifiedExtent.X <= ClosestPointDelta.X && ClosestPointDelta.X <= ModifiedExtent.X
				&& -ModifiedExtent.Y <= ClosestPointDelta.Y && ClosestPointDelta.Y <= ModifiedExtent.Y
				&& -ModifiedExtent.Z <= ClosestPointDelta.Z && ClosestPointDelta.Z <= ModifiedExtent.Z)
			{
				bSuccess = true;
				Result = FNavLocation(UnrealClosestPoint, PolyRef);
			}
			else
			{
				const UObject* LogOwner = Owner ? Owner : NavMeshOwner;
				UE_VLOG(LogOwner, LogNavigation, Error, TEXT("ProjectPointToNavMesh failed due to ClosestPoint being too far away from projected point."));
				UE_VLOG_LOCATION(LogOwner, LogNavigation, Error, Point, 30, FColor::Blue, TEXT("Requested point"));
				UE_VLOG_LOCATION(LogOwner, LogNavigation, Error, UnrealClosestPoint, 30, FColor::Red, TEXT("Projection result"));
				UE_VLOG_SEGMENT(LogOwner, LogNavigation, Error, Point, UnrealClosestPoint, FColor::Red, TEXT(""));
			}
		}
	}

	return (bSuccess);
}

// 在 [MinZ, MaxZ] 垂直范围内做"多重投影"：返回所有命中的 Poly（如多层楼板时上下楼层都会命中）。
// 与 ProjectPointToNavMesh 的"单一最近点"策略不同：先用 dtNavMeshQuery::queryPolygons 取 AABB 内所有候选 Poly，
// 再对每块 Poly 用 projectedPointOnPoly + getPolyHeight 求精确高度，全部加入 Result 数组。
bool FPImplRecastNavMesh::ProjectPointMulti(const FVector& Point, TArray<FNavLocation>& Result, const FVector& Extent,
	FVector::FReal MinZ, FVector::FReal MaxZ, const FNavigationQueryFilter& Filter, const UObject* Owner) const
{
	// sanity check
	if (DetourNavMesh == NULL)
	{
		return false;
	}

	bool bSuccess = false;

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes(), LinkFilter);

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter)
	{
		const FVector ModifiedExtent = NavMeshOwner->GetModifiedQueryExtent(Extent);
		const FVector AdjustedPoint(Point.X, Point.Y, (MaxZ + MinZ) * 0.5f);
		const FVector AdjustedExtent(ModifiedExtent.X, ModifiedExtent.Y, (MaxZ - MinZ) * 0.5f);

		const FVector RcPoint = Unreal2RecastPoint( AdjustedPoint );
		const FVector RcExtent = Unreal2RecastPoint( AdjustedExtent ).GetAbs();

		const int32 MaxHitPolys = 256;
		dtPolyRef HitPolys[MaxHitPolys];
		int32 NumHitPolys = 0;

		dtStatus status = NavQuery.queryPolygons(&RcPoint.X, &RcExtent.X, QueryFilter, HitPolys, &NumHitPolys, MaxHitPolys);
		if (dtStatusSucceed(status))
		{
			for (int32 i = 0; i < NumHitPolys; i++)
			{
				FVector::FReal ClosestPoint[3];
				
				status = NavQuery.projectedPointOnPoly(HitPolys[i], &RcPoint.X, ClosestPoint);
				if (dtStatusSucceed(status))
				{
					FVector::FReal ExactZ = 0.0f;
					status = NavQuery.getPolyHeight(HitPolys[i], ClosestPoint, &ExactZ);
					if (dtStatusSucceed(status))
					{
						FNavLocation HitLocation(Recast2UnrealPoint(ClosestPoint), HitPolys[i]);
						HitLocation.Location.Z = ExactZ;

						ensure((HitLocation.Location - AdjustedPoint).SizeSquared2D() < KINDA_SMALL_NUMBER);

						Result.Add(HitLocation);
						bSuccess = true;
					}
				}
			}
		}
	}

	return bSuccess;
}

// 找到给定点附近 Extent 范围内"最近"的 NavMesh Poly。
// 转发到 dtNavMeshQuery::findNearestPoly2D；输入会先按 NavMeshOwner->GetModifiedQueryExtent 调整，
// 并经 Unr2Recast 坐标翻转。返回 Poly 的 NavNodeRef，找不到则返回 INVALID_NAVNODEREF。
NavNodeRef FPImplRecastNavMesh::FindNearestPoly(FVector const& Loc, FVector const& Extent, const FNavigationQueryFilter& Filter, const UObject* Owner) const
{
	// sanity check
	if (DetourNavMesh == NULL)
	{
		return INVALID_NAVNODEREF;
	}

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes(), LinkFilter);

	// inits to "pass all"
	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter)
	{
		FVector::FReal  RecastLoc[3];
		Unr2RecastVector(Loc, RecastLoc);
		FVector::FReal  RecastExtent[3];
		Unr2RecastSizeVector(NavMeshOwner->GetModifiedQueryExtent(Extent), RecastExtent);

		NavNodeRef OutRef;
		dtStatus Status = NavQuery.findNearestPoly(RecastLoc, RecastExtent, QueryFilter, &OutRef, NULL);
		if (dtStatusSucceed(Status))
		{
			return OutRef;
		}
	}

	return INVALID_NAVNODEREF;
}

// 在以 CenterPos 为圆心、Radius 为半径的圆形区域内 Dijkstra 扩张，输出所有可达 Poly。
// 转发到 dtNavMeshQuery::findPolysAroundCircle。可选输出：每块 Poly 的父节点(OutPolysParent)与
// 累计 Cost(OutPolysCost)。MaxSearchNodes 受 Filter 配置限制（硬上限 4096，超出请用 GetPolyNeighbors 自循环）。
bool FPImplRecastNavMesh::FindPolysAroundCircle(const FVector& CenterPos, const NavNodeRef CenterNodeRef, const FVector::FReal Radius, const FNavigationQueryFilter& Filter, const UObject* Owner, TArray<NavNodeRef>* OutPolys, TArray<NavNodeRef>* OutPolysParent, TArray<float>* OutPolysCost, int* OutPolysCount) const
{
	// sanity check
	if (DetourNavMesh == NULL || NavMeshOwner == NULL || CenterNodeRef == INVALID_NAVNODEREF)
	{
		return false;
	}

	TArray<FVector::FReal> PolysCost;

	// limit max number of polys found by that function
	// if you need more, please scan manually using ARecastNavMesh::GetPolyNeighbors for A*/Dijkstra loop
	const int32 MaxSearchLimit = 4096;
	const int32 MaxSearchNodes = Filter.GetMaxSearchNodes();
	ensureMsgf(MaxSearchNodes > 0 && MaxSearchNodes <= MaxSearchLimit, TEXT("MaxSearchNodes:%d is not within range: 0..%d"), MaxSearchNodes, MaxSearchLimit);

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes(), LinkFilter);

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	if (ensure(QueryFilter))
	{
		if (OutPolys)
		{
			OutPolys->Reset();
			OutPolys->AddUninitialized(MaxSearchNodes);
		}

		if (OutPolysParent)
		{
			OutPolysParent->Reset();
			OutPolysParent->AddUninitialized(MaxSearchNodes);
		}

		if (OutPolysCost)
		{
			PolysCost.Reset();
			PolysCost.AddUninitialized(MaxSearchNodes);
		}

		FVector::FReal RecastLoc[3];
		Unr2RecastVector(CenterPos, RecastLoc);
		const dtStatus Status = NavQuery.findPolysAroundCircle(CenterNodeRef, RecastLoc, Radius, QueryFilter, OutPolys ? OutPolys->GetData() : nullptr, OutPolysParent ? OutPolysParent->GetData() : nullptr, OutPolysCost ? PolysCost.GetData() : nullptr, OutPolysCount, MaxSearchNodes);

		if (OutPolysCost)
		{
			*OutPolysCost = UE::LWC::ConvertArrayTypeClampMax<float>(PolysCost);
		}

		if (dtStatusSucceed(Status))
		{
			return true;
		}
	}

	return false;
}

// 从 StartLoc 出发、按 NavMesh 路径距离（不是直线距离）扩张，收集所有距离 ≤ PathingDistance 的 Poly。
// 转发到 dtNavMeshQuery::findPolysInPathDistance。先用 findNearestPoly 把起点贴 Poly。
// 可选输出 FRecastDebugPathfindingData 用于调试着色（节点 open/closed 状态）。
bool FPImplRecastNavMesh::GetPolysWithinPathingDistance(FVector const& StartLoc, const FVector::FReal PathingDistance,
	const FNavigationQueryFilter& Filter, const UObject* Owner,
	TArray<NavNodeRef>& FoundPolys, FRecastDebugPathfindingData* OutDebugData) const
{
	ensure(PathingDistance > 0. && "PathingDistance <= 0 doesn't make sense");
	
	// limit max number of polys found by that function
	// if you need more, please scan manually using ARecastNavMesh::GetPolyNeighbors for A*/Dijkstra loop
	const int32 MaxSearchLimit = 4096;
	const int32 MaxSearchNodes = Filter.GetMaxSearchNodes();
	ensureMsgf(MaxSearchNodes > 0 && MaxSearchNodes <= MaxSearchLimit, TEXT("MaxSearchNodes:%d is not within range: 0..%d"), MaxSearchNodes, MaxSearchLimit);

	// sanity check
	if (DetourNavMesh == nullptr || MaxSearchNodes <= 0 || MaxSearchNodes > MaxSearchLimit)
	{
		return false;
	}

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), Owner);
	INITIALIZE_NAVQUERY(NavQuery, MaxSearchNodes, LinkFilter);

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter == nullptr)
	{
		return false;
	}

	// @todo this should be configurable in some kind of FindPathQuery structure
	const FVector NavExtent = NavMeshOwner->GetModifiedQueryExtent(NavMeshOwner->GetDefaultQueryExtent());
	const FVector::FReal Extent[3] = { NavExtent.X, NavExtent.Z, NavExtent.Y };

	FVector::FReal RecastStartPos[3];
	Unr2RecastVector(StartLoc, RecastStartPos);
	// @TODO add failure handling
	NavNodeRef StartPolyID = INVALID_NAVNODEREF;
	NavQuery.findNearestPoly(RecastStartPos, Extent, QueryFilter, &StartPolyID, NULL);

	FoundPolys.Reset(MaxSearchNodes);
	FoundPolys.AddUninitialized(MaxSearchNodes);
	int32 NumPolys = 0;

	NavQuery.findPolysInPathDistance(StartPolyID, RecastStartPos, PathingDistance, QueryFilter, FoundPolys.GetData(), &NumPolys, MaxSearchNodes);
	FoundPolys.RemoveAt(NumPolys, FoundPolys.Num() - NumPolys);

	if (OutDebugData)
	{
		StorePathfindingDebugData(NavQuery, DetourNavMesh, *OutDebugData);
	}

	return FoundPolys.Num() > 0;
}

// 根据 NavLink UserId 更新对应 OffMeshConnection 的 AreaType 和 PolyFlags。
// 转发到 dtNavMesh::updateOffMeshConnectionByUserId（运行时动态修改 NavLink 区域属性）。
void FPImplRecastNavMesh::UpdateNavigationLinkArea(FNavLinkId UserId, uint8 AreaType, uint16 PolyFlags) const
{
	if (DetourNavMesh)
	{
		DetourNavMesh->updateOffMeshConnectionByUserId(UserId.GetId(), AreaType, PolyFlags);
	}
}

#if WITH_NAVMESH_SEGMENT_LINKS
// 根据 SegmentLink UserId 更新 OffMeshSegmentConnection 的 AreaType 和 PolyFlags（仅 WITH_NAVMESH_SEGMENT_LINKS）。
// 转发到 dtNavMesh::updateOffMeshSegmentConnectionByUserId。
void FPImplRecastNavMesh::UpdateSegmentLinkArea(int32 UserId, uint8 AreaType, uint16 PolyFlags) const
{
	if (DetourNavMesh)
	{
		DetourNavMesh->updateOffMeshSegmentConnectionByUserId(UserId, AreaType, PolyFlags);
	}
}

// 处理指定 Tile 的 Segment Link（与相邻 Tile 之间生成跨 Tile 段连接）的简化重载。
// 内部调用下方完整重载，不收集 SkippedNeighbors。
void FPImplRecastNavMesh::ProcessSegmentLinksForTile(FNavTileRef TileRef) const
{
	uint32 NumSkippedNeighborTiles;
	ProcessSegmentLinksForTile(TileRef, 0 /*MaxSkippedNeigborTiles*/, nullptr /*OutSkippedNeigborTiles*/, NumSkippedNeighborTiles);
}

// 处理指定 Tile 的 Segment Link 的完整重载：转发到 dtNavMesh::processSegmentLinksForTile。
// 当邻居 Tile 尚未加载时，最多记录 MaxSkippedNeigborTiles 个跳过的邻居 TileRef 到 OutSkippedNeigborTiles，
// 调用方可在邻居 Tile 加载后再次触发处理以补全跨 Tile Link。
void FPImplRecastNavMesh::ProcessSegmentLinksForTile(FNavTileRef TileRef, uint32 MaxSkippedNeigborTiles, dtTileRef* OutSkippedNeigborTiles, uint32& OutNumSkippedNeigborTiles) const
{
	if (DetourNavMesh)
	{
		DetourNavMesh->processSegmentLinksForTile(static_cast<dtTileRef>(TileRef), MaxSkippedNeigborTiles, OutSkippedNeigborTiles, OutNumSkippedNeigborTiles);
	}
}
#endif // WITH_NAVMESH_SEGMENT_LINKS

// 计算 Poly 的几何中心点。
// 数据来源：通过 dtNavMesh::getTileAndPolyByRef 取出所属 Tile 与 dtPoly，
// 然后对 Tile->verts[] 中由 Poly->verts[] 索引出来的所有顶点取算术平均，
// 最后用 Recast2UnrVector 翻回 UE 坐标系。
bool FPImplRecastNavMesh::GetPolyCenter(NavNodeRef PolyID, FVector& OutCenter) const
{
	if (DetourNavMesh)
	{
		// get poly data from recast
		dtPoly const* Poly;
		dtMeshTile const* Tile;
		dtStatus Status = DetourNavMesh->getTileAndPolyByRef((dtPolyRef)PolyID, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			// average verts
			FVector::FReal Center[3] = {0,0,0};

			for (uint32 VertIdx=0; VertIdx < Poly->vertCount; ++VertIdx)
			{
				const FVector::FReal* V = &Tile->verts[Poly->verts[VertIdx]*3];
				Center[0] += V[0];
				Center[1] += V[1];
				Center[2] += V[2];
			}
			const FVector::FReal InvCount = 1.0f / Poly->vertCount;
			Center[0] *= InvCount;
			Center[1] *= InvCount;
			Center[2] *= InvCount;

			// convert output to UE coords
			OutCenter = Recast2UnrVector(Center);

			return true;
		}
	}

	return false;
}

// 取得 Poly 的所有顶点（UE 坐标系）。
// 数据来源：dtNavMesh::getTileAndPolyByRef → 读 Tile->verts 配合 Poly->verts 索引。
// 特殊处理：DT_POLYTYPE_OFFMESH_SEGMENT 类型的 Poly，第 2/3 顶点存储顺序需要交换以保证顺时针绘制（避免段连接 Poly 扭曲）。
bool FPImplRecastNavMesh::GetPolyVerts(NavNodeRef PolyID, TArray<FVector>& OutVerts) const
{
	if (DetourNavMesh)
	{
		// get poly data from recast
		dtPoly const* Poly;
		dtMeshTile const* Tile;
		dtStatus Status = DetourNavMesh->getTileAndPolyByRef((dtPolyRef)PolyID, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			// flush and pre-size output array
			OutVerts.Reset(Poly->vertCount);

			// convert to UE coords and copy verts into output array 
			for (uint32 VertIdx=0; VertIdx < Poly->vertCount; ++VertIdx)
			{
				const FVector::FReal* V = &Tile->verts[Poly->verts[VertIdx]*3];
				OutVerts.Add( Recast2UnrVector(V) );
			}

#if WITH_NAVMESH_SEGMENT_LINKS
			// Segment polys store each segment edge from left to right.
			// Therefore, swap the edge stored at indices 2 and 3 to ensure the poly verts
			// are in proper clockwise order for drawing to avoid twisted segment link polys.
			if (Poly->getType() == DT_POLYTYPE_OFFMESH_SEGMENT)
			{
				OutVerts.SwapMemory(2, 3);
			}
#endif // WITH_NAVMESH_SEGMENT_LINKS

			return true;
		}
	}

	return false;
}

// 在指定 Poly 内部均匀随机取一点。
// 转发到 dtNavMeshQuery::findRandomPointInPoly，输出经 Recast→UE 翻转的世界坐标。
bool FPImplRecastNavMesh::GetRandomPointInPoly(NavNodeRef PolyID, FVector& OutPoint) const
{
	if (DetourNavMesh)
	{
		INITIALIZE_NAVQUERY_SIMPLE(NavQuery, RECAST_MAX_SEARCH_NODES);

		FVector::FReal RandPt[3];
		dtStatus Status = NavQuery.findRandomPointInPoly((dtPolyRef)PolyID, FMath::FRand, RandPt);
		if (dtStatusSucceed(Status))
		{
			OutPoint = Recast2UnrVector(RandPt);
			return true;
		}
	}

	return false;
}

FVector::FReal FPImplRecastNavMesh::GetPolySurfaceArea(NavNodeRef PolyID) const
{
	if (DetourNavMesh)
	{
		dtPoly const* Poly = 0;
		dtMeshTile const* Tile = 0;
		dtStatus Status = DetourNavMesh->getTileAndPolyByRef((dtPolyRef)PolyID, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			// Calc area of the polygon.
			dtReal PolyArea = 0;
			for (int j = 2; j < Poly->vertCount; ++j)
			{
				const dtReal* VA = &Tile->verts[Poly->verts[0] * 3];
				const dtReal* VB = &Tile->verts[Poly->verts[j - 1] * 3];
				const dtReal* VC = &Tile->verts[Poly->verts[j] * 3];
				PolyArea += dtTriArea2D(VA, VB, VC);
			}

			return (FVector::FReal)PolyArea;
		}
	}

	return 0;
}

// 取出指定 Poly 的 Area ID（区域分类，对应 NavArea 类）。
// 通过 dtNavMesh::getTileAndPolyByRef 拿到 dtPoly，调用其 getArea()。失败时返回 RECAST_NULL_AREA。
uint32 FPImplRecastNavMesh::GetPolyAreaID(NavNodeRef PolyID) const
{
	uint32 AreaID = RECAST_NULL_AREA;

	if (DetourNavMesh)
	{
		// get poly data from recast
		dtPoly const* Poly;
		dtMeshTile const* Tile;
		dtStatus Status = DetourNavMesh->getTileAndPolyByRef((dtPolyRef)PolyID, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			AreaID = Poly->getArea();
		}
	}

	return AreaID;
}

// 设置指定 Poly 的 Area ID。转发到 dtNavMesh::setPolyArea，会修改对应 Tile 内 dtPoly 的区域分类位。
void FPImplRecastNavMesh::SetPolyAreaID(NavNodeRef PolyID, uint8 AreaID)
{
	if (DetourNavMesh)
	{
		DetourNavMesh->setPolyArea((dtPolyRef)PolyID, AreaID);
	}
}

// 一次性取出 Poly 的 Flags（PolyFlags 位掩码）和 AreaType（NavArea 编号）。
// 通过 dtNavMesh::getTileAndPolyByRef 直接读取 dtPoly 字段。
bool FPImplRecastNavMesh::GetPolyData(NavNodeRef PolyID, uint16& Flags, uint8& AreaType) const
{
	if (DetourNavMesh)
	{
		// get poly data from recast
		dtPoly const* Poly;
		dtMeshTile const* Tile;
		dtStatus Status = DetourNavMesh->getTileAndPolyByRef((dtPolyRef)PolyID, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			Flags = Poly->flags;
			AreaType = Poly->getArea();
			return true;
		}
	}

	return false;
}

// 取得 Poly 所有邻居 Poly 的 Portal Edge（与邻居共享的过道边，含 Left/Right 端点 + ToRef）。
// 数据来源：先 getTileAndPolyByRef 拿到 Poly，再沿其 firstLink 链表遍历每条 dtLink，
// 对每条 Link 调用 dtNavMeshQuery::getPortalPoints 求出门户两端在 Recast 空间的坐标，翻回 UE 后塞入 Neighbors。
bool FPImplRecastNavMesh::GetPolyNeighbors(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Neighbors) const
{
	if (DetourNavMesh)
	{
		dtPolyRef PolyRef = (dtPolyRef)PolyID;
		dtPoly const* Poly = 0;
		dtMeshTile const* Tile = 0;

		dtStatus Status = DetourNavMesh->getTileAndPolyByRef(PolyRef, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			INITIALIZE_NAVQUERY_SIMPLE(NavQuery, RECAST_MAX_SEARCH_NODES);

			FVector::FReal RcLeft[3], RcRight[3];
			uint8 DummyType1, DummyType2;

			uint32 LinkIdx = Poly->firstLink;
			while (LinkIdx != DT_NULL_LINK)
			{
				const dtLink& Link = DetourNavMesh->getLink(Tile, LinkIdx);
				LinkIdx = Link.next;
				
				Status = NavQuery.getPortalPoints(PolyRef, Link.ref, RcLeft, RcRight, DummyType1, DummyType2);
				if (dtStatusSucceed(Status))
				{
					FNavigationPortalEdge NeiData;
					NeiData.ToRef = Link.ref;
					NeiData.Left = Recast2UnrealPoint(RcLeft);
					NeiData.Right = Recast2UnrealPoint(RcRight);

					Neighbors.Add(NeiData);
				}
			}

			return true;
		}
	}

	return false;
}

// GetPolyNeighbors 的轻量重载：只返回邻居的 NavNodeRef，不计算 Portal 端点。
// 沿 Poly->firstLink 遍历 dtLink 链表，把 Link.ref 全部塞入 Neighbors。
bool FPImplRecastNavMesh::GetPolyNeighbors(NavNodeRef PolyID, TArray<NavNodeRef>& Neighbors) const
{
	if (DetourNavMesh)
	{
		const dtPolyRef PolyRef = static_cast<dtPolyRef>(PolyID);
		dtPoly const* Poly = 0;
		dtMeshTile const* Tile = 0;

		const dtStatus Status = DetourNavMesh->getTileAndPolyByRef(PolyRef, &Tile, &Poly);

		if (dtStatusSucceed(Status))
		{
			uint32 LinkIdx = Poly->firstLink;
			Neighbors.Reserve(DT_VERTS_PER_POLYGON);

			while (LinkIdx != DT_NULL_LINK)
			{
				const dtLink& Link = DetourNavMesh->getLink(Tile, LinkIdx);
				LinkIdx = Link.next;

				Neighbors.Add(Link.ref);
			}

			return true;
		}
	}

	return false;
}

// 取出 Poly 的所有"边"（与 GetPolyNeighbors 相比，按 Poly 顶点边来描述，而不是 Portal 中点）。
// 数据来源：遍历 Poly->firstLink 链表的每个 dtLink，用 LinkInfo.edge 索引 Poly->verts 找到边的两顶点。
// 若是 NavLink/Off-Mesh 类型 Poly，Right 端会等于 Left（退化为点而不是真实边）。
bool FPImplRecastNavMesh::GetPolyEdges(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Edges) const
{
	if (DetourNavMesh)
	{
		dtPolyRef PolyRef = (dtPolyRef)PolyID;
		dtPoly const* Poly = 0;
		dtMeshTile const* Tile = 0;

		dtStatus Status = DetourNavMesh->getTileAndPolyByRef(PolyRef, &Tile, &Poly);
		if (dtStatusSucceed(Status))
		{
			const bool bIsNavLink = (Poly->getType() != DT_POLYTYPE_GROUND);

			for (uint32 LinkIt = Poly->firstLink; LinkIt != DT_NULL_LINK;)
			{
				const dtLink& LinkInfo = DetourNavMesh->getLink(Tile, LinkIt);
				if (LinkInfo.edge >= 0 && LinkInfo.edge < Poly->vertCount)
				{
					FNavigationPortalEdge NeiData;
					NeiData.Left = Recast2UnrealPoint(&Tile->verts[3 * Poly->verts[LinkInfo.edge]]);
					NeiData.Right = bIsNavLink ? NeiData.Left : Recast2UnrealPoint(&Tile->verts[3 * Poly->verts[(LinkInfo.edge + 1) % Poly->vertCount]]);
					NeiData.ToRef = LinkInfo.ref;
					Edges.Add(NeiData);
				}

				LinkIt = LinkInfo.next;
			}

			return true;
		}
	}

	return false;
}

// 取出 Poly 周围"墙体段"——即按 Filter 不可通行的边（不能从该边走到邻居 Poly）。
// 转发到 dtNavMeshQuery::getPolyWallSegments，最多返回 64 段；输出端点经 Recast→UE 翻转。
// 用于 AI 避障/视线判断（已被过滤器禁通的边视为墙）。
bool FPImplRecastNavMesh::GetPolyWallSegments(NavNodeRef PolyID, const FNavigationQueryFilter& InQueryFilter, const UObject* QueryOwner, TArray<FNavigationPortalEdge>& OutNeighbors) const
{
	const FRecastQueryFilter* FilterImplementation = (const FRecastQueryFilter*)(InQueryFilter.GetImplementation());
	if (FilterImplementation == nullptr)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Error, TEXT("%hs failed due to passed filter having NULL implementation!"), __FUNCTION__);
		return false;
	}

	const dtQueryFilter* QueryFilter = FilterImplementation->GetAsDetourQueryFilter();
	if (QueryFilter == nullptr)
	{
		UE_VLOG(NavMeshOwner, LogNavigation, Warning, TEXT("%hs failed due to QueryFilter == nullptr"), __FUNCTION__);
		return false;
	}
	
	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMeshOwner->GetWorld()), QueryOwner);
	INITIALIZE_NAVQUERY(NavQuery, InQueryFilter.GetMaxSearchNodes(), LinkFilter);

	constexpr int32 MaxSegments = 64;
	constexpr int32 ComponentsPerSegment = 6;
	dtReal RcVertices[MaxSegments * ComponentsPerSegment] = { 0 }; // segments * ax,ay,az,bx,by,bz

	int32 NumSegments = 0;
	dtPolyRef SegmentRefs[MaxSegments] = { 0 };
	
	if (dtStatusSucceed(NavQuery.getPolyWallSegments(PolyID, QueryFilter, RcVertices, SegmentRefs, &NumSegments, MaxSegments)))
	{
		OutNeighbors.SetNum(NumSegments);
	
		for (int32 i = 0; i < NumSegments; ++i)
		{
			FNavigationPortalEdge& Edge = OutNeighbors[i];
			Edge.Left = Recast2UnrealPoint(&RcVertices[i * ComponentsPerSegment]);
			Edge.Right = Recast2UnrealPoint(&RcVertices[i * ComponentsPerSegment + 3]);
			Edge.ToRef = SegmentRefs[i];
		}
		return true;
	}

	OutNeighbors.SetNum(0);
	return false;
}

// 把 PolyRef 拆解为所属 Tile 的索引和 Poly 在 Tile 内的索引。
// 转发到 dtNavMesh::decodePolyId（PolyRef 内编码为 Salt|TileIndex|PolyIndex）。
bool FPImplRecastNavMesh::GetPolyTileIndex(NavNodeRef PolyID, uint32& PolyIndex, uint32& TileIndex) const
{
	if (DetourNavMesh && PolyID)
	{
		uint32 SaltIdx = 0;
		DetourNavMesh->decodePolyId(PolyID, SaltIdx, TileIndex, PolyIndex);
		return true;
	}

	return false;
}

// 取得 Poly 在所属 Tile 内的索引以及 Tile 自身的 FNavTileRef。
// 与 GetPolyTileIndex 相比，额外用 dtNavMesh::encodePolyId(Salt, TileIndex, 0) 重新编码出 dtTileRef。
bool FPImplRecastNavMesh::GetPolyTileRef(NavNodeRef PolyId, uint32& OutPolyIndex, FNavTileRef& OutTileRef) const
{
	if (DetourNavMesh && PolyId)
	{
		// Similar to UE::NavMesh::Private::GetTileRefFromPolyRef
		unsigned int Salt = 0;
		unsigned int TileIndex = 0;
		DetourNavMesh->decodePolyId(PolyId, Salt, TileIndex, OutPolyIndex);
		const dtTileRef TileRef = DetourNavMesh->encodePolyId(Salt, TileIndex, 0);
		OutTileRef = FNavTileRef(TileRef);
		return true;
	}

	return false;
}

// 求 TestPt 在指定 Poly 上的最近点。
// 转发到 dtNavMeshQuery::closestPointOnPoly。坐标先 UE→Recast，结果再 Recast→UE 翻回。
bool FPImplRecastNavMesh::GetClosestPointOnPoly(NavNodeRef PolyID, const FVector& TestPt, FVector& PointOnPoly) const
{
	if (DetourNavMesh && PolyID)
	{
		INITIALIZE_NAVQUERY_SIMPLE(NavQuery, RECAST_MAX_SEARCH_NODES);

		FVector::FReal RcTestPos[3] = { 0.0f };
		FVector::FReal RcClosestPos[3] = { 0.0f };
		Unr2RecastVector(TestPt, RcTestPos);

		const dtStatus Status = NavQuery.closestPointOnPoly(PolyID, RcTestPos, RcClosestPos);
		if (dtStatusSucceed(Status))
		{
			PointOnPoly = Recast2UnrealPoint(RcClosestPos);
			return true;
		}
	}

	return false;
}

// 由 NavLink 的 PolyRef 反查 OffMeshConnection 的用户自定义 ID（FNavLinkId）。
// 通过 dtNavMesh::getOffMeshConnectionByRef 拿到 dtOffMeshConnection 后读 userId 字段。
FNavLinkId FPImplRecastNavMesh::GetNavLinkUserId(NavNodeRef LinkPolyID) const
{
	const dtOffMeshConnection* offmeshCon = DetourNavMesh ? DetourNavMesh->getOffMeshConnectionByRef(LinkPolyID) : nullptr;

	return offmeshCon ? FNavLinkId(offmeshCon->userId) : FNavLinkId::Invalid;
}

// 取得指定 NavLink Poly 两端在世界中的端点（A→B）。
// 转发到 dtNavMesh::getOffMeshConnectionPolyEndPoints；结果坐标 Recast→UE 翻转。
bool FPImplRecastNavMesh::GetLinkEndPoints(NavNodeRef LinkPolyID, FVector& PointA, FVector& PointB) const
{
	if (DetourNavMesh)
	{
		FVector::FReal RcPointA[3] = { 0 };
		FVector::FReal RcPointB[3] = { 0 };
		
		dtStatus status = DetourNavMesh->getOffMeshConnectionPolyEndPoints(0, LinkPolyID, 0, RcPointA, RcPointB);
		if (dtStatusSucceed(status))
		{
			PointA = Recast2UnrealPoint(RcPointA);
			PointB = Recast2UnrealPoint(RcPointB);
			return true;
		}
	}

	return false;
}

// 判断 Poly 是否对应一条"自定义 NavLink"（即用户脚本/蓝图驱动的 OffMeshConnection）。
// 通过 dtNavMesh::getOffMeshConnectionByRef 取到 OffMeshConnection 并检查其 userId 是否合法。
bool FPImplRecastNavMesh::IsCustomLink(NavNodeRef PolyRef) const
{
	if (DetourNavMesh)
	{
		const dtOffMeshConnection* offMeshCon = DetourNavMesh->getOffMeshConnectionByRef(PolyRef);
		return offMeshCon && FNavLinkId(offMeshCon->userId) != FNavLinkId::Invalid;
	}

	return false;
}

#if WITH_NAVMESH_CLUSTER_LINKS
// 计算指定 Cluster 的世界空间 AABB（仅 WITH_NAVMESH_CLUSTER_LINKS）。
// 通过 dtNavMesh::getTileByRef + decodeClusterIdCluster 找到 Tile 与 Cluster Index，
// 遍历 Tile->polyClusters[] 找出归属于该 Cluster 的所有 ground Poly，把它们的顶点累加进 OutBounds。
bool FPImplRecastNavMesh::GetClusterBounds(NavNodeRef ClusterRef, FBox& OutBounds) const
{
	if (DetourNavMesh == NULL || !ClusterRef)
	{
		return false;
	}

	const dtMeshTile* Tile = DetourNavMesh->getTileByRef(ClusterRef);
	uint32 ClusterIdx = DetourNavMesh->decodeClusterIdCluster(ClusterRef);

	int32 NumPolys = 0;
	if (Tile && ClusterIdx < (uint32)Tile->header->clusterCount)
	{
		for (int32 i = 0; i < Tile->header->offMeshBase; i++)
		{
			if (Tile->polyClusters[i] == ClusterIdx)
			{
				const dtPoly* Poly = &Tile->polys[i];
				for (int32 iVert = 0; iVert < Poly->vertCount; iVert++)
				{
					const FVector::FReal* V = &Tile->verts[Poly->verts[iVert]*3];
					OutBounds += Recast2UnrealPoint(V);
				}

				NumPolys++;
			}
		}
	}

	return NumPolys > 0;
}
#endif // WITH_NAVMESH_CLUSTER_LINKS

// GetEdgesForPathCorridor 的内部实现：遍历走廊相邻 Poly 对，调用 dtNavMeshQuery::getPortalPoints
// 求每对相邻 Poly 之间的 Portal 边端点（Left/Right），翻回 UE 后塞入 PathCorridorEdges。
// 用于 String Pull / 路径平滑前的"门户"输入。
void FPImplRecastNavMesh::GetEdgesForPathCorridorImpl(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges, const dtNavMeshQuery& NavQuery) const
{
	const int32 CorridorLenght = PathCorridor->Num();

	PathCorridorEdges->Empty(CorridorLenght - 1);
	for (int32 i = 0; i < CorridorLenght - 1; ++i)
	{
		unsigned char FromType = 0, ToType = 0;
		FVector::FReal Left[3] = {0.f}, Right[3] = {0.f};

		NavQuery.getPortalPoints((*PathCorridor)[i], (*PathCorridor)[i+1], Left, Right, FromType, ToType);

		PathCorridorEdges->Add(FNavigationPortalEdge(Recast2UnrVector(Left), Recast2UnrVector(Right), (*PathCorridor)[i+1]));
	}
}

// GetEdgesForPathCorridor 公开入口：构建一个简单 NavQuery 后转交 GetEdgesForPathCorridorImpl 处理。
void FPImplRecastNavMesh::GetEdgesForPathCorridor(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges) const
{
	// sanity check
	if (DetourNavMesh == NULL)
	{
		return;
	}

	INITIALIZE_NAVQUERY_SIMPLE(NavQuery, RECAST_MAX_SEARCH_NODES);

	GetEdgesForPathCorridorImpl(PathCorridor, PathCorridorEdges, NavQuery);
}

// 就地过滤 Poly 列表：剔除不通过 FRecastQueryFilter::passFilter 或 Area Cost ≤ 0（不可走）的 Poly。
// 通过 dtNavMesh::getTileAndPolyByRef 取出 Tile/Poly 后逐个测试，从尾到头 RemoveAt 以保持索引稳定。
bool FPImplRecastNavMesh::FilterPolys(TArray<NavNodeRef>& PolyRefs, const FRecastQueryFilter* Filter, const UObject* Owner) const
{
	if (Filter == NULL || DetourNavMesh == NULL)
	{
		return false;
	}

	for (int32 PolyIndex = PolyRefs.Num() - 1; PolyIndex >= 0; --PolyIndex)
	{
		dtPolyRef TestRef = PolyRefs[PolyIndex];

		// get poly data from recast
		dtPoly const* Poly = NULL;
		dtMeshTile const* Tile = NULL;
		const dtStatus Status = DetourNavMesh->getTileAndPolyByRef(TestRef, &Tile, &Poly);

		if (dtStatusSucceed(Status))
		{
			const bool bPassedFilter = Filter->passFilter(TestRef, Tile, Poly);
			const bool bWalkableArea = Filter->getAreaCost(Poly->getArea()) > 0.0f;
			if (bPassedFilter && bWalkableArea)
			{
				continue;
			}
		}
		
		PolyRefs.RemoveAt(PolyIndex, 1);
	}

	return true;
}

// Deprecated
// 取得指定 Tile 内所有 ground 类型 Poly（不含 OffMeshConnection 等）的 Ref + 中心点。
// 数据来源：dtNavMesh::getTile(TileIndex) 拿到 dtMeshTile，遍历前 offMeshBase 个 Poly，
// 算其顶点平均得到中心点，并用 encodePolyId 重建 PolyRef。注：函数已 Deprecated。
// 注意：此处 TileIndex 是 Recast 内部线性索引（非 UE 的 X/Y 网格坐标）。
bool FPImplRecastNavMesh::GetPolysInTile(int32 TileIndex, TArray<FNavPoly>& Polys) const
{
	if (DetourNavMesh == NULL || TileIndex < 0 || TileIndex >= DetourNavMesh->getMaxTiles())
	{
		return false;
	}

	const dtMeshTile* Tile = ((const dtNavMesh*)DetourNavMesh)->getTile(TileIndex);
	const int32 MaxPolys = Tile && Tile->header ? Tile->header->offMeshBase : 0;
	if (MaxPolys > 0)
	{
		// only ground type polys
		int32 BaseIdx = Polys.Num();
		Polys.AddZeroed(MaxPolys);

		dtPoly* Poly = Tile->polys;
		for (int32 i = 0; i < MaxPolys; i++, Poly++)
		{
			FVector PolyCenter(0);
			for (int k = 0; k < Poly->vertCount; ++k)
			{
				PolyCenter += Recast2UnrealPoint(&Tile->verts[Poly->verts[k]*3]);
			}
			PolyCenter /= Poly->vertCount;

			FNavPoly& OutPoly = Polys[BaseIdx + i];
			OutPoly.Ref = DetourNavMesh->encodePolyId(Tile->salt, TileIndex, i);
			OutPoly.Center = PolyCenter;
		}
	}

	return (MaxPolys > 0);
}

/** Internal. Calculates squared 2d distance of given point PT to segment P-Q. Values given in Recast coordinates */
static FORCEINLINE FVector::FReal PointDistToSegment2DSquared(const FVector::FReal* PT, const FVector::FReal* P, const FVector::FReal* Q)
{
	FVector::FReal pqx = Q[0] - P[0];
	FVector::FReal pqz = Q[2] - P[2];
	FVector::FReal dx = PT[0] - P[0];
	FVector::FReal dz = PT[2] - P[2];
	FVector::FReal d = pqx*pqx + pqz*pqz;
	FVector::FReal t = pqx*dx + pqz*dz;
	if (d != 0) t /= d;
	dx = P[0] + t*pqx - PT[0];
	dz = P[2] + t*pqz - PT[2];
	return dx*dx + dz*dz;
}

// Deprecated
// 已废弃：转发到 GetTilePolyEdges。保留以兼容旧调用方。
void FPImplRecastNavMesh::GetDebugPolyEdges(const dtMeshTile& Tile, bool bInternalEdges, bool bNavMeshEdges, TArray<FVector>& InternalEdgeVerts, TArray<FVector>& NavMeshEdgeVerts) const
{
	GetTilePolyEdges(Tile, bInternalEdges, bNavMeshEdges, InternalEdgeVerts, NavMeshEdgeVerts);
}

// 在指定 Tile 内按 QueryFilter 找出"可走 Poly 与不可走 Poly 的分界边"（外墙边/Filter 边）。
// 算法：对每个 ground Poly 判断它对当前 Filter 是否"内部/外部"，若邻居穿越 DT_EXT_LINK 跨 Tile 则取邻居 Tile 的 Poly 验证；
// 仅在"自身内部 vs 邻居外部"的边界时输出（V0,V1）到 OutEdges，输出端点经 Recast→UE 翻转。
void FPImplRecastNavMesh::GetTilePolyEdgesForFilter(const dtMeshTile& Tile, const FNavigationQueryFilter& InQueryFilter, TArray<FNavigationWallEdge>& OutEdges) const
{
	const FRecastQueryFilter* FilterImplementation = (const FRecastQueryFilter*)(InQueryFilter.GetImplementation());
	if (!FilterImplementation)
	{
		return;
	}

	const dtQueryFilter* QueryFilter = FilterImplementation->GetAsDetourQueryFilter();
	for (int i = 0; i < Tile.header->polyCount; ++i)
	{
		const dtPoly* Poly = &Tile.polys[i];

		if (Poly->getType() != DT_POLYTYPE_GROUND)
		{
			continue;
		}

		const dtPolyRef Base = DetourNavMesh->getPolyRefBase(&Tile);
		auto IsPolyExterior = [QueryFilter](const dtPoly& Poly, const dtMeshTile& Tile, const dtPolyRef PolyIndex, const dtPolyRef Base) -> bool
			{
				if (!QueryFilter)
				{
					return Poly.getArea() == RECAST_NULL_AREA;
				}

				return !QueryFilter->passFilter(Base | (dtPolyRef)PolyIndex, &Tile, &Poly);
			};

		const bool bIsPolyiExterior = IsPolyExterior(*Poly, Tile, (dtPolyRef)i, Base);
		for (int j = 0, nj = (int)Poly->vertCount; j < nj; ++j)
		{
			if (bIsPolyiExterior)
			{
				const bool bIsPolyjLink = (Poly->neis[j] & DT_EXT_LINK) != 0;
				if (bIsPolyjLink)
				{
					// Recover poly j from the neighbor tile and test the filter
					unsigned int k = Poly->firstLink;
					while (k != DT_NULL_LINK)
					{
						const dtLink& Link = DetourNavMesh->getLink(&Tile, k);
						if (Link.edge == j)
						{
							const dtMeshTile* PolyjTile;
							const dtPoly* Polyj;
							const dtStatus Status = DetourNavMesh->getTileAndPolyByRef(Link.ref, &PolyjTile, &Polyj);
							if (dtStatusSucceed(Status) && Polyj && PolyjTile)
							{
								if (!IsPolyExterior(*Polyj, *PolyjTile, Link.ref, DetourNavMesh->getPolyRefBase(PolyjTile)))
								{
									// Poly i is interior and neighbor j is exterior
									const FVector::FReal* V0 = &Tile.verts[Poly->verts[Link.edge] * 3];
									const FVector::FReal* V1 = &Tile.verts[Poly->verts[(Link.edge + 1) % nj] * 3];
									OutEdges.Emplace(Recast2UnrVector(V0), Recast2UnrVector(V1));
								}
							}

							break;
						}
						k = Link.next;
					}
				}
				else
				{
					// Check if j is interior to mark that edge
					const bool bIsPolyjExterior = Poly->neis[j] == 0 || IsPolyExterior(Tile.polys[Poly->neis[j] - 1], Tile, Poly->neis[j] - 1, Base);
					if (!bIsPolyjExterior)
					{
						// Poly i is interior and neighbor j is exterior
						const FVector::FReal* V0 = &Tile.verts[Poly->verts[j] * 3];
						const FVector::FReal* V1 = &Tile.verts[Poly->verts[(j + 1) % nj] * 3];
						OutEdges.Emplace(Recast2UnrVector(V0), Recast2UnrVector(V1));
					}
				}
			}
			else
			{
				// Here we only need to check for no poly since the case where i is Interior and j is Exterior through filter is handled above
				// with the polygons interchanged and we don't want to duplicate the edges. The inverted case doesn't happen if j is not a poly since 
				// the for loop won't iterate on it
				const bool bIsPolyjOutsideNavmesh = Poly->neis[j] == 0;
				if (bIsPolyjOutsideNavmesh)
				{
					// Poly i is interior and neighbor j is exterior
					// Swap the vertices to keep the same edges orientations
					const FVector::FReal* V0 = &Tile.verts[Poly->verts[(j + 1) % nj] * 3];
					const FVector::FReal* V1 = &Tile.verts[Poly->verts[j] * 3];
					OutEdges.Emplace(Recast2UnrVector(V0), Recast2UnrVector(V1));
				}
			}
		}
	}
}

// 在 Tile 内收集"内部多边形边"或"NavMesh 外缘边"，输出端点坐标到两个数组（UE 坐标）。
// bGatherInteriorPolyEdges：收集多边形之间的连接边（detail mesh 真实细分边）。
// bGatherExteriorNavMeshEdges：仅收集对外悬空/无邻居的外缘边。
// 通过遍历 Tile->polys/detailMeshes/detailTris/detailVerts，并用阈值判断 detail 边是否对齐于 Poly 边来过滤。
void FPImplRecastNavMesh::GetTilePolyEdges(const dtMeshTile& Tile, bool bGatherInteriorPolyEdges, bool bGatherExteriorNavMeshEdges, TArray<FVector>& OutInteriorPolyEdgeVerts, TArray<FVector>& OutExteriorNavMeshEdgeVerts) const
{
	static const FVector::FReal thr = FMath::Square(0.01f);

	ensure(bGatherInteriorPolyEdges || bGatherExteriorNavMeshEdges);
	const bool bExportAllEdges = bGatherInteriorPolyEdges && !bGatherExteriorNavMeshEdges;
	
	for (int i = 0; i < Tile.header->polyCount; ++i)
	{
		const dtPoly* Poly = &Tile.polys[i];

		if (Poly->getType() != DT_POLYTYPE_GROUND)
		{
			continue;
		}

		const dtPolyDetail* pd = &Tile.detailMeshes[i];
		for (int j = 0, nj = (int)Poly->vertCount; j < nj; ++j)
		{
			bool bIsExternal = !bExportAllEdges && (Poly->neis[j] == 0 || Poly->neis[j] & DT_EXT_LINK);
			bool bIsConnected = !bIsExternal;

			if (Poly->getArea() == RECAST_NULL_AREA)
			{
				if (Poly->neis[j] && !(Poly->neis[j] & DT_EXT_LINK) &&
					Poly->neis[j] <= Tile.header->offMeshBase &&
					Tile.polys[Poly->neis[j] - 1].getArea() != RECAST_NULL_AREA)
				{
					bIsExternal = true;
					bIsConnected = false;
				}
				else if (Poly->neis[j] == 0)
				{
					bIsExternal = true;
					bIsConnected = false;
				}
			}
			else if (bIsExternal)
			{
				unsigned int k = Poly->firstLink;
				while (k != DT_NULL_LINK)
				{
					const dtLink& link = DetourNavMesh->getLink(&Tile, k);
					k = link.next;

					if (link.edge == j)
					{
						bIsConnected = true;
						break;
					}
				}
			}

			TArray<FVector>* EdgeVerts = bGatherInteriorPolyEdges && bIsConnected ? &OutInteriorPolyEdgeVerts 
				: (bGatherExteriorNavMeshEdges && bIsExternal && !bIsConnected ? &OutExteriorNavMeshEdgeVerts : NULL);
			if (EdgeVerts == NULL)
			{
				continue;
			}

			const FVector::FReal* V0 = &Tile.verts[Poly->verts[j] * 3];
			const FVector::FReal* V1 = &Tile.verts[Poly->verts[(j + 1) % nj] * 3];

			// Gather detail mesh edges which align with the actual poly edge.
			// This is really slow.
			for (int32 k = 0; k < pd->triCount; ++k)
			{
				const unsigned char* t = &(Tile.detailTris[(pd->triBase + k) * 4]);
				const FVector::FReal* tv[3];

				for (int32 m = 0; m < 3; ++m)
				{
					if (t[m] < Poly->vertCount)
					{
						tv[m] = &Tile.verts[Poly->verts[t[m]] * 3];
					}
					else
					{
						tv[m] = &Tile.detailVerts[(pd->vertBase + (t[m] - Poly->vertCount)) * 3];
					}
				}
				for (int m = 0, n = 2; m < 3; n=m++)
				{
					if (((t[3] >> (n*2)) & 0x3) == 0)
					{
						continue;	// Skip inner detail edges.
					}
					
					if (PointDistToSegment2DSquared(tv[n],V0,V1) < thr && PointDistToSegment2DSquared(tv[m],V0,V1) < thr)
					{
						int32 const AddIdx = (*EdgeVerts).AddZeroed(2);
						(*EdgeVerts)[AddIdx] = Recast2UnrVector(tv[n]);
						(*EdgeVerts)[AddIdx+1] = Recast2UnrVector(tv[m]);
					}
				}
			}
		}
	}
}

uint8 GetValidEnds(const dtNavMesh& NavMesh, const dtMeshTile& Tile, const dtPoly& Poly)
{
	if (Poly.getType() == DT_POLYTYPE_GROUND)
	{
		return false;
	}

	uint8 ValidEnds = FRecastDebugGeometry::OMLE_None;

	unsigned int k = Poly.firstLink;
	while (k != DT_NULL_LINK)
	{
		const dtLink& link = NavMesh.getLink(&Tile, k);
		k = link.next;

		if (link.edge == 0)
		{
			ValidEnds |= FRecastDebugGeometry::OMLE_Left;
		}
		if (link.edge == 1)
		{
			ValidEnds |= FRecastDebugGeometry::OMLE_Right;
		}
	}

	return ValidEnds;
}

// Deprecated
// 已废弃：基于 TileIndex（线性索引）取调试几何。先用 DeprecatedMakeTileRefsFromTileIds 把
// TileIndex 转换为 FNavTileRef，再转发到 FNavTileRef 版本的 GetDebugGeometryForTile。
bool FPImplRecastNavMesh::GetDebugGeometryForTile(FRecastDebugGeometry& OutGeometry, int32 TileIndex) const
{
	FNavTileRef TileRef;
	if (TileIndex != INDEX_NONE)
	{
		TArray<FNavTileRef> TileRefs;
		FNavTileRef::DeprecatedMakeTileRefsFromTileIds(this, { static_cast<uint32>(TileIndex) }, TileRefs);
		TileRef = TileRefs[0];
	}
	return GetDebugGeometryForTile(OutGeometry, TileRef);
}

// 把单个 Tile（或者全 NavMesh / 全部 ActiveTile）的几何数据填充进 OutGeometry，供编辑器/调试 HUD 绘制。
// 三个分支：
//   1) TileRef 有效——只处理这一块 Tile；
//   2) bIsGenerationRestrictedToActiveTiles——遍历 NavMeshOwner 的 ActiveTileSet，逐 (X,Y) 取 Tiles；
//   3) 否则遍历 0..MaxTiles 全量。
// OutGeometry 的填充由 GetTilesDebugGeometry 完成：MeshVerts(顶点)、AreaIndices/BuiltMeshIndices(三角索引)、
// 各种 NavLink/Cluster/Edge 数组等。返回 bDone=true 表示已完成全部 Tile 的处理。
bool FPImplRecastNavMesh::GetDebugGeometryForTile(FRecastDebugGeometry& OutGeometry, FNavTileRef TileRef) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FPImplRecastNavMesh_GetDebugGeometryForTile);
	
	bool bDone = false;
	if (DetourNavMesh == nullptr)
	{
		bDone = true;
		return bDone;
	}
				
	check(NavMeshOwner);

	const dtNavMesh* const ConstNavMesh = DetourNavMesh;

	// presize our tarrays for efficiency
	int32 NumVertsToReserve = 0;
	int32 NumIndicesToReserve = 0;

	const uint16 ForbiddenFlags = OutGeometry.bMarkForbiddenPolys 
		? GetFilterForbiddenFlags((const FRecastQueryFilter*)NavMeshOwner->GetDefaultQueryFilterImpl()) 
		: 0;

	const FRecastNavMeshGenerator* Generator = static_cast<const FRecastNavMeshGenerator*>(NavMeshOwner->GetGenerator());
	const bool bIsGenerationRestrictedToActiveTiles = Generator && Generator->IsBuildingRestrictedToActiveTiles() && !NavMeshOwner->GetActiveTileSet().IsEmpty();

	auto ComputeSizeToReserve = [](dtMeshTile const* const Tile, int32& OutNumVertsToReserve, int32& OutNumIndicesToReserve)
	{
		if (Tile != nullptr && Tile->header != nullptr)
		{
			OutNumVertsToReserve += Tile->header->vertCount + Tile->header->detailVertCount;

			for (int32 PolyIdx = 0; PolyIdx < Tile->header->polyCount; ++PolyIdx)
			{
				dtPolyDetail const* const DetailPoly = &Tile->detailMeshes[PolyIdx];
				OutNumIndicesToReserve += (DetailPoly->triCount * 3);
			}
		}
	};

	auto ReserveGeometryArrays = [](FRecastDebugGeometry& OutGeometry, const int32 NumVertsToReserve, const int32 NumIndicesToReserve)
	{
		OutGeometry.MeshVerts.Reserve(OutGeometry.MeshVerts.Num() + NumVertsToReserve);
		OutGeometry.AreaIndices[0].Reserve(OutGeometry.AreaIndices[0].Num() + NumIndicesToReserve);
		OutGeometry.BuiltMeshIndices.Reserve(OutGeometry.BuiltMeshIndices.Num() + NumIndicesToReserve);
		for (int32 Index = 0; Index < FRecastDebugGeometry::BuildTimeBucketsCount; Index++)
		{
			OutGeometry.TileBuildTimesIndices[Index].Reserve(OutGeometry.TileBuildTimesIndices[Index].Num() + NumIndicesToReserve);	
		}
	};

	if (TileRef.IsValid())
	{
		dtMeshTile const* const Tile = ConstNavMesh->getTileByRef(static_cast<dtTileRef>(TileRef));
		if (Tile != nullptr && Tile->header != nullptr)
		{
			const FIntPoint TileCoord = FIntPoint(Tile->header->x, Tile->header->y);
			if (!bIsGenerationRestrictedToActiveTiles || Generator->IsInActiveSet(TileCoord))
			{
				ComputeSizeToReserve(Tile, NumVertsToReserve, NumIndicesToReserve);

				ReserveGeometryArrays(OutGeometry, NumVertsToReserve, NumIndicesToReserve);

				const uint32 VertBase = OutGeometry.MeshVerts.Num();
				GetTilesDebugGeometry(Generator, *Tile, VertBase, OutGeometry, ConstNavMesh->getTileIndex(Tile), ForbiddenFlags);
			}
		}
	}
	else if (bIsGenerationRestrictedToActiveTiles)
	{
		TArray<const dtMeshTile*> Tiles;
		const TSet<FIntPoint>& ActiveTiles = NavMeshOwner->GetActiveTileSet();
		for (const FIntPoint& TileLocation : ActiveTiles)
		{
			Tiles.Reset();
			Tiles.AddZeroed(ConstNavMesh->getTileCountAt(TileLocation.X, TileLocation.Y));
			ConstNavMesh->getTilesAt(TileLocation.X, TileLocation.Y, Tiles.GetData(), Tiles.Num());
			for (const dtMeshTile* Tile : Tiles)
			{
				ComputeSizeToReserve(Tile, NumVertsToReserve, NumIndicesToReserve);
			}
		}

		ReserveGeometryArrays(OutGeometry, NumVertsToReserve, NumIndicesToReserve);

		uint32 VertBase = OutGeometry.MeshVerts.Num();
		for (const FIntPoint& TileLocation : ActiveTiles)
		{
			Tiles.Reset();
			Tiles.AddZeroed(ConstNavMesh->getTileCountAt(TileLocation.X, TileLocation.Y));
			ConstNavMesh->getTilesAt(TileLocation.X, TileLocation.Y, Tiles.GetData(), Tiles.Num());
			for (const dtMeshTile* Tile : Tiles)
			{
				if (Tile != nullptr && Tile->header != nullptr)
				{
					VertBase += GetTilesDebugGeometry(Generator, *Tile, VertBase, OutGeometry, INDEX_NONE, ForbiddenFlags);
				}
			}
		}

		bDone = true;
	}
	else
	{
		const int32 NumTiles = ConstNavMesh->getMaxTiles();
		for (int32 TileIdx = 0; TileIdx < NumTiles; ++TileIdx)
		{
			dtMeshTile const* const Tile = ConstNavMesh->getTile(TileIdx);
			ComputeSizeToReserve(Tile, NumVertsToReserve, NumIndicesToReserve);
		}

		ReserveGeometryArrays(OutGeometry, NumVertsToReserve, NumIndicesToReserve);

		uint32 VertBase = OutGeometry.MeshVerts.Num();
		for (int32 TileIdx = 0; TileIdx < NumTiles; ++TileIdx)
		{
			dtMeshTile const* const Tile = ConstNavMesh->getTile(TileIdx);
			if (Tile != nullptr && Tile->header != nullptr)
			{
				VertBase += GetTilesDebugGeometry(Generator, *Tile, VertBase, OutGeometry, TileIdx, ForbiddenFlags);
			}
		}

		bDone = true;
	}

	return bDone;
}

// 把单个 Tile 的几何数据填充到 OutGeometry，由 GetDebugGeometryForTile 统一调度调用。
// 填充内容：
//   - MeshVerts：先添加 polyVerts，再添加 detailVerts；返回新增顶点总数（用作下一 Tile 的 VertBase 偏移）。
//   - 索引按 Poly 是否在建/被禁通(ForbiddenFlags)/构建耗时桶 分流到 BuiltMeshIndices/ForbiddenIndices/AreaIndices/TileBuildTimesIndices。
//   - OffMeshLinks/OffMeshSegments/ClusterLinks（受相关编译宏控制）。
//   - 可选：PolyEdges 与 NavMeshEdges（由 GetTilePolyEdges 收集）。
int32 FPImplRecastNavMesh::GetTilesDebugGeometry(const FRecastNavMeshGenerator* Generator, const dtMeshTile& Tile, int32 VertBase, FRecastDebugGeometry& OutGeometry, int32 TileIdx, uint16 ForbiddenFlags) const
{
	check(NavMeshOwner && DetourNavMesh);
	dtMeshHeader const* const Header = Tile.header;
	check(Header);

#if RECAST_INTERNAL_DEBUG_DATA
	OutGeometry.TilesToDisplayInternalData.Push(FIntPoint(Header->x, Header->y));
#endif

	const bool bIsBeingBuilt = Generator != nullptr && !!NavMeshOwner->bDistinctlyDrawTilesBeingBuilt
		&& Generator->IsTileChanged(FNavTileRef(DetourNavMesh->getTileRef(&Tile)));

	UE_SUPPRESS(LogNavigation, VeryVerbose,
	{
		if (bIsBeingBuilt)
		{
			const dtTileRef TileRef = DetourNavMesh->getTileRef(&Tile);
			UE_LOG(LogNavigation, VeryVerbose, TEXT("%hs TileId: %d Salt: %d TileRef: 0x%llx bIsBeingBuilt"),
				__FUNCTION__, DetourNavMesh->decodePolyIdTile(TileRef), DetourNavMesh->decodePolyIdSalt(TileRef), TileRef);	
		}
	});

	// add all the poly verts
	FVector::FReal* F = Tile.verts;
	for (int32 VertIdx = 0; VertIdx < Header->vertCount; ++VertIdx)
	{
		FVector const VertPos = Recast2UnrVector(F);
		OutGeometry.MeshVerts.Add(VertPos);
		F += 3;
	}

	int32 const DetailVertIndexBase = Header->vertCount;
	// add the detail verts
	F = Tile.detailVerts;
	for (int32 DetailVertIdx = 0; DetailVertIdx < Header->detailVertCount; ++DetailVertIdx)
	{
		FVector const VertPos = Recast2UnrVector(F);
		OutGeometry.MeshVerts.Add(VertPos);
		F += 3;
	}

#if RECAST_INTERNAL_DEBUG_DATA	
	const FIntPoint TileCoord(Header->x, Header->y);
	const FRecastInternalDebugData* DebugData = DebugDataMap.Find(TileCoord);
#endif // RECAST_INTERNAL_DEBUG_DATA	
	
	// add all the indices
	for (int32 PolyIdx = 0; PolyIdx < Header->polyCount; ++PolyIdx)
	{
		dtPoly const* const Poly = &Tile.polys[PolyIdx];

		if (Poly->getType() == DT_POLYTYPE_GROUND)
		{
			dtPolyDetail const* const DetailPoly = &Tile.detailMeshes[PolyIdx];

			TArray<int32>* Indices = nullptr;
			if (bIsBeingBuilt)
			{
				Indices = &OutGeometry.BuiltMeshIndices;
			}
			else if ((Poly->flags & ForbiddenFlags) != 0)
			{
				Indices = &OutGeometry.ForbiddenIndices;
			}
#if RECAST_INTERNAL_DEBUG_DATA			
			else if (OutGeometry.bGatherTileBuildTimesHeatMap && DebugData)
			{
				const double Range = OutGeometry.MaxTileBuildTime - OutGeometry.MinTileBuildTime;
				int32 Rank = 0;
				if (Range != 0.)
				{
					Rank = static_cast<int32>(FRecastDebugGeometry::BuildTimeBucketsCount * ((DebugData->BuildTime - OutGeometry.MinTileBuildTime) / Range));
					Rank = FMath::Clamp(Rank, 0, FRecastDebugGeometry::BuildTimeBucketsCount-1);
				}
				Indices = &OutGeometry.TileBuildTimesIndices[Rank];
			}
#endif // RECAST_INTERNAL_DEBUG_DATA
			else
			{
				Indices = &OutGeometry.AreaIndices[Poly->getArea()];
			}

			// one triangle at a time
			for (int32 TriIdx = 0; TriIdx < DetailPoly->triCount; ++TriIdx)
			{
				int32 DetailTriIdx = (DetailPoly->triBase + TriIdx) * 4;
				const unsigned char* DetailTri = &Tile.detailTris[DetailTriIdx];

				// calc indices into the vert buffer we just populated
				int32 TriVertIndices[3];
				for (int32 TriVertIdx = 0; TriVertIdx < 3; ++TriVertIdx)
				{
					if (DetailTri[TriVertIdx] < Poly->vertCount)
					{
						TriVertIndices[TriVertIdx] = VertBase + Poly->verts[DetailTri[TriVertIdx]];
					}
					else
					{
						TriVertIndices[TriVertIdx] = VertBase + DetailVertIndexBase + (DetailPoly->vertBase + DetailTri[TriVertIdx] - Poly->vertCount);
					}
				}

				Indices->Add(TriVertIndices[0]);
				Indices->Add(TriVertIndices[1]);
				Indices->Add(TriVertIndices[2]);

#if WITH_NAVMESH_CLUSTER_LINKS
				if (Tile.polyClusters)
				{
					const uint16 ClusterId = Tile.polyClusters[PolyIdx];
					if (ClusterId < MAX_uint8)
					{
						if (ClusterId >= OutGeometry.Clusters.Num())
						{
							OutGeometry.Clusters.AddDefaulted(ClusterId - OutGeometry.Clusters.Num() + 1);
						}

						TArray<int32>& ClusterIndices = OutGeometry.Clusters[ClusterId].MeshIndices;
						ClusterIndices.Add(TriVertIndices[0]);
						ClusterIndices.Add(TriVertIndices[1]);
						ClusterIndices.Add(TriVertIndices[2]);
					}
				}
#endif // WITH_NAVMESH_CLUSTER_LINKS
			}
		}
	}

	for (int32 i = 0; i < Header->offMeshConCount; ++i)
	{
		const dtOffMeshConnection* OffMeshConnection = &Tile.offMeshCons[i];

		if (OffMeshConnection != NULL)
		{
			dtPoly const* const LinkPoly = &Tile.polys[OffMeshConnection->poly];
			const FVector::FReal* va = &Tile.verts[LinkPoly->verts[0] * 3]; //OffMeshConnection->pos;
			const FVector::FReal* vb = &Tile.verts[LinkPoly->verts[1] * 3]; //OffMeshConnection->pos[3];

			const FRecastDebugGeometry::FOffMeshLink Link = {
				Recast2UnrVector(va)
				, Recast2UnrVector(vb)
				, LinkPoly->getArea()
				, (uint8)OffMeshConnection->getBiDirectional()
				, GetValidEnds(*DetourNavMesh, Tile, *LinkPoly)
				, OffMeshConnection->getIsGenerated()
				, UE_REAL_TO_FLOAT_CLAMPED_MAX(OffMeshConnection->rad)
			};

			(LinkPoly->flags & ForbiddenFlags) != 0
				? OutGeometry.ForbiddenLinks.Add(Link)
				: OutGeometry.OffMeshLinks.Add(Link);
		}
	}

#if WITH_NAVMESH_SEGMENT_LINKS
	for (int32 i = 0; i < Header->offMeshSegConCount; ++i)
	{
		const dtOffMeshSegmentConnection* OffMeshSeg = &Tile.offMeshSeg[i];
		if (OffMeshSeg != NULL)
		{
			const int32 polyBase = Header->offMeshSegPolyBase + OffMeshSeg->firstPoly;
			for (int32 j = 0; j < OffMeshSeg->npolys; j++)
			{
				dtPoly const* const LinkPoly = &Tile.polys[polyBase + j];

				FRecastDebugGeometry::FOffMeshSegment Link;
				Link.LeftStart = Recast2UnrealPoint(&Tile.verts[LinkPoly->verts[0] * 3]);
				Link.LeftEnd = Recast2UnrealPoint(&Tile.verts[LinkPoly->verts[1] * 3]);
				Link.RightStart = Recast2UnrealPoint(&Tile.verts[LinkPoly->verts[2] * 3]);
				Link.RightEnd = Recast2UnrealPoint(&Tile.verts[LinkPoly->verts[3] * 3]);
				Link.AreaID = LinkPoly->getArea();
				Link.Direction = (uint8)OffMeshSeg->getBiDirectional();
				Link.ValidEnds = GetValidEnds(*DetourNavMesh, Tile, *LinkPoly);

				const int LinkIdx = OutGeometry.OffMeshSegments.Add(Link);
				ensureMsgf((LinkPoly->flags & ForbiddenFlags) == 0, TEXT("Not implemented"));
				OutGeometry.OffMeshSegmentAreas[Link.AreaID].Add(LinkIdx);
			}
		}
	}
#endif // WITH_NAVMESH_SEGMENT_LINKS

#if WITH_NAVMESH_CLUSTER_LINKS
	for (int32 i = 0; i < Header->clusterCount; i++)
	{
		const dtCluster& c0 = Tile.clusters[i];
		uint32 iLink = c0.firstLink;
		while (iLink != DT_NULL_LINK)
		{
			const dtClusterLink& link = DetourNavMesh->getClusterLink(&Tile, iLink);
			iLink = link.next;

			dtMeshTile const* const OtherTile = DetourNavMesh->getTileByRef(link.ref);
			if (OtherTile)
			{
				int32 linkedIdx = DetourNavMesh->decodeClusterIdCluster(link.ref);
				const dtCluster& c1 = OtherTile->clusters[linkedIdx];

				FRecastDebugGeometry::FClusterLink LinkGeom;
				LinkGeom.FromCluster = Recast2UnrealPoint(c0.center);
				LinkGeom.ToCluster = Recast2UnrealPoint(c1.center);

				if (linkedIdx > i || TileIdx > (int32)DetourNavMesh->decodeClusterIdTile(link.ref))
				{
					FVector UpDir(0, 0, 1.0f);
					FVector LinkDir = (LinkGeom.ToCluster - LinkGeom.FromCluster).GetSafeNormal();
					FVector SideDir = FVector::CrossProduct(LinkDir, UpDir);
					LinkGeom.FromCluster += SideDir * 40.0f;
					LinkGeom.ToCluster += SideDir * 40.0f;
				}

				OutGeometry.ClusterLinks.Add(LinkGeom);
			}
		}
	}
#endif // WITH_NAVMESH_CLUSTER_LINKS

	// Get tile edges and navmesh edges
	if (OutGeometry.bGatherPolyEdges || OutGeometry.bGatherNavMeshEdges)
	{
		GetTilePolyEdges(Tile, !!OutGeometry.bGatherPolyEdges, !!OutGeometry.bGatherNavMeshEdges
			, OutGeometry.PolyEdges, OutGeometry.NavMeshEdges);
	}

	return Header->vertCount + Header->detailVertCount;
}

// 计算整个 NavMesh 的世界 AABB（遍历所有 Tile 的 header bmin/bmax 取并）。
// Tile bounds 在 dtMeshHeader 中以 Recast 坐标存储，此处 Recast2UnrealBox 翻回 UE 坐标。
FBox FPImplRecastNavMesh::GetNavMeshBounds() const
{
	FBox Bbox(ForceInit);

	// @todo, calc once and cache it
	if (DetourNavMesh)
	{
		// workaround for privacy issue in the recast API
		dtNavMesh const* const ConstRecastNavMesh = DetourNavMesh;

		// spin through all the tiles and accumulate the bounds
		for (int32 TileIdx=0; TileIdx < DetourNavMesh->getMaxTiles(); ++TileIdx)
		{
			dtMeshTile const* const Tile = ConstRecastNavMesh->getTile(TileIdx);
			if (Tile)
			{
				dtMeshHeader const* const Header = Tile->header;
				if (Header)
				{
					const FBox NodeBox = Recast2UnrealBox(Header->bmin, Header->bmax);
					Bbox += NodeBox;
				}
			}
		}
	}

	return Bbox;
}

// Deprecated
// 已废弃：按 TileIndex（Recast 内部线性索引）取 Tile 的世界 AABB。
// 通过 dtNavMesh::getTile(TileIndex) 取头部 bmin/bmax 并 Recast→UE 翻转。
FBox FPImplRecastNavMesh::GetNavMeshTileBounds(int32 TileIndex) const
{
	FBox Bbox(ForceInit);

	if (DetourNavMesh && TileIndex >= 0 && TileIndex < DetourNavMesh->getMaxTiles())
	{
		// workaround for privacy issue in the recast API
		dtNavMesh const* const ConstRecastNavMesh = DetourNavMesh;

		dtMeshTile const* const Tile = ConstRecastNavMesh->getTile(TileIndex);
		if (Tile)
		{
			dtMeshHeader const* const Header = Tile->header;
			if (Header)
			{
				Bbox = Recast2UnrealBox(Header->bmin, Header->bmax);
			}
		}
	}

	return Bbox;
}

// Deprecated
/** Retrieves XY coordinates of tile specified by index */
// 已废弃：按 TileIndex（Recast 内部线性索引）取 Tile 网格坐标 X/Y 与 Layer。
// 数据来源：dtMeshTile->header 的 x/y/layer 字段（Tile 在 NavMesh 网格中的整数格坐标）。
bool FPImplRecastNavMesh::GetNavMeshTileXY(int32 TileIndex, int32& OutX, int32& OutY, int32& OutLayer) const
{
	if (DetourNavMesh && TileIndex >= 0 && TileIndex < DetourNavMesh->getMaxTiles())
	{
		// workaround for privacy issue in the recast API
		dtNavMesh const* const ConstRecastNavMesh = DetourNavMesh;

		dtMeshTile const* const Tile = ConstRecastNavMesh->getTile(TileIndex);
		if (Tile)
		{
			dtMeshHeader const* const Header = Tile->header;
			if (Header)
			{
				OutX = Header->x;
				OutY = Header->y;
				OutLayer = Header->layer;
				return true;
			}
		}
	}

	return false;
}

// 由"UE 世界点"计算其落在的 Tile 网格坐标 (X,Y)。
// 输入是 UE 坐标，函数内部通过 Unreal2RecastPoint 翻转为 Recast 坐标后调用 dtNavMesh::calcTileLoc。
// 注：返回的 (X,Y) 是 NavMesh 网格的整数 Tile 坐标，不是 UE 世界坐标。
bool FPImplRecastNavMesh::GetNavMeshTileXY(const FVector& Point, int32& OutX, int32& OutY) const
{
	if (DetourNavMesh)
	{
		// workaround for privacy issue in the recast API
		dtNavMesh const* const ConstRecastNavMesh = DetourNavMesh;

		const FVector RecastPt = Unreal2RecastPoint(Point);
		int32 TileX = 0;
		int32 TileY = 0;

		ConstRecastNavMesh->calcTileLoc(&RecastPt.X, &TileX, &TileY);
		OutX = TileX;
		OutY = TileY;
		return true;
	}

	return false;
}

// Deprecated
// 已废弃：取 (TileX, TileY) 网格坐标处所有 Layer 对应的 Tile 线性索引。
// 输入 TileX/TileY 是 Recast NavMesh 网格坐标。通过 dtNavMesh::getTilesAt 取出该格上的所有 Tile（多 Layer），
// 再用 decodePolyIdTile 把 dtTileRef 解析为 TileIndex 加入 Indices。
void FPImplRecastNavMesh::GetNavMeshTilesAt(int32 TileX, int32 TileY, TArray<int32>& Indices) const
{
	if (DetourNavMesh)
	{
		// workaround for privacy issue in the recast API
		dtNavMesh const* const ConstRecastNavMesh = DetourNavMesh;

		const int32 MaxTiles = ConstRecastNavMesh->getTileCountAt(TileX, TileY);
		TArray<const dtMeshTile*> Tiles;
		Tiles.AddZeroed(MaxTiles);

		const int32 NumTiles = ConstRecastNavMesh->getTilesAt(TileX, TileY, Tiles.GetData(), MaxTiles);
		for (int32 i = 0; i < NumTiles; i++)
		{
			dtTileRef TileRef = ConstRecastNavMesh->getTileRef(Tiles[i]);
			if (TileRef)
			{
				const int32 TileIndex = (int32)ConstRecastNavMesh->decodePolyIdTile(TileRef);
				Indices.Add(TileIndex);
			}
		}
	}
}

// Deprecated
// 已废弃：按一组 UE 世界 AABB 取出所有相交 Tile 的"线性索引"。
// 内部转发到 FNavTileRef 版本，再用 DeprecatedGetTileIdsFromNavTileRefs 把 TileRef 转回旧式 TileIndex。
void FPImplRecastNavMesh::GetNavMeshTilesIn(const TArray<FBox>& InclusionBounds, TArray<int32>& Indices) const
{
	TArray<FNavTileRef> Refs;
	GetNavMeshTilesIn(InclusionBounds, Refs);

	TArray<uint32> UnsignedIndices;
	FNavTileRef::DeprecatedGetTileIdsFromNavTileRefs(this, Refs, UnsignedIndices);
	Indices.Append(UnsignedIndices);
}

// 按一组 UE 世界 AABB 取出所有相交的 Tile 的 FNavTileRef。
// 算法：
//   1) 把每个 Bounds 用 NavMesh 原点 + tileWidth 转成 Recast 网格 (XMin,YMin)~(XMax,YMax)；
//   2) 收集到去重的 TileCoord 集合；
//   3) 对每个 (X,Y) 调用 dtNavMesh::getTilesAt 获取多 Layer Tile，再用 Recast→UE 翻转后的 TileBounds 与请求 Bounds 求交以剔除误报。
// 输入 Bounds 是 UE 坐标；输出是 dtTileRef（包装为 FNavTileRef）。
void FPImplRecastNavMesh::GetNavMeshTilesIn(const TArray<FBox>& InclusionBounds, TArray<FNavTileRef>& OutRefs) const
{
	if (DetourNavMesh)
	{
		const FVector::FReal* NavMeshOrigin = DetourNavMesh->getParams()->orig;
		const FVector::FReal TileSize = DetourNavMesh->getParams()->tileWidth;

		// Generate a set of all possible tile coordinates that belong to requested bounds
		TSet<FIntPoint>	TileCoords;	
		for (const FBox& Bounds : InclusionBounds)
		{
			if (ensureMsgf(Bounds.IsValid, TEXT("%hs Bounds is not valid"), __FUNCTION__))
			{
				const FVector RcNavMeshOrigin(NavMeshOrigin[0], NavMeshOrigin[1], NavMeshOrigin[2]);
				const FRcTileBox TileBox(Bounds, RcNavMeshOrigin, TileSize);

				for (int32 y = TileBox.YMin; y <= TileBox.YMax; ++y)
				{
					for (int32 x = TileBox.XMin; x <= TileBox.XMax; ++x)
					{
						TileCoords.Add(FIntPoint(x, y));
					}
				}
			}
		}

		// We guess that each tile has 3 layers in average
		OutRefs.Reserve(TileCoords.Num()*3);

		TArray<const dtMeshTile*> MeshTiles;
		MeshTiles.Reserve(3);

		for (const FIntPoint& TileCoord : TileCoords)
		{
			int32 MaxTiles = DetourNavMesh->getTileCountAt(TileCoord.X, TileCoord.Y);
			if (MaxTiles > 0)
			{
				MeshTiles.SetNumZeroed(MaxTiles, EAllowShrinking::No);
				
				const int32 MeshTilesCount = DetourNavMesh->getTilesAt(TileCoord.X, TileCoord.Y, MeshTiles.GetData(), MaxTiles);
				for (int32 i = 0; i < MeshTilesCount; ++i)
				{
					const dtMeshTile* MeshTile = MeshTiles[i];
					dtTileRef TileRef = DetourNavMesh->getTileRef(MeshTile);
					if (TileRef)
					{
						// Consider only mesh tiles that actually belong to a requested bounds
						FBox TileBounds = Recast2UnrealBox(MeshTile->header->bmin, MeshTile->header->bmax);
						for (const FBox& RequestedBounds : InclusionBounds)
						{
							if (TileBounds.Intersect(RequestedBounds))
							{
								OutRefs.Add(FNavTileRef(TileRef));
								break;
							}
						}
					}
				}
			}
		}
	}
}

// 统计 NavMesh 总占用内存（KB），含本对象自身大小 + 所有 Tile 的 dataSize 之和。
// 注意：返回值是 KB（除以 1024）。
SIZE_T FPImplRecastNavMesh::GetTotalDataSize() const
{
	SIZE_T TotalBytes = sizeof(*this);

	if (DetourNavMesh)
	{
		// iterate all tiles and sum up their DataSize
		dtNavMesh const* ConstNavMesh = DetourNavMesh;
		for (int i = 0; i < ConstNavMesh->getMaxTiles(); ++i)
		{
			const dtMeshTile* Tile = ConstNavMesh->getTile(i);
			if (Tile != NULL && Tile->header != NULL)
			{
				TotalBytes += Tile->dataSize;
			}
		}
	}

	return TotalBytes / 1024;
}

#if !UE_BUILD_SHIPPING
// 仅 Non-Shipping：累计所有压缩 Tile 缓存层（CompressedTileCacheLayers Map）的字节数总和。
// 用于内存监控/Stat 调试。
int32 FPImplRecastNavMesh::GetCompressedTileCacheSize()
{
	int32 CompressedTileCacheSize = 0;

	for (TPair<FIntPoint, TArray<FNavMeshTileData>>& TilePairIter : CompressedTileCacheLayers)
	{
		TArray<FNavMeshTileData>& NavMeshTileDataArray = TilePairIter.Value;

		for (FNavMeshTileData& NavMeshTileDataIter : NavMeshTileDataArray)
		{
			CompressedTileCacheSize += NavMeshTileDataIter.DataSize;
		}
	}

	return CompressedTileCacheSize;
}
#endif

// 把整个 NavMesh 平移 InOffset（用于 World Origin Shifting / 世界原点重定位）。
// 偏移量先 UE→Recast 翻转，然后转发到 dtNavMesh::applyWorldOffset，会就地修改所有 Tile 的顶点与 BV/AABB。
void FPImplRecastNavMesh::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	if (DetourNavMesh != NULL)
	{
		// transform offset to Recast space
		const FVector OffsetRC = Unreal2RecastPoint(InOffset);
		// apply offset
		DetourNavMesh->applyWorldOffset(&OffsetRC.X);
	}
}

// 取得 Filter 的"禁止位"（PolyFlags 中被排除的位）。
// 直接读 dtQueryFilter::getExcludeFlags()。
uint16 FPImplRecastNavMesh::GetFilterForbiddenFlags(const FRecastQueryFilter* Filter)
{
	return ((const dtQueryFilter*)Filter)->getExcludeFlags();
}

// 设置 Filter 的"禁止位"。直接转发到 dtQueryFilter::setExcludeFlags。
// 注：include/exclude 不需要对称，过滤器会同时检查这两个条件。
void FPImplRecastNavMesh::SetFilterForbiddenFlags(FRecastQueryFilter* Filter, uint16 ForbiddenFlags)
{
	((dtQueryFilter*)Filter)->setExcludeFlags(ForbiddenFlags);
	// include-exclude don't need to be symmetrical, filter will check both conditions
}

// 当 Area Cost 配置发生变化时被调用：根据"TravelCost + EntryCost"对全部 Area 排序，
// 生成 AreaCostOrder 排名表（越靠前代价越小），转发到 dtNavMesh::applyAreaCostOrder
// 让 Detour 在 A* 中按新顺序优先扩展低代价 Area。
void FPImplRecastNavMesh::OnAreaCostChanged()
{
	struct FRealntPair
	{
		FVector::FReal Score;
		int32 Index;

		FRealntPair() : Score(MAX_FLT), Index(0) {}
		FRealntPair(int32 AreaId, FVector::FReal TravelCost, FVector::FReal EntryCost) : Score(TravelCost + EntryCost), Index(AreaId) {}

		bool operator <(const FRealntPair& Other) const { return Score < Other.Score; }
	};

	if (NavMeshOwner && DetourNavMesh)
	{
		const INavigationQueryFilterInterface* NavFilter = NavMeshOwner->GetDefaultQueryFilterImpl();
		const dtQueryFilter* DetourFilter = ((const FRecastQueryFilter*)NavFilter)->GetAsDetourQueryFilter();

		TArray<FRealntPair> AreaData;
		AreaData.Reserve(RECAST_MAX_AREAS);
		for (int32 Idx = 0; Idx < RECAST_MAX_AREAS; Idx++)
		{
			AreaData.Add(FRealntPair(Idx, DetourFilter->getAreaCost(Idx), DetourFilter->getAreaFixedCost(Idx)));
		}

		AreaData.Sort();

		uint8 AreaCostOrder[RECAST_MAX_AREAS];
		for (int32 Idx = 0; Idx < RECAST_MAX_AREAS; Idx++)
		{
			AreaCostOrder[AreaData[Idx].Index] = static_cast<uint8>(Idx);
		}

		DetourNavMesh->applyAreaCostOrder(AreaCostOrder);
	}
}

// 移除 (TileX, TileY) 上所有 Layer 的压缩 Tile 缓存（运行时动态导航时由 dtTileCache 流程驱动）。
// 仅从 CompressedTileCacheLayers 的 Map 中删除该坐标键。
void FPImplRecastNavMesh::RemoveTileCacheLayers(int32 TileX, int32 TileY)
{
	CompressedTileCacheLayers.Remove(FIntPoint(TileX, TileY));
}

// 移除 (TileX, TileY) 处指定 LayerIdx 的压缩 Tile 缓存，并把后续 Layer 索引重新连续化（LayerIndex 自减）。
// 若该 (X,Y) 上已无任何 Layer，则连同条目一起删除（并清理调试数据）。配合 dtTileCache 动态更新。
void FPImplRecastNavMesh::RemoveTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx)
{
	TArray<FNavMeshTileData>* ExistingLayersList = CompressedTileCacheLayers.Find(FIntPoint(TileX, TileY));
	if (ExistingLayersList)
	{
		if (ExistingLayersList->IsValidIndex(LayerIdx))
		{
			ExistingLayersList->RemoveAt(LayerIdx);

			for (int32 Idx = LayerIdx; Idx < ExistingLayersList->Num(); Idx++)
			{
				(*ExistingLayersList)[Idx].LayerIndex = Idx;
			}
		}
		
		if (ExistingLayersList->Num() == 0)
		{
			RemoveTileCacheLayers(TileX, TileY);

#if RECAST_INTERNAL_DEBUG_DATA
			NavMeshOwner->RemoveTileDebugData(TileX, TileY);
#endif
		}
	}
}

// 一次性写入 (TileX, TileY) 处的所有 Layer 压缩缓存（覆盖旧值）。
// 用于 NavMesh 生成器把整个 Tile 的多 Layer 数据塞入 dtTileCache 流程的入口。
void FPImplRecastNavMesh::AddTileCacheLayers(int32 TileX, int32 TileY, const TArray<FNavMeshTileData>& Layers)
{
	CompressedTileCacheLayers.Add(FIntPoint(TileX, TileY), Layers);
}

// 在 (TileX, TileY) 处添加/更新单个 LayerIdx 的压缩缓存数据（不存在则创建条目，存在则按需扩容数组并写入指定槽位）。
// 与 dtTileCache 配合：运行时增量构建 NavMesh 时把单层缓存放入此 Map，后续可重新解码为 dtMeshTile。
void FPImplRecastNavMesh::AddTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx, const FNavMeshTileData& LayerData)
{
	TArray<FNavMeshTileData>* ExistingLayersList = CompressedTileCacheLayers.Find(FIntPoint(TileX, TileY));
	
	if (ExistingLayersList)
	{
		ExistingLayersList->SetNum(FMath::Max(ExistingLayersList->Num(), LayerIdx + 1));
		(*ExistingLayersList)[LayerIdx] = LayerData;
	}
	else
	{
		TArray<FNavMeshTileData> LayersList;
		LayersList.SetNum(FMath::Max(LayersList.Num(), LayerIdx + 1));
		LayersList[LayerIdx] = LayerData;
		CompressedTileCacheLayers.Add(FIntPoint(TileX, TileY), LayersList);
	}
}

// 标记 (TileX, TileY) 为"空 Tile"：在 CompressedTileCacheLayers 中放入空数组占位。
// 用于流式生成时表示"该 Tile 已处理但无可用层"，避免被误判为未生成。
void FPImplRecastNavMesh::MarkEmptyTileCacheLayers(int32 TileX, int32 TileY)
{
	if (!CompressedTileCacheLayers.Contains(FIntPoint(TileX, TileY)))
	{
		TArray<FNavMeshTileData> EmptyLayersList;
		CompressedTileCacheLayers.Add(FIntPoint(TileX, TileY), EmptyLayersList);
	}
}

// 取得 (TileX, TileY) 上指定 LayerIdx 的压缩 Tile 缓存数据；不存在则返回空 FNavMeshTileData。
FNavMeshTileData FPImplRecastNavMesh::GetTileCacheLayer(int32 TileX, int32 TileY, int32 LayerIdx) const
{
	const TArray<FNavMeshTileData>* LayersList = CompressedTileCacheLayers.Find(FIntPoint(TileX, TileY));
	if (LayersList && LayersList->IsValidIndex(LayerIdx))
	{
		return (*LayersList)[LayerIdx];
	}

	return FNavMeshTileData();
}

TArray<FNavMeshTileData> FPImplRecastNavMesh::GetTileCacheLayers(int32 TileX, int32 TileY) const
{
	return CompressedTileCacheLayers.FindRef(FIntPoint(TileX, TileY));
}

// 判断 (TileX, TileY) 处是否已存在任何压缩 Tile 缓存条目（含空数组占位也算）。
bool FPImplRecastNavMesh::HasTileCacheLayers(int32 TileX, int32 TileY) const
{
	return CompressedTileCacheLayers.Contains(FIntPoint(TileX, TileY));
}

#undef INITIALIZE_NAVQUERY

#endif // WITH_RECAST