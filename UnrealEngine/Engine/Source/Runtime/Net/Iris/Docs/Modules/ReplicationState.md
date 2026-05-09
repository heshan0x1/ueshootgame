# ReplicationState 模块

> 路径：`Engine/Source/Runtime/Net/Iris/{Public,Private}/Iris/ReplicationState/`  
> 分层：**L3 数据模型 / 中间件层**（反射→Descriptor 烘焙 + 运行时 state 操作）  
> 职责：把一个 UClass/UStruct/UFunction 通过反射一次性烘成紧凑的 `FReplicationStateDescriptor`，并在运行时用 `FPropertyReplicationState` 操作"外部状态缓冲"（poll/push/CallRepNotify/dirty 跟踪），兼容 FastArray 与 Native Iris 两条路径。

---

## 1. 文件清单

### 1.1 Public

| 文件 | 职责 |
|------|------|
| `ReplicationStateDescriptor.h` | 核心数据结构 `FReplicationStateDescriptor`（含 `FReplicationStateMember*Descriptor` 家族、`EReplicationStateTraits` / `EReplicationStateMemberTraits`、`FReplicationStateIdentifier`、`FNetReferenceInfo`、Construct/Destruct 函数指针、`CreateAndRegisterReplicationFragmentFunc`、`AddRef/Release`、`DescribeReplicationDescriptor`） |
| `ReplicationStateDescriptorBuilder.h` | `FReplicationStateDescriptorBuilder::CreateDescriptorsForClass / CreateDescriptorForStruct / CreateDescriptorForFunction` + `FParameters`（支持 FastArray 特化、单属性裁切、可选 `DescriptorRegistry` 去重复用） |
| `ReplicationStateDescriptorConfig.h` | `UReplicationStateDescriptorConfig`（config=Engine）：`SupportsStructNetSerializerList`、`EnsureFullyPushModelClassNames`、`bEnsureAllClassesAreFullyPushModel` |
| `ReplicationStateFwd.h` | `FReplicationStateHeader`（30-bit NetHandleId + 2-bit dirty flags）+ `Private::FReplicationStateHeaderAccessor` |
| `ReplicationStateUtil.h` | 在 StateBuffer 中定位 ChangeMask / ConditionalChangeMask / PollMask 的 `FNetBitArrayView` 访问器、`MarkDirty(header, changemask, bits)`、`MarkNetObjectStateHeaderDirty` |
| `ReplicationStateDescriptorMacros.h` | 手写伪代码生成辅助宏 `IRIS_DECLARE_COMMON` / `IRIS_ACCESS_BY_VALUE` / `IRIS_ACCESS_BY_REFERENCE` |
| `ReplicationStateDescriptorImplementationMacros.h` | `IRIS_BEGIN/END_*_DESCRIPTOR` + `IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR[_WITH_TRAITS]` |
| `PropertyReplicationState.h` | `FPropertyReplicationState`：基于 Descriptor 构造 StateBuffer；`PollPropertyReplicationState` / `PollPushBasedPropertyReplicationState` / `PollObjectReferences` / `StoreCurrentPropertyReplicationStateForRepNotifies` / `PushPropertyReplicationState` / `CopyDirtyProperties` / `CallRepNotifies` / `IsDirty` / `MarkDirty` / `IsCustomConditionEnabled` / `ToString` |
| `PropertyNetSerializerInfoRegistry.h` | `FPropertyNetSerializerInfo`（虚接口）+ `FPropertyNetSerializerInfoRegistry`（静态 Register/Unregister/Freeze/Find）+ `TSimplePropertyNetSerializerInfo` + `FNamedStructPropertyNetSerializerInfo` + `FLastResortPropertyNetSerializerInfo` + `FNamedStructLastResortPropertyNetSerializerInfo` + 大量注册宏 |
| `IrisFastArraySerializer.h` | `FIrisFastArraySerializer`：继承 `FFastArraySerializer`；重写 `MarkItemDirty/MarkArrayDirty` 以同步 `GlobalDirtyNetObjectTracker`；内嵌 `FReplicationStateHeader + ChangeMaskStorage[4]`（63-bit 元素 changemask + 1-bit array + 条件位） |
| `Private/FastArrayReplicationFragmentInternal.h` | `FIrisFastArraySerializerPrivateAccessor`（internal accessor）+ 模板 `TIrisFastArrayEditor<FastArrayType>`：推式 Add/Edit/Remove/RemoveAtSwap/Empty 直接标 changemask 避免 poll |

### 1.2 Private

| 文件 | 职责 |
|------|------|
| `ReplicationStateDescriptor.cpp` | `AddRef/Release` + `DescribeReplicationDescriptor` 诊断输出 |
| `ReplicationStateDescriptorBuilder.cpp`（~4000 行） | 反射 → Descriptor 数组（合并 lifetime conditionals、RepIndex 映射、FastArray 特化、Struct custom NetSerializer 判定、`FMemoryLayoutUtil` 单块连续内存 layout、默认状态缓冲构造 + CityHash 稳定 identifier） |
| `ReplicationStateDescriptorConfig.cpp` | config 访问器 |
| `ReplicationStateDescriptorRegistry.h/.cpp` | `FReplicationStateDescriptorRegistry`：以 `FFieldVariant` 为键缓存 Descriptor 列表，带 `TWeakObjectPtr` PostGC 修剪；与 `FReplicationProtocolManager` 配合 |
| `ReplicationStateStorage.h/.cpp` | `FReplicationStateStorage`：per-object SendState / RecvState / Baseline 缓冲管理（`AllocBaseline` / `ReserveBaseline` / `CommitBaselineReservation` / `CancelBaselineReservation` / `FreeBaseline` / `CloneState` / `CloneDefaultState`）；delta-compression 基础 |
| `PropertyNetSerializerInfoRegistry.cpp` | 注册表实现（sort / binary search / delegate 集成） |
| `PropertyReplicationState.cpp`（~700 行） | 所有 Poll / Push / CallRepNotifies / Compare 的业务逻辑实现 |
| `InternalPropertyReplicationState.h/.cpp` | Construct / Destruct、`CopyPropertyReplicationState` / `CopyDirtyMembers` / `InternalApplyPropertyValue` / `InternalCopyStructProperty` / `InternalCompareStructProperty` / `InternalCompareAndCopyArrayWithElementChangeMask` |
| `InternalReplicationStateDescriptorUtils.h` | `IsUsingStructNetSerializer` / `IsUsingArrayPropertyNetSerializer` 等便捷判断 |
| `DefaultPropertyNetSerializerInfos.h/.cpp` | 为 `bool` / `FIntProperty` / `FFloatProperty` / `FByteProperty` / `FObjectPropertyBase` / `FNameProperty` / `FStrProperty` / `FTextProperty` / `FEnumProperty` / `FArrayProperty` / `FStructProperty` / `FInterfaceProperty` / `FSoftObjectProperty` 等注册默认 `FPropertyNetSerializerInfo` |
| `EnumPropertyNetSerializerInfo.h/.cpp` | 针对 `FEnumProperty`（含底层 byte/int、Native enum）的专用 NetSerializer 选择 |
| `IrisFastArraySerializer.cpp` | `FIrisFastArraySerializer` 的构造/拷贝/移动 + 私有 accessor 实现；调用 `GlobalDirtyNetObjectTracker` 通知 |

---

## 2. 核心类

### 2.1 `FReplicationStateDescriptor`（引用计数、不可变、可跨连接共享）

Iris 最重要的数据结构之一。字段按作用分组：

- **成员数组**：Member Descriptor / Serializer / Traits / Property / ChangeMask / Reference / LifetimeCondition / RepIndex→Member / Function / Tag / Debug
- **大小对齐**：External/Internal Size/Alignment、ChangeMaskBitCount、ChangeMasksExternalOffset、DefaultStateBuffer
- **行为指针**：`ConstructReplicationState` / `DestructReplicationState` / `CreateAndRegisterReplicationFragmentFunction`
- **`EReplicationStateTraits`**：
  - `InitOnly` / `HasLifetimeConditionals` / `HasObjectReference` / `NeedsRefCount` / `HasRepNotifies` / `KeepPreviousState`
  - `HasDynamicState` / `AllMembersAreReplicated`
  - `IsFastArrayReplicationState` / `IsNativeFastArrayReplicationState`
  - `HasConnectionSpecificSerialization` / `HasPushBasedDirtiness` / `HasFullPushBasedDirtiness`
  - `SupportsDeltaCompression` / `UseSerializerIsEqual`
  - `IsDerivedFromStructWithCustomSerializer` / `IsStructWithCustomSerializer`
- **`FReplicationStateIdentifier`**：CityHash64 稳定 ID（基于名字 + 默认状态 hash）

### 2.2 `FReplicationStateDescriptorBuilder`

从反射烘焙 Descriptor 的**唯一入口**。

```cpp
struct FParameters {
    FReplicationStateDescriptorRegistry* DescriptorRegistry = nullptr;
    bool bAllowFastArrayWithExtraReplicatedProperties = false;
    const FProperty* SinglePropertyOverride = nullptr;
    // ...
};

static void CreateDescriptorsForClass(TArray<TRefCountPtr<const FReplicationStateDescriptor>>& OutDescriptors, UClass* Class, const FParameters& Params);
static TRefCountPtr<const FReplicationStateDescriptor> CreateDescriptorForStruct(UStruct* Struct, const FParameters& Params);
static TRefCountPtr<const FReplicationStateDescriptor> CreateDescriptorForFunction(UFunction* Function, const FParameters& Params);
```

支持：FastArray 分支（识别 `FFastArraySerializer` 派生）、单属性裁切、super class 包含、lifetime 条件合并、Struct 自定义 NetSerializer 判定。

### 2.3 `FReplicationStateHeader`

嵌在每个 external state buffer 起点（NativeFastArray 例外，用 `ChangeMasksExternalOffset - sizeof(Header)` 位置）：

```
[30-bit NetHandleId][2-bit dirty flags]
```

与 `FDirtyNetObjectTracker` 联动。

### 2.4 `FPropertyReplicationState`

运行时"带 Descriptor 的状态缓冲壳"：

```cpp
// 构造：按 Descriptor 分配 state buffer（含动态成员）+ 绑定 ExternalInstance
FPropertyReplicationState(const FReplicationStateDescriptor* Descriptor, EStateOwnership Ownership);

// 从 UObject 属性 poll 值
void PollPropertyReplicationState(const void* Instance);
void PollPushBasedPropertyReplicationState(const void* Instance);
void PollObjectReferences(const void* Instance);

// 反向 push 回 UObject
void PushPropertyReplicationState(void* Instance, bool bPushAll=false);

// RepNotify 流程
void StoreCurrentPropertyReplicationStateForRepNotifies();
void CallRepNotifies(UObject* Object, const FPropertyReplicationState* PreviousState, bool bIsInit, bool bOnlyCallIfDiffersFromLocal);

// 其它
void CopyDirtyProperties(const FPropertyReplicationState& Src);
bool IsDirty(uint32 MemberIndex) const;
void MarkDirty(uint32 MemberIndex);
bool IsCustomConditionEnabled(uint32 MemberIndex) const;
FString ToString() const;
```

### 2.5 `FReplicationStateStorage`（内部）

per-object 的 SendState / RecvState / Baseline 缓冲管理，支持 delta-compression（基线增量）。

### 2.6 `FPropertyNetSerializerInfoRegistry`

`FProperty` 类型 / `UStruct` 名 → `FNetSerializer + Config` 的静态注册表。

- `Register(FName TypeName, const FPropertyNetSerializerInfo* Info)`
- `Unregister(FName TypeName)`
- `Freeze()`（排序提升查找）
- `FindSerializerInfo(FProperty*)` / `FindStructSerializerInfo(UScriptStruct*)`
- 支持 `LastResort`（未知 struct fallback 到通用字节流 NetSerializer）+ struct "custom fragment" 重定向

### 2.7 `FIrisFastArraySerializer` / `TIrisFastArrayEditor`

继承 UE 原 `FFastArraySerializer`，为 Iris 埋入 `FReplicationStateHeader + ChangeMaskStorage[4]`：
- 63-bit 元素 changemask；
- 1-bit array 总体位；
- 条件位用于 LifetimeCondition。

`TIrisFastArrayEditor<FastArray>` 提供推模式 Editor：`Add/Edit/Remove/RemoveAtSwap/Empty` 直接操作 item 的 changemask，避免 poll 扫描整个 TArray。

---

## 3. 依赖关系

- **Iris 内**：
  - `Iris/Core`（`IrisLog.h` / `IrisProfiler.h` / `MemoryLayoutUtil.h` / `BitTwiddling.h` / `NetObjectReference.h`）
  - `Iris/Serialization`（`FNetSerializer` / `FNetSerializerConfig` / `NetSerializerDelegates` / `StructNetSerializer` / `ArrayPropertyNetSerializer` / `NetSerializers.h` / `InternalNetSerializationContext`）
  - `Iris/ReplicationSystem`（`ReplicationProtocol` / `ReplicationFragment` / `FFragmentRegistrationContext` / `NetRefHandleManager` / `GlobalDirtyNetObjectTracker` / `NetHandle`）
  - `Iris/IrisConfig.h` / `IrisConfigInternal.h`
- **UE 外**：
  - `CoreUObject`（`FProperty` / `UClass` / `UStruct` / `UFunction` / `UField` / `UScriptStruct` / `CoreNet.h` 的 `ELifetimeCondition`）
  - `Core`（`Templates/RefCounting` / `Containers/Array/Map` / `Templates/Tuple` / `UObject/WeakObjectPtr/ObjectKey/Field`）
  - `NetCore`（`FNetBitArray` / `FNetHandleManager` / `GlobalDirtyNetObjectTracker` / `FastArraySerializer` / `NetDebugName`）
  - `<atomic>`（Descriptor 引用计数）

反向依赖：`ReplicationSystem`（广泛使用 Descriptor/State/FastArray 接口）。

---

## 4. 对外 API / 扩展点

### 4.1 数据结构（public）

`FReplicationStateDescriptor` / 各 MemberDescriptor / `EReplicationStateTraits` / `EReplicationStateMemberTraits` / `FReplicationStateIdentifier` / `FReplicationStateHeader` / `FNetReferenceInfo` / `FPropertyReplicationState` / `FIrisFastArraySerializer` / `TIrisFastArrayEditor`。

### 4.2 烘焙 API

`FReplicationStateDescriptorBuilder::CreateDescriptorsForClass / CreateDescriptorForStruct / CreateDescriptorForFunction`。

### 4.3 注册 API

`FPropertyNetSerializerInfoRegistry::Register / Unregister / Reset / Freeze / FindSerializerInfo / FindStructSerializerInfo / GetNopNetSerializerInfo`。

### 4.4 函数指针扩展点

- `ConstructReplicationStateFunc` / `DestructReplicationStateFunc`
- `CreateAndRegisterReplicationFragmentFunc`（允许 struct 绑定自定义 `FReplicationFragment`；官方标注 highly discouraged）

### 4.5 宏（核心）

- 手写伪生成状态：`IRIS_DECLARE_COMMON` / `IRIS_ACCESS_BY_VALUE` / `IRIS_ACCESS_BY_REFERENCE` / `IRIS_BEGIN/END_*_DESCRIPTOR` / `IRIS_IMPLEMENT_CONSTRUCT_AND_DESTRUCT` / `IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR[_WITH_TRAITS]`
- Property NetSerializer 注册：`UE_NET_IS_DECLARED_TYPE` / `UE_NET_DECLARE_NETSERIALIZER_INFO` / `UE_NET_IMPLEMENT_NETSERIALIZER_INFO` / `UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO` / `UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO` / `UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_WITH_CUSTOM_FRAGMENT_INFO` / `UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO[_WITH_SIZE_OVERRIDE]` / `UE_NET_REGISTER_NETSERIALIZER_INFO` / `UE_NET_UNREGISTER_NETSERIALIZER_INFO` / `UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES` / `UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES` / `UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES[_WITH_SIZE_OVERRIDE]`

---

## 5. 注释推进计划

- **文件数**：28 文件。
- **并行度**：**3 个 subagent 并行**：
  - agent-A：`ReplicationStateDescriptor*` + `ReplicationStateFwd` + `ReplicationStateUtil` + `*Macros`（含 `ReplicationStateDescriptorBuilder.cpp` 4000 行，重点关注）
  - agent-B：`PropertyReplicationState.*` + `InternalPropertyReplicationState.*` + `ReplicationStateStorage.*`
  - agent-C：`PropertyNetSerializerInfoRegistry.*` + `DefaultPropertyNetSerializerInfos.*` + `EnumPropertyNetSerializerInfo.*` + `IrisFastArraySerializer.*` + `ReplicationStateDescriptorConfig.*` + `InternalReplicationStateDescriptorUtils.h`
- **阶段**：**阶段 3**（与 DataStream 并行）。
- **重点关注**：
  - `FReplicationStateDescriptor` 内存布局（MemberDescriptor 数组、ChangeMask 位分配、DefaultStateBuffer、`FMemoryLayoutUtil` 拼接顺序）；
  - `FReplicationStateDescriptorBuilder` 的 FastArray 分支判定 + lifetime conditionals 合并 + RepIndex→Member 映射；
  - `FPropertyReplicationState` 的 push/poll 分支（PushModel 与传统 poll）；
  - `FIrisFastArraySerializer` 的 ChangeMaskStorage 位布局；
  - `FPropertyNetSerializerInfoRegistry::Freeze` 排序 + `Find` 二分；
  - `ELifetimeCondition` → `EReplicationCondition` 的映射。

### 注释完成情况

- [x] `ReplicationStateDescriptor.h/.cpp` + `Fwd.h` + `Util.h` + 宏文件 + `Builder.h/.cpp` + `Registry.h/.cpp` + `Config.h/.cpp`（12 文件，agent-A）
- [x] `PropertyReplicationState.h/.cpp` + `InternalPropertyReplicationState.h/.cpp` + `Storage.h/.cpp`（6 文件，agent-B）
- [x] `PropertyNetSerializerInfoRegistry.h/.cpp` + `DefaultPropertyNetSerializerInfos.h/.cpp` + `EnumPropertyNetSerializerInfo.h/.cpp` + `IrisFastArraySerializer.h/.cpp` + `InternalReplicationStateDescriptorUtils.h`（10 文件，agent-C）
- [x] 阶段 5 后补：`Public/Iris/ReplicationState/Private/IrisFastArraySerializerInternal.h`（agent 顺手完成）

**全部 28 个 ReplicationState 源文件已添加中文全量注释。**
