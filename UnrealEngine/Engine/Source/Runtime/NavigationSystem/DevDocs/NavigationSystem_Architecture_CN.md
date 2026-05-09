# NavigationSystem 模块架构文档（中文）

> 针对 UE5 Engine `Runtime/NavigationSystem` 模块。
> 本文是在对公开头文件与部分 Private 文件的快速扫描后产出的初版；会在后续各子模块注释过程中持续修正补充，每次修改会在文末"修订记录"追加。

---

## 0. 文档用途

- 在对源码进行中文注释前，先建立对"这个模块到底在做什么"的整体认知，避免在几十万行代码中迷失。
- 作为 subagent 并行添加注释时的分工依据：按下述"模块拓扑"拆分，每个 agent 负责一批文件。
- 作为后续阅读/维护的中文索引。

---

## 1. 模块总览

### 1.1 模块职责

`NavigationSystem` 是 UE 中负责**导航数据生成、存储、查询**的运行时模块：

- 接收并追踪场景中所有"对导航可能产生影响"的对象（几何体、Modifier、Link 等）
- 按"代理类型 (Agent)"维护一份或多份 `ANavigationData`（通常是 `ARecastNavMesh`）
- 基于 **Recast & Detour**（Epic 的 fork）异步生成/刷新导航网格 Tile
- 提供寻路、投影、随机点、Raycast、批量查询等 API，既暴露给 C++ 也暴露给蓝图
- 支持"只在玩家附近生成导航"（Navigation Invokers）的动态世界场景
- 支持自定义链接（NavLink，用于门/梯子/跳跃等非网格可达）
- 支持 Crowd Manager 插槽（用于人群避障，实现在其它模块）

### 1.2 编译开关

定义在 `NavigationSystem.Build.cs`：

| 宏 | 含义 |
| --- | --- |
| `WITH_RECAST` | 是否链接 Recast/Detour 并启用 NavMesh 所有功能；默认 1，可通过 Target `bCompileRecast` 关闭 |
| `WITH_NAVMESH_SEGMENT_LINKS` | 是否启用 segment 链接（段到段连接，实验特性） |
| `WITH_NAVMESH_CLUSTER_LINKS` | 是否启用 cluster 链接（多边形分簇，用于层级寻路） |
| `RECAST_INTERNAL_DEBUG_DATA` | 非 Shipping 下开启，用于保留 Tile 生成的中间数据（高度场/Contour/PolyMesh 等） |
| `RECAST_ASYNC_REBUILDING` | 是否使用后台工作线程生成 Tile（默认 1） |
| `ALLOW_TIME_SLICE_NAV_REGEN` | 是否启用时间切片式生成（仅当异步关闭时生效） |
| `ALLOW_TIME_SLICE_DEBUG` | 非 Shipping 下开启，用于对 time slice 计时并标记区段 |
| `NAVSYS_DEBUG` | 内部断言调试 |

### 1.3 外部依赖

- `Public`: Core / CoreUObject / Engine / Chaos / GeometryCollectionEngine
- `Private`: RHI / RenderCore / DerivedDataCache / TargetPlatform
- `WITH_RECAST`: Navmesh（Recast/Detour 源码封装，位于 `Runtime/Navmesh`）
- `WITH_EDITOR`: EditorFramework / UnrealEd（循环依赖）

---

## 2. 核心概念（读代码前必看）

| 术语 | 说明 |
| --- | --- |
| `FNavAgentProperties` | 一类"寻路代理"的物理参数：半径、高度、最大步高、最大斜率、类型 | 
| `FNavDataConfig` | Agent + 生成参数（cell/tile 尺寸、色彩等）。一份 `ANavigationData` 关联一个 `FNavDataConfig` |
| `ANavigationData` | 代表一份导航数据（被放进世界里的 AActor），抽象基类。派生实现：`ARecastNavMesh`、`AAbstractNavData`、`ANavigationGraph` |
| `FNavigationElement` | 5.5 新引入，对"任何影响导航的来源"（Actor/Component/自定义对象）的轻量抽象包装，持有 Bounds、Modifier、GeometryExportDelegate 等；后续代码大量以 `TSharedRef<const FNavigationElement>` 流转 |
| `FNavigationElementHandle` | `FNavigationElement` 的句柄（稳定整数 ID），用于 TMap 键 |
| `FNavigationRelevantData` | "一个 NavigationElement 对导航的贡献数据"：几何顶点+索引、链接、区域修饰、Bounds 等 |
| `FNavigationOctree` | 世界级 Octree（`TOctree2`），每个叶子存 `FNavigationOctreeElement`，用于快速查询"哪些元素落在某 Tile/Box 内" |
| `FNavigationDirtyArea` | 一块需要重建的 AABB + Dirty 标记位 |
| `INavRelevantInterface` | 组件/Actor 想参与导航生成的标志接口，实现后就能被登记进 Octree |
| `INavLinkCustomInterface` | 自定义链接接口，可以动态开关（门）、走楼梯等 |
| `UNavArea` | "区域类"——寻路代价 / 颜色 / 允许的 Agent / Flag 的 UObject 描述；派生出 `UNavArea_Default / _Null / _Obstacle / _LowHeight`。**AreaFlags==0 在 Recast 约定里代表"不可通行"**（NavArea_Null / NavArea_LowHeight 即此约定）|
| `FNavigationQueryFilter` | 寻路过滤器运行时数据（每个 Area 的 cost 覆写、允许/禁止 flag、启发函数 scale 等），线程安全共享指针 |
| `UNavigationQueryFilter` | 过滤器的 UObject 描述版，可配置 Area 覆写；蓝图可见；运行时会被"初始化"成 `FNavigationQueryFilter` 并缓存在 `ANavigationData`。默认构造 `IncludeFlags.Packed=0xFFFF`（全接受）、`ExcludeFlags.Packed=0`（全不禁）。**`bInstantiateForQuerier=true` 时每次 `GetQueryFilter` 都会克隆 Default Filter 并重新 Initialize**，高频寻路有性能代价 |
| `Invoker` | 注册到导航系统的"活跃点"（一般是玩家 Pawn），仅在其附近生成 Tile；见 `FNavigationInvoker` / `UNavigationInvokerComponent` |
| `NavTile` | Recast 空间划分的基本单元；拥有多层 (`dtMeshTile`)。所有增量更新以 Tile 为粒度 |
| `NavNodeRef` | Recast 多边形引用（32/64 bit polyRef） |
| `FNavTileRef` | 64 位 Tile 引用，5.5 之后替代裸 `dtTileRef` |
| `FNavLinkId` | 5.3 引入的 64 位链接 ID（稳定，可序列化）；之前使用自增 uint32。`UNavLinkCustomComponent` 的 ID 生成**双轨制**：编辑器路径用 `GenerateUniqueId(AuxiliaryCustomLinkId, Actor.InstanceGuid)` 保证 Level Instance 之间确定性不冲突；运行时路径若为 Invalid 则用 `FGuid::NewGuid()` 非确定性种子。Serialize 时会把 UE5.3 之前的 `NavLinkUserId (uint32)` 升级为 `FNavLinkId` |

---

## 3. 模块内文件分层与拓扑排序

按 `依赖方向`：Layer 越小越底层、无依赖；Layer 越大越接近上层业务。同 Layer 内部基本平级。

### Layer 0：类型/接口/帮助类

> 几乎无内部依赖，定义全局符号与基础结构。

- `Public/NavigationSystemTypes.h` + `Private/NavigationSystemTypes.cpp`  
  FPathFindingQuery / FPathFindingQueryData / FNavigationInvoker / FCustomLinkOwnerInfo / ProcessNavLink* 委托与默认实现
- `Public/NavDebugTypes.h`  
  （单一 typedef）`FNavDebugMeshData = FDebugRenderSceneProxy::FMesh`
- `Public/NavNodeInterface.h`  
  `INavNodeInterface`：Actor 可暴露自身 `UNavigationGraphNodeComponent`
- `Public/NavigationPathGenerator.h`  
  `INavigationPathGenerator`：对"能为 Agent 生成路径"的统一接口（很窄）
- `Public/NavLinkHostInterface.h`  
  `INavLinkHostInterface`：Actor 宣告自身"托管多少 NavLink 定义类"
- `Public/NavLinkCustomInterface.h`  
  `INavLinkCustomInterface`：一条自定义链接应实现的接口（GetLinkData / GetId / OnLinkMoveStarted 等）
- `Public/CrowdManagerBase.h` + （无对应 cpp）  
  `UCrowdManagerBase`：人群管理器抽象基类，真正实现在外部模块
- `Public/NavMesh/RecastHelpers.h` + `Private/NavMesh/RecastHelpers.cpp`  
  `Unreal2Recast` / `Recast2Unreal` 坐标转换函数：UE (x,y,z) ↔ Recast (-x,z,-y)
- `Public/NavMesh/RecastVersion.h`  
  NavMesh 序列化版本号宏定义（历史变更日志）
- `Public/NavMesh/LinkGenerationDebugFlags.h`  
  NavLink 自动生成调试位 enum
- `Public/NavMesh/RecastInternalDebugData.h` + `Public/NavMesh/RecastInternalDebugData.cpp`  
  Recast 生成中间产物的可视化数据容器

### Layer 1：NavArea 与 Filter

> 依赖 Layer 0。寻路代价、过滤策略。

- `Public/NavAreas/NavArea.h` / `Private/NavAreas/NavArea.cpp` —— `UNavArea` 基类
  - `UNavArea_Default / _LowHeight / _Null / _Obstacle`：预定义 4 种
  - `UNavAreaMeta` / `UNavAreaMeta_SwitchByAgent`：Meta 区域，按 Agent 切换具体区域
- `Public/NavFilters/NavigationQueryFilter.h` / `.cpp` —— `UNavigationQueryFilter`
- `Public/NavFilters/RecastFilter_UseDefaultArea.h` / `.cpp` —— Recast 专用过滤器

### Layer 2：导航元素相关容器

> 依赖 Layer 0+1。Octree、脏区域管理、路径对象。

- `Public/NavigationOctree.h` + `Private/NavigationOctree.cpp`  
  `FNavigationOctree` / `FNavigationOctreeElement` / `FNavigationOctreeSemantics`；核心数据结构
- `Public/NavigationOctreeController.h` + `Private/NavigationOctreeController.cpp`  
  封装 NavOctree 读写锁、PendingUpdates、Parent/Child 关系
- `Public/NavigationDirtyAreasController.h` + `Private/NavigationDirtyAreasController.cpp`  
  按帧累积 Dirty Area，Tick 时推送给各 `ANavigationData`
- `Public/NavigationPath.h` + `Private/NavigationPath.cpp`  
  `UNavigationPath`：对 `FNavigationPath` 的 UObject 包装（蓝图友好）。**注意**：`FNavigationPath` 自身的大量方法（`Invalidate` / `RePathFailed` / `DoneUpdating` / `ResetForRepath` / `InternalResetNavigationPath` / `TickPathObservation` / `DoesIntersectBox` / `DebugDraw` / `DescribeSelfToVisLog` 等）的 **C++ 实现**也放在 `NavigationPath.cpp`，并不在 `NavigationData.cpp` 里
- `Public/NavCollision.h` + `Private/NavCollision.cpp`  
  `UNavCollision`：StaticMesh 用的导航碰撞（盒/柱/凸包），走 DDC 缓存
- `Private/NavigationObjectRepository.h` + `.cpp`  
  `UNavigationObjectRepository`：World 级子系统，集中保管"已注册的导航元素/自定义链接"

### Layer 3：NavigationData 抽象与基础实现

> 依赖 Layer 0-2。

- `Public/NavigationData.h` + `Private/NavigationData.cpp`  
  `ANavigationData` 基类：Actor 形态、挂 `RenderingComp`；持有  
  - `FindPathImplementation` 函数指针（非虚，避免调用开销）  
  - `FNavDataGenerator` 生成器（可选）  
  - `ActivePaths` 列表 / `ObservedPaths`（定点观察） / `RepathRequests`  
  - 查询 API：`FindPath / TestPath / Raycast / ProjectPoint / BatchRaycast / FindMoveAlongSurface / GetRandomPoint` 等  
  + `FNavigationPath` 内嵌结构（路径数据，含重算相关字段与 GoalActor 观察）  
  + `FPathFindingResult` / `FNavPathRecalculationRequest` / `FSupportedAreaData` 等辅助
- `Public/AbstractNavData.h` + `Private/AbstractNavData.cpp`  
  "没 NavMesh 但仍需占位"时使用的空壳（返回空路径）
- `Public/NavigationDataHandler.h` + `Private/NavigationDataHandler.cpp`  
  组合 OctreeController+DirtyController，提供 `RegisterElementWithNavOctree` / `UpdateNavOctreeElement` / `RemoveFromNavOctree` / `ProcessPendingOctreeUpdates` 等统一门面

### Layer 4：NavMesh（Recast 实现）

> 依赖 Layer 0-3 + 外部 Navmesh 模块。本层是本项目**代码量最大**部分。

核心三件套（都非常大）：

- `Public/NavMesh/RecastNavMesh.h` + `Private/NavMesh/RecastNavMesh.cpp` （150KB）  
  `ARecastNavMesh`：具体 NavMesh 派生；暴露 Debug 参数；大量 Serialize / 过滤器命名集 / Tile 坐标 API / 静态 `ARecastNavMesh::FindPath` 回调（被挂到基类 `FindPathImplementation`）
- `Public/NavMesh/PImplRecastNavMesh.h` + `Private/NavMesh/PImplRecastNavMesh.cpp` （125KB）  
  `FPImplRecastNavMesh`：Pimpl 模式，隐藏 `dtNavMesh*` 的具体访问；所有 Detour 调用都经这里；查询/Raycast/FindPath/Stringpull 的具体 C++→Detour 适配在此
- `Public/NavMesh/RecastNavMeshGenerator.h` + `Private/NavMesh/RecastNavMeshGenerator.cpp` （301KB，最大）  
  `FRecastNavMeshGenerator`：实现 `FNavDataGenerator`。Tile 任务派发、Dirty→Tile 解析、异步构建、Active Tiles、Link 生成、时间切片、Voxel cache…  
  + `FRecastTileGenerator` 作为单 Tile 构建实体  
  + `FRecastBuildConfig` / `FRecastVoxelCache` / `FRecastGeometryCache` / `FRecastRawGeometryElement` 等结构

辅助：

- `Public/NavMesh/NavMeshPath.h` + `Private/NavMesh/NavMeshPath.cpp`  
  `FNavMeshPath`：派生自 `FNavigationPath`，带 PathCorridor、PortalEdge、StringPull 标志等
- `Public/NavMesh/NavMeshBoundsVolume.h` + `Private/NavMesh/NavMeshBoundsVolume.cpp`  
  `ANavMeshBoundsVolume`：关卡里"画框"告诉系统在哪生成导航
- `Public/NavMesh/RecastNavMeshDataChunk.h` + `Private/NavMesh/RecastNavMeshDataChunk.cpp`  
  `URecastNavMeshDataChunk`：Tile 数据块，可独立加载（关卡流式）
- `Public/NavMesh/NavMeshRenderingComponent.h` + `Private/NavMesh/NavMeshRenderingComponent.cpp` （77KB）  
  视觉化组件，聚合所有 Recast 调试几何
- `Public/NavMesh/NavTestRenderingComponent.h` + `Private/NavMesh/NavTestRenderingComponent.cpp`  
  `ANavigationTestingActor` 专用的可视化
- `Public/NavMesh/RecastGeometryExport.h`  
  Recast 的 `ExportRigidBodyGeometry` 之类 helper
- `Public/NavMesh/RecastQueryFilter.h`  
  `FRecastQueryFilter`：实现 `INavigationQueryFilterInterface`，内嵌 Detour `dtQueryFilter`
- `Public/NavMesh/LinkGenerationConfig.h` + `Private/NavMesh/LinkGenerationConfig.cpp`  
  `FNavLinkGenerationJumpConfig`：自动跳跃链接的尺寸参数

### Layer 4'：NavGraph（不完整的替代实现）

> 与 NavMesh 平行，目前是半成品。

- `Public/NavGraph/NavigationGraph.h` + `Private/NavGraph/NavigationGraph.cpp`  
  `ANavigationGraph`（abstract）
- `Public/NavGraph/NavigationGraphNode.h` + `Public/NavGraph/NavigationGraphNodeComponent.h`  
  节点结构
- `Private/NavGraph/NavGraphGenerator.h` + `.cpp`  
  半成品生成器

### Layer 5：组件层

> 依赖 Layer 0-4；挂到 Actor 上对导航产生"效果"。

- `Public/NavRelevantComponent.h` + `Private/NavRelevantComponent.cpp`  
  `UNavRelevantComponent`：基类——任何想"挂到 Actor 上参与导航"的组件都派生此
- `Public/NavModifierComponent.h` + `Private/NavModifierComponent.cpp`  
  应用某 Area 到自己包围盒内
- `Public/NavModifierVolume.h` + `Private/NavModifierVolume.cpp`  
  体积版 Modifier（AVolume 派生）
- `Public/SplineNavModifierComponent.h` + `Private/SplineNavModifierComponent.cpp`  
  沿 Spline 的 Modifier
- `Public/NavigationInvokerComponent.h` + `Private/NavigationInvokerComponent.cpp`  
  Invoker：附近生成 NavMesh
- `Public/NavLinkComponent.h` + `Private/NavLinkComponent.cpp`  
  简单链接组件
- `Public/NavLinkCustomComponent.h` + `Private/NavLinkCustomComponent.cpp`  
  SmartLink（可开关、可广播、可禁区）
- `Public/NavLinkRenderingComponent.h` / `Public/NavLinkRenderingProxy.h` + `Private/NavLinkRenderingComponent.cpp`  
  链接的可视化
- `Public/NavLinkTrivial.h`  
  占位简单链接
- `Public/BaseGeneratedNavLinksProxy.h` + `Private/BaseGeneratedNavLinksProxy.cpp`  
  自动生成链接的 Proxy 基类（实验特性）
- `Public/NavigationTestingActor.h` + `Private/NavigationTestingActor.cpp`  
  编辑器里拉出来拖动测试路径的 Actor
- `Public/NavSystemConfigOverride.h` + `Private/NavSystemConfigOverride.cpp`  
  关卡里放一个 Actor 覆盖 NavigationSystem 的配置

### Layer 6：NavigationSystem 主类与模块入口

> 依赖上述全部。

- `Public/NavigationSystem.h` + `Private/NavigationSystem.cpp` （源文件 220 KB 最大单文件）  
  `UNavigationSystemV1`：派生自 `UNavigationSystemBase`，World-owning UObject；持有  
  - `NavDataSet` / `MainNavData` / `AbstractNavData`  
  - `DefaultOctreeController` / `DefaultDirtyAreasController`  
  - `PendingNavBoundsUpdates` / `RegisteredNavBounds`  
  - `Invokers` / `InvokerLocations`  
  - `FNavRegenTimeSliceManager`（管理时间切片参数与 Tile 构建优先级）  
  + 大量 `FindPathSync / FindPathAsync / ProjectPointToNavigation / GetRandomPoint / ...` 入口  
  + 静态蓝图库形式的 K2_* 函数（每个 K2_ 版包装成 `UFUNCTION`）
- `Public/NavigationSystemModule.h` + `Private/NavigationSystemModule.cpp`  
  `INavSysModule` / `FNavigationSystemModule`：模块生命周期；仅在 WITH_EDITOR 下挂 OnPreWorldInitialization / OnPostEngineInit 委托
- `Private/NavigationInterfaces.cpp`  
  4 行占位（编译单元，触发 UInterface 自动注册）

---

## 4. 关键执行流程（First Pass）

### 4.1 启动与 World 初始化

1. `IMPLEMENT_MODULE(FNavigationSystemModule)` → `StartupModule` 仅在 WITH_EDITOR 下注册 `INavLinkCustomInterface::OnPreWorldInitialization`、`ANavMeshBoundsVolume::OnPostEngineInit`。
2. 引擎在创建 `UWorld` 时根据 Project Settings 的 `NavigationSystemConfig` 实例化 `UNavigationSystemV1`（或关卡里 `ANavSystemConfigOverride` 指定的类）。
3. `UNavigationSystemV1::PostInitProperties` → 构造 OctreeController、DirtyController、初始化 Invoker 数据、注册到 `UNavigationObjectRepository`。
4. `ConditionalPopulateNavOctree` 视条件分配 `FNavigationOctree`。
5. `GatherNavigationBounds` 遍历世界所有 `ANavMeshBoundsVolume`，加入 `RegisteredNavBounds`；出现/消失时通过 `PendingNavBoundsUpdates` 延迟处理。
6. `RegisterNavigationDataInstances` 把世界里已存在的 `ANavigationData` Actor 登记进来，按 Agent 分配槽位；支持的 Agent 在 `SupportedAgents` 里。
7. 若没有匹配的 `ANavigationData` 且 `bAutoCreateNavigationData==true`，系统会 `CreateNavigationDataInstanceInLevel` 自动 Spawn 一个（通常是 `ARecastNavMesh`）。

### 4.2 Navigation Octree 更新路径

任何组件/Actor 发生移动/增删/属性修改时：

1. 引擎 / 组件 内部调用静态入口 `UNavigationSystemV1::OnComponentRegistered/Unregistered/UpdateComponent/UpdateActor*`。  
2. 这些函数把当前 World 的 NavigationSystem 取出来，委托给 `FNavigationDataHandler`。  
3. `FNavigationDataHandler::RegisterElementWithNavOctree`：  
   - 构造 / 复用 `FNavigationElement` 与 `FNavigationRelevantData`  
   - 投进 `FNavigationOctreeController::PendingUpdates`（而非立即进 Octree，避免一帧多动）  
4. 每帧 Tick 触发 `ProcessPendingOctreeUpdates`：  
   - 对每个 `FNavigationDirtyElement` 调 `AddElementToNavOctree`  
   - 真正插入 `FNavigationOctree`  
   - 按 Flags 追加到 `FNavigationDirtyAreasController`  
5. Dirty Area 按 `DirtyAreasUpdateFreq` 周期推给每个 `ANavigationData::RebuildDirtyAreas`。

### 4.3 Recast Tile 生成

`FRecastNavMeshGenerator::Tick` 被 `ARecastNavMesh::TickAsyncBuild` 驱动：

1. 消化 Dirty Areas → 转为 Tile 坐标集合 → 按优先级 / 距离 Invoker 排序进入 `PendingDirtyTiles`
2. 按 `MaxSimultaneousTileGenerationJobs` 上限挑 Tile → 构造 `FRecastTileGenerator`
3. `FRecastTileGenerator::DoWork`（或 `DoWorkTimeSliced`）：  
   a. **收集几何** —— 用 NavOctree 的 Box 查询收集所有影响本 Tile 的 `FNavigationRelevantData`；  
   b. **体素化** —— Recast `rcRasterizeTriangles*`；  
   c. **过滤** —— Ledge / LowHeight 过滤（`ENavigationLedgeSlopeFilterMode`）；  
   d. **CompactHeightfield → Region → Contour → PolyMesh → DetailMesh**（Recast 标准管线）；注意 `SolidHF`/`CompactHF` 是 TileGenerator 的成员字段而非 RasterizationContext 的栈对象，因此**整个 Tile 构建流程内都活着**；  
   e. **Area Marking** —— 应用 `FAreaNavModifier` 到 PolyMesh，再把 Meta Area 替换为具体 Area；  
   f. **Link 生成** —— 若 `bGenerateLinks`，调用 Detour `dtNavLinkBuilder::buildForAllEdges`（位于 `FRecastTileGenerator::BuildTileCacheLinks`）沿边缘采样投射生成跳跃链接；UE 端只传 `dtNavLinkBuilderJumpConfig` 与做结果转换（`AddGeneratedLinks` 把 JumpLink 按 up/down Area 是否相同拆成单向/双向 FGeneratedNavigationLink）。**Link 构建必须与 CompressedLayers 同帧完成**（除非从磁盘重载 TileCache），因为它依赖仍活着的 CompactHF；  
   g. **输出 Tile 数据** `FNavMeshTileData` → 主线程 `AddGeneratedTiles`。
4. 异步用 `RECAST_ASYNC_REBUILDING=1` 时走 `FAsyncTask<FRecastTileGeneratorWrapper>`，Wrapper 内部持有 `TSharedRef<FRecastTileGenerator>`；否则可走 `FNavRegenTimeSliceManager` 时间切片。
5. **时间切片实现细节** —— 每个 TileGenerator 维护多个状态枚举（`EDoWorkTimeSlicedState` / `EGenerateTileTimeSlicedState` / `EGenerateCompressedLayersTimeSliced` / `EGenerateRecastFilterTimeSlicedState` / `ERasterizeGeomTimeSlicedState` / `ERasterizeGeomRecastTimeSlicedState` / `EGenerateNavDataTimeSlicedState`），通过嵌套 fall-through `switch` 在一次 Tick 内连续推进，直到 `TestTimeSliceFinished()` 返回 true。`FNavRegenTimeSliceManager` 在 NavigationSystem 侧统一调节预算，`CalcTimeSliceDuration` 的思路是"期望在 `MaxDesiredTileRegenDuration` 秒内完成所有 `PendingDirtyTiles`，据此推算本帧应给的预算，clamp 到 `[Min, Max]TimeSliceDuration`"；稳定器为 256-sample 移动平均。

### 4.4 寻路（同步）

以 NavMesh 为例：

1. 蓝图 `FindPathToLocationSynchronously` → `UNavigationSystemV1::FindPathSync`
2. 根据 Agent 选 `ANavigationData` → 调 `ANavigationData::FindPath` → 实际走 `FindPathImplementation` 函数指针（`ARecastNavMesh` 在构造里把它指向自己的静态方法 `ARecastNavMesh::FindPath`，定义在 `RecastNavMesh.cpp:~3509`）
3. 最终落到 `FPImplRecastNavMesh::FindPath`：  
   - `findNearestPoly(Start)` / `findNearestPoly(End)`  
   - `dtNavMeshQuery::findPath` 得到多边形走廊 `PathCorridor`  
   - （可选）`FindStraightPath` 做 String Pulling 产生路径点  
   - （可选）Post-process：`OffsetFromCorners`、自定义链接标注、`ApplyFlags`
4. 返回 `FPathFindingResult`（含 `FNavPathSharedPtr`）。

### 4.5 自定义链接 (Custom Nav Links)

1. 实现 `INavLinkCustomInterface` 或使用现成 `UNavLinkCustomComponent`。
2. 组件注册 / OnRegister：  
   - 调用 `UNavigationSystemV1::RegisterCustomLink`（间接经 `UNavigationObjectRepository`）  
   - 组件被当作 `INavRelevantInterface`，链接会 **作为 Modifier** 走到 Tile 生成里 → Recast `OffMeshConnection`
3. 寻路时 `FRecastSpeciaLinkFilter::isLinkAllowed` 回调回接口 `IsLinkPathfindingAllowed`。
4. 到达链接时 `OnLinkMoveStarted`；用户可以返回 `true` 接管 Movement（爬梯/弹射等）；结束时 `OnLinkMoveFinished`。
5. 区域切换与开关策略：  
   - `SetEnabled / SetEnabledArea / SetDisabledArea` 只调用 `UNavigationSystemV1::UpdateCustomLink`，**不重建 Tile、不标脏几何**（仅改 OffMeshConnection 的 areaId/flag）。  
   - 但 `AddNavigationObstacle / ClearNavigationObstacle` 会走 `RefreshNavigationModifiers`，**Obstacle 变更仍需 Tile 重建**（因为 Obstacle 的 Box 会影响几何 Marking）。
6. **ID 生命周期**：5.3 之前的 `NavLinkUserId (uint32)` 全局自增，在 `OnPreWorldInitialization` → `ResetUniqueId` 每 World 复位到 1；5.3 之后改用 `FNavLinkId`（确定性/稳定哈希），不再需要此复位。

### 4.6 Navigation Invokers（局部生成）

1. `bGenerateNavigationOnlyAroundNavigationInvokers=true` 时生效。
2. `RegisterInvoker` 把 Actor/接口注册进 `Invokers`，缓存为 `InvokerLocations` (raw 位置+半径+优先级)。
3. 每 `ActiveTilesUpdateInterval` 秒一次 `UpdateNavDataActiveTiles`，用 Invoker 位置 + `GenerationRadius/RemovalRadius` 计算活跃 Tile 集合 → 差集驱动生成/丢弃。
   - 实现链路：`UNavigationSystemV1::UpdateNavDataActiveTiles`（NavigationSystem.cpp:6094）遍历世界中每个 `ARecastNavMesh`，调用其 `UpdateActiveTiles(InvokerLocations)`（RecastNavMesh.cpp:~4260）。**差集对比的真正逻辑在 `ARecastNavMesh::UpdateActiveTiles`**：先用 `OldActiveSet = ActiveTiles` 保存旧集合，再按 Invoker 范围算出新的 `TilesInMinDistance` / `TilesInMaxDistance`，最后产出 `TilesToRemove`（旧集合里超出 RemovalRadius 的）与 `TilesToUpdate`（新增进入 GenerationRadius 的）；通过 `RemoveTiles(TilesToRemove)` 和 `RebuildTile(TilesToUpdate)`（最终走 `Generator->RemoveTiles / ReAddTiles`）反馈给 Generator。
   - 注：`FRecastNavMeshGenerator::RestrictBuildingToActiveTiles(true)` 不参与差集计算，仅做"开关 + 把当前 NavMesh 里所有非空 Tile 一次性灌进 ActiveTileSet"作为初始集合（RecastNavMeshGenerator.cpp:6118）。
4. `DirtyTilesInBuildBounds` 在 `BuildBounds` 改变或 Invoker 迁移时触发。
5. **Seed 裁剪**：当配置了 `InvokersMaximumDistanceFromSeed` 为正值时，Invoker 要先通过 "Seed" 的 AABB 过滤。Seed 位置由 `GetInvokerSeedLocations` 提供，默认是所有 PlayerPawn 的位置（若 PlayerController 无 Pawn 则用相机位置）。这个机制在开放世界里用于避免远处 AI 携带的 Invoker 导致意外 Tile 生成。
6. **GeometryCollection 特殊路径**：`NavModifierComponent::CalcAndCacheBounds` 处理 Component → BodySetup 几何提取时，对 `UGeometryCollectionComponent` 走单独路径（因为它没有 Component 级的 BodySetup），从 `RestCollection.RootProxyData.ProxyMeshes` 逐个 StaticMesh 的 BodySetup 取几何。

---

## 5. 子模块拆分 → subagent 分派计划

按照上述 Layer，选择**依赖从下到上**的拆分方式，并保证每个 agent 拿到的文件之间关系紧密，便于单独阅读：

### Module A（基础层 / 接口 / 辅助）
- 包含 Layer 0 全部 + Layer 1 的 NavArea / NavFilter（小文件聚合）
- 文件清单：  
  `NavigationSystemTypes.h/.cpp`、`NavDebugTypes.h`、`NavNodeInterface.h`、`NavigationPathGenerator.h`、`NavLinkHostInterface.h`、`NavLinkCustomInterface.h`、`CrowdManagerBase.h`、`NavLinkTrivial.h`、`Private/NavigationInterfaces.cpp`、`NavMesh/RecastHelpers.h/.cpp`、`NavMesh/RecastVersion.h`、`NavMesh/RecastInternalDebugData.h/.cpp`、`NavMesh/LinkGenerationDebugFlags.h`

### Module B（区域与过滤器）
- `Public/NavAreas/*` + `Private/NavAreas/*`（共 14 个）
- `Public/NavFilters/*` + `Private/NavFilters/*`（共 4 个）

### Module C（导航元素容器 + NavigationData 抽象）
- `NavigationOctree.h/.cpp`、`NavigationOctreeController.h/.cpp`
- `NavigationDirtyAreasController.h/.cpp`
- `NavigationDataHandler.h/.cpp`
- `NavigationPath.h/.cpp`
- `NavCollision.h/.cpp`
- `NavigationObjectRepository.h/.cpp`（Private）
- `NavigationData.h/.cpp`、`AbstractNavData.h/.cpp`

### Module D（NavMesh / Recast）
- `NavMesh/RecastQueryFilter.h`
- `NavMesh/LinkGenerationConfig.h/.cpp`
- `NavMesh/NavMeshBoundsVolume.h/.cpp`
- `NavMesh/NavMeshPath.h/.cpp`
- `NavMesh/RecastGeometryExport.h`
- `NavMesh/PImplRecastNavMesh.h/.cpp`
- `NavMesh/RecastNavMesh.h/.cpp`
- `NavMesh/RecastNavMeshDataChunk.h/.cpp`
- `NavMesh/RecastNavMeshGenerator.h/.cpp`（超大）
- `NavMesh/NavMeshRenderingComponent.h/.cpp`
- `NavMesh/NavTestRenderingComponent.h/.cpp`

> 注：这个 Module 是所有 Module 里代码量最大的（~800KB），subagent 单次无法全部吃下。会在实施阶段再拆成 D1（查询/路径 PImpl+RecastNavMesh+NavMeshPath+Filter+Bounds+DataChunk）和 D2（生成器 Generator+Rendering+LinkConfig+GeometryExport）。

### Module E（Link / Graph / 组件 / 主系统）
- `NavGraph/*`（6 个）
- Link 系 & 组件：`NavLinkComponent.*`、`NavLinkCustomComponent.*`、`NavLinkRenderingComponent.*`、`NavLinkRenderingProxy.h`、`BaseGeneratedNavLinksProxy.h/.cpp`
- Modifier 系：`NavRelevantComponent.h/.cpp`、`NavModifierComponent.h/.cpp`、`NavModifierVolume.h/.cpp`、`SplineNavModifierComponent.h/.cpp`
- Invoker/Testing：`NavigationInvokerComponent.h/.cpp`、`NavigationTestingActor.h/.cpp`、`NavSystemConfigOverride.h/.cpp`
- 主类与模块：`NavigationSystem.h/.cpp`（超大）、`NavigationSystemModule.h/.cpp`

---

## 6. 注释规约（给自己和 subagent）

1. 所有原生英文注释**保留不动**，仅在其上方/后方追加中文注释行，或在无注释的位置新增中文注释。  
2. 不用修改任何可执行代码、不变更 `#include` 顺序、不修改空白格式，除非纯粹是注释行。  
3. 中文注释首选**行注释 `//`**；仅在需要整段说明（函数头 / 复杂循环说明 / 关键算法推导）时使用块注释 `/* ... */`。  
4. 在函数定义（cpp 的实现处）顶端加概要说明：  
   - 目的 / 调用者 / 重要副作用（Dirty / 锁 / 线程约束）
5. 每个**非平凡循环**写一行"循环不变量"或"迭代目标"。  
6. 对**分支关键条件**写一行语义注释（"为什么要这个判断"）。  
7. 成员变量在声明处写一行："存储什么、由谁维护、什么时候刷新"。  
8. 使用统一术语表（见第 2 节），避免"节点/多边形/Poly/Tile/Cell"来回换。
9. 保留 UE 的 `UPROPERTY / UFUNCTION / GENERATED_BODY` 宏不改动；中文注释放在宏所在行的上方。
10. 绝不删除已有的 `// UE_DEPRECATED / // @todo / // LWC_TODO` 等标记。

---

## 7. 已知暂未深入的细节（待注释阶段补全）

- ~~`FNavRegenTimeSliceManager` 与 `FNavRegenTimeSlicer` 的具体时间分配算法（MinDuration/MaxDuration/DesiredDuration 之间的权衡）~~ —— 已在 4.3 节补充：本质是"`MaxDesiredTileRegenDuration` 秒内完成 `PendingDirtyTiles`"的反推预算 + 256-sample 移动平均平滑。
- `ARecastNavMesh` 针对 World Partition Dynamic 模式的差异处理（`bUseWorldPartitionedDynamicMode`）
- ~~`NavLink` 生成算法中 `dtNavLinkBuilderJumpConfig` 的具体采样方式~~ —— 已在 4.3 节补充：底层采样算法由 Detour `dtNavLinkBuilder::buildForAllEdges` 完成；UE 端仅传 config 与结果转换。
- Recast 5.x 新引入的 `TileResolution`（Low/Default/High）在 Tile Layout 中的作用与序列化
- Crowd Manager 的外部实现位置（在 AIModule，本模块只留抽象基类）

## 7.1 专题补充（注释阶段新增）

### 7.1.1 ANavSystemConfigOverride 的三种策略实现差异

| 策略 | 行为 |
| --- | --- |
| `Override` | 销毁旧 NavSys → `AddNavigationSystemToWorld(bOverridePreviousNavSys=true)`。**编辑器模式**下 Initialize 延迟到下一帧（`TimerManager::SetTimerForNextTick`）；**运行时模式**同步初始化 |
| `Append` | `PrevNavSys.AppendConfig(Config)` —— 只合并 Agent 之类非冲突字段，**不**替换 NavSys 类 |
| `Skip` | 若 PrevNavSys 存在则完全跳过 |

**共同行为**：无论哪种策略都会调用 `WorldSettings.SetNavigationSystemConfigOverride(Config)` 写入，目的是"未来世界重建 NavSys 时还能拿到此 Config"。

### 7.1.2 NavGraph 当前的半成品程度

- `FNavGraphNode` 连 **坐标字段都没有**（只有 `Owner` + `Edges` 列表）；头文件里 `// Location to be added here / Radius might be needed as well` 是遗留 TODO。
- `NavGraphGenerator` 是空壳（约 40 行，全 `TODO`）。
- 真正要用图式导航，至少要先补 Node 的空间信息。
- **`UNavigationGraphNodeComponent`** 用双向链表把同一图里的节点串起来，`INavNodeInterface` 暴露给 Actor 查询所属节点组件。

### 7.1.3 NavigationSystem 相关 CVar（本模块内定义）

| CVar | 位置 | 作用 |
| --- | --- | --- |
| `ai.DestroyNavDataInCleanUpAndMarkPendingKill` | `NavigationData.cpp` | 控制 NavData 被标脏 pending kill 时是否立即 Destroy |
| `ai.navigation.MaxDirtyAreasCountWhileSuspended` | `NavigationData.cpp` | `SetRebuildingSuspended(true)` 期间的 DirtyArea 队列上限，超过会触发 RebuildAll |

## 8. 修订记录

| 版本 | 修改人 | 变更 |
| --- | --- | --- |
| 0.1 | 初版 | 扫描公开头文件产出骨架 |
| 0.2 | 5 个 subagent 并行注释反馈 | 补充：Filter 默认值与性能代价、AreaFlags=0 约定、FNavLinkId 双轨制、TileGenerator 异步 Wrapper、Link 生成走 dtNavLinkBuilder、TimeSlice 状态机细节、Invoker Seed 裁剪、ANavSystemConfigOverride 三策略、NavGraph 半成品程度、模块内 CVar 清单 |
| 0.3 | 自检阶段 | 修正：(1) FindPathImplementation 函数指针实际指向 `ARecastNavMesh::FindPath`（之前错误声明 `FRecastNavMeshGenerator::FindPathRecast`，该函数不存在）；(2) UpdateNavDataActiveTiles 的差集逻辑实际在 `ARecastNavMesh::UpdateActiveTiles` 而非直接在 NavSys 里；(3) 同时修正 RecastNavMesh.h 文件头中文注释里 `Cell*/Agent*/` 引发的视觉混淆；(4) 修正若干文件头里行数声明不准（NavigationSystem.cpp、PImplRecastNavMesh.cpp、RecastNavMeshGenerator.cpp）；(5) Filter 注释中"按单位性格"修正为"按 Pawn/AI 个体"；(6) NavSystemConfigOverride.cpp 注释中"我们的 Config"修正为"本 Override Actor 的 Config" |
| 0.4 | 二轮自检阶段 | 发现并补全：(1) PImplRecastNavMesh.cpp 函数级注释覆盖率从 7.1% 提升到 100%（新增 65 个核心查询/Tile/Poly API 注释，包括 FindNearestPoly / FindMoveAlongSurface / ProjectPointToNavMesh / GetPolyVerts / GetPolyNeighbors / GetDebugGeometryForTile / Serialize 系列等）；(2) NavigationSystem.cpp 函数级注释从 17.7% 提升到 82.3%（新增 142 个核心函数注释，覆盖 P0 级所有 API 与 P1 级编辑器/调试钩子）；(3) RecastNavMeshGenerator.cpp 从 55% 提升到 72.5%（新增 16 个 Tile 构建主流程核心函数：CreateHeightField / GenerateRecastFilter / BuildCompactHeightField / RasterizeGeometryRecast / GenerateCompressedLayers / GenerateNavigationData / MarkDynamicArea / MarkRasterizationMask / MarkNavBoundsDirty 等）；(4) 全模块总中文字符数 60869 → 68044（增加 7175） |
| 0.5 | 三轮自检阶段 | (1) 修正章节编号：7.5 / 7.5.x → 7.1 / 7.1.x（保持 7 节下子节连续编号）；(2) 修正 PImplRecastNavMesh.cpp 文件头注释 "~3800 行" → "~4000 行"（注释膨胀后实际 3984 行）；(3) 全面验证：105 文件无注释错位 / 121 个 ClassName::Func 引用全部在代码中存在 / Markdown 标题层级与代码块成对 / 抽 5 处"返回 X"声明与实际 return 完全一致（NavigationDataHandler::UpdateNavOctreeElementBounds 的 false/true 三分支等）/ 重复注释扫描零命中 / 大文件 cpp 中"中文注释段紧接非声明行"经验证均为函数体内步骤注释（无错位）|

---

（注：本文件与 DevDocs/How To Optimize Navmesh Generation.md 配合阅读，前者讲"模块在做什么"，后者讲"生成慢了怎么调"。）
