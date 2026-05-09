// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// 【MassDebuggerBreakpoints.cpp —— 断点匹配引擎实现】
//
// 本文件实现 FBreakpoint::ApplyEntityFilter*() 与 Check*Breakpoints() 系列函数。
// 整体算法骨架：
//   1) 顶层快路径：if (!bHasBreakpoint) return false;  —— 几乎所有调用都在此返回
//   2) 遍历 ActiveEnvironments，跳过 Env.bHasBreakpoint=false 的环境
//   3) 用 ProcessorsWithBreakpoints / FragmentsWithBreakpoints 集合 O(1) 判断"该 trigger 是否相关"
//   4) 仅在相关时才线性遍历 Env.Breakpoints 数组
//   5) 对每个候选 breakpoint：trigger 类型/对象匹配 → ApplyEntityFilter → 匹配则 ++HitCount，
//      若 bEnabled 还需 break 则记录 LastBreakpointHandle 并返回 true
//
// 关键设计原则：即使发现一个匹配的 breakpoint 也**不立即 return**，而是继续把同一帧里
// 所有匹配的 breakpoint 的 HitCount 都累加完——这样 UI 显示的 hit count 才准确。
// =====================================================================================================================

#include "MassDebuggerBreakpoints.h"
#if WITH_MASSENTITY_DEBUG
#include "MassDebugger.h"
#include "UObject/Class.h"
#include "UObject/ObjectKey.h"
#include "MassRequirements.h"
#include "MassProcessor.h"

#define LOCTEXT_NAMESPACE "MassDebugger"

namespace UE::Mass::Debug
{
	/**
	 * UE_DISABLE_OPTIMIZATION_SHIP / UE_ENABLE_OPTIMIZATION_SHIP 包裹：
	 * 关闭本函数的优化，确保 IDE 在断点处停下时，bDisableThisBreakpoint 等局部变量
	 * 仍在 watch 窗口中可见、可被用户改写。优化开启时这些变量可能被寄存器 alias 掉。
	 */
	UE_DISABLE_OPTIMIZATION_SHIP
	void FBreakpoint::DebugBreak()
	{
		// 这个局部 bool 是给 IDE watch 窗口"修改局部变量"用的"出口"——
		// 用户在 break 时把它改为 1/true，函数就会顺势调用 DisableActiveBreakpoint()
		// 把当前断点禁用，避免反复打扰。
		bool bDisableThisBreakpoint = false;

		//====================================================================
		//= A breakpoint set in the MassDebugger has triggered
		//= Step out of this function to debug the actual code being run
		// 
		//= To disable this specific breakpoint use the Watch window to set
		//= bDisableThisBreakpoint to `true` or 1
		//====================================================================
		// 真正的"停下来"——平台无关的 debug break 内联汇编/intrinsic。
		// 没有 IDE attach 时此调用会成为 abort，因此 SetXxxBreakpoint() 注册时会
		// 调 FNoDebuggerNotification::ConditionallyNotifyUser() 提醒用户。
		UE_DEBUG_BREAK();

		if (bDisableThisBreakpoint)
		{
			DisableActiveBreakpoint();
		}
	}
	UE_ENABLE_OPTIMIZATION_SHIP

	// 静态成员定义。bHasBreakpoint 是全局快路径标志，所有 Check* 函数第一行检查它。
	bool FBreakpoint::bHasBreakpoint = false;
	FBreakpointHandle FBreakpoint::LastBreakpointHandle = FBreakpointHandle::Invalid();

	FBreakpoint::FBreakpoint()
	{
		// 每个 breakpoint 在构造时立即分配一个唯一 handle。
		Handle = FBreakpointHandle::CreateHandle();
	}

	/**
	 * 【ApplyEntityFilterByFragments —— 按 fragment 列表评估过滤器】
	 *
	 * 用于"实体即将创建"的事件——此时实体尚不存在，只能用类型列表近似匹配。
	 *
	 * Filter 类型分支语义：
	 *   None           ：始终匹配（无过滤）
	 *   SpecificEntity ：无法仅靠类型判断特定实体 → 返回 false
	 *   SelectedEntity ：同理 → 返回 false
	 *   Query          ：把 Filter 中的 FMassFragmentRequirements 与 Fragments 列表逐项比对
	 *                    AllOf/NoneOf 任一不满足即 false；通过后返回 false（注意：bug? 见下）
	 *
	 * 【疑似 bug 1】：Query 分支末尾返回 false 而非 true。如果所有 requirement 都通过，
	 * 该函数返回 false——这意味着 Query 过滤器在 fragment 列表场景下永远不能"通过"，只有 None 能通过。
	 * 看起来应该是返回 true 才对，但需要进一步对照调用方语义确认。
	 */
	bool FBreakpoint::ApplyEntityFilterByFragments(const TArray<const UScriptStruct*>& Fragments) const
	{
		switch (FilterType)
		{
		case EFilterType::None:
			return true;

		case EFilterType::SpecificEntity:
			return false;

		case EFilterType::SelectedEntity:
			return false;

		case EFilterType::Query:
			if (const FMassFragmentRequirements* Requirements = Filter.TryGet<FMassFragmentRequirements>())
			{
				// 普通 fragment 需求逐条匹配
				TConstArrayView<FMassFragmentRequirementDescription> FragmentRequirements = Requirements->GetFragmentRequirements();
				for (const FMassFragmentRequirementDescription& FragmentRequirement : FragmentRequirements)
				{
					if (FragmentRequirement.Presence == EMassFragmentPresence::All
						&& !Fragments.Contains(FragmentRequirement.StructType))
					{
						return false;
					}
					else if (FragmentRequirement.Presence == EMassFragmentPresence::None
						&& Fragments.Contains(FragmentRequirement.StructType))
					{
						return false;
					}
				}

				// chunk fragment 需求同样逐条匹配
				TConstArrayView<FMassFragmentRequirementDescription> ChunkRequirements = Requirements->GetChunkFragmentRequirements();
				for (const FMassFragmentRequirementDescription& FragmentRequirement : ChunkRequirements)
				{
					if (FragmentRequirement.Presence == EMassFragmentPresence::All
						&& !Fragments.Contains(FragmentRequirement.StructType))
					{
						return false;
					}
					else if (FragmentRequirement.Presence == EMassFragmentPresence::None
						&& Fragments.Contains(FragmentRequirement.StructType))
					{
						return false;
					}
				}

				// 必有 tag：通过 BitSet 导出后逐个查 Fragments 列表
				const FMassTagBitSet& RequiredAllTagsBitSet = Requirements->GetRequiredAllTags();
				TArray<const UScriptStruct*> RequiredAllTags;
				RequiredAllTagsBitSet.ExportTypes(RequiredAllTags);
				for (const UScriptStruct* Tag : RequiredAllTags)
				{
					if (!Fragments.Contains(Tag))
					{
						return false;
					}
				}

				// 禁止 tag
				const FMassTagBitSet& BlockedTagsBitSet = Requirements->GetRequiredNoneTags();
				TArray<const UScriptStruct*> BlockedTags;
				BlockedTagsBitSet.ExportTypes(BlockedTags);
				for (const UScriptStruct* Tag : BlockedTags)
				{
					if (Fragments.Contains(Tag))
					{
						return false;
					}
				}
			}
			// 注意：所有 requirement 都通过后这里返回 false——见函数顶部 bug 提示。
			return false;
		default:
			break;
		}

		return true;
	}

	/**
	 * 【ApplyEntityFilterByArchetype —— 按 archetype 评估过滤器】
	 * 用 archetype 内置的 DoesArchetypeMatchRequirements 直接判断；只支持 None/Query 两种 FilterType。
	 */
	bool FBreakpoint::ApplyEntityFilterByArchetype(const FMassArchetypeHandle& ArchetypeHandle) const
	{
		switch (FilterType)
		{
		case EFilterType::None:
			return true;

		case EFilterType::Query:
		{
			if (const FMassFragmentRequirements* Requirements = Filter.TryGet<FMassFragmentRequirements>())
			{
				return Requirements->DoesArchetypeMatchRequirements(ArchetypeHandle);
			}
			return false;
		}
		default:
			break;
		}

		return false;
	}

	/**
	 * 【ApplyEntityFilter —— 完整版过滤器评估】
	 * 已知具体 EntityHandle 时使用，能正确处理所有 4 种 FilterType。
	 *
	 * 这是断点检测最常走的路径——Check*Breakpoints 系列在确定 trigger 匹配后调用本函数做 filter 二次过滤。
	 */
	bool FBreakpoint::ApplyEntityFilter(const FMassEntityManager& EntityManager, const FMassEntityHandle& Entity) const
	{
		switch (FilterType)
		{
		case EFilterType::None:
			return true;

		case EFilterType::SpecificEntity:
		{
			// 直接对比 EntityHandle 是否相等
			if (const FMassEntityHandle* SpecificEntity = Filter.TryGet<FMassEntityHandle>())
			{
				return SpecificEntity && (*SpecificEntity == Entity);
			}
			return false;
		}

		case EFilterType::SelectedEntity:
		{
			// 实时查询当前 selected entity——用户切换焦点时断点目标会自动跟随
			FMassEntityHandle SelectedEntity = FMassDebugger::GetSelectedEntity(EntityManager);
			return SelectedEntity == Entity;
		}

		case EFilterType::Query:
		{
			// 取出实体所在 archetype，用 requirement 验证
			if (const FMassFragmentRequirements* Requirements = Filter.TryGet<FMassFragmentRequirements>())
			{
				FMassArchetypeHandle Archetype = EntityManager.GetArchetypeForEntity(Entity);
				if (Archetype.IsValid())
				{
					return Requirements->DoesArchetypeMatchRequirements(Archetype);
				}
			}
			return false;
		}
		default:
			break;
		}

		return false;
	}

	// ===== TriggerType / FilterType 与字符串的双向转换 ===============================
	// 用于 UI 显示和断点持久化（保存/恢复到配置文件）。
	// =================================================================================

	FString FBreakpoint::TriggerTypeToString(FBreakpoint::ETriggerType InType)
	{
		switch (InType)
		{
		case FBreakpoint::ETriggerType::None:
			return TEXT("None");

		case FBreakpoint::ETriggerType::ProcessorExecute:
			return TEXT("ProcessorExecute");

		case FBreakpoint::ETriggerType::FragmentWrite:
			return TEXT("FragmentWrite");

		case FBreakpoint::ETriggerType::EntityCreate:
			return TEXT("EntityCreate");

		case FBreakpoint::ETriggerType::EntityDestroy:
			return TEXT("EntityDestroy");

		case FBreakpoint::ETriggerType::FragmentAdd:
			return TEXT("FragmentAdd");

		case FBreakpoint::ETriggerType::FragmentRemove:
			return TEXT("FragmentRemove");

		case FBreakpoint::ETriggerType::TagAdd:
			return TEXT("TagAdd");

		case FBreakpoint::ETriggerType::TagRemove:
			return TEXT("TagRemove");
		}
		return TEXT("UnknownTrigger");
	}

	bool FBreakpoint::StringToTriggerType(const FString& InString, FBreakpoint::ETriggerType& OutType)
	{
		if (InString == TEXT("None"))
		{
			OutType = FBreakpoint::ETriggerType::None;
			return true;
		}
		if (InString == TEXT("ProcessorExecute"))
		{
			OutType = FBreakpoint::ETriggerType::ProcessorExecute;
			return true;
		}
		if (InString == TEXT("FragmentWrite"))
		{
			OutType = FBreakpoint::ETriggerType::FragmentWrite;
			return true;
		}
		if (InString == TEXT("EntityCreate"))
		{
			OutType = FBreakpoint::ETriggerType::EntityCreate;
			return true;
		}
		if (InString == TEXT("EntityDestroy"))
		{
			OutType = FBreakpoint::ETriggerType::EntityDestroy;
			return true;
		}
		if (InString == TEXT("FragmentAdd"))
		{
			OutType = FBreakpoint::ETriggerType::FragmentAdd;
			return true;
		}
		if (InString == TEXT("FragmentRemove"))
		{
			OutType = FBreakpoint::ETriggerType::FragmentRemove;
			return true;
		}
		if (InString == TEXT("TagAdd"))
		{
			OutType = FBreakpoint::ETriggerType::TagAdd;
			return true;
		}
		if (InString == TEXT("TagRemove"))
		{
			OutType = FBreakpoint::ETriggerType::TagRemove;
			return true;
		}

		return false;
	}


	FString FBreakpoint::FilterTypeToString(FBreakpoint::EFilterType InType)
	{
		switch (InType)
		{
		case FBreakpoint::EFilterType::None:
			return TEXT("None");

		case FBreakpoint::EFilterType::SpecificEntity:
			return TEXT("SpecificEntity");

		case FBreakpoint::EFilterType::SelectedEntity:
			return TEXT("SelectedEntity");

		case FBreakpoint::EFilterType::Query:
			return TEXT("Query");
		}

		return TEXT("UnknownFilter");
	}

	bool FBreakpoint::StringToFilterType(const FString& InString, FBreakpoint::EFilterType& OutType)
	{
		if (InString == TEXT("None"))
		{
			OutType = FBreakpoint::EFilterType::None;
			return true;
		}
		if (InString == TEXT("SpecificEntity"))
		{
			OutType = FBreakpoint::EFilterType::SpecificEntity;
			return true;
		}
		if (InString == TEXT("SelectedEntity"))
		{
			OutType = FBreakpoint::EFilterType::SelectedEntity;
			return true;
		}
		if (InString == TEXT("Query"))
		{
			OutType = FBreakpoint::EFilterType::Query;
			return true;
		}

		return false;
	}

	/**
	 * 【CheckDestroyEntityBreakpoints —— 实体销毁断点检查】
	 *
	 * 算法：
	 *   1. 全局快路径：!bHasBreakpoint 直接 return false
	 *   2. 遍历所有 environment（每个 EntityManager 一份）
	 *   3. 跳过 Env.bHasBreakpoint=false 的 environment
	 *   4. 遍历该 env 的 Breakpoints，找 TriggerType==EntityDestroy 的
	 *   5. 对每个候选调 ApplyEntityFilter 二次过滤
	 *   6. 命中即递增 HitCount；若 bEnabled 则记 LastBreakpointHandle 并设 bShouldBreak
	 *      （注意不立即 return——继续把同一帧里所有匹配的 HitCount 都累加）
	 *   7. 当前 env 处理完后才统一 return bShouldBreak
	 */
	bool FBreakpoint::CheckDestroyEntityBreakpoints(const FMassEntityHandle& Entity)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		for (const FMassDebugger::FEnvironment& Env : FMassDebugger::GetEnvironments())
		{
			if (!Env.bHasBreakpoint)
			{
				continue;
			}

			if (TSharedPtr<const FMassEntityManager> Manager = Env.EntityManager.Pin())
			{
				bool bShouldBreak = false;
				for (const FBreakpoint& Breakpoint : Env.Breakpoints)
				{
					if (Breakpoint.TriggerType == FBreakpoint::ETriggerType::EntityDestroy
						&& Breakpoint.ApplyEntityFilter(*Manager, Entity))
					{
						++Breakpoint.HitCount;
						if (Breakpoint.bEnabled)
						{
							LastBreakpointHandle = Breakpoint.Handle;
							// Set flag to break but check the rest of the breakpoints to keep accurate HitCounts
							bShouldBreak = true;
						}
					}
				}
				if (bShouldBreak)
				{
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * 数组版：对每个实体逐个调用单实体版本。
	 * 注意 `|` 而非 `||`——不短路，确保所有实体的 HitCount 都被正确累加。
	 */
	bool FBreakpoint::CheckDestroyEntityBreakpoints(TConstArrayView<FMassEntityHandle> Entities)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		bool ShouldBreak = false;
		for (const FMassEntityHandle& Handle : Entities)
		{
			// need to check them all before returning to ensure hitcount is accurate
			ShouldBreak = CheckDestroyEntityBreakpoints(Handle) | ShouldBreak;
		}
		return ShouldBreak;
	}

	/**
	 * 【CheckFragmentAddBreakpoints —— fragment 添加断点检查】
	 *
	 * 比 CheckDestroyEntityBreakpoints 多一道加速：先用
	 * `Env.FragmentsWithBreakpoints.Contains(FragmentType)` 判断该 fragment
	 * 是否被任何断点关注，避免无关断点进入慢路径。
	 */
	bool FBreakpoint::CheckFragmentAddBreakpoints(const FMassEntityHandle& Entity, const UScriptStruct* FragmentType)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		for (const FMassDebugger::FEnvironment& Env : FMassDebugger::GetEnvironments())
		{
			if (!Env.bHasBreakpoint)
			{
				continue;
			}
			
			if (TSharedPtr<const FMassEntityManager> ManagerPtr = Env.EntityManager.Pin())
			{
				// O(1) 集合查询：该 fragment 类型是否有任何断点关联
				if (Env.FragmentsWithBreakpoints.Contains(TObjectKey<const UScriptStruct>(FragmentType)))
				{
					bool bShouldBreak = false;

					for (const FBreakpoint& Breakpoint : Env.Breakpoints)
					{
						// 仅处理 FragmentAdd trigger 且 trigger 载荷是 ScriptStruct（fragment 类型）
						if (Breakpoint.TriggerType != FBreakpoint::ETriggerType::FragmentAdd
							|| !Breakpoint.Trigger.IsType<TObjectKey<const UScriptStruct>>())
						{
							continue;
						}

						const UScriptStruct* BreakpointFragmentType = Breakpoint.Trigger.Get<TObjectKey<const UScriptStruct>>().ResolveObjectPtr();

						if (BreakpointFragmentType == FragmentType && Breakpoint.ApplyEntityFilter(*ManagerPtr, Entity))
						{
							++Breakpoint.HitCount;
							if (Breakpoint.bEnabled)
							{
								LastBreakpointHandle = Breakpoint.Handle;
								bShouldBreak = true;
							}
						}
					}
					if (bShouldBreak)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * 数组版的 fragment 添加检查：早退优化（任一实体命中即返回）。
	 * 与 Destroy 版不同——这里用了 if-return 短路而非 `|=`，意味着后续实体的 HitCount
	 * 不会被累加。一致性方面与 Destroy 版略有差异。
	 */
	bool FBreakpoint::CheckFragmentAddBreakpoints(TConstArrayView<FMassEntityHandle> Entities, const UScriptStruct* FragmentType)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		for (const FMassEntityHandle& Handle : Entities)
		{
			if (FBreakpoint::CheckFragmentAddBreakpoints(Handle, FragmentType))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * 【CheckFragmentRemoveBreakpoints —— fragment 移除断点检查】
	 * 与 FragmentAdd 几乎对称，区别仅在 TriggerType 比较的常量。
	 */
	bool FBreakpoint::CheckFragmentRemoveBreakpoints(const FMassEntityHandle& Handle, const UScriptStruct* FragmentType)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		for (const FMassDebugger::FEnvironment& Env : FMassDebugger::GetEnvironments())
		{
			if (!Env.bHasBreakpoint)
			{
				continue;
			}

			if (TSharedPtr<const FMassEntityManager> ManagerPtr = Env.EntityManager.Pin())
			{
				if (Env.FragmentsWithBreakpoints.Contains(TObjectKey<const UScriptStruct>(FragmentType)))
				{
					bool bShouldBreak = false;

					for (const FBreakpoint& Breakpoint : Env.Breakpoints)
					{
						if (Breakpoint.TriggerType != FBreakpoint::ETriggerType::FragmentRemove
							|| !Breakpoint.Trigger.IsType<TObjectKey<const UScriptStruct>>())
						{
							continue;
						}

						const UScriptStruct* BreakpointFragmentType = Breakpoint.Trigger.Get<TObjectKey<const UScriptStruct>>().ResolveObjectPtr();

						if (BreakpointFragmentType == FragmentType && Breakpoint.ApplyEntityFilter(*ManagerPtr, Handle))
						{
							++Breakpoint.HitCount;
							if (Breakpoint.bEnabled)
							{
								LastBreakpointHandle = Breakpoint.Handle;
								bShouldBreak = true;
							}
						}
					}
					if (bShouldBreak)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * 【CheckCreateEntityBreakpoints（fragment 列表版）—— 实体创建断点检查】
	 *
	 * 实体即将被创建——此时还没有 EntityHandle，只能用即将赋予的 fragment 类型列表
	 * 调 ApplyEntityFilterByFragments 做粗粒度过滤。
	 */
	bool FBreakpoint::CheckCreateEntityBreakpoints(TArray<const UScriptStruct*> Fragments)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		for (const FMassDebugger::FEnvironment& Env : FMassDebugger::GetEnvironments())
		{
			if (!Env.bHasBreakpoint)
			{
				continue;
			}

			bool bShouldBreak = false;

			for (const FBreakpoint& Breakpoint : Env.Breakpoints)
			{
				if (Breakpoint.TriggerType != FBreakpoint::ETriggerType::EntityCreate)
				{
					continue;
				}

				if (Breakpoint.ApplyEntityFilterByFragments(Fragments))
				{
					++Breakpoint.HitCount;
					if (Breakpoint.bEnabled)
					{
						LastBreakpointHandle = Breakpoint.Handle;
						bShouldBreak = true;
					}
				}
			}
			if (bShouldBreak)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * 【CheckCreateEntityBreakpoints（archetype 版）】
	 * 已确定 archetype 时的快路径——用 ApplyEntityFilterByArchetype 直接做精确匹配。
	 */
	bool FBreakpoint::CheckCreateEntityBreakpoints(const FMassArchetypeHandle& ArchetypeHandle)
	{
		if (LIKELY(!bHasBreakpoint))
		{
			return false;
		}

		for (const FMassDebugger::FEnvironment& Env : FMassDebugger::GetEnvironments())
		{
			if (!Env.bHasBreakpoint)
			{
				continue;
			}

			bool bShouldBreak = false;

			for (const FBreakpoint& Breakpoint : Env.Breakpoints)
			{
				if (Breakpoint.TriggerType != FBreakpoint::ETriggerType::EntityCreate)
				{
					continue;
				}

				if (Breakpoint.ApplyEntityFilterByArchetype(ArchetypeHandle))
				{
					++Breakpoint.HitCount;
					if (Breakpoint.bEnabled)
					{
						LastBreakpointHandle = Breakpoint.Handle;
						bShouldBreak = true;
					}
				}
			}
			if (bShouldBreak)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * 把"刚刚命中的"断点禁用——通过 LastBreakpointHandle 记忆完成转发。
	 * 用户在 IDE 内手动改 bDisableThisBreakpoint=true 触发。
	 */
	void FBreakpoint::DisableActiveBreakpoint()
	{
		FMassDebugger::SetBreakpointEnabled(LastBreakpointHandle, false);
	}

} // namespace UE::Mass::Debug

#undef LOCTEXT_NAMESPACE
#endif // WITH_MASSENTITY_DEBUG
