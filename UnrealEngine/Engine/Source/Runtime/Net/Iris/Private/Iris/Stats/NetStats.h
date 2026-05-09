// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: NetStats.h
// 分层: Iris L2 观测层 (Private, 无对外 Public 头)
// 职责: 声明 Iris 网络复制的 CSV 性能统计核心类型
//   - FNetSendStats     : 每帧发送端的"摘要"统计(对象数、huge object 耗时等)
//   - FNetTypeStats     : 按"对象类型"分组的阶段级统计, 管理 Parent + N 个 Child
//                         NetStatsContext, 支持 LowLevelTasks 并行 task 采集
//   - FReplicationStats : 更高级别聚合(avg/max)的待发送/HugeObject 队列指标
// 上层使用方: ReplicationSystem (散落各阶段采集点通过 UE_NET_IRIS_STATS_* 宏调用)
// 编译开关 : UE_NET_IRIS_CSV_STATS / CSV_PROFILER_STATS
// =============================================================================

#pragma once

#include "HAL/Platform.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogMacros.h"
#include "Containers/Array.h"

/**
 * Iris 网络统计模块专用日志类别。
 * 默认 Verbosity = Log, 编译期最大 Verbosity = All。
 * 典型使用点: PreUpdateSetup 创建 Child NetStatsContext 时输出诊断日志。
 */
DECLARE_LOG_CATEGORY_EXTERN(LogNetStats, Log, All);

namespace UE::Net::Private
{
	// 前向声明: NetRefHandleManager 提供 InternalNetRefIndex -> ReplicatedObjectData 查询
	// 以便从对象查到其 ReplicationProtocol::TypeStatsIndex, 定位对应的统计槽。
	class FNetRefHandleManager;
	// 前向声明: per-task 统计上下文容器(见 NetStatsContext.h)。
	class FNetStatsContext;
}

namespace UE::Net::Private
{

/**
 * Send stats for Iris replication reported to the CSV profiler. Mostly of interest on the server side due to the server authoritative network model.
 * Its intended use is to do thread local tracking to an instance and then use the Accumulate function for thread safe updating of the ReplicationSystem owned instance.
 * The ReplicationSystem owned instance is the one reporting to the CSV profiler.
 *
 * 【FNetSendStats】发送端每帧"摘要"统计, 主要用于服务端(权威网络模型下客户端数据较少)。
 *
 * 典型使用模式:
 *   1) 每个 Connection/task 在栈上或 thread-local 持有一个 FNetSendStats 实例, 调用
 *      不加锁的 setter/adder 做本地累计(因此除 Accumulate/Reset/ReportCsvStats 外的
 *      接口都是非线程安全的, 调用者自己保证 per-thread 隔离);
 *   2) 发送阶段完成后, 统一 Accumulate() 到 ReplicationSystem 持有的全局实例(Accumulate
 *      内部加锁, 可并发调用);
 *   3) 每帧末尾由持有者统一调用 ReportCsvStats() 写入 CSV Profiler。
 *
 * 设计要点:
 *   - 拷贝与拷贝赋值被显式禁用, 保证统计实例不会被意外克隆导致重复累加。
 *   - 内部只有 Accumulate/Reset/ReportCsvStats 三个接口持 CS 锁; 其余 inline setter
 *     一律无锁, 这种"thread-local + barrier 合并"模式是热路径上的常见做法。
 *   - 若未编译 UE_NET_IRIS_CSV_STATS / CSV_PROFILER_STATS, Accumulate/Reset/Report
 *     会被 #if 编译为空, 在非 Profiling 构建中无任何运行时开销。
 */
class FNetSendStats
{
public:
	FNetSendStats() = default;
	// 显式禁用拷贝: FCriticalSection 不可拷贝, 且复制统计实例会导致重复累加的语义风险。
	FNetSendStats(const FNetSendStats&) = delete;
	FNetSendStats& operator=(const FNetSendStats&) = delete;

	/** Set number of objects scheduled for replication. */
	// 设置"本帧被安排参与复制的 Root 对象总数"(覆盖写入, 非累加)。
	// 线程安全: 否, 仅本地实例调用。
	void SetNumberOfRootObjectsScheduledForReplication(uint32 Count);

	/** Add number of replicated root objects. */
	// 累加"本帧实际复制出去的 Root 对象数"。Root 对象 = 非 SubObject 的复制对象。
	// 线程安全: 否。
	void AddNumberOfReplicatedRootObjects(uint32 Count);

	/** Add number of replicated objects, including subobjects. */
	// 累加"本帧实际复制出去的对象总数(包含 SubObject)"。
	// 注意: NumReplicatedObjects 一般 >= NumReplicatedRootObjects, 其差值 = 被复制的 SubObject 数。
	void AddNumberOfReplicatedObjects(uint32 Count);

	/** Add number of replicated destruction infos. */
	// 累加"本帧复制出去的 DestructionInfo(销毁通告)数量"。
	// DestructionInfo != ReplicatedObject: 对象已销毁, 但仍需告知客户端销毁事件; 不走常规复制管线。
	// 因此它们在字段上与 NumReplicatedObjects 互不重叠。
	void AddNumberOfReplicatedDestructionInfos(uint32 Count);

	/** Add number of replicated objects, including subobjects, using delta compression. */
	// 累加"本帧使用 Delta 压缩复制的对象数(含 SubObject)"。与 NumReplicatedObjects 是子集关系。
	void AddNumberOfDeltaCompressedReplicatedObjects(uint32 Count);

	/** Add number of replicated object states masked out such that no state is replicated for the object. The object may still replicate attachments. */
	// 累加"本帧状态被完全 mask out 的对象数"——即无任何属性位被复制(但仍可能有 Attachments/RPC)。
	// 用于衡量 condition/filtering 命中率、判断是否存在"空 header 浪费"。
	void AddNumberOfReplicatedObjectStatesMaskedOut(uint32 Count);

	/** Get the number of replicated root objects. */
	uint32 GetNumberOfReplicatedRootObjects() const;

	/** Get the number of replicated objects, including subobjects. */
	uint32 GetNumberOfReplicatedObjects() const;

	/** Set the number of huge objects in sending or waiting to be acked. */
	// 覆盖写入"当前仍处于 HugeObject 发送队列(未完成发送或未 ack)的对象数"的瞬时值。
	// HugeObject = 单次写入超过可用包容量的大对象, 必须分片且全部 ack 才算完成。
	void SetNumberOfActiveHugeObjects(uint32 Count);

	/** Add time in seconds waiting for completely sent huge object to be acked. */
	// 累加一次"HugeObject 已全部发出、正在等待 ack"的耗时, 同时计数 +1。
	// 由此 Report 阶段能算出总时间与等待次数。
	void AddHugeObjectWaitingTime(double Seconds);

	/** Add time in seconds waiting to be able to continue sending huge object. */
	// 累加一次"HugeObject 在发送过程中因带宽/pack 容量不足而卡顿(stall)"的耗时, 同时计数 +1。
	// HugeObjectStallTimeMs 指标是诊断服务端复制背压的重要依据。
	void AddHugeObjectStallTime(double Seconds);

	/** Add stats from another instance. */
	// 将另一实例的数据合并进本实例, 内部持 CS 锁, 线程安全。
	// 典型调用: 每个 Connection/Task 的 thread-local 实例 -> ReplicationSystem 全局实例。
	// 合并完后并不自动 Reset 源实例(Reset 语义由调用方控制)。
	IRISCORE_API void Accumulate(const FNetSendStats& Stats);

	/** Reset stats. */
	// 清零所有字段, 内部加锁。通常在 ReportCsvStats 之后调用以迎接下一帧。
	IRISCORE_API void Reset();

	/** Report the stats to the CSV profiler. Does nothing if CSV profiler support is compiled out. */
	// 将当前累计数据写入 CSV Profiler(类别 Iris)。
	// 若 CSV_PROFILER_STATS 未开启则整个函数体被编译掉, 不产生任何运行时开销。
	// 会根据 ReplicatingConnectionCount 计算各"平均值"(AvgXxx) 指标, 以便跨连接归一化。
	IRISCORE_API void ReportCsvStats();

	/** Set number of replicating connections. */
	// 覆盖写入"本帧参与复制的连接数"。
	// 计数规则: 只统计真正进入 SendStep 的活跃连接; 不包含 pending 握手、已断开的连接。
	// ReportCsvStats 会用它作为"每连接平均"指标的分母。
	void SetNumberOfReplicatingConnections(uint32 Count);

private:
	// Helper struct to facilitate reset of stats.
	// POD-风格的内部字段结构体, 让 Reset() 只需 "Stats = FStats()" 一行即可清零, 无需逐字段维护。
	struct FStats
	{
		double HugeObjectWaitingForAckTimeInSeconds = 0; // 累计 HugeObject 等待 ack 时间(秒)
		double HugeObjectStallingTimeInSeconds = 0;      // 累计 HugeObject 因容量不足卡顿时间(秒); 即 HugeObjectStallTimeMs 的来源

		int32 ScheduledForReplicationRootObjectCount = 0; // 本帧被调度参与复制的 Root 对象数(Set 覆盖)
		int32 ReplicatedRootObjectCount = 0;              // 本帧实际复制的 Root 对象数(累加)
		int32 ReplicatedObjectCount = 0;                  // 本帧实际复制的对象数(含 SubObject, 累加)
		int32 ReplicatedDestructionInfoCount = 0;         // 本帧复制出的 DestructionInfo 数——与 ReplicatedObjectCount 语义不同, 不重叠
		int32 DeltaCompressedObjectCount = 0;             // 本帧使用 Delta 压缩的对象数(含 SubObject)
		int32 ReplicatedObjectStatesMaskedOut = 0;        // 本帧状态被完全 mask out 的对象数
		int32 ActiveHugeObjectCount = 0;                  // 当前 HugeObject 活跃数(瞬时)
		int32 HugeObjectsWaitingForAckCount = 0;          // 累计 "HugeObject 进入等待 ack" 事件次数
		int32 HugeObjectsStallingCount = 0;               // 累计 "HugeObject stall" 事件次数
		int32 ReplicatingConnectionCount = 0;             // 本帧参与复制的连接数(Set 覆盖)
	};

	// 临界区, 仅用于 Accumulate / Reset / ReportCsvStats 三个接口; 其他 inline setter 不加锁。
	// 热路径上通过"thread-local 实例 + 后期 barrier 合并"的模式避免这里成为瓶颈。
	FCriticalSection CS;
	FStats Stats;
};

// ===== FNetSendStats 的 inline 实现 =========================================
// 说明: 这些 inline setter/adder 全部无锁, 仅用于"thread-local 实例"场景。
// 真正跨线程合并只通过 Accumulate() 完成; ReportCsvStats/Reset 也持同一把 CS。

inline void FNetSendStats::SetNumberOfRootObjectsScheduledForReplication(uint32 Count)
{
	Stats.ScheduledForReplicationRootObjectCount = Count;
}

inline void FNetSendStats::AddNumberOfReplicatedRootObjects(uint32 Count)
{
	Stats.ReplicatedRootObjectCount += Count;
}

inline void FNetSendStats::AddNumberOfReplicatedObjects(uint32 Count)
{
	Stats.ReplicatedObjectCount += Count;
}

inline void FNetSendStats::AddNumberOfDeltaCompressedReplicatedObjects(uint32 Count)
{
	Stats.DeltaCompressedObjectCount += Count;
}

inline void FNetSendStats::AddNumberOfReplicatedObjectStatesMaskedOut(uint32 Count)
{
	Stats.ReplicatedObjectStatesMaskedOut += Count;
}

inline void FNetSendStats::AddNumberOfReplicatedDestructionInfos(uint32 Count)
{
	Stats.ReplicatedDestructionInfoCount += Count;
}

inline uint32 FNetSendStats::GetNumberOfReplicatedRootObjects() const
{
	return Stats.ReplicatedRootObjectCount;
}

inline uint32 FNetSendStats::GetNumberOfReplicatedObjects() const
{
	return Stats.ReplicatedObjectCount;
}

inline void FNetSendStats::SetNumberOfActiveHugeObjects(uint32 Count)
{
	// Set 语义: 覆盖当前 HugeObject 活跃数(而非累加)。
	Stats.ActiveHugeObjectCount = static_cast<int32>(Count);
}

inline void FNetSendStats::AddHugeObjectWaitingTime(double Seconds)
{
	// 同时累加"事件次数"与"总耗时", 保证 Report 阶段可以算平均。
	++Stats.HugeObjectsWaitingForAckCount;
	Stats.HugeObjectWaitingForAckTimeInSeconds += Seconds;
}

inline void FNetSendStats::AddHugeObjectStallTime(double Seconds)
{
	// 同上; stall 是指 HugeObject 发送过程中因包容量/带宽不足导致本帧无法继续发送。
	++Stats.HugeObjectsStallingCount;
	Stats.HugeObjectStallingTimeInSeconds += Seconds;
}

inline void FNetSendStats::SetNumberOfReplicatingConnections(uint32 Count)
{
	Stats.ReplicatingConnectionCount = Count;
}

/**
 * Stats defined per object type for Iris replication reported to the CSV profiler. Mostly of interest on the server side due to the server authoritative network model.
 * Currently we use a single NetStatsContext when collecting the stats, when we go wide we need to extend this to use separate contexts for different threads.
 *
 * 【FNetTypeStats】按"对象类型名"分组的阶段级统计管理器, 由 ReplicationSystem 持有。
 *
 * 并发模型 (架构文档 §3.2 关键):
 *   - 持有一个 ParentStatsContext (主上下文) + N 个 ChildStatsContext (per-task);
 *   - 每帧开始 PreUpdateSetup() 按照 LowLevelTasks::FScheduler::GetMaxNumWorkers()+1
 *     (+1 是把 GameThread 也算在内, 因为 task 也可能在 GT 上执行)
 *     预分配足够的 Child context, 避免并行阶段内动态创建造成的锁竞争;
 *   - 并行阶段: 各 task 通过 AcquireChildNetStatsContext() 取得独占的 Child context
 *     (返回前会置 bIsAcquiredByTask=true, 防止其他 task 抢占同一个 Child), 结束时
 *     ReleaseChildNetStatsContext() 归还;
 *   - 并行阶段结束后主线程 AccumulateChildrenToParent() 将 Child 合并回 Parent;
 *   - ReportCSVStats() 每帧末从 Parent 写入 CSV Profiler 并重置。
 *
 * 类型槽(TypeStatsIndex)约定:
 *   - 索引 0 = DefaultTypeStatsIndex, 名称 "Undefined", 兜底槽;
 *   - 索引 1 = OOBChannelTypeStatsIndex, 名称 "OOBChannel", 带外通道/无 Protocol 的对象;
 *   - >= 2 由 GetOrCreateTypeStats(FName) 按需注册, 幂等: 同名重复调用返回同一索引。
 *
 * 注意:
 *   - GetOrCreateTypeStats 在 parallel phase 内调用会 check 失败——并行阶段对象列表
 *     不允许扩张, 否则会破坏各 Child context TypeStatsData 数组长度的一致性。
 *   - bIsEnabled 依据 FCsvProfiler::IsCapturing() 每帧动态更新, 关闭 CSV 采集时大部分
 *     路径都会走 early-out, 几乎零开销。
 */
class FNetTypeStats
{
public:

	/**
	 * Init 参数包, 目前只有 NetRefHandleManager 指针。
	 * 解耦: FNetTypeStats 不直接依赖 ReplicationSystem, 由外部注入。
	 */
	struct FInitParams
	{
		// 用于从 FInternalNetRefIndex 反查 FReplicatedObjectData, 进而取得 Protocol::TypeStatsIndex。
		FNetRefHandleManager* NetRefHandleManager = nullptr;
	};

	// Preset stats type indices
	// 预置槽位索引, 在构造函数里通过 GetOrCreateTypeStats 按顺序注册, 因此一定是 0/1。
	static constexpr int32 DefaultTypeStatsIndex = 0U;   // "Undefined"   —— 尚未注册专属 TypeStatsIndex 的对象
	static constexpr int32 OOBChannelTypeStatsIndex = 1U; // "OOBChannel"  —— 无 ReplicationProtocol 的对象(带外/系统通道)

public:
	FNetTypeStats();
	// 禁用拷贝: 内部含 FCriticalSection + 指针资源, 不允许拷贝。
	FNetTypeStats(const FNetTypeStats&) = delete;
	FNetTypeStats& operator=(const FNetTypeStats&) = delete;
	~FNetTypeStats();

	// 初始化: 保存 NetRefHandleManager 指针, 并对 Parent/现有 Child 做一次 UpdateContext。
	// 应在进入任何 parallel phase 之前调用一次; 非线程安全。
	void Init(FInitParams& InitParams);

	/** Called once a frame before stats collection starts in order to set up ChildStatsContexts */
	// 每帧开始调用一次, 按需创建 Child NetStatsContext 以匹配当前调度器的最大并发度。
	// 由于 Scheduler 的 worker 数量在运行期可能变化, 这里按 (MaxNumWorkers+1) 预分配。
	// 主线程调用, 非线程安全; 执行时不能已处于 parallel phase。
	void PreUpdateSetup();

	/** Reset stats */
	// 清空 Parent 与所有 Child context 的 TypeStatsData(通过 UpdateContext 重建为当前长度)。
	// 典型触发: Init 时, 或统计配置发生变化时强制重置。
	void ResetStats();
		
	/** Returns the TypeStatIndex associated with the Name or creates a new one if it does not exist */
	// 幂等地注册/查询一个类型名对应的统计槽位。
	// 线性搜索 TypeStatsNames, 适合"启动期注册、运行期只查询"的场景; 并行阶段不得调用(内部 check)。
	// 新增槽位时会同步扩展 Parent 与所有 Child context 的 TypeStatsData 数组长度, 保证合并对齐。
	int32 GetOrCreateTypeStats(FName Name);

	/** Get parent context if stats is enabled. Should not be called when in a parallel phase, as the ChildContexts should be used instead. */
	// 取 Parent 上下文。仅当 CSV 采集启用时返回非空, 否则返回 nullptr 以让调用方 early-out。
	// 并行阶段禁止调用(check): 此时应使用 AcquireChildNetStatsContext 取得独立 Child。
	FNetStatsContext* GetNetStatsContext() { check(!bIsInParallelPhase); return IsEnabled() ?  ParentStatsContext : nullptr; }

	/** Updated every frame based on the state of the CSVProfiler */
	// ReportCSVStats 开头根据 FCsvProfiler::IsCapturing() 更新该状态。
	// 若当前 CSV 采集已关闭, 整个模块进入低开销分支。
	bool IsEnabled() const { return bIsEnabled; }

	/** Accumulate stats from context to main context */
	// 将指定 Child context 的数据合并到 Parent 并 Reset 源 context。
	// 若传入的就是 Parent 则直接 return(幂等自保护), 防止对自身 double-add。
	// 数组长度以 min(Context, Parent) 为准, 但内部 ensure Child 长度 <= Parent 长度。
	void Accumulate(FNetStatsContext& Context);

	/** ReportCSVStats and reset context */
	// 每帧末调用: 先 AccumulateChildrenToParent, 再把 Parent 按类型/阶段写入 CSV, 最后清零每个 TypeStatsData。
	// 开启 CSV 时才真正执行, 否则为空函数。必须在 parallel phase 之外调用。
	void ReportCSVStats();

	/** Wipes the Child NetStatsContext Map, ready for the next frame */
	// 释放所有 Child context(delete + Empty)。由析构或需要重建全部 Child 的时刻调用。
	// 注意: 这是"销毁"操作, 不是"清零"; 日常帧末重置走 Accumulate -> Reset, 不会销毁 Child。
	void CleanupChildNetStatsContexts();

	/** For each Child NetStatsContext, accumulate its values into the Parent NetStatsContext */
	// 顺序遍历 ChildStatsContext 并对每个调用 Accumulate()。
	// 必须在 parallel phase 之外(所有 task 已 Join)调用, 否则可能读到半成品。
	void AccumulateChildrenToParent();

	/** Returns the next available ChildNetStatsContext which isn't being used by another task. (Thread-safe) */
	// 并行阶段内由 worker task 调用, 取得一个"当前未被其他 task 占用"的 Child context。
	// 实现: 持 ChildStatsContextCS 锁线性扫描 CanAcquire() 的第一个, 标记 Acquire()。
	// 若预分配不够(PreUpdateSetup 理论上已按 MaxNumWorkers+1 预分配)会 ensure 报警并临时新建。
	FNetStatsContext* AcquireChildNetStatsContext();

	/** Relinquishes a ChildNetStatsContext so it can be used by another task. (Thread-safe) */
	// 释放之前 AcquireChildNetStatsContext 取得的 context, 持锁清除 bIsAcquiredByTask。
	// 传入 nullptr 会 ensure 警告(可能意味着统计数据丢失的 bug)。
	void ReleaseChildNetStatsContext(FNetStatsContext* StatsContext);

	/** Sets a flag used to guard against using non thread safe operations when we're running parallel tasks. */
	// 在外部(ReplicationSystem)进入/离开 parallel phase 时设置, 用于对多个 check 断言做保护,
	// 防止诸如 GetOrCreateTypeStats / ResetStats 等非并发安全接口被误用。
	void SetIsInParallelPhase(const bool InParallelPhase) { bIsInParallelPhase = InParallelPhase; }

private:
	// 创建一个 NetStatsContext 并初始化其 NetRefHandleManager / CVar 状态等; 由 Parent 构造路径调用。
	FNetStatsContext* CreateNetStatsContext();

	/** Creates a new FNetStatsContext and adds it to ChildStatsContext */
	// 创建一个 Child context 并加入 ChildStatsContext 数组(持锁), 供并行 task 认领。
	FNetStatsContext* CreateChildNetStatsContext();

	// 将给定 context 的基础字段(NetRefHandleManager / CVar 快照 / TypeStatsData 长度)刷新到当前状态。
	void UpdateContext(FNetStatsContext& Context);

	FNetStatsContext* ParentStatsContext = nullptr; // 主上下文: 所有 Child 最终汇聚到这里, ReportCSVStats 从这里读。

	/** Critical Section used to prevent multiple threads from accessing ChildStatsContext array simultaneously */
	// 保护 ChildStatsContext 数组本身 (Add/遍历) 与每个 Child 的 bIsAcquiredByTask 状态位修改。
	// 注意: "采集数据到 Child" 的热路径不持这把锁, 仅在 Acquire/Release/Create 时短暂持有。
	FCriticalSection ChildStatsContextCS;

	/** NetStatsContexts that are used by sub tasks during a parallel phase. Are accumulated to ParentStatsContext at the end of a frame. */
	// Child 池, 运行期动态增长(但一般只在 PreUpdateSetup 增长); 帧末合并进 Parent 后不销毁, 只 Reset。
	TArray<FNetStatsContext*> ChildStatsContext;

	FNetRefHandleManager* NetRefHandleManager = nullptr; // 外部注入的对象索引 -> 对象数据查询器
	TArray<FName> TypeStatsNames;                        // 索引 <-> 类型名 映射; 下标就是 TypeStatsIndex
	bool bIsEnabled = false;                             // 当前是否正在 CSV 采集; 每帧 ReportCSVStats 开头刷新
	bool bIsInParallelPhase = false;                     // 是否处于 parallel phase 中(外部显式设置), 用于 check 守卫
};

/**
 * 【FReplicationStats】比 FNetSendStats 更高阶的聚合指标。
 *
 * 与 FNetSendStats 的区别:
 *   - FNetSendStats = 每帧"发送端总量"类计数(对象数、HugeObject 耗时), 注重 throughput;
 *   - FReplicationStats = 关注"待发送队列水位"类指标, 每次采样累加 sum + 记录 max,
 *     ReportCSVStats 时以 SampleCount 为分母给出"平均值"(AvgXxx)与"峰值"(MaxXxx)。
 *
 * 典型采样点: 每次 ReplicationSystem 完成一轮 SendStep 后取一次 PendingObjectCount /
 *            HugeObjectSendQueue 快照, 调用 Accumulate 合并。
 *
 * 非线程安全: 该结构通常绑定在单个 ReplicationSystem 主线程上下文, 无锁设计。
 */
struct FReplicationStats
{
	/** Report the stats to the CSV profiler. Does nothing if CSV profiler support is compiled out. */
	// 以 SampleCount 为分母计算 Avg*、直接上报 Max*。
	// SampleCount = 0 时所有指标输出 0 以避免 NaN/脏数据。
	void ReportCSVStats();

	// 把 Other 的一次采样累加到本结构: sum/max 分别用加法与 Max 合并; SampleCount 自增。
	// 注意调用方需保证 Accumulate 非并发(结构本身无锁)。
	void Accumulate(const FReplicationStats& Stats)
	{
		PendingObjectCount += Stats.PendingObjectCount;                   // 累积 pending 对象总量(用于求平均)
		PendingDependentObjectCount += Stats.PendingDependentObjectCount; // 累积 pending 依赖对象总量
		HugeObjectSendQueue += Stats.HugeObjectSendQueue;                 // 累积 HugeObject 发送队列长度
		MaxPendingObjectCount = FMath::Max(MaxPendingObjectCount, Stats.MaxPendingObjectCount);                   // 峰值 = max
		MaxPendingDependentObjectCount = FMath::Max(MaxPendingDependentObjectCount, Stats.MaxPendingDependentObjectCount);
		MaxHugeObjectSendQueue = FMath::Max(MaxHugeObjectSendQueue, Stats.MaxHugeObjectSendQueue);
		SampleCount += Stats.SampleCount;                                 // 样本数(平均值的分母)
	}

	uint64 PendingObjectCount;             // 采样期内"待发送对象数"的累积和
	uint64 PendingDependentObjectCount;    // 采样期内"待发送依赖对象数"的累积和(依赖 = 必须先于某对象发送的前置对象)
	uint64 HugeObjectSendQueue;            // 采样期内"HugeObject 发送队列长度"的累积和
	uint32 MaxPendingObjectCount;          // 采样期内 PendingObjectCount 的峰值
	uint32 MaxPendingDependentObjectCount; // 采样期内 PendingDependentObjectCount 的峰值
	uint32 MaxHugeObjectSendQueue;         // 采样期内 HugeObjectSendQueue 的峰值
	uint32 SampleCount;                    // 样本数; ReportCSVStats 时用作 Avg 的分母
};

} // end namespace UE::Net::Private
