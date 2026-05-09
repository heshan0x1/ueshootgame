// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"

// ============================================================================
// Iris LLM（Low Level Memory Tracker）标签声明
// ----------------------------------------------------------------------------
// 作用：
//   在 UE 的 LLM 体系下，为 Iris 框架划分 6 个内存分类标签，使得内存剖析
//   工具（`stat llm` / `stat llmfull` / 遥测）能看到"Iris 到底把内存花
//   在了哪里"。
//
// 标签一览（精细度自顶向下，隶属 NetworkingSummary 汇总组）：
//   1. Iris                —— Iris 框架根桶（Catch-all），
//                             不属于下列细分标签的所有 Iris 分配都计入这里。
//   2. IrisState           —— 复制"状态数据"本身：FReplicationStateDescriptor
//                             缓冲、量化/反量化 state buffer、baseline 快照等。
//   3. IrisInitialization  —— Iris 启动/模块加载阶段的一次性分配：
//                             NetSerializer 注册表、ReplicationFragment 注册、
//                             Config ini 解析结果等（随运行基本不增长）。
//   4. IrisConnection      —— 每连接的分配：per-connection ReplicationWriter/
//                             Reader 状态、PerObjectInfo、DeltaCompression 基线
//                             池、ChangeMaskCache 等（连接数的 O(N)）。
//   5. NetToken            —— FNetTokenStore 及字符串/Struct 等派生 Token Store
//                             的哈希表 + payload 缓冲。
//   6. NetTokenStructState —— Struct 类 NetToken（例如 FStructNetTokenDataStore）
//                             的状态数据存储，单独计一个桶便于观察"按值结构"的内存占用。
//
// 位置关系与 Core.md 的差异：
//   - Core.md §1.2 / §2.6 与该文件保持一致：共 6 个 Tag，依次对应上表。
//
// 使用方式：
//   - 分配热点处：`LLM_SCOPE_BYTAG(IrisState);` 进入作用域把分配重定向到对应标签；
//   - 模块 bootstrap 处：`LLM(LLM_SCOPE_BYNAME(TEXT("Iris")))` 粗粒度包裹。
// ----------------------------------------------------------------------------

/** Iris 根标签：所有未被下列子标签覆盖的 Iris 分配都归入此处。 */
LLM_DECLARE_TAG_API(Iris, IRISCORE_API);

/** 复制状态数据专用标签：FReplicationStateDescriptor、state buffer、baseline 等。 */
LLM_DECLARE_TAG_API(IrisState, IRISCORE_API);

/** 初始化阶段分配标签：Serializer/Fragment 注册表、Config 解析等一次性内存。 */
LLM_DECLARE_TAG_API(IrisInitialization, IRISCORE_API);

/** 每连接分配标签：ReplicationWriter/Reader、PerObjectInfo、DeltaCompression 基线池等。 */
LLM_DECLARE_TAG_API(IrisConnection, IRISCORE_API);

/** NetToken 存储标签：FNetTokenStore/String/Name/Struct TokenStore 的哈希表与 payload。 */
LLM_DECLARE_TAG_API(NetToken, IRISCORE_API);

/** Struct 类 NetToken 状态存储专用标签：独立计桶以便观察 StructState 的占用趋势。 */
LLM_DECLARE_TAG_API(NetTokenStructState, IRISCORE_API);
