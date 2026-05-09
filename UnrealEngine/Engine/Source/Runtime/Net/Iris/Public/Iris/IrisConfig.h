// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// IrisConfig.h —— Iris 框架对外公开的「全局开关 / 调试宏」配置
// -----------------------------------------------------------------------------
// 本文件承担两类职责：
//   (A) 提供 3 个 IRISCORE_API 函数，供引擎 / 上层（典型为 UNetDriver）查询 /
//       修改「是否启用 Iris 复制系统」的全局意图：
//         * ShouldUseIrisReplication()           —— 读 CVar net.Iris.UseIrisReplication
//         * SetUseIrisReplication(bool)          —— 写 CVar
//         * GetUseIrisReplicationCmdlineValue()  —— 解析命令行
//             -UseIrisReplication=N（1=Iris；0=Generic）
//             返回 EReplicationSystem::Default/Generic/Iris
//       UNetDriver::IsUsingIrisReplication() 读取此 CVar 决定是否真正接入
//       UReplicationSystem。命令行覆盖发生在 FIrisCoreModule::StartupModule()
//       的非常早期，确保 NetDriver 初始化前就已生效。
//
//   (B) 暴露若干「编译期宏开关」：UE_NETBITSTREAMWRITER_VALIDATE /
//       UE_NETBITSTREAMREADER_VALIDATE / UE_NET_IRIS_CSV_STATS /
//       UE_NET_THREAD_SAFETY_CHECK / UE_NET_ASYNCLOADING_DEBUG。
//       这些宏出现在 NetBitStream / Stat / 异步加载等多个公共头中，必须
//       随头一同分发，否则会因 ABI 不一致导致内联函数体积/字段偏移错乱。
//
// 注意：EReplicationSystem 的 enum 定义位于 Net/Core/Connection/NetEnums.h，
// 此处只 include 不重复声明，保证 Iris 与 NetCore 共用同一枚举。
// =============================================================================

#pragma once

#include "HAL/Platform.h"

#include "Net/Core/Connection/NetEnums.h"
#include "ProfilingDebugging/CsvProfilerConfig.h"

namespace UE::Net
{

/** Returns if the preferred replication system should be Iris. */
// 查询全局意图：是否应启用 Iris 复制系统。
// 实现：读 CVar net.Iris.UseIrisReplication（>0 即启用）。
// 调用方：UNetDriver::IsUsingIrisReplication() —— 决定 NetDriver 启动时是否
// 创建 UReplicationSystem 并切到 Iris 路径。
IRISCORE_API bool ShouldUseIrisReplication();

/** Set if the preferred replication system should be Iris or not. */
// 设置全局意图。一般情况下不应在运行期切换，因为 NetDriver 已经实例化；
// 主要使用场景：FIrisCoreModule::StartupModule() 在 NetDriver 创建前根据
// 命令行覆盖 CVar；以及 dev 工具 / 测试用例。
IRISCORE_API void SetUseIrisReplication(bool EnableIrisReplication);

/** Returns what replication sytem was set to be used by the cmdline. Returns Default when the command line was not set. */
// 解析命令行 -UseIrisReplication=N。
// 返回值：
//   * EReplicationSystem::Iris    —— 命令行指定 N>0；
//   * EReplicationSystem::Generic —— 命令行指定 N<=0（强制使用旧 Replication）；
//   * EReplicationSystem::Default —— 命令行未指定，由 ini / 默认值决定。
// 注：`-LegacyReplication` 这个开关由调用层根据需要自行映射，本函数只识别
// `-UseIrisReplication=N` 这一种形式。
IRISCORE_API EReplicationSystem GetUseIrisReplicationCmdlineValue();

}

/* NetBitStreamReader/Writer validation support */
// FNetBitStreamWriter / FNetBitStreamReader 的「越界 / 状态」校验开关。
// Dev / Editor 下默认开启：写超过容量、读越界、Commit 顺序错误等都会 ensure；
// Shipping/Test 默认关闭，避免热路径产生分支与 ensure 开销。
// 注意：此宏会影响公共头 NetBitStreamReader/Writer 的内联实现，因此必须
// 在 Public/ 中暴露，且模块编译/外部调用方编译必须使用相同值。

#ifndef UE_NETBITSTREAMWRITER_VALIDATE
#define UE_NETBITSTREAMWRITER_VALIDATE !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

#ifndef UE_NETBITSTREAMREADER_VALIDATE
#define UE_NETBITSTREAMREADER_VALIDATE !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

/** CSV stats. */
// Iris 的 CSV Profiler 统计开关；默认跟随引擎全局 CSV_PROFILER_STATS 决策。
// 关闭后 CSV_SCOPED_TIMING_STAT / CSV_CUSTOM_STAT 在 Iris 范围内变为空操作。
#ifndef UE_NET_IRIS_CSV_STATS
#	define UE_NET_IRIS_CSV_STATS CSV_PROFILER_STATS
#endif

/** Enables code that detects non-thread safe access to network data */
// 检测「网络数据被非预期跨线程访问」的额外校验代码（线程检查、原子序计数器等）。
// 默认跟随 DO_CHECK：Dev/Editor=1，Shipping=0。
#ifndef UE_NET_THREAD_SAFETY_CHECK
#	define UE_NET_THREAD_SAFETY_CHECK DO_CHECK
#endif

/** Enables code to simulate async loading stalls on specific objects on the client */
// 在客户端为特定对象「模拟异步加载阻塞」的调试支持。
// 仅在非 Shipping/Test 启用——研发期用于复现「加载未完成期间收到对象更新」
// 这类时序边界 bug。Shipping 关闭，避免任何运行时分支。
#ifndef UE_NET_ASYNCLOADING_DEBUG
#	define UE_NET_ASYNCLOADING_DEBUG !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif