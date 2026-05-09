# DataStream 模块

> 路径：`Engine/Source/Runtime/Net/Iris/{Public,Private}/Iris/DataStream/`  
> 分层：**L3 中间件层**（上承 ReplicationSystem，下承 Serialization；通过 `UDataStreamManager` 暴露给 NetDriver/NetConnection）  
> 职责：Iris 的可插拔"子数据流"抽象——每连接一个 `UDataStreamManager`，承载多条可按 `UDataStreamDefinitions` ini 配置的 `UDataStream`（状态复制流、RPC 流、Reliable 流、NetObject 创建流等），统一做 BeginWrite/WriteData/EndWrite/ReadData 与 ACK/Lost 通知。

---

## 1. 文件清单

| 路径 | 职责 |
|------|------|
| `Public/Iris/DataStream/DataStream.h` | `UDataStream` 抽象基类；`FDataStreamRecord`（ACK 跟踪记录基类）；`EDataStreamSendStatus` / `EDataStreamWriteMode`；`UDataStream::EDataStreamState` / `EWriteResult` / `EUpdateType` / `FInitParameters` / `FBeginWriteParameters` / `FUpdateParameters` 等 |
| `Public/Iris/DataStream/DataStreamManager.h` | `UDataStreamManager`（Pimpl）：`CreateStream` / `GetStream` / `CloseStream` / `SetSendStatus` / `GetStreamState` / `IsKnownStreamDefinition` / `GetNetExports`；`ECreateDataStreamResult` |
| `Private/Iris/DataStream/DataStream.cpp` | `UDataStream` 基类默认实现 + `LexToString(EDataStreamState)` + 转发到 Manager 的 `RequestClose/GetState` |
| `Private/Iris/DataStream/DataStreamDefinitions.h` | `FDataStreamDefinition`（Name / ClassName / Class / DefaultSendStatus / bAutoCreate / bDynamicCreate / StreamIndex）+ `UDataStreamDefinitions`（config=Engine） |
| `Private/Iris/DataStream/DataStreamDefinitions.cpp` | Load `UClass`、校验重复、分配 `StreamIndex`、`FindDefinition`、`GetStreamNamesToAutoCreateOrRegister` |
| `Private/Iris/DataStream/DataStreamManager.cpp` | Pimpl `FImpl`：`Streams` / `StreamSendStatus` / `StreamState` / `RecordStorage`、`TResizableCircularQueue<FRecord*>`、握手/开关状态机（4-bit 状态，最多 32 条流）、序列化 stream 状态位到 bitstream、整合 `FNetExports` 导出上下文 |

---

## 2. 核心类

### 2.1 `UDataStream`（抽象 UCLASS，`MinimalAPI, transient`）

**生命周期**：`Init(FInitParameters) → Deinit() → Update(EUpdateType::PreSendUpdate | PostTickFlush)`。

**写入管线**：

```
BeginWrite(FBeginWriteParameters)     // 提示即将开始一个 packet（获取 export context 等）
  → WriteData(Context, OutRecord) : EWriteResult { NoData / Ok / HasMoreData }
EndWrite()                            // 通知 packet 写完，锁定 record
```

**读取**：`ReadData(Context)`（可能触发对端状态应用）。

**投递反馈**：`ProcessPacketDeliveryStatus(EPacketDeliveryStatus, FDataStreamRecord*)`（配对 `WriteData` 创建的 record）。

**可靠性查询**：`HasAcknowledgedAllReliableData()`。

**状态机**（由 Manager 驱动）：

```
EDataStreamState
  Invalid
  PendingCreate              ← 请求创建（尚未握手确认）
  WaitOnCreateConfirmation
  Open                       ← 可读可写
  PendingClose
  WaitOnCloseConfirmation
```

### 2.2 `FDataStreamRecord`

派生类自定义字段，用于记录"本 packet 我发了什么"以便 ACK/NACK 时：
- Delivered → 释放 record
- Lost → 重发或从累积队列撤销标记
- Discard → 连接断开时释放资源

### 2.3 `UDataStreamManager` extends `UDataStream`

自身也是一条 `UDataStream`（组合模式），对外部传输层（NetDriver/DataStreamChannel）只需调用这一条流。

**内部 `FImpl`**：
- 最多 `MaxStreamCount=32` 条子流（5-bit 索引 + 4-bit 状态）；
- dynamic stream 通过握手开/关；
- 在 packet 头序列化 DataStreamMask + StateMask（让对端知道本包里有哪些子流的数据）；
- 聚合所有子流的 `WriteData`，按 `FRecord`（含子 record 数组 + mask）聚合并入 `TResizableCircularQueue` 以便 ACK 分发；
- 暴露 `FNetExports`（NetToken / 对象引用 export 对象）。

### 2.4 `FDataStreamDefinition` + `UDataStreamDefinitions`（config=Engine）

ini 示例：

```ini
[/Script/IrisCore.DataStreamDefinitions]
+DataStreams=(DataStreamName="Replication",ClassName="/Script/IrisCore.ReplicationDataStream",bAutoCreate=true,bDynamicCreate=false,DefaultSendStatus=Send)
+DataStreams=(DataStreamName="NetToken",ClassName="/Script/IrisCore.NetTokenDataStream",bAutoCreate=true,bDynamicCreate=false)
+DataStreams=(DataStreamName="ChunkedData",ClassName="/Script/IrisCore.ChunkedDataStream",bAutoCreate=false,bDynamicCreate=true)
```

`FixupDefinitions` 在首次访问时加载 UClass 并分配 `StreamIndex`。

---

## 3. 依赖关系

- **Iris 内**：
  - `Iris/Core/IrisLog.h`
  - `Iris/PacketControl/PacketNotification.h`（`EPacketDeliveryStatus`）
  - `Iris/Serialization`（`FNetBitStreamReader/Writer` / `FNetSerializationContext` / `NetBitStreamUtil`）
  - `Iris/ReplicationSystem/NetExports.h`
- **UE 外**：`CoreUObject`（UCLASS/USTRUCT/UENUM）/ `Core`（`TPimplPtr` / `TObjectPtr`） / `NetCore`（`ResizableCircularQueue.h`）。

反向依赖：`ReplicationSystem`（`UReplicationDataStream` / `UChunkedDataStream` / `UNetTokenDataStream` 都是派生 `UDataStream`）。

---

## 4. 对外 API / 扩展点

- UCLASS：`UDataStream` / `UDataStreamManager`。
- UENUM：`EDataStreamSendStatus`（顶层，便于 UObject 使用）。
- `namespace UE::Net::EDataStreamWriteMode`。
- 扩展点：
  1. 继承 `UDataStream`，实现 `Update/BeginWrite/WriteData/EndWrite/ReadData/ProcessPacketDeliveryStatus/HasAcknowledgedAllReliableData`；
  2. 在 `DefaultEngine.ini` `[/Script/IrisCore.DataStreamDefinitions]` 注册；
  3. 运行时通过 `UReplicationSystem::OpenDataStream(ConnId, Name)` / `CloseDataStream(ConnId, Name)` 动态开关（若 `bDynamicCreate=true`）。
- 错误码：`ECreateDataStreamResult`。

---

## 5. 注释推进计划

- **并行度**：1 个 subagent（6 文件，但 `DataStreamManager.cpp` 复杂度高，单独串行处理）。
- **阶段**：**阶段 3**（与 ReplicationState 并行）。
- **重点关注**：
  - `UDataStream` 状态机转移条件与 `EDataStreamSendStatus`（Send / Pause / …）的关系；
  - `UDataStreamManager::FImpl` 中 `bitmask serialization` 的精确位布局（DataStreamMask / StateMask）；
  - `FRecord` 的生命周期与 `TResizableCircularQueue` ACK 分发路径；
  - `UDataStreamDefinitions` 的 config 驱动装载 + StreamIndex 分配顺序；
  - `FNetExports` 在 Manager 级和 per-stream 级的职责划分。

### 注释完成情况

- [x] `DataStream.h` / `DataStream.cpp`
- [x] `DataStreamManager.h` / `DataStreamManager.cpp`
- [x] `DataStreamDefinitions.h` / `DataStreamDefinitions.cpp`
