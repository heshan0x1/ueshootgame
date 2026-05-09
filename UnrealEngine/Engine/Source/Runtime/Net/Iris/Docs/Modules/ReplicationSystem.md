# ReplicationSystem 模块

> 路径：`Engine/Source/Runtime/Net/Iris/{Public,Private}/Iris/ReplicationSystem/`  
> 分层：**L4 业务核心层**（Iris 最大、最复杂的模块，约 160 个文件、7 个子目录）  
> 职责：Iris 的"复制大脑"——总门面 `UReplicationSystem` + 对游戏世界的 `UObjectReplicationBridge` + 7 个正交子系统（Filtering/Prioritization/Conditionals/DeltaCompression/NetBlob/Polling/ChunkedDataStream）+ Reader/Writer/Protocol/NetRefHandle/NetObjectFactory/ObjectReferenceCache/WorldLocations/DirtyTracker/TokenStores 等。

---

## 1. 模块地图

```
ReplicationSystem/ (根)
├── ReplicationSystem.{h,cpp}            # UReplicationSystem 门面 + FReplicationSystemImpl
├── ReplicationSystemInternal.h          # FReplicationSystemInternal（聚合根）
├── ReplicationBridge.{h,cpp}            # UReplicationBridge 抽象基类
├── ReplicationBridgeTypes.{h,cpp}       # FNetObjectCreationHeader、EEndReplicationFlags、FObjectReplicationBridgeInstantiateResult 等
├── ObjectReplicationBridge.{cpp,h(Public)}           # UObjectReplicationBridge：UObject 标准 Bridge
├── ObjectReplicationBridgeConfig.{cpp,h(Public)}     # Class 级配置（PollFrequency / DeltaCompression / Dormancy ...）
├── ObjectReplicationBridgeDebugging.cpp
├── NetRefHandle.{h(Public),cpp}         # FNetRefHandle（64-bit ID）
├── NetRefHandleManager.{h,cpp}          # 中央登记表
├── NetRefHandleManagerTypes.h           # FReplicatedObjectData / 标志位等
├── NetObjectFactory.{cpp,h(Public)}     # UNetObjectFactory
├── NetObjectFactoryRegistry.{cpp,h(Public)}
├── ReplicationProtocol.{h,cpp}          # FReplicationProtocol / FReplicationInstanceProtocol
├── ReplicationProtocolManager.{h,cpp}   # 缓存去重
├── ReplicationReader.{h,cpp}            # FReplicationReader（每连接）
├── ReplicationWriter.{h,cpp}            # FReplicationWriter（每连接）
├── ReplicationDataStream.{h,cpp}        # UReplicationDataStream（接入 DataStream 框架）
├── ReplicationDataStreamDebug.h
├── ReplicationConnections.{h,cpp}       # FReplicationConnections（per-conn 信息聚合）
├── ReplicationOperations.{cpp,h(Public)}            # FReplicationOperations 门面
├── ReplicationOperationsInternal.{h,cpp}            # Quantize / Copy 内部实现
├── ReplicationFragment.{h(Public)}                  # FReplicationFragment 抽象
├── ReplicationFragmentInternal.h
├── ReplicationFragmentUtil.{cpp,h(Public)}
├── ReplicationTypes.{h,cpp}             # FReplicationParameters 等
├── ReplicationRecord.h                  # 每次传输的记录（ChangeMask/Baseline/Exports）
├── ReplicationView.h(Public)            # FReplicationView
├── NetObjectGroupHandle.h(Public)
├── PropertyReplicationFragment.{cpp,h(Public)}      # FPropertyReplicationFragment（基于 UProperty）
├── FastArrayReplicationFragment.{cpp,h(Public)}     # FFastArrayReplicationFragment
├── Private/FastArrayReplicationFragmentInternal.h
├── TypedReplicationFragment.h(Public)   # TReplicationFragment<> 模板便捷基类
├── AttachmentReplication.{h,cpp}        # FNetObjectAttachmentSendQueue / *Reader（Normal/OOB/Huge）
├── ChangeMaskCache.h + ChangeMaskUtil.{h,cpp}       # ChangeMask 缓存与位操作
├── DequantizeAndApplyHelper.{h,cpp}                 # 接收端 Internal→External → Apply
├── DirtyNetObjectTracker.{h,cpp}        # 对接 FGlobalDirtyNetObjectTracker + LegacyPushModel
├── LegacyPushModel.{h,cpp}              # 旧 PushModel 宏桥接
├── NetDependencyData.{h,cpp}            # SubObject/Dependent/CreationDependency 邻接表
├── NetExports.{h,cpp}                   # 每连接的 NetToken/ObjectReference 导出 ACK 跟踪
├── ObjectPollFrequencyLimiter.{h,cpp}   # 按帧周期筛选的"应轮询"对象集合
├── ObjectReferenceCache.{h,cpp} + ObjectReferenceCacheFwd.h  # 全局 UObject ↔ FNetObjectReference 映射
├── ObjectReferenceTypes.{cpp,h(Public)}
├── PendingBatchData.h                   # Reader 端等引用解析/async load 完成的 batch
├── RepTag.{cpp,h(Public)}               # Tag 系统
├── WorldLocations.{cpp,h(Public)}       # 每对象世界位置 + CullDistance
├── NameTokenStore.{cpp,h(Public)} + StringTokenStore.{cpp,h(Public)} + NetTokenStore.{cpp,h(Public)} + StructNetTokenDataStore.h(Public) + NetTokenStructDefines.h(Public)
├── NetTokenDataStream.{h,cpp}           # 把 token export 作为独立 DataStream
│
├── ChunkedDataStream/                   # ← 子模块 1
│   ├── Public/ChunkedDataStream.h
│   ├── Private/ChunkedDataStream.cpp
│   ├── Private/ChunkedDataReader.{h,cpp}
│   ├── Private/ChunkedDataWriter.{h,cpp}
│   └── Private/ChunkedDataStreamCommon.h
│
├── Conditionals/                        # ← 子模块 2
│   ├── Public/ReplicationCondition.h
│   └── Private/ReplicationConditionals.{h,cpp}
│
├── DeltaCompression/                    # ← 子模块 3（Private-only）
│   ├── DeltaCompressionBaseline.{h,cpp}
│   ├── DeltaCompressionBaselineStorage.{h,cpp}
│   ├── DeltaCompressionBaselineManager.{h,cpp}
│   └── DeltaCompressionBaselineInvalidationTracker.{h,cpp}
│
├── Filtering/                           # ← 子模块 4
│   ├── Public/NetObjectFilter.h + NetObjectFilterDefinitions.h + ReplicationFilteringConfig.h
│   ├── Public/AlwaysRelevantNetObjectFilter.h + FilterOutNetObjectFilter.h + NetObjectConnectionFilter.h + NetObjectGridFilter.h + SharedConnectionFilterStatus.h
│   ├── Private/ReplicationFiltering.{h,cpp}
│   ├── Private/NetObjectGroups.{h,cpp}
│   ├── Private/ObjectScopeHysteresisUpdater.{h,cpp}
│   └── Private/AlwaysRelevant*.cpp / FilterOut*.cpp / NetObjectGridFilter.cpp / NetObjectFilter.cpp / NetObjectFilterDefinitions.cpp / SharedConnectionFilterStatus.cpp
│
├── NetBlob/                             # ← 子模块 5（最大）
│   ├── Public/NetBlob.h + NetBlobHandler.h + NetBlobAssembler.h + PartialNetBlob.h + RawDataNetBlob.h + ReliableNetBlobQueue.h + SequentialPartialNetBlobHandler.h + ShrinkWrapNetBlob.h
│   ├── Private/NetBlobManager.{h,cpp}
│   ├── Private/NetBlobHandlerManager.{h,cpp} + NetBlobHandlerDefinitions.{h,cpp}
│   ├── Private/NetObjectBlobHandler.{h,cpp}
│   ├── Private/NetRPC.{h,cpp} + NetRPCHandler.{h,cpp}
│   ├── Private/PartialNetObjectAttachmentHandler.{h,cpp}
│   └── Private/RawDataNetBlob.cpp / ReliableNetBlobQueue.cpp / SequentialPartialNetBlobHandler.cpp / ShrinkWrapNetBlob.cpp / PartialNetBlob.cpp / NetBlob.cpp / NetBlobAssembler.cpp / NetBlobHandler.cpp
│
├── Polling/                             # ← 子模块 6
│   └── Private/ObjectPoller.{h,cpp}     # （ObjectPollFrequencyLimiter 在根目录）
│
└── Prioritization/                      # ← 子模块 7
    ├── Public/NetObjectPrioritizer.h + LocationBasedNetObjectPrioritizer.h + SphereNetObjectPrioritizer.h + SphereWithOwnerBoostNetObjectPrioritizer.h + FieldOfViewNetObjectPrioritizer.h + NetObjectCountLimiter.h
    ├── Private/ReplicationPrioritization.{h,cpp}
    ├── Private/NetObjectPrioritizerDefinitions.{h,cpp}
    └── Private/各 Prioritizer 实现 .cpp + NetObjectConnectionFilter.cpp
```

---

## 2. 顶层协调者

### 2.1 `UReplicationSystem`（`Public/ReplicationSystem.h`）

门面类（PImpl）。由 `FReplicationSystemFactory` 创建。对外暴露：

**帧循环**：
- `NetUpdate(DeltaSeconds)` / `TickPostReceive()`
- `PreReceiveUpdate()` / `PostReceiveUpdate()`
- `SendUpdate(SendFn)` / `PostSendUpdate()`

**连接**：`AddConnection` / `RemoveConnection` / `IsValidConnection` / `SetConnectionGracefullyClosing` / `SetReplicationEnabledForConnection` / `IsReplicationEnabledForConnection` / `SetReplicationView` / `SetConnectionUserData` / `GetConnectionUserData` / `InitDataStreamManager` / `GetDataStream` / `OpenDataStream` / `CloseDataStream` / `IsKnownDataStreamDefinition`。

**Prioritization**：`SetStaticPriority` / `SetPrioritizer` / `GetPrioritizerHandle` / `GetPrioritizer`。

**Filtering**：`SetFilter` / `GetFilterHandle` / `GetFilter` / `GetFilterName` / `AddExclusionFilterGroup` / `AddInclusionFilterGroup` / `RemoveGroupFilter` / `SetGroupFilterStatus`（多重载）/ `GetOrCreateSubObjectFilter` / `SetSubObjectFilterStatus` / `RemoveSubObjectFilter`。

**Group**：`CreateGroup` / `DestroyGroup` / `FindGroup` / `IsValidGroup` / `AddToGroup` / `RemoveFromGroup` / `RemoveFromAllGroups` / `IsInGroup` / `GetNotReplicatedNetObjectGroup` / `GetNetGroupOwnerNetObjectGroup` / `GetNetGroupReplayNetObjectGroup`。

**Condition / Owner / 特殊**：`SetReplicationCondition` / `SetReplicationConditionConnectionFilter` / `SetOwningNetConnection` / `GetOwningNetConnection` / `SetDeltaCompressionStatus` / `SetIsNetTemporary` / `TearOffNextUpdate` / `ForceNetUpdate` / `MarkDirty` / `SetCullDistanceOverride` / `ClearCullDistanceOverride` / `GetCullDistance`。

**NetBlob / RPC**：`RegisterNetBlobHandler` / `QueueNetObjectAttachment` / `SendRPC`（multicast/unicast）/ `SetRPCSendPolicyFlags` / `ResetRPCSendPolicyFlags`。

**Meta / 调试**：`GetReplicationBridge[As<T>]` / `GetNetTokenStore` / `GetNetTokenResolveContext` / `IsNetRefHandleAssigned` / `GetReplicationProtocol` / `GetDebugName` / `GetWorldLocations` / `CollectNetMetrics` / `ResetNetMetrics` / `ReportProtocolMismatch` / `ReportErrorWithNetRefHandle` / `GetDelegates` / `GetElapsedTime` / `GetPIEInstanceID`。

### 2.2 `FReplicationSystemImpl`（实际编排者，位于 `ReplicationSystem.cpp` 内部）

分阶段调度（见 `Iris_Architecture.md §4` 帧循环细节）：

```
StartPreSendUpdate → UpdateWorldLocations → UpdateFiltering → UpdateConnectionSpecifics(Poll+Copy)
  → QuantizeDirtyStateData → UpdateObjectScopes → PropagateDirtyChanges → UpdatePrioritization
  → DeltaCompression.PreSend → (Write 由 NetDriver 触发) → EndPostSendUpdate
```

### 2.3 `FReplicationSystemInternal`（聚合根，`ReplicationSystemInternal.h`）

持有所有内部子系统实例：`FReplicationProtocolManager` / `FNetRefHandleManager` / `FReplicationStateDescriptorRegistry` / `FReplicationStateStorage` / `FDirtyNetObjectTracker` / `FChangeMaskCache` / `FReplicationConnections` / `FReplicationFiltering` / `FNetObjectGroups` / `FReplicationConditionals` / `FReplicationPrioritization` / `FObjectReferenceCache` / `FNetBlobManager` / `FWorldLocations` / `FDeltaCompressionBaselineManager` / `FDeltaCompressionBaselineInvalidationTracker` / `FNetSendStats` / `FNetTypeStats` / `UObjectReplicationBridge*`。

### 2.4 `FReplicationSystemFactory`

全局 RS 实例注册表（支持 PIE 多系统）：`CreateReplicationSystem / DestroyReplicationSystem / GetAllReplicationSystems`，以及 `FReplicationSystemCreatedDelegate/DestroyedDelegate`。

---

## 3. Bridge（游戏世界 ↔ Iris）

### 3.1 `UReplicationBridge`（抽象基类）

提供 `StopReplicatingNetRefHandle` / `CacheNetRefHandleCreationInfo` / `DetachInstance` / `PreSendUpdate` / `OnPostSendUpdate` / `OnPostReceiveUpdate` 等 hook；友元访问 `FNetRefHandleManager` / `FReplicationProtocolManager` / `FObjectReferenceCache` / `FNetObjectGroups`。还负责 LevelGroup、PendingEndReplication、TearOff、flush state data。

### 3.2 `UObjectReplicationBridge`（标准 UObject Bridge）

核心对外 API：
- `StartReplicatingRootObject(UObject*, FRootObjectReplicationParams, FactoryId)`
- `StartReplicatingSubObject(UObject*, FSubObjectReplicationParams, FactoryId)`
- `StopReplicatingNetObject(UObject*, EEndReplicationFlags)`
- `GetReplicatedObject` / `GetReplicatedRefHandle`
- `AddDependentObject` / `RemoveDependentObject` / `AddCreationDependencyLink`
- `AddStaticDestructionInfo`
- `PreRegisterNewObjectReferenceHandle` / `PreRegisterObjectWithReferenceHandle`

PreUpdate + Poll + Copy 流程；绑定 NetFactory；class 级配置（Dynamic Filter/Prioritizer/PollFrequency/DeltaCompression/AsyncLoading/TypeStats/Critical）；Dormancy；Dependent/Creation Dependency；World Location；PIE 路径重映射。

---

## 4. 数据标识、协议、工厂

### 4.1 `FNetRefHandle`

64-bit：54-bit Id + 10-bit ReplicationSystemId；奇数 Id = static，偶数 = dynamic。是 Iris 所有"网络对象引用"的底层 ID。

### 4.2 `FNetRefHandleManager`

所有已注册 NetObject 的中央登记表：`FReplicatedObjectData` 数组（`Protocol` / `InstanceProtocol` / `InternalIndex` / SubObjects / Dependents / Flags）+ 多个 `FNetBitArray`（ScopedObjects / RelevantObjects / DirtyObjectsToQuantize / PolledObjects / WantsToBeDormant）+ `OnPreSendUpdate` / `OnPostSendUpdate` / `DestroyObjectsPendingDestroy` / SubObject & Dependent 拓扑维护。

### 4.3 `UNetObjectFactory` + `UNetObjectFactoryRegistry`

抽象工厂：根据 NetFactoryId 决定接收端如何实例化/查找对象、序列化 CreationHeader。Actor / Component 等各一套。

### 4.4 `FReplicationProtocol` / `FReplicationInstanceProtocol` / `FReplicationProtocolManager`

- `FReplicationProtocol`：一类对象共享的不可变描述（`ReplicationStateDescriptors[]` / InternalSize / Alignment / ChangeMaskBitCount / ConditionalChangeMask 偏移 / Traits / ProtocolIdentifier），引用计数。
- `FReplicationInstanceProtocol`：每实例的 `FragmentData[]`（ExternalSrcBuffer 指向真实 UObject 内属性地址）与 Fragments + InstanceTraits。是 Poll/Copy 阶段的入口表。
- `FReplicationProtocolManager`：全局缓存 + 去重（同 ProtocolId 只建一次）。

---

## 5. I/O（每连接一套）

- `FReplicationWriter`：状态机 PerObjectInfo（`PendingCreate → WaitOnCreateConfirmation → Created → PendingTearOff → PendingDestroy`）、per-object 调度 priority、`FReplicationRecord`（记录已发送的 ChangeMask/基线/版本）、HugeObject 通道、Attachment 发送队列。核心：`UpdateScope / UpdateDirtyChangeMasks / Write / ProcessPacketDeliveryStatus`。
- `FReplicationReader`：按 batch 解析对象创建/销毁/状态/附件；处理 async-loading 阻塞（`PendingBatchData`）、引用解析（`FObjectReferenceCache`）、`DequantizeAndApplyHelper`。
- `UReplicationDataStream`：`UDataStream` 派生，接入 `UDataStreamManager` 框架。

---

## 6. 7 个子模块

### 6.1 Filtering（Scope 计算）

Exclusion → Dynamic → Inclusion 三阶段：
- **`FReplicationFiltering`**：核心调度器，每帧 `Filter()` 得到每连接 `RelevantObjectsInScope`；含 `ObjectScopeHysteresisUpdater` 滞后处理。
- **`UNetObjectFilter`**：抽象基类；内置 `AlwaysRelevantNetObjectFilter` / `FilterOutNetObjectFilter` / `NetObjectConnectionFilter` / `NetObjectGridFilter`（空间网格兴趣管理） / `SharedConnectionFilterStatus`。
- **`UNetObjectFilterDefinitions`**（ini 注册）。
- **`FNetObjectGroups`**：Exclusion/Inclusion/SubObject Group 容器。

### 6.2 Prioritization（打分排序）

- **`FReplicationPrioritization`**：核心调度器。
- **`UNetObjectPrioritizer`**：抽象基类；内置 `LocationBasedNetObjectPrioritizer` / `SphereNetObjectPrioritizer` / `SphereWithOwnerBoostNetObjectPrioritizer` / `FieldOfViewNetObjectPrioritizer` / `NetObjectCountLimiter`。
- **`UNetObjectPrioritizerDefinitions`**（ini 注册）。

### 6.3 Conditionals

- **`EReplicationCondition`**：`RoleAutonomous` / `ReplicatePhysics`。
- **`FReplicationConditionals`**：每连接条件位图维护；`Update()` 时计算最终 conditional ChangeMask。

### 6.4 DeltaCompression

- **`FDeltaCompressionBaselineManager`**：入口，决定为对象/连接何时创建、分发、释放基线；`PreSendUpdate` / `PostSendUpdate`。
- **`FDeltaCompressionBaseline`**：基线快照数据结构与生命周期。
- **`FDeltaCompressionBaselineStorage`**：基线存储池（共享配额）。
- **`FDeltaCompressionBaselineInvalidationTracker`**：跟踪哪些对象/连接的基线需要作废。

### 6.5 NetBlob（通用消息载荷：RPC / Attachment / HugeObject）

- **`FNetBlob`** / **`FNetObjectAttachment`**：抽象基类。
- **`UNetBlobHandler`** / **`UNetBlobHandlerDefinitions`**（ini）：类型注册与序列化回调。
- **`FNetBlobManager`**：RPC/附件入队、按 mode 派发、OOB 立即发送。
- **`FNetBlobHandlerManager`**：Handler 注册、按 typeId 反查。
- **`FNetBlobAssembler`**：分片重组。
- **`FPartialNetBlob`**：超过 MTU 时的分片 blob。
- **`FRawDataNetBlob`**：承载原始字节流。
- **`FReliableNetBlobQueue`**：可靠 blob 有序队列。
- **`USequentialPartialNetBlobHandler`**：按序组装 HugeObject。
- **`FShrinkWrapNetBlob`**：包装已序列化数据不再二次量化。
- **`UNetObjectBlobHandler`**：对象状态专用 handler。
- **`UNetRPCHandler`** + **`FNetRPC`**：`SendRPC` 的底层 handler。
- **`UPartialNetObjectAttachmentHandler`**：对象附件分片 handler。

### 6.6 Polling

- **`FObjectPoller`**：执行 PreUpdate 回调 + 拷贝脏状态到内部 buffer，支持并行 `LowLevelTasks`。
- **`FObjectPollFrequencyLimiter`**（在根目录）：按帧周期筛选需轮询对象。

### 6.7 ChunkedDataStream

与对象复制解耦的大数据分块传输流（独立 `UDataStream` 派生）：
- **`UChunkedDataStream`**
- **`FChunkedDataWriter`**：切分 payload + 按可靠度发送。
- **`FChunkedDataReader`**：接收端重组 + 检测重复和丢包。

---

## 7. 辅助 & 周边

| 类 | 职责 |
|---|---|
| `FWorldLocations` | 每对象世界位置 + CullDistance + Override 集中存储；供 Grid/Sphere/FOV prioritizer/filter 使用 |
| `FDirtyNetObjectTracker` + `LegacyPushModel` | 对接 `FGlobalDirtyNetObjectTracker` + PushModel 宏桥接 |
| `FReplicationConnections` | 所有连接聚合；每连接 `{ ReplicationWriter, ReplicationReader, DataStreamManager, UserData, bIsClosing, ReplicationView }` |
| `FNetObjectAttachmentSendQueue/Reader` | 附件发送/接收队列（Normal / OOB / HugeObject） |
| `FNetExports` | 每连接 NetToken/ObjectReference 导出 ACK 跟踪 |
| `FNetDependencyData` | SubObject/ChildSubObject（含 LifetimeCondition）/DependentParent/CreationDependency 邻接表 |
| `FNetTokenStore` + `FStringTokenStore` + `FNameTokenStore` + `FStructNetTokenDataStore` | Token 数据存储；管理 Local/Remote Token 映射与导出 |
| `FNetTokenDataStream` | 把 token export 作为独立 DataStream |
| `FStaticDestructionInfo` | static actor 销毁指令描述 |
| `PendingBatchData` | Reader 端等待引用解析/async load 的 batch |
| `RepTag` | 标记 fragment/state 用途 |
| `ReplicationSystemTypes.h` | `ENetFilterStatus` / `EDependentObjectSchedulingHint` / `EReplicationFragmentTraits` / `ENetObjectDeltaCompressionStatus` / `ENetRefHandleError` 等 |
| `ReplicationSystemDelegates.h` | 系统级多播委托（连接/Handle/Protocol 事件） |
| `ReplicationOperations` / `ReplicationOperationsInternal` | Quantize/Dequantize/Serialize/Apply 门面 + 内部实现 |
| `ReplicationFragment` / `PropertyReplicationFragment` / `FastArrayReplicationFragment` / `TypedReplicationFragment` | Fragment 抽象与实现 |
| `ChangeMaskCache` / `ChangeMaskUtil` | ChangeMask 位图缓存与位操作 |
| `DequantizeAndApplyHelper` | 接收端 Internal→External → Apply 统一流程 |

---

## 8. 对外 API / 扩展点

### 8.1 接口类（UCLASS，可扩展）

- `UReplicationBridge` / `UObjectReplicationBridge`
- `UNetObjectFactory`
- `UNetObjectFilter`（+ `UNetObjectFilterDefinitions` ini）
- `UNetObjectPrioritizer`（+ `UNetObjectPrioritizerDefinitions` ini）
- `UNetBlobHandler`（+ `UNetBlobHandlerDefinitions` ini）
- `UDataStream`（NetToken/Chunked/Replication 均派生）

### 8.2 ini 配置

- `[/Script/IrisCore.NetObjectFilterDefinitions]`
- `[/Script/IrisCore.NetObjectPrioritizerDefinitions]`
- `[/Script/IrisCore.NetBlobHandlerDefinitions]`
- `[/Script/IrisCore.DataStreamDefinitions]`
- `[/Script/IrisCore.ReplicationFilteringConfig]`
- `[/Script/IrisCore.ObjectReplicationBridgeConfig]`

### 8.3 关键宏 / 委托

- `UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL()` / `UE_NET_IRIS_SHUTDOWN_LEGACY_PUSH_MODEL()`
- `FReplicationSystemCreatedDelegate` / `FReplicationSystemDestroyedDelegate`

---

## 9. 注释推进计划

- **文件数**：约 179 个（远超其他模块总和；分布：根目录 91 + Filtering 20 + NetBlob 30 + Prioritization 17 + DeltaCompression 8 + ChunkedDataStream 7 + Conditionals 3 + Polling 2 + Public/.../Private 1）。
- **并行度**：实际采用 **3 批次共 12 个 subagent**：
  - 第一批（4 agent）：根目录-协调/Bridge/Handle (25) + Filtering (20) + Conditionals+DeltaCompression (11) + ChunkedDataStream (7)
  - 第二批（4 agent）：根目录-协议/IO/DataStream (12) + 根目录-Operation/Fragment/Types (15) + Prioritization+Polling (19) + NetBlob 抽象/Manager/Handler (14)
  - 第三批（4 agent）：根目录-ChangeMask/Dirty/Conn/Attach (13) + 根目录-Tokens/Exports/Dep/RepTag (17) + 根目录-ObjectRef/WorldLoc/Poll (9) + NetBlob 具体类型 (15)
  - 加 1 个手工补完：PartialNetBlob.cpp。

### 注释完成情况

- [x] agent-R0 根目录-协调/Bridge/Handle/Factory（25 文件）
- [x] agent-R1a 根目录-协议/IO/DataStream/Record（12 文件）
- [x] agent-R1b 根目录-Operation/Fragment/Types（15 文件）
- [x] agent-R2a 根目录-ChangeMask/Dirty/Conn/Attach/Dequantize/Legacy（13 文件）
- [x] agent-R2b 根目录-Tokens/Exports/Dep/RepTag/PendingBatch（17 文件）
- [x] agent-R2c 根目录-ObjectRef/WorldLoc/Poll（9 文件）
- [x] agent-F Filtering/（20 文件）
- [x] agent-P Prioritization/+Polling/（19 文件）
- [x] agent-C Conditionals/+DeltaCompression/（11 文件）
- [x] agent-B-pt1 NetBlob 抽象/Manager/Handler（14 文件）
- [x] agent-B-pt2 NetBlob 具体类型 RPC/Partial/Reliable/Raw/Sequential/ShrinkWrap（15 文件）
- [x] agent-K ChunkedDataStream/（7 文件）
- [x] 手工补完：PartialNetBlob.cpp、Public/.../Private/FastArrayReplicationFragmentInternal.h（顺手完成）

**全部 179 个 ReplicationSystem 文件已添加中文全量注释。**
