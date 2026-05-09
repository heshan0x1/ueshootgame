// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// IrisLog.cpp —— Iris 框架的日志分类（Log Category）定义文件
// ----------------------------------------------------------------------------
// 职责：仅负责把头文件 Public/Iris/Core/IrisLog.h 中用
//   DECLARE_LOG_CATEGORY_EXTERN(Name, DefaultVerbosity, CompileTimeVerbosity)
// 声明的三条 Iris 专用日志类别，在链接期真正落地为全局符号。
//
// 三条类别一览（默认 Verbosity = Log，编译期最高 Verbosity = All）：
//   * LogIris           ：Iris 框架的主日志通道，几乎所有模块通用；
//   * LogIrisFiltering  ：Filtering 子系统专用（Scope / Exclusion / Dynamic
//                         Filter / Inclusion / Hysteresis / SubObjectFilter
//                         相关日志汇聚到此处，避免淹没主通道）；
//   * LogIrisNetCull    ：NetCull（基于距离/视锥的剔除）专用，便于在调试
//                         大场景时单独打开而不影响其他日志量。
//
// 使用方式：上层源码通过 UE_LOG(LogIris, Log, TEXT("...")) 写入；运行期可用
//   log LogIris VeryVerbose / log LogIrisFiltering Verbose / log LogIrisNetCull Log
// 调整各自的 Verbosity 级别。
//
// 线程安全：DEFINE_LOG_CATEGORY 生成的是静态 FLogCategoryBase 实例，UE 日志
// 系统自身内部做了线程同步，此处无需额外处理。
// ============================================================================

#include "Iris/Core/IrisLog.h"

/**
 * Iris 主日志通道。
 * 默认 Verbosity：Log（普通日志，Release 版本也会输出）。
 * 编译期最高 Verbosity：All（允许开发版本临时开到 VeryVerbose 调试）。
 * 覆盖范围：ReplicationSystem / Serialization / DataStream / ReplicationState
 * 等绝大多数 Iris 代码的缺省日志去向。
 */
DEFINE_LOG_CATEGORY(LogIris);

/**
 * Filtering 子系统专用日志通道。
 * 运行时可独立开关，避免 Scope 计算每帧都往主通道刷屏。
 * 典型消息：对象何时进入/离开某个连接的 Scope、Hysteresis 延迟移除、
 * SubObjectFilter 决策结果、Dynamic Filter（UNetObjectFilter 子类）回调等。
 */
DEFINE_LOG_CATEGORY(LogIrisFiltering);

/**
 * NetCull（网络剔除）日志通道。
 * 专用于基于距离 / FOV / 球体等几何关系的可见性裁剪输出，常与
 * UNetObjectCountLimiterPrioritizer / Sphere / FOV Filter 等一起使用。
 * 单独成一路，方便在大世界压测中临时开启诊断而不污染 LogIris。
 */
DEFINE_LOG_CATEGORY(LogIrisNetCull);
