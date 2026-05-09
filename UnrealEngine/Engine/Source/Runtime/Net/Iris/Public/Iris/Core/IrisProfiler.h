// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "ProfilingDebugging/CsvProfiler.h"

// ============================================================================
// IrisProfiler —— Iris 的 CPU/协议/客户端剖析宏总入口
// ----------------------------------------------------------------------------
// 提供 3 组能力（所有宏在禁用编译下都会退化成空，不留运行期开销）：
//
//   1) 通用作用域计时：
//      IRIS_PROFILER_SCOPE / _CONDITIONAL / _TEXT / _TEXT_CONDITIONAL
//      - 默认路径：走 UE 的 `CpuProfilerTrace`（TRACE_CPUPROFILER_EVENT_SCOPE）
//        —— 可以在 Unreal Insights 里看到；
//      - 如果定义了 IRIS_USE_SUPERLUMINAL，则路由到 Superluminal Performance API
//        —— 商业 CPU 剖析器，提供函数级 inline 采样。
//
//   2) 协议名标记：
//      IRIS_PROFILER_PROTOCOL_NAME(X)
//      - 在 profile 录制时，把"当前处理的协议名"作为一个文本 scope 注入，
//        以便在 trace 上直接看到是哪种 protocol 花了时间；
//      - 开销：只在"正在 trace 录制"时有少量 CPU 代价，不录制就几乎 0 开销，
//        但仍然被预处理保护：Shipping 默认关闭，避免 Shipping 出现任何
//        额外字符串化代码。
//
//   3) Verbose 级 CPU scope：
//      IRIS_PROFILER_SCOPE_VERBOSE / _CONDITIONAL
//      - 用于"逐对象级、逐字段级"的细粒度计时；Shipping/Test 里强制关闭，
//        Dev 里默认开启。
//
//   4) 客户端 CSV profiler（FClientProfiler）：
//      在"客户端 + 启用 CSV"的构建里可用，按 FName 记录 ObjectCreate /
//      RepNotify / RPC 耗时，以及"被 AsyncLoading 等阻塞的 batch 时间"。
//      由 net.Iris.EnableDetailedClientProfiler CVar 动态开关。
//
// 多条路径总览：
//     路径                              | 条件                                 | 作用
//     --------------------------------- | ------------------------------------ | ------------------------------
//     Superluminal                      | IRIS_PROFILER_ENABLE + IRIS_USE_SUPERLUMINAL | 商业采样剖析
//     CpuProfilerTrace (Unreal Insights)| IRIS_PROFILER_ENABLE + !IRIS_USE_SUPERLUMINAL | UE 默认 trace
//     空宏                              | !IRIS_PROFILER_ENABLE (Shipping 默认) | 零开销
//     CSV（IrisCsv.h）                  | UE_NET_IRIS_CSV_STATS                | CSV 列聚合
//     Client CSV（FClientProfiler）     | IRIS_CLIENT_PROFILER_ENABLE          | 客户端细项 RPC/RepNotify 行情
// ============================================================================

// ----------------------------------------------------------------------------
// IRIS_PROFILER_ENABLE：总开关。Shipping 默认关闭，开发构建默认开启。
// 可由 Build.cs 或命令行预先 define 强制覆盖。
// ----------------------------------------------------------------------------
#ifndef IRIS_PROFILER_ENABLE
#	if (UE_BUILD_SHIPPING)
#		define IRIS_PROFILER_ENABLE 0
#	else
#		define IRIS_PROFILER_ENABLE 1
#	endif
#endif

// When true this adds dynamic protocol names in profile captures. The downside is a noticeable cpu cost overhead but only while cpu trace recording is occurring.
// 当为 true 时，剖析录制期间会为"每种协议"插入动态命名的 scope（便于区分不同 protocol）；
// 代价是录制时有可察觉的 CPU 开销，不录制则接近 0。默认：非 Shipping 下开启。
#ifndef UE_IRIS_PROFILER_ENABLE_PROTOCOL_NAMES
#	define UE_IRIS_PROFILER_ENABLE_PROTOCOL_NAMES !UE_BUILD_SHIPPING
#endif

// When true this adds low-level cpu trace captures of operations in Iris. Adds a little cpu overhead but only while cpu trace recording is occurring.
// 当为 true 时允许超细粒度（per-object/per-field）的 CPU scope；在 Shipping/Test 禁用。
#ifndef UE_IRIS_PROFILER_ENABLE_VERBOSE
#	if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#		define UE_IRIS_PROFILER_ENABLE_VERBOSE 0
#	else
#		define UE_IRIS_PROFILER_ENABLE_VERBOSE 1
#	endif
#endif


// 如需接入 Superluminal，取消下一行注释（或在 Build.cs 预定义）。
// 定义后 IRIS_PROFILER_SCOPE 会改走 Superluminal 的 Performance API，
// 并自动链接其 lib（参见下方 #pragma comment）。
//#define IRIS_USE_SUPERLUMINAL

// ----------------------------------------------------------------------------
// 分支一：总开关打开
// ----------------------------------------------------------------------------
#if IRIS_PROFILER_ENABLE
#	ifdef IRIS_USE_SUPERLUMINAL
		// -- Superluminal 分支：
		//    · 直接包含其头文件与静态库路径（写死 c:\Program Files 下安装的默认路径，
		//      如机器没装会编译失败）；
		//    · PREPROCESSOR_TO_STRING(X) 把宏参数原样字符串化作为 scope 名；
		//    · _TEXT 变体用 __LINE__ 生成唯一变量名 + 运行期字符串。
#		include "c:/Program Files/Superluminal/Performance/API/include/Superluminal/PerformanceAPI.h"
#		include "HAL/PreprocessorHelpers.h"
#		pragma comment (lib, "c:/Program Files/Superluminal/Performance/API/lib/x64/PerformanceAPI_MD.lib")
#		define IRIS_PROFILER_SCOPE(X) PERFORMANCEAPI_INSTRUMENT(PREPROCESSOR_TO_STRING(X))
#		define IRIS_PROFILER_SCOPE_CONDITIONAL(X,Cond) PERFORMANCEAPI_INSTRUMENT(PREPROCESSOR_TO_STRING(X))
#		define IRIS_PROFILER_SCOPE_TEXT(X) PERFORMANCEAPI_INSTRUMENT_DATA(PREPROCESSOR_JOIN(IrisProfilerScope, __LINE__), X)
#		define IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(X, Cond) PERFORMANCEAPI_INSTRUMENT_DATA(PREPROCESSOR_JOIN(IrisProfilerScope, __LINE__), X)
#	else
		// -- 默认分支：走 UE CpuProfilerTrace（Unreal Insights）。
		//    CPU Trace 宏本身在没有开启 trace 时接近 0 开销（仅条件跳转）。
#		include "ProfilingDebugging/CpuProfilerTrace.h"
#		define IRIS_PROFILER_SCOPE(X) TRACE_CPUPROFILER_EVENT_SCOPE(X)
#		define IRIS_PROFILER_SCOPE_CONDITIONAL(X,Cond) TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(X, Cond)
#		define IRIS_PROFILER_SCOPE_TEXT(X) TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(X)
#		define IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(X, Cond) TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_CONDITIONAL(X, Cond)
#	endif
#else
	// -- 分支二：总开关关闭（Shipping 默认）
	//    所有宏展开为空；同时把 Superluminal 自身的启用宏强制置 0，
	//    确保"他们宏里基于 PERFORMANCEAPI_ENABLED 的分支"也归零。
#	define PERFORMANCEAPI_ENABLED 0
#	define IRIS_PROFILER_SCOPE(X)
#	define IRIS_PROFILER_SCOPE_CONDITIONAL(X, Cond)
#	define IRIS_PROFILER_SCOPE_TEXT(X)
#	define IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(X, Cond)
#endif

// ----------------------------------------------------------------------------
// 协议名字标记：只在启用"动态协议名"时展开为文本 scope，否则消失。
// 用法：在每种协议入口处 `IRIS_PROFILER_PROTOCOL_NAME(ProtocolName);`，
//      这样 trace 上能一眼看出花在哪个协议上。
// ----------------------------------------------------------------------------
#if UE_IRIS_PROFILER_ENABLE_PROTOCOL_NAMES
#	define IRIS_PROFILER_PROTOCOL_NAME(X) IRIS_PROFILER_SCOPE_TEXT(X)
#	define IRIS_PROFILER_PROTOCOL_NAME_CONDITIONAL(X, Cond) IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(X, Cond)
#else
#	define IRIS_PROFILER_PROTOCOL_NAME(X)
#	define IRIS_PROFILER_PROTOCOL_NAME_CONDITIONAL(X, Cond)
#endif

// ----------------------------------------------------------------------------
// Verbose 级 scope：Shipping/Test 强制关闭，避免大量细粒度 scope 带来的开销。
// ----------------------------------------------------------------------------
#if UE_IRIS_PROFILER_ENABLE_VERBOSE
#	define IRIS_PROFILER_SCOPE_VERBOSE(X) IRIS_PROFILER_SCOPE(X);
#	define IRIS_PROFILER_SCOPE_VERBOSE_CONDITIONAL(X, Cond) IRIS_PROFILER_SCOPE_CONDITIONAL(X, Cond);
#else
#	define IRIS_PROFILER_SCOPE_VERBOSE(X)
#	define IRIS_PROFILER_SCOPE_VERBOSE_CONDITIONAL(X, Cond)
#endif

// ----------------------------------------------------------------------------
// 客户端细项 Profiler 启用条件：
//   - 仅客户端构建（!WITH_SERVER_CODE）；
//   - 且 CSV_PROFILER_STATS 已启用（否则根本没 CSV 记录能力）。
// 该开关控制 FClientProfiler 类是否存在于编译单元中——关闭时所有调用代码
// 需要自行用 `#if IRIS_CLIENT_PROFILER_ENABLE` 包围，或使用 ScopedXxx RAII
// 包装（实现细节在 .cpp 中同样做了条件编译）。
// ----------------------------------------------------------------------------
#ifndef IRIS_CLIENT_PROFILER_ENABLE
#	define IRIS_CLIENT_PROFILER_ENABLE (!WITH_SERVER_CODE && CSV_PROFILER_STATS)
#endif

namespace UE::Net
{

#if IRIS_CLIENT_PROFILER_ENABLE

/**
 * 客户端细项剖析器（仅客户端 + CSV 启用）。
 *
 * 负责收集客户端"由网络触发的细项操作"的 CSV 数据：
 *   - ObjectCreate：复制创建一个新对象（RootObject/SubObject）；
 *   - RepNotify：触发 UFUNCTION(OnRep_XXX) 的耗时；
 *   - RPC：接收到 RPC 的调度/执行耗时；
 *   - BlockedReplication：当一批复制数据因为资源 async load 而卡住时的
 *     "被阻塞时长 + 阻塞的对象名 + 待加载资源数量"。
 *
 * 运行时开关：`net.Iris.EnableDetailedClientProfiler` CVar（详见 .cpp）。
 *
 * 所有 Record* 为静态方法，方便在任意调用点直接调用；内部按 CSV 类别
 * `IrisClient` 写入。实现上通常有对应的 RAII 包装类（如 ScopedObjectCreate）
 * 在高频路径上使用。
 */
class FClientProfiler
{
public:

	/** Record profiler events. */
	/** 记录一次对象创建事件。bIsSubObject 用于区分 Root 与 SubObject 的统计列。 */
	static void RecordObjectCreate(FName ObjectName, bool bIsSubObject);
	/** 记录一次 RepNotify 执行耗时（UFUNCTION 名） */
	static void RecordRepNotify(FName RepNotifyName);
	/** 记录一次 RPC 调度/执行耗时（UFUNCTION 名） */
	static void RecordRPC(FName RPCName);

	/**
	 * 记录一次"因 async loading 阻塞而等待"的复制 batch。
	 * @param BlockedObject    被阻塞的主对象名（调试标识）；
	 * @param NumBlockedAssets 当前还有多少待加载资产；
	 * @param BlockedTime      累计阻塞时长（秒）。
	 */
	static void RecordBlockedReplication(const TCHAR* BlockedObject, int32 NumBlockedAssets, float BlockedTime);

	/** Return true if capturing events. */
	/** 是否正在采集。由 CVar + CSV 采集状态共同决定；用于跳过昂贵的调用点。 */
	static bool IsCapturing();
};

#endif

}
