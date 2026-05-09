// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// 依赖 StructUtils 提供的基础宏（如 WITH_STRUCTUTILS_DEBUG 等）。
// MassEntity 框架本身是建立在 StructUtils（UStruct 动态视图/容器）之上的，
// 因此 MassEntity 的调试开关也会联动 StructUtils 的调试开关。
#include "StructUtils/StructUtilsMacros.h"

// -----------------------------------------------------------------------------
// WITH_MASSENTITY_DEBUG 宏
// -----------------------------------------------------------------------------
// 作用：MassEntity 模块范围内的"调试支持"总开关。
// 只有在该宏为 1 时，以下类型的代码才会被编译进来：
//   * 实体/Archetype/Processor 的 Debugger 面板和可视化日志（VisualLogger）接入
//   * 断言/诊断信息、额外的名字字符串、统计桩点
//   * MassDebugger、FMassDebugLogFragment 等调试专用数据结构
//
// 生效条件（三者都为真才为 1）：
//   1) 不是 Shipping / ShippingWithEditor / Test 构建（即 Debug、Development 等）
//   2) StructUtils 的调试开关已打开（WITH_STRUCTUTILS_DEBUG）
//   3) 最后那个 "&& 1" 是预留的人工总开关，方便本地临时关闭调试而不改前两项
//
// 使用方式：外部可以先 #define WITH_MASSENTITY_DEBUG 0 再包含本文件，强制关闭。
#ifndef WITH_MASSENTITY_DEBUG
#define WITH_MASSENTITY_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && WITH_STRUCTUTILS_DEBUG && 1)
#endif
