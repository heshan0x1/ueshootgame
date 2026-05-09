# MassEntity 疑似 Bug 与改进清单

> 本文档由"为 MassEntity 添加中文注释"任务过程中累积，每条均为人工审阅源码后的发现，均已在对应源文件的中文注释中就地标注。
>
> 文档不修改源码，仅作记录与待办。建议结合源码（行号会随版本漂移）确认后再决定是否修复或上报 Epic。
>
> 模块根目录：`D:\workspace\ShootGame\UnrealEngine\Engine\Source\Runtime\MassEntity\`
>
> **统计**：共 **76** 项发现 = 8 项严重 + 7 项中等 + 41 项较低（typo/死代码/不一致）+ 8 项设计/文档"灰色区域"+ 12 项一致性提示

---

## 目录

- [§1 严重（功能失效或崩溃风险，建议优先修复）](#1-严重)
- [§2 中等（行为不一致，业务踩坑风险）](#2-中等)
- [§3 较低（typo / 死代码 / 不一致 / 风格）](#3-较低)
- [§4 设计 / 文档"灰色区域"（需澄清）](#4-灰色区域)
- [§5 性能 / 设计可优化点](#5-可优化点)

---

## 1. 严重

> 这一类如果业务代码命中相关路径，会出现"功能失效"、"崩溃"、或"无声错位"的后果。优先级最高。

### B-001 · `FMassCommandAddFragmentInstances` ctor 硬编码 `Set` 操作类型

| 项 | 内容 |
|---|---|
| 文件 | `Public/MassCommands.h` |
| 位置 | 大致行 409–410（构造函数） |
| 严重程度 | **严重** |
| 模块 | M7 Command Buffer |

**现象**

```cpp
FMassCommandAddFragmentInstances(EMassCommandOperationType OperationType = EMassCommandOperationType::Set DEBUG_NAME_PARAM(...))
    : Super(EMassCommandOperationType::Set FORWARD_DEBUG_NAME_PARAM)   // ← 这里硬编码 Set！
```

形参 `OperationType` 完全被丢弃。

**影响**

派生类 `FMassCommandBuildEntity` 在 ctor 中传 `EMassCommandOperationType::Create` 想覆盖父类，但实际仍落到 `Set` 阶段执行。`BuildEntity` 不会按 Create 阶段在 Flush 第一组执行，而是在 Set 阶段（4）执行——极可能与原设计意图不符（命令名/注释/排序表都按 Create 来理解）。

**建议修复**

```cpp
: Super(OperationType FORWARD_DEBUG_NAME_PARAM)
```

---

### B-002 · `FGroupEntities::Add` 未调 `Super::Add`，命令永不执行

| 项 | 内容 |
|---|---|
| 文件 | `Public/MassArchetypeGroupCommands.h` |
| 位置 | 大致行 30 |
| 严重程度 | **严重** |
| 模块 | M7 Command Buffer |

**现象**

函数把 entity 全塞进自家 `Groups` map，**没有把 entity 推入基类的 `TargetEntities`，也没有把 `bHasWork` 置为 true**。

**影响**

- 基类 `HasWork()` 一直返回 false → Flush 阶段 `CommandTypeOrder` 映射判断 `HasWork()==false`，命令被排到 `MAX_int32` 也就是"无效"组、最终被 `IsValid()` 判定后**整个跳过执行**。
- 这条命令很可能根本不会运行（除非另一条同类命令把 `bHasWork` 置为 true）。
- `GetNumOperationsStat()` 永远返回 0，CSV 计数失真。

**建议修复**

至少补一行 `bHasWork = true;`，并酌情考虑统一通过 `TargetEntities` 跟踪（或重写 `HasWork()` / `GetNumOperationsStat()` 反映 `Groups.Num()`）。

---

### B-003 · `FMassProcessingPhaseManager` 两个 delegate 都注册到同一事件

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassProcessingPhaseManager.cpp` |
| 位置 | 构造函数 |
| 严重程度 | **严重** |
| 模块 | M9 Processor & Scheduling |

**现象**

```cpp
OnDebugEntityManagerInitializedHandle   = FMassDebugger::OnEntityManagerInitialized.AddRaw(
    this, &FMassProcessingPhaseManager::OnDebugEntityManagerInitialized);
OnDebugEntityManagerDeinitializedHandle = FMassDebugger::OnEntityManagerInitialized.AddRaw(
    //                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //                              第二行也注册到 OnEntityManagerInitialized
    this, &FMassProcessingPhaseManager::OnDebugEntityManagerDeinitialized);
```

两行都注册到 `OnEntityManagerInitialized` —— 第二行应当是 `OnEntityManagerDeinitialized`。

**影响**

deinit 永远不会回调到 `OnDebugEntityManagerDeinitialized`。

**建议修复**

把第二行改为 `FMassDebugger::OnEntityManagerDeinitialized.AddRaw(...)`。

---

### B-004 · `FMassProcessingPhaseManager` 析构未 Remove debug delegate handles（UAF 风险）

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassProcessingPhaseManager.cpp` |
| 位置 | 析构函数 |
| 严重程度 | **严重** |
| 模块 | M9 Processor & Scheduling |

**现象**

构造时 `AddRaw(this, ...)` 注册了两个 handle，析构时**没有任何** `Remove`。

**影响**

`FMassProcessingPhaseManager` 析构后，`FMassDebugger` 的多播仍持有原始 `this` 指针——下次 broadcast 即 use-after-free。

**建议修复**

```cpp
~FMassProcessingPhaseManager()
{
    FMassDebugger::OnEntityManagerInitialized.Remove(OnDebugEntityManagerInitializedHandle);
    FMassDebugger::OnEntityManagerDeinitialized.Remove(OnDebugEntityManagerDeinitializedHandle);
    // ...
}
```

---

### B-005 · `SetFragmentWriteBreakForSelectedEntity` 漏写引用 `&`

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassDebugger.cpp` |
| 位置 | `FMassDebugger::SetFragmentWriteBreakForSelectedEntity` |
| 严重程度 | **严重** |
| 模块 | M14 Debug |

**现象**

```cpp
FBreakpoint NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();
//          ^ 缺 & ：拿的是值拷贝而非引用
NewBreakpoint.SomeField = ...; // 写到栈拷贝里
```

**影响**

后续对 `NewBreakpoint` 的字段写入全部丢失。结果是该函数实质上**只把空白断点入队**，写入 fragment 时虽然进入慢路径但找不到匹配的 trigger，最终始终返回 Invalid。

**真正的写入断点功能在被选中实体路径上失效**——直接影响 `mass.debug.SetFragmentBreakpoint` 控制台命令的可用性。

**建议修复**

```cpp
FBreakpoint& NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();
```

---

### B-006 · `ClearAllFragmentWriteBreak` 比较了错误的 TriggerType

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassDebugger.cpp` |
| 位置 | `FMassDebugger::ClearAllFragmentWriteBreak` |
| 严重程度 | **严重** |
| 模块 | M14 Debug |

**现象**

函数名/语义是清除 fragment 写入断点，但代码里比较的是 `ETriggerType::ProcessorExecute`，应当是 `FragmentWrite`。

**影响**

1. 想清除的断点不被清除；
2. 试图把 `Trigger.Get` 当作 ScriptStruct 解读 `ProcessorExecute` trigger（实际上 `Trigger` 是 `UMassProcessor`），可能触发 `TVariant` 断言。

**建议修复**

把比较常量改为 `ETriggerType::FragmentWrite`。

---

### B-007 · `FConstOptionalFragmentAccess::ConfigureQuery` 漏传 `Optional` 存在性

| 项 | 内容 |
|---|---|
| 文件 | `Public/MassQueryExecutor.h` |
| 位置 | `FConstOptionalFragmentAccess::ConfigureQuery` |
| 严重程度 | **严重** |
| 模块 | M4 Query |

**现象**

```cpp
EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly);
//                                ↑ 缺少 EMassFragmentPresence::Optional
```

与同文件的 `FMutableOptionalFragmentAccess::ConfigureQuery` 不一致——后者正确传了 `EMassFragmentPresence::Optional`。

**影响**

按当前实现，"const optional" accessor 实际会被注册为 "const required"，导致 archetype 匹配错误（要求 fragment 必须存在）——所有"声明 const optional 但 archetype 没有该 fragment"的 query 都会丢失对应的 archetype。

**建议修复**

```cpp
EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
```

---

### B-008 · `FBreakpoint::ApplyEntityFilterByFragments` Query 分支永远返回 false

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassDebuggerBreakpoints.cpp` |
| 位置 | `FBreakpoint::ApplyEntityFilterByFragments`，Query 分支末尾 |
| 严重程度 | **严重** |
| 模块 | M14 Debug |

**现象**

所有 requirement 通过后返回 `false` 而非 `true`——逻辑上 Query 过滤器在 fragment 列表场景**永远不可能"通过"**。

**影响**

结合调用上下文（`CheckCreateEntityBreakpoints` 用此函数），这意味着 `FilterType::Query` 的"实体创建"断点几乎不会触发。

**建议修复**

末尾改为 `return true;`。

---

## 2. 中等

> 行为不一致或踩坑场景。业务代码"碰巧"会工作，但语义偏离设计意图，需要小心使用。

### B-009 · `SwapTagsForEntity` 不触发 observer 通知

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassEntityManager.cpp` |
| 位置 | `SwapTagsForEntity`（约 1262–1288） |
| 严重程度 | 中 |
| 模块 | M6 EntityManager |

**现象**

直接调用 `MoveEntityToAnotherArchetype`（archetype 层），**不调** `ObserverManager.OnPreCompositionRemoved` / `OnPostCompositionAdded`。

对比：`AddTagToEntity` / `RemoveTagFromEntity` 都会触发对应 observer 通知。

**影响**

项目代码若依赖 tag observer 监听状态切换（如 `FStateA` → `FStateB`），用 `SwapTagsForEntity` 会静默丢失通知，而 `RemoveTag(A) + AddTag(B)` 会正常触发。

**建议修复**

在 `SwapTagsForEntity` 内补上 `OnPreCompositionRemoved(OldTagBitSet)` 和 `OnPostCompositionAdded(NewTagBitSet)`。

---

### B-010 · `FMassCommandRemoveTagsInternal::CheckBreakpoints` 调用 Add 路径

| 项 | 内容 |
|---|---|
| 文件 | `Public/MassCommands.h` |
| 位置 | 约 374 行 |
| 严重程度 | 中 |
| 模块 | M7 Command Buffer |

**现象**

```cpp
return UE::Mass::Debug::FBreakpoint::CheckFragmentAddBreakpoints(...);
//                                               ^^^ 应当是 Remove
```

按上下文（Add 用 CheckFragmentAddBreakpoints，Remove 用 CheckFragmentRemoveBreakpoints；且这里是 *Tag* Remove）应为 `CheckFragmentRemoveBreakpoints` 或专用的 Tag 版本。形参名 `InFragments`（实际是 tags）也佐证此处是 copy-paste 残留。

**影响**

Remove tag 操作上设的断点不会触发；调试器在该路径上失明。

**建议修复**

调用对应的 Remove/Tag 断点函数。

---

### B-011 · `FMassFragmentRequirements::Reset()` 漏清 `bRequiresGameThreadExecution`

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassRequirements.cpp` |
| 位置 | `FMassFragmentRequirements::Reset()` |
| 严重程度 | 中 |
| 模块 | M4 Requirements |

**现象**

注意 `FMassSubsystemRequirements::Reset()` 是清零的，但 `FMassFragmentRequirements::Reset()` 漏掉了 `bRequiresGameThreadExecution`。

**影响**

曾经声明过 GT-only shared fragment 的 query 实例 Reset 后再添加非 GT-only 需求，仍会汇报 GT-only，造成不必要的 GT 串行化（错失并行度）。

**建议修复**

`Reset()` 末尾加 `bRequiresGameThreadExecution = false;`。

---

### B-012 · `FMassQueryRequirementIndicesMapping::IsEmpty()` 用 `||` 而非 `&&`

| 项 | 内容 |
|---|---|
| 文件 | `Internal/MassArchetypeData.h` |
| 位置 | `FMassQueryRequirementIndicesMapping::IsEmpty()` |
| 严重程度 | 中 |
| 模块 | M2 Archetype |

**现象**

```cpp
return EntityFragments.Num() == 0 || ChunkFragments.Num() == 0;
//                                ^^ 用 || 不是 &&
```

只要任意一类（entity 或 chunk）需求为空就视作"empty"。

**影响**

若一个 query 只需要 chunk fragment 而不需要 entity fragment（理论上合法），它会被错误地视作 empty，可能跳过应有的执行。

**建议修复**

确认作者意图：若是"两者都为空才算空"，改为 `&&`；若是其它语义，应在文档中明确并改名。

---

### B-013 · `BatchGroupEntities` 缺失 observer 接入（已知 TODO）

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassEntityManager.cpp` |
| 位置 | 约 914–952 |
| 严重程度 | 中 |
| 模块 | M6 EntityManager |

**现象**

代码注释 `// we need something like the following to support observers` 直接保留在 `BatchMoveEntitiesToAnotherArchetype` 调用处，说明 5.6 引入 ArchetypeGroup 时 observer 接入未完成。

**影响**

archetype group 变化无法被 observer 监听（如"entity 进入 LOD0 桶"事件无法触发 processor 反应）。

**建议修复**

补全相应的 observer 通知。

---

### B-014 · `MassChildOf.cpp::Execute` 硬编码 `FMassChildOfFragment::StaticStruct()`

| 项 | 内容 |
|---|---|
| 文件 | `Private/Relations/MassChildOf.cpp` |
| 位置 | `UMassChildOfRelationEntityCreation::Execute` |
| 严重程度 | 中 |
| 模块 | M15 Built-in Relations |

**现象**

注释说"this observer processor being used by relations that extend the ChildOf"，但代码硬编码 fragment 类型 `FMassChildOfFragment::StaticStruct()`。

**影响**

派生关系类型若复用本 observer，会写错 fragment（仍写到 `FMassChildOfFragment` 上而不是派生类的 fragment）。

**建议修复**

改为 `RelationData.Traits.RoleTraits[Subject].Element`。

---

### B-015 · 已废弃的 `FMassFragmentRequirements` 构造函数总是 check 失败

| 项 | 内容 |
|---|---|
| 文件 | `Private/MassRequirements.cpp` |
| 位置 | 约 421–435 |
| 严重程度 | 中 |
| 模块 | M4 Requirements |

**现象**

`FMassFragmentRequirements(initializer_list)` / `(TConstArrayView)` 直接调用 `AddRequirement`，而 `AddRequirement` 的 `checkf(bInitialized, ...)` 在没经过 `Initialize()` 的情况下必然触发。

**影响**

这两个构造函数等于不可用——任何调用都会崩。5.6 标 deprecated 是合理的。

**建议修复**

要么把 `bInitialized` 直接置 true（或换成 ensure），要么彻底移除这两个构造函数。

---

## 3. 较低

> typo / 死代码 / 注释错位 / 风格不一致 / 微小性能瑕疵。逐项列出供 code review 时一并处理。

### 3.1 Typo

| ID | 位置 | 错 | 应为 |
|---|---|---|---|
| L-001 | `MassRequirementAccessDetector.h` 私有方法名 | `Aquire` | `Acquire` |
| L-002 | `MassExternalSubsystemTraits.h` 第 17 行示例注释 | `enum { GameThreadOnly = false; }` | `enum { GameThreadOnly = false };` |
| L-003 | `MassExecutionContext.h` 约 24 行 ASCII 注释 | `MaxBreakFragmentCount` | `MaxFragmentBreakpointCount` |
| L-004 | `MassSubsystemAccess.h` 注释 | `equivelncy` | `equivalency` |
| L-005 | `MassExecutor.h` 约 55 行注释 | `deprevate` | `deprecated` |
| L-006 | `MassRelationObservers.cpp` 约 365 行 | `// fall through on purse` | `// fall through on purpose` |
| L-007 | `MassSettings.h` 第 12 行 | `common parrent` | `common parent` |
| L-008 | `MassEntityCollection.h` 约 190 行 | `Can go our of date` | `Can go out of date` |
| L-009 | `MassEntityManagerStorage.cpp` `GetAllocatedSize` | `MagPageCount` | `MaxPageCount` |
| L-010 | `MassChildOf.cpp` 第 61 行注释 | `// UMassRelationEntityCreation` | `// UMassChildOfRelationEntityCreation` |
| L-011 | `MassEntitySettings.cpp` 约 38 行注释 | `register with UMassGameplaySettings` | `register with UMassSettings` |
| L-012 | `MassEntityElementTypes.h` | `MASS_INVALID_FRAGMENT_MSG_F`（`_F` 像 printf-family） | `MASS_INVALID_FRAGMENT_MSG_FMT` 或在注释里强调 |

### 3.2 死代码 / 未使用变量 / 多余分号

| ID | 位置 | 描述 |
|---|---|---|
| L-013 | `TypeBitSetBuilder.h:286` `TTypeBitSetBuilder::operator+` | 调用了不存在的"只接 `FStructTracker&`"构造，疑似从未被实例化 |
| L-014 | `TypeBitSetBuilder.h:203` `GetTypeBitSet<T>` | 同上，构造参数 `int32` 不存在 |
| L-015 | `MassProcessor.cpp::UpdateProcessorsCollection` | 局部 `PhaseConfig` 取出后从未使用 |
| L-016 | `MassEntitySettings.cpp::BuildPhases` | `PhaseDumpDependencyGraphFileName` 计算后未传给下游，看起来是死代码 |
| L-017 | `MassEntitySettings.cpp::PostEditChangeChainProperty` | `FProperty* Property = ...` 局部赋值后未使用 |
| L-018 | `MassRelationManager.cpp:9` | `#include "Interfaces/ITextureFormat.h"` 与关系系统无关，疑似错误遗留 |
| L-019 | `MassRelationManager.cpp` 末尾 | `#undef WITH_RELATIONSHIP_VALIDATION`，但全文未 `#define`，是空 undef |
| L-020 | `MassCommands.h:488` `TCommandTraits<FMassCommandAddElement>` 末尾 | `};;` —— 多打了一个分号 |
| L-021 | `MassEntityManagerStorage.cpp::FConcurrentEntityStorage::Num()` 末尾 | `;;` —— 多余分号 |
| L-022 | `MassObserverManager.cpp:674-679 / 731-736` `AddObserverInstance` / `RemoveObserverInstance` | `check` + 三元 fallback 的 false 分支永远走不到，是死代码 |
| L-023 | `MassExecutor.cpp:182` `RunProcessorsView` deprecated 实现 | 没有 `UE_DEPRECATED` 宏头标记（虽然 .h 中有） |

### 3.3 一致性 / 对称性问题

| ID | 位置 | 描述 |
|---|---|---|
| L-024 | `MassDebugLogging.h:83` | `GetLogOwner` 返回类型在 debug/release 不一致（`const UObject*` vs `UObject*`） |
| L-025 | `MassDebugLogging.h:74` | non-debug 分支构造函数形参 `bInCheckVisualLoggerForRecording`，debug 分支 `bInLogEverythingWhenRecording`，命名不统一 |
| L-026 | `MassArchetypeGroup.h:61` `FArchetypeGroupID(const uint32)` | 缺 `explicit`，与 `FArchetypeGroupType` 不对称 |
| L-027 | `MassEntityManagerStorage.cpp::IEntityStorageInterface::GetEntityState` doc | 注释写"Returns true if reserved, false otherwise"，但实际返回三态枚举 |
| L-028 | `IEntityStorageInterface::Num()` doc vs `FConcurrentEntityStorage::Num()` 实现 | 接口 doc 说"返回非 Free 实体数"，实现返回所有已分配槽位数（含 Free） |
| L-029 | `FSingleThreadedEntityStorage::GetEntityState` 中 `uint32 CurrentSerialNumber` | 字段是 `int32`，局部 `uint32`。仅 `!=0` 比较所以无害，但风格不一致 |
| L-030 | `MassRequirements`：`ChunkFragmentRequirements` / `SharedFragmentRequirements` / `ConstSharedFragmentRequirements` 在 `Presence::None` 分支不写 TArray，而 `FragmentRequirements` 在 `Presence != None` 时才写 TArray。两套语义互补但不对称 | 需要在 doc 中点明 |
| L-031 | `MassObserverManager.h:144` `DummyCollection` | 静态变量未标 `const`，调用方可能误改 |
| L-032 | `MassObserverNotificationTypes.h:248–260` | 注释保留了原作者尝试 `compare_exchange_weak` 的痕迹，但实际是单线程实现，注释令人困惑 |
| L-033 | `MassObserverManager.cpp:516–540` | 栈 `LocalContextBuffer` 用 placement-new + 手动析构，**异常不安全** |
| L-034 | `MassObserverRegistry` vs `MassObserverManager` | Registry `FObserverClassesMap` 不带 UPROPERTY；Manager `FMassObserversMap` 带 UPROPERTY。GC 引用语义差异未文档化 |
| L-035 | `FMassExecutionContext::GetSubsystemInternal` | `(T*)Subsystems[SystemIndex]` 强转后赋回 USubsystem*，是无意义的中间步骤 |
| L-036 | `FMassExecutionContext::GetDummyInstance` | `new FMassEntityManager()` 静态实例不会 `Initialize()`，仅作占位 |
| L-037 | `FMassExecutionContext::PushQuery` `BreakFragmentsCount` 上限 8 | 静默丢弃多余断点，调试时可能无声忽略 |
| L-038 | `FMassCommandBuffer::GetAllocatedSize` | 漏算 `AppendedCommandInstances` 自身数组容量 |
| L-039 | `UE::Mass::Debug::CallCheckBreakpointsByInstance` 函数 vs concept | concept `HasCheckBreakpointsWithEntity` 检测的是 `CheckBreakpoints(...)`，但函数体调 `CheckBreakpointsByInstance(...)`；任何匹配 concept 但只实现 `CheckBreakpoints` 的用户命令会编译失败 |
| L-040 | `MassRelationObservers.cpp::ConfigureRelationObserver` | 用 `const_cast` 去掉 `RelationFragmentType` 的 const，暗示父类 `ObservedType` 应声明 `const UScriptStruct*` |
| L-041 | `MassEntityManager.cpp` `BatchChangeTagsForEntities` | `if (TagsAdded.IsEmpty() == false)` 条件 vs `bTagsAddedAreObserved` 不对称（虽然 ObserverManager 内部会再过滤，仍易让阅读者困惑） |

---

## 4. 灰色区域

> 设计/文档需澄清的语义边界。不一定是 bug，但容易让使用者踩坑。

### G-001 · `FRelationTypeTraits::IsValid()` 仅声明未实现

- `Public/MassEntityRelations.h:124` 声明 `bool IsValid() const;`
- `Private/MassEntityRelations.cpp` 中没找到实现
- 若有调用方使用会导致**链接错误**
- **建议**：要么补 cpp 实现，要么改为 inline `{ return RelationTagType != nullptr && RelationFragmentType != nullptr; }`

### G-002 · `FEntityData` 30-bit Generation 设计动机未文档化

- 类型 `uint32` + `bIsAllocated:1` = 31 位可用，留出 30 位是为了对齐还是留作未来扩展？
- 源码无说明，建议补一行注释/文档

### G-003 · `FMassRelationRoleInstanceHandle` 不存 SerialNumber

- handle 仅 8 字节（30+30+2 bit），运行时还原完整 EntityHandle 时通过 `EntityManager.CreateEntityIndexHandle` 拉取当前 SerialNumber
- 这是性能/正确性的取舍点，应作为"关系系统已知限制"列出

### G-004 · `FArchetypeGroups::GetID` 潜在 UB

- 用 `IDContainer.IsValidIndex(...)` 而非 `IsAllocated`
- `TSparseArray::IsValidIndex` 只检查"下标 < MaxIndex"，不检查该槽位是否已分配
- 如果某个 GroupType 曾被 Remove 且不是尾部（未触发 Shrink），此处会读到被销毁槽位的残留值
- **建议**：改用 `IDContainer.IsAllocated(Index)`

### G-005 · `FRelationManager` 递归限制 10 vs BFS 上限 64

- `FScopedRecursiveLimit` 用 `check(LimitRef < 10)` 限制深度
- `IsSubjectOfRelationRecursive` 的迭代上限是 64（`constexpr int32 IterationsLimit = 64`）
- 两个值不一致——前者是 destruction observer 链深度，后者是查询 BFS 步数；应提到命名常量并文档化

### G-006 · `FMassChildOfRelation` Subject 端 `bExclusive=false` 的语义

- 典型"父子树"应该是"一个 child 只有一个 parent"
- 按字面理解 `bExclusive=false` 似乎允许多父
- 但 `FMassChildOfFragment` 仅存单个 `Parent` 字段——多父时只能存第一个还是覆盖？
- **建议**：在文档/代码中澄清

### G-007 · `FRoleTraits::RequiresExternalMapping=No` 路径"未通"

- `MassEntityRelations.h:91` 官方 `@todo`：`RequiresExternalMapping=No` 路径"is not plugged in yet"
- `FRoleTraits` 的这个字段当前实际不工作，所有路径都假定 Yes
- 文档应明确标注此限制

### G-008 · `FMassArchetypeSharedFragmentValues::Reset` 与 `DirtyHashCache` 的 `bSorted` 行为差异

- `Reset` 把 `bSorted = false`
- `DirtyHashCache` 按"元素数 ≤ 1"设置
- 二者不一致可能导致 Reset 后立刻调用 `CalculateHash` 触发 ensure
- 看起来是有意为之（空容器 Reset 后要求外部显式重建），但值得在文档强调

---

## 5. 可优化点

> 性能/可读性/未来维护改进建议。

### O-001 · `FMassRuntimePipeline::AppendUniqueRuntimeProcessorCopies` 5.6 行为变更日志文案

- 日志 `"Skipping %s due to it being a duplicate"` 与实际触发分支（`ShouldAllowMultipleInstances() == false`）有微妙出入

### O-002 · `FMassTrace::OutputBeginPhaseWithID` 两次调用 `FPlatformTime::Cycles64()`

- PhaseId 和 Cycle 字段取自两次独立调用
- 高精度场景下可能略有偏差
- **建议**：用同一变量

### O-003 · `FMassTrace::OutputRegisterFragment` 未覆盖 ChunkFragment / ConstSharedFragment

- 这两类 element 会被标为 `FFragmentType::Unknown`
- Insights 上看不出区别
- **建议**：补充分类值

### O-004 · `FMassTrace::OutputRegisterArchetype` 未注册 Shared/ConstShared/Chunk fragments

- 事件字段 `Fragments` 仅含 FragmentBitSet + TagBitSet
- Insights 端无法区分 archetype 是否含 shared

### O-005 · `FMassTrace::EntitiesDestroyed` 强转布局依赖

- `reinterpret_cast<const uint64*>(Entities.GetData())` 假设 `FMassEntityHandle == {int32 Index, int32 SerialNumber}` 且无填充
- M0 层若变更 Handle 结构（如加调试字段）将静默发送错误数据
- **建议**：加 `static_assert(sizeof(FMassEntityHandle)==sizeof(uint64))`

### O-006 · `FConcurrentEntityStorage::Release` / `ForceRelease` 字段更新放在锁外

- `++GenerationId` / `bIsAllocated=0` / `CurrentArchetype.Reset()` 三条更新在锁外执行
- 同一时刻另一线程的 `IsEntityActive(handle)` 可能读到中间态
- 调用方约定的"瞬时近似"语义可以容忍 *查询*，但不能容忍 *持有结果做后续访问*
- **建议**：把字段更新移到 `FreeListLock` 内，或改用 atomic generation+RCU 风格发布

### O-007 · `FMassEntityQuery::ForEachEntityChunk(EntityCollection, ...)` 复制开销

- 原作者注释：`// mz@todo I don't like that we're copying data here.`
- `SetEntityCollection` 涉及一次复制
- **建议**：改为引用/借用语义

### O-008 · `FResourceUsage::HasArchetypeConflict` 过度保守

- 注释 `@todo`："如果跟踪了具体哪个 Element 冲突，可以更精确"
- 当前实现是"任一类资源里任一 user 与 InArchetypes 重叠 → 视为整体冲突"
- 可能导致误报、动态边过多、并发度降低

### O-009 · `MassArchetypeData.cpp` 1524-1729 行大段注释代码

- 200 行的历史遗留 `//` 注释代码（已被 `FMassDebugger::GetArchetypeRequirementCompatibilityDescription` 取代）
- 严重影响文件可读性
- **建议**：整段删除或迁移到 git history

### O-010 · `CompactEntities` 命名易反直觉

- `ChunkToFill` 取自 sort 后 NumInstances 最少的 chunk（"最空的"）
- 但语义是被填满的目标
- **建议**：改名 `LeastPopulatedTarget` / `MostPopulatedSource`

### O-011 · `SortArchetypes(FirstNewArchetypeIndex)` 增量参数名误导

- 函数签名暗示增量，但内部 `Ranges.Add({0, OrderedArchetypeIndices.Num()})` 是从头开始排，等同于全量重排
- 可能是早期增量实现遗留的接口

### O-012 · `MassQueryExecutor.cpp::ValidateAccessors` 看起来有笔误

```cpp
const uintptr_t AccessorsStart = (uintptr_t)this;   // ← 应为 (uintptr_t)AccessorsPtr
checkf(ExecutorStart <= AccessorsStart && AccessorsStart <= ExecutorEnd, ...)
```

`AccessorsStart` 取的是 `this` 而不是 `AccessorsPtr`，导致 check 永远通过、不能真正验证 "AccessorsPtr 必须是派生类成员变量"这个约束。

---

## 附录 A · 严重 Bug 一键定位（grep 友好）

| ID | 文件 | 关键标识符（搜索用） |
|---|---|---|
| B-001 | `Public/MassCommands.h` | `FMassCommandAddFragmentInstances` ctor 内 `Super(EMassCommandOperationType::Set` |
| B-002 | `Public/MassArchetypeGroupCommands.h` | `FGroupEntities::Add` |
| B-003 | `Private/MassProcessingPhaseManager.cpp` | `OnDebugEntityManagerDeinitializedHandle` |
| B-004 | `Private/MassProcessingPhaseManager.cpp` | `~FMassProcessingPhaseManager` |
| B-005 | `Private/MassDebugger.cpp` | `SetFragmentWriteBreakForSelectedEntity` |
| B-006 | `Private/MassDebugger.cpp` | `ClearAllFragmentWriteBreak` |
| B-007 | `Public/MassQueryExecutor.h` | `FConstOptionalFragmentAccess::ConfigureQuery` |
| B-008 | `Private/MassDebuggerBreakpoints.cpp` | `ApplyEntityFilterByFragments` |

---

## 附录 B · 推荐处理顺序

1. **先打补丁的（影响功能）**：B-005 / B-006（调试器命令完全不可用），B-001（Build 命令排序错误），B-002（命令永不执行），B-007（query 匹配错误）
2. **再修内存安全的**：B-004（UAF），B-003（delegate 注册错位）
3. **再补语义一致的**：B-008，B-009，B-010
4. **最后清扫细节**：§3 typo / 死代码 / 风格

---

## 附录 C · 文档约定

- **B-XXX**：严重或中等 bug（功能/正确性）
- **L-XXX**：较低（typo / 死代码 / 不一致）
- **G-XXX**：灰色区域（设计语义需澄清）
- **O-XXX**：可优化点（性能/可读性/未来）

行号会随 Epic 版本更新漂移。建议用上方"关键标识符"做 `grep`/`Find in Files` 定位。

---

*本清单与 `ARCHITECTURE.md` 配套使用。建议任何修复前先在源文件中找到对应中文注释（其中已就地标注了"疑似 bug"），核对上下文。*
