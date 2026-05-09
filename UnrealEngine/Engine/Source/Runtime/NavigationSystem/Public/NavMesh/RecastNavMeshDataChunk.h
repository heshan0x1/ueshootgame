// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ============================================================================
// 文件概览：RecastNavMeshDataChunk.h
// ----------------------------------------------------------------------------
// URecastNavMeshDataChunk 是 UNavigationDataChunk 针对 Recast NavMesh 的派生；
// 用于"关卡流式加载 / World Partition 动态模式"下的 Tile 数据搬运：
//
//   1) 烘焙阶段：每个子关卡/Data Layer 生成时，属于该关卡的 Tile 会被打包成
//      一个 URecastNavMeshDataChunk（随 Level 一起序列化到磁盘）。
//   2) 运行时加载子关卡 → AttachTiles() 把 RawData 装进 ARecastNavMesh 的 dtNavMesh。
//   3) 卸载子关卡 → DetachTiles() 把 Tile 从 dtNavMesh 拿回本 Chunk 存着。
//   4) MoveTiles() 支持对 Tile 在 XY 平面平移 / 旋转（实验，用于可拼接关卡）。
//
// FRecastTileData 是单个 Tile 的装箱结构，内部通过 TSharedPtr<FRawData> 共享
// 裸字节缓冲，避免多处拷贝；FRawData::~FRawData 负责最终 dtFree 回收。
// ============================================================================

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "AI/Navigation/NavigationTypes.h"
#endif
#include "AI/Navigation/NavigationDataChunk.h"
#include "NavMesh/RecastNavMesh.h"
#include "RecastNavMeshDataChunk.generated.h"

class FPImplRecastNavMesh;

// 单个 Tile 的装箱数据。
// 同一 Tile 可能在多个 Chunk 间共享（World Partition 场景），因此 Raw 缓冲用共享指针管理。
struct FRecastTileData
{
	// Tile 字节缓冲的引用计数持有者；
	// 析构时会做 dtFree（或 FMemory::Free，具体见 .cpp 实现）释放原始内存。
	struct FRawData
	{
		FRawData(uint8* InData);
		~FRawData();

		uint8* RawData;     // Recast 原始 Tile 字节流（dtMeshHeader + 顶点 + poly + link + detail + BV tree ...）
	};

	FRecastTileData();
	FRecastTileData(int32 TileDataSize, uint8* TileRawData, int32 TileCacheDataSize, uint8* TileCacheRawData);
	
	// Location of attached tile
	int32					OriginalX;	// Tile X coordinates when gathered —— 采集时的原始 Tile X；MoveTiles 后 X 会改而 OriginalX 不变，用作同源跟踪
	int32					OriginalY;	// Tile Y coordinates when gathered —— 同上，原始 Tile Y
	int32					X;					// 当前 Tile 坐标 X（可能被 MoveTiles 平移/旋转后修改）
	int32					Y;					// 当前 Tile 坐标 Y
	int32					Layer;              // Tile 层号；Recast 垂直叠放时每层一个 dtMeshTile
		
	// Tile data
	int32					TileDataSize;       // RawData 字节数
	TSharedPtr<FRawData>	TileRawData;        // 共享持有的 Tile 字节流

	// Compressed tile cache layer 
	int32					TileCacheDataSize;  // 压缩后的 TileCache 字节数（动态障碍使用）
	TSharedPtr<FRawData>	TileCacheRawData;   // TileCache 压缩层（用于 Obstacle 运行时重建）

	// Whether this tile is attached to NavMesh
	bool					bAttached;	        // 当前 Tile 是否已 AttachTo dtNavMesh（false 说明仅持有数据但未挂载）
};

class dtNavMesh;
class FPImplRecastNavMesh;

// GetTiles 的拷贝模式位掩码
enum EGatherTilesCopyMode
{
	NoCopy = 0,                                 // 只记录 Tile 引用，不复制数据
	CopyData = 1 << 0,                          // 复制主 Tile 数据
	CopyCacheData = 1 << 1,                     // 复制 TileCache 压缩层
	CopyDataAndCacheData = (CopyData | CopyCacheData)
};

/** 
 * 
 */
// 用于关卡流送 / 分块导航数据序列化的 UObject 容器。
// 一个 URecastNavMeshDataChunk 实例对应"同一 NavMesh 里属于某关卡的一批 Tile"。
UCLASS(MinimalAPI)
class URecastNavMeshDataChunk : public UNavigationDataChunk
{
	GENERATED_UCLASS_BODY()

	//~ Begin UObject Interface
	// 序列化入口；内部转发到 SerializeRecastData 并做版本兼容
	NAVIGATIONSYSTEM_API virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

#if WITH_RECAST
	/** Attaches tiles to specified navmesh, transferring tile ownership to navmesh */
	// 把所有 Tile 挂到 NavMesh，数据所有权转移（本 Chunk 不再持有）；返回已附着 Tile 的 64bit 引用
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> AttachTiles(ARecastNavMesh& NavMesh);

	/** Attaches tiles to specified navmesh */
	// 可选保留主数据/缓存数据的副本在本 Chunk（便于后续 Detach 再 Attach 时免序列化）
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> AttachTiles(ARecastNavMesh& NavMesh, const bool bKeepCopyOfData, const bool bKeepCopyOfCacheData);

	/** Detaches tiles from specified navmesh, taking tile ownership */
	// 把 Tile 从 NavMesh 拿回本 Chunk；默认夺取数据所有权
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> DetachTiles(ARecastNavMesh& NavMesh);

	/** Detaches tiles from specified navmesh */
	// 细粒度版本：分别决定是否夺取主数据/缓存数据的所有权
	NAVIGATIONSYSTEM_API TArray<FNavTileRef> DetachTiles(ARecastNavMesh& NavMesh, const bool bTakeDataOwnership, const bool bTakeCacheDataOwnership);
#endif // WITH_RECAST

	/** 
	 * Experimental: Moves tiles data on the xy plane by the offset (in tile coordinates) and rotation (in degree).
	 * @param NavMeshImpl		Recast navmesh implementation.
	 * @param Offset			Offset in tile coordinates.
	 * @param RotationDeg		Rotation in degrees.
	 * @param RotationCenter	World position
	 */
	// 实验性：对 Chunk 内所有 Tile 做平面平移+绕 RotationCenter 旋转。
	// 用途：可拼接/可旋转关卡（例如程序化生成的房间模板），让烘焙后的 Tile 数据可复用到不同位姿。
	// 内部会修改每个 Tile 的 Vertex/BV/Link 坐标并重写入 RawData。
	NAVIGATIONSYSTEM_API void MoveTiles(FPImplRecastNavMesh& NavMeshImpl, const FIntPoint& Offset, const FVector::FReal RotationDeg, const FVector2D& RotationCenter);
	
	/** Number of tiles in this chunk */
	NAVIGATIONSYSTEM_API int32 GetNumTiles() const;

	/** Const accessor to the list of tiles in the data chunk. */
	const TArray<FRecastTileData>& GetTiles() const { return Tiles; }

	/** Returns the AABB for the given tiles. */
	// 5.5 弃用：老版本用 int32 TileIndex；请改用 FNavTileRef 64 位版本
	UE_DEPRECATED(5.5, "Use the version of this function that takes an array of FNavTileRefs instead.")
	NAVIGATIONSYSTEM_API void GetTilesBounds(const FPImplRecastNavMesh& NavMeshImpl, const TArray<int32>& TileIndices, FBox& OutBounds) const;

#if WITH_RECAST
	/** Returns the AABB for the given tiles. */
	// 计算指定 Tile 集合在世界空间的 AABB（UE 坐标），供渲染/LOD 系统使用
	NAVIGATIONSYSTEM_API void GetTilesBounds(const FPImplRecastNavMesh& NavMeshImpl, const TArray<FNavTileRef>& TileRefs, FBox& OutBounds) const;
#endif // WITH_RECAST

	/** Mutable accessor to the list of tiles in the data chunk. */
	TArray<FRecastTileData>& GetMutableTiles() { return Tiles; }

	/** Releases all tiles that this chunk holds */
	// 清空本 Chunk 持有的 Tile；若 RawData 仍有其它共享引用则只减引用计数，否则释放裸内存
	NAVIGATIONSYSTEM_API void ReleaseTiles();

	/** Collect tiles with data and/or cache data from the provided TileIndices. */
	UE_DEPRECATED(5.5, "Use the version of this function that takes an array of FNavTileRefs instead.")
	NAVIGATIONSYSTEM_API void GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<int32>& TileIndices, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached = true);

#if WITH_RECAST
	/** Collect tiles with data and/or cache data from the provided tile references. */
	// 从 NavMeshImpl 中按 TileRefs 收集（可选拷贝）Tile 数据到本 Chunk；bMarkAsAttached 指示是否视为"已挂载"
	NAVIGATIONSYSTEM_API void GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<FNavTileRef>& TileRefs, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached = true);
#endif // WITH_RECAST

private:
#if WITH_RECAST
	// 实际的二进制 Tile 数据序列化；NavMeshVersion 决定字段布局（见 RecastVersion.h 里的 NAVMESHVER_*）
	NAVIGATIONSYSTEM_API void SerializeRecastData(FArchive& Ar, int32 NavMeshVersion);
#endif//WITH_RECAST

private:
	// 本 Chunk 持有的所有 Tile
	TArray<FRecastTileData> Tiles;
};
