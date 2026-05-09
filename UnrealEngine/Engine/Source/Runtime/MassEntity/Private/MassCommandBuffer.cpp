// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCommandBuffer.h"
#include "Containers/AnsiString.h"
#include "MassEntityManager.h"
#include "MassObserverManager.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "VisualLogger/VisualLogger.h"


// 【中文】CSV profiler 类别：MassEntities 用作 stat scope，MassEntitiesCounters 用作累加计数器。
// Flush 期间每条命令会上报"该命令类型的处理条数"到 MassEntitiesCounters 类别。
CSV_DEFINE_CATEGORY(MassEntities, true);
CSV_DEFINE_CATEGORY(MassEntitiesCounters, true);
// 【中文】Flush 整体耗时的 cycle stat，归入 STATGROUP_Mass。
DECLARE_CYCLE_STAT(TEXT("Mass Flush Commands"), STAT_Mass_FlushCommands, STATGROUP_Mass);

namespace UE::Mass::Command
{
	/**
	 * Note that we default to `false` because the correctness of the feature's behavior depends on use cases.
	 * If there are no observers watching fragment removal, everything will be great. If not, enabling the feature
	 * will result in the data removed no longer being available when the removal-observers get triggered upon lock's release
	 */
	// 【中文】控制台开关：Flush 期间是否锁住 observer。打开时性能更好（observer 通知被
	// 推迟到锁释放后），但语义会变化——removal-observer 看不到"被删除前"的数据。默认关。
	bool bLockObserversDuringFlushing = false;
	FAutoConsoleVariableRef CVarLockObserversDuringFlushing(TEXT("mass.commands.LockObserversDuringFlushing"), bLockObserversDuringFlushing
		, TEXT("Controls whether observers will get locked during commands flushing."), ECVF_Default);

#if CSV_PROFILER_STATS
	// 【中文】是否上报"每命令类型一条 stat"的细粒度统计。关闭时所有命令共用一个名字
	// "BatchedCommand"，避免 stat 表过度膨胀；打开后每种命令独立成行，便于性能分析。
	bool bEnableDetailedStats = false;

	FAutoConsoleVariableRef CVarEnableDetailedCommandStats(TEXT("massentities.EnableCommandDetailedStats"), bEnableDetailedStats,
		TEXT("Set to true create a dedicated stat per type of command."), ECVF_Default);

	/** CSV stat names */
	// 【中文】未启用 detailed 时使用的默认 stat 名（FString 版本，用于 RecordCustomStat）
	static FString DefaultBatchedName = TEXT("BatchedCommand");
	// 【中文】detailed 模式下每种命令名字 → (FString, FAnsiString) 的缓存表，
	// 避免每条命令 Flush 都做一次 FName→string 的转换。
	static TMap<FName, TPair<FString, FAnsiString>> CommandBatchedFNames;

	/** CSV custom stat names (ANSI) */
	// 【中文】未启用 detailed 时使用的默认 ANSI 名（FScopedCsvStat 接受 ANSI 字符串）
	static FAnsiString DefaultANSIBatchedName = "BatchedCommand";

	/**
	 * Provides valid names for CSV profiling.
	 * @param Command is the command instance
	 * @param OutName is the name to use for csv custom stats
	 * @param OutANSIName is the name to use for csv stats
	 */
	// 【中文】根据命令实例和 detailed 开关，返回正确的 stat 名（指针指向静态/缓存的字符串）。
	void GetCommandStatNames(FMassBatchedCommand& Command, FString*& OutName, FAnsiString*& OutANSIName)
	{
		OutANSIName = &DefaultANSIBatchedName;
		OutName     = &DefaultBatchedName;
		if (!bEnableDetailedStats)
		{
			return;
		}

		const FName CommandFName = Command.GetFName();

		TPair<FString, FAnsiString>& Names = CommandBatchedFNames.FindOrAdd(CommandFName);
		OutName     = &Names.Get<FString>();
		OutANSIName = &Names.Get<FAnsiString>();
		if (OutName->IsEmpty())
		{
			*OutName     = CommandFName.ToString();
			*OutANSIName = **OutName;
		}
	}
#endif
} // UE::Mass::Command

//-----------------------------------------------------------------------------
// FMassBatchedCommand
//-----------------------------------------------------------------------------
// 【中文】FMassBatchedCommand::CommandsCounter 的定义。
// 这是个进程级 atomic uint32，用于在第一次实例化某个命令类型 T 时递增分配出唯一序号
// (见 GetCommandIndex<T>())，作为 FMassCommandBuffer::CommandInstances 数组的下标。
// 由于使用静态 local 变量 + atomic，注册过程线程安全且 O(1)。
std::atomic<uint32> FMassBatchedCommand::CommandsCounter;

//-----------------------------------------------------------------------------
// FMassCommandBuffer
//-----------------------------------------------------------------------------
// 【中文】构造时记录 owner 线程；后续 push 必须在该线程上完成。
FMassCommandBuffer::FMassCommandBuffer()
	: OwnerThreadId(FPlatformTLS::GetCurrentThreadId())
{	
}

// 【中文】析构时如果还有未 Flush 命令，触发 ensure 警告 —— 因为这些操作再也不会被执行了，
// 通常意味着"buffer 在拥有它的 system 关闭/重建前没有被正确 Flush"。
FMassCommandBuffer::~FMassCommandBuffer()
{
	ensureMsgf(HasPendingCommands() == false, TEXT("Destroying FMassCommandBuffer while there are still unprocessed commands. These operations will never be performed now."));

	CleanUp();
}

// 【中文】把 OwnerThreadId 重设为当前线程：仅用于 buffer 在线程间合法迁移
// （如 ParallelFor worker 切换、server fork）。详见头文件警告。
void FMassCommandBuffer::ForceUpdateCurrentThreadID()
{
	OwnerThreadId = FPlatformTLS::GetCurrentThreadId();
}

// 【中文】Flush —— 命令"事务提交"主入口。
// 步骤：
//   1) 标记 bIsFlushing；如果没有待处理命令，直接返回；
//   2) 用预定义的 CommandTypeOrder 表把每条命令映射到一个 group 号；
//   3) 把 CommandInstances + AppendedCommandInstances 一起按 group 稳定排序；
//   4) 可选地获取 ObserverLock / CreationLock，按指定时机释放；
//   5) 依次 Run() 每条命令并 Reset() 释放命令内部参数（CommandInstances 留壳供下次复用，
//      AppendedCommandInstances 直接清空）。
bool FMassCommandBuffer::Flush(FMassEntityManager& EntityManager)
{
	check(!bIsFlushing);
	// 【中文】TGuardValue 在作用域结束时自动还原 bIsFlushing → 异常安全。
	TGuardValue FlushingGuard(bIsFlushing, true);

	// short-circuit exit
	if (HasPendingCommands() == false)
	{
		return false;
	}

	UE_MT_SCOPED_WRITE_ACCESS(PendingBatchCommandsDetector);
	LLM_SCOPE_BYNAME(TEXT("Mass/FlushCommands"));
	SCOPE_CYCLE_COUNTER(STAT_Mass_FlushCommands);

	// array used to group commands depending on their operations. Based on EMassCommandOperationType
	// 【中文】把 EMassCommandOperationType 枚举值映射到"执行顺序组号"。
	// 排序后 group 号小的先执行：
	//   None=MAX-1（最末尾，作为 fallback）
	//   Create=0           ：先创建，让后续 Add/Set 命令能找到这些新 entity
	//   Add=2              ：再补 fragment / tag
	//   ChangeComposition=3：原子 add+remove（如 SwapTags）
	//   Set=4              ：写入已存在 fragment 的值
	//   Remove=6  Destroy=6：最后做"减法"，避免误删后续命令仍引用的 entity
	// 注意：Remove 和 Destroy 共用 6，所以它们之间的相对顺序由 stable sort + 数组中的
	// 原始顺序决定。
	constexpr int32 CommandTypeOrder[static_cast<uint8>(EMassCommandOperationType::MAX)] =
	{
		MAX_int32 - 1, // None
		0, // Create
		2, // Add
		6, // Remove
		3, // ChangeComposition
		4, // Set
		6, // Destroy
	};

	/**
	 * The following three types of commands are the ones where we cannot guarantee the new behavior
	 * will be consistent with the pre-change behavior.
	 * Before the change, every removal-observer gets notified before the data is actually removed,
	 * which means the observer can access the data-about-to-be-removed. Now, if removal happens while
	 * an observer lock is active, then the removal-observers will get notified after the fact.
	 * For now we're going to support the old behavior. 
	 */
	// 【中文】到达"破坏性"命令组之前必须释放 observer lock，否则 removal-observer
	// 看不到原数据。这里取 Remove/ChangeComposition/Destroy 三个 group 号的最小值。
	constexpr int32 CommandTypeGroupToReleaseObserverLock = FMath::Min3(
		CommandTypeOrder[static_cast<uint8>(EMassCommandOperationType::Remove)]
		, CommandTypeOrder[static_cast<uint8>(EMassCommandOperationType::ChangeComposition)]
		, CommandTypeOrder[static_cast<uint8>(EMassCommandOperationType::Destroy)]
	);

	// 【中文】排序辅助结构：保留命令在两个原数组中的全局下标 + 它的 group 号。
	// IsValid==false 表示该命令"无事可做"（HasWork 为 false / 命令为空），将被排到最后并跳过。
	struct FBatchedCommandsSortedIndex
	{
		FBatchedCommandsSortedIndex(const int32 InIndex, const int32 InGroupOrder)
			: Index(InIndex), GroupOrder(InGroupOrder)
		{}

		const int32 Index = -1;
		const int32 GroupOrder = MAX_int32;
		bool IsValid() const { return GroupOrder < MAX_int32; }
		bool operator<(const FBatchedCommandsSortedIndex& Other) const { return GroupOrder < Other.GroupOrder; }
	};
		
	TArray<FBatchedCommandsSortedIndex> CommandsOrder;
	const int32 OwnedCommandsCount = CommandInstances.Num();

	CommandsOrder.Reserve(OwnedCommandsCount);
	// 【中文】先把 CommandInstances（按类型唯一）登记进排序数组：下标 i ∈ [0, OwnedCommandsCount)
	for (int32 i = 0; i < OwnedCommandsCount; ++i)
	{
		const TUniquePtr<FMassBatchedCommand>& Command = CommandInstances[i];
		CommandsOrder.Add(FBatchedCommandsSortedIndex(i, (Command && Command->HasWork())? CommandTypeOrder[(int)Command->GetOperationType()] : MAX_int32));
	}
	// 【中文】再把 AppendedCommandInstances 接在后面：下标统一加上 OwnedCommandsCount，
	// 后面恢复原数组时再减回来即可分辨它属于哪个数组。
	for (int32 i = 0; i < AppendedCommandInstances.Num(); ++i)
	{
		const TUniquePtr<FMassBatchedCommand>& Command = AppendedCommandInstances[i];
		CommandsOrder.Add(FBatchedCommandsSortedIndex(i + OwnedCommandsCount, (Command && Command->HasWork()) ? CommandTypeOrder[(int)Command->GetOperationType()] : MAX_int32));
	}
	// 【中文】StableSort 确保同 group 内命令保持原相对顺序——这对 Remove/Destroy（共用 group 6）尤为重要。
	CommandsOrder.StableSort();

	// 【中文】可选：在 Flush 期间锁住 observer / creation context，让多条命令产生的
	// observer 通知合并成一波，避免边删边通知带来的数据竞争与重复成本。
	TSharedPtr<FMassObserverManager::FObserverLock> ObserverLock;
	TSharedPtr<FMassObserverManager::FCreationContext> CreationLock;
	if (UE::Mass::Command::bLockObserversDuringFlushing 
		&& CommandsOrder[0].GroupOrder < CommandTypeGroupToReleaseObserverLock)
	{
		ObserverLock = EntityManager.GetOrMakeObserversLock();
		//  we only want to create CreationLock if the very first command is of `Create` type.
		// 【中文】只有当第一条命令是 Create 时才需要 CreationLock —— Create 完成后就可以释放。
		if (CommandsOrder[0].GroupOrder == CommandTypeOrder[static_cast<uint8>(EMassCommandOperationType::Create)])
		{
			CreationLock = EntityManager.GetOrMakeCreationContext();
		}
	}
	bool bObserversLock = ObserverLock.IsValid();
	bool bCreationLock = CreationLock.IsValid();

	for (int32 k = 0; k < CommandsOrder.Num() && CommandsOrder[k].IsValid(); ++k)
	{
		// 【中文】跨过 Create 组后释放 CreationLock；跨过破坏性组前释放 ObserverLock。
		// 这两个 if 确保在 Flush 推进过程中按合适的"阶段"逐步释放锁。
		if (bCreationLock && CommandsOrder[k].GroupOrder > 0)
		{
			bCreationLock = false;
			CreationLock.Reset();
		}
		if (bObserversLock && CommandsOrder[k].GroupOrder >= CommandTypeGroupToReleaseObserverLock)
		{
			bObserversLock = false;
			ObserverLock.Reset();
		}

		const int32 CommandIndex = CommandsOrder[k].Index;
		// 【中文】根据下标判断在哪个原数组：< OwnedCommandsCount 取自 CommandInstances，
		// 否则减回偏移取自 AppendedCommandInstances。
		TUniquePtr<FMassBatchedCommand>& Command = CommandIndex < OwnedCommandsCount
			? CommandInstances[CommandIndex]
			: AppendedCommandInstances[CommandIndex - OwnedCommandsCount];
		check(Command)

#if CSV_PROFILER_STATS
		using namespace UE::Mass::Command;

		// Extract name (default or detailed)
		FAnsiString* ANSIName = nullptr;
		FString*     Name     = nullptr;
		GetCommandStatNames(*Command, Name, ANSIName);

		// Push stats
		// 【中文】FScopedCsvStat 度量该命令 Run() 的耗时；RecordCustomStat 累加它处理的实体数。
		FScopedCsvStat ScopedCsvStat(**ANSIName, CSV_CATEGORY_INDEX(MassEntities));
		FCsvProfiler::RecordCustomStat(**Name, CSV_CATEGORY_INDEX(MassEntitiesCounters), Command->GetNumOperationsStat(), ECsvCustomStatOp::Accumulate);
#endif // CSV_PROFILER_STATS

		// 【中文】真正执行命令并清空它累积的参数。CommandInstances 中的命令 Reset 后
		// 仍然留在数组里供下次 push 复用（避免反复 new/delete）。
		Command->Run(EntityManager);
		Command->Reset();
	}

	// 【中文】Appended 命令是"一次性"的，整体清空（Reset 释放容器但保留容量）。
	AppendedCommandInstances.Reset();

	ActiveCommandsCounter = 0;

	return true;
}
 
// 【中文】彻底丢弃所有命令实例。CancelCommands() 与析构都会调用。
void FMassCommandBuffer::CleanUp()
{
	CommandInstances.Reset();
	AppendedCommandInstances.Reset();

	ActiveCommandsCounter = 0;
}

// 【中文】从另一个 buffer "搬"命令进来。注意：
//   - 全部命令都被搬到 AppendedCommandInstances（按顺序，无去重）—— 因为 CommandInstances
//     的下标含义是 "GetCommandIndex<T>()"，本 buffer 已经按这个下标管理自己同类型命令的
//     单例，简单 Append 进 CommandInstances 会破坏 "下标 = GetCommandIndex<T>()" 的不变量。
//   - 用 AppendingCommandsCS 锁保护，调用方可以在多线程中并发 MoveAppend 多个 worker buffer
//     到主 buffer。
//   - 读 other 用 SCOPED_READ_ACCESS，写 self 用 SCOPED_WRITE_ACCESS，配合 TS-RW detector 检测违例。
void FMassCommandBuffer::MoveAppend(FMassCommandBuffer& Other)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandBuffer_MoveAppend);

	// @todo optimize, there's surely a way to do this faster than this.
	UE_MT_SCOPED_READ_ACCESS(Other.PendingBatchCommandsDetector);
	if (Other.HasPendingCommands())
	{
		UE::TScopeLock Lock(AppendingCommandsCS);
		UE_MT_SCOPED_WRITE_ACCESS(PendingBatchCommandsDetector);
		AppendedCommandInstances.Append(MoveTemp(Other.CommandInstances));
		AppendedCommandInstances.Append(MoveTemp(Other.AppendedCommandInstances));
		ActiveCommandsCounter += Other.ActiveCommandsCounter;
		Other.ActiveCommandsCounter = 0;
	}
}

// 【中文】统计两个数组里所有命令各自申请的内存 + CommandInstances 数组本身的容量。
// 注：当前实现没有把 AppendedCommandInstances.GetAllocatedSize() 也加上 —— 见末尾"疑似 bug"。
SIZE_T FMassCommandBuffer::GetAllocatedSize() const
{
	SIZE_T TotalSize = 0;
	for (const TUniquePtr<FMassBatchedCommand>& Command : CommandInstances)
	{
		TotalSize += Command ? Command->GetAllocatedSize() : 0;
	}
	for (const TUniquePtr<FMassBatchedCommand>& Command : AppendedCommandInstances)
	{
		TotalSize += Command ? Command->GetAllocatedSize() : 0;
	}

	TotalSize += CommandInstances.GetAllocatedSize();
	
	return TotalSize;
}

