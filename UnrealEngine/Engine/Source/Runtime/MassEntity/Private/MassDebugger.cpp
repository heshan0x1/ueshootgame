// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// 【MassDebugger.cpp —— FMassDebugger 静态接口的实现 + 控制台命令注册中心】
//
// 本文件是 Mass 调试系统最大的实现文件（约 1600 行），承担三大职责：
//
//   职责一：注册"mass.debug.*"和"mass.*"控制台命令族（约前 500 行）
//     - mass.debug.SetDebugEntityRange / DebugEntity / ResetDebugEntity：选定调试实体
//     - mass.debug.DestroyEntity：通过控制台销毁实体
//     - mass.debug.SetFragmentBreakpoint / ClearFragmentBreakpoint：动态布置/清除断点
//     - mass.PrintEntityFragments / LogArchetypes / LogFragmentSizes / LogMemoryUsage / LogKnownFragments
//     - mass.RecacheQueries
//   这些命令是程序员日常调试 Mass 内部状态的"瑞士军刀"。
//
//   职责二：FMassDebugger 静态成员定义与方法实现（中段）
//     - 委托/容器的静态实例
//     - 所有内省 API（GetXxx / EnumerateXxx）通过 friend 关系直接读取 Mass 内部字段
//     - 实体选择/高亮、Environment 注册、Provider 注册
//
//   职责三：断点管理（后段）
//     - Should*Break / Has*Breakpoint：热路径查询
//     - Set/Clear*Breakpoint：增删断点 + 维护 ProcessorsWithBreakpoints/FragmentsWithBreakpoints 加速集合
//     - RefreshBreakpoints：从 Breakpoints 数组重建加速集合（在外部修改后调用）
//     - GetFragmentTypeFromName：fragment 名→类型反查（通过扫描 archetype 收集类型缓存）
//
// 整个文件被 #if WITH_MASSENTITY_DEBUG / #endif 包裹——Shipping 构建中除了空 USTRUCT 注册
// 几乎不会编译任何代码，且无可执行符号。
// =====================================================================================================================

#include "MassDebugger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassDebugger)
#if WITH_MASSENTITY_DEBUG
#include "Algo/ForEach.h"
#include "MassProcessor.h"
#include "MassEntityManager.h"
#include "MassEntityManagerStorage.h"
#include "MassEntitySubsystem.h"
#include "MassArchetypeTypes.h"
#include "MassArchetypeData.h"
#include "MassRequirements.h"
#include "MassEntityQuery.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MassEntityUtils.h"
#include "MassCommandBuffer.h"
#include "MassEntityTrace.h"
#include "Misc/StringOutputDevice.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformMisc.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

#define LOCTEXT_NAMESPACE "MassDebugger"

namespace UE::Mass::Debug
{
	// ===== 三个核心调试开关（CVar）的真实存储 =======================================
	// 它们在 MassDebugger.h 中通过 extern 声明，定义在这里。
	// 通过 FAutoConsoleVariableRef 注册到控制台后，可在 ~ 控制台动态切换。
	// =================================================================================
	bool bAllowProceduralDebuggedEntitySelection = false;
	bool bAllowBreakOnDebuggedEntity = false;
	bool bTestSelectedEntityAgainstProcessorQueries = true;

	/**
	 * 把上面三个 CVar 注册到控制台。ECVF_Cheat 标记表明这些是开发用变量，
	 * Shipping 构建里默认不可在 GUI 控制台中显示（除非 Cheat 系统启用）。
	 */
	FAutoConsoleVariableRef CVars[] =
	{
		{ TEXT("mass.debug.AllowProceduralDebuggedEntitySelection"), bAllowProceduralDebuggedEntitySelection
			, TEXT("Guards whether MASS_SET_ENTITY_DEBUGGED calls take effect."), ECVF_Cheat}
		, {TEXT("mass.debug.AllowBreakOnDebuggedEntity"), bAllowBreakOnDebuggedEntity
			, TEXT("Guards whether MASS_BREAK_IF_ENTITY_DEBUGGED calls take effect."), ECVF_Cheat}
		, {	TEXT("mass.debug.TestSelectedEntityAgainstProcessorQueries"), bTestSelectedEntityAgainstProcessorQueries
			, TEXT("Enabling will result in testing all processors' queries against SelectedEntity (as indicated by")
			TEXT("mass.debug.DebugEntity or the gameplay debugger) and storing potential failure results to be viewed in MassDebugger")
			, ECVF_Cheat }
	};

	/**
	 * 【FNoDebuggerNotification —— "断点已设但 IDE 未挂载"提醒器】
	 *
	 * 用户用 mass.debug.SetFragmentBreakpoint 命令设了断点，结果触发时却没有 IDE attach——
	 * UE_DEBUG_BREAK() 实际上会 abort（或被 SEH 吞掉）。本结构通过对比"上次和现在"的
	 * IsDebuggerPresent() 状态，在检测到 attach→detach 边沿时弹一个对话框告知用户。
	 *
	 * 状态机：
	 *   - bDebuggerPresent：上次轮询的 IDE 状态
	 *   - bUserNotified：本次 detach 状态下是否已经弹过框（避免重复打扰）
	 *   - 当 detach 边沿出现且未通知 → 弹框 + 标记 notified
	 *   - 当 attach 重新出现 → 重置 notified（下次 detach 还会弹）
	 */
	struct FNoDebuggerNotification
	{
		static void ConditionallyNotifyUser()
		{
			const bool bLocalDebuggerPresent = FPlatformMisc::IsDebuggerPresent();
			if (bDebuggerPresent != bLocalDebuggerPresent)
			{
				bDebuggerPresent = bLocalDebuggerPresent;
				if (bLocalDebuggerPresent == false)
				{
					if (bUserNotified == false)
					{
						FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoDebuggerAttached", "Breakpoint set but no debugger is attached."));
						bUserNotified = true;
					}
				}
				else
				{
					bUserNotified = false;
				}
			}
		}
		static bool bDebuggerPresent;
		static bool bUserNotified;
	};
	bool FNoDebuggerNotification::bDebuggerPresent = false;
	bool FNoDebuggerNotification::bUserNotified = false;
	

	/** EMassFragmentAccess → 短串："--"/"RO"/"RW"。日志里以紧凑形式展示 fragment 访问权限。 */
	FString DebugGetFragmentAccessString(EMassFragmentAccess Access)
	{
		switch (Access)
		{
		case EMassFragmentAccess::None:	return TEXT("--");
		case EMassFragmentAccess::ReadOnly:	return TEXT("RO");
		case EMassFragmentAccess::ReadWrite:	return TEXT("RW");
		default:
			ensureMsgf(false, TEXT("Missing string conversion for EMassFragmentAccess=%d"), Access);
			break;
		}
		return TEXT("Missing string conversion");
	}

	void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar)
	{
		const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
		Ar.SetAutoEmitLineTerminator(false);
		for (const UMassProcessor* Proc : Processors)
		{
			if (Proc)
			{
				Proc->DebugOutputDescription(Ar);
				Ar.Logf(TEXT("\n"));
			}
			else
			{
				Ar.Logf(TEXT("NULL\n"));
			}
		}
		Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
	}

	// First Id of a range of lightweight entity for which we want to activate debug information
	// 【全局调试实体范围】这两个 int 是"被调试关注"的 entity index 范围；
	// 由控制台命令 mass.debug.SetDebugEntityRange / DebugEntity / ResetDebugEntity 修改。
	// 注意它们是 *namespace 全局变量*，不属于任何 EntityManager——
	// 也就是说所有 EntityManager 共享同一组范围（这与 FEnvironment::SelectedEntity 不同）。
	int32 DebugEntityBegin = INDEX_NONE;

	// Last Id of a range of lightweight entity for which we want to activate debug information
	int32 DebugEntityEnd = INDEX_NONE;

	void SetDebugEntityRange(const int32 InDebugEntityBegin, const int32 InDebugEntityEnd)
	{
		DebugEntityBegin = InDebugEntityBegin;
		DebugEntityEnd = InDebugEntityEnd;
	}

	/**
	 * 控制台命令：mass.debug.SetDebugEntityRange <First> <Last>
	 * 设定调试实体的索引范围。GameplayDebugger 等系统会用此范围过滤可视化。
	 */
	static FAutoConsoleCommand SetDebugEntityRangeCommand(
		TEXT("mass.debug.SetDebugEntityRange"),
		TEXT("Range of lightweight entity IDs that we want to debug.")
		TEXT("Usage: \"mass.debug.SetDebugEntityRange <FirstEntity> <LastEntity>\""),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() != 2)
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 2 parameters"));
					return;
				}

				int32 FirstID = INDEX_NONE;
				int32 LastID = INDEX_NONE;
				if (!LexTryParseString<int32>(FirstID, *Args[0]))
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Error: first parameter must be an integer"));
					return;
				}
			
				if (!LexTryParseString<int32>(LastID, *Args[1]))
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Error: second parameter must be an integer"));
					return;
				}

				SetDebugEntityRange(FirstID, LastID);
			}));

	static FAutoConsoleCommand ResetDebugEntity(
		TEXT("mass.debug.ResetDebugEntity"),
		TEXT("Disables lightweight entities debugging.")
		TEXT("Usage: \"mass.debug.ResetDebugEntity\""),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				SetDebugEntityRange(INDEX_NONE, INDEX_NONE);
			}));

	bool HasDebugEntities()
	{
		return DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE;
	}

	/** 是否调试范围只覆盖单个实体（Begin == End）。GameplayDebugger 用作"显示哪一只"。 */
	bool IsDebuggingSingleEntity()
	{
		return DebugEntityBegin != INDEX_NONE && DebugEntityBegin == DebugEntityEnd;
	}

	bool GetDebugEntitiesRange(int32& OutBegin, int32& OutEnd)
	{
		OutBegin = DebugEntityBegin;
		OutEnd = DebugEntityEnd;
		return DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE && DebugEntityBegin <= DebugEntityEnd;
	}
	
	/** 判断指定实体是否落入调试范围；可同时返回该实体的稳定调色板色（按 Index 取色）。 */
	bool IsDebuggingEntity(FMassEntityHandle Entity, FColor* OutEntityColor)
	{
		const int32 EntityIdx = Entity.Index;
		const bool bIsDebuggingEntity = (DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE && DebugEntityBegin <= EntityIdx && EntityIdx <= DebugEntityEnd);
	
		if (bIsDebuggingEntity && OutEntityColor != nullptr)
		{
			*OutEntityColor = GetEntityDebugColor(Entity);
		}

		return bIsDebuggingEntity;
	}

	/** 按 Entity.Index 在 GColorList（全局可读色板）中取色——同 Index 永远同色，
	 *  方便在轨迹图、画面叠加上跨帧追踪同一实体。 */
	FColor GetEntityDebugColor(FMassEntityHandle Entity)
	{
		const int32 EntityIdx = Entity.Index;
		return EntityIdx != INDEX_NONE ? GColorList.GetFColorByIndex(EntityIdx % GColorList.GetColorsNum()) : FColor::Black;
	}

	// ===== 控制台命令注册区块 =========================================================
	// 下面这一大批 FAutoConsoleCommand* 静态对象在模块加载时把命令登记到全局命令系统。
	// 模块卸载时静态对象析构会自动注销。
	// =================================================================================

	/** mass.PrintEntityFragments <Index> ：打印指定 entity 的所有 fragment 值。 */
	FAutoConsoleCommandWithWorldArgsAndOutputDevice PrintEntityFragmentsCmd(
		TEXT("mass.PrintEntityFragments"),
		TEXT("Prints all fragment types and values (uproperties) for the specified Entity index"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
			[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				check(World);
				if (UMassEntitySubsystem* EntityManager = World->GetSubsystem<UMassEntitySubsystem>())
				{
					int32 Index = INDEX_NONE;
					if (LexTryParseString<int32>(Index, *Params[0]))
					{
						FMassDebugger::OutputEntityDescription(Ar, EntityManager->GetEntityManager(), Index);
					}
					else
					{
						Ar.Logf(ELogVerbosity::Error, TEXT("Entity index parameter must be an integer"));
					}
				}
				else
				{
					Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find MassEntitySubsystem for world %s"), *GetPathNameSafe(World));
				}
			})
	);

	/** mass.LogArchetypes [bIncludeEmpty]：把所有 World 下的 archetype 描述打印出来。
	 *  默认包含空 archetype；适合排查"为什么有这么多 archetype"。 */
	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogArchetypesCmd(
		TEXT("mass.LogArchetypes"),
		TEXT("Dumps description of archetypes to log. Optional parameter controls whether to include or exclude non-occupied archetypes. Defaults to 'include'."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld*, FOutputDevice& Ar)
			{
				const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
				for (const FWorldContext& Context : WorldContexts)
				{
					UWorld* World = Context.World();
					if (World == nullptr || World->IsPreviewWorld())
					{
						continue;
					}

					Ar.Logf(ELogVerbosity::Log, TEXT("Dumping description of archetypes for world: %s (%s - %s)"),
						*GetPathNameSafe(World),
						LexToString(World->WorldType),
						*ToString(World->GetNetMode()));

					if (UMassEntitySubsystem* EntityManager = World->GetSubsystem<UMassEntitySubsystem>())
					{
						bool bIncludeEmpty = true;
						if (Params.Num())
						{
							LexTryParseString(bIncludeEmpty, *Params[0]);
						}
						Ar.Logf(ELogVerbosity::Log, TEXT("Include empty archetypes: %s"), bIncludeEmpty ? TEXT("TRUE") : TEXT("FALSE"));
						EntityManager->GetEntityManager().DebugGetArchetypesStringDetails(Ar, bIncludeEmpty);
					}
					else
					{
						Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find MassEntitySubsystem for world: %s (%s - %s)"),
							*GetPathNameSafe(World),
							LexToString(World->WorldType),
							*ToString(World->GetNetMode()));
					}
				}
			})
	);

	// @todo these console commands will be reparented to "massentities" domain once we rename and shuffle the modules around 
	/** mass.RecacheQueries：强制让所有 EntityQuery 重新缓存自己关联的 archetype 列表。
	 *  内部通过递增 ArchetypeDataVersion 让 query 下次执行时检测到变更并 recache。
	 *  常用于诊断"我新加了 archetype 但 query 没识别"的问题。 */
	FAutoConsoleCommandWithWorld RecacheQueries(
		TEXT("mass.RecacheQueries"),
		TEXT("Forces EntityQueries to recache their valid archetypes"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld)
			{
				check(InWorld);
				if (UMassEntitySubsystem* System = InWorld->GetSubsystem<UMassEntitySubsystem>())
				{
					System->GetMutableEntityManager().DebugForceArchetypeDataVersionBump();
				}
			}
	));

	/** mass.LogFragmentSizes：列出全局已注册的所有 FMass*Fragment 类型及其字节尺寸。
	 *  用于排查"哪个 fragment 太大导致 chunk 容量过小"。 */
	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogFragmentSizes(
		TEXT("mass.LogFragmentSizes"),
		TEXT("Logs all the fragment types being used along with their sizes."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				for (const TWeakObjectPtr<const UScriptStruct>& WeakStruct : FMassFragmentBitSet::DebugGetAllStructTypes())
				{
					if (const UScriptStruct* StructType = WeakStruct.Get())
					{
						Ar.Logf(ELogVerbosity::Log, TEXT("%s, size: %d"), *StructType->GetName(), StructType->GetStructureSize());
					}
				}
			})
	);

	/** mass.LogMemoryUsage：打印 MassEntitySubsystem 的总内存占用（含 archetype、chunk、命令缓冲等）。 */
	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogMemoryUsage(
		TEXT("mass.LogMemoryUsage"),
		TEXT("Logs how much memory the mass entity system uses"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				check(World);
				if (UMassEntitySubsystem* System = World->GetSubsystem<UMassEntitySubsystem>())
				{
					FResourceSizeEx CumulativeResourceSize;
					System->GetResourceSizeEx(CumulativeResourceSize);
					Ar.Logf(ELogVerbosity::Log, TEXT("MassEntity system uses: %d bytes"), CumulativeResourceSize.GetDedicatedSystemMemoryBytes());
				}
			}));

	/** mass.LogKnownFragments：打印全部已知 tag / fragment / shared fragment / chunk fragment
	 *  及其在 BitSet 中的下标。下标对调试 BitSet 比较意义重大。 */
	FAutoConsoleCommandWithOutputDevice LogFragments(
		TEXT("mass.LogKnownFragments"),
		TEXT("Logs all the known tags and fragments along with their \"index\" as stored via bitsets."),
		FConsoleCommandWithOutputDeviceDelegate::CreateStatic([](FOutputDevice& OutputDevice)
			{
				auto PrintKnownTypes = [&OutputDevice](TConstArrayView<TWeakObjectPtr<const UScriptStruct>> AllStructs) {
					int i = 0;
					for (TWeakObjectPtr<const UScriptStruct> Struct : AllStructs)
					{
						if (Struct.IsValid())
						{
							OutputDevice.Logf(TEXT("\t%d. %s"), i++, *Struct->GetName());
						}
					}
				};

				OutputDevice.Logf(TEXT("Known tags:"));
				PrintKnownTypes(FMassTagBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Fragments:"));
				PrintKnownTypes(FMassFragmentBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Shared Fragments:"));
				PrintKnownTypes(FMassSharedFragmentBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Chunk Fragments:"));
				PrintKnownTypes(FMassChunkFragmentBitSet::DebugGetAllStructTypes());
			}));

	/** mass.debug.DestroyEntity <ID>：通过 deferred 命令缓冲销毁指定实体——
	 *  不立即销毁是因为 destroy 在很多代码路径上有约束，必须排队到安全点。 */
	static FAutoConsoleCommandWithWorldAndArgs DestroyEntity(
		TEXT("mass.debug.DestroyEntity"),
		TEXT("ID of a Mass entity that we want to destroy.")
		TEXT("Usage: \"mass.debug.DestoryEntity <Entity>\""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 1 parameter"));
			return;
		}

		int32 ID = INDEX_NONE;
		if (!LexTryParseString<int32>(ID, *Args[0]))
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: parameter must be an integer"));
			return;
		}

		if (!World)
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: invalid world"));
			return;
		}

		FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(*World);
		FMassEntityHandle EntityToDestroy = EntityManager.CreateEntityIndexHandle(ID);
		if (!EntityToDestroy.IsSet())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("Error: cannot find entity for this index"));
			return;
		}

		EntityManager.Defer().DestroyEntity(EntityToDestroy);
	}));

	/** mass.debug.DebugEntity <ID>：选定调试实体——等价于 GameplayDebugger 拾取。
	 *  会同时设置 DebugEntityRange=[ID,ID] 并调 SelectEntity 走完整选定流程（广播委托）。 */
	static FAutoConsoleCommandWithWorldAndArgs SetDebugEntity(
		TEXT("mass.debug.DebugEntity"),
		TEXT("ID of a Mass entity that we want to debug.")
		TEXT("Note that this call results in the same behavior as if the entity was picked via the Mass GameplayDebugger's category.")
		TEXT("Usage: \"mass.debug.DebugEntity <Entity>\""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: invalid world"));
				return;
			}

			int32 ID = INDEX_NONE;
			if (Args.Num() > 0)
			{
				LexTryParseString<int32>(ID, *Args[0]);
			}

			SetDebugEntityRange(ID, ID);

			FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(*World);
			FMassEntityHandle EntityToDebug = EntityManager.CreateEntityIndexHandle(ID);
			if (!EntityToDebug.IsSet() && ID != INDEX_NONE)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Cannot find entity for this index, clearing current selection"));
				return;
			}

			FMassDebugger::SelectEntity(EntityManager, EntityToDebug);
		}
	));

	/**
	 * 按部分名查 fragment / shared fragment / const shared fragment 的 UScriptStruct。
	 * 仅在 WITH_STRUCTUTILS_DEBUG 启用时工作（此宏依赖 StructUtils 模块的调试支持）。
	 *
	 * 注意：此函数 *不* 查 chunk fragment 和 tag。
	 */
	const UScriptStruct* FindElementTypeByName(const FString& PartialFragmentName)
	{
		const UScriptStruct* Result = nullptr;
#if WITH_STRUCTUTILS_DEBUG
		Result = FMassFragmentBitSet::DebugFindTypeByPartialName(PartialFragmentName);
		if (Result == nullptr)
		{
			Result = FMassSharedFragmentBitSet::DebugFindTypeByPartialName(PartialFragmentName);
		}
		if (Result == nullptr)
		{
			Result = FMassConstSharedFragmentBitSet::DebugFindTypeByPartialName(PartialFragmentName);
		}
#endif // WITH_STRUCTUTILS_DEBUG
		return Result;
	}

	/** mass.debug.SetFragmentBreakpoint <Type1> [Type2 ...]：在 selected entity 上对
	 *  指定的若干 fragment 类型设置写入断点。前提：先用 mass.debug.DebugEntity 选中实体。 */
	static FAutoConsoleCommandWithWorldAndArgs SetFragmentBreakpoint(
		TEXT("mass.debug.SetFragmentBreakpoint"),
		TEXT("Enables fragment write break-point on an arbitrary number of fragment types, on the selected entity (see `mass.debug.DebugEntity`).")
		TEXT("Usage: `mass.debug.SetFragmentBreakpoint <FragmentTypeName> <FragmentTypeName2> <FragmentTypeName3> <...>`"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: invalid world"));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("No fragment types indicated"));
			}
			else
			{
				FMassEntityManager& EntityManager = Utils::GetEntityManagerChecked(*World);
				FMassEntityHandle SelectedEntity = FMassDebugger::GetSelectedEntity(EntityManager);
				if (SelectedEntity.IsValid())
				{
					for (const FString& PartialFragmentName : Args)
					{
						if (const UScriptStruct* FragmentType = FindElementTypeByName(PartialFragmentName))
						{
							FMassDebugger::SetFragmentWriteBreakpoint(EntityManager, FragmentType, SelectedEntity);
						}
						else
						{
							UE_LOG(LogConsoleResponse, Display, TEXT("Warning: Unable to find element type %s"), *PartialFragmentName);
						}
					}
				}
				else
				{
					UE_LOG(LogConsoleResponse, Display, TEXT("Warning: No entity selected, no break points set"));
				}
			}
		}
	));

	/** mass.debug.ClearFragmentBreakpoint <Type1> [Type2 ...]：清除断点。
	 *  特别注意：若当前没有 selected entity，则会清除所有实体上对该类型的断点。 */
	static FAutoConsoleCommandWithWorldAndArgs ClearFragmentBreakpoint(
		TEXT("mass.debug.ClearFragmentBreakpoint"),
		TEXT("Clears fragment write break-point on an arbitrary number of fragment types, on the selected entity (see `mass.debug.DebugEntity`).")
		TEXT("If no entity is currently selected then the call will clear the type breakpoints on all entities.")
		TEXT("Usage: `mass.debug.ClearFragmentBreakpoint <FragmentTypeName> <FragmentTypeName2> <FragmentTypeName3> <...>`"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: invalid world"));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("No fragment types indicated"));
			}
			else
			{
				FMassEntityManager& EntityManager = Utils::GetEntityManagerChecked(*World);
				FMassEntityHandle SelectedEntity = FMassDebugger::GetSelectedEntity(EntityManager);
				const bool bEntityValid = SelectedEntity.IsValid();

				for (const FString& PartialFragmentName : Args)
				{
					if (const UScriptStruct* FragmentType = FindElementTypeByName(PartialFragmentName))
					{
						bEntityValid
							? FMassDebugger::ClearFragmentWriteBreak(EntityManager, FragmentType, SelectedEntity)
							: FMassDebugger::ClearFragmentWriteBreak(EntityManager, FragmentType, FMassEntityHandle());
					}
					else
					{
						UE_LOG(LogConsoleResponse, Display, TEXT("Warning: Unable to find element type %s"), *PartialFragmentName);
					}
				}
			}
		}
	));

} // namespace UE::Mass::Debug

//----------------------------------------------------------------------//
// FMassDebugger
//----------------------------------------------------------------------//
// 静态成员定义。委托/容器/锁全部为模块级 globals，由 ShutdownDebugger 显式清理。
FMassDebugger::FOnBreakpointsChanged FMassDebugger::OnBreakpointsChangedDelegate;
FMassDebugger::FOnEntitySelected FMassDebugger::OnEntitySelectedDelegate;

FMassDebugger::FOnMassEntityManagerEvent FMassDebugger::OnEntityManagerInitialized;
FMassDebugger::FOnMassEntityManagerEvent FMassDebugger::OnEntityManagerDeinitialized;
FMassDebugger::FOnEnvironmentEvent FMassDebugger::OnProcessorProviderRegistered;
FMassDebugger::FOnDebugEvent FMassDebugger::OnDebugEvent;
TArray<FMassDebugger::FEnvironment> FMassDebugger::ActiveEnvironments;
UE::FTransactionallySafeMutex FMassDebugger::EntityManagerRegistrationLock;
TMap<FName, const UScriptStruct*> FMassDebugger::FragmentsByName;

// ===== Processor / Query 内省（friend 直接访问私有字段）==============================

/** 直接读 Processor::OwnedQueries（private）——这是 friend 关系存在的核心理由。 */
TConstArrayView<FMassEntityQuery*> FMassDebugger::GetProcessorQueries(const UMassProcessor& Processor)
{
	return Processor.OwnedQueries;
}

/** "上一秒新鲜版"的 query：返回前先调每个 query 的 CacheArchetypes 强制刷新缓存。
 *  代价是 O(query 数 * archetype 数) 的匹配重算——只在 Debugger UI 主动拉取时才用。 */
TConstArrayView<FMassEntityQuery*> FMassDebugger::GetUpToDateProcessorQueries(const FMassEntityManager& EntityManager, UMassProcessor& Processor)
{
	for (FMassEntityQuery* Query : Processor.OwnedQueries)
	{
		if (Query)
		{
			Query->CacheArchetypes();
		}
	}

	return Processor.OwnedQueries;
}

/** 把 Query 的私有需求字段一次性打包成只读视图（零拷贝）。 */
UE::Mass::Debug::FQueryRequirementsView FMassDebugger::GetQueryRequirements(const FMassEntityQuery& Query)
{
	UE::Mass::Debug::FQueryRequirementsView View = { Query.FragmentRequirements, Query.ChunkFragmentRequirements, Query.ConstSharedFragmentRequirements, Query.SharedFragmentRequirements
		, Query.RequiredAllTags, Query.RequiredAnyTags, Query.RequiredNoneTags, Query.RequiredOptionalTags
		, Query.RequiredConstSubsystems, Query.RequiredMutableSubsystems };

	return View;
}

void FMassDebugger::GetQueryExecutionRequirements(const FMassEntityQuery& Query, FMassExecutionRequirements& OutExecutionRequirements)
{
	Query.ExportRequirements(OutExecutionRequirements);
}

/** 列举能匹配 Query 的所有实体——通过 EntityManager.GetMatchingArchetypes 拿到 archetype 列表，
 *  再把每个 archetype 内的实体收集起来。注意快照时刻的语义。 */
TArray<FMassEntityHandle> FMassDebugger::GetEntitiesMatchingQuery(const FMassEntityManager& EntityManager, const FMassEntityQuery& Query)
{
	TArray<FMassEntityHandle> Entities;
	TArray<FMassArchetypeHandle> Archetypes;
	EntityManager.GetMatchingArchetypes(Query, Archetypes, 0);
	for (FMassArchetypeHandle& ArchHandle : Archetypes)
	{
		Entities.Append(GetEntitiesOfArchetype(ArchHandle));
	}
	return Entities;
}

// ===== Archetype 内省 =================================================================

/** 遍历 EntityManager 的 FragmentHashToArchetypeMap（同 fragment 集合可能产生多个 archetype），
 *  对每个 archetype 调用回调。是大多数 archetype 全量遍历操作的基础。 */
void FMassDebugger::ForEachArchetype(const FMassEntityManager& EntityManager, const UE::Mass::Debug::FArchetypeFunction& Function)
{
	for (auto& KVP : EntityManager.FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& Archetype : KVP.Value)
		{
			Function(FMassArchetypeHelper::ArchetypeHandleFromData(Archetype));
		}
	}
}

TArray<FMassArchetypeHandle> FMassDebugger::GetAllArchetypes(const FMassEntityManager& EntityManager)
{
	TArray<FMassArchetypeHandle> Archetypes;

	for (auto& KVP : EntityManager.FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& Archetype : KVP.Value)
		{
			Archetypes.Add(FMassArchetypeHelper::ArchetypeHandleFromData(Archetype));
		}
	}

	return Archetypes;
}

const FMassArchetypeCompositionDescriptor& FMassDebugger::GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.CompositionDescriptor;
}

/** 把 archetype 数据指针的整数地址作为 Trace ID。
 *  注意：archetype 一旦创建就不会重新分配位置（SharedPtr 持有），所以这是稳定 ID。 */
uint64 FMassDebugger::GetArchetypeTraceID(const FMassArchetypeData& ArchetypeData)
{
	return reinterpret_cast<uint64>(&ArchetypeData);
}

uint64 FMassDebugger::GetArchetypeTraceID(const FMassArchetypeHandle& ArchetypeHandle)
{ 
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return GetArchetypeTraceID(ArchetypeData);
}

TConstArrayView<FMassEntityHandle> FMassDebugger::GetEntitiesViewOfArchetype(const FMassArchetypeData& ArchetypeData, const FMassArchetypeChunk& Chunk)
{
	FMassArchetypeChunk& MutableChunk = const_cast<FMassArchetypeChunk&>(Chunk);
	TConstArrayView<FMassEntityHandle> View(&MutableChunk.GetEntityArrayElementRef(ArchetypeData.EntityListOffsetWithinChunk, 0), Chunk.GetNumInstances());
	return View;
}

const FMassArchetypeData* FMassDebugger::GetArchetypeData(const FMassArchetypeHandle& ArchetypeHandle)
{
	FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
	return ArchetypeData;
}

void FMassDebugger::EnumerateChunks(const FMassArchetypeData& Archetype, TFunctionRef<void(const FMassArchetypeChunk&)> Fn)
{
	for (const FMassArchetypeChunk& Chunk : Archetype.Chunks)
	{
		Fn(Chunk);
	}
}

/**
 * 【GetArchetypeEntityStats —— archetype 统计采集（核心算法）】
 *
 * 把 FArchetypeStats 各字段一次性填好。其中 WastedEntityMemory 的算法分两步：
 *   1) DebugGetEntityMemoryNumbers 同时返回"已分配 chunk 总内存"和"被实体真正占用的内存"
 *   2) 二者之差就是 chunk 尾部空槽的浪费量
 * 这指标是判断 archetype 利用率（chunk 是否过度碎片化）的关键。
 */
void FMassDebugger::GetArchetypeEntityStats(const FMassArchetypeHandle& ArchetypeHandle, UE::Mass::Debug::FArchetypeStats& OutStats)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	OutStats.EntitiesCount = ArchetypeData.GetNumEntities();
	OutStats.EntitiesCountPerChunk = ArchetypeData.GetNumEntitiesPerChunk();
	OutStats.ChunksCount = ArchetypeData.GetChunkCount();
	OutStats.AllocatedSize = ArchetypeData.GetAllocatedSize();
	OutStats.BytesPerEntity = ArchetypeData.GetBytesPerEntity();

	SIZE_T ActiveChunksMemorySize = 0;
	SIZE_T ActiveEntitiesMemorySize = 0;
	ArchetypeData.DebugGetEntityMemoryNumbers(ActiveChunksMemorySize, ActiveEntitiesMemorySize);
	OutStats.WastedEntityMemory = ActiveChunksMemorySize - ActiveEntitiesMemorySize;
}

const TConstArrayView<FName> FMassDebugger::GetArchetypeDebugNames(const FMassArchetypeHandle& ArchetypeHandle)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.GetDebugNames();
}

/** 收集某 archetype 内的所有实体（按 chunk 拼接）。供 Mass Debugger UI"显示该 archetype 内的实体列表"用。 */
TArray<FMassEntityHandle> FMassDebugger::GetEntitiesOfArchetype(const FMassArchetypeHandle& ArchetypeHandle)
{
	TArray<FMassEntityHandle> EntitiesOfArchetype;
	FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	EntitiesOfArchetype.Reserve(ArchetypeData.GetNumEntities());
	for (FMassArchetypeChunk& Chunk : ArchetypeData.Chunks)
	{
		TArrayView<FMassEntityHandle> EntityListView = TArrayView<FMassEntityHandle>(&Chunk.GetEntityArrayElementRef(ArchetypeData.EntityListOffsetWithinChunk, 0), Chunk.GetNumInstances());
		EntitiesOfArchetype.Append(EntityListView);
	}
	return EntitiesOfArchetype;
}

// ===== Composite Processor 依赖图内省 ===============================================

/** 拿到 composite processor 内已扁平化的依赖图（拓扑排序后的节点列表）。 */
TConstArrayView<UMassCompositeProcessor::FDependencyNode> FMassDebugger::GetProcessingGraph(const UMassCompositeProcessor& GraphOwner)
{
	return GraphOwner.FlatProcessingGraph;
}

/** 拿到 composite processor 实际持有的子 processor 数组。 */
TConstArrayView<TObjectPtr<UMassProcessor>> FMassDebugger::GetHostedProcessors(const UMassCompositeProcessor& GraphOwner)
{
	return GraphOwner.ChildPipeline.GetProcessors();
}

// ===== Requirement 字符串化 ==========================================================

FString FMassDebugger::GetRequirementsDescription(const FMassFragmentRequirements& Requirements)
{
	TStringBuilder<256> StringBuilder;
	StringBuilder.Append(TEXT("<"));

	bool bNeedsComma = false;
	for (const FMassFragmentRequirementDescription& Requirement : Requirements.FragmentRequirements)
	{
		if (bNeedsComma)
		{
			StringBuilder.Append(TEXT(","));
		}
		StringBuilder.Append(*FMassDebugger::GetSingleRequirementDescription(Requirement));
		bNeedsComma = true;
	}

	StringBuilder.Append(TEXT(">"));
	return StringBuilder.ToString();
}

FString FMassDebugger::GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle)
{
	if (ArchetypeHandle.IsValid() == false)
	{
		return TEXT("Invalid");
	}

	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return FMassDebugger::GetArchetypeRequirementCompatibilityDescription(Requirements, ArchetypeData.GetCompositionDescriptor());
}
	
/**
 * 【GetArchetypeRequirementCompatibilityDescription —— 失败原因诊断】
 *
 * 这是"为什么我的实体没被处理"问题的核心 API。
 * 把 archetype 不满足 requirement 的具体原因生成多行字符串：
 *   - 若有 negative requirement (None) 不满足：列出意外存在的 fragment/tag/...
 *   - 若有 positive requirement (All/Any) 不满足：列出缺失的 fragment/tag/...
 *   - 若只有 optional 但都没匹配：报告 none of optionals satisfied
 * 全部条件均满足时返回 "Match"。
 *
 * 算法分两段：
 *   段1 (negative)：始终检查（只要 HasNegativeRequirements）；
 *   段2 (positive)：若 HasPositiveRequirements 则只检查硬性需求，
 *        否则若 HasOptionalRequirements 则检查 optional——三段互斥。
 */
FString FMassDebugger::GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeCompositionDescriptor& ArchetypeComposition)
{
	FStringOutputDevice OutDescription;

	if (Requirements.HasNegativeRequirements())
	{
		if (ArchetypeComposition.GetFragments().HasNone(Requirements.RequiredNoneFragments) == false)
		{
			// has some of the fragments required absent
			OutDescription += TEXT("\nHas fragments required absent: ");
			(Requirements.RequiredNoneFragments & ArchetypeComposition.GetFragments()).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetTags().HasNone(Requirements.RequiredNoneTags) == false)
		{
			// has some of the tags required absent
			OutDescription += TEXT("\nHas tags required absent: ");
			(Requirements.RequiredNoneTags & ArchetypeComposition.GetTags()).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetChunkFragments().HasNone(Requirements.RequiredNoneChunkFragments) == false)
		{
			// has some of the chunk fragments required absent
			OutDescription += TEXT("\nHas chunk fragments required absent: ");
			(Requirements.RequiredNoneChunkFragments & ArchetypeComposition.GetChunkFragments()).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetSharedFragments().HasNone(Requirements.RequiredNoneSharedFragments) == false)
		{
			// has some of the chunk fragments required absent
			OutDescription += TEXT("\nHas shared fragments required absent: ");
			(Requirements.RequiredNoneSharedFragments & ArchetypeComposition.GetSharedFragments()).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetConstSharedFragments().HasNone(Requirements.RequiredNoneConstSharedFragments) == false)
		{
			// has some of the chunk fragments required absent
			OutDescription += TEXT("\nHas shared fragments required absent: ");
			(Requirements.RequiredNoneConstSharedFragments & ArchetypeComposition.GetConstSharedFragments()).DebugGetStringDesc(OutDescription);
		}
	}

	// if we have regular (i.e. non-optional) positive requirements then these are the determining factor, we don't check optionals
	if (Requirements.HasPositiveRequirements())
	{
		if (ArchetypeComposition.GetFragments().HasAll(Requirements.RequiredAllFragments) == false)
		{
			// missing one of the strictly required fragments
			OutDescription += TEXT("\nMissing required fragments: ");
			(Requirements.RequiredAllFragments - ArchetypeComposition.GetFragments()).DebugGetStringDesc(OutDescription);
		}

		if (Requirements.RequiredAnyFragments.IsEmpty() == false && ArchetypeComposition.GetFragments().HasAny(Requirements.RequiredAnyFragments) == false)
		{
			// missing all of the "any" fragments
			OutDescription += TEXT("\nMissing all \'any\' fragments: ");
			Requirements.RequiredAnyFragments.DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetTags().HasAll(Requirements.RequiredAllTags) == false)
		{
			// missing one of the strictly required tags
			OutDescription += TEXT("\nMissing required tags: ");
			(Requirements.RequiredAllTags - ArchetypeComposition.GetTags()).DebugGetStringDesc(OutDescription);
		}

		if (Requirements.RequiredAnyTags.IsEmpty() == false && ArchetypeComposition.GetTags().HasAny(Requirements.RequiredAnyTags) == false)
		{
			// missing all of the "any" tags
			OutDescription += TEXT("\nMissing all \'any\' tags: ");
			Requirements.RequiredAnyTags.DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetChunkFragments().HasAll(Requirements.RequiredAllChunkFragments) == false)
		{
			// missing one of the strictly required chunk fragments
			OutDescription += TEXT("\nMissing required chunk fragments: ");
			(Requirements.RequiredAllChunkFragments - ArchetypeComposition.GetChunkFragments()).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetSharedFragments().HasAll(Requirements.RequiredAllSharedFragments) == false)
		{
			// missing one of the strictly required Shared fragments
			OutDescription += TEXT("\nMissing required Shared fragments: ");
			(Requirements.RequiredAllSharedFragments - ArchetypeComposition.GetSharedFragments()).DebugGetStringDesc(OutDescription);
		}

		if (ArchetypeComposition.GetConstSharedFragments().HasAll(Requirements.RequiredAllConstSharedFragments) == false)
		{
			// missing one of the strictly required Shared fragments
			OutDescription += TEXT("\nMissing required Shared fragments: ");
			(Requirements.RequiredAllConstSharedFragments - ArchetypeComposition.GetConstSharedFragments()).DebugGetStringDesc(OutDescription);
		}
	}
	// else we check if there are any optionals and if so test them
	else if (Requirements.HasOptionalRequirements() && (Requirements.DoesMatchAnyOptionals(ArchetypeComposition) == false))
	{
		// we report that none of the optionals has been met
		OutDescription += TEXT("\nNone of the optionals were safisfied while not having other positive hard requirements: ");

		Requirements.RequiredOptionalTags.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalFragments.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalChunkFragments.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalSharedFragments.DebugGetStringDesc(OutDescription);
		Requirements.RequiredOptionalConstSharedFragments.DebugGetStringDesc(OutDescription);
	}

	return OutDescription.Len() > 0 ? static_cast<FString>(OutDescription) : TEXT("Match");
}

/** 单条 requirement 的简短字符串（前缀 +/-/?）。 */
FString FMassDebugger::GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement)
{
	return FString::Printf(TEXT("%s%s[%s]"), Requirement.IsOptional() ? TEXT("?") : (Requirement.Presence == EMassFragmentPresence::None ? TEXT("-") : TEXT("+"))
		, *GetNameSafe(Requirement.StructType), *UE::Mass::Debug::DebugGetFragmentAccessString(Requirement.AccessMode));
}

// ===== 控制台命令实现：archetype 描述、entity 描述输出 ===============================

void FMassDebugger::OutputArchetypeDescription(FOutputDevice& Ar, const FMassArchetypeHandle& ArchetypeHandle)
{
	Ar.Logf(TEXT("%s"), ArchetypeHandle.IsValid() ? *FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle).DebugGetDescription() : TEXT("INVALID"));
}

void FMassDebugger::OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const int32 EntityIndex, const TCHAR* InPrefix)
{
	if (EntityIndex >= EntityManager.DebugGetEntityStorageInterface().Num())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for out of range index in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
		return;
	}
	
	if (!EntityManager.DebugGetEntityStorageInterface().IsValid(EntityIndex))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
	}
	
	FMassEntityHandle Entity;
	Entity.Index = EntityIndex;
	Entity.SerialNumber = EntityManager.DebugGetEntityStorageInterface().GetSerialNumber(EntityIndex);
	OutputEntityDescription(Ar, EntityManager, Entity, InPrefix);
}

void FMassDebugger::OutputEntityDescription(FOutputDevice& Ar, const FMassEntityManager& EntityManager, const FMassEntityHandle Entity, const TCHAR* InPrefix)
{
	if (!EntityManager.IsEntityActive(Entity))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("Listing fragments values for Entity[%s] in EntityManager owned by %s"), *Entity.DebugGetDescription(), *GetPathNameSafe(EntityManager.GetOwner()));

	FMassArchetypeData* Archetype = EntityManager.DebugGetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	if (Archetype == nullptr)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in EntityManager owned by %s"), *GetPathNameSafe(EntityManager.GetOwner()));
	}
	else
	{
		Archetype->DebugPrintEntity(Entity, Ar, InPrefix);
	}
}

// ===== 实体选择/高亮 =================================================================

/**
 * 【SelectEntity】
 * 1) 校验 EntityHandle 有效；
 * 2) 设置全局 DebugEntityRange = [Index, Index]（影响 IsDebuggingEntity 等查询）；
 * 3) 把 Environment 的 SelectedEntity 设为该实体；
 * 4) 广播 OnEntitySelectedDelegate（Debugger UI、GameplayDebugger 等订阅者更新视图）。
 *
 * 注意：本函数对无效实体**静默返回**，不会清空选中状态。这与
 * `mass.debug.DebugEntity` 命令的行为一致："找不到实体则保留之前的选择"。
 */
void FMassDebugger::SelectEntity(const FMassEntityManager& EntityManager, const FMassEntityHandle EntityHandle)
{
	if (EntityManager.IsEntityValid(EntityHandle))
	{
		UE::Mass::Debug::SetDebugEntityRange(EntityHandle.Index, EntityHandle.Index);

		GetActiveEnvironmentChecked(EntityManager).SelectedEntity = EntityHandle;

		OnEntitySelectedDelegate.Broadcast(EntityManager, EntityHandle);
	}
}

FMassEntityHandle FMassDebugger::GetSelectedEntity(const FMassEntityManager& EntityManager)
{
	return GetActiveEnvironmentChecked(EntityManager).SelectedEntity;
}

/**
 * 【HighlightEntity】纯 UI 高亮——*不验证* EntityHandle 有效性，*不广播*事件，
 * environment 缺失时**静默失败**（与 SelectEntity 用 Checked 版本不同）。
 *
 * 这种宽容设计允许调用方传入无效 handle 来"取消高亮"，且让外部 UI 工具
 * 在 EntityManager 已下线的窗口期内调用不会崩。
 */
void FMassDebugger::HighlightEntity(const FMassEntityManager& EntityManager, const FMassEntityHandle EntityHandle)
{
	if (FEnvironment* ActiveEnvironment = GetActiveEnvironment(EntityManager))
	{
		ActiveEnvironment->HighlightedEntity = EntityHandle;
	}
}

FMassEntityHandle FMassDebugger::GetHighlightedEntity(const FMassEntityManager& EntityManager)
{
	if (FEnvironment* ActiveEnvironment = GetActiveEnvironment(EntityManager))
	{
		return ActiveEnvironment->HighlightedEntity;
	}
	return FMassEntityHandle();
}

// ===== Environment 注册/反注册 =======================================================

bool FMassDebugger::IsEntityManagerInitialized(const FMassEntityManager& EntityManager)
{
	return EntityManager.InitializationState == FMassEntityManager::EInitializationState::Initialized;
}

/**
 * 【RegisterEntityManager】
 * 由 FMassEntityManager 在初始化末尾调用——构造一个新 FEnvironment 并 emplace 进
 * ActiveEnvironments，然后广播 OnEntityManagerInitialized 让 Debugger UI 增加该 manager。
 *
 * 锁的范围：仅保护 ActiveEnvironments.Emplace；广播在锁外做以避免长时间持锁。
 */
int32 FMassDebugger::RegisterEntityManager(FMassEntityManager& EntityManager)
{
	int32 NewEnvironmentIndex = INDEX_NONE;
	{
		UE::TScopeLock ScopeLock(EntityManagerRegistrationLock);
		NewEnvironmentIndex = ActiveEnvironments.Emplace(EntityManager);
	}
	OnEntityManagerInitialized.Broadcast(EntityManager);
	return NewEnvironmentIndex;
}

/**
 * 【UnregisterEntityManager】
 * 双分支处理：
 *   - 若 EntityManager 仍然 share-able（DoesSharedInstanceExist=true）：用 weak ptr 比对找到精确条目移除；
 *   - 否则：weak ptr 已经失效，无法精确比对——只能"清扫所有失效条目"。
 *
 * 这种设计应对"EntityManager 被销毁但本函数还没来得及调用"的边界情形。
 */
void FMassDebugger::UnregisterEntityManager(FMassEntityManager& EntityManager)
{
	if (EntityManager.DoesSharedInstanceExist())
	{
		UE::TScopeLock ScopeLock(EntityManagerRegistrationLock);
		const int32 Index = ActiveEnvironments.IndexOfByPredicate([WeakManager = EntityManager.AsWeak()](const FEnvironment& Element) 
		{
			return Element.EntityManager == WeakManager;
		});
		if (Index != INDEX_NONE)
		{
			ActiveEnvironments.RemoveAt(Index, EAllowShrinking::No);
		}
	}
	else
	{
		UE::TScopeLock ScopeLock(EntityManagerRegistrationLock);
		ActiveEnvironments.RemoveAll([](const FEnvironment& Item)
			{
				return Item.IsValid() == false;
			});
	}
	OnEntityManagerDeinitialized.Broadcast(EntityManager);
}

/**
 * 【RegisterProcessorDataProvider】
 * 关联一个 processor 数据提供器到指定 EntityManager 的 environment。
 * 若 environment 还不存在，会先调 RegisterEntityManager 创建（lazy initialize 语义）。
 * 这种"用名字键覆盖式注册"允许同名 provider 反复注册，每次替换上一次。
 */
void FMassDebugger::RegisterProcessorDataProvider(FName ProviderName, const TSharedRef<FMassEntityManager>& EntityManager, const UE::Mass::Debug::FProcessorProviderFunction& ProviderFunction)
{
	int32 Index;
	{
		UE::TScopeLock ScopeLock(EntityManagerRegistrationLock);
		Index = ActiveEnvironments.IndexOfByPredicate([WeakEntityManager = EntityManager->AsWeak()](const FEnvironment& Element) 
		{
			return Element.EntityManager == WeakEntityManager;
		});

		if (Index == INDEX_NONE)
		{
			Index = RegisterEntityManager(*EntityManager);
		}
	
		ActiveEnvironments[Index].ProcessorProviders.FindOrAdd(ProviderName, ProviderFunction);
	}
	OnProcessorProviderRegistered.Broadcast(ActiveEnvironments[Index]);
}

FMassDebugger::FEnvironment* FMassDebugger::FindEnvironmentForEntityManager(const FMassEntityManager& EntityManager)
{
	for (FMassDebugger::FEnvironment& Environment : ActiveEnvironments)
	{
		if (Environment.EntityManager.HasSameObject(&EntityManager))
		{
			return &Environment;
		}
	}
	return nullptr;
}

bool FMassDebugger::DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle, const FMassFragmentRequirements& Requirements, FOutputDevice& OutputDevice)
{
	if (const FMassArchetypeData* Archetype = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle))
	{
		return FMassArchetypeHelper::DoesArchetypeMatchRequirements(*Archetype, Requirements, /*bBailOutOnFirstFail=*/false, &OutputDevice);
	}
	return false;
}

// ===== 断点相关 API ===================================================================

/**
 * 【ShouldProcessorBreak —— processor 执行断点的热路径查询】
 *
 * 三段式短路：
 *   1) 全局快路径：FBreakpoint::HasBreakpoint() 检查整个进程是否有任何断点
 *   2) Environment 级：ActiveEnvironment.bHasBreakpoint
 *   3) Processor 级：ProcessorsWithBreakpoints.Contains(Processor)
 * 三道门都通过后才线性扫描 Breakpoints 数组找具体匹配。
 *
 * 注意：返回的是首个**已启用**且通过 filter 的断点 handle；但 HitCount 对所有匹配
 * 的断点（含禁用的）都会递增。
 *
 * 【疑似 bug 2】函数中 FoundHandle 的语义：在 for 循环中可能被多次赋值，最终只
 * 返回最后一个匹配的 handle。如果调用方期望"任意一个 handle"则没问题，但若期望
 * "首个匹配"则与代码不符——目前看代码用法 (MASS_BREAKPOINT 仅看 IsValid()) 也只
 * 关心 truthiness，所以无实际影响。
 */
UE::Mass::Debug::FBreakpointHandle FMassDebugger::ShouldProcessorBreak(const FMassEntityManager& EntityManager, const UMassProcessor* Processor, FMassEntityHandle Entity)
{
	if (LIKELY(!UE::Mass::Debug::FBreakpoint::HasBreakpoint()))
	{
		return UE::Mass::Debug::FBreakpointHandle::Invalid();
	}

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	if (LIKELY(!ActiveEnvironment.bHasBreakpoint))
	{
		return UE::Mass::Debug::FBreakpointHandle::Invalid();
	}

	const bool ProcessorHasBreakpoint = ActiveEnvironment.ProcessorsWithBreakpoints.Contains(Processor);
	
	if (ProcessorHasBreakpoint)
	{
		UE::Mass::Debug::FBreakpointHandle FoundHandle = 0;
		for (UE::Mass::Debug::FBreakpoint& BreakPoint : ActiveEnvironment.Breakpoints)
		{
			if (BreakPoint.TriggerType == UE::Mass::Debug::FBreakpoint::ETriggerType::ProcessorExecute)
			{
				TObjectKey<const UMassProcessor> Proc = BreakPoint.Trigger.Get<TObjectKey<const UMassProcessor>>();
				if (Proc == Processor && BreakPoint.ApplyEntityFilter(EntityManager, Entity))
				{
					++BreakPoint.HitCount;
					if (BreakPoint.bEnabled)
					{
						UE::Mass::Debug::FBreakpoint::LastBreakpointHandle = BreakPoint.Handle;
						FoundHandle = BreakPoint.Handle;
					}
				}
			}
		}
		return FoundHandle;
	}
	return UE::Mass::Debug::FBreakpointHandle::Invalid();
}

bool FMassDebugger::HasAnyProcessorBreakpoints(const FMassEntityManager& EntityManager, const UMassProcessor* Processor)
{
	if (LIKELY(!UE::Mass::Debug::FBreakpoint::HasBreakpoint()))
	{
		return false;
	}

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	if (LIKELY(!ActiveEnvironment.bHasBreakpoint))
	{
		return false;
	}

	return ActiveEnvironment.ProcessorsWithBreakpoints.Contains(Processor);
}

UE::Mass::Debug::FBreakpointHandle FMassDebugger::ShouldBreakOnFragmentWrite(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity)
{
	if (LIKELY(!UE::Mass::Debug::FBreakpoint::HasBreakpoint()))
	{
		return UE::Mass::Debug::FBreakpointHandle::Invalid();
	}

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	if (LIKELY(!ActiveEnvironment.bHasBreakpoint))
	{
		return UE::Mass::Debug::FBreakpointHandle::Invalid();
	}

	const bool bFragmentHasBreakpoint = ActiveEnvironment.FragmentsWithBreakpoints.Contains(FragmentType);

	if (bFragmentHasBreakpoint)
	{
		UE::Mass::Debug::FBreakpointHandle FoundHandle;
		for (UE::Mass::Debug::FBreakpoint& BreakPoint : ActiveEnvironment.Breakpoints)
		{
			if (BreakPoint.TriggerType == UE::Mass::Debug::FBreakpoint::ETriggerType::FragmentWrite)
			{
				TObjectKey<const UScriptStruct> BreakFragment = BreakPoint.Trigger.Get<TObjectKey<const UScriptStruct>>();
				if (BreakFragment == FragmentType && BreakPoint.ApplyEntityFilter(EntityManager, Entity))
				{
					++BreakPoint.HitCount;
					UE::Mass::Debug::FBreakpoint::LastBreakpointHandle = BreakPoint.Handle;
					if (BreakPoint.bEnabled)
					{
						FoundHandle = BreakPoint.Handle;
					}
				}
			}
		}
		return FoundHandle;
	}
	return UE::Mass::Debug::FBreakpointHandle::Invalid();
}

UE::Mass::Debug::FBreakpoint* FMassDebugger::FindBreakpoint(const FMassEntityManager& EntityManager, UE::Mass::Debug::FBreakpointHandle Handle)
{
	FEnvironment* ActiveEnvironment = GetActiveEnvironment(EntityManager);
	if (ActiveEnvironment)
	{
		return ActiveEnvironment->FindBreakpointByHandle(Handle);
	}
	return nullptr;
}

TArray<UE::Mass::Debug::FBreakpoint>& FMassDebugger::GetBreakpoints(const FMassEntityManager& EntityManager)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);
	return ActiveEnvironment.Breakpoints;
}

bool FMassDebugger::HasAnyFragmentWriteBreakpoints(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType)
{
	if (LIKELY(!UE::Mass::Debug::FBreakpoint::HasBreakpoint()))
	{
		return false;
	}

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	if (LIKELY(!ActiveEnvironment.bHasBreakpoint))
	{
		return false;
	}

	if (FragmentType == nullptr)
	{
		return ActiveEnvironment.FragmentsWithBreakpoints.Num() > 0;
	}

	return ActiveEnvironment.FragmentsWithBreakpoints.Contains(FragmentType);
}

UE::Mass::Debug::FBreakpoint& FMassDebugger::CreateBreakpoint(const FMassEntityManager& EntityManager)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	return ActiveEnvironment.Breakpoints.Emplace_GetRef();
}

/**
 * 【SetProcessorBreakpoint —— 注册 processor 执行断点】
 *
 * 步骤：
 *   1) 提醒用户"如果没 IDE attach 这是无效的"（FNoDebuggerNotification）
 *   2) 设全局/env 级 bHasBreakpoint=true
 *   3) 创建一个 FBreakpoint，TriggerType=ProcessorExecute、Trigger=Processor
 *   4) 若指定了 Entity，FilterType=SpecificEntity；否则 None（任意实体）
 *   5) 把 Processor 加入加速集合 ProcessorsWithBreakpoints
 *   6) 广播 OnBreakpointsChanged 让 Debugger UI 刷新
 */
void FMassDebugger::SetProcessorBreakpoint(const FMassEntityManager& EntityManager, TNotNull<const UMassProcessor*> Processor, FMassEntityHandle Entity)
{
	UE::Mass::Debug::FNoDebuggerNotification::ConditionallyNotifyUser();

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	ActiveEnvironment.bHasBreakpoint = true;
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = true;

	UE::Mass::Debug::FBreakpoint& NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();
	NewBreakpoint.bEnabled = true;
	NewBreakpoint.TriggerType = UE::Mass::Debug::FBreakpoint::ETriggerType::ProcessorExecute;
	NewBreakpoint.Trigger.Set<TObjectKey<const UMassProcessor>>(Processor);
	if (Entity.IsSet())
	{
		NewBreakpoint.FilterType = UE::Mass::Debug::FBreakpoint::EFilterType::SpecificEntity;
		NewBreakpoint.Filter.Set<FMassEntityHandle>(Entity);
	}

	ActiveEnvironment.ProcessorsWithBreakpoints.Add(Processor);
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::SetFragmentWriteBreakpoint(const FMassEntityManager& EntityManager, TNotNull<const UScriptStruct*> FragmentType, FMassEntityHandle Entity)
{
	UE::Mass::Debug::FNoDebuggerNotification::ConditionallyNotifyUser();

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	ActiveEnvironment.bHasBreakpoint = true;
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = true;

	UE::Mass::Debug::FBreakpoint& NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();
	NewBreakpoint.bEnabled = true;
	NewBreakpoint.TriggerType = UE::Mass::Debug::FBreakpoint::ETriggerType::FragmentWrite;
	NewBreakpoint.Trigger.Set<TObjectKey<const UScriptStruct>>(FragmentType);
	if (Entity.IsSet())
	{
		NewBreakpoint.FilterType = UE::Mass::Debug::FBreakpoint::EFilterType::SpecificEntity;
		NewBreakpoint.Filter.Set<FMassEntityHandle>(Entity);
	}

	ActiveEnvironment.FragmentsWithBreakpoints.Add(FragmentType);
	OnBreakpointsChangedDelegate.Broadcast();
}

/**
 * 【SetFragmentWriteBreakForSelectedEntity】
 * 与 SetFragmentWriteBreakpoint 不同的是 FilterType=SelectedEntity——
 * 触发时实时查询当前 selected entity，焦点切换则断点目标随之改变。
 *
 * 【疑似 bug 3】这里 `UE::Mass::Debug::FBreakpoint NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();`
 * 缺少了 `&` 引用——它把 emplace 出的元素 *复制* 到了局部变量，对该局部变量的修改
 * 不会影响 vector 中真正的元素！结果应该是：触发器/filter 全部留在 default 状态，
 * fragment 写入断点根本不会工作。其他类似函数（SetProcessorBreakpoint、
 * SetFragmentWriteBreakpoint）都是 `auto& NewBreakpoint`，这里看起来是漏写 &。
 * 注意：`FragmentsWithBreakpoints.Add(FragmentType)` 和 `bHasBreakpoint=true` 仍生效，
 * 所以 ShouldBreakOnFragmentWrite 进入慢路径后会找不到匹配的 trigger，最终返回 Invalid。
 */
void FMassDebugger::SetFragmentWriteBreakForSelectedEntity(const FMassEntityManager& EntityManager, TNotNull<const UScriptStruct*> FragmentType)
{
	UE::Mass::Debug::FNoDebuggerNotification::ConditionallyNotifyUser();

	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	ActiveEnvironment.bHasBreakpoint = true;
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = true;

	UE::Mass::Debug::FBreakpoint NewBreakpoint = ActiveEnvironment.Breakpoints.Emplace_GetRef();
	NewBreakpoint.bEnabled = true;
	NewBreakpoint.TriggerType = UE::Mass::Debug::FBreakpoint::ETriggerType::FragmentWrite;
	NewBreakpoint.Trigger.Set<TObjectKey<const UScriptStruct>>(FragmentType);
	NewBreakpoint.FilterType = UE::Mass::Debug::FBreakpoint::EFilterType::SelectedEntity;

	ActiveEnvironment.FragmentsWithBreakpoints.Add(FragmentType);
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::SetBreakpointEnabled(UE::Mass::Debug::FBreakpointHandle Handle, bool bEnable)
{
	for (FEnvironment& Environment : ActiveEnvironments)
	{
		for (UE::Mass::Debug::FBreakpoint& Breakpoint : Environment.Breakpoints)
		{
			if (Breakpoint.Handle == Handle)
			{
				Breakpoint.bEnabled = bEnable;
				OnBreakpointsChangedDelegate.Broadcast();
				return;
			}
		}
	}
}

void FMassDebugger::ClearProcessorBreakpoint(const FMassEntityManager& EntityManager, const UMassProcessor* Processor, FMassEntityHandle Entity)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	if (ActiveEnvironment.ProcessorsWithBreakpoints.Contains(Processor))
	{
		for (int EnvironmentIndex = 0; EnvironmentIndex < ActiveEnvironment.Breakpoints.Num(); EnvironmentIndex++)
		{
			UE::Mass::Debug::FBreakpoint& Breakpoint = ActiveEnvironment.Breakpoints[EnvironmentIndex];

			if (Breakpoint.TriggerType == UE::Mass::Debug::FBreakpoint::ETriggerType::ProcessorExecute
				&& Breakpoint.Trigger.Get<TObjectKey<const UMassProcessor>>() == Processor
				&& Breakpoint.ApplyEntityFilter(EntityManager, Entity))
			{
				ActiveEnvironment.Breakpoints.RemoveAt(EnvironmentIndex);
			}
		}
	}
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::ClearAllProcessorBreakpoints(const FMassEntityManager& EntityManager, const UMassProcessor* Processor)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);
	for (int EnvironmentIndex = 0; EnvironmentIndex < ActiveEnvironment.Breakpoints.Num(); EnvironmentIndex++)
	{
		UE::Mass::Debug::FBreakpoint& Breakpoint = ActiveEnvironment.Breakpoints[EnvironmentIndex];

		if (Breakpoint.TriggerType == UE::Mass::Debug::FBreakpoint::ETriggerType::ProcessorExecute
			&& Breakpoint.Trigger.Get<TObjectKey<const UMassProcessor>>() == Processor)
		{
			ActiveEnvironment.Breakpoints.RemoveAt(EnvironmentIndex);
		}
	}

	ActiveEnvironment.ProcessorsWithBreakpoints.Remove(Processor);
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::ClearFragmentWriteBreak(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	if (ActiveEnvironment.FragmentsWithBreakpoints.Contains(FragmentType))
	{
		for (int EnvironmentIndex = 0; EnvironmentIndex < ActiveEnvironment.Breakpoints.Num(); EnvironmentIndex++)
		{
			UE::Mass::Debug::FBreakpoint& Breakpoint = ActiveEnvironment.Breakpoints[EnvironmentIndex];

			if (Breakpoint.TriggerType == UE::Mass::Debug::FBreakpoint::ETriggerType::FragmentWrite
				&& Breakpoint.Trigger.Get<TObjectKey<const UScriptStruct>>() == FragmentType
				&& Breakpoint.ApplyEntityFilter(EntityManager, Entity))
			{
				ActiveEnvironment.Breakpoints.RemoveAt(EnvironmentIndex);
			}
		}
	}
	OnBreakpointsChangedDelegate.Broadcast();
}

/**
 * 【ClearAllFragmentWriteBreak】
 *
 * 【疑似 bug 4】`Breakpoint.TriggerType == FBreakpoint::ETriggerType::ProcessorExecute`
 * 看起来应当是 `FragmentWrite` 才对——本函数语义是"清除某 fragment 类型的所有写入断点"，
 * 但代码却在比较 ProcessorExecute trigger，与函数名字面意义矛盾。
 * 这可能导致：
 *   a) 想清除的 fragment 写入断点没被清除；
 *   b) 把 ProcessorExecute 类型的（碰巧 trigger 是 fragment 类型？这种组合本不存在）误删除。
 * 实际由于不同 trigger 类型的 Trigger.Get<TObjectKey<...>>() 类型不同，
 * 这里 Get<TObjectKey<UScriptStruct>>() 在 ProcessorExecute trigger 上会断言失败/未定义。
 * 强烈建议核对原意修复。
 */
void FMassDebugger::ClearAllFragmentWriteBreak(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);
	for (int EnvironmentIndex = 0; EnvironmentIndex < ActiveEnvironment.Breakpoints.Num(); EnvironmentIndex++)
	{
		UE::Mass::Debug::FBreakpoint& Breakpoint = ActiveEnvironment.Breakpoints[EnvironmentIndex];

		if (Breakpoint.TriggerType == UE::Mass::Debug::FBreakpoint::ETriggerType::ProcessorExecute
			&& Breakpoint.Trigger.Get<TObjectKey<const UScriptStruct>>() == FragmentType)
		{
			ActiveEnvironment.Breakpoints.RemoveAt(EnvironmentIndex);
		}
	}

	ActiveEnvironment.FragmentsWithBreakpoints.Remove(FragmentType);
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::ClearAllEntityBreakpoints(const FMassEntityManager& EntityManager, FMassEntityHandle Entity)
{
	FEnvironment& ActiveEnvironment = GetActiveEnvironmentChecked(EntityManager);

	for (int EnvironmentIndex = 0; EnvironmentIndex < ActiveEnvironment.Breakpoints.Num(); EnvironmentIndex++)
	{
		UE::Mass::Debug::FBreakpoint& Breakpoint = ActiveEnvironment.Breakpoints[EnvironmentIndex];

		FMassEntityHandle* BreakHandle = Breakpoint.Filter.TryGet<FMassEntityHandle>();
		if (BreakHandle
			&& *BreakHandle == Entity)
		{
			ActiveEnvironment.Breakpoints.RemoveAt(EnvironmentIndex);
		}
	}

	OnBreakpointsChangedDelegate.Broadcast();
}
 
void FMassDebugger::BreakOnFragmentWriteForSelectedEntity(FName FragmentName)
{
	for (FEnvironment& Environment : ActiveEnvironments)
	{
		SetFragmentWriteBreakForSelectedEntity(*Environment.EntityManager.Pin(), GetFragmentTypeFromName(FragmentName));
	}
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::ClearAllBreakpoints()
{
	for (FEnvironment& Environment : ActiveEnvironments)
	{
		Environment.ClearBreakpoints();
	}
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = false;
	OnBreakpointsChangedDelegate.Broadcast();
}

void FMassDebugger::RemoveBreakpoint(UE::Mass::Debug::FBreakpointHandle Handle)
{
	for (FEnvironment& Environment : ActiveEnvironments)
	{
		for (int EnvironmentIndex = 0; EnvironmentIndex < Environment.Breakpoints.Num(); EnvironmentIndex++)
		{
			if (Environment.Breakpoints[EnvironmentIndex].Handle == Handle)
			{
				Environment.Breakpoints.RemoveAt(EnvironmentIndex);
				RefreshBreakpoints();
				return;
			}
		}
	}
}

/**
 * 【ShutdownDebugger】
 * 模块卸载时调用——区别于 ClearAllBreakpoints：**不广播任何委托**，
 * 因为销毁过程中订阅者可能已经失效。直接清空所有数据结构。
 */
void FMassDebugger::ShutdownDebugger()
{
	// Don't call ClearAllBreakpoints here because we don't want to send the BreakpointsChanged delegate
	for (FEnvironment& Environment : ActiveEnvironments)
	{
		Environment.ClearBreakpoints();
	}
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = false;
	ActiveEnvironments.Empty();
	FragmentsByName.Empty();
}

/**
 * 【GetFragmentTypeFromName —— 按名字反查 fragment UScriptStruct】
 *
 * 算法：先查缓存（FragmentsByName），未命中则全量扫描所有 environment 的所有 archetype，
 * 把每个 archetype 的 composition 中所有 fragment / chunk fragment / shared / const shared
 * 类型加入缓存。然后再查一次缓存。
 *
 * 注意：未涉及 tag 类型——故"按名字"无法找到 tag。这与 FindElementTypeByName 行为略有差异。
 */
const UScriptStruct* FMassDebugger::GetFragmentTypeFromName(FName FragmentName)
{
	const UScriptStruct** FoundType = FragmentsByName.Find(FragmentName);
	if (FoundType)
	{
		return *FoundType;
	}

	for (FEnvironment& Environment : ActiveEnvironments)
	{
		TArray<FMassArchetypeHandle> ArchetypeHandles = GetAllArchetypes(*Environment.EntityManager.Pin());
		for (FMassArchetypeHandle& ArchetypeHandle : ArchetypeHandles)
		{
			const FMassArchetypeCompositionDescriptor& Composition = GetArchetypeComposition(ArchetypeHandle);

			FMassFragmentBitSet::FIndexIterator It = Composition.GetFragments().GetIndexIterator();
			while (It)
			{
				FName StructName = Composition.GetFragments().DebugGetStructTypeName(*It);
				const UScriptStruct* StructType = Composition.GetFragments().GetTypeAtIndex(*It);
				
				FragmentsByName.Add(StructName, StructType);
				++It;
			}

			FMassChunkFragmentBitSet::FIndexIterator ChunkIt = Composition.GetChunkFragments().GetIndexIterator();
			while (ChunkIt)
			{
				FName StructName = Composition.GetChunkFragments().DebugGetStructTypeName(*ChunkIt);
				const UScriptStruct* StructType = Composition.GetChunkFragments().GetTypeAtIndex(*ChunkIt);

				FragmentsByName.Add(StructName, StructType);
				++ChunkIt;
			}

			FMassSharedFragmentBitSet::FIndexIterator SharedFragIt = Composition.GetSharedFragments().GetIndexIterator();
			while (SharedFragIt)
			{
				FName StructName = Composition.GetSharedFragments().DebugGetStructTypeName(*SharedFragIt);
				const UScriptStruct* StructType = Composition.GetSharedFragments().GetTypeAtIndex(*SharedFragIt);

				FragmentsByName.Add(StructName, StructType);
				++SharedFragIt;
			}

			FMassConstSharedFragmentBitSet::FIndexIterator ConstSharedFragIt = Composition.GetConstSharedFragments().GetIndexIterator();
			while (ConstSharedFragIt)
			{
				FName StructName = Composition.GetConstSharedFragments().DebugGetStructTypeName(*ConstSharedFragIt);
				const UScriptStruct* StructType = Composition.GetConstSharedFragments().GetTypeAtIndex(*ConstSharedFragIt);

				FragmentsByName.Add(StructName, StructType);
				++ConstSharedFragIt;
			}
		}
	}

	FoundType = FragmentsByName.Find(FragmentName);
	if (FoundType)
	{
		return *FoundType;
	}

	return nullptr;
}

// ===== Fragment 数据读取 =============================================================
// 这组 API 给 Debugger UI 用：在外部窥探实体的 fragment 当前值（复制返回，不会污染原数据）。

/** 重载1：内部分配 FStructOnScope 后调重载2。 */
TSharedPtr<FStructOnScope> FMassDebugger::GetFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity)
{
	TSharedPtr<FStructOnScope> StructOnScope = MakeShared<FStructOnScope>(FragmentType);
	if (GetFragmentData(EntityManager, FragmentType, Entity, StructOnScope))
	{
		return StructOnScope;
	}
	return nullptr;
}

/**
 * 重载2：取实体 archetype，从 chunk 中找到 fragment 内存指针，CopyScriptStruct 到 OutStructData。
 * 关键点：返回的是**副本**——UI 编辑后需要其它机制写回（不是直接修改实体）。
 */
bool FMassDebugger::GetFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity, TSharedPtr<FStructOnScope>& OutStructData)
{
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	TSharedPtr<FMassArchetypeData> Archetype = EntityManager.DebugGetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	if (Archetype.IsValid())
	{
		void* FragmentData = Archetype->GetFragmentDataForEntity(FragmentType, Entity.Index);
		if (FragmentData)
		{
			const UStruct* AsUStruct = FragmentType;
			if (OutStructData->GetStruct() != AsUStruct)
			{
				OutStructData->Initialize(FragmentType);
			}
			
			CastChecked<UScriptStruct>(OutStructData->GetStruct())->CopyScriptStruct(OutStructData->GetStructMemory(), FragmentData);
			return true;
		}
	}
	return false;
}

const FMassArchetypeSharedFragmentValues& FMassDebugger::GetSharedFragmentValues(const FMassEntityManager& EntityManager, FMassEntityHandle Entity)
{
	if (EntityManager.IsEntityValid(Entity))
	{
		TSharedPtr<FMassArchetypeData> Archetype = EntityManager.DebugGetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
		if (Archetype.IsValid())
		{
			return Archetype->GetSharedFragmentValues(Entity);
		}
	}
	
	static FMassArchetypeSharedFragmentValues Dummy;
	return Dummy;
}

TSharedPtr<FStructOnScope> FMassDebugger::GetSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity)
{
	TSharedPtr<FStructOnScope> StructOnScope = MakeShared<FStructOnScope>(FragmentType);
	if (GetSharedFragmentData(EntityManager, FragmentType, Entity, StructOnScope))
	{
		return StructOnScope;
	}
	return nullptr;
}

bool FMassDebugger::GetSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity, TSharedPtr<FStructOnScope>& OutStructData)
{
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	TSharedPtr<FMassArchetypeData> Archetype = EntityManager.DebugGetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	if (Archetype.IsValid())
	{
		const FSharedStruct* SharedFragment = Archetype->GetSharedFragmentValues(Entity).GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(FragmentType));
		void* FragmentData = (SharedFragment != nullptr) ? SharedFragment->GetMemory() : nullptr;

		if (FragmentData)
		{
			const UStruct* AsUStruct = FragmentType;
			if (OutStructData->GetStruct() != AsUStruct)
			{
				OutStructData->Initialize(FragmentType);
			}

			CastChecked<UScriptStruct>(OutStructData->GetStruct())->CopyScriptStruct(OutStructData->GetStructMemory(), FragmentData);
			return true;
		}
	}
	return false;
}

TSharedPtr<FStructOnScope> FMassDebugger::GetConstSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity)
{
	TSharedPtr<FStructOnScope> StructOnScope = MakeShared<FStructOnScope>(FragmentType);
	if (GetConstSharedFragmentData(EntityManager, FragmentType, Entity, StructOnScope))
	{
		return StructOnScope;
	}
	return nullptr;
}

bool FMassDebugger::GetConstSharedFragmentData(const FMassEntityManager& EntityManager, const UScriptStruct* FragmentType, FMassEntityHandle Entity, TSharedPtr<FStructOnScope>& OutStructData)
{
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	TSharedPtr<FMassArchetypeData> Archetype = EntityManager.DebugGetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	if (Archetype.IsValid())
	{
		const FConstSharedStruct* SharedFragment = Archetype->GetSharedFragmentValues(Entity).GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(FragmentType));
		const void* FragmentData = (SharedFragment != nullptr) ? SharedFragment->GetMemory() : nullptr;

		if (FragmentData)
		{
			const UStruct* AsUStruct = FragmentType;
			if (OutStructData->GetStruct() != AsUStruct)
			{
				OutStructData->Initialize(FragmentType);
			}

			CastChecked<UScriptStruct>(OutStructData->GetStruct())->CopyScriptStruct(OutStructData->GetStructMemory(), FragmentData);
			return true;
		}
	}
	return false;
}

/**
 * 【RefreshBreakpoints —— 重建加速集合】
 *
 * 当外部代码直接修改了 Environment.Breakpoints 数组（增删/改 trigger）后调用，
 * 让 ProcessorsWithBreakpoints / FragmentsWithBreakpoints 与 bHasBreakpoint 标志重新对齐。
 *
 * 算法：
 *   1) 重置全局 bHasBreakpoint=false
 *   2) 遍历每个 environment：
 *      a) Reset 两个加速集合
 *      b) 遍历 Breakpoints，按 Trigger 的 TVariant 类型决定加进哪个集合
 *      c) 根据集合非空性更新 env.bHasBreakpoint
 *   3) 把所有 env.bHasBreakpoint 或合到全局 bHasBreakpoint
 *   4) 广播 OnBreakpointsChanged
 */
void FMassDebugger::RefreshBreakpoints()
{
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = false;

	for (FEnvironment& Environment : ActiveEnvironments)
	{
		Environment.ProcessorsWithBreakpoints.Reset();
		Environment.FragmentsWithBreakpoints.Reset();

		for (UE::Mass::Debug::FBreakpoint& BreakPoint : Environment.Breakpoints)
		{
			if(TObjectKey<const UMassProcessor>* BreakProcessor = BreakPoint.Trigger.TryGet<TObjectKey<const UMassProcessor>>())
			{
				Environment.ProcessorsWithBreakpoints.Add(*BreakProcessor);
			}
			else if (TObjectKey<const UScriptStruct>* BreakFragment = BreakPoint.Trigger.TryGet<TObjectKey<const UScriptStruct>>())
			{
				Environment.FragmentsWithBreakpoints.Add(*BreakFragment);
			}
		}

		Environment.bHasBreakpoint = Environment.ProcessorsWithBreakpoints.Num() != 0
			|| Environment.FragmentsWithBreakpoints.Num() != 0;
		UE::Mass::Debug::FBreakpoint::bHasBreakpoint |= Environment.bHasBreakpoint;
	}

	OnBreakpointsChangedDelegate.Broadcast();
}

FMassDebugger::FEnvironment& FMassDebugger::GetActiveEnvironmentChecked(const FMassEntityManager& EntityManager)
{
	const int32 Index = ActiveEnvironments.IndexOfByPredicate([WeakManager = EntityManager.AsWeak()](const FEnvironment& Element)
	{
		return Element.EntityManager == WeakManager;
	});

	checkf(Index != INDEX_NONE, TEXT("Mass Debug Environment not found for specified EntitManager"));

	return ActiveEnvironments[Index];
}

FMassDebugger::FEnvironment* FMassDebugger::GetActiveEnvironment(const FMassEntityManager& EntityManager)
{
	const int32 Index = ActiveEnvironments.IndexOfByPredicate([WeakManager = EntityManager.AsWeak()](const FEnvironment& Element)
	{
		return Element.EntityManager == WeakManager;
	});

	if (Index == INDEX_NONE)
	{
		return nullptr;
	}

	return &ActiveEnvironments[Index];
}

// ===== FEnvironment 实现 =============================================================

/**
 * 【FEnvironment 构造函数】
 * 初始化 weak ptr。若启用了 Mass trace，注册"trace 启动时回调"——
 * 当 Insights 开始 trace 时自动把现有 archetype 全部 emit 一次"创建事件"，
 * 让 trace 时间轴拥有完整的初始状态快照（否则只有 trace 启动后新建的 archetype 会出现）。
 */
FMassDebugger::FEnvironment::FEnvironment(FMassEntityManager& InEntityManager)
	: EntityManager(InEntityManager.AsWeak())
	, MutableEntityManager(InEntityManager.AsWeak())
{
#if UE_MASS_TRACE_ENABLED
	TraceStartedDelegateHandle = FTraceAuxiliary::OnTraceStarted.AddLambda([WeakEntityManager = EntityManager](FTraceAuxiliary::EConnectionType TraceType, const FString& TraceDestination)
	{
		if (TSharedPtr<const FMassEntityManager> Manager = WeakEntityManager.Pin())
		{
			ForEachArchetype(*Manager, [](FMassArchetypeHandle ArchetypeHandle)
			{
				UE_TRACE_MASS_ARCHETYPE_CREATED(ArchetypeHandle)
			});
		}
	});
#endif
}

/** 析构：解绑 trace 委托。 */
FMassDebugger::FEnvironment::~FEnvironment()
{
#if UE_MASS_TRACE_ENABLED
	FTraceAuxiliary::OnTraceStarted.Remove(TraceStartedDelegateHandle);
#endif
}

void FMassDebugger::FEnvironment::ClearBreakpoints()
{
	ProcessorsWithBreakpoints.Reset();
	FragmentsWithBreakpoints.Reset();
	Breakpoints.Reset();
	// 注意：直接把全局 bHasBreakpoint 置 false——但其他 environment 可能仍有断点！
	// 调用方（FMassDebugger::ClearAllBreakpoints / ShutdownDebugger）需要确保所有 env 都清空才行。
	// 单独对一个 env 调用此函数会错误地把全局标志关掉，影响其他 env。
	// 这点设计上有点 fragile：详见 ARCHITECTURE.md 的潜在问题列表。
	UE::Mass::Debug::FBreakpoint::bHasBreakpoint = false;
}

UE::Mass::Debug::FBreakpoint* FMassDebugger::FEnvironment::FindBreakpointByHandle(UE::Mass::Debug::FBreakpointHandle Handle)
{
	for (UE::Mass::Debug::FBreakpoint& Breakpoint : Breakpoints)
	{
		if (Breakpoint.Handle == Handle)
		{
			return &Breakpoint;
		}
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_MASSENTITY_DEBUG
