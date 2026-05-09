// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/CsvProfiler.h"
#include "Iris/IrisConfig.h"
#include "Iris/Core/IrisProfiler.h"

// ============================================================================
// Iris CSV Profiler 集成
// ----------------------------------------------------------------------------
// 目的：
//   - UE 的 CSV Profiler 按 "Category" 组织列；这里声明一个名为 "Iris" 的
//     CSV category（跨模块使用，因此用 CSV_DECLARE_CATEGORY_MODULE_EXTERN
//     带 IRISCORE_API 导出）；
//   - 提供宏 IRIS_CSV_PROFILER_SCOPE 把"CSV 计时 + CPU Trace 计时"两种
//     计时来源合二为一，避免业务代码到处写重复的 CSV_SCOPED_TIMING_STAT
//     + IRIS_PROFILER_SCOPE 组合。
//
// 与 IRIS_PROFILER_SCOPE 的差异：
//   - IRIS_PROFILER_SCOPE(Name)              —— 仅输出到 CPU 剖析
//     （CpuProfilerTrace 或 Superluminal），粒度细，不落入 CSV 行。
//   - IRIS_CSV_PROFILER_SCOPE(Cat, Name)     —— 同时：
//       a) 向 CSV Profiler 的 `Cat` 列累加耗时（宏观、每帧一行）；
//       b) 向 CPU Trace 追加一个同名 scope（微观、支持火焰图）。
//     因此适合那些"需要在 CSV 里单独成列做趋势监控"的热点，例如
//     Iris 主 Tick 的关键阶段：Filtering、Prioritization、Write 等。
// ============================================================================

/**
 * 对外导出的 CSV Profiler 类别 "Iris"。
 * 使用 CSV_DECLARE_CATEGORY_MODULE_EXTERN 确保跨模块引用时不会产生
 * 重复定义；实际 definition 位于 IrisProfiler.cpp。
 */
CSV_DECLARE_CATEGORY_MODULE_EXTERN(IRISCORE_API, Iris);

// ------------------------------------------------------------------
// 条件编译路径说明：
//   UE_NET_IRIS_CSV_STATS（定义在 Iris/IrisConfig.h）控制是否真的把 CSV
//   计时项展开。在 Shipping/测试禁用 CSV 的构建中，该宏展开为空，以保证
//   零额外开销；开发构建里则同时向 CSV 与 CPU Trace 写入。
// ------------------------------------------------------------------
#if UE_NET_IRIS_CSV_STATS
	/**
	 * 同时记录 CSV + CPU Trace 的作用域计时宏。
	 * @param CsvCategory CSV 类别标识（如 Iris）；
	 * @param x           作用域名字（推荐使用无空格常量标识，便于在
	 *                    CSV 列头和 Trace Event 名里保持一致）。
	 */
	#define IRIS_CSV_PROFILER_SCOPE(CsvCategory, x) \
	CSV_SCOPED_TIMING_STAT(CsvCategory, x); \
	IRIS_PROFILER_SCOPE(x)
#else
	/**
	 * 当 CSV 统计被编译期关闭时，仅保留 CPU Trace 的计时 scope，
	 * 语义仍然可用但不再产出 CSV 列。
	 */
	#define IRIS_CSV_PROFILER_SCOPE(CsvCategory, x) IRIS_PROFILER_SCOPE(x)
#endif

