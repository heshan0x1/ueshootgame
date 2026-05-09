# MassEntity 框架架构文档

> 目标：帮助读者系统性理解 Unreal Engine 中 MassEntity（Mass ECS）框架的总体架构、核心类型、执行模型与模块边界。
>
> 阅读本文时建议配合源码中的中文注释（本文档同步生成）一起阅读。所有带有 `file.ext:line` 形式的引用都可以直接在 IDE 中跳转。

---

## 0. 这是什么？

**MassEntity** 是 Unreal Engine 的 **Archetype 风格 ECS (Entity-Component-System) 框架**。它提供的是一套「数据驱动、面向批量处理」的游戏对象模型，典型应用场景是需要同时处理 **大量轻量实体**（AI、群体、物理代理、装饰物等）而 Actor/Component 模型开销过高的情形。

与经典 Actor/Component 体系的最大区别：

| 视角 | Actor/Component | MassEntity |
|------|-----------------|------------|
| 身份 | `AActor*`（UObject，带 GC） | `FMassEntityHandle`（`Index + Serial` 的 8 字节值类型） |
| 数据 | 分散在 UObject 链 | 按 Archetype 聚合的列式 Chunk 存储 |
| 逻辑 | 每 Actor 每 Tick 各自调度 | `UMassProcessor` 对整类实体批量执行 |
| 访问 | 指针 + 虚函数 | `FMassEntityQuery` 声明需求 + `FMassExecutionContext` 取视图 |
| 调度 | Tick 依赖手写 | 依赖求解器按读/写冲突自动排序 |

核心思想可归纳为三句话：
1. **实体只是 ID**。实体本身没有字段，只是一个轻量 Handle。
2. **数据按 Archetype 分桶**。拥有**完全相同元素集合**（fragments + tags + ...）的实体共享一个 `FMassArchetypeData`，数据按 Chunk 列式存放。
3. **逻辑按需声明数据依赖**。`UMassProcessor` 通过 `FMassEntityQuery` 声明 "我需要哪些 fragment、以什么访问权限"；框架据此匹配 archetype、求解依赖、调度执行。

---

## 1. 核心概念总览

### 1.1 Entity（实体）
- 数据结构：`FMassEntityHandle = { int32 Index, int32 SerialNumber }`
- 全部通过 `FMassEntityManager` 分配/销毁；`Index` 为 0 保留为无效 handle。
- SerialNumber 用于防止 Index 复用造成的"悬垂引用"。
- 详见：`Public/MassEntityHandle.h`

### 1.2 Element（元素）— 五大基类
所有能挂到实体上的数据/标记类型，都必须继承下列五个基类之一：

| 基类 | 用途 | 存储位置 |
|------|------|----------|
| `FMassFragment` | 普通数据组件（位置、速度等） | 按 entity 存储于 chunk 列 |
| `FMassTag` | 纯标记（无数据） | 仅参与 archetype 识别，不占空间 |
| `FMassChunkFragment` | 一个 chunk 一份的数据（缓存、LOD 等） | 每 chunk 一份 |
| `FMassSharedFragment` | 多个 archetype 共享、可写 | 引用式 |
| `FMassConstSharedFragment` | 多个 archetype 共享、只读 | 引用式 |

详见：`Public/MassEntityElementTypes.h`

### 1.3 Archetype（原型）
- **定义**：由「Fragments ∪ Tags ∪ ChunkFragments ∪ SharedFragments ∪ ConstSharedFragments」**完全相同**的一组实体构成的桶。
- **存储**：`FMassArchetypeData` 内含若干 `FMassArchetypeChunk`。每个 chunk 是一块连续字节，按 fragment 类型划分为列。
- **身份**：组合描述符 `FMassArchetypeCompositionDescriptor` 的哈希决定唯一性；`FragmentHashToArchetypeMap` 去重。
- **句柄**：`FMassArchetypeHandle`（`TSharedPtr<FMassArchetypeData>` 包装），`FMassArchetypeVersionedHandle` 附带版本号用于失效检测。
- 详见：`Internal/MassArchetypeData.h`, `Public/MassArchetypeTypes.h`

### 1.4 BitSet（位集）
- 每一种 Fragment/Tag/... 被分配一个全局位索引（由 `FStructTracker` 托管）。
- `FMassFragmentBitSet` / `FMassTagBitSet` / ... 都是位集，支持 O(1) 的"存在/相等/差集/并集"。
- 这是 archetype 匹配、requirement 求值的性能关键。
- 详见：`Public/TypeBitSetBuilder.h`, `Public/MassBitSetRegistry.h`, `Public/MassEntityTypes.h`

### 1.5 Query（查询）+ Requirements（需求）
- `FMassFragmentRequirements` 声明："我需要/可选/禁止哪些 fragment/tag，以什么访问权限"。
  - 访问权限：`EMassFragmentAccess = { None, ReadOnly, ReadWrite }`
  - 存在性：`EMassFragmentPresence = { All, Any, None, Optional }`
- `FMassSubsystemRequirements` 声明对 `USubsystem` 的读/写需求（`USubsystem` 无法进入 bitset 机制，但借助 traits 做 game-thread/thread-safe 分类）。
- `FMassEntityQuery` = Fragment Requirements + Subsystem Requirements + 缓存（匹配到的 archetype 集合、需求→fragment 索引映射表）+ 执行入口（`ForEachEntityChunk`）。
- 详见：`Public/MassRequirements.h`, `Public/MassEntityQuery.h`

### 1.6 Execution Context（执行上下文）
- `FMassExecutionContext` 是所有 processor 回调里看到的"当前 chunk 的视图"对象。
- 它持有：
  - 当前 chunk 的 fragment 视图（`TArrayView<T>`、`GetMutableFragmentView<T>()` 等）
  - 缓存好的 subsystem 指针（`FMassSubsystemAccess`）
  - 待提交的命令缓冲 `DeferredCommandBuffer`
  - 当前迭代的实体列表（`FEntityIterator` 可直接 range-for）
- 详见：`Public/MassExecutionContext.h`

### 1.7 Processor（处理器）
- `UMassProcessor` 是 `UObject`（可配置、可 GC、可蓝图扩展）。
- 两种使用方式：
  1. **手动模式**：重写 `ConfigureQueries()` 构造 `FMassEntityQuery`，重写 `Execute()` 调用 `Query.ForEachEntityChunk(...)`。
  2. **声明模式**：使用 `FQueryExecutor` / `FQueryDefinition<Ts...>`（`AutoExecuteQuery` 字段），由框架生成绑定。
- `UMassCompositeProcessor` 是容器型 processor，内部持有 `FMassRuntimePipeline`（子 processor 数组）和依赖解算后的 `FlatProcessingGraph`。
- 详见：`Public/MassProcessor.h`

### 1.8 Phase（阶段）与调度
- 6 个固定阶段（对应 `ETickingGroup`）：
  - `PrePhysics`、`StartPhysics`、`DuringPhysics`、`EndPhysics`、`PostPhysics`、`FrameEnd`
- 每个阶段内部是一个 `UMassCompositeProcessor`，其 `FlatProcessingGraph` 是由 `FMassProcessorDependencySolver` 按以下规则求解得到的 DAG：
  1. 显式 `ExecuteBefore/ExecuteAfter` 声明
  2. 读写冲突（RW → R 需要在 W 之后）
  3. 组（`ExecuteInGroup`）边界
  4. 执行优先级
- 运行时由 `FMassProcessingPhaseManager` 通过 `FTickFunction` 触发，支持并行模式 (`TriggerPhase` 返回 `FGraphEventRef`)。
- 详见：`Public/MassProcessorDependencySolver.h`, `Public/MassProcessingPhaseManager.h`

### 1.9 Command Buffer（命令缓冲）
- 在 processor 执行过程中，**不允许直接修改结构**（增删 fragment/tag/entity），否则会破坏正在迭代的 archetype 布局。
- 解决方案：所有结构变更经由 `FMassCommandBuffer` 延迟提交。
  - `Context.Defer().AddFragment<T>(...)`、`Context.Defer().DestroyEntity(...)` 等
  - Flush 时按 `EMassCommandOperationType` 排序：Create → Add → Remove → ChangeComposition → Set → Destroy
- 线程绑定：每个 buffer 有 owner thread；`ParallelFor` 场景下需 `ForceUpdateCurrentThreadID`。
- 详见：`Public/MassCommandBuffer.h`, `Public/MassCommands.h`

### 1.10 Observer（观察者）
- 对"某 fragment/tag 被添加/移除/实体被创建/销毁"做出响应的特殊 processor。
- `UMassObserverProcessor` 继承 `UMassProcessor`，声明 `ObservedType` 和 `ObservedOperations`，自动注册到 `UMassObserverRegistry`。
- `FMassObserverManager` 内嵌在 `FMassEntityManager` 里，执行回调；支持 `FObserverLock` 将一段代码内的多次变更合并为一次通知。
- 详见：`Public/MassObserverManager.h`, `Public/MassObserverProcessor.h`, `Public/MassObserverRegistry.h`

### 1.11 Relation（关系）
- MassEntity 特有的"实体与实体之间的链接"机制，**关系本身也是一个实体**。
- 一个 relation 类型由 `FRelationTypeTraits` 描述（关系 tag 类型、fragment 类型、subject/object 角色配置、观察者类等）。
- `FRelationManager` 管理关系数据；`UMassRelationObserver` 在创建/销毁/变更 relation 实体时维护内部索引。
- 内置示例：`FMassChildOfRelation`（父子关系，`Public/Relations/MassChildOf.h`）。
- 详见：`Public/MassEntityRelations.h`, `Public/MassRelationManager.h`

### 1.12 Subsystem（子系统集成）
- `UMassEntitySubsystem : UMassSubsystemBase (UWorldSubsystem)` 是每个 UWorld 持有的默认 `FMassEntityManager` 宿主。
- `UMassSubsystemBase` 为所有 Mass 相关 subsystem 提供统一的 traits 注册、延后创建（Gameplay Feature Actions）能力。
- 详见：`Public/MassEntitySubsystem.h`, `Public/MassSubsystemBase.h`

---

## 2. 模块划分与拓扑顺序

框架内部根据依赖关系划分为以下 16 个模块。阅读/分析/修改时建议按照此顺序（编号越小越基础）：

| # | 模块 | 角色 |
|---|------|------|
| **M0** | Foundations | 基础 USTRUCT 标记、C++20 concepts、traits、log/ensure 宏。无内部依赖。 |
| **M1** | Core Types / BitSets / Trace | `FMassEntityHandle`、位集基础设施、`FMassArchetypeCompositionDescriptor`、`FMassArchetypeSharedFragmentValues`、`FArchetypeGroupType`、trace hooks。 |
| **M2** | Archetype & Storage | `FMassArchetypeData`（Chunk + 列式存储）、`IEntityStorageInterface`（单线程/并发两种存储后端）。 |
| **M3** | Processing Types | `EProcessorExecutionFlags`、`FMassRuntimePipeline`、`EMassProcessingPhase`、`FMassProcessorOrderInfo`。 |
| **M4** | Requirements & Query | `FMassFragmentRequirements`、`FMassSubsystemRequirements`、`FMassEntityQuery`、R/W 访问检测器、`FQueryExecutor`。 |
| **M5** | Execution Context | `FMassSubsystemAccess`、`FMassExecutionContext`（processor 回调里看到的视图对象）。 |
| **M6** | Entity Manager & Views | `FMassEntityManager`（全局 Hub）、`FMassEntityView`（单实体访问器）、`FEntityCollection`、通用 Utils。 |
| **M7** | Command Buffer | `FMassCommandBuffer`、`FMassBatchedCommand` 家族、`EMassCommandOperationType` 的排序规则。 |
| **M8** | Observer | `FMassObserverManager`（内嵌于 EntityManager）、`UMassObserverProcessor`、`UMassObserverRegistry`、buffered notification。 |
| **M9** | Processor & Scheduling | `UMassProcessor`、`UMassCompositeProcessor`、`FMassProcessorDependencySolver`、`FProcessingContext`、`Executor::Run`、`FMassProcessingPhaseManager`。 |
| **M10** | Entity Builder | `UE::Mass::FEntityBuilder`——流式构建单实体的高级 API。 |
| **M11** | Type Manager & Subsystems | `UE::Mass::FTypeManager`、`UMassSubsystemBase`、`UMassEntitySubsystem`。 |
| **M12** | Relations | `FMassRelation`、`FRelationManager`、`UMassRelationObserver`。 |
| **M13** | Settings & Module | `UMassSettings`、`UMassEntitySettings`（`UDeveloperSettings`）、`FMassEntityModule`。 |
| **M14** | Debug & Diagnostics | `FMassDebugger`、`FBreakpoint`、日志/Insights trace。 |
| **M15** | Built-in Relations | `FMassChildOfRelation`。 |

### 2.1 拓扑依赖图（简化）

```
            M0 Foundations
                 │
        ┌────────┴────────┐
        ▼                 ▼
  M1 CoreTypes        M3 ProcTypes
        │                 │
        └────────┬────────┘
                 ▼
          M2 Archetype
                 │
                 ▼
       M4 Requirements/Query
                 │
                 ▼
       M5 ExecutionContext
                 │
                 ▼
       M7 CommandBuffer ◄─── M8 Observer
                 │                │
                 └────────┬───────┘
                          ▼
                M11 TypeManager (FTypeManager)
                          │
                          ▼
                  M12 Relations (核心)
                          │
                          ▼
                M6 EntityManager & Views
                          │
              ┌───────────┼───────────┐
              ▼           ▼           ▼
          M10 Builder  M9 Processor  M11 UObject Subs
                          │
                          ▼
                 M12 RelationObservers
                          │
                          ▼
                 M15 ChildOf, M13 Settings
                          │
                          ▼
                   M14 Debug（横切）
```

### 2.2 每个模块的源文件清单

详见本文档 **§4 源文件索引**。

---

## 3. 关键执行流程

### 3.1 创建实体
```
用户代码
 └─> FMassEntityManager::CreateEntity(composition, shared_values)
      │
      ├─> 计算 FMassArchetypeCompositionDescriptor
      ├─> 查 FragmentHashToArchetypeMap 找/建 FMassArchetypeData
      ├─> IEntityStorageInterface::AcquireEntityIndex() 拿到 {Index, Serial}
      ├─> FMassArchetypeData::AddEntity() 在 chunk 里分配槽位
      └─> FMassObserverManager::OnPostEntitiesCreated() 通知 observer
```

### 3.2 删除实体（立即）
```
FMassEntityManager::DestroyEntity
 ├─> FMassObserverManager::OnPreEntitiesDestroyed
 ├─> FMassArchetypeData::RemoveEntity()  ← 把末尾 entity 搬到空位上
 └─> IEntityStorageInterface::ReleaseEntityIndex()
```

### 3.3 延迟结构变更（processor 内部）
```
processor Execute()
 └─> Context.Defer().AddFragment<T>(entity)      // 仅入队
 ...
processor 结束后
 └─> FMassEntityManager::FlushCommands()
       └─> 按 EMassCommandOperationType 排序执行
            ├─> Create
            ├─> Add        ← 会触发 OnPostCompositionAdded observer
            ├─> Remove     ← 会触发 OnPreCompositionRemoved observer
            ├─> Set
            └─> Destroy
```

### 3.4 查询匹配与遍历
```
Query.CacheArchetypes()
 └─> 遍历 EntityManager.AllArchetypes
      └─> FMassFragmentRequirements::DoesArchetypeMatchRequirements(Archetype.Composition)
           ├─> RequiredAll ⊆ composition
           ├─> RequiredAny ∩ composition ≠ ∅
           ├─> RequiredNone ∩ composition = ∅
           └─> ... tags / chunk / shared / const shared 同理

Query.ForEachEntityChunk(Context, UserLambda)
 └─> for each matched archetype
      └─> for each chunk
           ├─> Context.ApplyFragmentRequirements()  // 建立 TArrayView<T>
           └─> UserLambda(Context)                   // 用户代码
```

### 3.5 阶段调度
```
UWorld Tick
 └─> FMassProcessingPhaseManager::FTickFunction (PrePhysics 等)
      └─> FMassProcessingPhase::Execute
           └─> UMassCompositeProcessor::Execute
                └─> walk FlatProcessingGraph (已由 DependencySolver 拓扑排序)
                     └─> for each node:
                          ├─ if node == processor:
                          │    └─ DispatchProcessorTasks / Execute
                          └─ if node == group:
                               └─ 递归
```

### 3.6 依赖求解
```
FMassProcessorDependencySolver::ResolveDependencies
 ├─> 为每个 UMassProcessor 收集 FMassExecutionRequirements
 │    (fragment R/W、subsystem R/W、tag filter)
 ├─> 按 ExecuteBefore/After 建静态边
 ├─> 对每个共享的 fragment/subsystem 建 RW 边（W→R、W→W、R→W 都要串行）
 ├─> 拓扑排序 → FlatProcessingGraph
 └─> 计算 MaxSequenceLength（供并行 dispatch 使用）
```

---

## 4. 源文件索引

> 路径相对于 `Engine/Source/Runtime/MassEntity/`。

### M0 — Foundations
| 文件 | 说明 |
|------|------|
| `Public/MassEntityMacros.h` | `WITH_MASSENTITY_DEBUG` 等宏开关 |
| `Public/MassTestableEnsures.h` | 套 AITestSuite 的 `ensureMsgf` 包装 |
| `Public/MassEntityElementTypes.h` | `FMassFragment`/`FMassTag`/... 五大基类 |
| `Public/MassEntityConcepts.h` | C++20 concepts：`CFragment`、`CTag` 等 |
| `Public/MassExternalSubsystemTraits.h` | `TMassExternalSubsystemTraits<T>`（game-thread / thread-safe） |
| `Public/MassDebugLogging.h` | 调试日志宏 |

### M1 — Core Types / BitSets / Trace
| 文件 | 说明 |
|------|------|
| `Public/MassEntityHandle.h` | `FMassEntityHandle`（8 字节身份） |
| `Public/TypeBitSetBuilder.h` | 模板 bitset 构建器 |
| `Public/MassBitSetRegistry.h` | 位集注册表、per-type `FStructTracker` |
| `Private/MassBitSetRegistry.cpp` | 同上实现 |
| `Public/MassArchetypeGroup.h` | `FArchetypeGroupType` / `Handle` / `Groups` |
| `Private/MassArchetypeGroup.cpp` | 同上实现 |
| `Public/MassEntityTypes.h` | `FMassArchetypeCompositionDescriptor`、`FMassArchetypeSharedFragmentValues`、位集声明 |
| `Private/MassEntityTypes.cpp` | 同上实现 |
| `Public/MassEntityTrace.h` | Insights trace 工具 |
| `Private/MassEntityTrace.cpp` | 同上实现 |
| `MassEntity.natvis` | VS 调试器可视化 |

### M2 — Archetype & Storage
| 文件 | 说明 |
|------|------|
| `Public/MassArchetypeTypes.h` | `FMassArchetypeHandle`、`FMassArchetypeEntityCollection`、ChunkIterator |
| `Private/MassArchetypeTypes.cpp` | 同上实现 |
| `Internal/MassArchetypeData.h` | `FMassArchetypeChunk`、`FMassArchetypeData`（**内部头**） |
| `Private/MassArchetypeData.cpp` | 同上实现（巨型文件） |
| `Public/MassEntityManagerStorage.h` | `IEntityStorageInterface`、单线程/并发存储后端 |
| `Private/MassEntityManagerStorage.cpp` | 同上实现 |

### M3 — Processing Types
| 文件 | 说明 |
|------|------|
| `Public/MassProcessingTypes.h` | `FMassRuntimePipeline`、`EMassProcessingPhase`、`FMassProcessorOrderInfo` |
| `Private/MassProcessingTypes.cpp` | 同上实现 |

### M4 — Requirements & Query
| 文件 | 说明 |
|------|------|
| `Public/MassRequirements.h` | `FMassFragmentRequirements`、`FMassSubsystemRequirements` |
| `Private/MassRequirements.cpp` | 同上实现 |
| `Public/MassEntityQuery.h` | `FMassEntityQuery` |
| `Private/MassEntityQuery.cpp` | 同上实现 |
| `Public/MassRequirementAccessDetector.h` | 调试期 R/W 访问检测器 |
| `Private/MassRequirementAccessDetector.cpp` | 同上实现 |
| `Public/MassQueryExecutor.h` | `FQueryExecutor` / `FQueryDefinition<Ts...>` |
| `Private/MassQueryExecutor.cpp` | 同上实现 |

### M5 — Execution Context
| 文件 | 说明 |
|------|------|
| `Public/MassSubsystemAccess.h` | 缓存 `USubsystem*`（5 种派生） |
| `Private/MassSubsystemAccess.cpp` | 同上实现 |
| `Public/MassExecutionContext.h` | `FMassExecutionContext`、`FEntityIterator` |
| `Private/MassExecutionContext.cpp` | 同上实现 |

### M6 — Entity Manager & Views
| 文件 | 说明 |
|------|------|
| `Public/MassEntityManager.h` | `FMassEntityManager`（框架心脏） |
| `Private/MassEntityManager.cpp` | 同上实现（巨型文件） |
| `Public/MassEntityView.h` | 单实体访问器 |
| `Private/MassEntityView.cpp` | 同上实现 |
| `Public/MassEntityCollection.h` | 多实体 handle 累积器 |
| `Private/MassEntityCollection.cpp` | 同上实现 |
| `Public/MassEntityUtils.h` | 自由函数工具集 |
| `Private/MassEntityUtils.cpp` | 同上实现 |

### M7 — Command Buffer
| 文件 | 说明 |
|------|------|
| `Public/MassCommandBuffer.h` | `FMassCommandBuffer` |
| `Private/MassCommandBuffer.cpp` | 同上实现 |
| `Public/MassCommands.h` | `FMassBatchedCommand` 家族（纯模板） |
| `Public/MassArchetypeGroupCommands.h` | 分组命令 |
| `Public/MassRelationCommands.h` | 关系命令 |

### M8 — Observer
| 文件 | 说明 |
|------|------|
| `Public/MassObserverNotificationTypes.h` | `FObserverLock`、`FCreationContext` 等 |
| `Private/MassObserverNotificationTypes.cpp` | 同上实现 |
| `Public/MassObserverManager.h` | `FMassObserverManager` |
| `Private/MassObserverManager.cpp` | 同上实现（大文件） |
| `Public/MassObserverProcessor.h` | `UMassObserverProcessor` 基类 |
| `Private/MassObserverProcessor.cpp` | 同上实现 |
| `Public/MassObserverRegistry.h` | `UMassObserverRegistry` CDO |
| `Private/MassObserverRegistry.cpp` | 同上实现 |

### M9 — Processor & Scheduling
| 文件 | 说明 |
|------|------|
| `Public/MassProcessor.h` | `UMassProcessor`、`UMassCompositeProcessor` |
| `Private/MassProcessor.cpp` | 同上实现 |
| `Public/MassProcessorDependencySolver.h` | `FMassProcessorDependencySolver` |
| `Private/MassProcessorDependencySolver.cpp` | 同上实现（大文件） |
| `Public/MassProcessingContext.h` | `UE::Mass::FProcessingContext` |
| `Private/MassProcessingContext.cpp` | 同上实现 |
| `Public/MassExecutor.h` | `UE::Mass::Executor::Run` 系列 |
| `Private/MassExecutor.cpp` | 同上实现 |
| `Public/MassProcessingPhaseManager.h` | `FMassProcessingPhaseManager` |
| `Private/MassProcessingPhaseManager.cpp` | 同上实现 |

### M10 — Entity Builder
| 文件 | 说明 |
|------|------|
| `Public/MassEntityBuilder.h` | `UE::Mass::FEntityBuilder` |
| `Private/MassEntityBuilder.cpp` | 同上实现 |

### M11 — Type Manager & UObject Subsystems
| 文件 | 说明 |
|------|------|
| `Public/MassTypeManager.h` | `FTypeManager`、`FTypeHandle`、类型 traits |
| `Private/MassTypeManager.cpp` | 同上实现 |
| `Public/MassSubsystemBase.h` | `UMassSubsystemBase`/`Tickable` 基类 |
| `Private/MassSubsystemBase.cpp` | 同上实现 |
| `Public/MassEntitySubsystem.h` | `UMassEntitySubsystem`（Mass Hub 的 UWorld 宿主） |
| `Private/MassEntitySubsystem.cpp` | 同上实现 |

### M12 — Relations
| 文件 | 说明 |
|------|------|
| `Public/MassEntityRelations.h` | 关系概念、`FMassRelation` 基类等 |
| `Private/MassEntityRelations.cpp` | 同上实现 |
| `Public/MassRelationManager.h` | `FRelationManager` |
| `Private/MassRelationManager.cpp` | 同上实现 |
| `Public/MassRelationObservers.h` | 默认关系 observer 家族 |
| `Private/MassRelationObservers.cpp` | 同上实现 |

### M13 — Settings & Module
| 文件 | 说明 |
|------|------|
| `Public/MassSettings.h` | `UMassSettings` / `UMassModuleSettings` 基类 |
| `Private/MassSettings.cpp` | 同上实现 |
| `Public/MassEntitySettings.h` | `UMassEntitySettings` |
| `Private/MassEntitySettings.cpp` | 同上实现 |
| `Private/MassEntityModule.cpp` | `IMPLEMENT_MODULE` |
| `Private/MassEntityManagerConstants.h` | 内部常量 |
| `MassEntity.Build.cs` | 模块构建脚本 |

### M14 — Debug & Diagnostics
| 文件 | 说明 |
|------|------|
| `Public/MassDebugger.h` | `FMassDebugger` 静态接口 |
| `Private/MassDebugger.cpp` | 同上实现（巨型文件） |
| `Public/MassDebuggerBreakpoints.h` | `FBreakpoint`、`DebugBreak` hook |
| `Private/MassDebuggerBreakpoints.cpp` | 同上实现 |

### M15 — Built-in Relations
| 文件 | 说明 |
|------|------|
| `Public/Relations/MassChildOf.h` | `FMassChildOfRelation` |
| `Private/Relations/MassChildOf.cpp` | 同上实现 |

---

## 5. 常见使用模式

### 5.1 定义一个 Fragment
```cpp
USTRUCT()
struct FMyVelocityFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Velocity = FVector::ZeroVector;
};
```

### 5.2 写一个 Processor
```cpp
UCLASS()
class UMyMoveProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UMyMoveProcessor()
    {
        ProcessingPhase = EMassProcessingPhase::PrePhysics;
    }

protected:
    FMassEntityQuery Query;

    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override
    {
        Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
        Query.AddRequirement<FMyVelocityFragment>(EMassFragmentAccess::ReadOnly);
        Query.RegisterWithProcessor(*this);
    }

    virtual void Execute(FMassEntityManager& Manager, FMassExecutionContext& Context) override
    {
        Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
        {
            const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
            const TConstArrayView<FMyVelocityFragment> Velocities = Ctx.GetFragmentView<FMyVelocityFragment>();
            const float DT = Ctx.GetDeltaTimeSeconds();

            for (int32 i = 0; i < Ctx.GetNumEntities(); ++i)
            {
                Transforms[i].GetMutableTransform().AddToTranslation(Velocities[i].Velocity * DT);
            }
        });
    }
};
```

### 5.3 使用 EntityBuilder 建实体
```cpp
UE::Mass::FEntityBuilder Builder(EntityManager);
Builder.Add<FTransformFragment>()
       .Add<FMyVelocityFragment>()
       .Add<FMyHealthFragment>(FMyHealthFragment{100.f})  // 直接带初值
       .Commit();
FMassEntityHandle Handle = Builder;
```

### 5.4 使用 CommandBuffer 延迟修改
```cpp
// 在 processor 内部
Context.Defer().AddTag<FFrozenTag>(Entity);
Context.Defer().RemoveFragment<FMyVelocityFragment>(Entity);
// processor 结束后，EntityManager 会 FlushCommands()
```

---

## 6. 注意事项（反模式 / 陷阱）

1. **不要在 processor 内部直接调用 `EntityManager` 的结构修改接口**（`CreateEntity`/`AddFragment` 等），务必走 `Context.Defer()`。否则会破坏正在迭代的 archetype。
2. **`FMassEntityHandle` 没有 RAII**，不会自动持有引用。必须手动 `DestroyEntity`。
3. **`FMassArchetypeHandle` 是 `TSharedPtr` 包装**，比较时用 `==`。
4. **Fragment 结构体本身不会被 GC**；如果包含 `UObject*`，记得 `UPROPERTY()` 以便参与引用收集（`FMassEntityManager::AddReferencedObjects` 会遍历）。
5. **`FMassExecutionContext` 只在当前 chunk 内有效**；`for` 循环切到下一个 chunk 时，`GetMutableFragmentView<T>()` 返回的 `TArrayView` 就失效了。
6. **并行查询 (`ParallelForEachEntityChunk`) 需要确保 lambda 里所有操作都是线程安全的**；默认 `bAllowParallelCommands=false` 时 Defer 会报错。
7. **Observer 的递归**：observer 内部再修改 composition 会触发嵌套通知，`FObserverLock` 可以把这些合并成一次。

---

## 7. 术语对照（中英对照表）

| 英文 | 中文 | 说明 |
|------|------|------|
| Entity | 实体 | 通过 `FMassEntityHandle` 索引的数据聚合体 |
| Archetype | 原型 | 具有完全相同元素组合的实体桶 |
| Fragment | 片段 / 数据组件 | 附着在实体上的数据块 |
| Tag | 标签 | 无数据的标记，仅用于分类 |
| Chunk Fragment | Chunk 片段 | 每个 chunk 一份的数据 |
| Shared Fragment | 共享片段 | 多 archetype 共享的可写数据 |
| Const Shared Fragment | 只读共享片段 | 共享且只读的数据（如配置） |
| Processor | 处理器 | 对实体批量执行逻辑的 UObject |
| Composite Processor | 复合处理器 | 内部包含子 processor 的组 |
| Query | 查询 | 声明数据需求的对象 |
| Requirement | 需求 | 查询的组成部分（需要哪些 fragment、访问权限） |
| Execution Context | 执行上下文 | processor 回调里看到的 chunk 视图 |
| Command Buffer | 命令缓冲 | 延迟提交结构变更的队列 |
| Observer | 观察者 | 对组合变更做出响应的特殊 processor |
| Processing Phase | 处理阶段 | 6 个固定 tick 阶段之一 |
| Dependency Solver | 依赖求解器 | 根据读/写和显式顺序生成 DAG |
| Relation | 关系 | 实体之间的带类型链接，关系本身也是实体 |

---

## 8. 性能与并发模型（注释过程中梳理）

> 本节内容是在为全部 93 个源文件添加注释的过程中，根据代码细节归纳出的"易被忽视但关键"的系统行为。

### 8.1 缓存失效心跳：`ArchetypeDataVersion`

`FMassEntityManager::ArchetypeDataVersion` 是一个全局单调递增计数器，它的不变量是：

```
AllArchetypes.Num() == ArchetypeDataVersion
```

每次 `CreateArchetype` 创建新 archetype 都会 `++ArchetypeDataVersion`。这一计数器是**整个查询/调度系统增量更新的基石**：

- `FMassEntityQuery` 缓存上次扫到的 version；常规帧只是 O(1) 比较。仅当版本号变化才走 O(NewArchetypes × Requirements) 的增量匹配。
- `FMassProcessorDependencySolver::FResult` 也保存 `ArchetypeDataVersion`，`IsResultUpToDate` 比对版本号决定是否需要重新求解依赖图。
- `FMassProcessingPhaseManager` 监听 `OnNewArchetypeEvent`，新 archetype 出现时把所有 phase 的 `FPhaseGraphBuildState` 标记为 dirty。
- 调试用 `DebugForceArchetypeDataVersionBump()` 可强制使所有 query 缓存失效（无需真正创建 archetype）。

**关键点**：`AllArchetypes` 数组**仅追加，永不收缩**。即使某 archetype 全部 entity 都没了，元数据也保留以备复用——这是版本号语义的代价。

### 8.2 双 CommandBuffer 与 Observer 重入

`FMassEntityManager::DeferredCommandBuffers[2]` 不是简单的"双 buffer 平摊压力"，而是为了支持 **observer 在 flush 期间 push 新命令**：

```
Flush 开始
  ├─ 切换 OpenedCommandBufferIndex 到对面 buffer
  ├─ 逐条 apply 当前 buffer 的命令
  │     └─ apply 触发 observer
  │           └─ observer 调 Defer().Push 自动落到对面 buffer
  ├─ 当前 buffer 清空，检查对面 buffer
  └─ 对面非空 → 再来一轮（最多 5 轮，防 observer 死循环）
```

- `bCommandBufferFlushingInProgress` 防 flush 重入。
- 超 5 轮 log Error。
- `FMassCommandBuffer` 内部缓存 owner thread ID，仅 GameThread 可 push。
- ParallelFor 内每个 worker 一个独立 buffer，结束后 `MoveAppend` 合并到主 buffer（`AppendedCommandInstances` 不去重）。
- Fork 后 child 进程必须用 `OnPostFork` 重置 cached thread ID。

### 8.3 Flush 排序的 7 个阶段

`MassCommandBuffer.cpp::CommandTypeOrder` 按 `EMassCommandOperationType` 排序：

| Order | 操作 | 含义 |
|------:|------|------|
| 0 | Create | 先建实体，让后续 Add 能找到 |
| 2 | Add | 加 fragment/tag |
| 3 | ChangeComposition | 改组合 |
| 4 | Set | 写值（必须晚于 Add） |
| 6 | Remove | 移除 fragment/tag |
| 6 | Destroy | 销毁实体（与 Remove 同序，靠原 push 顺序定夺） |
| MAX-1 | None | 无效 |

**Set 排在 Add 之后**：`AddFragmentInstance` 命令（继承自 `FMassCommandAddFragmentInstances`）实际是先把 fragment 加到组合再写值——保证"加完即写"语义。

### 8.4 Archetype 自动去重 = 双索引

`FMassEntityManager` 维护两条索引：

| 索引 | 用途 |
|------|------|
| `FragmentHashToArchetypeMap` | `composition + groups` 哈希 → archetype（去重核心，hash 冲突走 `IsEquivalent` 二次比较） |
| `FragmentTypeToArchetypeMap` | fragment 类型 → 包含它的所有 archetype（query 早期"任意-X"快速预筛） |

两索引在每次 `CreateArchetype` 时同时维护。

### 8.5 Chunk 内存布局（SoA）

`FMassArchetypeChunk` 字节缓冲布局：

```
┌────────────────────────────────────────────────────────────┐
│ FMassEntityHandle[N]      ← 所有实体 handle 连续           │
├────────────────────────────────────────────────────────────┤
│ FragmentA[N]              ← 列 A，所有实体的 A 数据         │
├────────────────────────────────────────────────────────────┤
│ FragmentB[N]              ← 列 B                            │
├────────────────────────────────────────────────────────────┤
│ ...                                                         │
└────────────────────────────────────────────────────────────┘
```

- 所有实体的 Velocity 连续排布，然后是所有 Transform，缓存极友好（典型的 SoA / "结构体数组 → 数组结构体"变换）。
- N 由 `ChunkMemorySize / sizeof(所有列总和)` 决定，`UMassEntitySettings.ChunkMemorySize` 默认 128KB，范围 1KB ~ 512KB。
- **AddEntity / RemoveEntity 用 swap-and-pop**：删除时把末尾实体覆盖到空位，O(列数) 而非 O(N)，但破坏 chunk 内顺序。
- **空 chunk 的释放策略**：变空时立即 `FMemory::Free(RawMemory)` 但保留 `FMassArchetypeChunk` 元数据；只有"末尾连续的空 chunk"才会从 `Chunks` 数组移除。**中间空 chunk 永远保留**——因为 EntityMap 用绝对索引（`Abs / N` = chunk index, `Abs % N` = slot index）。
- **Fragment 必须 trivially-relocatable**（或至少 trivially-copyable）：`MoveFragmentsToNewLocationInternal` 实际是 memcpy 单向覆盖。

### 8.6 三层失效协议

| 层级 | 字段 | 失效粒度 | 检测者 |
|------|------|----------|--------|
| Manager | `ArchetypeDataVersion` | archetype 集合变化 | Query 缓存、PhaseGraph |
| Archetype | `EntityOrderVersion` | 同 archetype 内 entity 重排 | EntityCollection |
| Chunk | `SerialModificationNumber` | 单个 chunk 的任意变动（保守） | `FMassEntityInChunkDataHandle` |

### 8.7 并发存储后端的锁顺序

`FConcurrentEntityStorage`（启用条件 `WITH_MASS_CONCURRENT_RESERVE`）的硬约束：

```
正确顺序：FreeListMutex → PageAllocateMutex
```

`AddPage()` 顶部的 `check(FreeListMutex.IsLocked())` 是这一顺序的硬约束，反向加锁会死锁。

其他要点：
- **顶层 `EntityPages` 指针表是"分配后只读"**：页指针被发布后稳定、page 内存不会移动——这是无锁 `LookupEntity` 与 `Acquire/Release` 并发读取的根基。
- **首页 Index=0 哨兵**：与单线程后端 `Initialize` 里 `AcquireOne()` 占 0 号 Index 殊途同归，统一了"InvalidEntityIndex == 0"。
- **30-bit Generation 环绕风险**：约 10 亿次复用后 wrap 到 0，此时该槽下一个 Acquire 的 Serial=0 会被 `IsValid()` 误判（实践中难触达）。

### 8.8 Observer 三级 fast path

`FMassObserverManager::OnCompositionChanged` 的性能哲学：

| 层级 | 检查 | 复杂度 | 说明 |
|------|------|--------|------|
| L1 | `CompositionDelta.IsEmpty()` | O(1) | 没有变更直接 return |
| L2 | `ObservedFragments[Op].HasAny(delta)` | O(1) | 镜像位集快查 |
| L3 | `GetOverlap` 求交集 | O(类型数) | 仅对真正重叠的 type 触发 |

绝大多数变更在 L1/L2 跳出，零 observer 开销。

### 8.9 Subsystem 获取矩阵（5 种派生）

`FMassSubsystemAccess::FetchSubsystemInstance` 按编译期 traits 选择获取路径：

| Subsystem 派生 | 获取来源 |
|----------------|----------|
| `UEngineSubsystem` | `GEngine` |
| `UEditorSubsystem` | `GEditor`（编辑器构建） |
| `UWorldSubsystem` | `World->GetSubsystem<T>()` |
| `UGameInstanceSubsystem` | `World->GetGameInstance()->GetSubsystem<T>()` |
| `ULocalPlayerSubsystem` | `World->GetFirstLocalPlayerFromController()->GetSubsystem<T>()` |

**注意**：`ULocalPlayerSubsystem` 默认只取 first local player；本地分屏多人需特化模板。

### 8.10 依赖求解器的核心算法

`FMassProcessorDependencySolver` 的关键技巧：

1. **虚拟 archetype**：用户没传 EntityManager 时临时 `MakeShareable(new FMassEntityManager())`，为每个 Processor 用 `Requirements.AsCompositionDescriptor()` 建一个"刚好匹配它"的虚拟 archetype。让所有 Processor 用 query 互相匹配，得到 `ValidArchetypes`——是 **archetype-aware 冲突剪枝**的基础。
2. **`FResourceUsage::HandleElementType` 的 3 步算法**：
   - Read 等所有现存 Writer（不消费 Writer）
   - Write 等所有现存 Reader+Writer（消费它们，自己变成新唯一 Writer）
   - 登记自己进 Users 列表
3. **优先级传染**：高优先级节点沿依赖链向上 +1 抬高祖先 `MaxExecutionPriority`，保证整条链整体压前。
4. **`SequencePositionIndex`**：每个节点 = max(deps.Seq) + 1；全场最大值 = `MaxSequenceLength` = ParallelDispatch 需要的 fence/event 数。
5. **`bThreadSafeWrite` 优化**：`GatherSubsystemInformation` 把声明 `bThreadSafeWrite=true` 的 subsystem 收集到 `MultiThreadedSystemsBitSet`，求解器从 Read/Write 中减掉它们——多个 Processor 可同时写入而不插依赖边。
6. **dummy 节点**：缺失依赖名时建 `Processor=nullptr` 的 dummy group 节点，让"A→missingC→B"的传递依赖仍成立。

### 8.11 Processing Phase 与 UE Tick 的对接

| `EMassProcessingPhase` | UE `ETickingGroup` |
|------------------------|--------------------|
| PrePhysics | `TG_PrePhysics` |
| StartPhysics | `TG_StartPhysics` |
| DuringPhysics | `TG_DuringPhysics` |
| EndPhysics | `TG_EndPhysics` |
| PostPhysics | `TG_PostPhysics` |
| FrameEnd | `TG_LastDemotable` |

每个 `FMassProcessingPhase` 是一个 `FTickFunction`，注册到对应 TickGroup。

**Pause/Resume 是"边界对齐"语义**：Pause 在 FrameEnd 末尾生效、Resume 在 PrePhysics 起点生效，确保用户代码看到的暂停状态在一帧内一致。

### 8.12 类型 traits 的双路径

| 路径 | API | 时机 | 用途 |
|------|-----|------|------|
| 编译期 | `TMassExternalSubsystemTraits<T>` 偏特化 | 头文件 | Concept 检查、自动派生需求 |
| 运行期 | `FTypeManager::RegisterType` 写入的 `FSubsystemTypeTraits` | Initialize 期 | DependencySolver 等只持有 `UClass*` 的下游模块查询 |

`FTypeManager` 在 `RegisterBuiltInTypes` 时通过 `Make<T>()` 工厂把模板偏特化"快照"为运行时数据。

### 8.13 三套默认值不同的 traits

| Traits | GameThreadOnly | ThreadSafeWrite | 备注 |
|--------|----------------|-----------------|------|
| `TMassExternalSubsystemTraits` | `true` (保守) | `false` | Subsystem 默认必须 GT |
| `TMassSharedFragmentTraits` | `false` | `false` | 默认放开读、串行写 |
| `TMassFragmentTraits` | — | — | 只关心 `AuthorAcceptsItsNotTriviallyCopyable` |

**`ThreadSafeWrite=true` 的真实语义**：调度图把写也当读对待，允许并行——前提是作者自己保证写入是原子/无锁安全的。这是个**容易误用的语义**，使用者承担线程安全举证责任。

---

## 9. 注释过程中发现的疑似 Bug 与改进建议

> 以下是 10 波注释过程中累积的"看代码时发现的疑点"。所有发现都已在源文件的中文注释中就地标记。本节做汇总，按严重程度排序，便于后续修复或上报 Epic。

### 9.1 严重（功能失效或可能导致崩溃）

#### B1. `FMassCommandAddFragmentInstances` ctor 硬编码 `Set` 操作类型
- **位置**：`Public/MassCommands.h:409-410`
- **现象**：构造函数形参 `OperationType` 完全被丢弃，`Super(EMassCommandOperationType::Set ...)` 写死为 `Set`。
- **影响**：派生类 `FMassCommandBuildEntity` 在 ctor 中传 `Create` 想覆盖父类，但实际仍落到 `Set` 阶段——`BuildEntity` 不会按 Create 阶段在 Flush 第一组执行，而是在 Set 阶段（4）执行。
- **修复**：`Super(OperationType FORWARD_DEBUG_NAME_PARAM)`。

#### B2. `FGroupEntities::Add` 不调用 `Super::Add`，命令永不执行
- **位置**：`Public/MassArchetypeGroupCommands.h:30`
- **现象**：函数把 entity 全塞进自家 `Groups` map，没有把 entity 推入基类的 `TargetEntities`，也没有把 `bHasWork` 置为 true。
- **影响**：基类 `HasWork()` 返回 false → `CommandTypeOrder` 把命令排到 invalid → 整个跳过执行。**这条命令很可能根本不会运行**。
- **修复**：补 `bHasWork = true` 并/或在 `HasWork`/`GetNumOperationsStat` 反映 `Groups.Num()`。

#### B3. `FMassProcessingPhaseManager` 两个 delegate 都注册到 `OnEntityManagerInitialized`
- **位置**：`Private/MassProcessingPhaseManager.cpp` 构造函数
- **现象**：
  ```cpp
  OnDebugEntityManagerInitializedHandle   = FMassDebugger::OnEntityManagerInitialized.AddRaw(this, &...::OnDebugEntityManagerInitialized);
  OnDebugEntityManagerDeinitializedHandle = FMassDebugger::OnEntityManagerInitialized.AddRaw(this, &...::OnDebugEntityManagerDeinitialized);
  ```
  两行都是 `OnEntityManagerInitialized`。
- **影响**：deinit 永远不会回调到 `OnDebugEntityManagerDeinitialized`。
- **修复**：第二行改为 `OnEntityManagerDeinitialized`。

#### B4. `FMassProcessingPhaseManager` 析构未 Remove debug delegate handles
- **位置**：`Private/MassProcessingPhaseManager.cpp` 析构函数
- **现象**：构造时 `AddRaw(this, ...)` 注册了两个 handle，析构时没有任何 `Remove`。
- **影响**：`FMassDebugger` 的多播仍持有原始 `this` 指针——下次 broadcast 即 UAF。
- **修复**：析构中 `FMassDebugger::OnEntityManagerInitialized.Remove(handle)`。

#### B5. `SetFragmentWriteBreakForSelectedEntity` 漏写 `&`
- **位置**：`Private/MassDebugger.cpp`
- **现象**：
  ```cpp
  FBreakpoint NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();
  ```
  缺引用——后续对 `NewBreakpoint` 的字段写入全部丢失。
- **影响**：写入断点功能在被选中实体路径上失效——`mass.debug.SetFragmentBreakpoint` 控制台命令不可用。
- **修复**：`FBreakpoint& NewBreakpoint = ...`。

#### B6. `ClearAllFragmentWriteBreak` 比较了错误的 TriggerType
- **位置**：`Private/MassDebugger.cpp`
- **现象**：函数语义是清除 fragment 写入断点，但代码里比较的是 `ETriggerType::ProcessorExecute`，应当是 `FragmentWrite`。
- **影响**：(a) 想清除的断点不被清除；(b) 试图把 Trigger 当作 ScriptStruct 解读 ProcessorExecute trigger，可能触发 TVariant 断言。

#### B7. `FConstOptionalFragmentAccess::ConfigureQuery` 漏传 `Optional`
- **位置**：`Public/MassQueryExecutor.h`
- **现象**：
  ```cpp
  EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly);
  // 缺少 EMassFragmentPresence::Optional
  ```
  与同文件的 `FMutableOptionalFragmentAccess::ConfigureQuery`（正确传了 Optional）不一致。
- **影响**："const optional" accessor 实际被注册为 "const required"——archetype 匹配错误（要求 fragment 必须存在）。

#### B8. `FBreakpoint::ApplyEntityFilterByFragments` 的 Query 分支永远返回 false
- **位置**：`Private/MassDebuggerBreakpoints.cpp`
- **现象**：所有 requirement 通过后返回 `false` 而非 `true`。
- **影响**：`FilterType::Query` 的"实体创建"断点几乎不会触发。

### 9.2 中等（行为不一致或踩坑场景）

#### B9. `SwapTagsForEntity` 不触发 observer
- **位置**：`Private/MassEntityManager.cpp:1262-1288`
- **现象**：直接调用 `MoveEntityToAnotherArchetype`，不调 `OnPreCompositionRemoved`/`OnPostCompositionAdded`。
- **影响**：依赖 tag observer 监听状态切换的项目代码会静默丢失通知，而 `RemoveTag(A) + AddTag(B)` 会正常触发。

#### B10. `FMassCommandRemoveTagsInternal::CheckBreakpoints` 调用 Add 路径
- **位置**：`Public/MassCommands.h:374`
- **现象**：copy-paste 残留，调用了 `CheckFragmentAddBreakpoints` 而非 `CheckFragmentRemoveBreakpoints`。形参名 `InFragments`（实际是 tags）也佐证。

#### B11. `FMassFragmentRequirements::Reset()` 漏清 `bRequiresGameThreadExecution`
- **位置**：`Private/MassRequirements.cpp::Reset()`
- **现象**：`FMassSubsystemRequirements::Reset()` 是清零的，但 `FMassFragmentRequirements::Reset()` 漏掉。
- **影响**：曾声明过 GT-only shared fragment 的 query Reset 后再加非 GT-only 需求，仍报 GT-only，造成不必要的 GT 串行化。

#### B12. `FMassQueryRequirementIndicesMapping::IsEmpty()` 用 `||` 而非 `&&`
- **位置**：`Internal/MassArchetypeData.h`
- **现象**：`EntityFragments.Num() == 0 || ChunkFragments.Num() == 0`，只要任意一类为空就视作"empty"。
- **影响**：只需 chunk fragment 不需 entity fragment 的 query 会被错误视作 empty。

#### B13. `BatchGroupEntities` 缺失 observer 接入（已知 TODO）
- **位置**：`Private/MassEntityManager.cpp:914-952`
- **现象**：代码注释 `// we need something like the following to support observers` 直接保留。
- **影响**：archetype group 变化无法被 observer 监听。

#### B14. `MassChildOf.cpp::Execute` 硬编码 `FMassChildOfFragment::StaticStruct()`
- **位置**：`Private/Relations/MassChildOf.cpp`
- **现象**：注释说"this observer processor being used by relations that extend the ChildOf"，但代码硬编码 fragment 类型。
- **影响**：派生关系类型若复用本 observer，会写错 fragment。建议改为 `RelationData.Traits.RoleTraits[Subject].Element`。

#### B15. 已废弃的 `FMassFragmentRequirements` 构造函数总是 check 失败
- **位置**：`Private/MassRequirements.cpp:421-435`
- **现象**：`FMassFragmentRequirements(initializer_list)` / `(TConstArrayView)` 直接调 `AddRequirement`，但 `AddRequirement` 的 `checkf(bInitialized, ...)` 在没经过 `Initialize()` 的情况下必然触发。
- **影响**：这两个构造函数不可用——任何调用都会崩。

### 9.3 较低（设计不一致或可改进）

- **`MoveAppend` 内 `GetAllocatedSize` 漏算 `AppendedCommandInstances` 自身数组**
- **`FMassArchetypeCompositionDescriptor::DebugOutputDescription` 的 Empty 判定不完整**（只检查 3 个位集，忽略 Shared/ConstShared）
- **`AddElementRequirement` 路由不完整**（不支持 chunk/shared/constShared）
- **`FConcurrentEntityStorage::Release` 对槽位字段更新放在锁外**（理论上的 race window）
- **多处文档 typo**：`equivelncy`、`deprevate`、`fall through on purse`、`Can go our of date`、`MagPageCount`、注释类名错误等
- **多处死代码**：`PhaseConfig` 局部变量未使用、`PhaseDumpDependencyGraphFileName` 计算后未传递、Property 局部变量未使用
- **多处遗留注释**：`Interfaces/ITextureFormat.h` 错误 include、`#undef WITH_RELATIONSHIP_VALIDATION` 空 undef、几处 `@todo` 长期未跟进

### 9.4 文档/语义需明确化的"灰色区域"

- **`FRelationTypeTraits::IsValid()` 仅声明未实现**——若有调用方使用会链接错误。
- **30-bit Generation 而非 31-bit**：`FEntityData` 中 `bIsAllocated:1` + `uint32`，留出 30 位用途未文档化。
- **`FMassRelationRoleInstanceHandle` 不存 SerialNumber**：handle 仅 8 字节（30+30+2 bit），运行时还原时通过 `EntityManager.CreateEntityIndexHandle` 拉取——是性能/正确性的取舍点。
- **`FArchetypeGroups::GetID` 用 `IsValidIndex` 而非 `IsAllocated`**：可能读到稀疏数组中已销毁槽位的残留值。
- **`FRelationManager` 递归限制 10 vs BFS 上限 64** 不一致——前者 destruction observer 链深度，后者查询步数，应提到命名常量。
- **`FArchetypeGroupID(uint32)` 缺 `explicit`**：与 `FArchetypeGroupType` 不对称。

---

## 10. 实现自定义类型的 Checklist

> 以下是从注释过程中归纳出的「写新 X」的最小步骤集，便于快速对照。

### 10.1 实现一个 Fragment

```cpp
USTRUCT()
struct FMyFragment : public FMassFragment
{
    GENERATED_BODY()

    // 必须 trivially copyable，否则需要：
    // template<> struct TMassFragmentTraits<FMyFragment>
    // { enum { AuthorAcceptsItsNotTriviallyCopyable = true }; };

    UPROPERTY()
    float Value = 0.f;
};
```

### 10.2 实现一个 Tag

```cpp
USTRUCT()
struct FMyTag : public FMassTag  // 不能有任何 UPROPERTY
{
    GENERATED_BODY()
};
```

### 10.3 实现一个 Processor

```cpp
UCLASS()
class UMyProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UMyProcessor()
    {
        ProcessingPhase = EMassProcessingPhase::PrePhysics;
        ExecutionFlags  = (int32)EProcessorExecutionFlags::All;
        ExecutionOrder.ExecuteInGroup = TEXT("MyGroup");
    }

protected:
    FMassEntityQuery Query;  // 必须是成员变量（RegisterQuery 会校验地址范围）

    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Mgr) override
    {
        Query.AddRequirement<FMyFragment>(EMassFragmentAccess::ReadWrite);
        Query.RegisterWithProcessor(*this);
    }

    virtual void Execute(FMassEntityManager& Mgr, FMassExecutionContext& Ctx) override
    {
        Query.ForEachEntityChunk(Ctx, [](FMassExecutionContext& C)
        {
            const TArrayView<FMyFragment> Frags = C.GetMutableFragmentView<FMyFragment>();
            for (FMyFragment& F : Frags) { F.Value += 1.f; }
        });
    }
};
```

### 10.4 实现一个 Mass-aware Subsystem

```cpp
UCLASS()
class UMySubsystem : public UMassSubsystemBase  // 不要直接继承 UWorldSubsystem
{
    GENERATED_BODY()

public:
    UMySubsystem()
    {
        // 在 ctor 里声明线程模型，方便依赖求解器决策
        OverrideSubsystemTraits<UMySubsystem>(/*GameThreadOnly*/ true, /*ThreadSafeWrite*/ false);
    }
};
```

### 10.5 实现一个 Observer Processor

```cpp
UCLASS()
class UMyObserver : public UMassObserverProcessor
{
    GENERATED_BODY()

public:
    UMyObserver()
    {
        ObservedType        = FMyFragment::StaticStruct();
        ObservedOperations  = (int32)EMassObservedOperationFlags::Add;
        // bAutoRegisterWithObserverRegistry = true (默认)
    }
    // ConfigureQueries / Execute 同 UMassProcessor
};
```

### 10.6 实现一个 Relation 类型（参考 `FMassChildOfRelation`）

需要 5 个零件：
1. 关系 tag：`FMyRelation : public FMassRelation`
2. 角色端 fragment（可选）：`FMyRelationFragment : public FMassFragment { ... }`
3. Creation observer 类（可选）：`UMyRelationEntityCreation : public UMassRelationObserver`
4. 全局 TypeHandle 变量：`namespace UE::Mass::Relations { extern FTypeHandle MyRelationHandle; }`
5. 模块启动时调注册函数：
   ```cpp
   // 在 IMPLEMENT_MODULE 启动逻辑或 OnRegisterBuiltInTypes 委托里
   FRelationTypeTraits Traits;
   Traits.RelationTagType      = FMyRelation::StaticStruct();
   Traits.RelationFragmentType = FMyRelationFragment::StaticStruct();
   Traits.RoleTraits[Subject].bExclusive = true;  // 等
   Traits.ObserverClassCreation = UMyRelationEntityCreation::StaticClass();
   FTypeManager::Get().RegisterType(Traits);
   ```

### 10.7 添加自定义命令

```cpp
struct FMyCommand : public FMassBatchedCommand
{
    explicit FMyCommand(/* ctor params */)
        : FMassBatchedCommand(EMassCommandOperationType::Set DEBUG_NAME_PARAM(...))
    {}

    virtual void Run(FMassEntityManager& Mgr) override
    {
        // 应用累积的所有命令参数
    }

    // 同类命令在 buffer 中会被聚合成一个 FMyCommand 实例，调用方多次 push 时
    // 会通过 GetCommandIndex<T>() 找到这个实例并 Add 参数到内部 TArray
};
```

---

## 11. 阅读源码的推荐路径

如果你刚接触 Mass，建议按此顺序阅读：

1. **入门（半天）**：
   - `MassEntityElementTypes.h` → `MassEntityHandle.h` → `MassEntityTypes.h`（前 200 行）
   - `MassEntityManager.h`（顶部 doc + `FScopedProcessing`）
2. **核心（一天）**：
   - `MassRequirements.h` → `MassEntityQuery.h`
   - `MassExecutionContext.h`
   - `MassProcessor.h`
3. **进阶（半天）**：
   - `MassCommandBuffer.h` + `MassCommands.h`
   - `MassObserverManager.h` + `MassObserverProcessor.h`
4. **调度（半天）**：
   - `MassProcessorDependencySolver.h`
   - `MassProcessingPhaseManager.h`
5. **可选（半天）**：
   - `MassEntityRelations.h` + `MassRelationManager.h`
   - `Relations/MassChildOf.h`（看完整示例）
6. **存储（深入）**：
   - `Internal/MassArchetypeData.h` + `Private/MassArchetypeData.cpp`
   - `MassEntityManagerStorage.h`

---

## 附录 A — 文档维护

本文档由自动化注释任务生成，每次对模块添加中文注释后会回头审视并修订本文档。
- 初版生成：注释工作启动前
- 最终审订：所有 16 个模块、93 个源文件注释完成后

如发现与源码不一致之处，请以源码为准。

### 注释覆盖率验证

```powershell
$folder = "Engine\Source\Runtime\MassEntity"
Get-ChildItem $folder -Recurse -Include "*.h","*.cpp" |
    ForEach-Object {
        $count = (Select-String -Path $_.FullName -Pattern '[\u4e00-\u9fff]' -Encoding UTF8 -List | Measure-Object).Count
        if ($count -eq 0) { Write-Host "MISSING: $($_.FullName)" }
    }
```

最终验证（截至本文档最终审订时）：**93 / 93** 个源文件均含中文注释。

---

*完*
