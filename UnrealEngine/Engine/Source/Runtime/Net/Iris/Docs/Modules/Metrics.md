# Metrics 模块

> 路径：`Engine/Source/Runtime/Net/Iris/Public/Iris/Metrics/`  
> 分层：**L0 基础数据容器层**（单头文件，仅依赖 `Core`）  
> 职责：提供一个极简的、按 `FName` 键收集整型/浮点度量值的容器，便于 Iris 子系统把运行时指标统一汇总给上层（分析/遥测）。

---

## 1. 文件清单

| 文件 | 职责 |
|------|------|
| `Public/Iris/Metrics/NetMetrics.h` | 定义 `FNetMetric`（`Unsigned/Signed/Double` 三态 union）、`FNetMetrics`（`TMap<FName, FNetMetric>` 收集器） |

无 Private 实现。

---

## 2. 核心类

### 2.1 `UE::Net::FNetMetric`

存储单个指标的类型安全 union：

```cpp
enum class EDataType : uint8 { None, Unsigned, Signed, Double };
```

- `None` 为默认构造后的空状态；
- 构造器模板借 `if constexpr` 按输入类型分发到 `uint32 / int32 / double`；
- `GetSigned/GetUnsigned/GetDouble` 带 `check(DataType==...)` 断言；
- `static_assert` 限制只接受整型或浮点。

> **⚠ 注意（实现约束）**：`FNetMetric` 内部存储位宽固定为 32 位（`uint32 / int32`）+ `double`；传入 `int64/uint64` 会被**静默截断到 32 位**。累计字节数/包数等大数值统计建议用 `double`，或在上层做溢出保护。

### 2.2 `UE::Net::FNetMetrics`

```cpp
void EmplaceMetric(FName Key, FNetMetric Value);
void AddMetric(FName Key, FNetMetric Value);
const TMap<FName, FNetMetric>& GetMetrics() const;
```

> **⚠ 注意（实现约束）**：当前代码中 `EmplaceMetric` 和 `AddMetric` 的实际行为**都是"同键覆盖写"**（均为 `TMap::Emplace/Add` 的薄包装，未实现累加），要真正做累加需在调用方显式读取旧值后再写入。

典型使用方：`UReplicationSystem::CollectNetMetrics(FNetMetrics&)` 从内部系统（NetRefHandleManager / Filtering / Prioritization / DeltaCompression / NetBlob / Stats）收集关键指标后一次性返回给 NetDriver 或分析 Hook。

---

## 3. 依赖关系

- **Iris 内**：无。
- **UE 外**：`Core`（`HAL/Platform` / `Containers/Map` / `UObject/NameTypes` / `Misc/AssertionMacros`） + C++ 标准 `<type_traits>`。

反向依赖：`ReplicationSystem`（`UReplicationSystem::CollectNetMetrics` / `ResetNetMetrics`）。

---

## 4. 对外 API / 扩展点

- 类：`UE::Net::FNetMetric` / `UE::Net::FNetMetrics`。
- 无宏、无 CVar、无扩展注册表——上层自行 `EmplaceMetric(TEXT("Xxx"), value)`。

---

## 5. 注释推进计划

- **并行度**：微型模块，并入阶段 1 的 Core/Metrics agent 附带处理。
- **阶段**：**阶段 1**。
- **重点关注**：`FNetMetric` 的 `if constexpr` 分发、union 字段生命周期、类型安全检查逻辑。

### 注释完成情况

- [x] `NetMetrics.h`（已完成，并修正文档：`EDataType` 4 值含 `None`、`AddMetric/EmplaceMetric` 实际都是覆盖写、int64→int32 静默截断风险）
