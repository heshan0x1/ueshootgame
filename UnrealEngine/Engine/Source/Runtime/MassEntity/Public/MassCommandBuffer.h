// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityManager.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "Misc/MTTransactionallySafeAccessDetector.h"
#include "Misc/TransactionallySafeCriticalSection.h"
#include "MassEntityUtils.h"
#include "MassDebuggerBreakpoints.h"
#include "MassCommands.h"
// required for UE::Mass::Deprecation
#include <type_traits>
#include "Concepts/ConvertibleTo.h"

struct FMassEntityManager;

// ============================================================================
// 【中文整体说明 - 命令缓冲系统 (Command Buffer)】
// ----------------------------------------------------------------------------
// 为什么需要"命令缓冲"？
//   Mass 中真正的"结构性变更"(增删 fragment / tag / entity) 会改变 entity 所在
//   archetype 的布局：entity 会被从一个 archetype chunk 移动到另一个 archetype
//   chunk。如果 processor 正处于 ForEachEntityChunk 的内层迭代里，直接执行这种
//   变更会让"当前正在迭代的 chunk 被改写"，导致迭代失效甚至崩溃。
//
//   解决方案：所有结构性变更不立即执行，而是被记录成"命令" (FMassBatchedCommand)
//   暂存到 FMassCommandBuffer 中；processor 跑完后由框架统一调用 Flush 一次性
//   提交。这就是"延迟提交 (deferred commit)"思路 —— 类似消息队列 + 数据库事务：
//     PushCommand* / Add* / Remove* / DestroyEntity*  ≈  入队
//     Flush                                            ≈  事务提交
//
// 与之配套的设计：
//   1. 每条命令按"操作类型"(EMassCommandOperationType) 分组，Flush 时按
//      Create → Add → ChangeComposition → Set → Remove/Destroy 的顺序排序
//      执行（详见 MassCommands.h 的枚举与 MassCommandBuffer.cpp 的 CommandTypeOrder）。
//   2. 同一类型的命令会被复用：CreateOrAddCommand<T>() 利用
//      FMassBatchedCommand::GetCommandIndex<T>() 给每个具体命令类型分配一个
//      静态唯一序号，对应的 instance 累积所有调用的参数到 TArray，Flush 时
//      一次性批量处理 (Batched)，这是高性能路径。
//   3. 线程模型：每个 buffer 绑定一个 owner thread（构造线程），不允许跨线程
//      Push。ParallelFor 的典型用法是"每个 worker 独立一个 buffer，结束后
//      MoveAppend 合并到主 buffer"。
// ============================================================================

//@TODO: Consider debug information in case there is an assert when replaying the command buffer
// (e.g., which system added the command, or even file/line number in development builds for the specific call via a macro)

// 【中文】Push 命令时统一的安全断言宏：
//   1) 不允许在 Flush 进行中再往同一个 buffer push 命令（会引起递归/数据竞争）
//   2) 不允许在非 owner 线程 push（除非显式调用 ForceUpdateCurrentThreadID）
#define COMMAND_PUSHING_CHECK() \
checkf(IsFlushing() == false, TEXT("Trying to push commands is not supported while the given buffer is being flushed")); \
checkf(OwnerThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("Commands can be pushed only in the same thread where the command buffer was created."))

// The following is a temporary construct to help users update their code after FMassBatchedCommand::Execute deprecation
// @todo remove by 5.9
// 【中文】用于在 5.7→5.9 过渡期内检测用户是否仍然 override 了已废弃的 const Execute()
// 而不是新的 Run()。如果用户重写了旧函数，static_assert 会在 PushCommand 实例化时报错，
// 引导他们迁移到 Run。
namespace UE::Mass::Deprecation
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// 【中文】判断 TCommand::Execute 与基类版本是否同一函数指针。如果不是，说明派生类重写过。
	template <typename TCommand>
	constexpr bool IsExecuteOverridden()
	{
	    using TResolved = decltype(&TCommand::Execute);
	    using TBase = decltype(&FMassBatchedCommand::Execute);
	    return !std::is_same_v<TResolved, TBase>;
	}

	// 【中文】对外暴露的 type-trait，把 IsExecuteOverridden 包装成 bool_constant，便于在
	// static_assert / if constexpr 中使用。仅对 FMassBatchedCommand 派生类开启 (concept 约束)。
	template <typename TCommand>
	requires std::is_base_of_v<FMassBatchedCommand, TCommand>
	struct TOverridesExecute : std::bool_constant<IsExecuteOverridden<TCommand>()>
	{
	};
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// 【中文】内部使用的辅助宏：在 CreateOrAddCommand<T>() 实例化路径上插入静态断言，强制
// 用户必须用 Run() 而非 Execute() 来实现命令逻辑。
#define ASSERT_EXECUTE_DEPRECATION(CommandType) \
		static_assert(!UE::Mass::Deprecation::TOverridesExecute<CommandType>::value, "Mass Commands: CONST Execute function is deprecated in 5.7 and will be removed by 5.9. Use Run instead.");

namespace UE::Mass::Debug
{
	// 【中文】调试器断点集成（需要 WITH_MASSENTITY_DEBUG）。理念：
	//   Mass 调试器允许用户基于"某个 entity"或"某种 fragment 类型"挂断点，
	//   当对应的命令被 push 时（addFragment / removeFragment / destroyEntity 等），
	//   会调用 TCommand::CheckBreakpoints / CheckBreakpointsByInstance 询问命令类型本身：
	//   "本次操作是否命中了用户设置的断点？" 命中则触发 DebugBreak()，把用户拉到调试器。
	//
	// 命令类型只要实现一个静态方法 CheckBreakpoints(...) 即可被自动接入；下面的两个 concept
	// 通过 SFINAE 检测命令类型有没有提供这两个静态接口（"是否支持 by-instance 检查"）。

	// 【中文】检测 TCommand 是否提供 static CheckBreakpoints(Args...)（不要求 Entity 在最前）
	template<typename TCommand, typename... Args>
	concept HasCheckBreakpoints =
		requires(Args&&... args) {
			{ TCommand::CheckBreakpoints(Forward<Args>(args)...) }
			-> CConvertibleTo<bool>;
	};

	// 【中文】检测 TCommand 是否提供 static CheckBreakpoints(FMassEntityHandle, Args...)，
	// 也就是支持"对具体 entity 实例进行断点检查"
	template<typename TCommand, typename... Args>
	concept HasCheckBreakpointsWithEntity =
		requires(const FMassEntityHandle Entity, Args&&... args) {
			{ TCommand::CheckBreakpoints(Entity, Forward<Args>(args)...) }
			-> CConvertibleTo<bool>;
	};

	// 【中文】Push 命令时调用：若命令类型实现了 CheckBreakpoints 且返回 true，触发 DebugBreak。
	// 仅在 WITH_MASSENTITY_DEBUG 编译开启时有效，零成本于 Shipping。
	template<typename TCommand, typename... TArgs>
	void CallCheckBreakpoints(TArgs&&... InArgs)
	{
#if WITH_MASSENTITY_DEBUG
		if constexpr (HasCheckBreakpoints<TCommand, TArgs...>)
		{
			if (TCommand::CheckBreakpoints(Forward<TArgs>(InArgs)...))
			{
				UE::Mass::Debug::FBreakpoint::DebugBreak();
			}
		}
#endif //WITH_MASSENTITY_DEBUG
	}

	// 【中文】"按实例 (by instance)" 版本：Push 携带 Entity 的命令时调用。差异在于命令类型
	// 实现的是 CheckBreakpointsByInstance，可以同时筛选 entity 和 fragment 类型组合。
	// 注意：从代码看，concept 名为 HasCheckBreakpointsWithEntity，但函数体内调用的是
	// TCommand::CheckBreakpointsByInstance —— 见后文"疑似 bug"汇总。
	template<typename TCommand, typename... TArgs >
	void CallCheckBreakpointsByInstance(TArgs&&... InArgs)
	{
#if WITH_MASSENTITY_DEBUG
		if constexpr (HasCheckBreakpointsWithEntity<TCommand, TArgs...>)
		{
			if (TCommand::CheckBreakpointsByInstance(Forward<TArgs>(InArgs)...))
			{
				UE::Mass::Debug::FBreakpoint::DebugBreak();
			}
		}
#endif //WITH_MASSENTITY_DEBUG
	}
} // namespace UE::Mass::Debug


// ============================================================================
// 【中文】FMassCommandBuffer —— Mass 的"延迟命令队列"
// ----------------------------------------------------------------------------
// 角色与生命周期：
//   - 每个 processing context / 每个 ParallelFor worker 一般持有一个 buffer；
//   - processor 通过 PushCommand / AddFragment / RemoveTag / DestroyEntity 等
//     接口把"想做但现在不能做的事"塞进队列；
//   - processor 跑完后，由框架（FMassEntityManager）调用 Flush 一次性提交所有
//     变更，按操作类型分组排序执行 —— 类似数据库的 COMMIT。
//
// 内部存放两类命令：
//   - CommandInstances：按命令类型唯一存放（CreateOrAddCommand 去重并复用），
//     适合"同一种 push 调用很多次"的高频场景，参数累积在内部 TArray，Flush 时
//     一次批量执行；
//   - AppendedCommandInstances：通过 PushUniqueCommand / MoveAppend 加入的
//     命令，按顺序保留，无去重（典型来源是其他 buffer 合入或带成员状态的
//     unique 命令）。
//
// 线程安全：
//   - Push 必须在 owner thread 上调用（OwnerThreadId）；
//   - MoveAppend / PushUniqueCommand 内部用 AppendingCommandsCS 串行化对
//     AppendedCommandInstances 的写入，因此可以从其它 buffer "并入" 进来。
// ============================================================================
struct FMassCommandBuffer
{
	MASSENTITY_API FMassCommandBuffer();
	MASSENTITY_API ~FMassCommandBuffer();

	/** Adds a new entry to a given TCommand batch command instance */
	// 【中文】模板模板参数版本：当命令类是 TCommand<TArgs...>（例如
	// FMassCommandAddFragmentsInternal<CompileTimeCheck, FFoo>）时，由调用者提供
	// "命令模板"和"模板实参"，函数自动实例化具体类型并把 (Entity, args...) 累加进去。
	// - Entity: 操作目标实体；
	// - InArgs...: 转发给具体命令的 Add() 接口的额外参数（如 fragment 实例）。
	template< template<typename... TArgs> typename TCommand, typename... TArgs >
	void PushCommand(const FMassEntityHandle Entity, TArgs&&... InArgs)
	{
		COMMAND_PUSHING_CHECK();
		// 【中文】调试断点集成：若命令支持 by-instance 断点且当前 entity / 参数命中，触发 DebugBreak
		UE::Mass::Debug::CallCheckBreakpointsByInstance<TCommand<TArgs...>>(Entity, Forward<TArgs>(InArgs)...);

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		// 【中文】CreateOrAddCommand 按 GetCommandIndex<T>() 在 CommandInstances 中查重，
		// 如果该类型命令已存在则复用；不存在则 new 一个。后续多次 push 都会累加到同一实例的
		// 内部数组里，Flush 时一并处理 —— 这就是"批量化"的核心。
		TCommand<TArgs...>& Instance = CreateOrAddCommand<TCommand<TArgs...>>();
		Instance.Add(Entity, Forward<TArgs>(InArgs)...);
		++ActiveCommandsCounter;
	}

	// 【中文】常规模板版本：当命令类是单一类型（已实例化好的 TCommand），调用者直接
	// 把"想要传给命令 Add() 的所有参数"原样转发即可。
	template<typename TCommand, typename... TArgs>
	void PushCommand(TArgs&&... InArgs)
	{
		COMMAND_PUSHING_CHECK();
		UE::Mass::Debug::CallCheckBreakpointsByInstance<TCommand>(Forward<TArgs>(InArgs)...);

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		TCommand& Instance = CreateOrAddCommand<TCommand>();
		Instance.Add(Forward<TArgs>(InArgs)...);
		++ActiveCommandsCounter;
	}

	/** Adds a new entry to a given TCommand batch command instance */
	// 【中文】只关心 entity（不带额外参数）的最简形式，例如 AddTag<T>(Entity)。
	// 注意此处使用 CallCheckBreakpoints（不是 by-instance 版本），因为命令类没有
	// 模板参数的可变 args，断点检查只看 entity。
	template< typename TCommand>
	void PushCommand(const FMassEntityHandle Entity)
	{
		COMMAND_PUSHING_CHECK();
		UE::Mass::Debug::CallCheckBreakpoints<TCommand>(Entity);

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		CreateOrAddCommand<TCommand>().Add(Entity);
		++ActiveCommandsCounter;
	}

	// 【中文】对一组 entity 一次性入队的版本（如批量 DestroyEntities）。同样会复用
	// 内部命令实例，把传入的 entity 数组 Append 到命令的 TargetEntities。
	template< typename TCommand>
	void PushCommand(TConstArrayView<FMassEntityHandle> Entities)
	{
		COMMAND_PUSHING_CHECK();
		UE::Mass::Debug::CallCheckBreakpoints<TCommand>(Entities);

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		CreateOrAddCommand<TCommand>().Add(Entities);
		++ActiveCommandsCounter;
	}

	/**
	 * Ordinary PushCommand calls try to reuse existing command instances (as stored in CommandInstances)
	 * based on their type.
	 * This command lets callers add a manually configured command instance, that 
	 * might not be distinguishable from other commands based solely on its type.
	 * For example, when you implement a command type that has member properties that 
	 * control how the given command works or what exactly it does.
	 */
	// 【中文】"独立命令"入队接口：用于 RequiresUniqueHandling=true 的命令类型
	// （如 FMassCommandAddElement——它的 ElementType 在 ctor 中传入，每个实例代表
	// 不同的目标类型，无法靠"类型唯一"做去重）。这些命令直接 append 到
	// AppendedCommandInstances，按 push 顺序保留，Flush 时逐个执行。
	// 因为这条路径可能跨线程（来自 MoveAppend 等），使用 AppendingCommandsCS 加锁。
	void PushUniqueCommand(TUniquePtr<FMassBatchedCommand>&& CommandInstance)
	{
		COMMAND_PUSHING_CHECK();

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		UE::TScopeLock Lock(AppendingCommandsCS);
		UE_MT_SCOPED_WRITE_ACCESS(PendingBatchCommandsDetector);
		AppendedCommandInstances.Add(MoveTemp(CommandInstance));

		++ActiveCommandsCounter;
	}

	// -----------------------------------------------------------------------
	// 【中文】下面是高层便捷封装。它们把 "AddFragment<T>(entity)" 等常用操作转换成
	// 对应的具体命令类型 + PushCommand 调用，避免使用者直接写复杂的命令模板名。
	// CompileTimeCheck 版本要求 T 在编译期就是合法 fragment/tag；RuntimeCheck
	// 版本则放宽为运行期 IsA<...>() 检查（用于一些反射场景）。
	// -----------------------------------------------------------------------

	template<typename T>
	void AddFragment(FMassEntityHandle Entity)
	{
		static_assert(UE::Mass::CFragment<T>, MASS_INVALID_FRAGMENT_MSG);
		PushCommand<FMassCommandAddFragmentsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	// 【中文】运行期检查变体：T 不一定在编译期被识别为 FMassFragment 的派生（例如来自
	// 反射代码），此处用 checkf 在运行时校验。其余路径与编译期版本一致。
	template<typename T>
	void AddFragment_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(UE::Mass::IsA<FMassFragment>(T::StaticStruct()), TEXT(MASS_INVALID_FRAGMENT_MSG_F), *T::StaticStruct()->GetName());
		PushCommand<FMassCommandAddFragmentsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	template<typename T>
	void RemoveFragment(FMassEntityHandle Entity)
	{
		static_assert(UE::Mass::CFragment<T>, MASS_INVALID_FRAGMENT_MSG);
		PushCommand<FMassCommandRemoveFragmentsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	template<typename T>
	void RemoveFragment_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(UE::Mass::IsA<FMassFragment>(T::StaticStruct()), TEXT(MASS_INVALID_FRAGMENT_MSG_F), *T::StaticStruct()->GetName());
		PushCommand<FMassCommandRemoveFragmentsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	/** the convenience function equivalent to calling PushCommand<FMassCommandAddTag<T>>(Entity) */
	// 【中文】添加 Tag (空类型的标记)：只改变 archetype 标识，不携带数据。
	template<typename T>
	void AddTag(FMassEntityHandle Entity)
	{
		static_assert(UE::Mass::CTag<T>, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandAddTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	// 【中文】对一组 entity 批量打 tag —— 一次 push 后参数会累加到同一命令实例中。
	template<typename T>
	void AddTag(TConstArrayView<FMassEntityHandle> Entities)
	{
		static_assert(UE::Mass::CTag<T>, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandAddTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entities);
	}

	template<typename T>
	void AddTag_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(UE::Mass::IsA<FMassTag>(T::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		PushCommand<FMassCommandAddTagsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	/** the convenience function equivalent to calling PushCommand<FMassCommandRemoveTag<T>>(Entity) */
	template<typename T>
	void RemoveTag(FMassEntityHandle Entity)
	{
		static_assert(UE::Mass::CTag<T>, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandRemoveTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	template<typename T>
	void RemoveTag(TConstArrayView<FMassEntityHandle> Entities)
	{
		static_assert(UE::Mass::CTag<T>, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandRemoveTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entities);
	}

	template<typename T>
	void RemoveTag_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(UE::Mass::IsA<FMassTag>(T::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		PushCommand<FMassCommandRemoveTagsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	/** the convenience function equivalent to calling PushCommand<FMassCommandSwapTags<TOld, TNew>>(Entity)  */
	// 【中文】"原子地"把 TOld 换成 TNew —— 内部会用一条 ChangeComposition 命令同时
	// 描述添加 TNew 与移除 TOld。比"先 RemoveTag<TOld> 再 AddTag<TNew>" 更高效，
	// 也避免了中途出现两个 archetype 抖动的过渡态。
	template<typename TOld, typename TNew>
	void SwapTags(FMassEntityHandle Entity)
	{
		static_assert(UE::Mass::CTag<TOld>, "Given struct type is not a valid tag type.");
		static_assert(UE::Mass::CTag<TNew>, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandSwapTagsInternal<EMassCommandCheckTime::CompileTimeCheck, TOld, TNew>>(Entity);
	}

	template<typename TOld, typename TNew>
	void SwapTags_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(UE::Mass::IsA<FMassTag>(TOld::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		checkf(UE::Mass::IsA<FMassTag>(TNew::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		PushCommand<FMassCommandSwapTagsInternal<EMassCommandCheckTime::RuntimeCheck, TOld, TNew>>(Entity);
	}

	// 【中文】销毁 entity：被分类为 Destroy 操作类型，Flush 时会落到最后一组执行，
	// 避免提前销毁了后续命令仍要引用的 entity。
	void DestroyEntity(FMassEntityHandle Entity)
	{
		PushCommand<FMassCommandDestroyEntities>(Entity);
	}

	void DestroyEntities(TConstArrayView<FMassEntityHandle> InEntitiesToDestroy)
	{
		PushCommand<FMassCommandDestroyEntities>(InEntitiesToDestroy);
	}

	// 【中文】右值版本，可以把调用者持有的 TArray<Entity> 直接 Move 进命令，避免复制。
	void DestroyEntities(TArray<FMassEntityHandle>&& InEntitiesToDestroy)
	{
		PushCommand<FMassCommandDestroyEntities>(MoveTemp(InEntitiesToDestroy));
	}

	// 【中文】调试/统计：返回 buffer 持有的所有命令累计申请的内存大小（含命令内部数组）。
	MASSENTITY_API SIZE_T GetAllocatedSize() const;

	/** 
	 * Appends the commands from the passed buffer into this one
	 * @param InOutOther the source buffer to copy the commands from. Note that after the call the InOutOther will be 
	 *	emptied due to the function using Move semantics
	 */
	// 【中文】把另一个 buffer 的命令"搬"过来。典型场景：ParallelFor 给每个 worker
	// 分配一个 buffer 让它们并发 push（互不干扰），所有 worker 完成后，把它们的
	// buffer 全部 MoveAppend 到主 buffer，再由主线程 Flush。
	// 注意搬过来的命令一律走 AppendedCommandInstances（保序、不去重），因此与本
	// buffer 自己的同类型命令不会自动合并。
	MASSENTITY_API void MoveAppend(FMassCommandBuffer& InOutOther);

	// 【中文】是否还有"未 Flush"的命令。
	bool HasPendingCommands() const 
	{
		return ActiveCommandsCounter > 0;
	}
	// 【中文】当前是否在 Flush 过程中。Flush 时不允许再 push（参见 COMMAND_PUSHING_CHECK）。
	bool IsFlushing() const { return bIsFlushing; }

	/**
	 * Removes any pending command instances
	 * This could be required for CommandBuffers that are queued to
	 * flush their commands on the game thread but the EntityManager is no longer available.
	 * In such scenario we need to cancel commands to avoid an ensure for unprocessed commands
	 * when the buffer gets destroyed.
	 */
	// 【中文】"放弃所有命令"：在 EntityManager 被销毁、buffer 还排队中等等异常路径下，
	// 直接清空，避免析构时触发 "destroying buffer with unprocessed commands" 警告。
	void CancelCommands()
	{
		CleanUp();
	}

	bool IsInOwnerThread() const
	{
		return OwnerThreadId == FPlatformTLS::GetCurrentThreadId();
	}

	/**
	 * Updates the OwnerThreadId which indicates that the given command buffer instance is being
	 * used in a different thread now. Use this with extreme caution, it's not a tool to be used
	 * every time we get "Commands can be pushed only in the same thread where the command buffer was created."
	 * error. It's meant to be used when there's a possibility the code owning the buffer has been
	 * moved to another thread (like in ParallelFor).
	 */
	// 【中文】把 OwnerThreadId 强行更新为当前线程。仅在合法的"逻辑迁移"场景使用，例如
	// ParallelFor 内部 worker 重新被调度到另一线程上。绝不能用来"绕过"线程检查。
	void ForceUpdateCurrentThreadID();

private:
	friend FMassEntityManager;

	// 【中文】根据命令类型 T 取得（或新建）一个唯一的命令实例。核心：
	//   - GetCommandIndex<T>() 给每个 T 分配一个进程内唯一的 uint32 序号；
	//   - 用该序号当作 CommandInstances 数组的下标，做到 O(1) 查重；
	//   - 不存在则 MakeUnique<T>() 并填进去；存在则直接复用。
	// static_assert 阻止 RequiresUniqueHandling 的命令走这条路径——它们必须用
	// PushUniqueCommand。
	template<typename T>
	T& CreateOrAddCommand()
	{
		ASSERT_EXECUTE_DEPRECATION(T);

		static_assert(!UE::Mass::Command::TCommandTraits<T>::RequiresUniqueHandling, "This command type needs to be added via PushUniqueCommand");
		const int32 Index = FMassBatchedCommand::GetCommandIndex<T>();

		if (CommandInstances.IsValidIndex(Index) == false)
		{
			// 【中文】数组按需扩容到 Index+1；新元素默认为 nullptr (AddZeroed for TUniquePtr)。
			CommandInstances.AddZeroed(Index - CommandInstances.Num() + 1);
		}
		else if (CommandInstances[Index])
		{
			return (T&)(*CommandInstances[Index].Get());
		}

		CommandInstances[Index] = MakeUnique<T>();
		return (T&)(*CommandInstances[Index].Get());
	}

	/** 
	 * Executes all accumulated commands. 
	 * @return whether any commands have actually been executed
	 */
	// 【中文】事务"COMMIT"：把累积的命令按操作类型分组排序后逐个 Run。具体顺序参见
	// MassCommandBuffer.cpp 中的 CommandTypeOrder 表。Flush 仅由 FMassEntityManager
	// 触发（friend 关系），用户不应直接调用。
	bool Flush(FMassEntityManager& EntityManager);
	MASSENTITY_API void CleanUp();

	// 【中文】保护 AppendedCommandInstances 的写入：跨线程 MoveAppend / PushUniqueCommand
	// 都要在它的保护下追加。CommandInstances 不需要锁——只能由 owner 线程 push。
	FTransactionallySafeCriticalSection AppendingCommandsCS;

	// 【中文】多线程访问检测（Debug-only）：检测是否有"读 + 写"或"写 + 写"并发，及早暴露
	// 跨线程数据竞争问题。Read 用于 MoveAppend(other) 时读取 other 的 pending 状态。
	UE_MT_DECLARE_TS_RW_ACCESS_DETECTOR(PendingBatchCommandsDetector);
	/** 
	 * Commands created for this specific command buffer. All commands in the array are unique (by type) and reusable 
	 * with subsequent PushCommand calls
	 */
	// 【中文】"按命令类型唯一"的命令实例数组。下标 = GetCommandIndex<T>()。
	// 同一种命令多次 push 会在同一 instance 内累积参数，Flush 时一次批量执行。
	TArray<TUniquePtr<FMassBatchedCommand>> CommandInstances;
	/** 
	 * Commands appended to this command buffer (via FMassCommandBuffer::MoveAppend). These commands are just naive list
	 * of commands, potentially containing duplicates with multiple MoveAppend calls. Once appended these commands are 
	 * not being reused and consumed, destructively, during flushing
	 */
	// 【中文】"追加进来的"命令列表：来自 MoveAppend / PushUniqueCommand。保留原顺序，
	// 允许重复（多次 MoveAppend 同类型命令都会保留各自实例），Flush 时按顺序消耗一次。
	TArray<TUniquePtr<FMassBatchedCommand>> AppendedCommandInstances;

	// 【中文】尚未 Flush 的命令 push 次数计数，用于 HasPendingCommands() 快速短路。
	int32 ActiveCommandsCounter = 0;

	/** Indicates that this specific MassCommandBuffer is currently flushing its contents */
	// 【中文】Flush 重入保护标志（TGuardValue<bool> 在 Flush 期间设为 true）。
	bool bIsFlushing = false;

	/** 
	 * Identifies the thread where given FMassCommandBuffer instance was created. Adding commands from other
	 * threads is not supported and we use this value to check that.
	 * Note that it could be const since we set it in the constructor, but we need to recache on server forking.
	 */
	// 【中文】所有者线程 ID。构造时记录，PushCommand 全程校验。设计上几乎不可变，但
	// "服务器 fork" 等极端场景下需要刷新 → 因此保留 ForceUpdateCurrentThreadID 接口。
	uint32 OwnerThreadId;
};

#undef COMMAND_PUSHING_CHECK
