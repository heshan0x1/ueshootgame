# Iris 框架总体架构文档

> 研究版本：UE `Runtime/Net/Iris`（UnrealEngine 源码树中的 `IrisCore` 模块）  
> 文档目的：学习 Iris（UE 的新一代 Replication 框架）的整体架构、模块拆分、运行流程与扩展点。  
> 本文档是"总览"，各模块的详细说明见 `Docs/Modules/*.md`。

---

## 0. Iris 是什么

Iris 是 Unreal Engine 在 UE5 引入的**新一代网络对象复制（Replication）子系统**，位于 `Engine/Source/Runtime/Net/Iris`，编译单元为 `IrisCore` 模块。设计目标：

- 取代以 `UNetDriver + UActorChannel + FRepLayout + FNetGUIDCache` 为核心的旧复制管线；
- 数据与行为分离：把"复制什么"（`FReplicationStateDescriptor`）从"在哪条连接上、何时、如何发送/接收"（`FReplicationSystem` 及其子系统）中分离出来；
- 更好地支持大量可复制对象（Scope / Filtering / Prioritization / DeltaCompression 等可插拔子系统）；
- 模板元编程 + 反射结合的可扩展 `FNetSerializer` 体系；
- 面向多线程与多 PIE 实例（多 `UReplicationSystem`）。

上层接入方式：`UNetDriver` 在启用 Iris（`net.Iris.UseIrisReplication`）时，创建 `UReplicationSystem`，并经由 `UObjectReplicationBridge` 把游戏对象挂到 Iris 上；帧循环由 NetDriver Tick 驱动。

---

## 1. 顶层目录结构

```
Engine/Source/Runtime/Net/Iris/
├── IrisCore.Build.cs       # UBT 构建规则：依赖 Core / CoreUObject / NetCore / TraceLog
├── IrisCore.natvis         # VS 调试可视化
├── Docs/                   # 学习文档（本仓库注释版新增）
│   ├── Iris_Architecture.md
│   └── Modules/*.md
├── Public/Iris/            # 对外 API
│   ├── Core/
│   ├── DataStream/
│   ├── Metrics/
│   ├── PacketControl/
│   ├── ReplicationState/
│   ├── ReplicationSystem/
│   │   ├── ChunkedDataStream/
│   │   ├── Conditionals/
│   │   ├── Filtering/
│   │   ├── NetBlob/
│   │   └── Prioritization/
│   └── Serialization/
├── Private/Iris/           # 内部实现
│   ├── Core/
│   ├── DataStream/
│   ├── IrisConfig.cpp
│   ├── IrisCoreModule.cpp
│   ├── IrisConfigInternal.h
│   ├── ReplicationState/
│   ├── ReplicationSystem/
│   │   ├── ChunkedDataStream/
│   │   ├── Conditionals/
│   │   ├── DeltaCompression/
│   │   ├── Filtering/
│   │   ├── NetBlob/
│   │   ├── Polling/
│   │   └── Prioritization/
│   ├── Serialization/
│   └── Stats/
└── (IrisCoreModule.cpp 与 IrisConfig.cpp 位于 Private/Iris 下)
```

所有源文件总计 **343 个** `.h/.cpp`。

---

## 2. 模块分层与拓扑

按"谁依赖谁"画出 Iris 内部模块的 DAG（节点越下越基础，越上越业务化）：

```
                ┌──────────────────────────────────────────────┐
                │       IrisCoreModule (FIrisCoreModule)       │
                │   (顶层聚合，挂接 ModuleManager 生命周期)    │
                └──────────────────────┬───────────────────────┘
                                       │
                ┌──────────────────────▼───────────────────────┐
                │              ReplicationSystem               │
                │  (UReplicationSystem + FReplicationSystemImpl│
                │   + 7 个子模块: Filtering/Prioritization/…)  │
                └──┬───────────────┬───────────────┬──────┬────┘
                   │               │               │      │
          ┌────────▼──────┐  ┌─────▼────────┐  ┌───▼──┐ ┌─▼────────┐
          │ ReplicationSt │  │  DataStream  │  │ Stats│ │ Metrics  │
          │     ate        │  │              │  │      │ │ (DTO)    │
          └───────┬───────┘  └───────┬──────┘  └──┬───┘ └─┬────────┘
                  │                  │            │       │
                  │     ┌────────────▼────────┐   │       │
                  └────►│   Serialization     │◄──┘       │
                        │ (FNetSerializer /   │           │
                        │  FNetBitStream /    │           │
                        │  FNetSerialContext) │           │
                        └──────────┬──────────┘           │
                                   │                      │
                   ┌───────────────▼──────────────────────▼──────┐
                   │   Core (IrisLog/IrisProfiler/IrisMemory/    │
                   │         NetChunkedArray/NetObjectReference/ │
                   │         BitTwiddling/IrisDelegates/IrisCsv/ │
                   │         IrisDebugging/IrisLogUtils)         │
                   └─────────────────────┬───────────────────────┘
                                         │
                          ┌──────────────▼───────────────┐
                          │   PacketControl (纯枚举契约) │
                          └──────────────────────────────┘

外部模块依赖：Core / CoreUObject / NetCore / TraceLog / ProfilingDebugging
（来自 IrisCore.Build.cs + 各头文件 #include）
```

### 2.1 分层与注释推进顺序

| 层 | 模块 | 说明 | 本注释工作所在阶段 |
|----|------|------|-------|
| L0 基础契约 | **PacketControl** | 单枚举 `EPacketDeliveryStatus` | 阶段 1 |
| L0 基础容器 | **Metrics** | `FNetMetric` / `FNetMetrics` 轻量 DTO | 阶段 1 |
| L1 基础设施 | **Core** | Log/Profiler/LLM/Debug/BitTwiddling/NetChunkedArray/NetObjectReference/Delegates/LogUtils/Csv | 阶段 1 |
| L2 观测 | **Stats** | `FNetSendStats`/`FNetTypeStats`/`FNetStatsContext` | 阶段 2 |
| L2 底层数据 | **Serialization** | `FNetBitStream*`/`FNetSerializer`/`FNetSerializationContext`/具体 `FXxxNetSerializer`/`FNetReferenceCollector`/`UIrisObjectReferencePackageMap` | 阶段 2 |
| L3 中间件 | **DataStream** | `UDataStream` / `UDataStreamManager` / `UDataStreamDefinitions`（连接内多流复用） | 阶段 3 |
| L3 数据模型 | **ReplicationState** | `FReplicationStateDescriptor(+Builder)`/`FPropertyReplicationState`/`FIrisFastArraySerializer`/`FPropertyNetSerializerInfoRegistry` | 阶段 3 |
| L4 业务核心 | **ReplicationSystem** | `UReplicationSystem`/`UObjectReplicationBridge`/`NetRefHandleManager`/`ReplicationWriter/Reader`/`ReplicationProtocol`/Filtering/Prioritization/DeltaCompression/NetBlob/… | 阶段 4 |
| L5 顶层聚合 | **IrisCoreModule + IrisConfig** | `FIrisCoreModule::StartupModule` 负责注册 NetSerializer、绑定引擎生命周期、多 RS 管理 | 阶段 5 |

---

## 3. 模块一览

下面是每个模块的最小认知（详情见 `Docs/Modules/*.md`）。

### 3.1 Core（[Core.md](Modules/Core.md)）

基础设施层，不依赖 Iris 其他模块。提供：

- **`FNetObjectReference`**：Iris 对 UObject 的通用"网络引用"值类型（组合 `FNetRefHandle + FNetToken + trait`）。
- **`TNetChunkedArray<T>`**：继承 `TChunkedArray`，首批连续、禁止 `Empty/Reset` 的 per-object 存储容器。
- **`FIrisLogOnceTracker`**：按 hash 的 log-once。
- **`FIrisDelegates::GetCriticalErrorDetectedDelegate()`**：全局关键错误钩子。
- **`GetBitsNeeded*`、`GetLeastSignificantBit`**：位运算工具。
- **`IrisDebugHelper`**：`extern "C"` 导出的调试器 watch-window helper + `Net.Iris.DebugXxx` CVar。
- **Profiler/CSV/LLM 宏**：`IRIS_PROFILER_SCOPE*` / `IRIS_CSV_PROFILER_SCOPE` / `LLM_DECLARE_TAG_API(Iris/IrisState/…)`。
- **Log Category**：`LogIris` / `LogIrisFiltering` / `LogIrisNetCull`。

### 3.2 Stats（[Stats.md](Modules/Stats.md)）

观测层（内部模块，无 Public 头）。

- **`FNetSendStats`**：每帧发送端摘要统计（复制对象数 / Delta 数 / HugeObject 队列 / 活跃连接）。
- **`FNetTypeStats` + `FNetStatsContext`**：按"类型 + 阶段"（PreUpdate / Poll / Quantize / Write / Exports / …）做并行 task-friendly 的统计收集（`AcquireChildNetStatsContext` / `AccumulateChildrenToParent` / `ReportCSVStats`）。
- `UE_NET_IRIS_STATS_TIMER` / `..._ADD_TIME_AND_COUNT_FOR_OBJECT` / `..._ADD_BITS_WRITTEN_FOR_OBJECT` 一整套采集宏贯穿 ReplicationSystem 各阶段。

### 3.3 Metrics（[Metrics.md](Modules/Metrics.md)）

极轻量的 DTO 模块。`FNetMetric`（`uint32/int32/double` 三态 union） + `FNetMetrics`（`TMap<FName, FNetMetric>`）。供 `UReplicationSystem::CollectNetMetrics` 向上层分析/遥测系统输出。

### 3.4 PacketControl（[PacketControl.md](Modules/PacketControl.md)）

最小契约模块。单枚举 `EPacketDeliveryStatus { Delivered, Lost, Discard }`，是 `UDataStream::ProcessPacketDeliveryStatus` / `UDataStreamManager::ProcessPacketDeliveryStatus` 的唯一参数类型。

### 3.5 Serialization（[Serialization.md](Modules/Serialization.md)）

Iris 的"底层数据层"，包含三层：

- **位流原语**：`FNetBitStreamReader/Writer`（支持 substream/rollback/overflow）、`NetBitStreamUtil`（PackedUint/String/Vector/Rotator/SparseBitArray）、`BitPacking`（Delta 整数 + 单位浮点量化）。
- **Serializer 契约**：`FNetSerializer` 函数表 + `FNetSerializationContext` 聚合对象 + `FNetSerializerConfig` 基类 + `TNetSerializer<Impl>` 模板 + `TNetSerializerBuilder<>` SFINAE 探测 + `UE_NET_DECLARE/IMPLEMENT_SERIALIZER` 宏。
- **具体 Serializer**：一大批 `FXxxNetSerializer`——标量（Int/Uint/Range/Packed/Enum/Float/Bool/Bitfield/Date/Guid/Nop/NetRole）、字符串（Name/Str/NameAsNetToken）、数学（Vector/PackedVector/Rotator/Quat）、对象引用（Object/ObjectPtr/WeakObject/ScriptInterface/SoftObject/SoftObjectPath/Remote*）、结构化（Struct/Array/InstancedStruct/Polymorphic/LastResortProperty/FieldPath）。

以及：

- **上下文**：`FNetSerializationContext`、`FNetErrorContext` + `GNetError_*` FName、`FNetJournal`（环形 read log）、`FInternalNetSerializationContext`（持有 `UReplicationSystem*` / `FObjectReferenceCache*` / 分配器）、`FNetExportContext`。
- **引用收集**：`FNetReferenceCollector`、`FQuantizedObjectReference`。
- **PackageMap 桥接**：`UIrisObjectReferencePackageMap` + `FIrisPackageMapExportsUtil`（兼容旧 `FProperty::NetSerialize`）。
- **存储**：`FNetSerializerArrayStorage<T, Policy>` 动态量化存储。
- **扩展点**：`FNetSerializerRegistryDelegates`（继承并在 PreFreeze/PostFreeze 注册自家 Serializer）。

### 3.6 DataStream（[DataStream.md](Modules/DataStream.md)）

连接内多流复用框架（中间件层）：

- **`UDataStream`**：抽象基类。生命周期 `Init/Deinit/Update`；写入管线 `BeginWrite → WriteData → EndWrite`；读取 `ReadData`；投递反馈 `ProcessPacketDeliveryStatus`。
- **`FDataStreamRecord`**：流自定义的 per-packet 记录，用于 ACK/NACK 处理。
- **`UDataStreamManager`**：组合模式——自身也是 `UDataStream`，内部聚合最多 32 条子流，做状态机握手（`PendingCreate/WaitOnCreateConfirmation/Open/PendingClose/WaitOnCloseConfirmation/Invalid`）、序列化流开关位图、ACK 分发。
- **`UDataStreamDefinitions`**（Engine ini 配置）：声明所有可用流类型（Name/Class/AutoCreate/DynamicCreate/DefaultSendStatus）。
- **扩展点**：继承 `UDataStream` + 在 `DefaultEngine.ini` 的 `[/Script/IrisCore.DataStreamDefinitions]` 注册。

### 3.7 ReplicationState（[ReplicationState.md](Modules/ReplicationState.md)）

Iris 的"数据模型"层——反射烘焙：

- **`FReplicationStateDescriptor`**（引用计数，不可变，可跨连接共享）：描述一块可复制状态的完整形态——成员描述、Serializer、ChangeMask、RepTag、LifetimeCondition、Reference、Trait（含 PushBased/InitOnly/HasDynamicState/IsFastArray/…）、RepIndex 映射、默认状态缓冲、构造/析构函数指针、`CreateAndRegisterReplicationFragmentFunc`。
- **`FReplicationStateDescriptorBuilder`**：从反射（UClass/UScriptStruct/UFunction）烘焙 Descriptor 的唯一入口；支持 FastArray 特化、单属性裁切、`FReplicationStateDescriptorRegistry` 去重复用。
- **`FPropertyReplicationState`**：运行时"带 Descriptor 的状态缓冲壳"——Poll(full/push/ref-only)、Push、StoreSnapshot + CallRepNotifies、CopyDirty、IsDirty、CustomConditionEnabled。
- **`FReplicationStateStorage`**：per-object SendState/RecvState/Baseline 分配管理（Delta Compression 基础）。
- **`FPropertyNetSerializerInfoRegistry`**：`FProperty` / Struct 名 → `FNetSerializer + Config` 静态注册表，以及 `DefaultPropertyNetSerializerInfos` 默认注册。
- **`FIrisFastArraySerializer`** / **`TIrisFastArrayEditor`**：继承 UE 原 `FFastArraySerializer`，内嵌 `FReplicationStateHeader` + ChangeMaskStorage[4]，并与 `GlobalDirtyNetObjectTracker` 集成；推模式 Editor 减少 poll。
- **扩展点**：`UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO`、`UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_WITH_CUSTOM_FRAGMENT_INFO`、`UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO`、`CreateAndRegisterReplicationFragmentFunc`（不鼓励）、`UReplicationStateDescriptorConfig`（ini 白名单）。

### 3.8 ReplicationSystem（[ReplicationSystem.md](Modules/ReplicationSystem.md)）

Iris 的"业务核心"，是最大的模块（约 160 个文件、7 个子目录）。

**顶层协调者**：

- **`UReplicationSystem`**（门面，PImpl）：暴露 NetUpdate/TickPostReceive/PreReceiveUpdate/PostReceiveUpdate/PostSendUpdate/SendUpdate 帧循环；对外所有 Get/Set（Filter/Prioritizer/Group/Condition/CullDistance/Owner/TearOff/SendRPC/…）。
- **`FReplicationSystemImpl`**（内部编排者）：分阶段调度 `StartPreSendUpdate → UpdateWorldLocations → UpdateFiltering → UpdateConnectionSpecifics(Poll+Copy) → QuantizeDirtyStateData → UpdateObjectScopes → PropagateDirtyChanges → UpdatePrioritization → DeltaCompression.PreSend → (由 NetDriver 触发 Write) → EndPostSendUpdate`。
- **`FReplicationSystemInternal`**（聚合根）：持有所有子系统的私有实例。
- **`FReplicationSystemFactory`**：全局 RS 实例注册表（支持 PIE 多系统）。

**Bridge（游戏世界 ↔ Iris）**：

- **`UReplicationBridge`**：抽象基类。
- **`UObjectReplicationBridge`**：UObject 标准 Bridge；`StartReplicatingRootObject/SubObject/StopReplicatingNetObject`；PreUpdate+Poll+Copy；Class 级配置；Dormancy；Dependent/Creation dep；World Location；PIE 重映射。

**NetRefHandle / Factory**：

- **`FNetRefHandle`**（64-bit：54 Id + 10 RS Id）、`FNetRefHandleManager`（所有注册 NetObject 的中央表）、`UNetObjectFactory` + `UNetObjectFactoryRegistry` + `FNetObjectCreationHeader`。

**协议（形状 + 实例）**：

- **`FReplicationProtocol`**（共享形态，含 `FReplicationStateDescriptor[]` / InternalSize / ChangeMaskBitCount / Traits / ProtocolIdentifier）、`FReplicationInstanceProtocol`（每实例 Fragment 表）、`FReplicationProtocolManager`（缓存去重）。

**I/O（每连接一套）**：

- **`FReplicationWriter`**（状态机 PerObjectInfo + HugeObject 通道 + 调度/优先级 + `FReplicationRecord` ACK 回滚）、
- **`FReplicationReader`**（按 batch 解码创建/销毁/状态/附件；`PendingBatchData` 等待解引；`DequantizeAndApplyHelper`）、
- **`UReplicationDataStream`**（接入 DataStream 框架的具体 stream）。

**7 个子模块（均有独立子文档）**：

| 子模块 | 路径 | 职责 |
|---|---|---|
| Filtering | `Filtering/` | Scope 计算（Exclusion → Dynamic UNetObjectFilter → Inclusion + Hysteresis + SubObjectFilter） |
| Prioritization | `Prioritization/` | 打分排序（UNetObjectPrioritizer / Location / Sphere / FOV / CountLimiter / ConnectionFilter） |
| Conditionals | `Conditionals/` | LifetimeCondition + 动态 Condition → 每对象/每连接 conditional ChangeMask |
| DeltaCompression | `DeltaCompression/` | per-connection 基线快照 + 失效跟踪 + 基线存储池 |
| NetBlob | `NetBlob/` | RPC / Attachment / Huge / Reliable / Raw / ShrinkWrap / Partial / Handler + Manager（`SendRPC` 底层） |
| Polling | `Polling/` | `FObjectPoller`（并行任务化）+ `FObjectPollFrequencyLimiter` |
| ChunkedDataStream | `ChunkedDataStream/` | 与对象复制解耦的大数据分块传输流（独立 UDataStream 派生） |

**辅助 & 周边**：`FWorldLocations`、`FDirtyNetObjectTracker` + `LegacyPushModel`、`FReplicationConnections`、`FObjectReferenceCache`、`FNetExports`、`FNetDependencyData`、`FNetTokenStore`/`FStringTokenStore`/`FNameTokenStore`/`FStructNetTokenDataStore`、`AttachmentReplication`、`ChangeMaskCache/Util`、`ReplicationOperations`、`PropertyReplicationFragment`、`FastArrayReplicationFragment`、`RepTag`、`ReplicationView`、`NetObjectGroupHandle`。

### 3.9 顶层：IrisCoreModule + IrisConfig

- **`FIrisCoreModule::StartupModule()`**：
  1. 强制加载 `NetCore` 模块；
  2. 订阅 `OnAllModuleLoadingPhasesComplete`；
  3. 根据命令行覆盖 `net.Iris.UseIrisReplication`；
  4. 调用 `RegisterPropertyNetSerializerSelectorTypes()`：`PreFreeze 广播 → RegisterDefaultPropertyNetSerializerInfos → Freeze → PostFreeze 广播`；
  5. `UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL()` 注册 PushModel 桥接；
  6. 订阅 `FModuleManager::OnModulesChanged` / `RepSystemCreated/Destroyed` 回调；
  7. NetTrace 下重置 `LifetimeConditionDebugNames`。
- **`IrisConfig`**：`EReplicationSystem` 枚举（Default/Legacy/Iris）与命令行解析。
- **`IrisConfigInternal.h`**：内部 validate 开关（BitStreamWriter Validate 等）。

---

## 4. 一次典型帧循环（服务器端）

> 以 `UReplicationSystem::NetUpdate(DeltaSeconds)` 为入口，结合 `FReplicationSystemImpl` 中的阶段。

```
NetUpdate(DeltaSeconds)
  1. ElapsedTime 累加 + 必要时 CollectGarbage
  2. UpdateDataStreams(PreSendUpdate)         — DataStreamManager 预更新（ReplicationDataStream / ChunkedDataStream / NetTokenDataStream …）
  3. StartPreSendUpdate                       — Bridge.OnStartPreSendUpdate / NetRefHandleManager.OnPreSendUpdate
  4. DirtyNetObjectTracker.UpdateDirtyNetObjects  ← 合并 GlobalDirtyNetObjectTracker + LegacyPushModel 标脏
  5. Bridge.CallUpdateInstancesWorldLocation      → FWorldLocations 刷新（供 Filter/Prioritizer）
  6. ReplicationFiltering.Filter                  → Exclusion→Dynamic→Inclusion + Hysteresis
  7. Bridge.CallPreSendUpdate(DeltaSeconds)      → BuildPollList + PreUpdate + FinalizeDirty + PollAndCopy（并行）
  8. DirtyNetObjectTracker.UpdateAccumulatedDirtyList + WorldLocations.LockDirtyInfoList(true)
  9. Conditionals.Update                          → 计算 per-conn conditional ChangeMask
 10. QuantizeDirtyStateData                       → ReplicationInstanceOperationsInternal.QuantizeObjectStateData → ChangeMaskCache
 11. NetBlobManager.ProcessOutOfScopeAttachments
 12. per-conn: Filtering.GetRelevantObjectsInScope → ReplicationWriter.UpdateScope
 13. ReplicationWriter.UpdateDirtyChangeMasks(ChangeMaskCache)
 14. NetBlobManager.ProcessInScopeAttachments + ResetSendQueue
 15. Prioritization.Prioritize(ReplicatingConns, DirtyAndRelevantObjects)
 16. DeltaCompressionBaselineManager.PreSendUpdate(ChangeMaskCache)
 17. UnresolvableReferenceTracking + NetRefHandleManager.DestroyObjectsPendingDestroy

SendUpdate(SendFn)
  → 由 NetDriver Tick 触发：DataStreamManager.WriteData → ReplicationDataStream.WriteData → ReplicationWriter.Write
    （HugeObject 通道 → 按 priority 选对象 → batch 写入 state/creation/destroy → 附件 → Exports）

PostSendUpdate(TickFlush)
  1. ResetObjectStateDirtiness
  2. EndPostSendUpdate:
       ChangeMaskCache.ResetCache
       NetRefHandleManager.OnPostSendUpdate
       Bridge.UpdateHandlesPendingEndReplication
       DeltaCompressionBaselineInvalidationTracker.PostSendUpdate
       WorldLocations.PostSendUpdate
       Bridge.OnPostSendUpdate
  3. DeltaCompressionBaselineManager.PostSendUpdate（回收过期基线）
  4. UpdateDataStreams(PostTickFlush)
  5. 解锁 FilterChanges
```

**接收侧**：

```
PreReceiveUpdate → Bridge.PreReceiveUpdate (清 HandlesToStopReplicating / 置 bInReceiveUpdate)

(NetDriver 读包) → UDataStreamChannel → DataStreamManager.Read → ReplicationDataStream.ReadData → ReplicationReader.Read
  · 读 Export（NetExports / TokenStore）
  · 按 batch 解析：NetRefHandle 查/建（CreationHeader → Factory → ObjectReferenceCache 绑定） / 销毁 / 状态 Deserialize → DequantizeAndApply → RepNotify / 附件经 NetBlobHandlerManager 分发
  · 无法解析的 batch → PendingBatchData 推迟

PostReceiveUpdate → Bridge.PostReceiveUpdate → OnPostReceiveUpdate (延期 detach / pending StopReplicating)

TickPostReceive → ProcessOOBNetObjectAttachmentSendQueue（把 SendImmediate RPC 塞进 WriterQueue，标记需立即发包）
```

---

## 5. 扩展点一览（外部模块如何扩展 Iris）

| 维度 | 入口 | 备注 |
|------|------|------|
| 新 NetSerializer | 继承 `FNetSerializerRegistryDelegates` → PreFreeze/PostFreeze 注册；`UE_NET_DECLARE_SERIALIZER` + `UE_NET_IMPLEMENT_SERIALIZER` 宏声明 | 详见 `Serialization.md` |
| 新 Property → NetSerializer 绑定 | `UE_NET_IMPLEMENT_NETSERIALIZER_INFO` / `UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO` 宏 + `FPropertyNetSerializerInfoRegistry::Register` | 详见 `ReplicationState.md` |
| Struct 自定义 Fragment | `CreateAndRegisterReplicationFragmentFunc`（Descriptor）+ `UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_WITH_CUSTOM_FRAGMENT_INFO` | 官方标注"highly discouraged"，仅高级用途 |
| 多态 struct 注册 | `TPolymorphicStructNetSerializerImpl` / `TPolymorphicArrayStructNetSerializerImpl` + `FPolymorphicStructNetSerializerConfig::RegisteredTypes` | 运行时最多 31 类型（5-bit） |
| InstancedStruct 白名单 | `FInstancedStructNetSerializerConfig::SupportedTypes`（`TArray<TSoftObjectPtr<UScriptStruct>>`） | LRU 缓存 Descriptor |
| 新 DataStream | 继承 `UDataStream` + 在 `[/Script/IrisCore.DataStreamDefinitions]` ini 注册 | 详见 `DataStream.md` |
| 新 Filter | 继承 `UNetObjectFilter` + 在 `UNetObjectFilterDefinitions` 注册 | 详见 `ReplicationSystem.md` |
| 新 Prioritizer | 继承 `UNetObjectPrioritizer` + 在 `UNetObjectPrioritizerDefinitions` 注册 | 详见 `ReplicationSystem.md` |
| 新 NetBlob/Handler | 继承 `UNetBlobHandler` + 在 `UNetBlobHandlerDefinitions` 注册；`FNetBlob` 子类定义 payload | 详见 `ReplicationSystem.md` |
| 新 NetObjectFactory | 继承 `UNetObjectFactory` + `UNetObjectFactoryRegistry` 注册 | Actor/Component 等都是 Factory |
| 旧 NetSerialize 兼容 | `UIrisObjectReferencePackageMap` + `FIrisPackageMapExportsUtil` 捕获并用 Iris 统一导出；或用 `FLastResortPropertyNetSerializer` 兜底 | 详见 `Serialization.md` |
| PushModel 桥接 | 宏 `MARK_PROPERTY_DIRTY(…)` → `FNetHandleLegacyPushModelHelper` → `FDirtyNetObjectTracker` | 详见 `ReplicationSystem.md` |
| 错误/关键错误钩子 | `FIrisDelegates::GetCriticalErrorDetectedDelegate()` 全局多播；序列化层 `Context.SetError(GNetError_*)` | 详见 `Core.md` / `Serialization.md` |

---

## 6. 与旧版 Replication 的差异要点

1. **对象身份**：旧 `FNetworkGUID`（动态分配）→ 新 `FNetRefHandle`（64-bit，54-bit Id + 10-bit ReplicationSystemId，支持 static/dynamic）。
2. **引用缓存**：旧 `FNetGUIDCache` → 新 `FObjectReferenceCache`（集成路径 NetToken、PIE 重映射、async loading pending batch）。
3. **状态描述**：旧 `FRepLayout`（每次 diff 扫描） → 新 `FReplicationStateDescriptor`（引用计数、跨连接共享、ChangeMask 位预分配、支持 PushModel）。
4. **序列化**：旧 `NetSerialize(FArchive&)` → 新 `FNetSerializer` 函数表 + `FNetBitStream*`（精确到位）+ 量化/反量化两步。
5. **子系统正交**：旧逻辑大多在 `UActorChannel` / `FRepLayout` 内耦合 → 新 Filter / Prioritizer / DeltaCompression / Condition / Blob 都是独立可插拔子系统，有自己的生命周期与 ini 注册表。
6. **PushModel 原生化**：新体系以 `FGlobalDirtyNetObjectTracker` / `FDirtyNetObjectTracker` 为主，`FNetHandleLegacyPushModelHelper` 仅作过渡兼容。
7. **多 RS 支持**：PIE 多实例各一套 `UReplicationSystem`，通过 `FReplicationSystemFactory::GetAllReplicationSystems` 管理。
8. **多线程友好**：Poll/Quantize/Write 各阶段支持并行 `LowLevelTasks`，Stats 用 per-task Child context 汇聚。

---

## 7. 本注释工作的组织方式

按上节"分层与拓扑"分 **5 个阶段** 推进。每阶段的进入条件是"其依赖层已全部完成注释"，保证跨文件的概念一致性。

| 阶段 | 目标模块 | 并行 subagent 粒度 |
|------|---------|--------------------|
| 阶段 1 | PacketControl / Metrics / Core | 3 个 agent（每模块 1 个） |
| 阶段 2 | Stats / Serialization | Stats 1 agent + Serialization 按子类拆 3-4 agent |
| 阶段 3 | DataStream / ReplicationState | 各 1 agent，ReplicationState 按 "Descriptor/Builder" "State Runtime" "FastArray+Registry" 拆 3 agent |
| 阶段 4 | ReplicationSystem | 按 7 子目录 + 根目录拆 8 agent |
| 阶段 5 | IrisCoreModule / IrisConfig | 1 agent |

每阶段完成后：
1. 回写更新本文档和对应 `Docs/Modules/*.md` 的"注释确认"一节；
2. 修正与实际代码不符的叙述；
3. 补充遗漏的概念。

---

## 8. 相关外部参考

- UE 源码内更基础的网络设施：`Engine/Source/Runtime/Net/Core/`（`FNetBitArray` / `FGlobalDirtyNetObjectTracker` / `FNetToken` / `FNetHandle` 等）；
- UE NetDriver/ActorChannel（老路径，与 Iris 经 `UNetDriver::IsUsingIrisReplication` 互斥）；
- `NetCore.Build.cs` / `IrisCore.Build.cs` 可看清编译期模块边界。
