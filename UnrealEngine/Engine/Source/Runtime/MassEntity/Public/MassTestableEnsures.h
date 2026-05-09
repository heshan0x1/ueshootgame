// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// -----------------------------------------------------------------------------
// 测试友好的 ensure/check 宏封装
// -----------------------------------------------------------------------------
// 作用：
//   引擎自带的 ensureMsgf / checkf 在触发时行为是"弹断言窗 + 写日志"。
//   但单元测试（尤其是 AITestSuite 框架下的自动化测试）需要能"捕获"这些
//   断言失败，把它们转成测试用例的 fail，而不是让进程崩溃。
//
// 解决方案：
//   * 当工程编译了 AITestSuite 时（定义了 WITH_AITESTSUITE），本头文件会转发
//     到 "TestableEnsures.h"，那里提供了可被测试框架拦截的版本 testableEnsureMsgf
//     / testableCheckf / testableCheckfReturn。
//   * 在普通运行时构建下（未定义 WITH_AITESTSUITE），这些 testable* 符号
//     被降级为标准 ensureMsgf / checkf，行为完全一致、零开销。
//
// 因此，MassEntity 内部凡是"希望断言能被测试拦截"的地方都应改用 testable*
// 版本，而不是直接用 ensure / check。
#ifdef WITH_AITESTSUITE
#include "TestableEnsures.h"
#else
// 在非测试构建下，testableEnsureMsgf 直接等价于 ensureMsgf。
#define testableEnsureMsgf ensureMsgf
// 在非测试构建下，testableCheckf 直接等价于 checkf。
#define testableCheckf checkf
// testableCheckfReturn：带返回值版本的 check。
// 在测试框架中：断言失败后通过 return 返回一个用户提供的 ReturnValue，
//   从而让测试用例能继续跑而不崩溃。
// 在普通构建中：退化为标准 checkf，失败会直接中止，ReturnValue 被忽略。
#define testableCheckfReturn(InExpression, ReturnValue, InFormat, ... ) checkf(InExpression, InFormat, ##__VA_ARGS__)
#endif 
