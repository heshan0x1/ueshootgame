// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/CsvProfilerConfig.h"
#include "AutoRTFM.h"
#include "Misc/MTTransactionallySafeAccessDetector.h"
#include "MassEntityTypes.h"
#include "MassEntityUtils.h"
#include "MassEntityManager.h"
#include "MassDebuggerBreakpoints.h"
#include "MassCommands.generated.h"

/**
 * Enum used by MassBatchCommands to declare their "type". This data is later used to group commands so that command 
 * effects are applied in a controllable fashion 
 * Important: if changed make sure to update FMassCommandBuffer::Flush.CommandTypeOrder as well
 */
// 【中文】命令"操作类型"枚举 —— 决定 Flush 时各类命令的执行顺序。
// Flush 会按下面的次序对命令做稳定排序后逐条执行：
//   Create → Add → ChangeComposition → Set → Remove/Destroy（None 总是最后）
// 这个顺序的合理性：
//   - Create 必须最先：后续 Add/Set 命令可能要往这次新创建的 entity 上挂 fragment；
//     若先 Add 后 Create，新 entity 还不存在，Add 会找不到目标。
//   - Add 在 Remove 之前：避免对同一帧内同一 entity 的 add 立刻被 remove 抵消
//     掉的同时，archetype 在内存里来回抖动（每次结构变更都意味着 entity 被搬到
//     另一个 archetype chunk）。
//   - Set 在 Add 之后：Set 命令通常假设 fragment 已经在 entity 上；如果用户
//     同时 push 了 Add+Set，得先 Add 把 fragment 加上才能成功 Set。
//   - Remove/Destroy 最后：避免误删后续命令仍需引用的 entity / fragment。
//
// !!! 重要：如修改本枚举顺序或新增项，必须同步更新
//     FMassCommandBuffer::Flush() 中的 CommandTypeOrder 数组（cpp 文件）。
UENUM()
enum class EMassCommandOperationType : uint8
{
	None,				// default value. Commands marked this way will be always executed last. Programmers are encouraged to instead use one of the meaningful values below.
						// 【中文】未指定 → 默认值，被映射到极大 group，事实上落在最后；不建议使用，应在自定义命令时显式指定语义。
	Create,				// signifies commands performing entity creation
						// 【中文】"创建" entity 的命令（如 FMassCommandBuildEntity）。Flush 第一阶段执行。
	Add,				// signifies commands adding fragments or tags to entities
						// 【中文】"加法"：往已有 entity 添加 fragment / tag。
	Remove,				// signifies commands removing fragments or tags from entities
						// 【中文】"减法"：移除 fragment / tag。被排到最后阶段，避免破坏前面命令的引用。
	ChangeComposition,	// signifies commands both adding and removing fragments and/or tags from entities
						// 【中文】同时加和减（如 SwapTags），原子地切换 archetype，避免中间过渡态。
	Set,				// signifies commands setting values to pre-existing fragments. The fragments might be added if missing,
						// depending on specific command, so this group will always be executed after the Add group
						// 【中文】给已有 fragment 写入数据值（不一定改变 archetype）。
	Destroy,			// signifies commands removing entities
						// 【中文】销毁 entity。最后阶段执行。
	MAX
};

// 【中文】用于"是编译期检查 fragment/tag 类型"还是"运行期检查"的开关：
//   - CompileTimeCheck：通过 static_assert + 编译期 BitSet 构造，零运行成本，类型必须在编译期已知；
//   - RuntimeCheck   ：通过 IsA<FMassFragment/FMassTag>() + 运行期遍历 StaticStruct 构造 BitSet，
//                       适合反射场景下用 UScriptStruct* 表达的类型。
enum class EMassCommandCheckTime : bool
{
	RuntimeCheck = true,
	CompileTimeCheck = false
};

// 【中文】调试/CSV 用的命名宏（仅 Debug 构建或开启 CSV profiler 时启用）。
//   命令 ctor 通过它把命令名字写到 DebugName，供 stat 和断点调试系统识别。
//   关闭时全部展开为空，零开销。
#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
#	define DEBUG_NAME(Name) , FName(TEXT(Name))
#	define DEBUG_NAME_PARAM(Name) , const FName InDebugName = TEXT(Name)
#	define FORWARD_DEBUG_NAME_PARAM , InDebugName
#else
#	define DEBUG_NAME(Name)
#	define DEBUG_NAME_PARAM(Name)
#	define FORWARD_DEBUG_NAME_PARAM
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

namespace UE::Mass::Utils
{
	// 【中文】根据"编译期/运行期"的策略，构造一个 BitSetType，把 TTypes... 全部置位。
	// 这是命令类构造时常用的辅助：把命令的"目标 fragment / tag 类型集合"压成一个 bitset，
	// 后续 BatchChangeFragmentCompositionForEntities / BatchChangeTagsForEntities 直接吃 bitset。
	template<typename BitSetType, EMassCommandCheckTime CheckTime, typename... TTypes>
	BitSetType ConstructBitSet()
	{
		if constexpr (CheckTime == EMassCommandCheckTime::RuntimeCheck)
		{
			// 【中文】运行期分支：通过 StaticStruct() 拿到 UScriptStruct*，由 BitSetType 的 ctor
			// 内部反查类型 → 位下标。允许 T 不是 BitSetType 模板系统识别的类型。
			return BitSetType({ TTypes::StaticStruct()... });
		}
		else
		{
			// 【中文】编译期分支：直接利用 TMultiTypeList::PopulateBitSet 把每个 T 的位下标写进结果。
			// 完全没有运行期反射成本。
			BitSetType Result;
			UE::Mass::TMultiTypeList<TTypes...>::PopulateBitSet(Result);
			return Result;
		}
	}

	// 【中文】Fragment 专用便捷封装。
	template<EMassCommandCheckTime CheckTime, typename... TTypes>
	FMassFragmentBitSet ConstructFragmentBitSet()
	{
		return ConstructBitSet<FMassFragmentBitSet, CheckTime, TTypes...>();
	}

	// 【中文】Tag 专用便捷封装。
	template<EMassCommandCheckTime CheckTime, typename... TTypes>
	FMassTagBitSet ConstructTagBitSet()
	{
		return ConstructBitSet<FMassTagBitSet, CheckTime, TTypes...>();
	}
} // namespace UE::Mass::Utils

namespace UE::Mass::Command
{
	// 【中文】命令类型 traits，可被部分特化以声明命令的"特殊处理需求"。
	// 当前唯一的项是 RequiresUniqueHandling：
	//   - false（默认）：命令通过类型唯一辨识，可走 PushCommand→CreateOrAddCommand 复用路径。
	//   - true         ：命令实例携带状态（如 ElementType），同类型不同实例语义不同，
	//                    无法基于"类型"做去重。这种命令必须用 PushUniqueCommand 入队，
	//                    存进 AppendedCommandInstances 数组，按顺序保留。
	template<typename T>
	struct TCommandTraits final
	{
		enum
		{
			RequiresUniqueHandling = false
		};
	};
};

// 【中文】FMassBatchedCommand —— 所有"批量命令"的基类。
// 设计模式说明：
//   - 它本身只持有 (OperationType, bHasWork, [DebugName])，是命令最小骨架；
//   - 派生类（如 FMassBatchedEntityCommand）通过持有 TArray 等容器累积同类型命令的参数，
//     把"对 N 个 entity 做同一种事"压缩成一次 Run() 调用，避免每个 entity 单独走一遍流程；
//   - GetCommandIndex<T>() 用 static-local + atomic counter 做"每命令类型一个唯一序号"，
//     专给 FMassCommandBuffer::CommandInstances 当数组下标。
struct FMassBatchedCommand
{
	FMassBatchedCommand() = default;
	// 【中文】常规构造：派生类必须告诉基类自己属于哪种 EMassCommandOperationType，
	// Flush 排序就靠它。
	explicit FMassBatchedCommand(EMassCommandOperationType OperationType)
		: OperationType(OperationType)
	{}
#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	// 【中文】Debug/CSV 构造：额外接收命令的可读名（用于 profiler 与调试器展示）。
	FMassBatchedCommand(EMassCommandOperationType OperationType, FName DebugName)
		: OperationType(OperationType)
		, DebugName(DebugName)
	{}
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	// 【中文】析构兜底 Reset，避免派生类容器有未释放资源。
	virtual ~FMassBatchedCommand()
	{
		Reset();
	}

	// 【中文】已废弃接口（5.7 deprecate, 5.9 删除）。Mass 5.7 把命令执行从 const 函数 Execute
	// 改成可写的 Run，原因是命令需要在执行时修改自身（例如 Reset 容器）。新代码必须 override Run。
	UE_DEPRECATED(5.7, "Mass Commands: CONST Execute function is deprecated in 5.7 and will be removed by 5.9. Use Run instead.")
	virtual void Execute(FMassEntityManager& EntityManager) const
	{
		ensureMsgf(false, TEXT("FMassBatchedCommand::Execute is DEPRECATED, override Run function instead."));
	}

	// 【中文】命令"执行"主入口：Flush 阶段被调用，把累积的参数批量提交给 EntityManager。
	// 默认实现转调旧的 Execute，保持兼容；派生类必须 override 这一函数。
	virtual void Run(FMassEntityManager& EntityManager)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Execute(EntityManager);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// 【中文】Run 完成后 Flush 调用：清空命令内部累积的参数，但实例本身保留供下次复用。
	// 派生类需要 override 来重置自己的容器（并调用 Super::Reset()）。
	virtual void Reset()
	{
		bHasWork = false;
	}

	// 【中文】Flush 中"快速跳过空命令"的依据：派生类在累积参数时把 bHasWork 设为 true，
	// Reset 时归 false。
	bool HasWork() const { return bHasWork; }
	EMassCommandOperationType GetOperationType() const { return OperationType; }
	
	// 【中文】每个具体命令类型 T 的"全局唯一索引"。原理：
	//   - static const uint32 ThisTypesStaticIndex = CommandsCounter++; 这一行只在 T 第一次
	//     被实例化时执行一次（C++ 静态局部变量保证）；
	//   - CommandsCounter 是 atomic uint32，多线程并发取号也安全；
	//   - 之后无论谁调用 GetCommandIndex<T>() 都返回同一个值。
	// FMassCommandBuffer::CreateOrAddCommand<T>() 用它当数组下标，O(1) 复用同类命令。
	// UE_AUTORTFM_ALWAYS_OPEN：保证此函数在 AutoRTFM 事务中按 Open 模式执行（不被回滚）。
	template<typename T>
	UE_AUTORTFM_ALWAYS_OPEN
	static uint32 GetCommandIndex()
	{
		static const uint32 ThisTypesStaticIndex = CommandsCounter++;
		return ThisTypesStaticIndex;
	}

	// 【中文】返回命令内部容器累积的字节数，用于内存统计 (FMassCommandBuffer::GetAllocatedSize)。
	virtual SIZE_T GetAllocatedSize() const = 0;

#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	// 【中文】CSV stat 用：本次将处理多少条"操作"（typically TargetEntities.Num()）。
	virtual int32 GetNumOperationsStat() const = 0;
	FName GetFName() const { return DebugName; }
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

protected:
	// @todo note for reviewers - I could use an opinion if having a virtual function per-command would be a more 
	// preferable way of asking commands if there's anything to do.
	// 【中文】是否有累积工作待做。派生类的 Add() 中置 true，Reset() 中置 false。
	// 命令的"无事可做"状态 (HasWork==false) 在 Flush 排序时被排到最末并跳过。
	bool bHasWork = false;
	// 【中文】操作类型。决定本命令在 Flush 中的执行阶段。
	EMassCommandOperationType OperationType = EMassCommandOperationType::None;

#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	// 【中文】调试/profiler 中显示的命令名。仅 debug 构建/CSV 启用时占用空间。
	FName DebugName;
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

private:
	// 【中文】GetCommandIndex<T>() 共享的全局原子计数器（定义在 .cpp）。
	static MASSENTITY_API std::atomic<uint32> CommandsCounter;
};

// ============================================================================
// 【中文】FMassBatchedEntityCommand —— 以"目标 entity 列表"为基础的命令通用基类。
// 大多数内置命令（添加/移除 fragment、添加/移除 tag、销毁 entity 等）都派生自它。
// 累积的参数最少是 TArray<FMassEntityHandle> TargetEntities；派生类可在此基础上
// 增加更多并行数组（如 FMassCommandAddFragmentInstances 的 Fragments TMultiArray）。
// ============================================================================
struct FMassBatchedEntityCommand : public FMassBatchedCommand
{
	using Super = FMassBatchedCommand;

	FMassBatchedEntityCommand() = default;
	explicit FMassBatchedEntityCommand(EMassCommandOperationType OperationType DEBUG_NAME_PARAM("BatchedEntityCommand"))
		: Super(OperationType FORWARD_DEBUG_NAME_PARAM)
	{}

	// 【中文】单个 entity 累加：被 FMassCommandBuffer::PushCommand 一次次调用，
	// 把目标 entity 追加到 TargetEntities 数组。
	void Add(FMassEntityHandle Entity)
	{
		UE_MT_SCOPED_WRITE_ACCESS(EntitiesAccessDetector);
		TargetEntities.Add(Entity);
		bHasWork = true;
	}

	// 【中文】数组形式的累加（拷贝）：用于 DestroyEntities(view) 等批量入参。
	void Add(TConstArrayView<FMassEntityHandle> Entities)
	{
		UE_MT_SCOPED_WRITE_ACCESS(EntitiesAccessDetector);
		TargetEntities.Append(Entities.GetData(), Entities.Num());
		bHasWork = true;
	}

	// 【中文】数组形式（move）：调用方可以 MoveTemp 整个 TArray 进来，零拷贝。
	void Add(TArray<FMassEntityHandle>&& Entities)
	{
		UE_MT_SCOPED_WRITE_ACCESS(EntitiesAccessDetector);
		TargetEntities.Append(Forward<TArray<FMassEntityHandle>>(Entities));
		bHasWork = true;
	}

protected:
	// 【中文】内存统计：仅 entity 数组（派生类会再叠加自己的）。
	virtual SIZE_T GetAllocatedSize() const
	{
		return TargetEntities.GetAllocatedSize();
	}

	// 【中文】Reset 时清空 entity 列表，但保留容量便于下一帧复用。
	virtual void Reset() override
	{
		TargetEntities.Reset();
		Super::Reset();
	}

#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	virtual int32 GetNumOperationsStat() const override { return TargetEntities.Num(); }
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

	// 【中文】对 TargetEntities 的并发访问检测（Debug-only）。命令的 Add 应仅由 owner 线程调用。
	UE_MT_DECLARE_TS_RW_ACCESS_DETECTOR(EntitiesAccessDetector); 
	// 【中文】累积的目标 entity 列表。派生类的 Run() 一般会先 CreateEntityCollections
	// 把同 archetype 的 entity 折叠在一起，再做批量结构变更。
	TArray<FMassEntityHandle> TargetEntities;
};

//-----------------------------------------------------------------------------
// Entity destruction
//-----------------------------------------------------------------------------
// 【中文】销毁 entity 命令。OperationType=Destroy → Flush 时落到最后阶段执行。
// Run() 内部把 TargetEntities 按所在 archetype 分组后，调用 BatchDestroyEntityChunks
// 一次性销毁，避免逐 entity 触发 archetype 重组。
struct FMassCommandDestroyEntities : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;

	FMassCommandDestroyEntities()
		: Super(EMassCommandOperationType::Destroy DEBUG_NAME("DestroyEntities"))
	{
	}

#if WITH_MASSENTITY_DEBUG
	// 【中文】调试器断点钩子：用户可在调试器里指定"销毁某 entity 时停下"。
	// PushCommand 时框架会调用本函数询问是否命中。
	template<typename T>
	static bool CheckBreakpoints(T Entity)
	{
		return UE::Mass::Debug::FBreakpoint::CheckDestroyEntityBreakpoints(Entity);
	}
#endif // WITH_MASSENTITY_DEBUG

protected:
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandDestroyEntities_Execute);

		// 【中文】FoldDuplicates：同一 entity 被 push 两次也只销毁一次。
		// CreateEntityCollections 把 entity 按所在 archetype 聚合成 collection，
		// EntityManager 再以 collection 为粒度高效操作（同 archetype 内可批量内存操作）。
		TArray<FMassArchetypeEntityCollection> EntityCollectionsToDestroy;
		UE::Mass::Utils::CreateEntityCollections(EntityManager, TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates, EntityCollectionsToDestroy);
		EntityManager.BatchDestroyEntityChunks(EntityCollectionsToDestroy);
	}
};

//-----------------------------------------------------------------------------
// Simple fragment composition change
//-----------------------------------------------------------------------------
// 【中文】"添加若干 fragment 类型"命令模板。
// 模板参数：
//   CheckTime   ：编译期/运行期 fragment 类型校验；
//   TTypes...   ：要添加的 fragment 类型列表（可一次添加多个）。
// 命令构造时立刻把 TTypes 压成 FragmentsAffected bitset；后续每次 PushCommand
// 只往 TargetEntities 追加 entity，FragmentsAffected 不变（同类型命令复用同一实例）。
// Flush 时一次性给所有 entity 添加这组 fragment。
template<EMassCommandCheckTime CheckTime, typename... TTypes>
struct FMassCommandAddFragmentsInternal : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;
	FMassCommandAddFragmentsInternal()
		: Super(EMassCommandOperationType::Add DEBUG_NAME("AddFragments"))
		, FragmentsAffected(UE::Mass::Utils::ConstructFragmentBitSet<CheckTime, TTypes...>())
	{}

#if WITH_MASSENTITY_DEBUG
	// 【中文】Add Fragment 断点：任何挂在这些 fragment / 这个 entity 上的"add"断点都会触发。
	template<typename T>
	static bool CheckBreakpoints(T Entity, TTypes... InFragments)
	{
		return UE::Mass::Debug::FBreakpoint::CheckFragmentAddBreakpoints(Entity, Forward<TTypes>(InFragments)...);
	}
#endif // WITH_MASSENTITY_DEBUG

protected:
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandAddFragments_Execute);
		TArray<FMassArchetypeEntityCollection> EntityCollections;
		UE::Mass::Utils::CreateEntityCollections(EntityManager, TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates, EntityCollections);
		// 【中文】Add 用第二个 bitset (要添加) 非空，第三个 bitset (要移除) 留空。
		EntityManager.BatchChangeFragmentCompositionForEntities(EntityCollections, FragmentsAffected, FMassFragmentBitSet());
	}
	// 【中文】要添加的 fragment 类型集合，编译期常量（构造时一次性算好）。
	FMassFragmentBitSet FragmentsAffected;
};

// 【中文】常用别名：默认编译期检查。用户代码中通常直接用 FMassCommandAddFragments<FFoo, FBar>。
template<typename... TTypes>
using FMassCommandAddFragments = FMassCommandAddFragmentsInternal<EMassCommandCheckTime::CompileTimeCheck, TTypes...>;

// 【中文】"移除若干 fragment 类型" —— 与 Add 对称，OperationType=Remove。
template<EMassCommandCheckTime CheckTime, typename... TTypes>
struct FMassCommandRemoveFragmentsInternal : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;
	FMassCommandRemoveFragmentsInternal()
		: Super(EMassCommandOperationType::Remove DEBUG_NAME("RemoveFragments"))
		, FragmentsAffected(UE::Mass::Utils::ConstructFragmentBitSet<CheckTime, TTypes...>())
	{}

#if WITH_MASSENTITY_DEBUG
	template<typename T>
	static bool CheckBreakpoints(T Entity, TTypes... InFragments)
	{
		return UE::Mass::Debug::FBreakpoint::CheckFragmentRemoveBreakpoints(Entity, Forward<TTypes>(InFragments)...);
	}
#endif // WITH_MASSENTITY_DEBUG

protected:
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandRemoveFragments_Execute);
		TArray<FMassArchetypeEntityCollection> EntityCollections;
		UE::Mass::Utils::CreateEntityCollections(EntityManager, TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates, EntityCollections);
		// 【中文】Remove 用第二个 bitset (要添加) 留空，第三个 bitset (要移除) 非空。
		EntityManager.BatchChangeFragmentCompositionForEntities(EntityCollections, FMassFragmentBitSet(), FragmentsAffected);
	}
	FMassFragmentBitSet FragmentsAffected;
};

template<typename... TTypes>
using FMassCommandRemoveFragments = FMassCommandRemoveFragmentsInternal<EMassCommandCheckTime::CompileTimeCheck, TTypes...>;

//-----------------------------------------------------------------------------
// Simple tag composition change
//-----------------------------------------------------------------------------
// 【中文】Tag 组合变更的中间基类：同时持有"要加的 tag"和"要移除的 tag"两套 bitset。
// 派生类（AddTags/RemoveTags/SwapTags）只是把 ctor 里的两个 bitset 用不同方式填充而已。
struct FMassCommandChangeTags : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;
	FMassCommandChangeTags()
		: Super(EMassCommandOperationType::ChangeComposition DEBUG_NAME("ChangeTags"))
	{}

	// 【中文】给派生类用的"全参数"构造：由它指定 OperationType 和具体 add/remove bitset。
	FMassCommandChangeTags(EMassCommandOperationType OperationType, FMassTagBitSet TagsToAdd, FMassTagBitSet TagsToRemove DEBUG_NAME_PARAM("ChangeTags"))
		: Super(OperationType FORWARD_DEBUG_NAME_PARAM)
		, TagsToAdd(TagsToAdd)
		, TagsToRemove(TagsToRemove)
	{}

protected:
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandChangeTags_Execute);
		TArray<FMassArchetypeEntityCollection> EntityCollections;
		UE::Mass::Utils::CreateEntityCollections(EntityManager, TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates, EntityCollections);

		// 【中文】Manager 一次性应用 add+remove，避免做两次 archetype 迁移。
		EntityManager.BatchChangeTagsForEntities(EntityCollections, TagsToAdd, TagsToRemove);
	}

	virtual SIZE_T GetAllocatedSize() const override
	{
		return TagsToAdd.GetAllocatedSize() + TagsToRemove.GetAllocatedSize() + Super::GetAllocatedSize();
	}

	// 【中文】要添加的 tag 集合 / 要移除的 tag 集合。SwapTags 时两者都非空。
	FMassTagBitSet TagsToAdd;
	FMassTagBitSet TagsToRemove;
};

// 【中文】"添加 tag" 命令：基类构造时只把 add 集合塞 TTypes，remove 留空，OperationType=Add。
template<EMassCommandCheckTime CheckTime, typename... TTypes>
struct FMassCommandAddTagsInternal : public FMassCommandChangeTags
{
	using Super = FMassCommandChangeTags;
	FMassCommandAddTagsInternal()
		: Super(
			EMassCommandOperationType::Add, 
			UE::Mass::Utils::ConstructTagBitSet<CheckTime, TTypes...>(),
			{} 
			DEBUG_NAME("AddTags"))
	{}
};

// 【中文】单 tag 与多 tag 的语法糖。
template<typename T>
using FMassCommandAddTag = FMassCommandAddTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>;

template<typename... TTypes>
using FMassCommandAddTags = FMassCommandAddTagsInternal<EMassCommandCheckTime::CompileTimeCheck, TTypes...>;

// 【中文】"移除 tag" 命令：基类构造时 add 留空，remove 集合塞 TTypes，OperationType=Remove。
template<EMassCommandCheckTime CheckTime, typename... TTypes>
struct FMassCommandRemoveTagsInternal : public FMassCommandChangeTags
{
	using Super = FMassCommandChangeTags;
	FMassCommandRemoveTagsInternal()
		: Super(
			EMassCommandOperationType::Remove, 
			{}, 
			UE::Mass::Utils::ConstructTagBitSet<CheckTime, TTypes...>()
			DEBUG_NAME("RemoveTags"))
	{}

#if WITH_MASSENTITY_DEBUG
	// 【中文】※ 见末尾"疑似 bug"汇总：这里调用的是 CheckFragmentAddBreakpoints —— 看起来
	// 复制自 AddFragments 的代码，应该是 CheckFragmentRemoveBreakpoints / 或 CheckTagRemove 才对。
	template<typename T>
	static bool CheckBreakpoints(T Entity, TTypes... InFragments)
	{
		return UE::Mass::Debug::FBreakpoint::CheckFragmentAddBreakpoints(Entity, Forward<TTypes>(InFragments)...);
	}
#endif // WITH_MASSENTITY_DEBUG
};

template<typename T>
using FMassCommandRemoveTag = FMassCommandRemoveTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>;

template<typename... TTypes>
using FMassCommandRemoveTags = FMassCommandRemoveTagsInternal<EMassCommandCheckTime::CompileTimeCheck, TTypes...>;

// 【中文】"原子换 tag"：把 TOld 替换成 TNew，作为一条 ChangeComposition 命令处理。
// 比"先 RemoveTag<TOld> 再 AddTag<TNew>"更高效——只触发一次 archetype 迁移，且
// 不会出现"中间状态"（既没 TOld 也没 TNew）。
template<EMassCommandCheckTime CheckTime, typename TOld, typename TNew>
struct FMassCommandSwapTagsInternal : public FMassCommandChangeTags
{
	using Super = FMassCommandChangeTags;
	FMassCommandSwapTagsInternal()
		: Super(
			EMassCommandOperationType::ChangeComposition,
			UE::Mass::Utils::ConstructTagBitSet<CheckTime, TNew>(),
			UE::Mass::Utils::ConstructTagBitSet<CheckTime, TOld>()
			DEBUG_NAME("SwapTags"))
	{}
};

template<typename TOld, typename TNew>
using FMassCommandSwapTags = FMassCommandSwapTagsInternal<EMassCommandCheckTime::CompileTimeCheck, TOld, TNew>;

//-----------------------------------------------------------------------------
// Struct Instances adding and setting
//-----------------------------------------------------------------------------
// 【中文】"添加 fragment 实例"命令：不仅添加 fragment 类型 (改 archetype)，还携带每个
// fragment 的初始数据值。TOthers... 一般是 (FFragmentA, FFragmentB, ...)。
// 内部用 TMultiArray<TOthers...> 维持每种 fragment 一个并行数组：
//   TargetEntities[i]                 ← 第 i 次 push 的 entity
//   Fragments.Get<TOthers>()...[i]    ← 同一次 push 给该 entity 的各 fragment 实例
// Flush 时把这些"并行数组"压成 FStructArrayView 列表，由
// EntityManager.BatchAddFragmentInstancesForEntities 一次性写入。
template<typename... TOthers>
struct FMassCommandAddFragmentInstances : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;

	// 【中文】OperationType 默认 Set —— 因为既加 fragment 又写入数据值，按"set 已存在的值"
	// 阶段执行更安全（如果之前 Add 阶段已经把 fragment 加上了，Set 阶段就只写值；如果没加，
	// BatchAddFragmentInstancesForEntities 内部也会补加）。
	FMassCommandAddFragmentInstances(EMassCommandOperationType OperationType = EMassCommandOperationType::Set DEBUG_NAME_PARAM("AddFragmentInstanceList"))
		: Super(EMassCommandOperationType::Set FORWARD_DEBUG_NAME_PARAM)
		, FragmentsAffected(UE::Mass::Utils::ConstructFragmentBitSet<EMassCommandCheckTime::CompileTimeCheck, TOthers...>())
	{}

	// 【中文】每次 push：把 entity 加入目标列表，把每个 fragment 实例加入并行数组。
	// 注意 Fragments.Add(InFragments...) 是 TMultiArray 的"一次性扩展所有列"语义。
	void Add(FMassEntityHandle Entity, TOthers... InFragments)
	{
		Super::Add(Entity);
		Fragments.Add(InFragments...);
	}

#if WITH_MASSENTITY_DEBUG
	static bool CheckBreakpoints(const FMassEntityHandle Entity, TOthers... InFragments)
	{
		return UE::Mass::Debug::FBreakpoint::CheckFragmentAddBreakpoints(Entity, Forward<TOthers>(InFragments)...);
	}
#endif // WITH_MASSENTITY_DEBUG

protected:
	virtual void Reset() override
	{
		Fragments.Reset();
		Super::Reset();
	}

	virtual SIZE_T GetAllocatedSize() const override
	{
		return Super::GetAllocatedSize() + Fragments.GetAllocatedSize() + FragmentsAffected.GetAllocatedSize();
	}

	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandAddFragmentInstances_Execute);

		// 【中文】把 TMultiArray 中的 N 个并行数组转成 FStructArrayView 列表，
		// 让下游 payload 系统按 (entity, payload-tuple) 组织成统一格式。
		TArray<FStructArrayView> GenericMultiArray;
		GenericMultiArray.Reserve(Fragments.GetNumArrays());
		Fragments.GetAsGenericMultiArray(GenericMultiArray);

		TArray<FMassArchetypeEntityCollectionWithPayload> EntityCollections;
		FMassArchetypeEntityCollectionWithPayload::CreateEntityRangesWithPayload(EntityManager, TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates
			, FMassGenericPayloadView(GenericMultiArray), EntityCollections);

		EntityManager.BatchAddFragmentInstancesForEntities(EntityCollections, FragmentsAffected);
	}

	// 【中文】每个 fragment 类型一个 TArray，并行存储所有 push 调用的 fragment 实例。
	mutable UE::Mass::TMultiArray<TOthers...> Fragments;
	// 【中文】静态压缩出的 fragment 类型 bitset，用于一次性 archetype 切换。
	const FMassFragmentBitSet FragmentsAffected;
};

/**
 * Command capable of adding any element type, be it a fragment or a tag.
 * Note that this type of command can only be added via PushUniqueCommand
 */
// 【中文】运行期决定"加什么类型 (UScriptStruct*)"的 element 命令。因为类型是实例数据
// 而非模板参数，同一 C++ 类型可以指代不同 element 类型 → 不能按"类型唯一"做去重，
// 因此 RequiresUniqueHandling=true，必须走 PushUniqueCommand 路径。
struct FMassCommandAddElement : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;

	FMassCommandAddElement(const TNotNull<const UScriptStruct*> InElementType)
		: Super(EMassCommandOperationType::Add DEBUG_NAME("AddElement"))
		, ElementType(InElementType)
	{}

protected:
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandAddElement_Execute);
		EntityManager.AddElementToEntities(TargetEntities, ElementType);
	}

	// 【中文】运行期类型句柄，决定本次给 entity 添加的是哪种 fragment / tag。
	TNotNull<const UScriptStruct*> ElementType;
};

// 【中文】对 FMassCommandAddElement 的 traits 特化：声明它需要 unique handling，
// 阻止用户错误地通过 PushCommand 进入 CreateOrAddCommand 复用路径。
template<>
struct UE::Mass::Command::TCommandTraits<FMassCommandAddElement> final
{
	enum
	{
		RequiresUniqueHandling = true
	};
};;

// ============================================================================
// 【中文】FMassCommandBuildEntity —— 高性能"批量构建 entity"命令。
// 思路：CRTP 风格继承 FMassCommandAddFragmentInstances 复用并行数组累积逻辑，
// 但 OperationType 改为 Create —— 这意味着该命令在 Flush 第一阶段执行，专门
// 对"刚被 reserve 但还没真正建过 archetype"的 entity 一次性把所有 fragment
// 装上、并安排到对应 archetype。
//
// 这是构建 entity 的"快路径"：
//   ReserveEntity()                               ← 拿到 archetype-less 的 entity handle
//   CommandBuffer.PushCommand<FMassCommandBuildEntity<F1,F2,F3>>(handle, F1{...}, F2{...}, F3{...})
//   Flush 阶段统一处理 → 同一组 (F1,F2,F3) 的 entities 走 BatchBuildEntities，
//   一次完成 archetype 选择 + 内存分配 + fragment 写入。比逐 entity Build 快得多。
// ============================================================================
template<typename... TOthers>
struct FMassCommandBuildEntity : public FMassCommandAddFragmentInstances<TOthers...>
{
	using Super = FMassCommandAddFragmentInstances<TOthers...>;

	FMassCommandBuildEntity()
		: Super(EMassCommandOperationType::Create DEBUG_NAME("BuildEntity"))
	{
	}

#if WITH_MASSENTITY_DEBUG
	// 【中文】"创建 entity"断点，区别于 fragment-add 断点。
	static bool CheckBreakpoints(TOthers... InFragments)
	{
		return UE::Mass::Debug::FBreakpoint::CheckCreateEntityBreakpoints(Forward<TOthers>(InFragments)...);
	}
#endif // WITH_MASSENTITY_DEBUG

protected:

	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandBuildEntity_Execute);

		// 【中文】把 TMultiArray 转成统一 payload 视图。
		TArray<FStructArrayView> GenericMultiArray;
		GenericMultiArray.Reserve(Super::Fragments.GetNumArrays());
		Super::Fragments.GetAsGenericMultiArray(GenericMultiArray);

		TArray<FMassArchetypeEntityCollectionWithPayload> EntityCollections;
		FMassArchetypeEntityCollectionWithPayload::CreateEntityRangesWithPayload(EntityManager, Super::TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates
			, FMassGenericPayloadView(GenericMultiArray), EntityCollections);

		// 【中文】因为所有目标 entity 都是"archetype-less"的（reserve 出来的），
		// CreateEntityRangesWithPayload 应当只返回 0 或 1 个 collection。
		check(EntityCollections.Num() <= 1);
		if (EntityCollections.Num())
		{
			// 【中文】BatchBuildEntities 内部根据 FragmentsAffected 决定目标 archetype，
			// 然后把所有 entity + payload 一次性写进新 archetype 的 chunk 里。
			// 这里 SharedFragmentValues 留空 —— 见下文带 shared fragment 的版本。
			EntityManager.BatchBuildEntities(EntityCollections[0], Super::FragmentsAffected, FMassArchetypeSharedFragmentValues());
		}
	}
};

/** 
 * Note: that TSharedFragmentValues is always expected to be FMassArchetypeSharedFragmentValues, but is declared as 
 *	template's param to maintain uniform command adding interface via FMassCommandBuffer.PushCommand. 
 *	PushCommands received all input params in one `typename...` list and as such cannot be easily split up to reason about.
 */
// 【中文】带 shared fragment 的批量 build 命令。
// 与上面差别：一次 push 还要带上一个 FMassArchetypeSharedFragmentValues（共享 fragment 的具体取值集合），
// 不同 SharedFragmentValues 必须落到不同 archetype，所以本命令内部用 TMap 按 hash(SharedFragmentValues) 分桶
// （FPerSharedFragmentsHashData），每个桶各自累积自己的 entities + fragments，最后逐桶 BatchBuildEntities。
//
// TSharedFragmentValues 之所以做成模板参数：FMassCommandBuffer::PushCommand 把所有入参一锅塞进
// "typename... TArgs"，如果不抽出来当模板参数，Add() 函数签名里就难以单独识别这个特殊参数。
template<typename TSharedFragmentValues, typename... TOthers>
struct FMassCommandBuildEntityWithSharedFragments : public FMassBatchedCommand
{
	using Super = FMassBatchedCommand;

	FMassCommandBuildEntityWithSharedFragments()
		: Super(EMassCommandOperationType::Create DEBUG_NAME("FMassCommandBuildEntityWithSharedFragments"))
		, FragmentsAffected(UE::Mass::Utils::ConstructFragmentBitSet<EMassCommandCheckTime::CompileTimeCheck, TOthers...>())
	{}

	// 【中文】push 时按 SharedFragmentValues 的 hash 分桶累积。
	// 注意：先调 Sort() 让 SharedFragmentValues 内部稳定排列，hash 才稳定；
	// 再先取 hash 再 MoveTemp（求值顺序无保证 → 必须显式预先 GetTypeHash）。
	void Add(FMassEntityHandle Entity, FMassArchetypeSharedFragmentValues&& InSharedFragments, TOthers... InFragments)
	{
		InSharedFragments.Sort();

		// Compute hash before adding to the map since evaluation order is not guaranteed
		// and MoveTemp will invalidate InSharedFragments
		const uint32 Hash = GetTypeHash(InSharedFragments);

		FPerSharedFragmentsHashData& Instance = Data.FindOrAdd(Hash, MoveTemp(InSharedFragments));
		Instance.Fragments.Add(InFragments...);
		Instance.TargetEntities.Add(Entity);

		bHasWork = true;
	}

#if WITH_MASSENTITY_DEBUG
	static bool CheckBreakpoints(TOthers... InFragments)
	{
		// debugger doesn't currently support shared fragment filtering, so just send the others
		// 【中文】调试器还不支持基于 shared fragment 取值的断点，仅过滤普通 fragment。
		return UE::Mass::Debug::FBreakpoint::CheckCreateEntityBreakpoints(Forward<TOthers>(InFragments)...);
	}
#endif // WITH_MASSENTITY_DEBUG

protected:
	virtual SIZE_T GetAllocatedSize() const override
	{
		SIZE_T TotalSize = 0;
		for (const auto& KeyValue : Data)
		{
			TotalSize += KeyValue.Value.GetAllocatedSize();
		}
		TotalSize += Data.GetAllocatedSize();
		TotalSize += FragmentsAffected.GetAllocatedSize();
		return TotalSize;
	}

	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandBuildEntityWithSharedFragments_Execute);

		constexpr int FragmentTypesCount = UE::Mass::TMultiTypeList<TOthers...>::Ordinal + 1;
		TArray<FStructArrayView> GenericMultiArray;
		GenericMultiArray.Reserve(FragmentTypesCount);

		// 【中文】每个 hash 桶对应同一组 SharedFragmentValues，逐桶 BatchBuildEntities，
		// 桶之间彼此独立、目标 archetype 不同。
		for (auto It : Data)
		{			
			It.Value.Fragments.GetAsGenericMultiArray(GenericMultiArray);

			TArray<FMassArchetypeEntityCollectionWithPayload> EntityCollections;
			FMassArchetypeEntityCollectionWithPayload::CreateEntityRangesWithPayload(EntityManager, It.Value.TargetEntities, FMassArchetypeEntityCollection::FoldDuplicates
				, FMassGenericPayloadView(GenericMultiArray), EntityCollections);
			checkf(EntityCollections.Num() <= 1, TEXT("We expect TargetEntities to only contain archetype-less entities, ones that need to be \'build\'"));

			if (EntityCollections.Num())
			{
				EntityManager.BatchBuildEntities(EntityCollections[0], FragmentsAffected, It.Value.SharedFragmentValues);
			}

			GenericMultiArray.Reset();
		}
	}

	virtual void Reset() override
	{
		Data.Reset();
		Super::Reset();
	}

#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	virtual int32 GetNumOperationsStat() const override
	{
		int32 TotalCount = 0;
		for (const auto& KeyValue : Data)
		{
			TotalCount += KeyValue.Value.TargetEntities.Num();
		}
		return TotalCount;
	}
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

	// 【中文】要添加的 fragment 类型集合（构造时一次确定）。
	FMassFragmentBitSet FragmentsAffected;

	// 【中文】每个独立 SharedFragmentValues 取值组合对应一个数据桶。
	struct FPerSharedFragmentsHashData
	{
		FPerSharedFragmentsHashData(FMassArchetypeSharedFragmentValues&& InSharedFragmentValues)
			: SharedFragmentValues(MoveTemp(InSharedFragmentValues))
		{	
		}

		SIZE_T GetAllocatedSize() const
		{
			return TargetEntities.GetAllocatedSize() + Fragments.GetAllocatedSize() + SharedFragmentValues.GetAllocatedSize();
		}

		// 【中文】落到本桶的 entity 列表。
		TArray<FMassEntityHandle> TargetEntities;
		// 【中文】落到本桶的 fragment 实例（与 TargetEntities 并行）。
		mutable UE::Mass::TMultiArray<TOthers...> Fragments;
		// 【中文】本桶共享的 SharedFragmentValues 取值。
		FMassArchetypeSharedFragmentValues SharedFragmentValues;
	};

	// 【中文】hash → 数据桶。hash 是 SharedFragmentValues 排序后稳定计算出来的。
	TMap<uint32, FPerSharedFragmentsHashData> Data;
};

//-----------------------------------------------------------------------------
// Commands that really can't know the types at compile time
//-----------------------------------------------------------------------------
// 【中文】"延迟执行任意 lambda"的命令，专给运行期才能确定行为的场景使用。
// 用法：把任何符合 void(FMassEntityManager&) 签名的函数对象 push 进来，Flush 时按
// OpType 决定的阶段顺序逐个调用。比起手写一个完整命令类，这是一条快捷路径，
// 但也牺牲了 batched 优势 —— 每个 lambda 是单独执行的。
template<EMassCommandOperationType OpType>
struct FMassDeferredCommand : public FMassBatchedCommand
{
	using Super = FMassBatchedCommand;
	using FExecFunction = TFunction<void(FMassEntityManager& EntityManager)>;

	FMassDeferredCommand()
		: Super(OpType DEBUG_NAME("BatchedDeferredCommand"))
	{}

	// 【中文】右值版本：把 lambda move 进来，避免拷贝。
	void Add(FExecFunction&& ExecFunction)
	{
		DeferredFunctions.Add(MoveTemp(ExecFunction));
		bHasWork = true;
	}

	// 【中文】左值版本：拷贝 lambda。
	void Add(const FExecFunction& ExecFunction)
	{
		DeferredFunctions.Add(ExecFunction);
		bHasWork = true;
	}

protected:
	virtual SIZE_T GetAllocatedSize() const
	{
		return DeferredFunctions.GetAllocatedSize();
	}

	// 【中文】Run：按 push 顺序逐个执行存储的 lambda。
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MassDeferredCommand_Execute);

		for (const FExecFunction& ExecFunction : DeferredFunctions)
		{
			ExecFunction(EntityManager);
		}
	}

	virtual void Reset() override
	{
		DeferredFunctions.Reset();
		Super::Reset();
	}

#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
	virtual int32 GetNumOperationsStat() const override
	{
		return DeferredFunctions.Num();
	}
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

	// 【中文】累积的 lambda 列表。
	TArray<FExecFunction> DeferredFunctions;
};

// 【中文】六个常用 OperationType 的"延迟 lambda"命令别名。
// 选用哪个取决于你的 lambda 应该和哪个阶段一起执行：例如要在 Add 阶段后做点善后用 SetCommand，
// 要保证它最先执行用 CreateCommand。
using FMassDeferredCreateCommand = FMassDeferredCommand<EMassCommandOperationType::Create>;
using FMassDeferredAddCommand = FMassDeferredCommand<EMassCommandOperationType::Add>;
using FMassDeferredRemoveCommand = FMassDeferredCommand<EMassCommandOperationType::Remove>;
using FMassDeferredChangeCompositionCommand = FMassDeferredCommand<EMassCommandOperationType::ChangeComposition>;
using FMassDeferredSetCommand = FMassDeferredCommand<EMassCommandOperationType::Set>;
using FMassDeferredDestroyCommand = FMassDeferredCommand<EMassCommandOperationType::Destroy>;

#undef DEBUG_NAME
#undef DEBUG_NAME_PARAM
#undef FORWARD_DEBUG_NAME_PARAM
