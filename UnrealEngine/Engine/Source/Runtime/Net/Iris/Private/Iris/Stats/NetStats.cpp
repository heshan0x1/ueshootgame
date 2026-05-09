// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   本文件实现 Iris 发送端的 CSV 性能统计聚合。包含三个层级：
//
//     1) FNetSendStats —— 每帧"全连接级"摘要（已复制对象数、Delta 数、
//        HugeObject 排队/卡顿、连接数等）。Accumulate 是带锁的合并路径。
//
//     2) FNetTypeStats / FNetStatsContext —— 按"类型 + 阶段"做并行 task-friendly
//        统计。FNetTypeStats 持有：
//          - 1 个 ParentStatsContext（单线程汇聚目标）
//          - N 个 ChildStatsContext（worker 线程在并行阶段独占使用）
//        每帧流程：PreUpdateSetup → 并行 task 用 AcquireChildNetStatsContext
//        拿独占 child → 数据写入 child → 并行结束 ReleaseChildNetStatsContext
//        归还 → ReportCSVStats 内 AccumulateChildrenToParent → 重置子 context。
//
//     3) FReplicationStats —— 跨帧聚合（取平均/最大）的 PendingObject /
//        HugeObjectSendQueue 等高层指标。
//
//   并发模型要点（参见 Stats.md §2.3）：
//     · TypeStatsNames 数组的扩容（GetOrCreateTypeStats）必须在串行阶段进行——
//       禁止在并行阶段扩容，会破坏 ChildStatsContext.TypeStatsData 的索引一致性。
//     · ChildStatsContextCS 临界区只保护"获取/归还/创建"动作；写入数据本身
//       不需要锁——同一时刻一个 child 只属于一个 task。
//     · bIsAcquiredByTask（FNetStatsContext::CanAcquire/Acquire/Release）保证
//       两个 task 不会同时拿到同一个 child。
//
//   所有 CSV category 都包在 WITH_SERVER_CODE 宏里——客户端构建不会编译这些
//   profiler 入口。
//
//   CVar net.Iris.Stats.ShouldIncludeSubObjectWithRoot：
//     · true（默认）—— SubObject 的统计被合并到其 RootObject 的 TypeStats 中
//       （便于按"父类型"聚合一个 Actor 树的全部带宽/耗时）；
//     · false —— SubObject 作为独立的 NetObject 单独记一行 stats。
//   该值在每次 ResetStats / CreateChildNetStatsContext 时被复制到 Context；
//   并行阶段中途修改不会立即生效。
// =============================================================================

#include "Iris/Stats/NetStats.h"
#include "Iris/Stats/NetStatsContext.h"
#include "Iris/Core/IrisCsv.h"
#include "Iris/Core/IrisProfiler.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Async/Fundamental/Scheduler.h"

// LogNetStats 仅供 Iris Stats 子系统使用，与 LogIris 区分以便单独筛选。
DEFINE_LOG_CATEGORY(LogNetStats);

namespace UE::Net::Private
{

// SubObject 是否与 RootObject 合并统计的全局开关。
// true（默认）：在 ChangeMaskCache 等下游写入时，把 SubObject 的字节/耗时
// 计入它的 RootObject TypeStats，这与"游戏中按 Actor 思维分析带宽"一致；
// false：每个 SubObject 都是独立的 NetObject，按各自 TypeStats 分摊。
static bool bCVARShouldIncludeSubObjectWithRoot = true;
static FAutoConsoleVariableRef CShouldIncludeSubObjectWithRoot(
	TEXT("net.Iris.Stats.ShouldIncludeSubObjectWithRoot"),
	bCVARShouldIncludeSubObjectWithRoot,
	TEXT("If enabled SubObjects will reports stats with RootObject, if set to false SubObjects will be treated as separate objects."
	));

// ---------------------------------------------------------------------------
// CSV Category 定义。
// 命名约定：Iris<阶段名><单位>，单位包括 MS（毫秒）/Count（计数）/KBytes（千字节）。
// 8 个阶段（与 FNetTypeStatsData::EStatsIndex 一一对应）：
//   PreUpdate            —— Bridge.PreSendUpdate / Build PollList 等准备耗时
//   Poll                 —— FObjectPoller 拉取 dirty + 复制源状态拷贝
//   PollWaste            —— 已 Poll 但最终未产生有效复制的浪费部分
//   Quantize             —— Source → Quantized 的转换
//   Write                —— ReplicationWriter 实际写入 BitStream（含字节数）
//   WriteWaste           —— Write 失败/被 rollback 的浪费部分
//   WriteCreationInfo    —— 创建/销毁 batch 的字节与计数
//   WriteExports         —— Export（NetToken/Reference 导出）的计数
// 仅在 WITH_SERVER_CODE 下编译——客户端不写这些指标。
// ---------------------------------------------------------------------------
// Per type stats
CSV_DEFINE_CATEGORY(IrisPreUpdateMS, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisPreUpdateCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisPollMS, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisPollCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisPollWasteMS, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisPollWasteCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisQuantizeMS, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisQuantizeCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteMS, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteKBytes, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteWasteMS, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteWasteCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteWasteKBytes, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteCreationInfoCount, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteCreationInfoKBytes, WITH_SERVER_CODE);
CSV_DEFINE_CATEGORY(IrisWriteExportsCount, WITH_SERVER_CODE);

// ---------------------------------------------------------------------------
// FNetSendStats 实现：每帧"摘要"统计。
// ---------------------------------------------------------------------------
void FNetSendStats::Accumulate(const FNetSendStats& Other)
{
#if UE_NET_IRIS_CSV_STATS
	// 多个连接/任务可能同时调用 Accumulate（每连接一份发送摘要，
	// 在 FReplicationSystemImpl 末尾汇总到 RS 级别），因此必须用临界区。
	FScopeLock Lock(&CS);

	// 逐字段相加——所有字段都是简单的累加语义（计数/累计时长/累计字节）。
	Stats.ScheduledForReplicationRootObjectCount += Other.Stats.ScheduledForReplicationRootObjectCount;
	Stats.ReplicatedRootObjectCount += Other.Stats.ReplicatedRootObjectCount;
	Stats.ReplicatedObjectCount += Other.Stats.ReplicatedObjectCount;
	Stats.ReplicatedDestructionInfoCount += Other.Stats.ReplicatedDestructionInfoCount;
	Stats.DeltaCompressedObjectCount += Other.Stats.DeltaCompressedObjectCount;
	Stats.ReplicatedObjectStatesMaskedOut += Other.Stats.ReplicatedObjectStatesMaskedOut;
	Stats.ActiveHugeObjectCount += Other.Stats.ActiveHugeObjectCount;
	Stats.HugeObjectsWaitingForAckCount += Other.Stats.HugeObjectsWaitingForAckCount;
	Stats.HugeObjectsStallingCount += Other.Stats.HugeObjectsStallingCount;

	// 注意：HugeObject 的等待/卡顿时间是浮点秒数，按相同的累加策略处理。
	Stats.HugeObjectWaitingForAckTimeInSeconds += Other.Stats.HugeObjectWaitingForAckTimeInSeconds;
	Stats.HugeObjectStallingTimeInSeconds += Other.Stats.HugeObjectStallingTimeInSeconds;
#endif 
}

void FNetSendStats::Reset()
{
#if UE_NET_IRIS_CSV_STATS
	// Reset 也加锁——避免与 Accumulate 竞争把字段重置成"半旧半新"。
	FScopeLock Lock(&CS);
	Stats = FStats();
#endif
}

void FNetSendStats::ReportCsvStats()
{
#if CSV_PROFILER_STATS
	// ReportCsvStats 通常只在 RS 的 PostSendUpdate 末尾被主线程调用一次，
	// 但仍加锁防御性保护。
	FScopeLock Lock(&CS);

	// Calculate connection averages for some stats
	// 中文：以下指标都按"复制连接数"取平均，便于跨负载横向对比。
	if (Stats.ReplicatingConnectionCount > 0)
	{
		const float ConnectionCountFloat = float(Stats.ReplicatingConnectionCount);
		CSV_CUSTOM_STAT(Iris, AvgScheduledForReplicationRootObjectCount, float(Stats.ScheduledForReplicationRootObjectCount)/ConnectionCountFloat, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedRootObjectCount, float(Stats.ReplicatedRootObjectCount)/ConnectionCountFloat, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedObjectCount, float(Stats.ReplicatedObjectCount)/ConnectionCountFloat, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedDestructionInfoCount, float(Stats.ReplicatedDestructionInfoCount)/ConnectionCountFloat, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedObjectStatesMaskedOut, float(Stats.ReplicatedObjectStatesMaskedOut)/ConnectionCountFloat, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgDeltaCompressedObjectCount, float(Stats.DeltaCompressedObjectCount)/ConnectionCountFloat, ECsvCustomStatOp::Set);
	}
	else
	{
		// 没有任何复制连接：直接写 0 让 CSV 列保持时间序列连续，避免分析工具断点。
		CSV_CUSTOM_STAT(Iris, AvgScheduledForReplicationRootObjectCount, 0.0f, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedRootObjectCount, 0.0f, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedObjectCount, 0.0f, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedDestructionInfoCount, 0.0f, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgReplicatedHugeObjectCount, 0.0f, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgDeltaCompressedObjectCount, 0.0f, ECsvCustomStatOp::Set);
	}

	// Huge object counts
	// 中文：HugeObject 这一组用绝对值（非平均值）报告，因为它们在整个 RS 里
	// 是"全局队列"概念，跟连接数无关。
	CSV_CUSTOM_STAT(Iris, ActiveHugeObjectCount, Stats.ActiveHugeObjectCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(Iris, HugeObjectsWaitingForAckCount, Stats.HugeObjectsWaitingForAckCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(Iris, HugeObjectsStallingCount, Stats.HugeObjectsStallingCount, ECsvCustomStatOp::Set);

	CSV_CUSTOM_STAT(Iris, ReplicatingConnectionCount, Stats.ReplicatingConnectionCount, ECsvCustomStatOp::Set);

	CSV_CUSTOM_STAT(Iris, HugeObjectWaitingForAckTimeInSeconds, Stats.HugeObjectWaitingForAckTimeInSeconds, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(Iris, HugeObjectStallingTimeInSeconds, Stats.HugeObjectStallingTimeInSeconds, ECsvCustomStatOp::Set);
#endif
}

// FNetStatsContext 的默认构造/析构在头文件中只前向声明，这里给出空实现。
// 这样可以把 FNetStatsContext 的成员（如 TArray<FNetTypeStatsData>）
// 的析构所需的完整类型保留在 .cpp 中，避免每个 include 头的人都拉一堆依赖。
FNetStatsContext::FNetStatsContext() = default;
FNetStatsContext::~FNetStatsContext() = default;

// ---------------------------------------------------------------------------
// FNetTypeStats 实现：管理 ParentStatsContext + ChildStatsContext[] 集合。
// ---------------------------------------------------------------------------
FNetTypeStats::FNetTypeStats()
{
	// 1) 创建 ParentStatsContext——这是最终聚合目标，单线程使用，不参与 Acquire/Release。
	ParentStatsContext = CreateNetStatsContext();
	// Add default TypeStats
	// 2) 注册两个内置 TypeStats 槽位（索引固定，下游代码通过 EConstants 引用）：
	//    - "Undefined" → DefaultTypeStatsIndex (0)：未指定类型时的兜底统计；
	//    - "OOBChannel" → OOBChannelTypeStatsIndex (1)：Out-Of-Band 附件
	//      （SendImmediate RPC 等不走对象 Quantize 路径的数据）的统计。
	GetOrCreateTypeStats(TEXT("Undefined"));
	GetOrCreateTypeStats(TEXT("OOBChannel"));
}

FNetTypeStats::~FNetTypeStats()
{
	// 销毁前必须确保不在并行阶段——否则 worker 线程仍可能持有 child context。
	check(!bIsInParallelPhase);
	// 先回收所有 child（按值释放，TArray.Empty()）。
	CleanupChildNetStatsContexts();
	// 再回收 parent。
	delete ParentStatsContext;
}

void FNetTypeStats::Init(FInitParams& InitParams)
{
	// NetRefHandleManager 是 FNetStatsContext 通过 InternalNetRefIndex 反查
	// FReplicationProtocol::TypeStatsIndex 的入口；初始化时要求注入。
	NetRefHandleManager = InitParams.NetRefHandleManager;
	ResetStats();
}

void FNetTypeStats::PreUpdateSetup()
{
	//Create child contexts if needed
	// 中文：在并行阶段开启前预分配足够多的 child context。
	// MaxNumWorkers + 1：包含 GameThread 自己——LowLevelTasks 系统中 GT 也能
	// 作为执行者跑 task。这样每个潜在 worker 都能 Acquire 到一个 child，避免
	// AcquireChildNetStatsContext 的 fallback 路径触发 ensure 警告。
	LowLevelTasks::FScheduler& Scheduler = LowLevelTasks::FScheduler::Get();
	const int32 MaxParallelThreads = Scheduler.GetMaxNumWorkers() + 1;//Include GameThread as Tasks can also run there in addition to Worker Threads
	const int32 NewChildCount = MaxParallelThreads - ChildStatsContext.Num();
	UE_CLOG(NewChildCount > 0, LogNetStats, Log, TEXT("FNetTypeStats Creating %i CreateChildNetStatsContexts. Total: %i"), NewChildCount, MaxParallelThreads);
	for (int32 i = 0; i < NewChildCount; ++i)
	{
		CreateChildNetStatsContext();
	}
}

void FNetTypeStats::ResetStats()
{
	// 重置 parent + 所有 child 的 TypeStatsData——常用于：
	//   - Init 时把所有 context 同步到当前 TypeStatsNames.Num()；
	//   - 调试场景下手动清零。
	UpdateContext(*ParentStatsContext);
	for (FNetStatsContext* ChildStatsContextPtr : ChildStatsContext)
	{
		if (ensure(ChildStatsContextPtr))
		{
			UpdateContext(*ChildStatsContextPtr);
		}
	}
}

int32 FNetTypeStats::GetOrCreateTypeStats(FName Name)
{
	// 简单线性查找——TypeStatsNames 通常很小（< 几十）。
	const int32 ExistingIndex = TypeStatsNames.Find(Name);
	if (ExistingIndex != INDEX_NONE)
	{
		return ExistingIndex;
	}

	// 新增类型槽必须在串行阶段进行：扩容会改变所有 ChildStatsContext.TypeStatsData
	// 的长度，并行阶段中 worker 可能正按旧长度索引数组，触发越界。
	checkf(!bIsInParallelPhase, TEXT("Trying to expand TypeStatsNames from within a parallel phase. This is not supported as the net object list should never expand during a parallel phase"));

	//We're adding a new stat, which means all ChildStatsContexts will need to be expanded to match TypeStatsData
	// 中文：新增 Name → 把 TypeStatsNames 末尾追加；同步把所有 context 的
	// TypeStatsData 数组长度对齐——保证 ParentStatsContext 与每个 ChildStatsContext
	// 的 TypeStatsData[i] 始终指向"同一类型的统计槽"。
	const int32 NewTypeStatsIndex = int32(TypeStatsNames.Num());
	TypeStatsNames.Add(Name);
	ParentStatsContext->TypeStatsData.SetNum(TypeStatsNames.Num());
	for (int32 i = 0; i < ChildStatsContext.Num(); ++i)
	{
		ChildStatsContext[i]->TypeStatsData.SetNum(TypeStatsNames.Num());
	}

	return NewTypeStatsIndex;
}

void FNetTypeStats::UpdateContext(FNetStatsContext& Context)
{
	// 把 Context 的 TypeStatsData 重置（清零）并对齐到当前 TypeStatsNames 长度，
	// 同时同步注入 NetRefHandleManager 与最新的 SubObjectWithRoot 开关值。
	Context.ResetStats(TypeStatsNames.Num());
	Context.NetRefHandleManager = NetRefHandleManager;
	Context.bShouldIncludeSubObjectWithRoot = bCVARShouldIncludeSubObjectWithRoot;
}

FNetStatsContext* FNetTypeStats::CreateNetStatsContext()
{
	// 仅在 ctor 中创建 ParentStatsContext 时使用：分配 + UpdateContext 同步状态。
	FNetStatsContext* Context = new FNetStatsContext;
	UpdateContext(*Context);
	return Context;
}

FNetStatsContext* FNetTypeStats::CreateChildNetStatsContext()
{
	IRIS_PROFILER_SCOPE(FNetTypeStats_CreateChildNetStatsContext);

	// 加锁保护 ChildStatsContext 数组的 Add——可能被 PreUpdateSetup（主线程）
	// 与 AcquireChildNetStatsContext 的 fallback 路径（worker 线程）并发调用。
	FScopeLock Lock(&ChildStatsContextCS);

	//Set up new StatsContext for this ThreadId
	// 中文：注意此处没有用 UpdateContext——为避免 UpdateContext 内调用
	// ResetStats 时与持锁下的其它操作交互，这里直接展开。两者效果一致。
	FNetStatsContext* NewChildStatsContext = new FNetStatsContext;
	NewChildStatsContext->ResetStats(TypeStatsNames.Num());
	NewChildStatsContext->NetRefHandleManager = ParentStatsContext->NetRefHandleManager;
	NewChildStatsContext->bShouldIncludeSubObjectWithRoot = bCVARShouldIncludeSubObjectWithRoot;

	ChildStatsContext.Add(NewChildStatsContext);
	return NewChildStatsContext;
}

FNetStatsContext* FNetTypeStats::AcquireChildNetStatsContext()
{
	int32 AvailableIndex = -1;
	
	// 临界区：扫描数组 + 标记 Acquire 必须原子。
	// 注意这里没用 FScopeLock 是因为下方 fallback 路径要"先解锁、创建、再加锁"，
	// 用 RAII 反而麻烦。
	ChildStatsContextCS.Lock();
	for (int32 SearchIndex = 0; SearchIndex < ChildStatsContext.Num() && AvailableIndex == -1; SearchIndex++)
	{
		// CanAcquire 检查 bIsAcquiredByTask 是否为 false。
		if (ChildStatsContext[SearchIndex]->CanAcquire())
		{
			AvailableIndex = SearchIndex;
			ChildStatsContext[SearchIndex]->Acquire();
		}
	}
	ChildStatsContextCS.Unlock();

	//Fallback to create a new context if we've run out (PreUpdateSetup should have created enough)
	// 中文：fallback 路径——理论上 PreUpdateSetup 已经按 MaxNumWorkers+1 预创建
	// 了足够数量；如果仍然抢不到，说明并发任务数超出预期，发出警告并新建一个。
	// 这种情况会让 ChildStatsContext 数量增加，下次 ReportCSVStats 后下一帧
	// PreUpdateSetup 仍会保留这些新加的 context（不会缩减），下次也能直接复用。
	ensureMsgf(AvailableIndex != -1, TEXT("Too many tasks accessing ChildNetStatsContext, creating a new one. This may cause unexpected memory usage. Current count: %i"), ChildStatsContext.Num());
	if(AvailableIndex == -1)
	{
		FNetStatsContext* NewContext = CreateChildNetStatsContext();
		check(NewContext);
		// 重新加锁标记 Acquire——CreateChildNetStatsContext 内部已经短暂持锁
		// 完成 Add，但此处必须再次持锁后调用 Acquire 才符合不变式。
		ChildStatsContextCS.Lock();
		NewContext->Acquire();
		ChildStatsContextCS.Unlock();
		return NewContext;
	}

	return ChildStatsContext[AvailableIndex];
}

void FNetTypeStats::ReleaseChildNetStatsContext(FNetStatsContext* StatsContext)
{
	// 健壮性：传入 nullptr 视为编程错误（数据丢失），但不 crash——只触发 ensure。
	if(ensureMsgf(StatsContext, TEXT("ReleaseChildNetStatsContext was passed a null StatsContext. Possibility of net stats data being lost.")))
	{
		FScopeLock Lock(&ChildStatsContextCS);
		// 必须当前是 Acquired 状态（CanAcquire 返回 false）才能释放——
		// 重复释放或释放未持有的 context 会触发 check。
		check(!StatsContext->CanAcquire());
		StatsContext->Release();
	}
}

void FNetTypeStats::CleanupChildNetStatsContexts()
{
	// 仅在 dtor 调用——遍历销毁所有 child（不加锁，因为 dtor 不允许并发）。
	for (int32 i = 0; i < ChildStatsContext.Num(); i++)
	{
		delete ChildStatsContext[i];
	}
	ChildStatsContext.Empty();
}

void FNetTypeStats::AccumulateChildrenToParent()
{
	IRIS_PROFILER_SCOPE(FNetTypeStats_AccumulateChildrenToParent);

	// 串行阶段不变式：所有 child 已被 Release，可安全合并。
	checkf(!bIsInParallelPhase, TEXT("Trying to expand Accumulate ChildStatsContexts from within a parallel phase. This is not supported as this expects ChildStatsContexts to remain constant."));

	// 顺序遍历每个 child，把它的 TypeStatsData 合并到 parent，并把 child 自身重置。
	// 用 TIterator 是历史写法——这里只读不删，等价于 ranged-for。
	for(TArray<FNetStatsContext*>::TIterator It = ChildStatsContext.CreateIterator(); It; ++It)
	{
		FNetStatsContext* StatsContext = *It;
		if (ensure(StatsContext))
		{
			Accumulate(*StatsContext);
		}
	}
}

void FNetTypeStats::Accumulate(FNetStatsContext& Context)
{
	IRIS_PROFILER_SCOPE(FNetTypeStats_Accumulate);

	// Skip default context as that is our target.
	// 中文：parent 是合并目标，不能合并到自己（否则会双倍计数）。
	if (&Context == ParentStatsContext)
	{
		return;
	}

	// 防御性检查：child 的 TypeStatsData 不应比 parent 长。
	// 长度差异通常意味着 GetOrCreateTypeStats 中途扩容失败（不可能在并行阶段），
	// 或是外部直接修改了 TypeStatsData——直接 return 跳过避免越界。
	if (!ensureMsgf(Context.TypeStatsData.Num() <= TypeStatsNames.Num(), TEXT("Invalid Context")))
	{
		return;
	}

	// Accumulate stats
	// 中文：逐槽位调用 FNetTypeStatsData::Accumulate（位于 NetStatsContext.h），
	// 把 8 个阶段的 (Time/Bits/Count) 三元组累加到 parent 对应槽。
	const FNetTypeStatsData* Src = Context.TypeStatsData.GetData();
	FNetTypeStatsData* Dst = ParentStatsContext->TypeStatsData.GetData();
	for (int32 StatsIndex = 0, EndIndex = FMath::Min(Context.TypeStatsData.Num(), ParentStatsContext->TypeStatsData.Num()); StatsIndex < EndIndex; ++StatsIndex)
	{
		Dst[StatsIndex].Accumulate(Src[StatsIndex]);
	}

	// 合并完成后立即重置 child——为下一帧并行阶段做好"零起点"。
	Context.ResetStats(TypeStatsNames.Num());
}

// ---------------------------------------------------------------------------
// 局部宏：把 (StatsName, ValueName, StatsData) 的 (Time/Count/Bits) 写到对应
// CSV category。
//   - TIME → 毫秒（FGenericPlatformTime::ToMilliseconds64 把 cycle 计数转为毫秒）
//   - COUNT → int32 计数
//   - BITS → 千字节（向上取整 8bit→1byte，再 /1000 转 KByte）
// 这些宏只在本函数内使用，函数末尾立即 #undef 防止污染。
// ---------------------------------------------------------------------------
#define UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, ValueName, StatsData) FCsvProfiler::RecordCustomStat(StatsName, CSV_CATEGORY_INDEX(Iris##ValueName##MS), FGenericPlatformTime::ToMilliseconds64(StatsData.Values[FNetTypeStatsData::EStatsIndex::ValueName].Time), ECsvCustomStatOp::Set)
#define UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, ValueName, StatsData) FCsvProfiler::RecordCustomStat(StatsName, CSV_CATEGORY_INDEX(Iris##ValueName##Count), static_cast<int32>(StatsData.Values[FNetTypeStatsData::EStatsIndex::ValueName].Count) , ECsvCustomStatOp::Set)
#define UE_NET_STATS_RECORD_TYPESTATS_BITS(StatsName, ValueName, StatsData) FCsvProfiler::RecordCustomStat(StatsName, CSV_CATEGORY_INDEX(Iris##ValueName##KBytes), float((StatsData.Values[FNetTypeStatsData::EStatsIndex::ValueName].Bits + 7U) / 8) / 1000.f , ECsvCustomStatOp::Set)

void FNetTypeStats::ReportCSVStats()
{
#if UE_NET_IRIS_CSV_STATS

	FCsvProfiler* Profiler = FCsvProfiler::Get();
	// 仅当 CSV profiler 当前正在 capture 时才花费时间合并/写入；
	// 否则跳过，避免 release build 中无意义开销。
	bIsEnabled = Profiler->IsCapturing();

	if (bIsEnabled)
	{
		IRIS_PROFILER_SCOPE(FNetTypeStats_ReportCSVStats);

		// Accumulate stats from child contexts into the parent one
		// 中文：先把 child → parent 合并；这一步会清空 child（为下一帧准备）。
		AccumulateChildrenToParent();

		// Report stats for this frame
		// 中文：遍历每个类型槽位，把 8 个阶段的 (Time/Count/Bits) 写到对应 CSV 列。
		// 注意各阶段记录的指标不同：
		//   PreUpdate / Poll / PollWaste / Quantize：Time + Count
		//   Write / WriteWaste：Time + Count + Bits（KBytes）
		//   WriteCreationInfo：Bits + Count（无 Time——创建/销毁是瞬时事件）
		//   WriteExports：仅 Count（Export 字节并入 Write）
		FNetTypeStatsData* TypeStatsDatas = ParentStatsContext->TypeStatsData.GetData();
		for (int32 StatsIndex = 0; StatsIndex < TypeStatsNames.Num(); ++StatsIndex)
		{
			const FName StatsName = TypeStatsNames[StatsIndex];
			FNetTypeStatsData& TypeStatsData = TypeStatsDatas[StatsIndex];

			// Report, we could do a loop here but we might end up not wanting to report all collected stats.
			// 中文：故意展开为"每阶段每指标一行"——便于将来挑选/裁剪某个阶段的报告项
			// 而不必动循环结构（CSV category 是编译期常量，无法用变量索引）。
			UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, PreUpdate, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, PreUpdate, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, Poll, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, Poll, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, PollWaste, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, PollWaste, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, Quantize, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, Quantize, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, Write, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, Write, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_BITS(StatsName, Write, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_TIME(StatsName, WriteWaste, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, WriteWaste, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_BITS(StatsName, WriteWaste, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_BITS(StatsName, WriteCreationInfo, TypeStatsData);
			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, WriteCreationInfo, TypeStatsData);

			UE_NET_STATS_RECORD_TYPESTATS_COUNT(StatsName, WriteExports, TypeStatsData);

			// Reset
			// 中文：本帧已上报，清空 parent 的本类型槽位，准备下一帧累加。
			TypeStatsData.Reset();
		}
	}

#endif
}

// 限定作用域内使用的辅助宏立即解除定义。
#undef UE_NET_STATS_RECORD_TYPESTATS_TIME
#undef UE_NET_STATS_RECORD_TYPESTATS_COUNT
#undef UE_NET_STATS_RECORD_TYPESTATS_BITS

// ---------------------------------------------------------------------------
// FReplicationStats 实现：高级别的"窗口聚合"指标。
// 由 ReplicationSystem 在 PostSendUpdate 时根据 PendingObject/HugeObject 队列
// 长度采样填入；这里负责一次性输出到 CSV。
// ---------------------------------------------------------------------------
void FReplicationStats::ReportCSVStats()
{
#if UE_NET_IRIS_CSV_STATS
	if (SampleCount)
	{
		// 计算窗口平均值与最大值。SampleCount 是窗口内累积的采样次数。
		const double SampleCountDbl = float(SampleCount);
		CSV_CUSTOM_STAT(Iris, AvgPendingObjectCount, double(PendingObjectCount) / SampleCountDbl, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgPendingDependentObjectCount, double(PendingDependentObjectCount) / SampleCountDbl, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgHugeObjectSendQueue, double(HugeObjectSendQueue) / SampleCountDbl, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, MaxPendingObjectCount, int32(MaxPendingObjectCount), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, MaxPendingDependentObjectCount, int32(MaxPendingDependentObjectCount), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, MaxHugeObjectSendQueue, int32(MaxHugeObjectSendQueue), ECsvCustomStatOp::Set);
	}
	else
	{
		// 无采样：仍写 0 保证时序连续（与 FNetSendStats::ReportCsvStats 同样的考虑）。
		CSV_CUSTOM_STAT(Iris, AvgPendingObjectCount, double(0), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgPendingDependentObjectCount, double(0), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, AvgHugeObjectSendQueue, double(0), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, MaxPendingObjectCount, int32(0), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, MaxPendingDependentObjectCount, int32(0), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(Iris, MaxHugeObjectSendQueue, int32(0), ECsvCustomStatOp::Set);
	}
#endif
}

} // end namespace UE::Net::Private
