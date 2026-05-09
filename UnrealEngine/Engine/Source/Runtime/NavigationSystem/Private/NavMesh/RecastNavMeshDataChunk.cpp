// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// 文件概览：RecastNavMeshDataChunk.cpp
// ----------------------------------------------------------------------------
// URecastNavMeshDataChunk 的实现：关卡流式加载 / World Partition 动态模式下
// 的 Tile 数据装卸核心。主要职责：
//   - Serialize / SerializeRecastData：磁盘↔内存的 Tile 字节流；
//     通过版本号 NAVMESHVER_* 做前后兼容（详见 RecastVersion.h）。
//   - AttachTiles：把本 Chunk 的 Tile 二进制插入 dtNavMesh（可选保留副本）。
//     若是"Active Tiles 生成模式"，同时把 (X,Y) 记入活动 Tile 集合。
//   - DetachTiles：把已挂载的 Tile 从 dtNavMesh 摘下，按参数决定是否夺取所有权。
//   - GetTiles / GetTilesBounds：从 dtNavMesh 采集 Tile 数据到本 Chunk。
//   - MoveTiles：实验性，XY 平移 + 旋转整批 Tile 的字节流（不重建）。
//
// 引用计数：TileRawData 的裸字节通过 FRawData(TSharedPtr) 包装，
// Attach 时根据 bKeepCopyOfData 决定"把所有权交给 dtNavMesh（不持有副本）"
// 还是"保留一份副本以便 Detach 后可以重新 Attach"。
// ============================================================================

#include "NavMesh/RecastNavMeshDataChunk.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/PImplRecastNavMesh.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastVersion.h"
#include "NavMesh/RecastNavMeshGenerator.h"

#if WITH_RECAST
#include "Detour/DetourNavMeshBuilder.h"
#endif // WITH_RECAST

#include UE_INLINE_GENERATED_CPP_BY_NAME(RecastNavMeshDataChunk)

//----------------------------------------------------------------------//
// FRecastTileData                                                                
//----------------------------------------------------------------------//
// FRawData 构造仅保存裸指针；不复制数据（因此两侧必须约定所有权）
FRecastTileData::FRawData::FRawData(uint8* InData)
	: RawData(InData)
{
}

// FRawData 析构时负责最终释放：
//   - WITH_RECAST：用 dtFree 释放（因为分配时走 dtAlloc）
//   - 否则：用 FMemory::Free
FRecastTileData::FRawData::~FRawData()
{
#if WITH_RECAST
	dtFree(RawData, DT_ALLOC_PERM_TILE_DATA);
#else
	FMemory::Free(RawData);
#endif
}

FRecastTileData::FRecastTileData()
	: OriginalX(0)
	, OriginalY(0)
	, X(0)
	, Y(0)
	, Layer(0)
	, TileDataSize(0)
	, TileCacheDataSize(0)
	, bAttached(false)
{
}

// 直接以已有字节缓冲构造：TSharedPtr 引用计数化，析构时自动释放
FRecastTileData::FRecastTileData(int32 DataSize, uint8* RawData, int32 CacheDataSize, uint8* CacheRawData)
	: OriginalX(0)
	, OriginalY(0)
	, X(0)
	, Y(0)
	, Layer(0)
	, TileDataSize(DataSize)
	, TileCacheDataSize(CacheDataSize)
	, bAttached(false)
{
	TileRawData = MakeShareable(new FRawData(RawData));
	TileCacheRawData = MakeShareable(new FRawData(CacheRawData));
}

// Helper to duplicate recast raw data
// 复制一段 Recast Tile 原始字节；保持 dtAlloc/malloc 的一致性，避免跨分配器释放
static uint8* DuplicateRecastRawData(const uint8* Src, int32 SrcSize)
{
#if WITH_RECAST	
	uint8* DupData = (uint8*)dtAlloc(SrcSize, DT_ALLOC_PERM_TILE_DATA);
#else
	uint8* DupData = (uint8*)FMemory::Malloc(SrcSize);
#endif
	FMemory::Memcpy(DupData, Src, SrcSize);
	return DupData;
}

namespace UE::NavMesh::Private
{
	// 判断本 NavMesh 是否工作在"Active Tiles 生成"模式（只在 Invoker 附近保留 Tile）。
	// Attach/Detach 时需要据此同步 ARecastNavMesh::ActiveTileSet。
	bool IsUsingActiveTileGeneration(const ARecastNavMesh& NavMesh)
	{
#if WITH_RECAST
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMesh.GetWorld());
		if (NavSys)
		{
			return NavMesh.IsUsingActiveTilesGeneration(*NavSys);
		}
#endif // WITH_RECAST
		return false;
	}
} // namespace UE::NavMesh::Private

//----------------------------------------------------------------------//
// URecastNavMeshDataChunk                                                                
//----------------------------------------------------------------------//
URecastNavMeshDataChunk::URecastNavMeshDataChunk(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 磁盘 ↔ 内存序列化入口。
// 版本控制策略：
//   1) 先写/读 NavMeshVersion（NAVMESHVER_LATEST）
//   2) 再写/读 RecastNavMeshSizeBytes（Saving 时先写 0 占位，最后回填真实大小）
//   3) 若 Loading 检测到版本不在 [NAVMESHVER_MIN_COMPATIBLE, NAVMESHVER_LATEST]
//      → 直接 Seek 跳过这段数据，打 Warning 让用户重建 NavMesh
// 这样可以实现"旧存档能读（忽略不兼容数据），新存档不会被旧代码误读"。
void URecastNavMeshDataChunk::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NavMeshVersion = NAVMESHVER_LATEST;
	Ar << NavMeshVersion;

	// when writing, write a zero here for now.  will come back and fill it in later.
	// 保存时先占 8 字节，等数据写完再回填；便于未来跳过整段数据
	int64 RecastNavMeshSizeBytes = 0;
	int64 RecastNavMeshSizePos = Ar.Tell();
	Ar << RecastNavMeshSizeBytes;

	if (Ar.IsLoading())
	{
		// 版本不兼容时：Seek 跳过整段 NavMesh 数据，继续读取 UObject 后续内容
		auto CleanUpBadVersion = [&Ar, RecastNavMeshSizePos, RecastNavMeshSizeBytes]()
		{
			// incompatible, just skip over this data. Navmesh needs rebuilt.
			Ar.Seek(RecastNavMeshSizePos + RecastNavMeshSizeBytes);
		};

		if (NavMeshVersion < NAVMESHVER_MIN_COMPATIBLE)
		{
			// 版本太旧：老数据无法解析，提示用户重建
			UE_LOG(LogNavigation, Warning, TEXT("%s: URecastNavMeshDataChunk: Nav mesh version %d < Min compatible %d. Nav mesh needs to be rebuilt. \n"), *GetFullName(), NavMeshVersion, NAVMESHVER_MIN_COMPATIBLE);

			CleanUpBadVersion();
		}
		else if (NavMeshVersion > NAVMESHVER_LATEST)
		{
			// 版本来自未来：旧代码读新数据不安全，同样跳过并告警
			UE_LOG(LogNavigation, Warning, TEXT("%s: URecastNavMeshDataChunk: Nav mesh version %d > NAVMESHVER_LATEST %d. Newer nav mesh should not be loaded by older versioned code. At a minimum the nav mesh needs to be rebuilt. \n"), *GetFullName(), NavMeshVersion, NAVMESHVER_LATEST);

			CleanUpBadVersion();
		}
#if WITH_RECAST
		else if (RecastNavMeshSizeBytes > 4)
		{
			// 合法：真正走 Recast 数据反序列化
			SerializeRecastData(Ar, NavMeshVersion);
		}
#endif// WITH_RECAST
		else
		{
			// empty, just skip over this data
			// 空数据段：直接跳过
			Ar.Seek(RecastNavMeshSizePos + RecastNavMeshSizeBytes);
		}
	}
	else if (Ar.IsSaving())
	{
#if WITH_RECAST
		// 保存实际 Recast 数据
		SerializeRecastData(Ar, NavMeshVersion);
#endif// WITH_RECAST

		// 回填占位的 Size 字段
		int64 CurPos = Ar.Tell();
		RecastNavMeshSizeBytes = CurPos - RecastNavMeshSizePos;
		Ar.Seek(RecastNavMeshSizePos);
		Ar << RecastNavMeshSizeBytes;
		Ar.Seek(CurPos);
	}
}

#if WITH_RECAST
// 真正把 Tile 的 Recast 二进制写入/读取的细节。
// 单 Tile 的字节布局交给 FPImplRecastNavMesh::SerializeRecastMeshTile /
// SerializeCompressedTileCacheData 完成，这里只负责 Tile 数组的管理。
void URecastNavMeshDataChunk::SerializeRecastData(FArchive& Ar, int32 NavMeshVersion)
{
	int32 TileNum = Tiles.Num();
	Ar << TileNum;

	if (Ar.IsLoading())
	{
		Tiles.Empty(TileNum);
		// 迭代目标：逐 Tile 读取 {大小, RawData, CacheData} 并构造 FRecastTileData
		for (int32 TileIdx = 0; TileIdx < TileNum; TileIdx++)
		{
			int32 TileDataSize = 0;
			Ar << TileDataSize;

			// Load tile data 
			// 内部会 dtAlloc 分配 TileRawData；失败则返回 nullptr
			uint8* TileRawData = nullptr;
			FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, NavMeshVersion, TileRawData, TileDataSize); //allocates TileRawData on load
			
			if (TileRawData != nullptr)
			{
				// Load compressed tile cache layer
				// 紧随主数据之后的 TileCache 层（用于动态障碍/重建）
				int32 TileCacheDataSize = 0;
				uint8* TileCacheRawData = nullptr;
				FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, NavMeshVersion, TileCacheRawData, TileCacheDataSize); //allocates TileCacheRawData on load
				
				// We are owner of tile raw data
				FRecastTileData TileData(TileDataSize, TileRawData, TileCacheDataSize, TileCacheRawData);
				Tiles.Add(TileData);
			}
		}
	}
	else if (Ar.IsSaving())
	{
		// 迭代目标：把每个有效 Tile 的两段字节流写入磁盘
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.TileRawData.IsValid())
			{
				// Save tile itself
				Ar << TileData.TileDataSize;
				FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, NavMeshVersion, TileData.TileRawData->RawData, TileData.TileDataSize);
				// Save compressed tile cache layer
				FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, NavMeshVersion, TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize);
			}
		}
	}
}
#endif// WITH_RECAST

#if WITH_RECAST
// 简化 Attach：依据"是否 Game World"自动选择是否保留一份副本。
// Editor 中需要保留（PIE / Rebuild 时还要再 Attach），Game 中不保留可省内存。
TArray<FNavTileRef> URecastNavMeshDataChunk::AttachTiles(ARecastNavMesh& NavMesh)
{
	check(NavMesh.GetWorld());
	const bool bIsGameWorld = NavMesh.GetWorld()->IsGameWorld();

	// In editor we still need to own the data so a copy will be made.
	const bool bKeepCopyOfData = !bIsGameWorld;
	const bool bKeepCopyOfCacheData = !bIsGameWorld;

	return AttachTiles(NavMesh, bKeepCopyOfData, bKeepCopyOfCacheData);
}

// Attach 核心：把 Chunk 中所有未挂载 Tile 插入目标 dtNavMesh。
// 关键步骤：
//   1) 若同坐标已存在 Tile → 先 removeTile（避免重复）
//   2) dtNavMesh::addTile() 失败（OOM）→ 打 Warning 跳过
//   3) 成功后更新 TileData.X/Y/Layer/bAttached；Active Tiles 模式下同步 (X,Y) 到集合
//   4) 根据 bKeepCopyOfData 决定是"清空本 Chunk 的裸指针"还是"Duplicate 一份"
//   5) 可选 AttachTileCacheLayer（压缩层），与主数据同样处理所有权
//   6) #if WITH_NAVMESH_SEGMENT_LINKS：所有 Tile 装完后再创建段链接（依赖邻接 Tile）
TArray<FNavTileRef> URecastNavMeshDataChunk::AttachTiles(ARecastNavMesh& NavMesh, const bool bKeepCopyOfData, const bool bKeepCopyOfCacheData)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%s Attaching to NavMesh - %s"), ANSI_TO_TCHAR(__FUNCTION__), *NavigationDataName.ToString());
	
	TArray<FNavTileRef> Result;
	Result.Reserve(Tiles.Num());

	dtNavMesh* DetourNavMesh = NavMesh.GetRecastMesh();

	if (DetourNavMesh != nullptr)
	{
		// 若使用"仅 Invoker 附近生成"模式，准备同步 ActiveTileSet
		TSet<FIntPoint>* ActiveTiles = nullptr;
		if (UE::NavMesh::Private::IsUsingActiveTileGeneration(NavMesh))
		{
			ActiveTiles = &NavMesh.GetActiveTileSet();
			ActiveTiles->Reserve(ActiveTiles->Num() + Tiles.Num());
		}
		
		// 迭代目标：依次挂载每个尚未 Attached 的 Tile
		for (FRecastTileData& TileData : Tiles)
		{
			if (!TileData.bAttached && TileData.TileRawData.IsValid())
			{
				// 防御：子关卡在 "被复用但未重新加载" 时 RawData 会变空
				if (TileData.TileRawData->RawData == nullptr)
				{
					UE_LOG(LogNavigation, Warning, TEXT("Null rawdata. This can be caused by the reuse of unloaded sublevels. 'LevelStreaming.ShouldReuseUnloadedButStillAroundLevels 0' can be used until this gets fixed."));
					continue;
				}
				
				// 校验 Header 的 Detour 版本号；老版本 Tile 直接跳过不插入
				const dtMeshHeader* Header = (dtMeshHeader*)TileData.TileRawData->RawData;
				if (Header->version != DT_NAVMESH_VERSION)
				{
					continue;
				}
				
				// If there was a previous tile at the location remove it
				// 同 (X,Y,Layer) 位置若已有 Tile，先移除，避免 dtNavMesh 冲突
				if (const dtMeshTile* PreExistingTile = DetourNavMesh->getTileAt(Header->x, Header->y, Header->layer))
				{
					if (const dtTileRef PreExistingTileRef = DetourNavMesh->getTileRef(PreExistingTile))
					{
						NavMesh.LogRecastTile(ANSI_TO_TCHAR(__FUNCTION__), FName("   "), FName("removing"), *DetourNavMesh, Header->x, Header->y, Header->layer, PreExistingTileRef);
						
						DetourNavMesh->removeTile(PreExistingTileRef, nullptr, nullptr);	
					}
				}

				// Attach mesh tile to target nav mesh 
				dtTileRef TileRef = 0;
				const dtMeshTile* MeshTile = nullptr;

				// DT_TILE_FREE_DATA：告诉 dtNavMesh 在将来 removeTile 时自动 dtFree；
				// 所以后续若 bKeepCopyOfData=false，本侧必须放弃指针（置 nullptr）避免双重释放。
				dtStatus status = DetourNavMesh->addTile(TileData.TileRawData->RawData, TileData.TileDataSize, DT_TILE_FREE_DATA, 0, &TileRef);

				if (dtStatusFailed(status))
				{
					// 超出 MaxTiles 的典型原因是 FixedTilePoolSize 设置过小
					if (dtStatusDetail(status, DT_OUT_OF_MEMORY))
					{
						UE_LOG(LogNavigation, Warning, TEXT("%s> Failed to add tile (%d,%d:%d), %d tile limit reached! (from: %s). If using FixedTilePoolSize, try increasing the TilePoolSize or using bigger tiles."),
							*NavMesh.GetName(), Header->x, Header->y, Header->layer, DetourNavMesh->getMaxTiles(), ANSI_TO_TCHAR(__FUNCTION__));
					}
					
					continue;
				}
				else
				{
					// 成功：写回 Tile 实际坐标，标记为已挂载
					MeshTile = DetourNavMesh->getTileByRef(TileRef);
					check(MeshTile);
					
					TileData.X = MeshTile->header->x;
					TileData.Y = MeshTile->header->y;
					TileData.Layer = MeshTile->header->layer;
					TileData.bAttached = true;
				}

				NavMesh.LogRecastTile(ANSI_TO_TCHAR(__FUNCTION__), FName("   "), FName("added"), *DetourNavMesh, TileData.X, TileData.Y, TileData.Layer, TileRef);
				
				// Active Tiles 模式：同步坐标到 ActiveTileSet
				if (ActiveTiles)
				{
					ActiveTiles->FindOrAdd(FIntPoint(TileData.X, TileData.Y));
				}
				
				if (bKeepCopyOfData == false)
				{
					// We don't own tile data anymore it will be released by recast navmesh 
					// 把指针还给 dtNavMesh；本侧置空以免析构时双重释放
					TileData.TileDataSize = 0;
					TileData.TileRawData->RawData = nullptr;
				}
				else
				{
					// In the editor we still need to own data, so make a copy of it
					// Editor：保留本侧副本（下次 Detach+Attach 免序列化）
					TileData.TileRawData->RawData = DuplicateRecastRawData(TileData.TileRawData->RawData, TileData.TileDataSize);
				}

				// Attach tile cache layer to target nav mesh
				// 若存在 TileCache 层，按同样语义挂载
				if (TileData.TileCacheDataSize > 0)
				{
					FBox TileBBox = Recast2UnrealBox(MeshTile->header->bmin, MeshTile->header->bmax);

					FNavMeshTileData LayerData(TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize, TileData.Layer, TileBBox);
					NavMesh.GetRecastNavMeshImpl()->AddTileCacheLayer(TileData.X, TileData.Y, TileData.Layer, LayerData);

					if (bKeepCopyOfCacheData == false)
					{
						// We don't own tile cache data anymore it will be released by navmesh
						TileData.TileCacheDataSize = 0;
						TileData.TileCacheRawData->RawData = nullptr;
					}
					else
					{
						// In the editor we still need to own data, so make a copy of it
						TileData.TileCacheRawData->RawData = DuplicateRecastRawData(TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize);
					}
				}

				Result.Add(FNavTileRef(TileRef));
			}
		}

#if WITH_NAVMESH_SEGMENT_LINKS
		// Create segment link connections now that all the tiles have been loaded.
		// Segment Link 跨 Tile 相连，必须所有 Tile 装完后再建立
		if (const FPImplRecastNavMesh* const NavMeshImpl = NavMesh.GetRecastNavMeshImpl())
		{
			for (int32 Index = 0; Index < Result.Num(); ++Index)
			{
				NavMeshImpl->ProcessSegmentLinksForTile(Result[Index]);
			}
		}
#endif // WITH_NAVMESH_SEGMENT_LINKS		
	}

	UE_LOG(LogNavigation, Verbose, TEXT("Attached %d tiles to NavMesh - %s"), Result.Num(), *NavigationDataName.ToString());
	return Result;
}

// 简化 Detach：Game 世界下夺取所有权（因为之前 Attach 没保留副本）；
// Editor 不夺取（Chunk 本身已有副本，Detach 只是取消挂载）。
TArray<FNavTileRef> URecastNavMeshDataChunk::DetachTiles(ARecastNavMesh& NavMesh)
{
	check(NavMesh.GetWorld());
	const bool bIsGameWorld = NavMesh.GetWorld()->IsGameWorld();

	// Keep data in game worlds (in editor we have a copy of the data so we don't keep it).
	const bool bTakeDataOwnership = bIsGameWorld;
	const bool bTakeCacheDataOwnership = bIsGameWorld;

	return DetachTiles(NavMesh, bTakeDataOwnership, bTakeCacheDataOwnership);
}

// Detach 核心：把 Chunk 中所有 bAttached==true 的 Tile 从 dtNavMesh 摘下。
// bIsDynamic（Dynamic 运行时生成模式）下，允许同一 (X,Y) 处存在多层 Tile，
// 额外清空该 XY 的所有层，避免残留。
TArray<FNavTileRef> URecastNavMeshDataChunk::DetachTiles(ARecastNavMesh& NavMesh, const bool bTakeDataOwnership, const bool bTakeCacheDataOwnership)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%s Detaching from %s"), ANSI_TO_TCHAR(__FUNCTION__), *NavigationDataName.ToString());

	TArray<FNavTileRef> Result;
	Result.Reserve(Tiles.Num());

	dtNavMesh* DetourNavMesh = NavMesh.GetRecastMesh();

	if (DetourNavMesh != nullptr)
	{
		TSet<FIntPoint>* ActiveTiles = nullptr;
		if (UE::NavMesh::Private::IsUsingActiveTileGeneration(NavMesh))
		{
			ActiveTiles = &NavMesh.GetActiveTileSet();
		}

		TArray<const dtMeshTile*> ExtraMeshTiles;
		check(NavMesh.GetWorld());
		const bool bIsGameWorld = NavMesh.GetWorld()->IsGameWorld();
		// Whether the navmesh is fully dynamic and supports rebuild from geometry. This allows for Dynamic Modifiers Only navmesh to take ownership of tiles on every layer when at the same XY location
		// Dynamic 模式下同 (X,Y) 可能有多层 Tile（上下两层楼面等），一并 Detach
		const bool bIsDynamic = bIsGameWorld && NavMesh.GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic;
		
		// 迭代目标：对每个 Attached 的 Tile 做"TileCache → 主数据"的顺序摘下
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.bAttached)
			{
				// Detach tile cache layer and take ownership over compressed data
				dtTileRef TileRef = 0;
				const dtMeshTile* MeshTile = DetourNavMesh->getTileAt(TileData.X, TileData.Y, TileData.Layer);
				if (MeshTile)
				{
					TileRef = DetourNavMesh->getTileRef(MeshTile);

					// 先处理 TileCache 压缩层
					if (bTakeCacheDataOwnership)
					{
						FNavMeshTileData TileCacheData = NavMesh.GetRecastNavMeshImpl()->GetTileCacheLayer(TileData.X, TileData.Y, TileData.Layer);
						if (TileCacheData.IsValid())
						{
							TileData.TileCacheDataSize = TileCacheData.DataSize;
							// Release() 把裸指针所有权从 FNavMeshTileData 转移到本侧
							TileData.TileCacheRawData->RawData = TileCacheData.Release();
						}
					}

					NavMesh.LogRecastTile(ANSI_TO_TCHAR(__FUNCTION__), FName("   "), FName("removing"), *DetourNavMesh, TileData.X, TileData.Y, TileData.Layer, TileRef);
				
					NavMesh.GetRecastNavMeshImpl()->RemoveTileCacheLayer(TileData.X, TileData.Y, TileData.Layer);

					// 再处理主 Tile 数据
					if (bTakeDataOwnership)
					{
						// Remove tile from navmesh and take ownership of tile raw data
						// 传入 out 指针 → dtNavMesh 会把裸数据所有权交回
						DetourNavMesh->removeTile(TileRef, &TileData.TileRawData->RawData, &TileData.TileDataSize);
					}
					else
					{
						// In the editor we have a copy of tile data so just release tile in navmesh
						// Editor：Chunk 自身已有副本，让 dtNavMesh 自行 dtFree
						DetourNavMesh->removeTile(TileRef, nullptr, nullptr);
					}

					if (ActiveTiles)
					{
						ActiveTiles->Remove(FIntPoint(TileData.X, TileData.Y));
					}
						
					Result.Add(FNavTileRef(TileRef));
				}

				if (bIsDynamic)
				{
					// Remove any tile remaining
					// Dynamic 模式：同 XY 可能多层；枚举所有层一并删
					const int32 MaxTiles = DetourNavMesh->getTileCountAt(TileData.X, TileData.Y);
					if (MaxTiles > 0)
					{
						ExtraMeshTiles.SetNumZeroed(MaxTiles, EAllowShrinking::No);
						const int32 MeshTilesCount = DetourNavMesh->getTilesAt(TileData.X, TileData.Y, ExtraMeshTiles.GetData(), MaxTiles);
						for (int32 i = 0; i < MeshTilesCount; ++i)
						{
							const dtMeshTile* ExtraMeshTile = ExtraMeshTiles[i];
							dtTileRef ExtraTileRef = DetourNavMesh->getTileRef(ExtraMeshTile);
							if (ExtraTileRef)
							{
								DetourNavMesh->removeTile(ExtraTileRef, nullptr, nullptr);
								Result.Add(FNavTileRef(ExtraTileRef));
							}
						}
					}
				}
				
			}

			// 置位清零，表示本 Tile 在 Chunk 侧已处于"离线"状态
			TileData.bAttached = false;
			TileData.X = 0;
			TileData.Y = 0;
			TileData.Layer = 0;
		}
	}

	UE_LOG(LogNavigation, Verbose, TEXT("Detached %d tiles from NavMesh - %s"), Result.Num(), *NavigationDataName.ToString());
	return Result;
}
#endif // WITH_RECAST

// 实验性：在 XY 平面按 TileSize 倍数平移 + 绕 RotationCenter 旋转 RotationDeg 度。
// 仅处理尚未 Attach 的 Tile（数据还在 Chunk 手里才能原地改写）。
// 前置条件：TileCacheDataSize == 0（当前未实现 Cache 的同步平移/旋转）。
// 核心：dtComputeTileOffsetFromRotation 求旋转带来的 Tile 坐标偏移 → dtTransformTileData 在字节流中改写所有坐标。
void URecastNavMeshDataChunk::MoveTiles(FPImplRecastNavMesh& NavMeshImpl, const FIntPoint& Offset, const FVector::FReal RotationDeg, const FVector2D& RotationCenter)
{
#if WITH_RECAST	
	UE_LOG(LogNavigation, Verbose, TEXT("%s Moving %i tiles on navmesh %s."), ANSI_TO_TCHAR(__FUNCTION__), Tiles.Num(), *NavigationDataName.ToString());

	dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;
	if (NavMesh != nullptr)
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.TileCacheDataSize != 0)
			{
				UE_LOG(LogNavigation, Error, TEXT("   TileCacheRawData is expected to be empty. No support for moving the cache data yet."));
				continue;
			}

			if ((TileData.bAttached == false) && TileData.TileRawData.IsValid())
			{
				// UE 旋转中心 → Recast 坐标
				const FVector RcRotationCenter = Unreal2RecastPoint(FVector(RotationCenter.X, RotationCenter.Y, 0.f));

				const FVector::FReal TileWidth = NavMesh->getParams()->tileWidth;
				const FVector::FReal TileHeight = NavMesh->getParams()->tileHeight;

				const dtMeshHeader* Header = (dtMeshHeader*)TileData.TileRawData->RawData;
				if (Header->version != DT_NAVMESH_VERSION)
				{
					continue;
				}

				// Apply rotation to tile coordinates
				// 计算"Tile 旋转后落在哪个格子"——以中心点为基准求格子坐标偏移
				int DeltaX = 0;
				int DeltaY = 0;
				FBox TileBox(Recast2UnrealPoint(Header->bmin), Recast2UnrealPoint(Header->bmax));
				FVector RcTileCenter = Unreal2RecastPoint(TileBox.GetCenter());
				dtComputeTileOffsetFromRotation(&RcTileCenter.X, &RcRotationCenter.X, RotationDeg, TileWidth, TileHeight, DeltaX, DeltaY);

				// 用户要求的平移 + 旋转累加的 Tile 偏移 = 最终新坐标
				const int OffsetWithRotX = Offset.X + DeltaX;
				const int OffsetWithRotY = Offset.Y + DeltaY;

				// 真正改写字节流：顶点 / 边 / BVTree / Link 全部按变换更新
				const bool bSuccess = dtTransformTileData(TileData.TileRawData->RawData, TileData.TileDataSize, OffsetWithRotX, OffsetWithRotY, TileWidth, TileHeight, RotationDeg, NavMesh->getBVQuantFactor(Header->resolution));
				UE_CLOG(bSuccess, LogNavigation, Verbose, TEXT("   Moved tile from (%i,%i) to (%i,%i)."), TileData.OriginalX, TileData.OriginalY, (TileData.OriginalX + OffsetWithRotX), (TileData.OriginalY + OffsetWithRotY));
			}
		}
	}

	UE_LOG(LogNavigation, Verbose, TEXT("%s Moving done."), ANSI_TO_TCHAR(__FUNCTION__));
#endif// WITH_RECAST
}

int32 URecastNavMeshDataChunk::GetNumTiles() const
{
	return Tiles.Num();
}

// 丢弃全部 Tile；共享指针会自动触发 FRawData::~FRawData 释放裸内存
void URecastNavMeshDataChunk::ReleaseTiles()
{
	Tiles.Reset();
}

// Deprecated
// 老 int32 TileIndex 版本 → 转换为 FNavTileRef 64bit 后走新 API
void URecastNavMeshDataChunk::GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<int32>& TileIndices, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached /*= true*/)
{
	Tiles.Empty(TileIndices.Num());

#if WITH_RECAST
	if (NavMeshImpl)
	{
		TArray<uint32> TileUnsignedIndices;
		TileUnsignedIndices.Append(TileIndices);

		TArray<FNavTileRef> TileRefs;
		FNavTileRef::DeprecatedMakeTileRefsFromTileIds(NavMeshImpl, TileUnsignedIndices, TileRefs);
		GetTiles(NavMeshImpl, TileRefs, CopyMode, bMarkAsAttached);
	}
#endif // WITH_RECAST
}

#if WITH_RECAST
// 从 NavMesh 中采集指定 Tile 到本 Chunk。
// CopyMode 决定是否实际复制字节（NoCopy 仅记录元信息；CopyData/CacheData/两者皆）。
// 典型使用：World Partition 保存当前激活 Tile 到 Chunk，跨关卡序列化。
void URecastNavMeshDataChunk::GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<FNavTileRef>& TileRefs, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached /*= true*/)
{
	Tiles.Empty(TileRefs.Num());

	const dtNavMesh* NavMesh = NavMeshImpl->DetourNavMesh;
	
	for (const FNavTileRef TileRef : TileRefs)
	{
		const dtMeshTile* Tile = NavMesh->getTileByRef(static_cast<dtTileRef>(TileRef));
		if (Tile && Tile->header)
		{
			// Make our own copy of tile data
			uint8* RawTileData = nullptr;
			if (CopyMode & EGatherTilesCopyMode::CopyData)
			{
				// 深拷贝以便本 Chunk 完全持有数据
				RawTileData = DuplicateRecastRawData(Tile->data, Tile->dataSize);
			}

			// We need tile cache data only if navmesh supports any kind of runtime generation
			FNavMeshTileData TileCacheData;
			uint8* RawTileCacheData = nullptr;
			if (CopyMode & EGatherTilesCopyMode::CopyCacheData)
			{
				TileCacheData = NavMeshImpl->GetTileCacheLayer(Tile->header->x, Tile->header->y, Tile->header->layer);
				if (TileCacheData.IsValid())
				{
					// Make our own copy of tile cache data
					RawTileCacheData = DuplicateRecastRawData(TileCacheData.GetData(), TileCacheData.DataSize);
				}
			}

			// 记录坐标 + 是否视为已 Attach（便于下一次 Attach 走增量）
			FRecastTileData RecastTileData(Tile->dataSize, RawTileData, TileCacheData.DataSize, RawTileCacheData);
			RecastTileData.OriginalX = Tile->header->x;
			RecastTileData.OriginalY = Tile->header->y;
			RecastTileData.X = Tile->header->x;
			RecastTileData.Y = Tile->header->y;
			RecastTileData.Layer = Tile->header->layer;
			RecastTileData.bAttached = bMarkAsAttached;

			Tiles.Add(RecastTileData);
		}
	}
}
#endif // WITH_RECAST

// Deprecated
// 老 int32 版本桥接到新 FNavTileRef 版本
void URecastNavMeshDataChunk::GetTilesBounds(const FPImplRecastNavMesh& NavMeshImpl, const TArray<int32>& TileIndices, FBox& OutBounds) const
{
	OutBounds.Init();
#if WITH_RECAST
	TArray<uint32> TileUnsignedIndices;
	TileUnsignedIndices.Append(TileIndices);

	TArray<FNavTileRef> TileRefs;
	FNavTileRef::DeprecatedMakeTileRefsFromTileIds(&NavMeshImpl, TileUnsignedIndices, TileRefs);
	GetTilesBounds(NavMeshImpl, TileRefs, OutBounds);
#endif // WITH_RECAST
}

#if WITH_RECAST
// 取指定 Tile 集合在世界空间的 AABB（UE 坐标）。
// dtMeshHeader::bmin/bmax 是 Recast 坐标，需用 Recast2UnrealBox 翻回 UE。
void URecastNavMeshDataChunk::GetTilesBounds(const FPImplRecastNavMesh& NavMeshImpl, const TArray<FNavTileRef>& TileRefs, FBox& OutBounds) const
{
	OutBounds.Init();

	const dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;

	for (const FNavTileRef TileRef : TileRefs)
	{
		const dtMeshTile* Tile = NavMesh->getTileByRef(static_cast<dtTileRef>(TileRef));
		if (Tile && Tile->header)
		{
			OutBounds += Recast2UnrealBox(Tile->header->bmin, Tile->header->bmax);
		}
	}
}
#endif // WITH_RECAST

//----------------------------------------------------------------------//
// FRecastTileData                                                                
//----------------------------------------------------------------------//
FRecastTileData::FRawData::FRawData(uint8* InData)
	: RawData(InData)
{
}

FRecastTileData::FRawData::~FRawData()
{
#if WITH_RECAST
	dtFree(RawData, DT_ALLOC_PERM_TILE_DATA);
#else
	FMemory::Free(RawData);
#endif
}

FRecastTileData::FRecastTileData()
	: OriginalX(0)
	, OriginalY(0)
	, X(0)
	, Y(0)
	, Layer(0)
	, TileDataSize(0)
	, TileCacheDataSize(0)
	, bAttached(false)
{
}

FRecastTileData::FRecastTileData(int32 DataSize, uint8* RawData, int32 CacheDataSize, uint8* CacheRawData)
	: OriginalX(0)
	, OriginalY(0)
	, X(0)
	, Y(0)
	, Layer(0)
	, TileDataSize(DataSize)
	, TileCacheDataSize(CacheDataSize)
	, bAttached(false)
{
	TileRawData = MakeShareable(new FRawData(RawData));
	TileCacheRawData = MakeShareable(new FRawData(CacheRawData));
}

// Helper to duplicate recast raw data
static uint8* DuplicateRecastRawData(const uint8* Src, int32 SrcSize)
{
#if WITH_RECAST	
	uint8* DupData = (uint8*)dtAlloc(SrcSize, DT_ALLOC_PERM_TILE_DATA);
#else
	uint8* DupData = (uint8*)FMemory::Malloc(SrcSize);
#endif
	FMemory::Memcpy(DupData, Src, SrcSize);
	return DupData;
}

namespace UE::NavMesh::Private
{
	bool IsUsingActiveTileGeneration(const ARecastNavMesh& NavMesh)
	{
#if WITH_RECAST
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavMesh.GetWorld());
		if (NavSys)
		{
			return NavMesh.IsUsingActiveTilesGeneration(*NavSys);
		}
#endif // WITH_RECAST
		return false;
	}
} // namespace UE::NavMesh::Private

//----------------------------------------------------------------------//
// URecastNavMeshDataChunk                                                                
//----------------------------------------------------------------------//
URecastNavMeshDataChunk::URecastNavMeshDataChunk(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URecastNavMeshDataChunk::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NavMeshVersion = NAVMESHVER_LATEST;
	Ar << NavMeshVersion;

	// when writing, write a zero here for now.  will come back and fill it in later.
	int64 RecastNavMeshSizeBytes = 0;
	int64 RecastNavMeshSizePos = Ar.Tell();
	Ar << RecastNavMeshSizeBytes;

	if (Ar.IsLoading())
	{
		auto CleanUpBadVersion = [&Ar, RecastNavMeshSizePos, RecastNavMeshSizeBytes]()
		{
			// incompatible, just skip over this data. Navmesh needs rebuilt.
			Ar.Seek(RecastNavMeshSizePos + RecastNavMeshSizeBytes);
		};

		if (NavMeshVersion < NAVMESHVER_MIN_COMPATIBLE)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s: URecastNavMeshDataChunk: Nav mesh version %d < Min compatible %d. Nav mesh needs to be rebuilt. \n"), *GetFullName(), NavMeshVersion, NAVMESHVER_MIN_COMPATIBLE);

			CleanUpBadVersion();
		}
		else if (NavMeshVersion > NAVMESHVER_LATEST)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s: URecastNavMeshDataChunk: Nav mesh version %d > NAVMESHVER_LATEST %d. Newer nav mesh should not be loaded by older versioned code. At a minimum the nav mesh needs to be rebuilt. \n"), *GetFullName(), NavMeshVersion, NAVMESHVER_LATEST);

			CleanUpBadVersion();
		}
#if WITH_RECAST
		else if (RecastNavMeshSizeBytes > 4)
		{
			SerializeRecastData(Ar, NavMeshVersion);
		}
#endif// WITH_RECAST
		else
		{
			// empty, just skip over this data
			Ar.Seek(RecastNavMeshSizePos + RecastNavMeshSizeBytes);
		}
	}
	else if (Ar.IsSaving())
	{
#if WITH_RECAST
		SerializeRecastData(Ar, NavMeshVersion);
#endif// WITH_RECAST

		int64 CurPos = Ar.Tell();
		RecastNavMeshSizeBytes = CurPos - RecastNavMeshSizePos;
		Ar.Seek(RecastNavMeshSizePos);
		Ar << RecastNavMeshSizeBytes;
		Ar.Seek(CurPos);
	}
}

#if WITH_RECAST
void URecastNavMeshDataChunk::SerializeRecastData(FArchive& Ar, int32 NavMeshVersion)
{
	int32 TileNum = Tiles.Num();
	Ar << TileNum;

	if (Ar.IsLoading())
	{
		Tiles.Empty(TileNum);
		for (int32 TileIdx = 0; TileIdx < TileNum; TileIdx++)
		{
			int32 TileDataSize = 0;
			Ar << TileDataSize;

			// Load tile data 
			uint8* TileRawData = nullptr;
			FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, NavMeshVersion, TileRawData, TileDataSize); //allocates TileRawData on load
			
			if (TileRawData != nullptr)
			{
				// Load compressed tile cache layer
				int32 TileCacheDataSize = 0;
				uint8* TileCacheRawData = nullptr;
				FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, NavMeshVersion, TileCacheRawData, TileCacheDataSize); //allocates TileCacheRawData on load
				
				// We are owner of tile raw data
				FRecastTileData TileData(TileDataSize, TileRawData, TileCacheDataSize, TileCacheRawData);
				Tiles.Add(TileData);
			}
		}
	}
	else if (Ar.IsSaving())
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.TileRawData.IsValid())
			{
				// Save tile itself
				Ar << TileData.TileDataSize;
				FPImplRecastNavMesh::SerializeRecastMeshTile(Ar, NavMeshVersion, TileData.TileRawData->RawData, TileData.TileDataSize);
				// Save compressed tile cache layer
				FPImplRecastNavMesh::SerializeCompressedTileCacheData(Ar, NavMeshVersion, TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize);
			}
		}
	}
}
#endif// WITH_RECAST

#if WITH_RECAST
TArray<FNavTileRef> URecastNavMeshDataChunk::AttachTiles(ARecastNavMesh& NavMesh)
{
	check(NavMesh.GetWorld());
	const bool bIsGameWorld = NavMesh.GetWorld()->IsGameWorld();

	// In editor we still need to own the data so a copy will be made.
	const bool bKeepCopyOfData = !bIsGameWorld;
	const bool bKeepCopyOfCacheData = !bIsGameWorld;

	return AttachTiles(NavMesh, bKeepCopyOfData, bKeepCopyOfCacheData);
}

TArray<FNavTileRef> URecastNavMeshDataChunk::AttachTiles(ARecastNavMesh& NavMesh, const bool bKeepCopyOfData, const bool bKeepCopyOfCacheData)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%s Attaching to NavMesh - %s"), ANSI_TO_TCHAR(__FUNCTION__), *NavigationDataName.ToString());
	
	TArray<FNavTileRef> Result;
	Result.Reserve(Tiles.Num());

	dtNavMesh* DetourNavMesh = NavMesh.GetRecastMesh();

	if (DetourNavMesh != nullptr)
	{
		TSet<FIntPoint>* ActiveTiles = nullptr;
		if (UE::NavMesh::Private::IsUsingActiveTileGeneration(NavMesh))
		{
			ActiveTiles = &NavMesh.GetActiveTileSet();
			ActiveTiles->Reserve(ActiveTiles->Num() + Tiles.Num());
		}
		
		for (FRecastTileData& TileData : Tiles)
		{
			if (!TileData.bAttached && TileData.TileRawData.IsValid())
			{
				if (TileData.TileRawData->RawData == nullptr)
				{
					UE_LOG(LogNavigation, Warning, TEXT("Null rawdata. This can be caused by the reuse of unloaded sublevels. 'LevelStreaming.ShouldReuseUnloadedButStillAroundLevels 0' can be used until this gets fixed."));
					continue;
				}
				
				const dtMeshHeader* Header = (dtMeshHeader*)TileData.TileRawData->RawData;
				if (Header->version != DT_NAVMESH_VERSION)
				{
					continue;
				}
				
				// If there was a previous tile at the location remove it
				if (const dtMeshTile* PreExistingTile = DetourNavMesh->getTileAt(Header->x, Header->y, Header->layer))
				{
					if (const dtTileRef PreExistingTileRef = DetourNavMesh->getTileRef(PreExistingTile))
					{
						NavMesh.LogRecastTile(ANSI_TO_TCHAR(__FUNCTION__), FName("   "), FName("removing"), *DetourNavMesh, Header->x, Header->y, Header->layer, PreExistingTileRef);
						
						DetourNavMesh->removeTile(PreExistingTileRef, nullptr, nullptr);	
					}
				}

				// Attach mesh tile to target nav mesh 
				dtTileRef TileRef = 0;
				const dtMeshTile* MeshTile = nullptr;

				dtStatus status = DetourNavMesh->addTile(TileData.TileRawData->RawData, TileData.TileDataSize, DT_TILE_FREE_DATA, 0, &TileRef);

				if (dtStatusFailed(status))
				{
					if (dtStatusDetail(status, DT_OUT_OF_MEMORY))
					{
						UE_LOG(LogNavigation, Warning, TEXT("%s> Failed to add tile (%d,%d:%d), %d tile limit reached! (from: %s). If using FixedTilePoolSize, try increasing the TilePoolSize or using bigger tiles."),
							*NavMesh.GetName(), Header->x, Header->y, Header->layer, DetourNavMesh->getMaxTiles(), ANSI_TO_TCHAR(__FUNCTION__));
					}
					
					continue;
				}
				else
				{
					MeshTile = DetourNavMesh->getTileByRef(TileRef);
					check(MeshTile);
					
					TileData.X = MeshTile->header->x;
					TileData.Y = MeshTile->header->y;
					TileData.Layer = MeshTile->header->layer;
					TileData.bAttached = true;
				}

				NavMesh.LogRecastTile(ANSI_TO_TCHAR(__FUNCTION__), FName("   "), FName("added"), *DetourNavMesh, TileData.X, TileData.Y, TileData.Layer, TileRef);
				
				if (ActiveTiles)
				{
					ActiveTiles->FindOrAdd(FIntPoint(TileData.X, TileData.Y));
				}
				
				if (bKeepCopyOfData == false)
				{
					// We don't own tile data anymore it will be released by recast navmesh 
					TileData.TileDataSize = 0;
					TileData.TileRawData->RawData = nullptr;
				}
				else
				{
					// In the editor we still need to own data, so make a copy of it
					TileData.TileRawData->RawData = DuplicateRecastRawData(TileData.TileRawData->RawData, TileData.TileDataSize);
				}

				// Attach tile cache layer to target nav mesh
				if (TileData.TileCacheDataSize > 0)
				{
					FBox TileBBox = Recast2UnrealBox(MeshTile->header->bmin, MeshTile->header->bmax);

					FNavMeshTileData LayerData(TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize, TileData.Layer, TileBBox);
					NavMesh.GetRecastNavMeshImpl()->AddTileCacheLayer(TileData.X, TileData.Y, TileData.Layer, LayerData);

					if (bKeepCopyOfCacheData == false)
					{
						// We don't own tile cache data anymore it will be released by navmesh
						TileData.TileCacheDataSize = 0;
						TileData.TileCacheRawData->RawData = nullptr;
					}
					else
					{
						// In the editor we still need to own data, so make a copy of it
						TileData.TileCacheRawData->RawData = DuplicateRecastRawData(TileData.TileCacheRawData->RawData, TileData.TileCacheDataSize);
					}
				}

				Result.Add(FNavTileRef(TileRef));
			}
		}

#if WITH_NAVMESH_SEGMENT_LINKS
		// Create segment link connections now that all the tiles have been loaded.
		if (const FPImplRecastNavMesh* const NavMeshImpl = NavMesh.GetRecastNavMeshImpl())
		{
			for (int32 Index = 0; Index < Result.Num(); ++Index)
			{
				NavMeshImpl->ProcessSegmentLinksForTile(Result[Index]);
			}
		}
#endif // WITH_NAVMESH_SEGMENT_LINKS		
	}

	UE_LOG(LogNavigation, Verbose, TEXT("Attached %d tiles to NavMesh - %s"), Result.Num(), *NavigationDataName.ToString());
	return Result;
}

TArray<FNavTileRef> URecastNavMeshDataChunk::DetachTiles(ARecastNavMesh& NavMesh)
{
	check(NavMesh.GetWorld());
	const bool bIsGameWorld = NavMesh.GetWorld()->IsGameWorld();

	// Keep data in game worlds (in editor we have a copy of the data so we don't keep it).
	const bool bTakeDataOwnership = bIsGameWorld;
	const bool bTakeCacheDataOwnership = bIsGameWorld;

	return DetachTiles(NavMesh, bTakeDataOwnership, bTakeCacheDataOwnership);
}

TArray<FNavTileRef> URecastNavMeshDataChunk::DetachTiles(ARecastNavMesh& NavMesh, const bool bTakeDataOwnership, const bool bTakeCacheDataOwnership)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%s Detaching from %s"), ANSI_TO_TCHAR(__FUNCTION__), *NavigationDataName.ToString());

	TArray<FNavTileRef> Result;
	Result.Reserve(Tiles.Num());

	dtNavMesh* DetourNavMesh = NavMesh.GetRecastMesh();

	if (DetourNavMesh != nullptr)
	{
		TSet<FIntPoint>* ActiveTiles = nullptr;
		if (UE::NavMesh::Private::IsUsingActiveTileGeneration(NavMesh))
		{
			ActiveTiles = &NavMesh.GetActiveTileSet();
		}

		TArray<const dtMeshTile*> ExtraMeshTiles;
		check(NavMesh.GetWorld());
		const bool bIsGameWorld = NavMesh.GetWorld()->IsGameWorld();
		// Whether the navmesh is fully dynamic and supports rebuild from geometry. This allows for Dynamic Modifiers Only navmesh to take ownership of tiles on every layer when at the same XY location
		const bool bIsDynamic = bIsGameWorld && NavMesh.GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic;
		
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.bAttached)
			{
				// Detach tile cache layer and take ownership over compressed data
				dtTileRef TileRef = 0;
				const dtMeshTile* MeshTile = DetourNavMesh->getTileAt(TileData.X, TileData.Y, TileData.Layer);
				if (MeshTile)
				{
					TileRef = DetourNavMesh->getTileRef(MeshTile);

					if (bTakeCacheDataOwnership)
					{
						FNavMeshTileData TileCacheData = NavMesh.GetRecastNavMeshImpl()->GetTileCacheLayer(TileData.X, TileData.Y, TileData.Layer);
						if (TileCacheData.IsValid())
						{
							TileData.TileCacheDataSize = TileCacheData.DataSize;
							TileData.TileCacheRawData->RawData = TileCacheData.Release();
						}
					}

					NavMesh.LogRecastTile(ANSI_TO_TCHAR(__FUNCTION__), FName("   "), FName("removing"), *DetourNavMesh, TileData.X, TileData.Y, TileData.Layer, TileRef);
				
					NavMesh.GetRecastNavMeshImpl()->RemoveTileCacheLayer(TileData.X, TileData.Y, TileData.Layer);

					if (bTakeDataOwnership)
					{
						// Remove tile from navmesh and take ownership of tile raw data
						DetourNavMesh->removeTile(TileRef, &TileData.TileRawData->RawData, &TileData.TileDataSize);
					}
					else
					{
						// In the editor we have a copy of tile data so just release tile in navmesh
						DetourNavMesh->removeTile(TileRef, nullptr, nullptr);
					}

					if (ActiveTiles)
					{
						ActiveTiles->Remove(FIntPoint(TileData.X, TileData.Y));
					}
						
					Result.Add(FNavTileRef(TileRef));
				}

				if (bIsDynamic)
				{
					// Remove any tile remaining
					const int32 MaxTiles = DetourNavMesh->getTileCountAt(TileData.X, TileData.Y);
					if (MaxTiles > 0)
					{
						ExtraMeshTiles.SetNumZeroed(MaxTiles, EAllowShrinking::No);
						const int32 MeshTilesCount = DetourNavMesh->getTilesAt(TileData.X, TileData.Y, ExtraMeshTiles.GetData(), MaxTiles);
						for (int32 i = 0; i < MeshTilesCount; ++i)
						{
							const dtMeshTile* ExtraMeshTile = ExtraMeshTiles[i];
							dtTileRef ExtraTileRef = DetourNavMesh->getTileRef(ExtraMeshTile);
							if (ExtraTileRef)
							{
								DetourNavMesh->removeTile(ExtraTileRef, nullptr, nullptr);
								Result.Add(FNavTileRef(ExtraTileRef));
							}
						}
					}
				}
				
			}

			TileData.bAttached = false;
			TileData.X = 0;
			TileData.Y = 0;
			TileData.Layer = 0;
		}
	}

	UE_LOG(LogNavigation, Verbose, TEXT("Detached %d tiles from NavMesh - %s"), Result.Num(), *NavigationDataName.ToString());
	return Result;
}
#endif // WITH_RECAST

void URecastNavMeshDataChunk::MoveTiles(FPImplRecastNavMesh& NavMeshImpl, const FIntPoint& Offset, const FVector::FReal RotationDeg, const FVector2D& RotationCenter)
{
#if WITH_RECAST	
	UE_LOG(LogNavigation, Verbose, TEXT("%s Moving %i tiles on navmesh %s."), ANSI_TO_TCHAR(__FUNCTION__), Tiles.Num(), *NavigationDataName.ToString());

	dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;
	if (NavMesh != nullptr)
	{
		for (FRecastTileData& TileData : Tiles)
		{
			if (TileData.TileCacheDataSize != 0)
			{
				UE_LOG(LogNavigation, Error, TEXT("   TileCacheRawData is expected to be empty. No support for moving the cache data yet."));
				continue;
			}

			if ((TileData.bAttached == false) && TileData.TileRawData.IsValid())
			{
				const FVector RcRotationCenter = Unreal2RecastPoint(FVector(RotationCenter.X, RotationCenter.Y, 0.f));

				const FVector::FReal TileWidth = NavMesh->getParams()->tileWidth;
				const FVector::FReal TileHeight = NavMesh->getParams()->tileHeight;

				const dtMeshHeader* Header = (dtMeshHeader*)TileData.TileRawData->RawData;
				if (Header->version != DT_NAVMESH_VERSION)
				{
					continue;
				}

				// Apply rotation to tile coordinates
				int DeltaX = 0;
				int DeltaY = 0;
				FBox TileBox(Recast2UnrealPoint(Header->bmin), Recast2UnrealPoint(Header->bmax));
				FVector RcTileCenter = Unreal2RecastPoint(TileBox.GetCenter());
				dtComputeTileOffsetFromRotation(&RcTileCenter.X, &RcRotationCenter.X, RotationDeg, TileWidth, TileHeight, DeltaX, DeltaY);

				const int OffsetWithRotX = Offset.X + DeltaX;
				const int OffsetWithRotY = Offset.Y + DeltaY;

				const bool bSuccess = dtTransformTileData(TileData.TileRawData->RawData, TileData.TileDataSize, OffsetWithRotX, OffsetWithRotY, TileWidth, TileHeight, RotationDeg, NavMesh->getBVQuantFactor(Header->resolution));
				UE_CLOG(bSuccess, LogNavigation, Verbose, TEXT("   Moved tile from (%i,%i) to (%i,%i)."), TileData.OriginalX, TileData.OriginalY, (TileData.OriginalX + OffsetWithRotX), (TileData.OriginalY + OffsetWithRotY));
			}
		}
	}

	UE_LOG(LogNavigation, Verbose, TEXT("%s Moving done."), ANSI_TO_TCHAR(__FUNCTION__));
#endif// WITH_RECAST
}

int32 URecastNavMeshDataChunk::GetNumTiles() const
{
	return Tiles.Num();
}

void URecastNavMeshDataChunk::ReleaseTiles()
{
	Tiles.Reset();
}

// Deprecated
void URecastNavMeshDataChunk::GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<int32>& TileIndices, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached /*= true*/)
{
	Tiles.Empty(TileIndices.Num());

#if WITH_RECAST
	if (NavMeshImpl)
	{
		TArray<uint32> TileUnsignedIndices;
		TileUnsignedIndices.Append(TileIndices);

		TArray<FNavTileRef> TileRefs;
		FNavTileRef::DeprecatedMakeTileRefsFromTileIds(NavMeshImpl, TileUnsignedIndices, TileRefs);
		GetTiles(NavMeshImpl, TileRefs, CopyMode, bMarkAsAttached);
	}
#endif // WITH_RECAST
}

#if WITH_RECAST
void URecastNavMeshDataChunk::GetTiles(const FPImplRecastNavMesh* NavMeshImpl, const TArray<FNavTileRef>& TileRefs, const EGatherTilesCopyMode CopyMode, const bool bMarkAsAttached /*= true*/)
{
	Tiles.Empty(TileRefs.Num());

	const dtNavMesh* NavMesh = NavMeshImpl->DetourNavMesh;
	
	for (const FNavTileRef TileRef : TileRefs)
	{
		const dtMeshTile* Tile = NavMesh->getTileByRef(static_cast<dtTileRef>(TileRef));
		if (Tile && Tile->header)
		{
			// Make our own copy of tile data
			uint8* RawTileData = nullptr;
			if (CopyMode & EGatherTilesCopyMode::CopyData)
			{
				RawTileData = DuplicateRecastRawData(Tile->data, Tile->dataSize);
			}

			// We need tile cache data only if navmesh supports any kind of runtime generation
			FNavMeshTileData TileCacheData;
			uint8* RawTileCacheData = nullptr;
			if (CopyMode & EGatherTilesCopyMode::CopyCacheData)
			{
				TileCacheData = NavMeshImpl->GetTileCacheLayer(Tile->header->x, Tile->header->y, Tile->header->layer);
				if (TileCacheData.IsValid())
				{
					// Make our own copy of tile cache data
					RawTileCacheData = DuplicateRecastRawData(TileCacheData.GetData(), TileCacheData.DataSize);
				}
			}

			FRecastTileData RecastTileData(Tile->dataSize, RawTileData, TileCacheData.DataSize, RawTileCacheData);
			RecastTileData.OriginalX = Tile->header->x;
			RecastTileData.OriginalY = Tile->header->y;
			RecastTileData.X = Tile->header->x;
			RecastTileData.Y = Tile->header->y;
			RecastTileData.Layer = Tile->header->layer;
			RecastTileData.bAttached = bMarkAsAttached;

			Tiles.Add(RecastTileData);
		}
	}
}
#endif // WITH_RECAST

// Deprecated
void URecastNavMeshDataChunk::GetTilesBounds(const FPImplRecastNavMesh& NavMeshImpl, const TArray<int32>& TileIndices, FBox& OutBounds) const
{
	OutBounds.Init();
#if WITH_RECAST
	TArray<uint32> TileUnsignedIndices;
	TileUnsignedIndices.Append(TileIndices);

	TArray<FNavTileRef> TileRefs;
	FNavTileRef::DeprecatedMakeTileRefsFromTileIds(&NavMeshImpl, TileUnsignedIndices, TileRefs);
	GetTilesBounds(NavMeshImpl, TileRefs, OutBounds);
#endif // WITH_RECAST
}

#if WITH_RECAST
void URecastNavMeshDataChunk::GetTilesBounds(const FPImplRecastNavMesh& NavMeshImpl, const TArray<FNavTileRef>& TileRefs, FBox& OutBounds) const
{
	OutBounds.Init();

	const dtNavMesh* NavMesh = NavMeshImpl.DetourNavMesh;

	for (const FNavTileRef TileRef : TileRefs)
	{
		const dtMeshTile* Tile = NavMesh->getTileByRef(static_cast<dtTileRef>(TileRef));
		if (Tile && Tile->header)
		{
			OutBounds += Recast2UnrealBox(Tile->header->bmin, Tile->header->bmax);
		}
	}
}
#endif // WITH_RECAST
