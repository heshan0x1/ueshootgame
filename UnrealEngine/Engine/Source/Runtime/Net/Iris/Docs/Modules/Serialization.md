# Serialization 模块

> 路径：`Engine/Source/Runtime/Net/Iris/{Public,Private}/Iris/Serialization/`  
> 分层：**L2 底层数据层**（Iris 最核心的比特流/序列化子系统，上承 ReplicationState/ReplicationSystem，下接 UE Core/NetCore）  
> 职责：提供位流读写原语、`FNetSerializer` 函数表契约与 SFINAE 模板构造器、上下文/错误/日志、引用收集、`UPackageMap` 桥接、具体类型 Serializer。

---

## 1. 文件清单（按作用分类）

### 1.1 位流原语（BitStream）

| 路径 | 职责 |
|------|------|
| `Public/Iris/Serialization/NetBitStreamReader.h` | `FNetBitStreamReader`：按位读；substream / overflow / seek |
| `Public/Iris/Serialization/NetBitStreamWriter.h` | `FNetBitStreamWriter`：按位写；substream / commit / overflow / seek |
| `Public/Iris/Serialization/NetBitStreamUtil.h` | 高层读写工具：`Write/ReadUint32/64` / `WritePackedUint32/Int32` / `Write/ReadString` / `Write/ReadVector/Rotator` / `Write/ReadSparseBitArray[Delta]` / `Write/ReadSentinelBits` / `Write/ReadBytes`；RAII：`FNetBitStreamWriteScope` / `FNetBitStreamRollbackScope` |
| `Public/Iris/Serialization/BitPacking.h` | 整数差分 + 单位浮点的量化 & 序列化模板：`SerializeIntDelta` / `SerializeUintDelta` / `QuantizeSignedUnitFloat` / `SerializeSameValue` 等 |
| `Private/Iris/Serialization/NetBitStreamUtils.h` | 内部 `UE::Net::BitStreamUtils::GetBits()` |
| `Private/Iris/Serialization/NetBitStreamReader.cpp` / `NetBitStreamWriter.cpp` / `NetBitStreamUtil.cpp` / `BitPacking.cpp` | 对应实现 |

### 1.2 通用 Serializer 契约

| 路径 | 职责 |
|------|------|
| `Public/Iris/Serialization/NetSerializer.h` | **模块核心**：`FNetSerializer` 结构体（函数指针表），所有 `FNet*Args`（Serialize/Deserialize/SerializeDelta/DeserializeDelta/Quantize/Dequantize/IsEqual/Validate/Clone/Free/CollectReferences/Apply）及函数指针类型；`ENetSerializerTraits`；`UE_NET_DECLARE/IMPLEMENT_SERIALIZER` 宏；`TNetSerializer<Impl>` 模板 |
| `Public/Iris/Serialization/NetSerializerConfig.h` | `FNetSerializerConfig`（USTRUCT，所有 Config 的基类）+ `ENetSerializerConfigTraits` |
| `Public/Iris/Serialization/NetSerializerBuilder.inl` | `TNetSerializerBuilder<>`：SFINAE 元编程构建器——探测实现者提供了哪些成员（Serialize/Delta/Quantize/Apply…），填充 `FNetSerializer` 函数指针；并提供 `NetSerializeDeltaDefault` / `NetQuantizeDefault` / `NetIsEqualDefault` 等默认实现 |
| `Public/Iris/Serialization/NetSerializerDelegates.h` | `FNetSerializerRegistryDelegates`：**扩展点基类**（继承以在 PreFreeze/PostFreeze 注册 Serializer） |
| `Public/Iris/Serialization/NetSerializerArrayStorage.h` | `FNetSerializerArrayStorage<T, Policy>` / `FNetSerializerAlignedStorage` / `FElementAllocationPolicy` / `TInlinedElementAllocationPolicy<N>`：容器型 Serializer 的量化状态动态存储 |
| `Private/Iris/Serialization/InternalNetSerializer.h` | `TInternalNetSerializer<>` + `UE_NET_DECLARE/IMPLEMENT_SERIALIZER_INTERNAL`（允许设置内部专用 trait） |
| `Private/Iris/Serialization/InternalNetSerializerBuilder.h` | `TInternalNetSerializerBuilder<>`：内部 Trait 的 SFINAE 探测 |
| `Private/Iris/Serialization/InternalNetSerializerDelegates.h/.cpp` | `FInternalNetSerializerDelegates`：PreFreeze / PostFreeze / LoadedModulesUpdated 全局多播委托持有者 |

### 1.3 上下文 / 错误 / 日志

| 路径 | 职责 |
|------|------|
| `Public/Iris/Serialization/NetSerializationContext.h` | `FNetSerializationContext`：所有 Serializer 函数的第一个参数；聚合 BitStreamReader/Writer、ErrorContext、NetBlobReceiver、LocalConnectionId、TraceCollector、InternalContext、ExportContext、NetStatsContext、ChangeMask、ReadJournal、PacketId、初态标志 |
| `Public/Iris/Serialization/NetErrorContext.h` | `FNetErrorContext` + 一组预定义 `GNetError_*` FName |
| `Public/Iris/Serialization/NetJournal.h` | `FNetJournal`：环形缓冲（32 项）跟踪最后几条读取记录 + `UE_ADD_READ_JOURNAL_ENTRY` 宏 |
| `Private/Iris/Serialization/InternalNetSerializationContext.h` | `FInternalNetSerializationContext`：持有 `UReplicationSystem*` / `FObjectReferenceCache*` / `UIrisObjectReferencePackageMap*` / `Alloc/Free/Realloc` 分配器；`bInlineObjectReferenceExports` / `bDowngradeAutonomousProxyRole` / `bSerializeObjectReferencesAsRemoteIds` 标志；`FForceInlineExportScope` RAII |
| `Private/Iris/Serialization/NetExportContext.h` | `FNetExportContext`：跟踪 batch 内已导出/待导出 `FNetRefHandle` / `FNetToken` / `FNetObjectReference`；`FNetExportRollbackScope` 错误回滚 |
| 对应 `.cpp` | 实现 |

### 1.4 引用收集 / PackageMap 桥接

| 路径 | 职责 |
|------|------|
| `Public/Iris/Serialization/NetReferenceCollector.h` | `FNetReferenceCollector` / `FReferenceInfo` / `ENetReferenceCollectorTraits` |
| `Private/Iris/Serialization/QuantizedObjectReference.h` | `FQuantizedObjectReference`（本地 `FNetObjectReference` 与远程两态） |
| `Private/Iris/Serialization/QuantizedRemoteObjectReference.h` | `FQuantizedRemoteObjectReference`（UE_WITH_REMOTE_OBJECT_HANDLE） |
| `Public/Iris/Serialization/IrisObjectReferencePackageMap.h` | `UIrisObjectReferencePackageMap`（继承 `UPackageMap`）+ `FIrisPackageMapExports` + `FIrisObjectReferencePackageMapReadScope/WriteScope`——调用旧 `FArchive::SerializeObject/Name/NetToken` 时把引用捕获为 index 交给 Iris |
| `Public/Iris/Serialization/IrisPackageMapExportUtil.h` | `FIrisPackageMapExportsUtil` / `FIrisPackageMapExportsQuantizedType`：把捕获结果做成标准 NetSerializer 量化状态 |
| 对应 `.cpp` | 实现 |

### 1.5 具体类型 Serializer

#### 基础标量

| 类型 | 文件 |
|---|---|
| Int / Uint / Int范围 / Uint范围 | `IntNetSerializers.h/.cpp` / `UintNetSerializers.h/.cpp` / `IntRangeNetSerializers.h/.cpp` / `UintRangeNetSerializers.h/.cpp` + Private 辅助 |
| 打包整数 | `PackedIntNetSerializers.h/.cpp` |
| 枚举 | `EnumNetSerializers.h/.cpp` / `Private/InternalEnumNetSerializers.h/.cpp` |
| 浮点 | `FloatNetSerializers.h/.cpp` |
| Bool / Bitfield / Nop / NetRole | `Private/BoolNetSerializer.cpp` / `Private/BitfieldNetSerializer.cpp` / `Private/NopNetSerializer.cpp` / `Private/NetRoleNetSerializer.cpp` |
| DateTime / Guid | `DateTimeNetSerializer.h/.cpp` / `GuidNetSerializer.h/.cpp` |
| 字符串 / Name / NameAsToken | `StringNetSerializers.h/.cpp` / `Private/StringNetSerializerUtils.h/.cpp` |

#### 数学

| 类型 | 文件 |
|---|---|
| Vector / Vector3f / Vector3d / PackedVector{,10,100,Normal} | `VectorNetSerializers.h/.cpp` / `PackedVectorNetSerializers.h/.cpp` |
| Rotator{,AsByte,AsShort,3f,3d} | `RotatorNetSerializers.h/.cpp` |
| UnitQuat{,4f,4d} | `QuatNetSerializers.h/.cpp` |

#### 对象引用 / 软引用 / 远程引用

| 类型 | 文件 |
|---|---|
| UObject* / TObjectPtr / TWeakObjectPtr / TScriptInterface | `ObjectNetSerializer.h` / `Private/ObjectNetSerializer.cpp` |
| SoftObject / SoftObjectPath / SoftClassPath | `SoftObjectNetSerializers.h` / `Private/SoftObjectNetSerializers.cpp` |
| RemoteObjectId / RemoteServerId / RemoteObjectReference | `Private/RemoteObjectIdNetSerializer.{h,cpp}` / `RemoteServerIdNetSerializer.{h,cpp}` / `RemoteObjectReferenceNetSerializer.{h,cpp}` |

#### 复合 / 容器 / 兜底

| 类型 | 文件 |
|---|---|
| Struct（容器）+ Util | `Private/StructNetSerializer.cpp` / `StructNetSerializerUtil.h/.cpp` |
| TArray 属性 | `Private/ArrayPropertyNetSerializer.cpp` |
| FieldPath | `Private/FieldPathNetSerializer.cpp` |
| LastResortProperty（兜底） | `Private/LastResortPropertyNetSerializer.cpp` |
| InstancedStruct | `InstancedStructNetSerializer.h` / `Private/InstancedStructNetSerializer.cpp` |
| 多态 struct / array | `PolymorphicNetSerializer.h` / `PolymorphicNetSerializerImpl.h` / `Private/PolymorphicNetSerializer.cpp` |
| 汇聚头 | `NetSerializers.h`（重新导出常用 Serializer 声明 + Bool/Struct/Nop） |
| Internal Config/Serializer | `Private/InternalNetSerializers.h/.cpp`（5 个内部 Config：Bitfield/Array/LastResort/NetRole/FieldPath） |
| Internal 判别 utility | `Private/InternalNetSerializerUtils.h/.cpp`（**仅 3 个判别函数**：`IsStructNetSerializer`/`IsArrayPropertyNetSerializer`/`IsObjectReferenceNetSerializer`，对单例指针做相等比较；不是注册入口） |

---

## 2. 关键依赖（文字版）

```
FNetSerializer（函数指针表）
  └─ TNetSerializer<Impl>::ConstructNetSerializer
        └─ TNetSerializerBuilder<Impl>（NetSerializerBuilder.inl）SFINAE

所有具体 Serializer 实现 Impl
  ├─ Config : FNetSerializerConfig
  ├─ Serialize/Deserialize(FNetSerializationContext&)
  ├─ Quantize/Dequantize（SourceType ↔ QuantizedType）
  └─ UE_NET_DECLARE/IMPLEMENT_SERIALIZER → XxxNetSerializerInfo

FNetSerializationContext（跨函数传递"世界")
  ├─ FNetBitStreamReader* / Writer*
  ├─ FNetErrorContext
  ├─ FNetJournal
  ├─ FNetTraceCollector*
  ├─ INetBlobReceiver*
  ├─ FNetBitArrayView*（ChangeMask）
  ├─ FInternalNetSerializationContext*
  │     └─ UReplicationSystem* / FObjectReferenceCache* / UIrisObjectReferencePackageMap* + 分配器
  ├─ FNetExportContext*
  └─ FNetStatsContext*

引用流：
  Serialize 时收集 → FNetReferenceCollector → FQuantizedObjectReference
  导出流：FNetExportContext（AckedExports / BatchExports）→ 下一包携带 → 错误时回滚

PackageMap 桥：
  旧式 NetSerialize ↔ UIrisObjectReferencePackageMap（读写 Scope）↔ FIrisPackageMapExportsUtil
```

---

## 3. 对外 API / 扩展点

### 3.1 核心类（IRISCORE_API）

见第 1 节。所有预声明的 `FXxxNetSerializer`（通过 `UE_NET_DECLARE_SERIALIZER`）。

### 3.2 宏

- 用户级：`UE_NET_DECLARE_SERIALIZER(Name, Api)` / `UE_NET_IMPLEMENT_SERIALIZER(Name)` / `UE_NET_GET_SERIALIZER(Name)` / `UE_NET_GET_SERIALIZER_INTERNAL_TYPE_SIZE/ALIGNMENT/DEFAULT_CONFIG(Name)`
- 内部：`UE_NET_DECLARE_SERIALIZER_INTERNAL` / `UE_NET_IMPLEMENT_SERIALIZER_INTERNAL`
- 日志：`UE_ADD_READ_JOURNAL_ENTRY` / `UE_RESET_READ_JOURNAL`

### 3.3 扩展点

1. **新类型 NetSerializer**：Impl struct（`SourceType/QuantizedType/ConfigType` + 可选成员 + `constexpr bool` Trait）+ `UE_NET_DECLARE_SERIALIZER` + `UE_NET_IMPLEMENT_SERIALIZER`；可缺失成员由 default 实现填充。

   **可选 trait 完整列表**（在 Impl struct 中以 `static constexpr bool ...` 形式声明）：
   - `bIsForwardingSerializer`（默认 false；若 true 表示该 Serializer 全程转发到内部 Descriptor，必须实现 12 项契约全部）
   - `bHasDynamicState`（默认 false；标记需要 Clone/Free 动态分配的量化状态）
   - `bHasConnectionSpecificSerialization`（默认 false；序列化结果依赖具体连接）
   - `bHasCustomNetReference`（默认 false；提供自定义 `CollectNetReferences`）
   - **`bUseDefaultDelta`（默认 *true*！）** ⚠ —— 标量与不需要差分的 Serializer 应显式置 false 以禁用默认 Delta 实现
   - `bUseSerializerIsEqual`（默认 false；用 Serializer 自定义的 `IsEqual` 而不是默认 memcmp）

   缺失成员函数会由 `NetSerializeDeltaDefault / NetQuantizeDefault / NetIsEqualDefault / NetValidateDefault` 等默认实现填充（详见 `NetSerializerBuilder.inl`）。

2. **生命周期**：`FNetSerializerRegistryDelegates`（继承并重写 `OnPreFreezeNetSerializerRegistry` / `OnPostFreezeNetSerializerRegistry` / `OnLoadedModulesUpdated`）。
3. **多态 struct**：`TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>` + `FPolymorphicStructNetSerializerConfig::RegisteredTypes` 运行时 `InitForType(UScriptStruct*)`。
   - **5-bit TypeIndex**（`RegisteredTypeBits = 5`）→ 编码空间 32 个值，0 保留为 `InvalidTypeIndex`，**实际可注册派生类型 = 31 个**；超过会触发 `check` 崩溃。
   - 排序规则：按 `ScriptStruct->GetName().ToLower()` **降序**稳定排序，保证两端 UClass 集合相同时 TypeIndex 必相同。
4. **InstancedStruct 白名单**：`FInstancedStructNetSerializerConfig::SupportedTypes`。
   - ⚠ **当前未硬启用**：`InitInstancedStructNetSerializerConfig` 中 `bIsAllowingArbitraryStruct = true` 强制接受任意类型（参见 UE-180981 待办）。SupportedTypes 字段已留位但目前不参与过滤。
5. **旧式 NetSerialize 桥接**：`UIrisObjectReferencePackageMap` + `FIrisPackageMapExportsUtil`；最差兜底 `FLastResortPropertyNetSerializer`。
   - **LastResort 触发条件**：`FNetSerializerSelector` 按下面优先级匹配 `FProperty`，**全部未命中**才降级至 LastResort——
     1. 在 `FPropertyNetSerializerInfoRegistry` 找不到原生 NetSerializer；
     2. struct 没有 `UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO` / `..._LASTRESORT_...` 声明；
     3. 反射不到对应 named struct serializer。
   - 不支持 `SerializeDelta`（无法对未知字节流做语义 diff）。
6. **动态量化存储**：`FNetSerializerArrayStorage<T>` + `static constexpr bool bHasDynamicState = true` + 转发 Clone/Free。
   - ⚠ `Clone` 中必须先 `AllocatorInstance.Initialize()` 把"被 memcpy 过来的悬挂指针"清零，否则 `ResizeAllocation` 会把别人的内存当自己的旧分配释放。
7. **自定义错误**：`extern const FName GNetError_Foo` + `Context.SetError(GNetError_Foo)`。

### 3.4 预定义 `GNetError_*` 名单（来自 `NetErrorContext.cpp`）

| 常量 | FName 字面量 | 触发场景 |
|------|--------------|----------|
| `GNetError_BitStreamOverflow` | `"BitStream overflow"` | 读/写位流越界（最常见） |
| `GNetError_BitStreamError` | `"BitStream error"` | 通用位流异常 |
| `GNetError_ArraySizeTooLarge` | `"Array size is too large"` | 反序列化数组长度超阈值（如 `MaxExports=65536`） |
| `GNetError_InvalidNetHandle` | `"Invalid NetHandle"` | NetHandle 在本端无法解析（未导出/已销毁） |
| `GNetError_BrokenNetHandle` | `"Broken NetHandle"` | NetHandle 内部一致性损坏 |
| `GNetError_InvalidValue` | `"Invalid value"` | 解出的值越界（非法枚举/异常浮点等） |
| `GNetError_InternalError` | `"Internal error"` | 内部 bug（非网络数据问题） |
| `GNetError_InvalidDataStream` | `"Invalid DataStream"` | 非法 DataStream（未注册 ID / Chunk 类型不在白名单） |
| `GNetError_CorruptString` | `"Corrupt string"` | 字符串编码损坏（在 `StringNetSerializerUtils.cpp` 定义） |

---

## 4. 依赖

### 4.1 对 Iris 其他模块

- `Iris/Core/`：`NetObjectReference.h` / `BitTwiddling.h` / `IrisLog.h` / `IrisConfig.h` / `IrisConstants.h`。
- `Iris/ReplicationSystem/`：`NetRefHandle.h` / `ObjectReferenceCacheFwd.h` / `ObjectReferenceCache.h` / `ReplicationSystem.h` / `ReplicationSystemInternal.h` / `NetRefHandleManager.h` / `NameTokenStore.h` / `StructNetTokenDataStore.h`。
- `Iris/ReplicationState/`：`ReplicationStateDescriptor.h`（`EReplicationStateTraits` / `FNetReferenceInfo`）。

### 4.2 对外部模块

- `Core` / `CoreUObject`（`UPackageMap` / `UScriptStruct` / `FProperty`） / `NetCore`（`NetToken.h`、`NetTrace.h`、`NetDebugName.h`） / `TraceLog`。

---

## 5. 注释推进计划

- **文件数**：68 cpp + 35 h = 103 个。
- **并行度**：**阶段 2 并行 4 个 subagent**，按子类分配：
  - agent-A：BitStream 原语 + 契约 + 上下文 + 引用 + PackageMap（约 20-25 文件）
  - agent-B：基础标量 Serializer（Int/Uint/Range/Packed/Enum/Float/Bool/Bitfield/Nop/NetRole/DateTime/Guid，约 20 文件）
  - agent-C：字符串 + 数学 + 对象引用 + 远程引用 Serializer（约 25 文件）
  - agent-D：复合/容器/兜底 Serializer（Struct/Array/FieldPath/LastResort/InstancedStruct/Polymorphic/InternalNet* 汇聚，约 25-30 文件）
- **重点关注**：
  - `NetSerializer.h` 的函数表字段（这是整个 Iris 序列化的"接口契约"）；
  - `NetSerializerBuilder.inl` 的 SFINAE 技巧，需要把每个 `detail::Has*` 的含义与对应 default 实现解释清楚；
  - `NetSerializationContext.h` 字段的生命周期；
  - 各具体 Serializer 的量化算法（特别是 PackedVector、Quat、Range 系列）；
  - PackageMap 桥接逻辑；
  - Polymorphic 类型表的 5-bit 编码上限。

### 注释完成情况

- [x] BitStream 原语与契约（Reader/Writer/Util/BitPacking/NetSerializer/Builder/Config/Delegates/ArrayStorage/InternalNet*）— 共 21 文件
- [x] 上下文/错误/日志/引用/PackageMap — 共 16 文件
- [x] 基础标量 Serializer（Int/Uint/Range/Packed/Enum/Float/Bool/Bitfield/Nop/NetRole/DateTime/Guid）— 共 17 文件 + 8 文件（Float/Date/Guid 在 Float-Math agent）= 25 文件
- [x] 字符串/数学/对象引用/远程引用 Serializer — 共 14 + 12 = 26 文件
- [x] 复合/容器/兜底 Serializer（Struct/Array/FieldPath/LastResort/InstancedStruct/Polymorphic）— 共 15 文件

**全部 104 个 Serialization 源文件已添加中文全量注释。**

### 已根据注释发现修正的文档差异

1. `bUseDefaultDelta` 默认 **true**（不是 false）；新增 `bUseSerializerIsEqual` trait 列表项；
2. Polymorphic 实际上限 31 个（5-bit，0 保留为 `InvalidTypeIndex`）；
3. InstancedStruct `SupportedTypes` 白名单**目前未启用**（UE-180981 待办，`bIsAllowingArbitraryStruct=true`）；
4. `LastResortPropertyNetSerializer` 触发条件清单（3 级匹配失败才回落）；
5. `InternalNetSerializerUtils` 是判别函数集合，不是注册入口；
6. 增补 `GNetError_*` 完整名单（含 `GNetError_CorruptString`）；
7. 命名修正：`Rotator AsByte` 是 360°/256（**度**）非 2π/256（弧度）；`PackedVector10/100` 实际 ScaleBitCount=3/7（×8/×128，2 的幂）；`UnitQuat` **永远省略 W**（不是经典 smallest-three）；
8. `WriteConditionallyQuantizedVector` 在代码中不存在，对应 `FPackedVectorNetSerializerBase::Quantize` 内部分支。
