// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   NavMesh 串行化的"历史版本号"常量集合。每当序列化布局发生破坏性改动
//   时会追加一个新的 NAVMESHVER_xxx 宏；NAVMESHVER_LATEST 标示当前版本，
//   NAVMESHVER_MIN_COMPATIBLE 标示仍可读的最老版本。
//
//   新增版本时建议走 Custom Version（FCustomVersion / UsingCustomVersion），
//   本枚举是历史遗留兼容路径。
// =============================================================================

#pragma once

/** 
 *  Versioning, Note the correct way of doing this is now to use the Custom Version see Ar.UsingCustomVersion(XXXBranchObjectVersion::GUID) this solves issues when versioning
 *  is changed in different branches at the same time.
 */

// 初始版本。
#define NAVMESHVER_INITIAL						1
// 支持 Tile 粒度生成。
#define NAVMESHVER_TILED_GENERATION				2
// 支持 Seamless 重建（第一次尝试）。
#define NAVMESHVER_SEAMLESS_REBUILDING_1		3
// 引入 Area 类（UNavArea）。
#define NAVMESHVER_AREA_CLASSES					4
// 引入 Cluster 路径（分层寻路）。
#define NAVMESHVER_CLUSTER_PATH					5
// 引入 Segment Link（段到段连接）。
#define NAVMESHVER_SEGMENT_LINKS				6
// 支持动态链接（运行时开关）。
#define NAVMESHVER_DYNAMIC_LINKS				7
// 64 位 PolyRef/TileRef。
#define NAVMESHVER_64BIT						9
// Cluster 数据结构简化。
#define NAVMESHVER_CLUSTER_SIMPLIFIED			10
// OffMesh 连接高度 Bug 修复。
#define NAVMESHVER_OFFMESH_HEIGHT_BUG			11
// Landscape 高度采样变更。
#define NAVMESHVER_LANDSCAPE_HEIGHT				13
// LWC：引入大世界坐标。
#define NAVMESHVER_LWCOORDS						14
// Oodle 压缩。
#define NAVMESHVER_OODLE_COMPRESSION			15
#define NAVMESHVER_LWCOORDS_SEREALIZATION 		17 // Allows for nav meshes to be serialized agnostic of LWCoords being float or double.
#define NAVMESHVER_MAXTILES_COUNT_CHANGE 		19
#define NAVMESHVER_LWCOORDS_OPTIMIZATION		20
#define NAVMESHVER_OPTIM_FIX_SERIALIZE_PARAMS	21 // Fix, serialize params that used to be in the tile and are now in the navmesh.
#define NAVMESHVER_MAXTILES_COUNT_SKIP_INCLUSION 22
#define NAVMESHVER_TILE_RESOLUTIONS				23 // Addition of a tile resolution index to the tile header.
#define NAVMESHVER_TILE_RESOLUTIONS_CELLHEIGHT	24 // Addition of CellHeight in the resolution params, deprecating the original CellHeight.
#define NAVMESHVER_1_VOXEL_AGENT_STEEP_SLOPE_FILTER_FIX	25 // Fix, remove steep slope filtering during heightfield ledge filtering when the agent radius is included into a single voxel
#define NAVMESHVER_TILE_RESOLUTIONS_AGENTMAXSTEPHEIGHT 26	// Addition of AgentMaxStepHeight in the resolution params, deprecating the original AgentMaxStepHeight.
#define NAVMESHVER_NAVLINK_JUMP_CONFIGS			27 // Addition of NavLinkJumpConfigs, deprecating the original NavLinkJumpDownConfig

// 当前最新序列化版本号。
#define NAVMESHVER_LATEST				NAVMESHVER_NAVLINK_JUMP_CONFIGS
// 仍被支持读取的最老版本号（更老的数据需要重新构建）。
#define NAVMESHVER_MIN_COMPATIBLE		NAVMESHVER_LWCOORDS_OPTIMIZATION
