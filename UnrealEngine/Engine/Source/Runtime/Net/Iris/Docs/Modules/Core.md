# Core 模块

> 路径：`Engine/Source/Runtime/Net/Iris/{Public,Private}/Iris/Core/`  
> 分层：**L1 基础设施层**（依赖 UE Core/CoreUObject/NetCore/TraceLog/ProfilingDebugging，几乎不依赖 Iris 其他模块）  
> 职责：为 Iris 框架提供横切支撑——日志、性能剖析（CPU/CSV）、内存跟踪（LLM）、调试 helper、位运算、分块数组容器、网络对象引用基础值类型、全局委托。

---

## 1. 文件清单（按作用分组）

### 1.1 日志 / 调试

| 文件 | 职责 |
|------|------|
| `Public/Iris/Core/IrisLog.h` | 声明 3 条 log category：`LogIris` / `LogIrisFiltering` / `LogIrisNetCull` |
| `Private/Iris/Core/IrisLog.cpp` | `DEFINE_LOG_CATEGORY` 具体实现 |
| `Public/Iris/Core/IrisLogUtils.h` | `FIrisLogOnceTracker`：按 hash 或 FName 的 "log-once" |
| `Private/Iris/Core/IrisLogUtils.cpp` | Tracker 实现 |
| `Public/Iris/Core/IrisDebugging.h` | `extern "C"` 调试 helper（watch window/immediate window 用）：按 NetRefHandle/InternalIndex/RPC 名/类名设断点、打印对象状态；对应 `Net.Iris.DebugName` / `Net.Iris.DebugRPCName` / `Net.Iris.DebugNetRefHandle` / `Net.Iris.DebugNetInternalIndex` CVar |
| `Private/Iris/Core/IrisDebugging.cpp` | 上述 API + `IConsoleObject` 注册 |

### 1.2 性能 / 指标

| 文件 | 职责 |
|------|------|
| `Public/Iris/Core/IrisProfiler.h` | `IRIS_PROFILER_SCOPE[_CONDITIONAL/_TEXT/_VERBOSE]` / `IRIS_PROFILER_PROTOCOL_NAME` 宏；可选接入 Superluminal；客户端 `FClientProfiler`（RPC/RepNotify/ObjectCreate/阻塞统计） |
| `Private/Iris/Core/IrisProfiler.cpp` | 定义 `IrisClient` CSV category、`FClientProfiler` 实现、`net.Iris.EnableDetailedClientProfiler` CVar |
| `Public/Iris/Core/IrisCsv.h` | 声明 `Iris` CSV profiler category 及合并宏 `IRIS_CSV_PROFILER_SCOPE` |
| `Public/Iris/Core/IrisMemoryTracker.h` | **6 个 LLM Tag 声明**：`Iris / IrisState / IrisInitialization / IrisConnection / NetToken / NetTokenStructState`（⚠ 其中 `NetToken` 在 Iris 侧**仅声明**，定义位于 `NetCore` 模块，Iris 跨模块引用以避免重复定义） |
| `Private/Iris/Core/IrisMemoryTracker.cpp` | LLM Tag 定义（Iris 本地 5 个：除 NetToken 外），挂到 `STATGROUP_LLMFULL / NetworkingSummary` |

### 1.3 基础数据类型 / 容器 / 工具

| 文件 | 职责 |
|------|------|
| `Public/Iris/Core/BitTwiddling.h` | 计算整数序列化所需比特数：`GetBitsNeeded<T>()` / `GetBitsNeededForRange<T>()` / `GetLeastSignificantBit` 等模板 |
| `Public/Iris/Core/NetChunkedArray.h` | `TNetChunkedArray<T>`：继承 `TChunkedArray<T>`，首批 chunk 在单块连续内存中分配（缓存友好），禁用 `Empty/Reset`，提供 `AddToIndexUninitialized/Zeroed` |
| `Public/Iris/Core/NetObjectReference.h` | `FNetObjectReference` = `FNetRefHandle` + `FNetToken`（路径）+ trait；`FNetDependencyInfo`、`ENetObjectReferenceResolveResult` |
| `Public/Iris/Core/IrisDelegates.h` | `FIrisDelegates` 静态入口（当前：`FIrisCriticalErrorDetected` 多播） |
| `Private/Iris/Core/IrisDelegates.cpp` | 静态单例实现 |
| `Private/Iris/Core/MemoryLayoutUtil.h` | `FMemoryLayoutUtil`：手工计算"多段 allocations 拼成单块内存"的 offset/alignment 工具（被 Descriptor/Protocol 等使用） |

---

## 2. 核心类/接口

### 2.1 `FNetObjectReference`

Iris 对外统一的"网络对象引用"值类型，用于：

- RPC 参数中的 UObject*；
- 可复制属性中的 `TObjectPtr` / `TWeakObjectPtr`；
- 依赖追踪（`UObjectReplicationBridge::AddDependentObject`）；
- 分布式/远程对象（UE_WITH_REMOTE_OBJECT_HANDLE 分支）。

**字段**：`FNetRefHandle RefHandle` + `FNetToken PathToken`（可选，用于 static 对象通过名字导出）+ trait（是否可解析、是否必须映射）。

**关键约束**：
- 外部仅有**默认构造**（空引用）可用；所有带参构造都是 `private` + `friend FObjectReferenceCache`——这保证 trait 字段的真实性**只由 Cache 维护**，外部无法伪造。
- `HasUnresolvedMustBeMappedReferences` 对应 `ReplicationReader` 的 `PendingBatchData` 推迟机制：收到的 batch 如果含"必须映射但当前不可解析"的引用，会挂起直到 async load 完成。

**友元**：`FObjectReferenceCache` 为构造者，`FObjectNetSerializer` 为读写方。

### 2.2 `TNetChunkedArray<T>`

Iris 所有 per-object 数组的首选容器。相比 UE 原生 `TChunkedArray`：

- 首批 `NumPreAllocatedChunks` 个 chunk 连续分配为单块 `new[]`，减少随机 chunk 跳转；
- 禁用 `Empty/Reset`，保证句柄稳定（Iris 许多系统用索引长期引用 entry）；
- 额外提供 `AddToIndexUninitialized/Zeroed(Index)`（给定内部索引确保容器够大）。

**重要实现细节**：
- **释放策略分两种**：首批连续 chunk 用 `delete[]`，之后按需追加的 chunk 用 `delete`；`NumPreAllocatedChunks` 是两者分界。任何想扩展此容器的代码必须遵守这一不变式，否则会产生 heap corruption。

典型使用方：`FNetRefHandleManager::ReplicatedObjectData`、`FReplicationStateStorage`。

### 2.3 `FClientProfiler`

客户端 CSV 统计。目前代码提供的是 **计数型 Record API**（`RecordObjectCreate` / `RecordRepNotify` / `RecordRPC` / `RecordBlockedReplication`），以及 5 个 CSV Category（`IrisClient` 总览 + 3 个 `IrisClientDetailXxx` 明细 + `IrisClientBlockedByAsyncLoading`）。

- 启用条件：`IRIS_CLIENT_PROFILER_ENABLE = !WITH_SERVER_CODE && CSV_PROFILER_STATS`（仅纯客户端 build 有）；
- Detail 列启用条件：`IRIS_CLIENT_PROFILER_DETAILED = !UE_BUILD_SHIPPING`；
- CVar `net.Iris.EnableDetailedClientProfiler` 运行时开关，典型用法是"抓几秒→关"（若 FName 种类多 detail 列会爆炸）。

⚠ 先前版本文档提到过 "ScopedObjectCreate / ScopedRepNotify / ScopedRPC / AddBlockingObjectTime" RAII helper，**当前代码并未实现**；保留为未来扩展位。

### 2.4 `FIrisDelegates::GetCriticalErrorDetectedDelegate()`

全局错误钩子（`FSimpleMulticastDelegate`）。Iris 内部碰到"复制数据损坏/protocol 不一致/必须重连"等关键事件时广播，供上层/分析系统捕获。

### 2.5 `IrisDebugHelper`

为让调试器直接访问设计的 `extern "C"` 函数：`DebugNetObject(uint64 NetRefHandleId)` / `DebugNetObjectById` / `DebugRPCName(char*)` / `SetIrisDebugNetRefHandle(uint64)` 等。可在 VS Watch 窗口直接调用。

### 2.6 宏与 Category

- Log：`LogIris` / `LogIrisFiltering` / `LogIrisNetCull`
- Profiler：`IRIS_PROFILER_SCOPE(Name)` / `_CONDITIONAL(Name, bEnabled)` / `_TEXT(FormatString, ...)` / `_VERBOSE(Name)` / `IRIS_PROFILER_PROTOCOL_NAME(Protocol)`
- CSV：`IRIS_CSV_PROFILER_SCOPE(Name)`（组合 CSV + CPU）
- LLM：`LLM_DECLARE_TAG_API(Iris / IrisState / IrisInitialization / IrisConnection / NetToken / NetTokenStructState)`

---

## 3. 依赖关系

### 3.1 对 Iris 其他模块

**几乎不依赖**。`NetObjectReference.h` 需要 `FNetRefHandle`（位于 `Iris/ReplicationSystem`）——但仅通过前向声明，头文件层面不破坏"Core 是最底层"的原则。

### 3.2 对 UE 外部模块

- `Core`：`HAL/PlatformMath` / `Templates/*` / `Logging/LogMacros` / `HAL/LowLevelMemTracker` / `Containers/ChunkedArray` / `Misc/EnumClassFlags`；
- `CoreUObject`：`UObject/NameTypes` / `UObject/Field`；
- `TraceLog` / `ProfilingDebugging`：`CsvProfiler` / `CpuProfilerTrace`；
- `NetCore`：`Net/Core/NetHandle/NetHandle.h` / `Net/Core/NetToken/NetToken.h`。

---

## 4. 对外 API / 扩展点

### 4.1 直接 API

- 位工具：`UE::Net::GetBitsNeeded<T>()` / `GetBitsNeededForRange<T>()` / `GetLeastSignificantBit`。
- 引用：`UE::Net::FNetObjectReference`（多数字段为 friend 访问） + `FNetDependencyInfo` + `ENetObjectReferenceResolveResult`。
- 容器：`UE::Net::TNetChunkedArray<T>`。
- Log-Once：`UE::Net::FIrisLogOnceTracker`。
- 全局：`UE::Net::FIrisDelegates::GetCriticalErrorDetectedDelegate()`。
- 调试：`UE::Net::IrisDebugHelper::*`、`DebugNet*`（`extern "C"`）。
- 内存工具：`UE::Net::Private::FMemoryLayoutUtil::*`（仅内部）。

### 4.2 宏（扩展）

见 §2.6。

### 4.3 CVar / Console Command

- **CVar**：`Net.Iris.DebugName` / `Net.Iris.DebugRPCName` / `net.Iris.EnableDetailedClientProfiler`
- **ConsoleCommand**（注意不是 CVar）：`Net.Iris.DebugNetRefHandle <Id>` / `Net.Iris.DebugNetInternalIndex <Index>`——不传参数等于清空；命中时触发 `UE_DEBUG_BREAK`

---

## 5. 注释推进计划

- **并行度**：2 个 subagent（一个负责 Public 头 10 文件，一个负责 Private 实现 7 文件）。
- **阶段**：**阶段 1（与 Metrics、PacketControl 并行）**。
- **重点关注**：
  - `NetObjectReference.h` 的字段含义与 trait 位、构造私有化约束；
  - `IrisProfiler.h` 的宏条件编译路径（`UE_TRACE_ENABLED` / Superluminal / CSV / 空宏）；
  - `NetChunkedArray.h` 与 UE 原 `TChunkedArray` 差异、首批/追加 chunk 释放策略分界；
  - `BitTwiddling.h` 的模板偏特化实现（有/无符号 × sizeof≤4/==8 共 4 个分支）；
  - `IrisDebugging.cpp` 的 CVar/ConsoleCommand 处理与调试下钻路径。

### 注释完成情况

- [x] Public 头 10 文件（全部完成，并修正文档：LLM Tag 数=6、NetObjectReference 构造约束、TNetChunkedArray 释放策略、FClientProfiler 实际 API 形态、ConsoleCommand 与 CVar 区分）
- [x] Private 实现 7 文件（全部完成，对 CVar/ConsoleCommand 做了详尽注释；对 `IrisDebugging.cpp` 做了完整的"下钻路径"注释，对 `IrisProfiler.cpp` 的 5 个 CSV Category 做了分工说明）
