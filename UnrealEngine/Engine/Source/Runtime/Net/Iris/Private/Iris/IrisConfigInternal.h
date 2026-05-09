// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// IrisConfigInternal.h —— Iris 框架内部的「编译期开关」集合
// -----------------------------------------------------------------------------
// 本文件位于 Private/，仅供 Iris 内部 .cpp 使用，对外不可见。其作用：
//   * 将「调试 / 校验 / 日志」相关的编译期开关集中宏化；
//   * 按 Build Configuration（Shipping / Test / Dev）自动调整默认值；
//   * 允许外部构建脚本通过预先 #define 来覆盖默认值（每个宏外都包了 #ifndef）。
//
// 与 Public/Iris/IrisConfig.h 的区别：
//   * IrisConfig.h 暴露面向「调用方」的开关（NetBitStream 校验 / CSV 统计
//     等），需要随对外头文件分发；
//   * IrisConfigInternal.h 仅承载「实现细节」开关，比如 ReplicationRecord 校验、
//     DeltaCompression Baseline 校验等，外部消费者无需知晓。
//
// 一致约定：
//   * 所有开关名以 UE_NET_ENABLE_xxx / UE_NET_VALIDATE_xxx 形式命名；
//   * Shipping/Test 默认全部关闭以减小开销；Development/Editor 默认开启
//     校验，便于早期发现错误。
// =============================================================================

#pragma once

#include "Iris/IrisConfig.h"

// -----------------------------------------------------------------------------
// 日志开关：ReplicationReader / ReplicationWriter
// -----------------------------------------------------------------------------
// ReplicationReader/Writer 在 ReceivePacket / SendPacket 路径中可能产生海量
// VERBOSE 日志，默认全配置都关闭（即便 Dev 也是 0）。如确需排查，可在外部
// 显式 #define UE_NET_ENABLE_REPLICATIONREADER_LOG=1 进行覆盖。

// Log config for Iris subsystems
#ifndef UE_NET_ENABLE_REPLICATIONREADER_LOG
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#	define UE_NET_ENABLE_REPLICATIONREADER_LOG 0
#else
#	define UE_NET_ENABLE_REPLICATIONREADER_LOG 0
#endif
#endif


#ifndef UE_NET_ENABLE_REPLICATIONWRITER_LOG
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#	define UE_NET_ENABLE_REPLICATIONWRITER_LOG 0
#else
#	define UE_NET_ENABLE_REPLICATIONWRITER_LOG 0
#endif
#endif

// -----------------------------------------------------------------------------
// 校验开关：ReplicationRecord / DeltaCompression Baseline
// -----------------------------------------------------------------------------
// ReplicationRecord 记录每个发出 Packet 的复制内容，供 ACK / NAK 时回放；
// 校验开关在 Dev 默认开，用于在 ReceiveAck/Nak 路径上做一致性 ensure。
// Shipping/Test 默认关闭以避免运行时校验代价。

// Misc config
#ifndef UE_NET_VALIDATE_REPLICATION_RECORD
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#	define UE_NET_VALIDATE_REPLICATION_RECORD 0
#else
#	define UE_NET_VALIDATE_REPLICATION_RECORD 1
#endif 
#endif

// DeltaCompression Baseline 校验：Dev 下确保 baseline 状态在创建/确认/移除
// 流程中保持一致；Shipping 关闭以减小开销。
#ifndef UE_NET_VALIDATE_DC_BASELINES
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#	define UE_NET_VALIDATE_DC_BASELINES 0
#else
#	define UE_NET_VALIDATE_DC_BASELINES 1
#endif 
#endif

// -----------------------------------------------------------------------------
// ReplicationWriter 的「无法发送」告警
// -----------------------------------------------------------------------------
// 当对象被标记 Dirty 但因带宽 / 优先级原因连续多帧无法发送时，发出 warning。
// 此项在所有配置下默认开启（包含 Shipping），因为它对调试线上「卡帧 / 漏发」
// 类问题非常关键，且只在异常路径触发。

#ifndef UE_NET_ENABLE_REPLICATIONWRITER_CANNOT_SEND_WARNING
#if (UE_BUILD_SHIPPING)
#	define UE_NET_ENABLE_REPLICATIONWRITER_CANNOT_SEND_WARNING 1
#else
#	define UE_NET_ENABLE_REPLICATIONWRITER_CANNOT_SEND_WARNING 1
#endif 
#endif

// -----------------------------------------------------------------------------
// Polling 详细 Profiling
// -----------------------------------------------------------------------------
// Polling 子系统按帧扫描所有非推式 Dirty 对象；开启此项后 Profiler 会拆出
// 子分类计数（按 Class / 按 Owner 等），方便 Dev 时优化扫描成本。
// Shipping 关闭，避免大量 Stat scope 进入热路径。

#ifndef UE_NET_WITH_VERBOSE_POLL_PROFILING
#   define UE_NET_WITH_VERBOSE_POLL_PROFILING (!UE_BUILD_SHIPPING)
#endif
