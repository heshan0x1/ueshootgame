// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

// ============================================================================
// Iris 框架日志类别（Log Category）声明
// ----------------------------------------------------------------------------
// 所在分层：Core 模块（L1 基础设施层）。
// 这里一共对外声明了 3 条 UE Log Category，按"关注点"进行分工，便于在运行
// 期通过控制台命令（如 `Log LogIrisFiltering Verbose`）单独开启/关闭，
// 避免"一开 Iris 日志就整个屏幕刷屏"的问题：
//   1. LogIris           —— 通用 Iris 日志：模块生命周期、ReplicationSystem、
//                            Protocol、Serializer、错误/警告等。
//   2. LogIrisFiltering  —— Scope/Filter 子系统专用：Exclusion / Dynamic /
//                            Inclusion Filter、NetObjectGroup、Hysteresis
//                            等相关的调试输出，调优可见性/裁剪规则时使用。
//   3. LogIrisNetCull    —— 视距/空间裁剪（NetCull）专用：WorldLocations、
//                            SphereFilter、距离阈值等剔除行为的追踪日志。
//
// DECLARE_LOG_CATEGORY_EXTERN 的参数语义：
//   <Name, DefaultVerbosity, CompileTimeVerbosity>
//   - DefaultVerbosity=Log：默认输出 Log 及以上级别；
//   - CompileTimeVerbosity=All：编译期保留所有级别（Verbose/VeryVerbose
//     在 Shipping 里也可打开，不会被预处理裁剪），便于线上排障。
// ============================================================================

/** 通用 Iris 日志类别：模块启动、ReplicationSystem 创建/销毁、协议/序列化错误等统一走这里。 */
IRISCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIris, Log, All);

/** 过滤子系统专用日志：用于排查 Scope/Filter（Exclusion/Dynamic/Inclusion/Group）相关问题。 */
IRISCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIrisFiltering, Log, All);

/** 空间视距剔除专用日志：WorldLocations 更新、距离阈值、SphereFilter 命中情况等。 */
IRISCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIrisNetCull, Log, All);
