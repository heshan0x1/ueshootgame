# PacketControl 模块

> 路径：`Engine/Source/Runtime/Net/Iris/Public/Iris/PacketControl/`  
> 分层：**L0 基础契约层**（单头文件 + 单枚举，零依赖）  
> 职责：定义 DataStream ↔ Iris 传输层之间用于"包投递状态"通知的统一枚举 `EPacketDeliveryStatus`。

---

## 1. 文件清单

| 文件 | 职责 |
|------|------|
| `Public/Iris/PacketControl/PacketNotification.h` | 定义 `enum class EPacketDeliveryStatus : uint8 { Delivered, Lost, Discard }` |

无 Private 实现。

---

## 2. 核心接口

**唯一用途**：`UDataStream::ProcessPacketDeliveryStatus(EPacketDeliveryStatus, FDataStreamRecord*)` / `UDataStreamManager::ProcessPacketDeliveryStatus(...)` 的参数。

### 2.1 `UE::Net::EPacketDeliveryStatus`

| 值 | 语义 |
|----|------|
| `Delivered` | 对端已确认送达，DataStream 可释放对应 Record |
| `Lost` | 被标记为丢失（乱序丢弃 / 超时未 ACK），通常要求 DataStream 回滚/重传 **但要保留 Record** 供重发 |
| `Discard` | 连接被关闭或资源回收时释放相关 `FDataStreamRecord`——**不是"失败"的结论，是"放弃追踪"的指令** |

> ⚠ 开发者常见误解：`Lost` 与 `Discard` 语义差别很大。`Lost` 意味着"数据丢了请处理"；`Discard` 意味着"这条记录不再需要追踪"（连接关闭、资源回收）。DataStream 实现方应明确区分。

### 注释完成情况

- [x] `PacketNotification.h`（已完成，并补充 Lost vs Discard 语义区分的警告）

**调用路径**（从 NetConnection 层向下传递 ACK/Lost）：

```
NetConnection ACK → UNetDriver/DataStreamChannel
 → UDataStreamManager.ProcessPacketDeliveryStatus(status, record)
   → 内部按 bitmask 分发到每条 UDataStream
     → UReplicationDataStream.ProcessPacketDeliveryStatus → FReplicationWriter.ProcessPacketDeliveryStatus
     → 其它 Stream（如 ChunkedDataStream）也会对应处理
```

---

## 3. 依赖关系

- **Iris 内**：无。
- **UE 外**：仅 `CoreTypes.h`。

反向依赖：`DataStream`（`UDataStream::ProcessPacketDeliveryStatus`）、`ReplicationSystem`（`FReplicationWriter::ProcessPacketDeliveryStatus`）。

---

## 4. 对外 API / 扩展点

- 无扩展点；为纯契约枚举。
- 若需要扩展新的投递状态，需要同步修改所有 `ProcessPacketDeliveryStatus` 的实现（ReplicationWriter、ChunkedDataStream、NetRPCHandler 等）。

---

## 5. 注释推进计划

- **并行度**：微型模块，并入阶段 1 的 Core/Metrics agent 附带处理即可。
- **阶段**：**阶段 1**。
- **重点关注**：枚举注释中说明三种语义差异，以及典型触发时机。

### 注释完成情况（汇总）

- [x] `PacketNotification.h`（已完成；详见 §5）
