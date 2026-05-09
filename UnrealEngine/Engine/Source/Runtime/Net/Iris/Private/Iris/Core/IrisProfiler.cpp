// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// IrisProfiler.cpp —— Iris 客户端 CSV 性能剖析器实现
// ----------------------------------------------------------------------------
// 职责：
//   1) 定义 5 个 CSV Profiler category，Iris 专用：
//        * IrisClient                       —— 客户端总览（CallCountRPC / RepNotifyCount / ObjectCreate …）
//        * IrisClientDetailObjectCreate     —— 按类名分列的对象创建计数
//        * IrisClientDetailRepNotify        —— 按 RepNotify 名分列的触发次数
//        * IrisClientDetailRPC              —— 按 RPC 名分列的调用次数
//        * IrisClientBlockedByAsyncLoading  —— 因 async loading 阻塞的事件
//   2) 暴露一个 CVar：net.Iris.EnableDetailedClientProfiler，用于在运行期开启
//      按名细分的三路 detail 统计；未开启时只记录主 IrisClient 汇总列，
//      降低 profile capture 开销。
//   3) 提供 FClientProfiler 静态方法，由 ReplicationSystem / ObjectReplicationBridge
//      在客户端创建对象、执行 RepNotify、执行 RPC、被异步加载阻塞等关键点调用，
//      把计数以 ECsvCustomStatOp::Accumulate 方式累加到 CSV Profiler。
//
// 编译期门控：
//   * IRIS_PROFILER_ENABLE          （Shipping 默认关，其余默认开）—— 控制整套 profiler 宏是否生效。
//   * IRIS_CLIENT_PROFILER_ENABLE   （!WITH_SERVER_CODE && CSV_PROFILER_STATS）—— 仅纯客户端 build 开启。
//   * IRIS_CLIENT_PROFILER_DETAILED （!UE_BUILD_SHIPPING）—— 控制 detail 三路 category 的编译是否生成。
// 本文件中 FClientProfiler 的所有方法均被包裹在 #if IRIS_CLIENT_PROFILER_ENABLE 内，
// 若服务端或 Shipping 则整块空编译，避免 CSV 宏在运行时产生开销。
//
// 生命周期（RAII 配对）：
//   本 .cpp 只提供"RecordXxx"的计数型 API；头文件中还有基于这些 API 的 RAII 
//   小工具（ScopedObjectCreate / ScopedRepNotify / ScopedRPC 等）负责 Start/Stop
//   配对——但注意：当前版本仅有计数（Accumulate），并未包含 scope 时间统计；
//   "ScopedXxx"名字来源于 Core.md 的描述，实际 public API 中保留为未来扩展点。
//   上层只需调用 FClientProfiler::RecordRPC(Name) 一次即可记录一次 RPC 调用。
//
// 线程安全：FCsvProfiler::RecordCustomStat / RecordEventf 由 UE 的 CsvProfiler
// 系统内部实现线程安全的 accumulate，因此本文件所有方法可从任意线程调用。
// ============================================================================

#include "Iris/Core/IrisProfiler.h"
#include "HAL/IConsoleManager.h"

// -----------------------------------------------------------------------------
// CSV Profiler Category 定义
//
// CSV_DEFINE_CATEGORY(CategoryName, bDefaultEnabled)：
//   - CategoryName    ：与 CSV_CATEGORY_INDEX(CategoryName) 宏配对使用；
//   - bDefaultEnabled ：编译期决定该 category 的记录条件——这里使用
//                       IRIS_CLIENT_PROFILER_ENABLE（纯客户端 build + 开启
//                       CSV profiler stats 时为 1，否则为 0），从而让服务端
//                       build / Shipping 完全不生成相关统计列。
//
// 运行期可通过 `csvCategory <Name> 0/1` 控制台命令在 capture 时启用/禁用
// 某个 category，单独关闭过于细粒度的列以压缩 CSV 体积。
// -----------------------------------------------------------------------------

/** 客户端 Iris 汇总 category：ClientObjectCreate / ClientObjectCreateRoot / CallCountRPC / RepNotifyCount 等汇总列。 */
CSV_DEFINE_CATEGORY(IrisClient, IRIS_CLIENT_PROFILER_ENABLE);
/** 细分 category：按"对象类名"记录根对象创建次数；开启 CVar 后才写入。 */
CSV_DEFINE_CATEGORY(IrisClientDetailObjectCreate, IRIS_CLIENT_PROFILER_ENABLE);
/** 细分 category：按"RepNotify 名字"记录触发次数；开启 CVar 后才写入。 */
CSV_DEFINE_CATEGORY(IrisClientDetailRepNotify, IRIS_CLIENT_PROFILER_ENABLE);
/** 细分 category：按"RPC 名字"记录调用次数；开启 CVar 后才写入。 */
CSV_DEFINE_CATEGORY(IrisClientDetailRPC, IRIS_CLIENT_PROFILER_ENABLE);
/** Async loading 阻塞事件 category：以 CSV event 形式记录"某对象被异步加载阻塞多久"。 */
CSV_DEFINE_CATEGORY(IrisClientBlockedByAsyncLoading, IRIS_CLIENT_PROFILER_ENABLE);

// Detailed CSV stats are disabled in shipping by default.
// 细分统计在 Shipping 版默认关闭：Shipping 构建中 detail 代码路径整块不编译，
// 避免 FName → string 拼接等细节开销混入正式版本。
#ifndef IRIS_CLIENT_PROFILER_DETAILED
#	define IRIS_CLIENT_PROFILER_DETAILED (!UE_BUILD_SHIPPING)
#endif

namespace UE::Net::Private
{

#if IRIS_CLIENT_PROFILER_DETAILED
/**
 * 控制 Detail 级 CSV 统计的总开关。
 *
 * 作用：
 *   - false（默认）：Record* 仅写汇总 category（IrisClient），不展开到名字列；
 *   - true         ：Record* 同时把"按 FName 单独成列"的数据写入 detail 三路 category。
 *
 * 修改影响：
 *   * 开启瞬间生效，下一次 RecordObjectCreate / RecordRepNotify / RecordRPC 调用
 *     会立刻开始产生细分列；已产生的 CSV 行不受影响。
 *   * 若 FName 种类很多（例如全项目 RPC 名），detail 列数会爆炸，显著增大
 *     CSV 体积与抓取时的性能开销。典型用法是"临时开启抓几秒→再关闭"。
 *
 * 默认值：0（关闭）。标志类型为 bool，但原代码以 int 字面量 0 初始化保持习惯兼容。
 */
static bool bEnableDetailedClientProfiler = 0;

/**
 * CVar：net.Iris.EnableDetailedClientProfiler
 * - 用途：运行期切换上面 bEnableDetailedClientProfiler；
 * - 典型命令：`net.Iris.EnableDetailedClientProfiler 1`（开启）/ `0`（关闭）；
 * - Flags：ECVF_Default（允许从命令行 / ini / 控制台修改，无特殊保护）；
 * - 仅在 !UE_BUILD_SHIPPING 下存在（受上面 #if 门控）。
 */
static FAutoConsoleVariableRef CVarEnableDetailedClientProfilerRef(TEXT("net.Iris.EnableDetailedClientProfiler"), bEnableDetailedClientProfiler, TEXT("Generates detailed CSV Iris stats (client only)."), ECVF_Default );
#endif

}

namespace UE::Net
{

#if IRIS_CLIENT_PROFILER_ENABLE

/**
 * 记录一次客户端"对象创建"事件。
 *
 * 调用路径：ReplicationReader / UObjectReplicationBridge 在处理 CreationHeader
 * 并成功生成 UObject 实例后调用，用于统计客户端每秒接收 spawn 的速率。
 *
 * @param ObjectName     该对象的类名或资源路径名（仅在 detail 模式下作为 CSV 列名）。
 * @param bIsSubObject   true 表示属于某个根对象下的 SubObject（Component/附着物），
 *                       false 表示独立复制的根对象。根对象会额外写入
 *                       ClientObjectCreateRoot 列，方便与总创建数区分。
 *
 * 记录方式：ECsvCustomStatOp::Accumulate —— 同一帧多次调用累加到同一个列。
 */
void FClientProfiler::RecordObjectCreate(FName ObjectName, bool bIsSubObject)
{
	// 汇总列：任何类型的对象创建都计入 ClientObjectCreate。
	FCsvProfiler::RecordCustomStat("ClientObjectCreate", CSV_CATEGORY_INDEX(IrisClient), 1, ECsvCustomStatOp::Accumulate);
	// 分支：只有根对象计入 ClientObjectCreateRoot；便于与 SubObject 分开观察。
	if (!bIsSubObject)
	{
		FCsvProfiler::RecordCustomStat("ClientObjectCreateRoot", CSV_CATEGORY_INDEX(IrisClient), 1, ECsvCustomStatOp::Accumulate);
	}

#if IRIS_CLIENT_PROFILER_DETAILED
	// 细分模式：仅当 CVar 打开时产生按类名的分列，避免默认情况下 CSV 列数爆炸。
	if (UE::Net::Private::bEnableDetailedClientProfiler)
	{
		// 只对根对象细分；SubObject 由其根对象聚合。
		if (!bIsSubObject)
		{
			FCsvProfiler::RecordCustomStat(ObjectName, CSV_CATEGORY_INDEX(IrisClientDetailObjectCreate), 1, ECsvCustomStatOp::Accumulate);
		}
	}
#endif
}

/**
 * 记录一次客户端 RepNotify 回调触发。
 *
 * 调用路径：ReplicationReader → DequantizeAndApplyHelper 在应用完差异后、
 * 调用 UFunction RepNotify 前插桩。
 *
 * @param RepNotifyName  被触发的 RepNotify 函数名；detail 模式下会以此作为列名。
 */
void FClientProfiler::RecordRepNotify(FName RepNotifyName)
{
	// 总 RepNotify 触发次数。
	FCsvProfiler::RecordCustomStat("RepNotifyCount", CSV_CATEGORY_INDEX(IrisClient), 1, ECsvCustomStatOp::Accumulate);

#if IRIS_CLIENT_PROFILER_DETAILED
	// 细分模式下按函数名单独成列。
	if (UE::Net::Private::bEnableDetailedClientProfiler)
	{
		FCsvProfiler::RecordCustomStat(RepNotifyName, CSV_CATEGORY_INDEX(IrisClientDetailRepNotify), 1, ECsvCustomStatOp::Accumulate);
	}
#endif
}

/**
 * 记录一次客户端 RPC 调用（实际触发 UFunction 执行前统计）。
 *
 * 调用路径：AttachmentReplication / NetBlobHandler 分发 RPC Blob，在调用
 * ProcessFunction 前打点。
 *
 * @param RPCName  被调用的 UFunction 名；detail 模式下用作列名。
 */
void FClientProfiler::RecordRPC(FName RPCName)
{
	// 总 RPC 调用计数。
	FCsvProfiler::RecordCustomStat("CallCountRPC", CSV_CATEGORY_INDEX(IrisClient), 1, ECsvCustomStatOp::Accumulate);

#if IRIS_CLIENT_PROFILER_DETAILED
	// 细分：按具体 RPC 名字分列，便于定位"哪个 RPC 频率异常高"。
	if (UE::Net::Private::bEnableDetailedClientProfiler)
	{
		FCsvProfiler::RecordCustomStat(RPCName, CSV_CATEGORY_INDEX(IrisClientDetailRPC), 1, ECsvCustomStatOp::Accumulate);
	}
#endif
}

/**
 * 记录一次"由于 async loading 等待资源而阻塞复制应用"的事件。
 *
 * 调用路径：ReplicationReader 在 PendingBatchData 因引用未解析（正等待
 * StreamableManager 加载 Asset）而推迟 apply 时调用。
 *
 * 注意：这里走的是 CSV Event（RecordEventf）而不是 CustomStat——Event 会在
 * CSV timeline 上打出一条竖线标记，适合描述"一次性瞬时事件"，便于视觉查
 * 找客户端卡顿/延迟的成因。
 *
 * @param BlockedObject    被阻塞的对象名（便于排查是哪个对象）；
 * @param NumBlockedAssets 当前仍在等待的资源数；
 * @param BlockedTime      已阻塞的秒数。
 */
void FClientProfiler::RecordBlockedReplication(const TCHAR* BlockedObject, int32 NumBlockedAssets, float BlockedTime)
{
	FCsvProfiler::RecordEventf(CSV_CATEGORY_INDEX(IrisClientBlockedByAsyncLoading), TEXT("ReplicatedObject: %s has been blocked waiting for async loading of %d assets for %f secs"), BlockedObject, NumBlockedAssets, BlockedTime);
}

/**
 * 查询当前 CSV Profiler 是否处于 capture 状态。
 *
 * 用途：上层在写入前先判断，避免无谓计算（例如 FName → string 拼接）；
 * 返回 false 时 Record* 调用虽然成本也很低，但跳过能节省极少量工作。
 */
bool FClientProfiler::IsCapturing()
{
	return FCsvProfiler::Get()->IsCapturing();
}

#endif

}
