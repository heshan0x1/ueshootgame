# Stats 模块

> 路径：`Engine/Source/Runtime/Net/Iris/Private/Iris/Stats/`（**仅私有**，无 Public 头）  
> 分层：**L2 观测层**（依赖 Core + ReplicationSystem 的 NetRefHandleManager/ReplicationProtocol 类型）  
> 职责：在发送端聚合 Iris 复制过程的 CSV 性能统计（对象数、字节数、轮询/量化/写入耗时等），支持并行 task 写入 + 聚合回主 context。

---

## 1. 文件清单

| 文件 | 职责 |
|------|------|
| `Private/Iris/Stats/NetStats.h` | 声明 `FNetSendStats`、`FNetTypeStats`、`FReplicationStats`；log category `LogNetStats` |
| `Private/Iris/Stats/NetStatsContext.h` | `FNetTypeStatsData`（阶段统计项）、`FNetStatsContext`（per-task 上下文）、`FNetStatsTimer`；采集宏 `UE_NET_IRIS_STATS_*` |
| `Private/Iris/Stats/NetStats.cpp` | CSV category 定义（`IrisPreUpdateMS`/`IrisPollMS`/`IrisQuantizeMS`/`IrisWriteMS`/…）、Parent/Child context 管理与聚合、CVar `net.Iris.Stats.ShouldIncludeSubObjectWithRoot` |

---

## 2. 核心类

### 2.1 `FNetSendStats`

每帧发送端的"一帧摘要"统计。

**字段（示例）**：
- `NumReplicatedDestroyedObjects`
- `NumReplicatedObjects`
- `NumDeltaCompressedObjects`
- `NumHugeObjectsSendQueue` / `HugeObjectStallTimeMs`
- `NumReplicatingConnections`

**方法**：线程不安全 setter + 带锁的 `Accumulate(other)` + `ReportCsvStats()`。

### 2.2 `FNetTypeStatsData` + `FNetStatsContext`

- **`EStatsIndex`** 8 个阶段：`PreUpdate / Poll / PollWaste / Quantize / Write / WriteWaste / WriteCreationInfo / WriteExports`
- **`FNetTypeStatsData`**：上述每个阶段 `(Time, Bits, Count)` 三元组。
  - ⚠ **`WriteCreationInfo` 没有 Time 维度**（仅 Bits + Count）——创建/销毁是瞬时事件，无独立计时；
  - ⚠ **`WriteExports` 仅记 Count**——Export 字节合入 `Write` 不单独记 Bits/Time。
- **`FNetStatsContext`**：per-task 的可采集上下文，持有多 TypeStats 的 `FNetTypeStatsData` 数组；通过 `NetRefHandleManager` 从 `FInternalNetRefIndex` → `FReplicationProtocol::TypeStatsIndex` → 对应的 `FNetTypeStatsData` 槽；含 `bIsAcquiredByTask` 防并发抢占。

### 2.3 `FNetTypeStats`（由 ReplicationSystem 拥有）

- 管理 1 个 `ParentStatsContext` + N 个 `ChildStatsContext`（按 `LowLevelTasks::FScheduler::GetMaxNumWorkers()+1` 预创建，含 GameThread）。
- `GetOrCreateTypeStats(FName)` 注册类型统计槽位：`DefaultTypeStatsIndex=0`、`OOBChannelTypeStatsIndex=1`。
  - 在并行阶段调用会 `checkf` 拒绝扩容（保证所有 child 的 `TypeStatsData` 索引一致）。
- `AcquireChildNetStatsContext / ReleaseChildNetStatsContext` 线程安全获取 per-task context（`ChildStatsContextCS` 临界区只保护"获取/归还/创建"动作，写入数据本身不锁；`bIsAcquiredByTask` 哨兵保证两 task 不会拿到同一 child）。
  - **健壮性提示**：抢不到 child 时会 `CreateChildNetStatsContext` 新建一个并 `ensureMsgf` 警告；新增 child 不会在 `ReportCSVStats` 后被回收，下一帧仍可复用（永久增长直到 dtor）。
- 每帧：`PreUpdateSetup → (并行子任务用 Child) → AccumulateChildrenToParent → ReportCSVStats`。

### 2.4 `FReplicationStats`

更高级别的聚合指标（PendingObject / HugeObjectSendQueue 的平均 / 最大值）。

### 2.5 `FNetStatsTimer`

派生自 `FNetTimer`（NetCore），仅当 `NetStatsContext != nullptr` 时启用；用于作用域内时间戳累加。

---

## 3. 采集宏（Iris 代码散落调用）

```cpp
UE_NET_IRIS_STATS_TIMER(TimerName, NetStatsContext)
UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT[_AS_WASTE]
UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_[AND_COUNT_]FOR_OBJECT[_AS_WASTE]
UE_NET_IRIS_STATS_INCREMENT_FOR_OBJECT
UE_NET_IRIS_STATS_ADD_COUNT_FOR_OBJECT
```

由 `UE_NET_IRIS_CSV_STATS` 开关编译出入。

---

## 4. 依赖关系

- **Iris 内**：`Iris/Core`（`IrisCsv.h` / `IrisProfiler.h`）；`Iris/ReplicationSystem`（`NetRefHandleManager`、`ReplicationProtocol` 提供 `TypeStatsIndex`）；`Net/Core/Trace/NetTrace.h`。
- **UE 外**：`Core`（`HAL/CriticalSection` / `Logging/LogMacros` / `Containers/Array` / `Misc/ScopeLock` / `HAL/IConsoleManager`）、`ProfilingDebugging/CsvProfiler`、`Async/Fundamental/Scheduler`（`LowLevelTasks::FScheduler`）。

反向依赖：`ReplicationSystem`（各阶段采集统计）。

---

## 5. 对外 API / 扩展点

- 类：`FNetSendStats` / `FNetTypeStats` / `FNetStatsContext` / `FReplicationStats` / `FNetStatsTimer`。
- 采集宏（见 §3）。
- CVar：`net.Iris.Stats.ShouldIncludeSubObjectWithRoot`。
- CSV Category：以 `Iris` 前缀的多个 `IrisXxxMS/Count/KBytes`。

---

## 6. 注释推进计划

- **并行度**：1 个 subagent（3 文件）。
- **阶段**：**阶段 2**（与 Serialization 并行）。
- **重点关注**：
  - 并发模型：Parent/Child context 的生命周期、`bIsAcquiredByTask` 的边界；
  - `Accumulate` 合并路径；
  - `LowLevelTasks::FScheduler::GetMaxNumWorkers()` 的 pre-allocation 逻辑；
  - 每个 CSV category 的含义（PreUpdate/Poll/PollWaste/Quantize/Write/WriteWaste/WriteCreationInfo/WriteExports）。

### 注释完成情况

- [x] `NetStats.h`
- [x] `NetStatsContext.h`
- [x] `NetStats.cpp`

### 已根据注释发现修正的文档差异

1. `WriteCreationInfo` 没有 Time，`WriteExports` 仅记 Count（其他阶段均有完整三元组）；
2. `AcquireChildNetStatsContext` fallback 路径会创建新 child（永久增长直到 dtor）；
3. `GetOrCreateTypeStats` 在并行阶段会 `checkf` 拒绝扩容。
