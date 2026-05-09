// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// RecastNavMeshGenerator.h —— 中文总览
// ---------------------------------------------------------------------------
// 本文件是整个 NavigationSystem 模块中最庞大的子系统（Generator），定义了 Recast
// NavMesh 的"生成侧"全部类型：
//   1) FRecastBuildConfig           —— Recast 生成参数（继承 rcConfig + UE 扩展位）
//   2) FRecastVoxelCache            —— 体素化缓存（减少重复 rasterize）
//   3) FRecastGeometryCache         —— 几何缓存头（UE 侧碰撞压缩进 NavCollision）
//   4) FRecastRawGeometryElement    —— 参与 Tile 构建的一块原始几何（含实例化 Transform）
//   5) FRecastAreaNavModifierElement —— 需要 Mark 的 Area 修饰（可带实例化 Transform）
//   6) FRcTileBox                   —— 把世界 Bounds → Tile (X,Y) 索引
//   7) FRecastTileGenerator         —— 【核心】单 Tile 异步生成器（DoWork/DoWorkTimeSliced）
//   8) FRecastNavMeshGenerator      —— 【核心】整张 NavMesh 生成调度器（多 Tile + Dirty 管理 + ActiveTiles）
//
// 关键数据流（架构文档 4.3 节 Recast Tile 生成）：
//   Dirty Area 流入 → RebuildDirtyAreas → MarkDirtyTiles 计算出 Tile 坐标集合
//     → ProcessTileTasksAsync 派发 FAsyncTask<FRecastTileGenerator>
//       → FRecastTileGenerator::DoWork：
//         a) GatherGeometry（Octree Box 查询收集 FNavigationRelevantData）
//         b) GenerateCompressedLayers:
//            CreateHeightField → RasterizeTriangles → VoxelFilter / RecastFilter
//            → BuildCompactHeightField → ErodeWalkable → BuildLayers → BuildTileCache
//         c) GenerateNavigationData（逐层）：
//            DecompressLayer → MarkDynamicAreas（Modifier → Area）
//            → BuildRegions → BuildContours → BuildPolyMesh / DetailMesh
//            → BuildTileCacheLinks（如 bGenerateLinks）→ dtCreateNavMeshData
//     → 主线程 AddGeneratedTiles / RemoveLayers / UpdateTileCache
//
// 时间切片机制（ALLOW_TIME_SLICE_NAV_REGEN）：
//   所有耗时函数提供 "XxxTimeSliced" 版本 + ETimeSliceWorkResult 返回值；
//   FRecastTileGenerator 内维护多个 EXxxState 枚举，记录上次切片停在哪里，
//   由 FNavRegenTimeSliceManager 根据预算（MinDuration/MaxDuration）决定何时 yield。
//
// 活跃 Tile（Invoker）：
//   当 bGenerateNavigationOnlyAroundNavigationInvokers=true 时：
//   FRecastNavMeshGenerator::UpdateActiveTiles 根据 InvokerLocations 做差集，
//   增加 PendingDirtyTiles，移除超出半径的已有 Tile。
// =====================================================================================

#pragma once 

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "Stats/Stats.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "AI/NavDataGenerator.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "AI/Navigation/NavigationInvokerPriority.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/NavigationModifier.h"
#include "AI/NavigationSystemBase.h"
#include "Async/AsyncWork.h"
#include "EngineDefines.h"
#include "NavDebugTypes.h"
#include "NavigationOctree.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/GCObject.h"

#if WITH_RECAST

#include "Recast/Recast.h"
#include "Detour/DetourNavMesh.h"
#include "Detour/DetourNavLinkBuilderConfig.h"

#if RECAST_INTERNAL_DEBUG_DATA
#include "NavMesh/RecastInternalDebugData.h"
#endif

class UBodySetup;
class ARecastNavMesh;
class FNavigationOctree;
class FNavMeshBuildContext;
class FRecastNavMeshGenerator;
struct FTileRasterizationContext;
struct BuildContext;
struct dtTileCacheLayer;
struct FKAggregateGeom;
struct FTileCacheCompressor;
struct FTileCacheAllocator;
struct FTileGenerationContext;
struct dtLinkBuilderData;
struct FNavigationElement;
class dtNavMesh;
class FNavRegenTimeSliceManager;
class UNavigationSystemV1;

#define MAX_VERTS_PER_POLY	6 // Recast PolyMesh 每个多边形最多 6 个顶点（与 Detour MAX_VERTS_PER_POLY 对齐）

// -------------------------------------------------------------------------------------
// FRecastBuildConfig —— 单 Tile 的 Recast 生成配置
// 继承 rcConfig（cs/ch/walkableHeight/walkableRadius/tileSize 等 Recast 原生字段）
// 再追加 UE 侧特有开关（体素过滤 / BV Tree / LowHeight / Link 生成等）
// 注：因为派生自 C struct 无虚函数，Reset() 用 Memzero 后再显式恢复默认值
// -------------------------------------------------------------------------------------
struct FRecastBuildConfig : public rcConfig
{
	/** controls whether voxel filtering will be applied (via FRecastTileGenerator::ApplyVoxelFilter) */
	// 是否应用体素过滤：在 Rasterize 之后用更激进的规则去掉"几何悬空"的 span
	uint32 bPerformVoxelFiltering:1;
	/** generate detailed mesh (additional tessellation to match heights of geometry) */
	// 生成 DetailMesh：用高度场细化 PolyMesh，贴合真实地形起伏（否则只在 Poly 平面上查询）
	uint32 bGenerateDetailedMesh:1;
	/** generate BV tree (space partitioning for queries) */
	// 构建 BV Tree：加速 Tile 内 Raycast/ProjectPoint 等查询
	uint32 bGenerateBVTree:1;
	/** if set, mark areas with insufficient free height instead of cutting them out  */
	// 把"高度不足"的多边形用 LowHeight Area 标记而不是删除
	// —— 用于 AI 蹲伏通过的矮通道；这些多边形被默认过滤器禁用，但专用 Filter 可以允许
	uint32 bMarkLowHeightAreas : 1;
	/** Expand the top of the area nav modifier's bounds by one cell height when applying to the navmesh.
		If unset, navmesh on top of surfaces might not be marked by marking bounds flush with top surfaces (since navmesh is generated slightly above collision, depending on cell height). */
	// Mark 时向顶部多扩 1 个 cell height，避免"盒子顶部恰好贴地"导致 Area 没标到
	uint32 bUseExtraTopCellWhenMarkingAreas : 1;
	/** if set, only single low height span will be allowed under valid one */
	// 限制 LowHeight span 序列：一个可走 span 下方只允许一个 LowHeight，防止多层堆叠
	uint32 bFilterLowSpanSequences : 1;
	/** if set, only low height spans with corresponding area modifier will be stored in tile cache (reduces memory, can't modify without full tile rebuild) */
	// 仅当存在对应 Area Modifier 时保留 LowHeight 到 TileCache（节省内存，代价是动态修改需要整 Tile 重建）
	uint32 bFilterLowSpanFromTileCache : 1;
	/** if set, navigation links will be automatically generated */
	// 自动生成 NavLink（跳跃/边沿）—— 由 JumpConfigs 驱动
	uint32 bGenerateLinks : 1;

	/** region partitioning method used by tile cache */
	// 区域划分算法：Monotone / Watershed / ChunkyMonotone（见 ERecastPartitioning）
	int32 TileCachePartitionType;
	/** chunk size for ChunkyMonotone partitioning */
	// ChunkyMonotone 的 chunk 尺寸（cell 数）
	int32 TileCacheChunkSize;

	/** indicates what's the limit of navmesh polygons per tile. This value is calculated from other
	 *	factors - DO NOT SET IT TO ARBITRARY VALUE */
	// 单 Tile 多边形上限：由 TileSize/CellSize 推算，不要手动改
	int32 MaxPolysPerTile;

	/** NavLink building configuration */
	// 自动 Link 生成的参数集合（可以有多组不同"跳跃类型"）
	TArray<dtNavLinkBuilderJumpConfig> JumpConfigs;
	
	/** Actual agent height (in uu)*/
	float AgentHeight;   // Agent 真实高度（Unreal 单位，cm）
	/** Actual agent climb (in uu)*/
	float AgentMaxClimb; // Agent 最大台阶
	/** Actual agent radius (in uu)*/
	float AgentRadius;   // Agent 真实半径
	/** Agent index for filtering links */
	int32 AgentIndex;    // Agent 槽位（对应 SupportedAgents 列表下标）
	/** Resolution level */ 
	ENavigationDataResolution TileResolution; // 本 Tile 使用的分辨率档（Low/Default/High）
	/** Ledge filtering mode */
	ENavigationLedgeSlopeFilterMode LedgeSlopeFilterMode; // 边沿过滤模式（Recast 原生 vs UE 自定义）
	/** Is the config completely setup */
	bool bIsTileSetupConfigCompleted; // 标记 Setup 是否已完成（Lazy 初始化守卫）
	/** Used when generating links automatically. Distance representing how far generated links can go outside a tile (in uu). */ 
	float LinkSpillDistance; // 自动生成的 Link 端点允许超出 Tile 的距离
	
	FRecastBuildConfig()
	{
		Reset();
	}

	// 重置到默认状态（用 Memzero 保证 rcConfig 基类也清零）
	void Reset()
	{
		FMemory::Memzero(*this);
		bPerformVoxelFiltering = true;
		bGenerateDetailedMesh = true;
		bGenerateBVTree = true;
		bMarkLowHeightAreas = false;
		bUseExtraTopCellWhenMarkingAreas = true;
		bFilterLowSpanSequences = false;
		bFilterLowSpanFromTileCache = false;
		bGenerateLinks = false;
		MaxPolysPerTile = -1;
		JumpConfigs.Empty();
		AgentIndex = 0;
		TileResolution = ENavigationDataResolution::Default;
		LedgeSlopeFilterMode = ENavigationLedgeSlopeFilterMode::Recast;
		bIsTileSetupConfigCompleted = false;
		LinkSpillDistance = 0.f;
	}

	// Tile 在世界单位下的尺寸 = tileSize (voxel 数) * cs (cell size)
	rcReal GetTileSizeUU() const { return tileSize * cs; }

	/** Detects if we are using big cell size values in relation to the walkableClimb and walkableSlopeAngle. */
	// 检测 cell 过大导致 walkableClimb/slope 与体素不匹配；用于日志警告
	bool IsUsingCoarseCellSize() const;
};

// -------------------------------------------------------------------------------------
// FRecastVoxelCache —— 多 Tile 的体素化缓存（减少相邻 Tile 重复 rasterize）
// 以链表串起 FTileInfo，SpanData 指向扁平 rcSpanCache 数组
// 仅在启用 TileVoxelCache 时使用
// -------------------------------------------------------------------------------------
struct FRecastVoxelCache
{
	struct FTileInfo
	{
		int32 TileX;         // Tile X 坐标
		int32 TileY;         // Tile Y 坐标
		int32 NumSpans;      // Span 数（SpanData 元素个数）
		FTileInfo* NextTile; // 链表下一项
		rcSpanCache* SpanData; // 扁平 Span 数组（由上游分配）
	};

	int32 NumTiles; // Tile 总数

	/** tile info */
	FTileInfo* Tiles; // 链表头

	FRecastVoxelCache() {}
	// 从一块序列化字节流还原（Memory 指向 FArchive 写出的数据）
	FRecastVoxelCache(const uint8* Memory);
};

// -------------------------------------------------------------------------------------
// FRecastGeometryCache —— StaticMesh/NavCollision 的"压缩几何"缓存读头
// 数据来源：UNavCollision 或 Cooked NavCollision；保存在 FNavigationRelevantData::CollisionData
// 本结构只提供指针布局，真正字节流由 Memory 生命周期控制
// -------------------------------------------------------------------------------------
struct FRecastGeometryCache
{
	struct FHeader
	{
		FNavigationRelevantData::FCollisionDataHeader Validation; // 版本校验/魔数

		int32 NumVerts;  // 顶点数
		int32 NumFaces;  // 面（三角形）数
		struct FWalkableSlopeOverride SlopeOverride; // 可选：覆盖斜率行为

		static uint32 StaticMagicNumber; // 版本迁移用
	};

	FHeader Header;

	/** recast coords of vertices (size: NumVerts * 3) */
	FVector::FReal* Verts; // Recast 坐标（-x,z,-y），交错 3 个为一顶点

	/** vert indices for triangles (size: NumFaces * 3) */
	int32* Indices;

	FRecastGeometryCache() {}
	NAVIGATIONSYSTEM_API FRecastGeometryCache(const uint8* Memory);
};

// -------------------------------------------------------------------------------------
// FRecastRawGeometryElement —— Tile 里一块原始几何的"工作内存"
// 来自 NavOctree 中的 FNavigationRelevantData：Elements 可以是单实例也可以是 ISM 实例化
// - GeomCoords/GeomIndices：基础网格（可能在 local 空间）
// - PerInstanceTransform：若非空则每个 Transform 是一个实例（ISM/HISM）
// -------------------------------------------------------------------------------------
struct FRecastRawGeometryElement
{
	// Instance geometry
	TArray<FVector::FReal>		GeomCoords;    // 交错 3 个为一个顶点（Recast 坐标）
	TArray<int32>				GeomIndices;   // 三角形索引

	// Bounds of the geometry
	FBox GeomCoordsBounds;
	
	// Per instance transformations in unreal coords
	// When empty, geometry is in world space
	TArray<FTransform>	PerInstanceTransform;

	rcRasterizationFlags RasterizationFlags; // 体素化特殊标志（例如双面）

	// 从 Modifier 解析需要的 Rasterization 位（如 FillCollisionUnderneath）
	NAVIGATIONSYSTEM_API static rcRasterizationFlags GetRasterizationFlags(const FCompositeNavModifier& InModifier);
};

// -------------------------------------------------------------------------------------
// FRecastAreaNavModifierElement —— 待在 Tile 内 Mark 的 Area 修饰
// 同样支持实例化 Transform。会被 MarkDynamicAreas 在 DecompressLayer 之后按 Area 应用
// -------------------------------------------------------------------------------------
struct FRecastAreaNavModifierElement
{
	TArray<FAreaNavModifier> Areas; // 一组 Area 定义（形状/Bounds/AreaClass）
	
	// Per instance transformations in unreal coords
	// When empty, areas are in world space
	TArray<FTransform>	PerInstanceTransform;

	ENavigationDataResolution NavMeshResolution = ENavigationDataResolution::Invalid; // 指定应用到哪个分辨率档
	
	bool bMaskFillCollisionUnderneathForNavmesh = false; // 是否屏蔽"FillCollisionUnderneath"副作用
};

// -------------------------------------------------------------------------------------
// FRcTileBox —— 把世界 AABB 转换到整数 Tile 坐标范围 [XMin,XMax] × [YMin,YMax]（闭区间）
// 关键点：
//  - 从 Unreal 坐标转到 Recast 坐标（注意轴互换：UE.Y ↔ Recast.Z）
//  - MaxCoord 如果正好落在 Tile 边界上，取下一格（保持半开区间语义）
//  - 无效 Bounds 时置 XMin=0,XMax=-1（循环体自然跳过）
// -------------------------------------------------------------------------------------
struct FRcTileBox
{
	int32 XMin, XMax, YMin, YMax;

	FRcTileBox(const FBox& UnrealBounds, const FVector& RcNavMeshOrigin, const FVector::FReal TileSizeInWorldUnits)
	{
		check(TileSizeInWorldUnits > 0);
		checkf(!RcNavMeshOrigin.ContainsNaN(), TEXT("%hs: RcNavMeshOrigin ContainsNaN"), __FUNCTION__);
		checkf(!UnrealBounds.ContainsNaN(), TEXT("%hs: UnrealBounds ContainsNaN"), __FUNCTION__);
		
		if (!ensureMsgf(UnrealBounds.IsValid, TEXT("%hs: UnrealBounds !IsValid"), __FUNCTION__))
		{
			// This is a bug but we'll handle it as well as we can.

			// Invalid bounds, set to empty range.
			// Max is set to -1, because the range is inclusive, used like: for (int32 y = TileBox.YMin; y <= TileBox.YMax; ++y)
			XMin = 0;
			XMax = -1;
			YMin = 0;
			YMax = -1;
			return;
		}

		auto CalcMaxCoordExclusive = [](const FVector::FReal MaxAsFloat, const int32 MinCoord) -> int32
		{
			FVector::FReal UnusedIntPart;
			// If MaxCoord falls exactly on the boundary of a tile
			if (FMath::Modf(MaxAsFloat, &UnusedIntPart) == 0)
			{
				// Return the lower tile
				return FMath::Max(ClampToInt32(FMath::FloorToInt(MaxAsFloat) - 1), MinCoord);
			}
			// Otherwise use default behaviour
			return ClampToInt32(FMath::FloorToInt(MaxAsFloat));
		};

		const FBox RcAreaBounds = Unreal2RecastBox(UnrealBounds);
		XMin = ClampToInt32(FMath::FloorToInt((RcAreaBounds.Min.X - RcNavMeshOrigin.X) / TileSizeInWorldUnits));
		XMax = CalcMaxCoordExclusive((RcAreaBounds.Max.X - RcNavMeshOrigin.X) / TileSizeInWorldUnits, XMin);
		YMin = ClampToInt32(FMath::FloorToInt((RcAreaBounds.Min.Z - RcNavMeshOrigin.Z) / TileSizeInWorldUnits));
		YMax = CalcMaxCoordExclusive((RcAreaBounds.Max.Z - RcNavMeshOrigin.Z) / TileSizeInWorldUnits, YMin);
	}

	inline bool Contains(const FIntPoint& Point) const
	{
		return Point.X >= XMin && Point.X <= XMax
			&& Point.Y >= YMin && Point.Y <= YMax;
	}

	static inline int32 ClampToInt32(const int64 Value)
	{
#if !NO_LOGGING
		UE_CLOG(!IntFitsIn<int32>(Value), LogNavigation, Warning,
			TEXT("FRcTileBox clamped a NavMesh transform value to fit in int32. Old value: %" UINT64_FMT), Value);
#endif // !NO_LOGGING

		return static_cast<int32>(FMath::Clamp(Value, MIN_int32, MAX_int32));
	}
};

/**
 * TIME SLICING 
 * The general idea is that any function that handles time slicing internally will be named XXXXTimeSliced
 * and returns a ETimeSliceWorkResult. These functions also call TestTimeSliceFinished() internally when required,
 * IsTimeSliceFinishedCached() can be called externally after they have finished. Non time sliced functions are 
 * managed externally and the calling function should call TestTimeSliceFinished() when necessary.
 */

/** Return state of calling time sliced functions */
// 时间切片函数的返回码；框架据此决定是否继续当前帧/排到下一帧
enum class ETimeSliceWorkResult : uint8
{
	Failed,                 // 出错，任务失败
	Succeeded,              // 成功完成（或无需再做）
	CallAgainNextTimeSlice, // 本帧预算用完，下一帧继续调用
};

/** State representing which area of GenerateCompressedLayersTimeSliced() we are processing */
// 压缩层生成的状态机：依次走过 Init→HeightField→Rasterize→Filter→Compact→Erode→BuildLayers→TileCache
enum class EGenerateCompressedLayersTimeSliced : uint8
{
	Invalid,
	Init,
	CreateHeightField,
	RasterizeTriangles,
	EmptyLayers,
	VoxelFilter,
	RecastFilter,
	CompactHeightField,
	ErodeWalkable,
	BuildLayers,
	BuildTileCache,
};

// 体素化（Recast）内部切片：标记可走三角形 → 真正 Rasterize
enum class ERasterizeGeomRecastTimeSlicedState : uint8 
{
	MarkWalkableTriangles,
	RasterizeTriangles,
};

// 带 Transform 的几何体素化切片：先转坐标/翻索引，再调 Recast
enum class ERasterizeGeomTimeSlicedState : uint8
{
	RasterizeGeometryTransformCoordsAndFlipIndices,
	RasterizeGeometryTransformCoords UE_DEPRECATED(5.5, "The state doesn't handle indices flipping. Use RasterizeGeometryTransformCoordsAndIndices instead.") = RasterizeGeometryTransformCoordsAndFlipIndices,
	RasterizeGeometryRecast,
};

// Recast 过滤 3 个阶段
enum class EGenerateRecastFilterTimeSlicedState : uint8
{
	FilterLowHangingWalkableObstacles, // 掉到邻居顶部的"悬挂"可走面
	FilterLedgeSpans,                   // 边沿过滤（走不了的台边）
	FilterWalkableLowHeightSpans,       // 可走但高度不够
};

// FRecastTileGenerator::DoWork 最外层状态
enum class EDoWorkTimeSlicedState : uint8
{
	Invalid,
	GatherGeometryFromSources, // 1. 收集几何
	GenerateTile,              // 2. 生成 Tile（CompressedLayers + NavigationData）
};

// 生成 Tile 的两阶段
enum class EGenerateTileTimeSlicedState : uint8
{
	Invalid,
	GenerateCompressedLayers,
	GenerateNavigationData,
};

// 生成 NavigationData 内部的阶段
enum class EGenerateNavDataTimeSlicedState : uint8
{
	Invalid,
	Init,
	GenerateLayers,
};

// Tile 级别时间切片调优常量（FilterLedge 每帧最多处理的 Y 扫描行数）
struct FRecastTileTimeSliceSettings
{
	int32 FilterLedgeSpansMaxYProcess = 13;
};

// FNavigationLink 的派生：带自动生成 Link 的 Poly Flag / Area 信息
struct FGeneratedNavigationLink : public FNavigationLink
{
	/// User defined flags assigned to the polys of off-mesh connections
	unsigned short generatedLinkPolyFlag = 0;

	/// User defined area ids assigned to the off-mesh connections
	unsigned char generatedLinkArea = 0;
};

/**
 * Class handling generation of a single tile, caching data that can speed up subsequent tile generations
 */
// -------------------------------------------------------------------------------------
// FRecastTileGenerator —— 单 Tile 构建实体（架构文档中简称 TileGenerator）
// 生命周期：
//   1. FRecastNavMeshGenerator 在发现 Tile 脏后 new 一个 FAsyncTask<FRecastTileGenerator>
//   2. Setup() 从 ParentGenerator 拷贝 NavMesh 配置、Tile 坐标、DirtyAreas
//   3. DoWork() 或 DoWorkTimeSliced() 在后台线程跑 Recast 管线
//   4. 完成后 ParentGenerator 取走 NavigationData/CompressedLayers → 主线程回补 NavMesh
// 继承 FGCObject：保证工作期间持有的 UObject 引用不被 GC 回收
// -------------------------------------------------------------------------------------
class FRecastTileGenerator : public FNoncopyable, public FGCObject
{
	friend FRecastNavMeshGenerator;

public:
	NAVIGATIONSYSTEM_API FRecastTileGenerator(FRecastNavMeshGenerator& ParentGenerator, const FIntPoint& Location, const double PendingTileCreationTime);
	NAVIGATIONSYSTEM_API virtual ~FRecastTileGenerator();
		
	/** Does the work involved with regenerating this tile using time slicing.
	 *  The return value determines the result of the time slicing
	 */
	// 时间切片版本：每次消耗一定预算后可 yield；调用方循环 tick 直到返回 Succeeded/Failed
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult DoWorkTimeSliced();
	/** Does the work involved with regenerating this tile */
	// 一次性完成的版本：在后台线程或同步上下文里跑完整个 Recast 管线
	NAVIGATIONSYSTEM_API bool DoWork();

	inline int32 GetTileX() const { return TileX; }      // Tile 的 X 坐标（Generator 空间整数）
	inline int32 GetTileY() const { return TileY; }      // Tile 的 Y 坐标
	inline const FBox& GetTileBB() const { return TileBB; } // Tile 在世界空间的 AABB
	/** Whether specified layer was updated */
	// 是否有指定层被重新构建（部分重建可能只刷特定层）
	inline bool IsLayerChanged(int32 LayerIdx) const { return DirtyLayers[LayerIdx]; }
	inline const TBitArray<>& GetDirtyLayersMask() const { return DirtyLayers; }
	/** Whether tile data was fully regenerated */
	// 是否完整重建了压缩层（bRegenerateCompressedLayers==true 时 true）
	inline bool IsFullyRegenerated() const { return bRegenerateCompressedLayers; }
	/** Whether tile task has anything to build */
	// 判断是否有几何/Modifier 参与构建，否则跳过直接产生空 Tile
	NAVIGATIONSYSTEM_API bool HasDataToBuild() const;

	const TArray<FNavMeshTileData>& GetCompressedLayers() const { return CompressedLayers; }

	// 纯函数：根据 Tile (X,Y) + NavMesh 原点 + TileSize + 总 Bounds 计算 Tile 的 AABB
	// 静态的，方便其它地方（例如 DirtyAreas 计算）复用
	static NAVIGATIONSYSTEM_API FBox CalculateTileBounds(int32 X, int32 Y, const FVector& RcNavMeshOrigin, const FBox& TotalNavBounds, FVector::FReal TileSizeInWorldUnits);

protected:
	// to be used solely by FRecastNavMeshGenerator
	// 仅供父 Generator 访问：取走本 Tile 生成出来的 NavigationData（移动语义消费）
	TArray<FNavMeshTileData>& GetNavigationData() { return NavigationData; }
	
public:
	// 统计本 Tile 使用的内存（StatReport 用）
	NAVIGATIONSYSTEM_API uint32 GetUsedMemCount() const;

	// Memory amount used to construct generator 
	uint32 UsedMemoryOnStartup;

	// FGCObject begin
	// 把 Geometry/NavRelevantData 里可能的 UObject 引用登记给 GC
	NAVIGATIONSYSTEM_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	NAVIGATIONSYSTEM_API virtual FString GetReferencerName() const override;
	// FGCObject end

#if RECAST_INTERNAL_DEBUG_DATA
	// 中间数据（Heightfield/Contour/PolyMesh 等）可视化访问
	const FRecastInternalDebugData& GetDebugData() const { return DebugData; }
	FRecastInternalDebugData& GetMutableDebugData() { return DebugData; }
#endif
		
	// 体素 mask 数组的 inline 分配器（常见 4096 cell）
	typedef TArray<int32, TInlineAllocator<4096>> TInlineMaskArray;

protected:
	/** Does the actual TimeSliced tile generation. 
	 *	@note always trigger tile generation only via DoWorkTimeSliced(). This is a worker function
	 *  The return value determines the result of the time slicing
	 *	@return Suceeded if new tile navigation data has been generated and is ready to be added to navmesh instance,
	 *	@return Failed if failed or no need to generate (still valid).
	 *  @return CallAgainNextTimeSlice, time slice is finished this frame but we need to call this function again next frame
	 */
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult GenerateTileTimeSliced();
	/** Does the actual tile generation.
	 *	@note always trigger tile generation only via DoWorkTime(). This is a worker function
	 *  The return value determines the result of the time slicing
	 *	@return true if new tile navigation data has been generated and is ready to be added to navmesh instance,
	 *	@return false if failed or no need to generate (still valid).
	 */
	NAVIGATIONSYSTEM_API bool GenerateTile();

	// 初始化：拷贝 ParentGenerator 的配置、Tile 参数、DirtyAreas 过滤；设置 TileBB
	NAVIGATIONSYSTEM_API void Setup(const FRecastNavMeshGenerator& ParentGenerator, const TArray<FBox>& DirtyAreas);

	/** Find highest navmesh resolution from Modifiers and use it to update the tile configuration. */
	// 扫描落入本 Tile 的 Modifier，取最高 Resolution，用它覆写 TileResolution
	void SetupTileConfigFromHighestResolution(const FRecastNavMeshGenerator& ParentGenerator);
	
	/** Gather geometry */
	// 一次性收集几何（在 Setup 阶段做；Setup 之后不可再访问 NavOctree，避免线程冲突）
	NAVIGATIONSYSTEM_API virtual void GatherGeometry(const FRecastNavMeshGenerator& ParentGenerator, bool bGeometryChanged);
	/** Gather geometry sources to be processed later by the GatherGeometryFromSources */
	// 只收集"来源"（FNavigationRelevantData 引用），真正解码推迟到工作线程
	// 优点：NavOctree Query 发生在主线程，解码耗时移到后台
	NAVIGATIONSYSTEM_API virtual void PrepareGeometrySources(const FRecastNavMeshGenerator& ParentGenerator, bool bGeometryChanged);
	/** Gather geometry from the prefetched sources */
	NAVIGATIONSYSTEM_API void GatherGeometryFromSources();
	/** Gather geometry from the prefetched sources time sliced version */
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult GatherGeometryFromSourcesTimeSliced();
	/** Gather geometry from a specified Navigation Data */
	// 单个 NavRelevantData 的处理入口：根据 Modifier 决定走几何 / Area Mark / NavLink 三路
	NAVIGATIONSYSTEM_API void GatherNavigationDataGeometry(const TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe>& ElementData, UNavigationSystemV1& NavSys, const FNavDataConfig& OwnerNavDataConfig, bool bGeometryChanged);

	/** Start functions used by GenerateCompressedLayersTimeSliced / GenerateCompressedLayers */
	// Recast 标准管线的每一步（Time Sliced 版与一次性版通常成对）：
	NAVIGATIONSYSTEM_API bool CreateHeightField(FNavMeshBuildContext& BuildContext);
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult RasterizeTrianglesTimeSliced(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API void RasterizeTriangles(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult RasterizeGeometryRecastTimeSliced(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FBox& CoordsBounds, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API void RasterizeGeometryRecast(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FBox& CoordsBounds, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API void RasterizeGeometryTransformCoordsAndFlipIndices(const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FBox& CoordsBounds, const FTransform& LocalToWorld);
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult RasterizeGeometryTimeSliced(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FBox& CoordsBounds, const FTransform& LocalToWorld, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API void RasterizeGeometry(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FBox& CoordsBounds, const FTransform& LocalToWorld, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API void GenerateRecastFilter(FNavMeshBuildContext& BuildContext);
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult GenerateRecastFilterTimeSliced(FNavMeshBuildContext& BuildContext);
	NAVIGATIONSYSTEM_API bool BuildCompactHeightField(FNavMeshBuildContext& BuildContext);
	NAVIGATIONSYSTEM_API bool RecastErodeWalkable(FNavMeshBuildContext& BuildContext);
	NAVIGATIONSYSTEM_API bool RecastBuildLayers(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API bool RecastBuildTileCache(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	/** End functions used by GenerateCompressedLayersTimeSliced / GenerateCompressedLayers */

	/** Builds CompressedLayers array (geometry + modifiers) time sliced*/
	// 压缩层生成总入口（切片版）
	NAVIGATIONSYSTEM_API virtual ETimeSliceWorkResult GenerateCompressedLayersTimeSliced(FNavMeshBuildContext& BuildContext);

	/** Builds CompressedLayers array (geometry + modifiers) */
	// 压缩层生成总入口（一次性版）；InLinkBuilderData 为 Link 生成提供 Tile 邻居信息
	NAVIGATIONSYSTEM_API virtual bool GenerateCompressedLayers(FNavMeshBuildContext& BuildContext, const dtLinkBuilderData& InLinkBuilderData);

	/** Builds a navigation data layer */
	// 生成单层 NavigationData：Decompress → MarkDynamicArea → BuildRegions → Contour → Poly → Detail → Links
	NAVIGATIONSYSTEM_API bool GenerateNavigationDataLayer(FNavMeshBuildContext& BuildContext, FTileCacheCompressor& TileCompressor, FTileCacheAllocator& GenNavAllocator, FTileGenerationContext& GenerationContext, const dtLinkBuilderData& InLinkBuilderData, int32 LayerIdx);

	/** Builds NavigationData array (layers + obstacles) time sliced */
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult GenerateNavigationDataTimeSliced(FNavMeshBuildContext& BuildContext, const dtLinkBuilderData& InLinkBuilderData);

	/** Builds NavigationData array (layers + obstacles) */
	NAVIGATIONSYSTEM_API bool GenerateNavigationData(FNavMeshBuildContext& BuildContext, const dtLinkBuilderData& InLinkBuilderData);

	/** Builds navigation links */
	// 基于 TileCacheContourSet 自动生成跳跃/下落 Link，输出到 OutGeneratedLinks
	dtStatus BuildTileCacheLinks(FNavMeshBuildContext& BuildContext, struct dtTileCacheAlloc* alloc, const dtTileCacheLayer& layer,
		const struct dtTileCacheContourSet& lcset, TArray<FGeneratedNavigationLink>& OutGeneratedLinks) const;
	
	// Voxel 级过滤（UE 扩展）：去除 Recast 过滤不了的悬空 span
	NAVIGATIONSYSTEM_API virtual void ApplyVoxelFilter(struct rcHeightfield* SolidHF, FVector::FReal WalkableRadius);

	/** Compute rasterization mask */
	// 预计算体素化 mask：某些 Modifier（FillCollisionUnderneath）需要在体素化时就改写
	NAVIGATIONSYSTEM_API void InitRasterizationMaskArray(const rcHeightfield* InSolidHF, TInlineMaskArray& OutRasterizationMasks);
	NAVIGATIONSYSTEM_API void ComputeRasterizationMasks(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	NAVIGATIONSYSTEM_API void MarkRasterizationMask(rcContext* /*BuildContext*/, rcHeightfield* InSolidHF,
		const FAreaNavModifier& Modifier, const FTransform& LocalToWorld, const int32 Mask, TInlineMaskArray& OutMaskArray);

	/** apply areas from DynamicAreas to layer */
	// 在 TileCache Layer 上把所有 Modifier 应用成 Area ID
	NAVIGATIONSYSTEM_API void MarkDynamicAreas(dtTileCacheLayer& Layer);
	NAVIGATIONSYSTEM_API void MarkDynamicArea(const FAreaNavModifier& Modifier, const FTransform& LocalToWorld, dtTileCacheLayer& Layer);
	NAVIGATIONSYSTEM_API void MarkDynamicArea(const FAreaNavModifier& Modifier, const FTransform& LocalToWorld, dtTileCacheLayer& Layer, const int32 AreaId, const int32* ReplaceAreaIdPtr);

	// 把一组 Modifier/几何追加进 Tile 的工作队列
	NAVIGATIONSYSTEM_API void AppendModifier(const FCompositeNavModifier& Modifier, const FNavDataPerInstanceTransformDelegate& InTransformsDelegate);
	/** Appends specified geometry to tile's geometry */
	NAVIGATIONSYSTEM_API void ValidateAndAppendGeometry(const FNavigationRelevantData& ElementData, const FCompositeNavModifier& InModifier);

	NAVIGATIONSYSTEM_API void AppendGeometry(const FNavigationRelevantData& ElementData, const FCompositeNavModifier& InModifier, const FNavDataPerInstanceTransformDelegate& InTransformsDelegate);
	
	/** prepare voxel cache from collision data */
	NAVIGATIONSYSTEM_API void PrepareVoxelCache(const TNavStatArray<uint8>& RawCollisionCache, const FBox& RecastBounds, const FCompositeNavModifier& InModifier, TNavStatArray<rcSpanCache>& SpanData);
	NAVIGATIONSYSTEM_API bool HasVoxelCache(const TNavStatArray<uint8>& RawVoxelCache, rcSpanCache*& CachedVoxels, int32& NumCachedVoxels) const;
	NAVIGATIONSYSTEM_API void AddVoxelCache(TNavStatArray<uint8>& RawVoxelCache, const rcSpanCache* CachedVoxels, const int32 NumCachedVoxels) const;

	NAVIGATIONSYSTEM_API void DumpAsyncData();
	NAVIGATIONSYSTEM_API void DumpSyncData();

#if RECAST_INTERNAL_DEBUG_DATA
	NAVIGATIONSYSTEM_API bool IsTileDebugActive() const;
	NAVIGATIONSYSTEM_API bool IsTileDebugAllowingGeneration() const;
#endif

	/** Deprecated functions */
	UE_DEPRECATED(5.5, "Use the new version without RasterContext instead.")
	NAVIGATIONSYSTEM_API bool CreateHeightField(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.5, "Use the new version without RasterContext instead.")
	NAVIGATIONSYSTEM_API void GenerateRecastFilter(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.5, "Use the new version without RasterContext instead.")
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult GenerateRecastFilterTimeSliced(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.5, "Use the new version without RasterContext instead.")
	NAVIGATIONSYSTEM_API bool BuildCompactHeightField(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.5, "Use the new version without RasterContext instead.")
	NAVIGATIONSYSTEM_API bool RecastErodeWalkable(FNavMeshBuildContext& BuildContext, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.5, "This function was not handling the indices order correctly and is replaced by RasterizeGeometryTransformCoordsAndFlipIndices.")
	NAVIGATIONSYSTEM_API void RasterizeGeometryTransformCoords(const TArray<FVector::FReal>& Coords, const FTransform& LocalToWorld);
	UE_DEPRECATED(5.5, "Use the overload with dtLinkBuilderData instead.")
	NAVIGATIONSYSTEM_API virtual bool GenerateCompressedLayers(FNavMeshBuildContext& BuildContext);
	UE_DEPRECATED(5.5, "Use the overload with dtLinkBuilderData instead.")
	NAVIGATIONSYSTEM_API bool GenerateNavigationDataLayer(FNavMeshBuildContext& BuildContext, FTileCacheCompressor& TileCompressor, FTileCacheAllocator& GenNavAllocator, FTileGenerationContext& GenerationContext, int32 LayerIdx);
	UE_DEPRECATED(5.5, "Use the overload with dtLinkBuilderData instead.")
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult GenerateNavigationDataTimeSliced(FNavMeshBuildContext& BuildContext);
	UE_DEPRECATED(5.5, "Use the overload with dtLinkBuilderData instead.")
	NAVIGATIONSYSTEM_API bool GenerateNavigationData(FNavMeshBuildContext& BuildContext);
	UE_DEPRECATED(5.5, "Use the version taking a const reference on FNavigationRelevantData.")
	NAVIGATIONSYSTEM_API void ValidateAndAppendGeometry(const TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe>& ElementData, const FCompositeNavModifier& InModifier);
	UE_DEPRECATED(5.6, "Please use the overload with the added CoordBounds.")
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult RasterizeGeometryRecastTimeSliced(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.6, "Please use the overload with the added CoordBounds.")
	NAVIGATIONSYSTEM_API void RasterizeGeometryRecast(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.6, "Please use the overload with the added CoordBounds.")
	NAVIGATIONSYSTEM_API void RasterizeGeometryTransformCoordsAndFlipIndices(const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FTransform& LocalToWorld);
	UE_DEPRECATED(5.6, "Please use the overload with the added CoordBounds.")
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult RasterizeGeometryTimeSliced(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FTransform& LocalToWorld, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.6, "Please use the overload with the added CoordBounds.")
	NAVIGATIONSYSTEM_API void RasterizeGeometry(FNavMeshBuildContext& BuildContext, const TArray<FVector::FReal>& Coords, const TArray<int32>& Indices, const FTransform& LocalToWorld, const rcRasterizationFlags RasterizationFlags, FTileRasterizationContext& RasterContext);
	UE_DEPRECATED(5.6, "Please use the overload with the added RecastBounds.")
	NAVIGATIONSYSTEM_API void PrepareVoxelCache(const TNavStatArray<uint8>& RawCollisionCache, const FCompositeNavModifier& InModifier, TNavStatArray<rcSpanCache>& SpanData);

protected:
	// ===== Tile 状态位 =====
	uint32 bRegenerateCompressedLayers : 1;           // 是否完整重建压缩层（true=丢弃 CachedLayer 重新 rasterize）
	uint32 bFullyEncapsulatedByInclusionBounds : 1;   // Tile 是否完全在 InclusionBounds 内（可省掉裁剪）
	uint32 bUpdateGeometry : 1;                       // 是否需要重新收集几何
	uint32 bHasLowAreaModifiers : 1;                  // 是否有 LowHeight Area 的 Modifier（影响 Filter 行为）
	uint32 bHasGeometryToRasterizeAsFilledConvexVolume : 1; // 存在"填充凸体"类型几何（CSG Cut）

	/** Start time slicing variables */
	// ===== 时间切片状态机持久化字段 =====
	// 存储每个阶段停在哪里，下一次调用从这里继续
	ERasterizeGeomRecastTimeSlicedState RasterizeGeomRecastState;
	ERasterizeGeomTimeSlicedState RasterizeGeomState;
	EGenerateRecastFilterTimeSlicedState GenerateRecastFilterState;
	int32 GenRecastFilterLedgeSpansYStart;            // FilterLedgeSpans 下次从第几行 Y 继续
	EDoWorkTimeSlicedState DoWorkTimeSlicedState;
	EGenerateTileTimeSlicedState GenerateTileTimeSlicedState;

	EGenerateNavDataTimeSlicedState GenerateNavDataTimeSlicedState;
	int32 GenNavDataLayerTimeSlicedIdx;               // 正在处理第几层
	EGenerateCompressedLayersTimeSliced GenCompressedLayersTimeSlicedState;
	int32 RasterizeTrianglesTimeSlicedRawGeomIdx;     // 体素化迭代到 RawGeometry 的第几项
	int32 RasterizeTrianglesTimeSlicedInstTransformIdx; // 该项的第几个实例 Transform
	TNavStatArray<uint8> RasterizeGeomRecastTriAreas; // rcMarkWalkableTriangles 的结果
	const FNavRegenTimeSliceManager* TimeSliceManager;// 父 Generator 的预算管理器（弱持有）

	TUniquePtr<struct FTileCacheAllocator> GenNavDataTimeSlicedAllocator;
	TUniquePtr<struct FTileGenerationContext> GenNavDataTimeSlicedGenerationContext;
	TUniquePtr<struct FTileRasterizationContext> GenCompressedlayersTimeSlicedRasterContext;

	FRecastTileTimeSliceSettings TileTimeSliceSettings;
	/** End time slicing variables */

	int32 TileX; // Tile 网格坐标 X
	int32 TileY; // Tile 网格坐标 Y
	uint32 Version; // 与 ParentGenerator.Version 对比，用于丢弃过期任务

	/** Time when the tile was requested */
	double TileCreationTime = 0.;
	
	/** Tile's bounding box, Unreal coords */
	FBox TileBB;                        // Tile 的精确 AABB

	/** Tile's bounding box expanded by Agent's radius horizontally, Unreal coords */
	FBox TileBBExpandedForAgent;        // 膨胀到 AgentRadius：用于收集跨边几何
	
	/** Layers dirty flags */
	TBitArray<> DirtyLayers;            // 每层是否需要刷新的位图
	
	/** Parameters defining navmesh tiles */
	FRecastBuildConfig TileConfig;      // 本 Tile 具体使用的配置（可能覆盖分辨率）

	/** Bounding geometry definition. */
	TNavStatArray<FBox> InclusionBounds; // 本 Tile 相交到的 NavMeshBoundsVolume

	/** Additional config */
	FRecastNavMeshCachedData AdditionalCachedData; // Area/Flag 缓存（避免每次去 ARecastNavMesh 查）

	// generated tile data
	TArray<FNavMeshTileData> CompressedLayers;  // 压缩后的 Tile 层（中间产物，用于增量修改 Area）
	TArray<FNavMeshTileData> NavigationData;    // 最终 dtMeshTile 可用的数据（主线程消费）

	/** Result of calling RasterizeGeometry() */
	// 体素化工作缓冲：把 UE 坐标转到 Recast 坐标的结果，跨越多个切片帧复用
	TArray<FVector::FReal> RasterizeGeometryWorldRecastCoords;
	TArray<int32> RasterizeGeometryFlippedIndices;
	FBox RasterizeGeometryWorldRecastCoordsBounds;
	uint8 bRasterizeGeometryUseFlippedIndices : 1;

	// tile's geometry: without voxel cache
	// 收集阶段产出；DoWork 阶段消费
	TArray<FRecastRawGeometryElement> RawGeometry;       // 原始几何（三角面）
	// areas used for creating navigation data: obstacles
	TArray<FRecastAreaNavModifierElement> Modifiers;     // Area 修饰
	// navigation links
	TArray<FSimpleLinkNavModifier> OffmeshLinks;         // 手动定义的 NavLink

	TWeakPtr<FNavDataGenerator, ESPMode::ThreadSafe> ParentGeneratorWeakPtr; // 父 Generator 弱引用

	TNavStatArray<TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> > NavigationRelevantData; // PrepareGeometrySources 阶段的"来源"
	TWeakObjectPtr<UNavigationSystemV1> NavSystem;
	FNavDataConfig NavDataConfig;

	// 体素中间产物：SolidHF→CompactHF→PolyMesh...
	rcHeightfield* SolidHF;
	rcCompactHeightfield* CompactHF;

	FRecastNavMeshTileGenerationDebug TileDebugSettings;

#if RECAST_INTERNAL_DEBUG_DATA
	FRecastInternalDebugData DebugData; // 保留中间数据供可视化
#endif
};

// -------------------------------------------------------------------------------------
// FRecastTileGeneratorWrapper —— AsyncTask 包装，持有 TileGenerator 的 SharedRef
// 作用：FAsyncTask 本身需要值语义 + 非 abandonable；真正的生成对象放在 SharedRef 里
// 方便主线程再持有一份引用（例如取消后依然能 Read Result）
// -------------------------------------------------------------------------------------
struct FRecastTileGeneratorWrapper : public FNonAbandonableTask
{
	TSharedRef<FRecastTileGenerator> TileGenerator;

	FRecastTileGeneratorWrapper(TSharedRef<FRecastTileGenerator> InTileGenerator)
		: TileGenerator(InTileGenerator)
	{
	}
	
	// AsyncTask 线程入口
	void DoWork()
	{
		TileGenerator->DoWork();
	}

	inline TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FRecastTileGenerator, STATGROUP_ThreadPoolAsyncTasks);
	}
};

typedef FAsyncTask<FRecastTileGeneratorWrapper> FRecastTileGeneratorTask;
//typedef FAsyncTask<FRecastTileGenerator> FRecastTileGeneratorTask;

// -------------------------------------------------------------------------------------
// FPendingTileElement —— 待构建 Tile 的排队描述
// 核心字段：Tile 坐标 + SeedDistance（距离 Invoker/玩家）+ CreationTime（老化控制）
// 被 PendingDirtyTiles（TArray）管理，按 SeedDistance 排序；生产者 = MarkDirtyTiles
// -------------------------------------------------------------------------------------
struct FPendingTileElement
{
	/** tile coordinates on a grid in recast space */
	FIntPoint	Coord;
	/** distance to seed, used for sorting pending tiles */
	FVector::FReal SeedDistance;
	/** time at which the element was first added to the queue */
	double		CreationTime;

#if !UE_BUILD_SHIPPING	
	/** Squared distance to invoker source */
	FVector::FReal DebugInvokerDistanceSquared = TNumericLimits<FVector::FReal>::Max();

	/** Priority from navigation invoker */
	ENavigationInvokerPriority DebugInvokerPriority = ENavigationInvokerPriority::Default;
#endif // !UE_BUILD_SHIPPING

	/** Priority used for sorting */
	ENavigationInvokerPriority SortingPriority = ENavigationInvokerPriority::Default;

	/** Whether we need a full rebuild for this tile grid cell */
	bool		bRebuildGeometry; // true=几何变化，需重建压缩层；false=仅 Area 变化
	/** We need to store dirty area bounds to check which cached layers needs to be regenerated
	 *  In case geometry is changed, cached layers data will be fully regenerated without using dirty areas list
	 */
	TArray<FBox> DirtyAreas;          // 变脏区域列表，用于"只刷被影响层"的优化

	FPendingTileElement()
		: Coord(FIntPoint::NoneValue)
		, SeedDistance(TNumericLimits<FVector::FReal>::Max())
		, CreationTime(-1.)
		, bRebuildGeometry(false)
	{
	}

	bool operator == (const FIntPoint& Location) const
	{
		return Coord == Location;
	}

	bool operator == (const FPendingTileElement& Other) const
	{
		return Coord == Other.Coord;
	}

	// 排序：SeedDistance 小的在前（近玩家优先）
	bool operator < (const FPendingTileElement& Other) const
	{
		return Other.SeedDistance < SeedDistance;
	}

	// 判断 Tile 在队列里停留了多久（用于超时/老化策略）
	double GetDurationSinceCreation(const double CurrenTimeSeconds) const
	{
		return (CreationTime > 0.) ? CurrenTimeSeconds - CreationTime : 0.;
	}

	friend uint32 GetTypeHash(const FPendingTileElement& Element)
	{
		return GetTypeHash(Element.Coord);
	}
};

// -------------------------------------------------------------------------------------
// TRunningTileElement —— 正在跑的 Tile 任务
// 持有对应 AsyncTask 的原始指针；bShouldDiscard=true 代表结果不要回补到 NavMesh
// （例如 NavMesh 已经 Reset 了）
// -------------------------------------------------------------------------------------
template<typename TTileGeneratorTask>
struct TRunningTileElement
{
	TRunningTileElement()
		: Coord(FIntPoint::NoneValue)
		, bShouldDiscard(false)
		, AsyncTask(nullptr)
	{
	}
	
	TRunningTileElement(FIntPoint InCoord)
		: Coord(InCoord)
		, bShouldDiscard(false)
		, AsyncTask(nullptr)
	{
	}

	bool operator == (const TRunningTileElement& Other) const
	{
		return Coord == Other.Coord;
	}
	
	/** tile coordinates on a grid in recast space */
	FIntPoint					Coord;
	/** whether generated results should be discarded */
	bool						bShouldDiscard; 
	FAsyncTask<TTileGeneratorTask>* AsyncTask;
};

UE_DEPRECATED(5.5, "FRunningTileElement is deprecated. Please use TRunningTileElement<FRecastTileGeneratorWrapper> instead.")
typedef TRunningTileElement<FRecastTileGeneratorWrapper> FRunningTileElement;

// Tile 时间戳：用于统计 Tile 的创建时刻（cvar 调试）
struct FTileTimestamp
{
	FNavTileRef NavTileRef;
	double Timestamp;
	
	bool operator == (const FTileTimestamp& Other) const
	{
		return NavTileRef == Other.NavTileRef;
	}
};

// ProcessTileTasksSyncTimeSliced 的状态机：从 Init → DoWork → AddGeneratedTiles
//   → StoreCompressedTileCacheLayers → AppendUpdateTiles → Finish
enum class EProcessTileTasksSyncTimeSlicedState : uint8
{
	Init,
	DoWork,
	AddGeneratedTiles,
	StoreCompessedTileCacheLayers,
	AppendUpdateTiles,
	Finish,
};

// AddGeneratedTiles 内部状态机
enum class EAddGeneratedTilesTimeSlicedState : uint8
{
	Init,
	AddTiles,
};

/**
 * Class that handles generation of the whole Recast-based navmesh.
 */
// -------------------------------------------------------------------------------------
// FRecastNavMeshGenerator —— 整张 NavMesh 的生成调度器（FNavDataGenerator 实现）
// 核心职责：
//   1) 持有 dtNavMesh* 句柄（ConstructTiledNavMesh 创建）
//   2) 维护 PendingDirtyTiles / RunningDirtyTiles / ActiveTiles 三个集合
//   3) RebuildDirtyAreas → MarkDirtyTiles 把脏区转成 Tile 坐标
//   4) Tick(TickAsyncBuild) 调度 ProcessTileTasksAsync/Sync 派发/回收 FRecastTileGenerator
//   5) AddGeneratedTilesAndGetUpdatedTiles 把 TileGenerator 产物提交到 dtNavMesh
//   6) Invoker 模式下维护 ActiveTiles 集合，按 GenerationRadius/RemovalRadius 差集
// 线程约束：大部分成员只被主线程/TickAsyncBuild 调用；SyncTimeSlicedData 内部字段按
// 时间切片状态机访问（见下方状态枚举）
// -------------------------------------------------------------------------------------
class FRecastNavMeshGenerator : public FNavDataGenerator
{
public:
	NAVIGATIONSYSTEM_API FRecastNavMeshGenerator(ARecastNavMesh& InDestNavMesh);
	NAVIGATIONSYSTEM_API virtual ~FRecastNavMeshGenerator();

private:
	/** Prevent copying. */
	FRecastNavMeshGenerator(FRecastNavMeshGenerator const& NoCopy) { check(0); };
	FRecastNavMeshGenerator& operator=(FRecastNavMeshGenerator const& NoCopy) { check(0); return *this; }

public:
	// 重建全部：清掉 NavMesh 数据并把所有 InclusionBounds 范围内 Tile 重新入队
	NAVIGATIONSYSTEM_API virtual bool RebuildAll() override;
	// 阻塞等待所有任务完成（关卡切换/退出用）
	NAVIGATIONSYSTEM_API virtual void EnsureBuildCompletion() override;
	// 取消全部 Pending/Running 任务（结果丢弃）
	NAVIGATIONSYSTEM_API virtual void CancelBuild() override;
	// Tick 入口：由 ARecastNavMesh::TickAsyncBuild 每帧调用
	NAVIGATIONSYSTEM_API virtual void TickAsyncBuild(float DeltaSeconds) override;
	// NavMeshBoundsVolume 变化时触发 Inclusion 重算
	NAVIGATIONSYSTEM_API virtual void OnNavigationBoundsChanged() override;

	/** Asks generator to update navigation affected by DirtyAreas */
	// 脏区 → Tile 坐标 → PendingDirtyTiles；节流由 NavigationSystem 控制
	NAVIGATIONSYSTEM_API virtual void RebuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas) override;

	/** determines whether this generator is performing navigation building actions at the moment, dirty areas are also checked */
	NAVIGATIONSYSTEM_API virtual bool IsBuildInProgressCheckDirty() const override;

#if !RECAST_ASYNC_REBUILDING
	/** returns true if we are time slicing and the data is valid to use false otherwise*/
	// 时间切片剩余量查询；只在同步时切片模式下有效
	NAVIGATIONSYSTEM_API virtual bool GetTimeSliceData(int32& OutNumRemainingBuildTasks, double& OutCurrentBuildTaskDuration) const override;
#endif

	// 剩余任务数 = Running + Pending + 正在做时间切片的那一个
	int32 GetNumRemaningBuildTasksHelper() const { return RunningDirtyTiles.Num() + PendingDirtyTiles.Num() + static_cast<int32>(SyncTimeSlicedData.TileGeneratorSync.IsValid()); }
	NAVIGATIONSYSTEM_API virtual int32 GetNumRemaningBuildTasks() const override;
	NAVIGATIONSYSTEM_API virtual int32 GetNumRunningBuildTasks() const override;

	/** Checks if a given tile is being build or has just finished building */
	NAVIGATIONSYSTEM_API bool IsTileChanged(const FNavTileRef InTileRef) const;
		
	inline uint32 GetVersion() const { return Version; } // 每次 RebuildAll 自增，用于作废旧任务

	const ARecastNavMesh* GetOwner() const { return DestNavMesh; }

	/** update area data */
	// 响应 AreaClass 增删：刷新 AdditionalCachedData 里的 Area/Flag 映射
	NAVIGATIONSYSTEM_API void OnAreaAdded(const UClass* AreaClass, int32 AreaID);
	NAVIGATIONSYSTEM_API void OnAreaRemoved(const UClass* AreaClass);
		
	//--- accessors --- //
	inline class UWorld* GetWorld() const { return DestNavMesh->GetWorld(); }

	const FRecastBuildConfig& GetConfig() const { return Config; }

	const FRecastNavMeshTileGenerationDebug& GetTileDebugSettings() const { return DestNavMesh->TileGenerationDebug; } 

	const TNavStatArray<FBox>& GetInclusionBounds() const { return InclusionBounds; }
	
	FVector GetRcNavMeshOrigin() const { return RcNavMeshOrigin; } // dtNavMesh 原点（Recast 坐标）

	/** checks if any on InclusionBounds encapsulates given box.
	 *	@return index to first item in InclusionBounds that meets expectations */
	// 查找完整包住 Box 的 InclusionBound；用于 DirtyArea 快速裁剪
	NAVIGATIONSYSTEM_API int32 FindInclusionBoundEncapsulatingBox(const FBox& Box) const;

	/** Total navigable area box, sum of all navigation volumes bounding boxes */
	FBox GetTotalBounds() const { return TotalNavBounds; }

	const FRecastNavMeshCachedData& GetAdditionalCachedData() const { return AdditionalCachedData; }

	// 判断有没有排队中 / 某区域内的脏 Tile
	NAVIGATIONSYSTEM_API bool HasDirtyTiles() const;
	NAVIGATIONSYSTEM_API bool HasDirtyTiles(const FBox& AreaBounds) const;
	NAVIGATIONSYSTEM_API int32 GetDirtyTilesCount(const FBox& AreaBounds) const;

	// 是否在游戏线程做几何收集（影响 GatherGeometry 时机）
	NAVIGATIONSYSTEM_API bool GatherGeometryOnGameThread() const;
	// 当前是否以时间切片方式进行（由 FNavRegenTimeSliceManager 启动）
	NAVIGATIONSYSTEM_API bool IsTimeSliceRegenActive() const;

	// 用 BBoxGrowthLow/High 膨胀 AABB；用于脏区扩展到相邻 Tile
	NAVIGATIONSYSTEM_API FBox GrowBoundingBox(const FBox& BBox, bool bIncludeAgentHeight) const;
	
	/** Returns if the provided Octree Element should generate geometry on the provided NavDataConfig. Can be used to extend the logic to decide what geometry is generated on what Navmesh */
	// 钩子：子类可覆盖来决定某元素是否参与本 NavMesh 构建
	NAVIGATIONSYSTEM_API virtual bool ShouldGenerateGeometryForOctreeElement(const FNavigationOctreeElement& Element, const FNavDataConfig& NavDataConfig) const;
	
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && ENABLE_VISUAL_LOG
	NAVIGATIONSYSTEM_API virtual void ExportNavigationData(const FString& FileName) const override;
	NAVIGATIONSYSTEM_API virtual void GrabDebugSnapshot(struct FVisualLogEntry* Snapshot, const FBox& BoundingBox, const FName& CategoryName, ELogVerbosity::Type Verbosity) const override;
#endif

	// 下面一系列 UE_DEPRECATED 的 Export* 5.5 版本已迁移到 FRecastGeometryExport；此处只保留兼容签名
	UE_DEPRECATED(5.5, "Use FRecastGeometryExport::ExportElementGeometry.")
	static NAVIGATIONSYSTEM_API void ExportNavRelevantObjectGeometry(INavRelevantInterface& InOutNavRelevantInterface, FNavigationRelevantData& OutData);
	UE_DEPRECATED(5.5, "Use FRecastGeometryExport::ExportVertexSoupGeometry.")
	static NAVIGATIONSYSTEM_API void ExportVertexSoupGeometry(const TArray<FVector>& InVerts, FNavigationRelevantData& OutData);

	UE_DEPRECATED(5.5, "Use FRecastGeometryExport::ExportRigidBodyGeometry.")
	static NAVIGATIONSYSTEM_API void ExportRigidBodyGeometry(UBodySetup& InOutBodySetup,
		TNavStatArray<FVector>& OutVertexBuffer,
		TNavStatArray<int32>& OutIndexBuffer,
		FBox& OutBounds,
		const FTransform& LocalToWorld = FTransform::Identity);

	UE_DEPRECATED(5.5, "Use FRecastGeometryExport::ExportRigidBodyGeometry.")
	static NAVIGATIONSYSTEM_API void ExportRigidBodyGeometry(
		UBodySetup& InOutBodySetup,
		TNavStatArray<FVector>& OutTriMeshVertexBuffer,
		TNavStatArray<int32>& OutTriMeshIndexBuffer,
		TNavStatArray<FVector>& OutConvexVertexBuffer,
		TNavStatArray<int32>& OutConvexIndexBuffer,
		TNavStatArray<int32>& OutShapeBuffer,
		FBox& OutBounds,
		const FTransform& LocalToWorld = FTransform::Identity);

	UE_DEPRECATED(5.5, "Use FRecastGeometryExport::ExportAggregatedGeometry.")
	static NAVIGATIONSYSTEM_API void ExportAggregatedGeometry(
		const FKAggregateGeom& AggGeom,
		TNavStatArray<FVector>& OutConvexVertexBuffer,
		TNavStatArray<int32>& OutConvexIndexBuffer,
		TNavStatArray<int32>& OutShapeBuffer,
		FBox& OutBounds,
		const FTransform& LocalToWorld = FTransform::Identity);

#if UE_ENABLE_DEBUG_DRAWING
	/** Converts data encoded in EncodedData.CollisionData to FNavDebugMeshData format */
	// 把 FNavigationRelevantData.CollisionData 解码成可视化 Mesh（NavMeshRenderingComponent 用）
	static NAVIGATIONSYSTEM_API void GetDebugGeometry(const FNavigationRelevantData& EncodedData, FNavDebugMeshData& DebugMeshData);
#endif  // UE_ENABLE_DEBUG_DRAWING

	const FNavRegenTimeSliceManager* GetTimeSliceManager() const { return SyncTimeSlicedData.TimeSliceManager; }
	
	void SetNextTimeSliceRegenActive(bool bRegenState) { SyncTimeSlicedData.bNextTimeSliceRegenActive = bRegenState; }

	/** Update the config according to the resolution */
	// 根据 TileResolution 覆写 Config 里的 cs/ch/walkableRadius 等
	NAVIGATIONSYSTEM_API virtual void SetupTileConfig(const ENavigationDataResolution TileResolution, FRecastBuildConfig& OutConfig) const;

protected:
	// Performs initial setup of member variables so that generator is ready to
	// do its thing from this point on. Called just after construction by ARecastNavMesh
	// 构造完毕后由 ARecastNavMesh 调用；分配 dtNavMesh、InclusionBounds、AdditionalCachedData
	NAVIGATIONSYSTEM_API virtual void Init();

	// Used to configure Config. Override to influence build properties
	// 组装 rcConfig + UE 扩展位；子类可覆盖自定义生成策略
	NAVIGATIONSYSTEM_API virtual void ConfigureBuildProperties(FRecastBuildConfig& OutConfig);

	// Updates cached list of navigation bounds
	// 扫描 NavigationSystem::RegisteredNavBounds 刷新 InclusionBounds / TotalNavBounds
	NAVIGATIONSYSTEM_API void UpdateNavigationBounds();
		
	// Sorts pending build tiles by proximity to player, so tiles closer to player will get generated first
	// 根据 SeedLocation 为每个 Pending Tile 打 SeedDistance 并排序
	NAVIGATIONSYSTEM_API virtual void SortPendingBuildTiles();

	// Get seed locations used for sorting pending build tiles. Tiles closer to these locations will be prioritized first.
	// 默认取 Invoker 位置 / PlayerController 位置；子类可覆盖
	NAVIGATIONSYSTEM_API virtual void GetSeedLocations(UWorld& World, TArray<FVector2D>& OutSeedLocations) const;

	/** Instantiates dtNavMesh and configures it for tiles generation. Returns false if failed */
	// 创建 dtNavMesh 并根据 Config 设定 Tile 参数 / Hash 位宽
	NAVIGATIONSYSTEM_API bool ConstructTiledNavMesh();

	/** Determine bit masks for poly address */
	// 根据 Tile 数量计算 poly/tile 在 polyRef 中各占多少 bit
	NAVIGATIONSYSTEM_API void CalcNavMeshProperties(int32& MaxTiles, int32& MaxPolys);
	
	/** Marks grid tiles affected by specified areas as dirty */
	// 脏区域 → FRcTileBox → PendingDirtyTiles；是 RebuildDirtyAreas 的实际工作函数
	NAVIGATIONSYSTEM_API virtual void MarkDirtyTiles(const TArray<FNavigationDirtyArea>& DirtyAreas);

	UE_DEPRECATED(5.5, "Use ShouldDirtyTilesRequestedByElement with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API virtual bool ShouldDirtyTilesRequestedByObject(
		const UNavigationSystemV1& NavSys,
		const FNavigationOctree& NavOctreeInstance,
		const UObject& SourceObject,
		const FNavDataConfig& NavDataConfig) const final;

	/** Returns if the provided FNavigationElement that requested a navmesh dirtying should dirty this Navmesh. Useful to avoid tiles regeneration from elements that are excluded from the provided NavDataConfig */
	// 钩子：过滤掉"不属于本 Agent 的元素"引起的脏事件
	NAVIGATIONSYSTEM_API virtual bool ShouldDirtyTilesRequestedByElement(
		const UNavigationSystemV1& NavSys,
		const FNavigationOctree& NavOctreeInstance,
		FNavigationElementHandle SourceElement,
		const FNavDataConfig& NavDataConfig) const;

	/** Marks all tiles overlapping with InclusionBounds dirty (via MarkDirtyTiles). */
	NAVIGATIONSYSTEM_API bool MarkNavBoundsDirty();

	// 主线程回补：把 Tile 对应的所有 Layer 从 dtNavMesh 里移除
	NAVIGATIONSYSTEM_API void RemoveLayers(const FIntPoint& Tile, TArray<FNavTileRef>& UpdatedTiles);
	
	// 把 TileGenerator.CompressedLayers 拷贝进 ARecastNavMesh 的 TileCache，便于增量修改
	NAVIGATIONSYSTEM_API void StoreCompressedTileCacheLayers(const FRecastTileGenerator& TileGenerator, int32 TileX, int32 TileY);

#if RECAST_INTERNAL_DEBUG_DATA
	NAVIGATIONSYSTEM_API void StoreDebugData(const FRecastTileGenerator& TileGenerator, int32 TileX, int32 TileY);
#endif

#if RECAST_ASYNC_REBUILDING
	/** Processes pending tile generation tasks Async*/
	// 从 PendingDirtyTiles 取 NumTasksToProcess 个派发成 FAsyncTask；回收已完成的任务
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> ProcessTileTasksAsyncAndGetUpdatedTiles(const int32 NumTasksToProcess);
#else
	// 非异步：构造一个 TileGenerator（从 Pending 队列取出或强制指定下标）
	NAVIGATIONSYSTEM_API TSharedRef<FRecastTileGenerator> CreateTileGeneratorFromPendingElement(FIntPoint &OutTileLocation, const int32 ForcedPendingTileIdx = INDEX_NONE);

	/** Processes pending tile generation tasks Sync with option for time slicing currently an experimental feature. */
	// 实验性：在主线程按预算切片运行单个 TileGenerator，结束后马上 AddGeneratedTiles
	NAVIGATIONSYSTEM_API virtual TArray<FNavTileRef> ProcessTileTasksSyncTimeSlicedAndGetUpdatedTiles();
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> ProcessTileTasksSyncAndGetUpdatedTiles(const int32 NumTasksToProcess);

	// 选下一个要构建的 Tile 下标（子类可改变优先级规则）
	NAVIGATIONSYSTEM_API virtual int32 GetNextPendingDirtyTileToBuild() const;
#endif
	/** Processes pending tile generation tasks */
	// 统一入口：按编译宏选择 Async / Sync / SyncTimeSliced
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> ProcessTileTasksAndGetUpdatedTiles(const int32 NumTasksToProcess);

	// 取消当前正在做时间切片的那个 Tile，重置状态
	NAVIGATIONSYSTEM_API void ResetTimeSlicedTileGeneratorSync();

public:
	/** Adds generated tiles to NavMesh, replacing old ones, uses time slicing returns Failed if any layer failed */
	// 切片版：一层一层地把 TileGenerator 产物塞回 dtNavMesh
	NAVIGATIONSYSTEM_API ETimeSliceWorkResult AddGeneratedTilesTimeSliced(FRecastTileGenerator& TileGenerator, TArray<FNavTileRef>& OutResultTileRefs);

	/** Adds generated tiles to NavMesh, replacing old ones */
	// 一次性版：把 TileGenerator.NavigationData 全部提交，返回影响到的 FNavTileRef
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> AddGeneratedTilesAndGetUpdatedTiles(FRecastTileGenerator& TileGenerator);

public:
	/** Removes all tiles at specified grid location and returns the updated FNavTileRef */
	// 拆除指定 (X,Y) 坐标所有层，同时可返回旧 Layer→polyRef 映射便于关联（例如做 Link 重定向）
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> RemoveTileLayersAndGetUpdatedTiles(const int32 TileX, const int32 TileY, TMap<int32, dtPolyRef>* OldLayerTileIdMap = nullptr);

	/** Removes all tiles at specified grid location */
	NAVIGATIONSYSTEM_API void RemoveTileLayers(dtNavMesh* DetourMesh, const int32 TileX, const int32 TileY);

	// 批量移除一组 Tile（Active Tile 离开 Invoker 范围时用）
	NAVIGATIONSYSTEM_API void RemoveTiles(const TArray<FIntPoint>& Tiles);

	// Invoker 重新到达时：把之前删掉的 Tile 重新请求回来
	NAVIGATIONSYSTEM_API void ReAddTiles(const TArray<FNavMeshDirtyTileElement>& Tiles);

	bool IsBuildingRestrictedToActiveTiles() const { return bRestrictBuildingToActiveTiles; }
	NAVIGATIONSYSTEM_API bool IsInActiveSet(const FIntPoint& Tile) const;

	/** sets a limit to number of asynchronous tile generators running at one time
	 *	@note if used at runtime will not result in killing tasks above limit count
	 *	@mote function does not validate the input parameter - it's on caller */
	void SetMaxTileGeneratorTasks(int32 NewLimit) { MaxTileGeneratorTasks = NewLimit; }

	void SetSortPendingTileMethod(const ENavigationSortPendingTilesMethod InMethod) { SortPendingTilesMethod = InMethod; }

	// 根据 Config 里 dtNavMeshParams 计算 polyRef 的 Tile/Poly 位宽（参考 dtNavMeshParams::maxTiles/maxPolys）
	static NAVIGATIONSYSTEM_API void CalcPolyRefBits(ARecastNavMesh* NavMeshOwner, int32& MaxTileBits, int32& MaxPolyBits);

	/** Returns true if bGenerateNavLinks is enabled and bAllowLinkGeneration is true. */ 
	bool IsGeneratingLinks() const;

protected:
	/** Resolve area class from link generation config into recast areaIds and flags.
	 * Must be called after AdditionalCachedData is constructed. */
	// 把 LinkGenerationConfig 中的 AreaClass 解析成 Recast AreaID 和 Flag，存入 Config.JumpConfigs
	void ResolveGeneratedLinkAreas(FRecastBuildConfig& OutConfig);
	
	// 切换"仅生成 Invoker 周围"模式；会重算 ActiveTileSet
	NAVIGATIONSYSTEM_API virtual void RestrictBuildingToActiveTiles(bool InRestrictBuildingToActiveTiles);
	
	/** Blocks until build for specified list of tiles is complete and discard results */
	NAVIGATIONSYSTEM_API void DiscardCurrentBuildingTasks();

	// 工厂方法：子类可重写以返回自定义 TileGenerator 派生类
	NAVIGATIONSYSTEM_API virtual TSharedRef<FRecastTileGenerator> CreateTileGenerator(const FIntPoint& Coord, const TArray<FBox>& DirtyAreas, const double PendingTileCreationTime = 0.);

	// 模板版本，允许在子类里 new 具体派生类型
	template <typename T>
	TSharedRef<T> ConstructTileGeneratorImpl(const FIntPoint& Coord, const TArray<FBox>& DirtyAreas, const double PendingTileCreationTime)
	{
		TSharedRef<T> TileGenerator = MakeShareable(new T(*this, Coord, PendingTileCreationTime));
		TileGenerator->Setup(*this, DirtyAreas);
		return TileGenerator;
	}

	UE_DEPRECATED(5.5, "Use BBoxGrowthLow and BBoxGrowthHigh properties instead.")
	void SetBBoxGrowth(const FVector& InBBox)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		BBoxGrowth = InBBox;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// 用 BBoxGrowthLow/High 膨胀脏区，使其覆盖 BorderSize 内的邻 Tile
	FBox GrowDirtyBounds(const FBox& BBox, bool bIncludeAgentHeight) const;

	//----------------------------------------------------------------------//
	// debug
	//----------------------------------------------------------------------//
	NAVIGATIONSYSTEM_API virtual uint32 LogMemUsed() const override;

	// 判断目标 Tile 是否允许被添加 Layer（考虑 ActiveTileSet 过滤）
	NAVIGATIONSYSTEM_API bool IsAllowedToAddTileLayers(const FIntPoint Tile) const;
	// 添加一个 Layer：dtNavMesh::addTile + 更新 OldLayerTileIdMap + 记录 OutResultTileRefs
	NAVIGATIONSYSTEM_API void AddGeneratedTileLayer(int32 LayerIndex, FRecastTileGenerator& TileGenerator, const TMap<int32, dtPolyRef>& OldLayerTileIdMap, TArray<FNavTileRef>& OutResultTileRefs);

#if !UE_BUILD_SHIPPING
	/** Data struct used by 'LogDirtyAreas' that contains all the information regarding the areas that are being dirtied, per dirtied tile. */
	struct FNavigationDirtyAreaPerTileDebugInformation
	{
		FNavigationDirtyArea DirtyArea;
		bool bTileWasAlreadyAdded = false;
	};
	
	/** Used internally, when LogNavigationDirtyArea is VeryVerbose, to log the number of tiles a dirty area is requesting. */
	// 脏区 → Tile 详细调试日志：显示每个脏区 Mark 了几个 Tile
	NAVIGATIONSYSTEM_API void LogDirtyAreas(const UObject& OwnerNav,
		const TMap<FPendingTileElement, TArray<FNavigationDirtyAreaPerTileDebugInformation>>& DirtyAreasDebuggingInformation) const;

#endif
	
protected:
	friend ARecastNavMesh;

	/** Parameters defining navmesh tiles */
	FRecastBuildConfig Config; // 全局 Config 快照；每个 TileGenerator 会各自在 Setup 里再覆写 TileResolution

	/** Used to grow generic element bounds to match this generator's properties
	 *	(most notably Config.borderSize) */
	UE_DEPRECATED(5.5, "Use BBoxGrowthLow and BBoxGrowthHigh instead.")
	FVector BBoxGrowth;
	
	/** Growth in the negative axis direction. 
	 * Used to grow generic element bounds to match this generator's properties (most notably Config.borderSize) */
	FVector BBoxGrowthLow;  // 向负方向的膨胀（一般=BorderSize*cs）

	/** Growth in the positive axis direction. 
	 * Used to grow generic element bounds to match this generator's properties (most notably Config.borderSize) */
	FVector BBoxGrowthHigh; // 向正方向的膨胀（可能比 Low 多 AgentHeight）
	
	int32 NumActiveTiles; // 当前 dtNavMesh 中活跃 Tile 数量（仅用于统计）
	/** the limit to number of asynchronous tile generators running at one time,
	 *  this is also used by the sync non time sliced regeneration ProcessTileTasksSync,
	 *  but not by ProcessTileTasksSyncTimeSliced.
	 */
	int32 MaxTileGeneratorTasks; // 同时运行的 Tile 任务上限（源自 MaxSimultaneousTileGenerationJobsCount）

	FVector::FReal AvgLayersPerTile; // 平均每 Tile 层数；用于预估内存

	/** Total bounding box that includes all volumes, in unreal units. */
	FBox TotalNavBounds;

	/** Bounding geometry definition. */
	TNavStatArray<FBox> InclusionBounds; // 所有 ANavMeshBoundsVolume 的 AABB

	/** Navigation mesh that owns this generator */
	ARecastNavMesh*	DestNavMesh; // 反向指针（裸指针但永远由 Actor 持有）
	
	/** List of dirty tiles that needs to be regenerated */
	// 待构建队列；MarkDirtyTiles 写入、ProcessTileTasks 消费
	TNavStatArray<FPendingTileElement> PendingDirtyTiles;
	
	/** List of dirty tiles currently being regenerated */
	// 正在运行的任务；DoWork 结束后主线程回收 NavigationData 再 AddGeneratedTiles
	TNavStatArray<TRunningTileElement<FRecastTileGeneratorWrapper>> RunningDirtyTiles;

#if WITH_NAVMESH_SEGMENT_LINKS
	/** List of tiles that have been generated but have not completed segment link connections yet */
	// Segment Link 是跨 Tile 的，需要在所有邻居 Tile 也完成后再连线
	TSet<FNavTileRef> TilesPendingSegmentLinkConnections;
#endif // WITH_NAVMESH_SEGMENT_LINKS

#if WITH_EDITOR
	/** List of tiles that were recently regenerated */
	// 编辑器红色高亮"刚构建"的 Tile 用
	TNavStatArray<FTileTimestamp> RecentlyBuiltTiles;
#endif// WITH_EDITOR
	
	// Invoker 模式下的活跃 Tile 集合；UpdateActiveTiles 维护
	TSet<FIntPoint> ActiveTileSet;

	/** */
	FRecastNavMeshCachedData AdditionalCachedData; // Area/Flag 缓存，TileGenerator Setup 时读它

	/** Use this if you don't want your tiles to start at (0,0,0) */
	FVector RcNavMeshOrigin; // Recast 空间的原点（对齐到 CellSize）

	double RebuildAllStartTime = 0; // RebuildAll 开始时间戳（用于性能 Log）
	
	uint32 bInitialized:1;              // Init() 完成标志
	uint32 bRestrictBuildingToActiveTiles:1; // 是否仅构建 ActiveTileSet

	/** Runtime generator's version, increased every time all tile generators get invalidated
	 *	like when navmesh size changes */
	uint32 Version; // 每次 RebuildAll / NavMesh 尺寸变化 ++，所有 running task 会被 discard

	// 排序策略：SeedLocations / Invokers
	ENavigationSortPendingTilesMethod SortPendingTilesMethod = ENavigationSortPendingTilesMethod::SortWithSeedLocations;

	/** Grouping all the member variables used by the time slicing code together for neatness */
	// -------------------------------------------------------------------------------
	// FSyncTimeSlicedData —— 同步时间切片模式专用数据（ALLOW_TIME_SLICE_NAV_REGEN）
	// 非异步重建时，Generator 只跑一个 TileGeneratorSync；每帧运行到预算耗尽就保存状态
	// -------------------------------------------------------------------------------
	struct FSyncTimeSlicedData
	{
		FSyncTimeSlicedData();

		/** Accumulated time spent processing the tile (in seconds). */
		double CurrentTileRegenDuration; // 当前 Tile 已累计耗时

#if !UE_BUILD_SHIPPING		
		/** Frame when the tile start being processed. */
		int64 TileRegenStartFrame = 0;   // 开始处理 Tile 的帧号（调试）

		/** Frame when the tile is done being processed. */
		int64 TileRegenEndFrame = 0;     // 完成 Tile 的帧号
#endif // !UE_BUILD_SHIPPING		
		
		/** if we are currently using time sliced regen or not - currently an experimental feature.
		 *  do not manipulate this value directly instead call SetNextTimeSliceRegenActive()
		 */
		bool bTimeSliceRegenActive;     // 当前是否处于时间切片模式
		bool bNextTimeSliceRegenActive; // 下一帧想切换到的状态（避免中途切换）
		
		/** Used by ProcessTileTasksSyncTimeSliced */
		EProcessTileTasksSyncTimeSlicedState ProcessTileTasksSyncState;

		/** Used by ProcessTileTasksSyncTimeSliced */
		TArray<FNavTileRef> UpdatedTilesCache; // 跨帧累积的受影响 Tile 列表

		/** Used by AddGeneratedTilesTimeSliced */
		TArray<FNavTileRef> ResultTileRefsCached;

		/** Used by AddGeneratedTilesTimeSliced */
		TMap<int32, dtPolyRef> OldLayerTileIdMapCached;

		/** Used by AddGeneratedTilesTimeSliced */
		EAddGeneratedTilesTimeSlicedState AddGeneratedTilesState;

		/** Used by AddGeneratedTilesTimeSliced */
		int32 AddGenTilesLayerIndex; // 下一个要添加的 Layer 下标

		/** Do not null or Reset this directly instead call ResetTimeSlicedTileGeneratorSync(). The only exception currently is in ProcessTileTasksSyncTimeSliced */
		TSharedPtr<FRecastTileGenerator> TileGeneratorSync; // 当前正在被切片运行的 TileGenerator
		FNavRegenTimeSliceManager* TimeSliceManager;        // 由 NavSystem 提供的预算管理器
	};

	FSyncTimeSlicedData SyncTimeSlicedData;
};

#endif // WITH_RECAST
