# Iris 顶层聚合：IrisCoreModule + IrisConfig

> 路径：`Engine/Source/Runtime/Net/Iris/Private/Iris/` 顶层  
> 分层：**L5 顶层**（聚合所有 Iris 模块，接入 UE 模块系统）  
> 职责：`FIrisCoreModule` 负责 UE 模块生命周期（`StartupModule` / `ShutdownModule`）、Serializer Registry 冻结广播、LegacyPushModel 初始化、多 ReplicationSystem 生命周期监听等；`IrisConfig` 提供全局开关与命令行解析。

---

## 1. 文件清单

| 文件 | 职责 |
|------|------|
| `Private/Iris/IrisCoreModule.cpp` | `FIrisCoreModule : IModuleInterface`：`StartupModule` / `ShutdownModule` / `OnAllModuleLoadingPhasesComplete` / `OnModulesChanged` / `BroadcastLoadedModulesUpdated` / `ForceBroadcastLoadedModulesUpdated` / `OnRepSystemCreated/Destroyed`；`IMPLEMENT_MODULE(FIrisCoreModule, IrisCore)` |
| `Public/Iris/IrisConfig.h` | `EReplicationSystem { Default, Legacy, Iris }`；对外 `GetUseIrisReplicationCmdlineValue()` / `SetUseIrisReplication(bool)` |
| `Private/Iris/IrisConfig.cpp` | 上述实现；命令行 `-UseIrisReplication` / `-LegacyReplication` 解析；CVar `net.Iris.UseIrisReplication` |
| `Public/Iris/IrisConstants.h` | 公共常量：`InvalidConnectionId` 等 |
| `Private/Iris/IrisConfigInternal.h` | 内部开关：`UE_NETBITSTREAMWRITER_VALIDATE` / ensure 开关等 |

---

## 2. `FIrisCoreModule::StartupModule()` 流程

```
1. FModuleManager::LoadModuleChecked("NetCore")                              // Iris 强依赖 NetCore
2. FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, OnAllModuleLoadingPhasesComplete)
3. 读命令行并根据 EReplicationSystem 覆盖 net.Iris.UseIrisReplication
4. RegisterPropertyNetSerializerSelectorTypes():
   - FPropertyNetSerializerInfoRegistry::Reset()
   - FInternalNetSerializerDelegates::BroadcastPreFreezeNetSerializerRegistry()   // 插件/外部模块在此注册 NetSerializer
   - RegisterDefaultPropertyNetSerializerInfos()                                  // 引擎默认 Property → NetSerializer 映射
   - FPropertyNetSerializerInfoRegistry::Freeze()                                 // 排序 + 禁止再注册
   - FInternalNetSerializerDelegates::BroadcastPostFreezeNetSerializerRegistry()  // 获取依赖 struct 描述符、完成二次绑定
5. UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL()                                      // 桥接旧 PushModel 宏 → FDirtyNetObjectTracker
6. 订阅 FModuleManager::OnModulesChanged → OnModulesChanged
7. 订阅 FReplicationSystemFactory::GetReplicationSystem{Created,Destroyed}Delegate
8. 枚举已存在的 RS，初始化 RepSystemCount
9. UE_NET_TRACE_ENABLED: 订阅 FNetTrace::OnResetPersistentNetDebugNames →
     UE::Net::ResetLifetimeConditionDebugNames()
```

## 3. `OnModulesChanged(ModuleName, Reason)` 热加载处理

当有模块加载/卸载时：
- `ModuleLoaded`：
  - `++LoadedModulesCount`
  - 若已启用 `bAllowLoadedModulesUpdatedCallback` 且有活跃 RS，注册一个 `FTSTicker` 等到模块加载完成后广播 `FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated()`（避免逐个模块连续广播）
  - 仍触发一次 `PreFreeze/PostFreeze` 广播，刷新注册表（允许新加载模块在运行期注册 Serializer）

## 4. `OnRepSystemCreated/Destroyed`

- Created：若已有 loaded modules 待广播则 `ForceBroadcastLoadedModulesUpdated()`；`++RepSystemCount`。
- Destroyed：`--RepSystemCount`。

## 5. `IrisConfig`

- **`EReplicationSystem`**：Default（不覆盖）/ Legacy（强制老复制） / Iris（强制 Iris）。
- `GetUseIrisReplicationCmdlineValue()`：解析 `-UseIrisReplication` / `-LegacyReplication`。
- `SetUseIrisReplication(bool)`：改 CVar `net.Iris.UseIrisReplication`。
- `UNetDriver::IsUsingIrisReplication()` 读取此 CVar 决定是否启用 Iris。

---

## 6. 注释推进计划

- **并行度**：1 个 subagent（4 文件，小）。
- **阶段**：**阶段 5**（最后一阶段，需所有其他模块完成）。
- **重点关注**：
  - `StartupModule` 顺序的严格性（为何先 LoadModuleChecked("NetCore")、为何 PreFreeze→Register→Freeze→PostFreeze）；
  - 模块热加载的 Ticker 合并策略；
  - Serializer 注册表的"Freeze 后不可改"约束；
  - 多 RS 计数与广播时机协调（RepSystemCount vs LoadedModulesCount vs BroadcastModulesUpdatedHandle）。

### 注释完成情况

- [x] `IrisCoreModule.cpp`
- [x] `IrisConfig.h` / `IrisConfig.cpp`
- [x] `IrisConstants.h` / `IrisConfigInternal.h`
