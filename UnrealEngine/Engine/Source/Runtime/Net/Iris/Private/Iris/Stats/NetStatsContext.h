// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: NetStatsContext.h
// 分层: Iris L2 观测层 (Private)
// 职责: 提供每个并行 task 独占的统计采集容器 FNetStatsContext, 以及从调用点
//       采集数据的宏 UE_NET_IRIS_STATS_*。
// 核心类型:
//   - FNetTypeStatsData::EStatsIndex : 8 个复制阶段(PreUpdate/Poll/PollWaste/
//                                      Quantize/Write/WriteWaste/WriteCreationInfo/WriteExports)
//   - FNetTypeStatsData              : 每个阶段的 (Time, Bits, Count) 三元组数组
//   - FNetStatsContext               : per-task 容器, 以 TypeStatsIndex 分组
//                                      FNetTypeStatsData, 含 bIsAcquiredByTask 互斥位
//   - FNetStatsTimer                 : 派生自 NetCore 的 FNetTimer, 作用域计时器
// 宏编译开关:
//   - UE_NET_IRIS_CSV_STATS  : 采集类宏是否展开
//   - UE_NET_TRACE_ENABLED   : NetTrace 使用的 Timer 单独开关(兼容两条路径)
// =============================================================================

#pragma once

#include "HAL/Platform.h"
#include "Iris/Stats/NetStats.h"
#include "ProfilingDebugging/CsvProfilerConfig.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"    // 取对象数据(Protocol)用
#include "Iris/ReplicationSystem/ReplicationProtocol.h"    // FReplicationProtocol::TypeStatsIndex
#include "Net/Core/Trace/NetTrace.h"                       // FNetTimer 基类来源

namespace UE::Net::Private
{

/**
 * 【FNetTypeStatsData】单一"对象类型"的阶段级统计值, 按 EStatsIndex 下标索引。
 *
 * 设计要点:
 *   - 每个阶段用 (Time, Bits, Count) 三元组描述: Time 用 cycles, Bits 用 uint32 位数, Count 是参与统计的对象数。
 *   - 选择这三项的理由: 上游 Report 阶段需要 MS (Time→Milliseconds)、KBytes (Bits→KB)、Count 三类派生指标,
 *     它们是完整覆盖"耗时 / 数据量 / 对象数"的最小正交集。
 *   - Reset() 用 "*this = FNetTypeStatsData()" 的 POD 风格清零, 规避漏初始化。
 */
struct FNetTypeStatsData
{
	// 统计阶段枚举——与 ReplicationSystem 的复制管线一一对应。
	// 每个阶段的典型触发点(从 ReplicationSystem 视角):
	//   PreUpdate         : 帧前预处理(dirty tracking 扫描等)
	//   Poll              : 从属性源抓取最新数值
	//   PollWaste         : Poll 中因对象未改变等原因而"白忙"的耗时/次数(诊断用)
	//   Quantize          : 将源值量化为网络表示(fp/vector 压缩等)
	//   Write             : 序列化并写入包数据(消耗 Bits)
	//   WriteWaste        : Write 中因包容量不足被回滚等造成的"白写"(Bits 即浪费)
	//   WriteCreationInfo : 写入对象创建信息(首次复制时额外成本)
	//   WriteExports      : 写入 exports(NetGUID / NetTokens 等引用)
	enum EStatsIndex : unsigned
	{
		PreUpdate = 0U,
		Poll,
		PollWaste,
		Quantize,
		Write,
		WriteWaste,
		WriteCreationInfo,
		WriteExports,
		Count                                // 数组维度边界, 不是真实阶段
	};

	/**
	 * 单个阶段的统计三元组。
	 * Time: 累计耗时(cycles, Report 时通过 ToMilliseconds64 转毫秒)。
	 * Bits: 累计写入比特数(Report 时 (Bits+7)/8 转字节再 /1000 转 KB)。
	 * Count: 累计参与的对象数(根对象计数)。
	 */
	struct FStatsValue
	{
		// 逐字段累加: Child -> Parent 合并的最内层原子操作。
		void Accumulate(const FStatsValue& Other)
		{
			Time += Other.Time;
			Bits += Other.Bits;
			Count += Other.Count;
		}
		uint64 Time = uint64(0); // 累积耗时 (cycles)
		uint32 Bits = 0U;        // 累积写入比特数
		uint32 Count = 0U;       // 累积对象数
	};

	/** Zero out all stats */
	// 利用默认构造的零初始化 POD 特性做整体清零。
	void Reset()
	{
		*this = FNetTypeStatsData();
	}

	/** Accumulate all stats */
	// 将 Other 的各阶段数据逐项累加进来; 由 FNetStatsContext 级别的合并循环调用。
	void Accumulate(const FNetTypeStatsData& Other)
	{
		for (int32 Index = 0; Index < EStatsIndex::Count; ++Index)
		{
			Values[Index].Accumulate(Other.Values[Index]);
		}
	}

	FStatsValue Values[EStatsIndex::Count]; // 下标即 EStatsIndex
};

/**
 * 【FNetStatsContext】per-task 的统计采集上下文。
 *
 * 生命周期:
 *   - 由 FNetTypeStats 统一管理: 1 个 Parent + N 个 Child;
 *   - Child 在 PreUpdateSetup 按 (FScheduler::GetMaxNumWorkers()+1) 预创建;
 *   - 并行阶段 Acquire/Release, 帧末 AccumulateChildrenToParent 合并回 Parent。
 *
 * 并发语义:
 *   - bIsAcquiredByTask 是一道"软互斥"——在 FNetTypeStats::ChildStatsContextCS 保护下
 *     读写, 保证同一时刻一个 Child 只属于一个 task; 防止多 task 并发写入同一
 *     TypeStatsData 造成的数据竞争。
 *   - 实际的"在 task 内部对 TypeStatsData 进行累加"是无锁的, 因为此时 Child 已独占。
 *
 * TypeStatsIndex 的查找规则(见 GetTypeStatsDataForObject):
 *   - Root 对象: 直接取其 Protocol::TypeStatsIndex; Protocol 为空则 OOBChannel 槽(索引 1);
 *   - SubObject: 若 bShouldIncludeSubObjectWithRoot=true 则归到 Root 的槽里, 且不 bump Count;
 *                否则独立统计(Count 算作一个 Root 级样本)。
 */
class FNetStatsContext
{
public:
	FNetStatsContext();
	~FNetStatsContext();
	// 禁止拷贝: 内部含数组、指针、互斥位状态, 不允许被克隆。
	FNetStatsContext(const FNetStatsContext&) = delete;
	FNetStatsContext& operator=(const FNetStatsContext&) = delete;

	// 清零所有 TypeStatsData, 并把数组长度调整为 NumTypeStats。
	// 由 FNetTypeStats::UpdateContext 调用, 与 TypeStatsNames 长度保持一致。
	void ResetStats(int32 NumTypeStats);
	
	// Helper to lookup TypeStatsIndex from object, we currently store the NetTypeStatsIndex in the ReplicationProtocol but nothing prevents us from storing it elsewhere
	// Depending on config subobjects report their stats with the root, for those cases we do not bump the count of root objects.
	// returns the FNetTypeStatsData associated with the protocol used by the object references by the InternalIndex
	//
	// 【查找对象对应的 FNetTypeStatsData】
	//   - 接受 InternalIndex(NetRefHandleManager 内部索引), 取 ObjectData;
	//   - 根据 bShouldIncludeSubObjectWithRoot + IsSubObject() 决定 bTreatAsRoot;
	//   - OutUpdateCount 版本会告知调用者"是否应当将本次调用视作一次 Root 级 Count 增量"(1 或 0),
	//     以便 UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT 这类宏精准累加 Count。
	// 线程安全: 只要 Context 当前仅被一个 task 使用, 整体是无锁安全的。
	static FNetTypeStatsData& GetTypeStatsDataForObject(FNetStatsContext& Context, FInternalNetRefIndex InternalIndex, uint32& OutUpdateCount);
	static FNetTypeStatsData& GetTypeStatsDataForObject(FNetStatsContext& Context, FInternalNetRefIndex InternalIndex);
	// 以 ObjectData + bTreatAsRoot 为输入的底层实现(不做 SubObject 归属判定)。
	static FNetTypeStatsData& GetTypeStatsData(FNetStatsContext& Context, const FNetRefHandleManager::FReplicatedObjectData& ObjectData, bool bTreatAsRoot);

	// 是否可以被某个 task Acquire(未被占用)。
	// 由 FNetTypeStats::AcquireChildNetStatsContext 在持锁状态下查询, 不做额外同步。
	bool CanAcquire() const
	{
		return !bIsAcquiredByTask;
	}

protected:
	// 设置"已被 task 占用"标志; 由 friend FNetTypeStats 在持 ChildStatsContextCS 锁时调用。
	// check(!bIsAcquiredByTask) 保证不会出现 double-acquire(防御性断言)。
	void Acquire()
	{
		check(!bIsAcquiredByTask);
		bIsAcquiredByTask = true;
	}
	// 对称 Release; check(bIsAcquiredByTask) 防御 double-release。
	void Release()
	{
		check(bIsAcquiredByTask);
		bIsAcquiredByTask = false;
	}

private:
	// 友元: FNetTypeStats 拥有 Parent/Child 的完整生命周期控制权。
	friend class FNetTypeStats;

	const FNetRefHandleManager* NetRefHandleManager = nullptr; // 对象数据查找器, 由 FNetTypeStats::UpdateContext 注入
	TArray<FNetTypeStatsData> TypeStatsData;                   // 下标 = TypeStatsIndex, 长度 = TypeStatsNames.Num()
	bool bShouldIncludeSubObjectWithRoot = false;              // 快照自 CVar net.Iris.Stats.ShouldIncludeSubObjectWithRoot
	bool bIsAcquiredByTask = false;//True if actively being used by a parallel task and cannot be acquired by another one
	                                                           // 仅在持 ChildStatsContextCS 锁时读写, 防止多 task 同时抢占同一 Child
};

/**
 * 【FNetStatsTimer】Iris CSV 统计专用作用域计时器。
 *
 * 派生自 NetCore 的 FNetTimer(计时基类): 父类构造函数接受"是否启用"参数, 为 false 时内部
 * 所有 API 走 no-op 路径, 天然把"关闭 CSV 采集"下的运行时开销降至零。
 * 因此这里只要将 (InStatsContext != nullptr) 作为启用位传入父类即可——
 *   - 若 FNetTypeStats 未启用, GetNetStatsContext 会返回 nullptr, Timer 自动变为 no-op;
 *   - 若启用, Timer 记录起始 cycles, GetCyclesSinceStart() 供采集宏读取。
 */
class FNetStatsTimer : public FNetTimer
{
public:
	FNetStatsTimer(FNetStatsContext* InStatsContext)
	: FNetTimer(InStatsContext != nullptr) // 传入 false 时父类走零开销路径
	, NetStatsContext(InStatsContext)
	{
	}

	// If we have a context, this timer is enabled 
	// 供 UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT 等宏取 Context 做 early-out。
	FNetStatsContext* GetNetStatsContext() const { return NetStatsContext; }
	
private:
	FNetStatsContext* NetStatsContext; // 关联的采集上下文; nullptr 表示 Timer 禁用
};

// ===== inline 实现 ==========================================================

/**
 * 基于 ObjectData + bTreatAsRoot 决定 TypeStatsIndex 的底层查询。
 * bTreatAsRoot=true: 直接用 ObjectData.Protocol->TypeStatsIndex;
 *                   若 Protocol 为空(某些 OOB/带外场景)回退到 OOBChannelTypeStatsIndex。
 * bTreatAsRoot=false(即 SubObject 且配置为"与 Root 合并"): 通过 SubObjectRootIndex 反查 Root
 *                   的 ObjectData, 使用它的 Protocol TypeStatsIndex, 从而让 SubObject 统计
 *                   落到 Root 类型的槽里。
 */
inline FNetTypeStatsData& FNetStatsContext::GetTypeStatsData(FNetStatsContext& Context, const FNetRefHandleManager::FReplicatedObjectData& ObjectData, bool bTreatAsRoot)
{
	int32 TypeStatsIndex = FNetTypeStats::DefaultTypeStatsIndex;
	if (bTreatAsRoot)
	{
		// Root 对象: 用自身 Protocol 上的 TypeStatsIndex; 无 Protocol 落到 OOBChannel 槽。
		TypeStatsIndex = ObjectData.Protocol ? ObjectData.Protocol->TypeStatsIndex : FNetTypeStats::OOBChannelTypeStatsIndex;
	}
	else
	{
		// SubObject 合并模式: 向上找 Root 的 Protocol TypeStatsIndex, 把数据累加到 Root 的槽。
		const FNetRefHandleManager::FReplicatedObjectData& RootObjectData = Context.NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectData.SubObjectRootIndex);
		TypeStatsIndex = RootObjectData.Protocol ? RootObjectData.Protocol->TypeStatsIndex : FNetTypeStats::OOBChannelTypeStatsIndex;
	}

	return Context.TypeStatsData[TypeStatsIndex];
}

/**
 * 按 InternalIndex 定位 TypeStatsData 的简化版(不返回 Count 增量)。
 * 用于只关心 Bits / Time 而不做"Root 对象样本计数"的宏, 如 UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_FOR_OBJECT。
 */
inline FNetTypeStatsData& FNetStatsContext::GetTypeStatsDataForObject(FNetStatsContext& Context, FInternalNetRefIndex InternalIndex)
{
	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = Context.NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalIndex);
	// bTreatAsRoot 规则:
	//   - bShouldIncludeSubObjectWithRoot = false => 任何对象都按 Root 独立统计
	//   - 或该对象本身不是 SubObject => 按 Root 独立统计
	//   - 否则 = SubObject 合并到 Root
	const bool bTreatAsRoot = !Context.bShouldIncludeSubObjectWithRoot || !ObjectData.IsSubObject();
	return GetTypeStatsData(Context, ObjectData, bTreatAsRoot);
}


/**
 * 带 OutUpdateCount 的版本: 若被视为 Root(SubObject 合并模式下 SubObject 返回 0), OutUpdateCount=1, 否则 0。
 * 这样采集宏 ADD_TIME_AND_COUNT 只在 Root 级累加 Count, 避免 SubObject 导致 Count 被重复计入。
 */
inline FNetTypeStatsData& FNetStatsContext::GetTypeStatsDataForObject(FNetStatsContext& Context, FInternalNetRefIndex InternalIndex, uint32& OutUpdateCount)
{
	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = Context.NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalIndex);		
	const bool bTreatAsRoot = !Context.bShouldIncludeSubObjectWithRoot || !ObjectData.IsSubObject();
	OutUpdateCount = bTreatAsRoot ? 1U : 0U;
	return GetTypeStatsData(Context, ObjectData, bTreatAsRoot);
}


/**
 * 将 TypeStatsData 数组长度调整为 NumTypeStats 并逐个 Reset。
 * SetNum 会保留前缀数据; Reset 把每个元素清零。
 * 在 FNetTypeStats::UpdateContext 与 Accumulate 末尾被调用。
 */
inline void FNetStatsContext::ResetStats(int32 NumTypeStats)
{ 
	TypeStatsData.SetNum(NumTypeStats);
	for (FNetTypeStatsData& TypeStats : TypeStatsData)
	{
		TypeStats.Reset();
	}
} 

}

// =============================================================================
// 采集宏区 (UE_NET_IRIS_STATS_*)
//
// 设计原则:
//   - 全部在 do{...}while(0) 中实现, 保证可用于 if/else 等上下文;
//   - 宏最外层总是"if (NetStatsContext/Timer.GetNetStatsContext())"做 early-out,
//     CSV 关闭时分支预测友好且热路径几乎零开销;
//   - 若 UE_NET_IRIS_CSV_STATS 未开启, 所有宏都被定义为空, 代码完全被编译掉;
//   - StatName 以 token 拼接形式嵌入 EStatsIndex, 必须是 PreUpdate/Poll/Quantize 等合法枚举名。
//
// 典型调用站点(由 ReplicationSystem 在各阶段触发):
//   - UE_NET_IRIS_STATS_TIMER + UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT
//       : 例如 Poll/Quantize 阶段, 作用域内测时并在对象处理完成时汇入。
//   - UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_AND_COUNT_FOR_OBJECT
//       : Write 阶段按对象记录写入比特数与对象数。
//   - UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_FOR_OBJECT_AS_WASTE
//       : Write 过程中对象被回滚/未能进入包, 视作"浪费"同时记入 Write + WriteWaste 两槽。
//   - UE_NET_IRIS_STATS_INCREMENT_FOR_OBJECT / ADD_COUNT_FOR_OBJECT
//       : 仅 Count 级别的增量, 典型用在 WriteExports 等没有 Time/Bits 的场合。
// =============================================================================

// Wrap usage of stats in macros so we can compile it out
#if UE_NET_IRIS_CSV_STATS

// 采集"某对象在某阶段的耗时 + 对象计数"。展开后:
//   1. 若 Timer 没有关联 Context 则 no-op;
//   2. 查 TypeStatsData (同时拿到 Root 级计数增量 CountIncrement);
//   3. StatsData.Values[StatName].Time += Timer.GetCyclesSinceStart();
//   4. StatsData.Values[StatName].Count += CountIncrement;
// StatName 必须为 EStatsIndex 合法成员(PreUpdate/Poll/Quantize/…)。
#define UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(Timer, StatName, ObjectIndex) \
	do { \
		if (UE::Net::Private::FNetStatsContext* LocalNetStatsContext = Timer.GetNetStatsContext()) \
		{ \
			uint32 CountIncrement = 0U; \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*LocalNetStatsContext, ObjectIndex, CountIncrement); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Time += Timer.GetCyclesSinceStart(); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Count += CountIncrement; \
		} \
	} while (0)

// 同时写入"正常阶段槽 + Waste 槽"两份耗时与计数。适用于"发生但被回滚"的调用场景(典型: Poll/PollWaste)。
// 依赖命名约定: StatName##Waste 必须是另一个合法 EStatsIndex(如 Poll 与 PollWaste 配对)。
#define UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT_AS_WASTE(Timer, StatName, ObjectIndex) \
	do { \
		if (UE::Net::Private::FNetStatsContext* LocalNetStatsContext = Timer.GetNetStatsContext()) \
		{ \
			const uint64 DeltaTimeForStat = Timer.GetCyclesSinceStart(); \
			uint32 CountIncrement = 0U; \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*LocalNetStatsContext, ObjectIndex, CountIncrement); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Time += DeltaTimeForStat; \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Count += CountIncrement; \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName##Waste].Time += DeltaTimeForStat; \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName##Waste].Count += CountIncrement; \
		} \
	} while (0)

// 采集"某对象在某阶段写入的 Bits + 对象计数"。Write 阶段按对象提交比特数时使用。
#define UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_AND_COUNT_FOR_OBJECT(NetStatsContext, BitCount, StatName, ObjectIndex) \
	do { \
		if (NetStatsContext) \
		{ \
			uint32 CountIncrement = 0U; \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*NetStatsContext, ObjectIndex, CountIncrement); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Bits += BitCount; \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Count += CountIncrement; \
		} \
	} while (0)

// 仅累加 Bits 不动 Count。典型场景: Write 阶段的额外细分(比如 CreationInfo 的 bits), 或已经由别的路径 bump Count 过。
#define UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_FOR_OBJECT(NetStatsContext, BitCount, StatName, ObjectIndex) \
	do { \
		if (NetStatsContext) \
		{ \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*NetStatsContext, ObjectIndex); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Bits += BitCount; \
		} \
	} while (0)

// 把 Bits 同时写进"主阶段槽"和"Waste 槽"。例: Write 回滚时(写入了 Bits 但最终丢弃), 同时累加 Write 与 WriteWaste 的 Bits。
#define UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_FOR_OBJECT_AS_WASTE(NetStatsContext, BitCount, StatName, ObjectIndex) \
	do { \
		if (NetStatsContext) \
		{ \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*NetStatsContext, ObjectIndex); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Bits += BitCount; \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName##Waste].Bits += BitCount; \
		} \
	} while (0)

// Increment stat count by 1
// 只给 Count 加 1; 用于事件性计数(比如每处理一个 SubObject 增 1)。
// 注意: 这里不走 OutUpdateCount 的 Root 归属逻辑, 直接 +1, 所以 SubObject 模式下调用方需自行注意是否会重复计数。
#define UE_NET_IRIS_STATS_INCREMENT_FOR_OBJECT(NetStatsContext, StatName, ObjectIndex) \
	do { \
		if (NetStatsContext) \
		{ \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*NetStatsContext, ObjectIndex); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Count++; \
		} \
	} while (0)

// Increment stat count by any amount
// 给 Count 加任意正数; 与 INCREMENT 类似但接受 CountToAdd 参数。典型用于 WriteExports 批量写多个 export 时一次性累加。
#define UE_NET_IRIS_STATS_ADD_COUNT_FOR_OBJECT(NetStatsContext, StatName, ObjectIndex, CountToAdd) \
	do { \
		if (NetStatsContext) \
		{ \
			UE::Net::Private::FNetTypeStatsData& StatsData = UE::Net::Private::FNetStatsContext::GetTypeStatsDataForObject(*NetStatsContext, ObjectIndex); \
			StatsData.Values[UE::Net::Private::FNetTypeStatsData::EStatsIndex::StatName].Count += CountToAdd; \
		} \
	} while (0)

#else

// UE_NET_IRIS_CSV_STATS 未开启时, 所有采集宏编译为空, 不产生任何代码。
#define UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(...)
#define UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT_AS_WASTE(...)
#define UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_AND_COUNT_FOR_OBJECT(...)
#define UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_FOR_OBJECT(...)
#define UE_NET_IRIS_STATS_ADD_BITS_WRITTEN_FOR_OBJECT_AS_WASTE(...)
#define UE_NET_IRIS_STATS_INCREMENT_FOR_OBJECT(...)
#define UE_NET_IRIS_STATS_ADD_COUNT_FOR_OBJECT(...)

#endif

// this is needed for net traces as well
// Timer 的开关要稍微宽松: 只要 CSV 统计或 NetTrace 任一开启, 就需要运行时 Timer(它们共用同一对象)。
// 这样保证即使未开 CSV, NetTrace 路径仍能拿到耗时, 反之亦然。
#if UE_NET_IRIS_CSV_STATS || UE_NET_TRACE_ENABLED

// 在作用域内创建一个 FNetStatsTimer 变量; 传入 nullptr 时父类走 no-op。
// 典型用法: 在阶段函数开头 UE_NET_IRIS_STATS_TIMER(PollTimer, Context);
//           在阶段函数尾部 UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(PollTimer, Poll, ObjectIndex);
#define UE_NET_IRIS_STATS_TIMER(TimerName, NetStatsContext) UE::Net::Private::FNetStatsTimer TimerName(NetStatsContext);

#else

// 两者都未开启时 Timer 宏也变为空。
#define UE_NET_IRIS_STATS_TIMER(...)

#endif
