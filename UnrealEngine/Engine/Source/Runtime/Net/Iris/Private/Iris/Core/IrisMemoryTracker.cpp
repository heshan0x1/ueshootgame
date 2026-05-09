// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// IrisMemoryTracker.cpp —— Iris 的 LLM（Low Level Memory Tracker）Tag 定义
// ----------------------------------------------------------------------------
// 职责：给 Iris 框架的各类内存分配打上 LLM 标签，使 `stat LLM` / `stat LLMFULL`
// 以及 Memory Insights 能够把 Iris 自己吃的内存拆分到精细类别下，方便排查
// 内存热点与退化回归。
//
// LLM 标签体系说明：
//   * DECLARE_LLM_MEMORY_STAT(DisplayName, StatName, StatGroup)
//       —— 声明一个"内存统计项"（只是 stat 层面的计数器），挂在 StatGroup 下。
//       这里 StatGroup 全部使用 STATGROUP_LLMFULL，因此只有启用
//       "LLMFULL" 级别（`-llm -llmtagsets=assets` 或 Insights）时才会显示细分。
//   * LLM_DEFINE_TAG(Tag, DisplayName, ParentTag, StatFName, SummaryStatFName)
//       —— 定义一个"作用域 Tag"，与 LLM_SCOPE_BYTAG / LLM_SCOPED_TAG 配合在
//       分配点周围入栈，使期间触发的所有内存分配被归入该 Tag。
//       Parent Tag 构成树形层级；Summary Stat 决定汇总到哪一个"顶级聚合条目"，
//       Iris 的 5 个 Tag 全部汇总到 STAT_NetworkingSummaryLLM（由 UE Core 层定义
//       的"网络"汇总桶），这样在 LLM 概览面板里 Iris 的内存会整体显示在
//       "Networking" 之下，再在该条目内部按子 Tag 细分。
//
// 父子关系（与 Core.md 对照）：
//    Iris (根)
//      ├── IrisState           ("State",          父 = "Iris")
//      ├── IrisInitialization  ("Initialization", 父 = "Iris")
//      ├── IrisConnection      ("Connection",     父 = "Iris")
//      └── NetTokenStructState ("NetTokenStructState", 父 = "Iris")
//
// 五个子 Tag 选这种父子结构的理由：上层看板先按"Iris 总占用"归类，再按
// "运行态数据 / 初始化一次性开销 / 每连接开销 / NetToken 关联结构"分别拆开，
// 与实际性能优化（预算、CreateSessions、AddClient）时的关注维度对齐。
//
// 注意：头文件 IrisMemoryTracker.h 另外声明了一个 LLM_DECLARE_TAG_API(NetToken)，
// 但本文件并未用 LLM_DEFINE_TAG 定义它——实际 "NetToken" 的定义位于
// NetCore 模块，Iris 只是"声明+使用"，避免重复定义。
// ============================================================================

#include "Iris/Core/IrisMemoryTracker.h"
#include "HAL/LowLevelMemStats.h"

// -----------------------------------------------------------------------------
// LLM stat 项（计数器）声明：DisplayName、StatName、所在 StatGroup
// 挂在 STATGROUP_LLMFULL 下，仅在启用 LLMFULL 模式时展示。
// -----------------------------------------------------------------------------

/** Iris 总占用（根节点）。Iris 内所有未显式更细分类的分配都会最终汇总到此处。 */
DECLARE_LLM_MEMORY_STAT(TEXT("Iris"), STAT_IrisLLM, STATGROUP_LLMFULL);

/** 运行态状态数据：ReplicationState/Descriptor/状态缓冲/ChangeMask 等每帧读写的结构。 */
DECLARE_LLM_MEMORY_STAT(TEXT("IrisState"), STAT_IrisStateLLM, STATGROUP_LLMFULL);

/** 一次性初始化开销：模块 Startup、Serializer 注册表、Protocol 缓存创建时的分配。 */
DECLARE_LLM_MEMORY_STAT(TEXT("IrisInitialization"), STAT_IrisInitializationLLM, STATGROUP_LLMFULL);

/** 每连接（per-connection）开销：ReplicationWriter/Reader、每连接的 ChangeMask、DeltaBaseline 等。 */
DECLARE_LLM_MEMORY_STAT(TEXT("IrisConnection"), STAT_IrisConnectionLLM, STATGROUP_LLMFULL);

/**
 * NetTokenStructState：NetToken 化的 struct 状态缓冲（FStructNetTokenDataStore 等）。
 * 这部分与 NetToken 生命周期相关，便于从"Token 化导出"维度单独量化成本。
 */
DECLARE_LLM_MEMORY_STAT(TEXT("NetTokenStructState"), STAT_NetTokenStructStateLLM, STATGROUP_LLMFULL);

// -----------------------------------------------------------------------------
// LLM Tag 作用域定义
// 参数顺序：Tag, DisplayName(用于 UI 显示), ParentTag, StatFName, SummaryStatFName
// 全部 Summary 到 STAT_NetworkingSummaryLLM，使得 LLM 概览面板看到的是
// "Networking → Iris → 各子项"的层级，与网络相关分配整体同屏比较。
// -----------------------------------------------------------------------------

// Tag name, display name, parent tag name, stat name, summary stat name

/**
 * Iris 根 Tag。
 * - Name=Iris（没有 DisplayName 意味着直接使用 Tag 名字）；
 * - 无父 Tag（作为 Iris 子树的根）；
 * - Summary 到 NetworkingSummaryLLM：Iris 的内存在顶层面板会和 NetDriver、
 *   NetConnection 等一起汇总到"Networking"大类下。
 */
LLM_DEFINE_TAG(Iris, NAME_None, NAME_None, GET_STATFNAME(STAT_IrisLLM), GET_STATFNAME(STAT_NetworkingSummaryLLM));

/**
 * Iris/State：运行态复制状态相关分配。
 * 包含但不限于：FReplicationStateDescriptor、默认状态缓冲、ChangeMask 存储、
 * FReplicationStateStorage baseline 池等。
 */
LLM_DEFINE_TAG(IrisState, "State", "Iris", GET_STATFNAME(STAT_IrisStateLLM), GET_STATFNAME(STAT_NetworkingSummaryLLM));

/**
 * Iris/Initialization：框架初始化阶段一次性分配。
 * 典型位置：FIrisCoreModule::StartupModule、Descriptor Registry 启动缓存、
 * NetSerializer 注册表 Freeze 阶段构建的数据。
 */
LLM_DEFINE_TAG(IrisInitialization, "Initialization", "Iris", GET_STATFNAME(STAT_IrisInitializationLLM), GET_STATFNAME(STAT_NetworkingSummaryLLM));

/**
 * Iris/Connection：每连接动态分配。
 * FReplicationWriter/Reader、per-conn 的 PerObjectInfo 数组、DeltaCompression
 * Baseline、Attachment 队列等"连接数 × 对象数"规模的数据落在这里。
 */
LLM_DEFINE_TAG(IrisConnection, "Connection", "Iris", GET_STATFNAME(STAT_IrisConnectionLLM), GET_STATFNAME(STAT_NetworkingSummaryLLM));

/**
 * Iris/NetTokenStructState：NetToken 化 struct 的状态存储。
 * 单列出来便于评估 Token 化导出（FStructNetTokenDataStore）带来的内存成本
 * 与收益（相对降低重复引用的网络带宽）。
 */
LLM_DEFINE_TAG(NetTokenStructState, "NetTokenStructState", "Iris", GET_STATFNAME(STAT_NetTokenStructStateLLM), GET_STATFNAME(STAT_NetworkingSummaryLLM));
