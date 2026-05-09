// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// 【MassDebuggerBreakpoints.h —— Mass 框架的"实体级断点"基础设施】
//
// 一、设计动机：
//   ECS 架构中，"对象"概念被解构为"index + chunk 数据"，传统 OOP 调试器无法在某个
//   "对象"上设断点；本文件提供一套数据驱动的断点系统：
//     * 触发类型（TriggerType）：什么操作（processor 执行 / fragment 写入 / 实体创建/销毁 / fragment 增删 / tag 增删）
//     * 过滤条件（FilterType）：作用于谁（特定实体 / 当前选中实体 / 满足 query 条件的实体集合）
//   两两组合即可表达 "当 ProcessorX 准备处理满足 FQuery 的实体时停下" 这种复杂条件。
//
// 二、运行时机制：
//   - FBreakpoint::DebugBreak() 内部调用 UE_DEBUG_BREAK()，让 IDE 在该函数内停下；
//     调用方（关键代码路径）通过 MASS_BREAKPOINT(bShouldBreak) 宏触发；
//   - bHasBreakpoint 是全局快路径标志：没有任何断点时，所有 Check* 函数都瞬间返回 false。
//
// 三、零成本抽象：
//   Shipping 构建中所有 enum/struct 都被替换为空 stub（见文件末尾 #else 分支），
//   MASS_BREAKPOINT 宏退化为 `do {} while(0)`——编译期完全消除。
// =====================================================================================================================

#pragma once

#include "HAL/Platform.h"
#include "MassEntityMacros.h"

#if WITH_MASSENTITY_DEBUG
/**
 * 【MASS_BREAKPOINT 宏】关键代码路径触发断点的标准入口。
 *
 * 用 do/while(0) 包裹保证宏作为单一语句行为；UNLIKELY 提示编译器走 fast path（默认无断点）。
 * 调用方先用 ShouldXxxBreak() 等查询函数得到 bShouldBreak，再用本宏决定是否真正 break。
 */
#define MASS_BREAKPOINT(bShouldBreak)					\
	do {												\
		if (UNLIKELY(bShouldBreak))						\
		{												\
			UE::Mass::Debug::FBreakpoint::DebugBreak();	\
		}												\
	} while (0)

#include "MassEntityHandle.h"
#include "Misc/TVariant.h"
#include "UObject/ObjectKey.h"
#include "MassRequirements.h"

struct FMassDebugger;
class UMassProcessor;
class UScriptStruct;

namespace UE::Mass::Debug
{
	/**
	 * 【FBreakpointHandle —— 断点的稳定身份标识】
	 *
	 * 内部仅一个 int32，但取值 > 0 才视为有效（0 = Invalid）。Handle 由全局静态计数器
	 * 单调递增分配，保证整个进程生命周期内唯一——这样 UI 持有 handle 跨帧引用断点
	 * 不会因数组重排而失效。
	 *
	 * 注意 LastHandle 是函数局部 static——全进程共享，但跨 environment 也唯一，
	 * 这点对断点持久化（editor 重启后从配置恢复）很重要。
	 */
	struct FBreakpointHandle
	{
		int32 Handle = 0;

		FBreakpointHandle(int32 InHandle)
			: Handle(InHandle)
		{
		}

		FBreakpointHandle() = default;

		friend bool operator==(const FBreakpointHandle& A, const FBreakpointHandle& B)
		{
			return A.Handle == B.Handle;
		}

		bool IsValid() const
		{
			return Handle > 0;
		}

		operator bool() const
		{
			return IsValid();
		}

		/** 分配一个新 handle（全局递增）。注意它不在 Invalid（0）上递增，所以一定 > 0。 */
		static FBreakpointHandle CreateHandle()
		{
			static int32 LastHandle = 0;
			return FBreakpointHandle{ ++LastHandle };
		}

		/** 返回 sentinel 无效 handle。 */
		static FBreakpointHandle Invalid()
		{
			return FBreakpointHandle();
		}
	};

	/**
	 * 【FBreakpoint —— 一条断点配置】
	 *
	 * 数据驱动的断点描述：(TriggerType, Trigger) + (FilterType, Filter) + bEnabled。
	 * 真正的检测逻辑在静态 Check*Breakpoints 系列函数中——它们在运行时关键事件被触发时调用。
	 *
	 * 关键字段：
	 *   - HitCount：mutable，命中计数。即使 bEnabled=false 也会递增——支持"统计但不停"模式。
	 *   - bEnabled：是否真正触发 break；UI 上勾选/取消即切换此字段。
	 *   - Trigger（TVariant）：依据 TriggerType 不同，可能是 UMassProcessor 或 UScriptStruct 的 ObjectKey；
	 *     使用 ObjectKey 是为了在对象被 GC 后断点仍能安全持久化（弱引用语义）。
	 *   - Filter（TVariant）：依据 FilterType 不同，可能是具体 Entity handle 或一组 query requirement。
	 */
	struct FBreakpoint
	{
		// 让 FMassDebugger 能访问私有静态 LastBreakpointHandle / bHasBreakpoint。
		friend FMassDebugger;

		MASSENTITY_API FBreakpoint();

		/**
		 * 触发器类型——决定"什么事件"会激活该断点。
		 * 每个枚举值对应一个 Check*Breakpoints 静态函数（见下面）。
		 */
		enum class ETriggerType : uint8
		{
			None,                //< 占位，未配置
			ProcessorExecute,    //< Processor 即将处理某实体
			FragmentWrite,       //< 即将写入某 fragment
			EntityCreate,        //< 实体创建
			EntityDestroy,       //< 实体销毁
			FragmentAdd,         //< 给实体添加 fragment
			FragmentRemove,      //< 移除 fragment
			TagAdd,              //< 添加 tag
			TagRemove,           //< 移除 tag
			MAX
		};
		/** 字符串化：用于 UI 显示和断点持久化（保存到 .ini）。 */
		static MASSENTITY_API  FString TriggerTypeToString(FBreakpoint::ETriggerType InType);
		static MASSENTITY_API bool StringToTriggerType(const FString& InString, FBreakpoint::ETriggerType& OutType);

		/**
		 * 过滤器类型——决定"作用于哪些实体"。
		 *   None           = 任何实体都触发
		 *   SpecificEntity = 仅当目标实体等于 Filter 中的 EntityHandle 时触发
		 *   SelectedEntity = 触发时实时查询当前 selected entity（动态目标）
		 *   Query          = 实体所在 archetype 是否满足一组 requirement
		 */
		enum class EFilterType : uint8
		{
			None,
			SpecificEntity,
			SelectedEntity,
			Query
		};

		static MASSENTITY_API FString FilterTypeToString(FBreakpoint::EFilterType InType);
		static MASSENTITY_API bool StringToFilterType(const FString& InString, FBreakpoint::EFilterType& OutType);

		/** 触发器载荷的 TVariant：要么是某 Processor 的 ObjectKey，要么是某 ScriptStruct（fragment 类型）的 ObjectKey。 */
		using TriggerVariant = TVariant<
			TObjectKey<const UMassProcessor>,
			TObjectKey<const UScriptStruct>
		>;

		/** 过滤器载荷的 TVariant：要么是具体 Entity handle，要么是一组 fragment requirement。 */
		using FilterVariant = TVariant<
			FMassEntityHandle,
			FMassFragmentRequirements
		>;

		FBreakpointHandle Handle;                    //< 唯一身份
		mutable uint64 HitCount = 0;                 //< 累计命中次数（mutable 让 const 路径也能递增）
		ETriggerType TriggerType = ETriggerType::None;
		EFilterType FilterType = EFilterType::None;
		TriggerVariant Trigger;                      //< 触发对象（按 TriggerType 解读）
		FilterVariant Filter;                        //< 过滤条件（按 FilterType 解读）
		bool bEnabled = false;                       //< 是否真正触发 IDE break

		/**
		 * Apply the breakpoint Filter based on a list of fragment types
		 *
		 * @param Fragments	Array of fragment types to evaluate the breakpoint filter against
		 *
		 * @return True if the filter is set to an applicable type it evaluates to true for the provided Fragments
		 *
		 * 基于 fragment 类型列表评估过滤器——主要用于"实体创建"事件，此时实体尚未真正存在，
		 * 只能根据将要赋予的 fragment 集合做匹配。
		 * 注意 SpecificEntity / SelectedEntity 类型都返回 false（无法仅靠类型列表判定特定实体）。
		 */
		MASSENTITY_API bool ApplyEntityFilterByFragments(const TArray<const UScriptStruct*>& Fragments) const;

		/**
		 * Apply the breakpoint Filter based on an FMassArchetypeHandle
		 *
		 * @param ArchetypeHandle	The archetype to evaluate breakpoint filter against
		 *
		 * @return True if the filter is set to an applicable type it evaluates to true for the provided ArchetypeHandle
		 *
		 * 基于 archetype 评估过滤器——给"实体即将进入某 archetype"的事件用。
		 */
		MASSENTITY_API bool ApplyEntityFilterByArchetype(const FMassArchetypeHandle& ArchetypeHandle) const;

		/**
		 * Evaluate the breakpoint filter against a specific entity
		 *
		 * 完整版本：给已存在的实体做过滤。能正确处理 SpecificEntity/SelectedEntity/Query 全部三种 FilterType。
		 */
		MASSENTITY_API bool ApplyEntityFilter(const FMassEntityManager& EntityManager, const FMassEntityHandle& Entity) const;

		// =================================================================================================
		// Check*Breakpoints 静态函数族——在关键事件路径埋点调用：
		//   遍历所有 environment 的所有 breakpoint，找匹配 TriggerType 且 Filter 通过的 breakpoint，
		//   累加其 HitCount，若 bEnabled 则返回 true 让调用方 break。
		//
		// 性能模式：所有函数第一行都是 `if (LIKELY(!bHasBreakpoint)) return false;`
		// 即没有任何断点时直接返回，热路径几乎零开销。
		// =================================================================================================

		/**
		 * Checks if a fragment add on a given entity should trigger a breakpoint
		 */
		static MASSENTITY_API bool CheckFragmentAddBreakpoints(const FMassEntityHandle& Handle, const UScriptStruct* FragmentType);

		/**
		 * Checks if a fragment add on a given entity should trigger a breakpoint (templated version)
		 *
		 * 可变模板版本：用 fold expression `(... || ...)` 把每个 fragment 类型分别检查，
		 * 任一命中即返回 true。允许调用方写 `CheckFragmentAddBreakpoints(Entity, FFoo{}, FBar{})`。
		 */
		template<typename... TFragments>
		static bool CheckFragmentAddBreakpoints(const FMassEntityHandle& Handle, TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}
			return (... || CheckFragmentAddBreakpoints(Handle, TFragments::StaticStruct()));
		}

		/**
		 * Checks if a fragment add on a group of entities should trigger a breakpoint
		 */
		static MASSENTITY_API bool CheckFragmentAddBreakpoints(TConstArrayView<FMassEntityHandle> Entities, const UScriptStruct* FragmentType);

		/**
		 * Checks if a fragment add on a group of entities should trigger a breakpoint (templated version)
		 */
		template<typename... TFragments>
		static bool CheckFragmentAddBreakpoints(TConstArrayView<FMassEntityHandle> Entities, TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}

			return (... || CheckFragmentAddBreakpoints(Entities, TFragments::StaticStruct()));
		}

		/**
		 * Checks if a fragment add on a group of entities should trigger a breakpoint (fragment instance version)
		 *
		 * "Instance" 版本：传入 fragment 实例引用（而非类型本身），用 std::remove_reference_t
		 * 取出类型再调 StaticStruct()。便于在已有 fragment 实例的代码路径直接调用，无需重复写类型名。
		 */
		template<typename... TFragments>
		static bool CheckFragmentAddBreakpointsByInstances(const FMassEntityHandle& Handle, TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}
			return (... || CheckFragmentAddBreakpoints(Handle, std::remove_reference_t<decltype(InFragments)>::StaticStruct()));
		}

		/**
		 * Checks if a fragment add on a group of entities should trigger a breakpoint (fragment instance version)
		 */
		template<typename... TFragments>
		static bool CheckFragmentAddBreakpointsByInstances(TConstArrayView<FMassEntityHandle> Entities, TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}

			return (... || CheckFragmentAddBreakpoints(Entities, std::remove_reference_t<decltype(InFragments)>::StaticStruct()));
		}

		/**
		 * Checks if a fragment remove on a given entity should trigger a breakpoint
		 */
		static MASSENTITY_API bool CheckFragmentRemoveBreakpoints(const FMassEntityHandle& Handle, const UScriptStruct* FragmentType);

		/**
		 * Checks if a fragment remove on a given entity should trigger a breakpoint (templated version)
		 */
		template<typename... TFragments>
		static bool CheckFragmentRemoveBreakpoints(const FMassEntityHandle& Handle, TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}
			return (... || CheckFragmentRemoveBreakpoints(Handle, std::remove_reference_t<decltype(InFragments)>::StaticStruct()));
		}

		/**
		 * Checks if a fragment remove on a group of entities should trigger a breakpoint
		 */
		static MASSENTITY_API bool CheckFragmentRemoveBreakpoints(TConstArrayView<FMassEntityHandle> Entities, const UScriptStruct* FragmentType);

		/**
		 * Checks if a fragment remove on a group of entities should trigger a breakpoint (templated version)
		 */
		template<typename... TFragments>
		static bool CheckFragmentRemoveBreakpoints(TConstArrayView<FMassEntityHandle> Entities, TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}
			return (... || CheckFragmentRemoveBreakpoints(Entities, TFragments::StaticStruct()));
		}

		/**
		 * Checks if an entity creation event should trigger a breakpoint
		 *
		 * 实体创建事件——按"将要赋予的 fragment 类型集合"过滤；调用 ApplyEntityFilterByFragments。
		 */
		static bool CheckCreateEntityBreakpoints(TArray<const UScriptStruct*> Fragments);

		/**
		 * Checks if an entity creation event should trigger a breakpoint (templated version)
		 */
		template<typename... TFragments>
		static bool CheckCreateEntityBreakpoints(TFragments... InFragments)
		{
			if (LIKELY(!bHasBreakpoint))
			{
				return false;
			}

			return CheckCreateEntityBreakpoints({ TFragments::StaticStruct()... });
		}

		/**
		 * Checks if an entity creation event should trigger a breakpoint (archetype version)
		 *
		 * 已知目标 archetype 时的快路径——直接用 ApplyEntityFilterByArchetype 而不必拆解 fragment 列表。
		 */
		static MASSENTITY_API bool CheckCreateEntityBreakpoints(const FMassArchetypeHandle& ArchetypeHandle);

		/**
		 * Checks if an entity destruction event should trigger a breakpoint
		 */
		static MASSENTITY_API bool CheckDestroyEntityBreakpoints(const FMassEntityHandle& Entity);

		/**
		 * Checks if an entity destruction event should trigger a breakpoint
		 */
		static MASSENTITY_API bool CheckDestroyEntityBreakpoints(TConstArrayView<FMassEntityHandle> Entities);

		/**
		 * Disable the currently-active breakpoint (if any)
		 *
		 * 在 DebugBreak() 函数内通过设 bDisableThisBreakpoint=true 触发——
		 * 把"刚刚命中的"断点禁用，避免反复 break 的烦扰。具体实现是把 LastBreakpointHandle
		 * 指向的断点 bEnabled=false。这个机制需要 IDE 在 DebugBreak() 中被命中后用户手动设置局部变量。
		 */
		static MASSENTITY_API void DisableActiveBreakpoint();

		/**
		 * Checks if any breakpoints are currently set
		 *
		 * 全局快路径标志：只要有任意一个 breakpoint（即使禁用）就返回 true，
		 * 让 HitCount 仍能累计。
		 */
		static inline bool HasBreakpoint()
		{
			return bHasBreakpoint;
		}

		/**
		 * Triggers a breakpoint in the attached debugger (if present)
		 *
		 * 真正的"停下来"实现：调用 UE_DEBUG_BREAK()。无附加 IDE 时为 no-op（FNoDebuggerNotification 会提示用户）。
		 * 函数内部禁用了优化（UE_DISABLE_OPTIMIZATION_SHIP），确保 break 时变量在 watch 窗口中可见。
		 */
		static MASSENTITY_API void DebugBreak();

	private:
		/** 全局快路径标志：是否存在任何断点。RefreshBreakpoints / Set*Breakpoint / Clear* 修改之。 */
		static bool bHasBreakpoint;
		/** 最近一次命中的断点 handle——给 DisableActiveBreakpoint() 找回该断点用。 */
		static FBreakpointHandle LastBreakpointHandle;
	};
	
} // namespace UE::Mass::Debug

#else // WITH_MASSENTITY_DEBUG
// =====================================================================================================================
// 【Shipping 构建分支：所有断点类型塌缩为空 stub】
// 仅保留 FBreakpointHandle / FBreakpoint 的空 struct 与空枚举，让外部代码（断点埋点）能继续编译。
// MASS_BREAKPOINT 宏退化为 `do {} while(0)`——编译器会完全消除。
// =====================================================================================================================
namespace UE::Mass::Debug
{
	struct FBreakpointHandle
	{
	};

	struct FBreakpoint
	{
		enum class ETriggerType : uint8
		{
		};
		enum class EFilterType : uint8
		{
		};
	};
}

#define MASS_BREAKPOINT(...)	\
		do { } while (0)

#endif // WITH_MASSENTITY_DEBUG
