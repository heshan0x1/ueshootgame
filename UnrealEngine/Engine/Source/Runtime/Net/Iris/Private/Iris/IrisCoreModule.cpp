// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// IrisCoreModule.cpp —— Iris 框架的「UE 模块入口（顶层 L5）」
// -----------------------------------------------------------------------------
// FIrisCoreModule 是 IrisCore 模块（Engine/Source/Runtime/Net/Iris）唯一的
// IModuleInterface 实现，由 IMPLEMENT_MODULE 在 .dll/.so 加载时实例化。
// 它负责把 Iris 的所有「全局状态」与 UE 的模块生命周期编排起来：
//
//   ┌──────────────────────── StartupModule（启动）─────────────────────────┐
//   │ 1) LoadModuleChecked("NetCore")        强依赖：必须先于 Iris 装载       │
//   │ 2) 订阅 OnAllModuleLoadingPhasesComplete 等所有阶段加载完成再发首次广播  │
//   │ 3) 解析命令行 -UseIrisReplication=N，覆盖 net.Iris.UseIrisReplication   │
//   │ 4) RegisterPropertyNetSerializerSelectorTypes()：                       │
//   │    Reset → PreFreeze 广播 → 注册默认 Property→Serializer → Freeze →     │
//   │    PostFreeze 广播。这是 NetSerializer 注册表的「唯一一次有序构建」，    │
//   │    Freeze 后注册表不再可改（Polymorphic 仅能在加载新模块时刷新）。       │
//   │ 5) UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL()：把旧 PushModel 宏转发到         │
//   │    Iris 的 FDirtyNetObjectTracker，使存量代码 MARK_PROPERTY_DIRTY 仍生效 │
//   │ 6) 订阅 FModuleManager::OnModulesChanged                                │
//   │ 7) 订阅 ReplicationSystem Created/Destroyed 委托                        │
//   │ 8) 枚举已存在 RS 初始化 RepSystemCount                                  │
//   │ 9) 订阅 NetTrace 重置回调，重置 LifetimeCondition 调试名映射             │
//   └─────────────────────────────────────────────────────────────────────────┘
//
//   ┌─────────── 运行期：模块热加载（OnModulesChanged）协调 ─────────────────┐
//   │  ModuleLoaded → ++LoadedModulesCount，若 RepSystemCount>0 且尚未挂      │
//   │   Ticker，则注册一个 FTSTicker：把多次「模块加载」合并为一次             │
//   │   BroadcastLoadedModulesUpdated()，避免连续加载 N 个插件触发 N 次广播；  │
//   │  无论是否需要广播，都立刻调用一次 PreFreeze→PostFreeze，让新模块在      │
//   │   注册表上完成「补充注册」（Polymorphic 子类型等）。                     │
//   └─────────────────────────────────────────────────────────────────────────┘
//
//   ┌──── 多 ReplicationSystem（PIE 多实例 / Server+Client 同进程）协调 ─────┐
//   │  OnRepSystemCreated  ：若已有未广播模块加载，立刻 ForceBroadcast；      │
//   │                        然后 ++RepSystemCount。                          │
//   │  OnRepSystemDestroyed：--RepSystemCount。                                │
//   │  目的：让「所有运行中的 RS 都看到同一份 NetSerializer 注册表快照」。     │
//   └─────────────────────────────────────────────────────────────────────────┘
//
//   ┌──────────────────── ShutdownModule（卸载）─────────────────────────────┐
//   │  反订阅顺序：OnModulesChanged → RepSysCreated/Destroyed →              │
//   │              SHUTDOWN_LEGACY_PUSH_MODEL                                │
//   └─────────────────────────────────────────────────────────────────────────┘
//
//   关键不变量（必读 Docs/Modules/IrisCoreModule.md §2-§4）：
//     * NetCore 必须先于 IrisCore 加载，否则 RegisterDefaultPropertyNet... 会
//       因找不到 Property reflection 而崩溃；
//     * Pre/Post Freeze 广播的「时序」是约定：插件在 PreFreeze 完成注册，在
//       PostFreeze 解析依赖（例如取已注册 struct 的 Descriptor）；
//     * Freeze 后的注册表通过排序保证 O(log n) 查找，且禁止再写。
// =============================================================================

#include "CoreTypes.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"

#include "Net/Core/Connection/NetEnums.h"
#include "Net/Core/Trace/Private/NetTraceInternal.h"

#include "Iris/Core/IrisLog.h"
#include "Iris/ReplicationState/DefaultPropertyNetSerializerInfos.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationSystem/LegacyPushModel.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/Serialization/InternalNetSerializerDelegates.h"
#include "Iris/IrisConfigInternal.h"


// =============================================================================
// FIrisCoreModule —— Iris 唯一的 IModuleInterface 实现
// 由 IMPLEMENT_MODULE(FIrisCoreModule, IrisCore) 在 dll/so 加载时构造。
// =============================================================================
class FIrisCoreModule : public IModuleInterface
{
private:

	// -------------------------------------------------------------------------
	// RegisterPropertyNetSerializerSelectorTypes
	// 构建 Property -> NetSerializer 选择器注册表（FPropertyNetSerializerInfoRegistry）。
	// 这是 NetSerializer 体系的「唯一一次有序构建」，只能在 StartupModule 调一次。
	//
	// 顺序：
	//   1) Reset()                       —— 清空（防止重新初始化时残留）；
	//   2) BroadcastPreFreezeNetSerializerRegistry —— 给外部插件机会注册自定义
	//      NetSerializerInfo；此时注册表仍可写。
	//   3) RegisterDefaultPropertyNetSerializerInfos —— 引擎默认绑定（int/float/
	//      FName/FVector/Enum/Struct ...）；
	//   4) Freeze()                      —— 排序 + 锁定注册表，之后再调用注册
	//      接口将 ensure；查找路径切到 O(log n) 二分。
	//   5) BroadcastPostFreezeNetSerializerRegistry —— 给外部插件做「依赖二次绑定」
	//      的机会（如 Polymorphic Struct 此时拿到所有已注册描述符做映射构建）。
	// -------------------------------------------------------------------------
	void RegisterPropertyNetSerializerSelectorTypes()
	{
		using namespace UE::Net;
		using namespace UE::Net::Private;

		FPropertyNetSerializerInfoRegistry::Reset();

		FInternalNetSerializerDelegates::BroadcastPreFreezeNetSerializerRegistry();
		RegisterDefaultPropertyNetSerializerInfos();

		FPropertyNetSerializerInfoRegistry::Freeze();
		FInternalNetSerializerDelegates::BroadcastPostFreezeNetSerializerRegistry();
	}

	// -------------------------------------------------------------------------
	// StartupModule —— 按 Docs/Modules/IrisCoreModule.md §2 流程顺序：
	//   1) 强依赖装载 NetCore（提供 NetEnums / NetGuid / Property 反射等）；
	//   2) 订阅「所有模块加载阶段完成」回调；
	//   3) 命令行覆盖 CVar；
	//   4) 构建 NetSerializer 注册表（Pre/Post Freeze 广播）；
	//   5) 桥接旧 PushModel；
	//   6) 订阅模块热加载；
	//   7) 订阅 ReplicationSystem Created/Destroyed；
	//   8) 用已存在 RS 数量初始化 RepSystemCount；
	//   9) NetTrace 重置回调。
	// -------------------------------------------------------------------------
	virtual void StartupModule() override
	{
		// Iris requires NetCore
		// (1) NetCore 是 Iris 的硬依赖（NetEnums.h 中的 EReplicationSystem、
		//     NetGuid、Property 反射等都在 NetCore 中），必须保证已加载，
		//     否则后面的 RegisterDefaultPropertyNetSerializerInfos 会 crash。
		FModuleManager::LoadModuleChecked<IModuleInterface>("NetCore");

		// (2) 订阅「所有模块加载阶段完成」：FIrisCoreModule 自身可能在
		//     PostConfigInit 等较早阶段就被加载，但部分依赖 Iris 的模块（注册
		//     自定义 NetSerializer 的插件）会在更晚的 LoadingPhase 才装载。
		//     因此首次「LoadedModulesUpdated」广播延迟到所有阶段都跑完后再发，
		//     避免在加载途中就强制冻结注册表导致后续模块无处注册。
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FIrisCoreModule::OnAllModuleLoadingPhasesComplete);

		// Check command line for whether we should override the net.Iris.UseIrisReplication cvar, as we need to do that early
		// (3) 命令行 -UseIrisReplication=N 必须在 NetDriver 创建前就把 CVar 改掉，
		//     否则 NetDriver::IsUsingIrisReplication() 会读到旧值导致路径选错。
		//     EReplicationSystem::Default 表示「命令行未指定」，此时不覆盖，由
		//     ini / 默认值决定。
		const EReplicationSystem CmdlineRepSystem = UE::Net::GetUseIrisReplicationCmdlineValue();
		if (CmdlineRepSystem != EReplicationSystem::Default)
		{
			const bool bEnableIris = CmdlineRepSystem == EReplicationSystem::Iris;
			UE::Net::SetUseIrisReplication(bEnableIris);
		}

		// (4) 构建 Property → NetSerializer 注册表（详见函数注释）。
		RegisterPropertyNetSerializerSelectorTypes();

		// (5) 桥接旧 PushModel：让所有已经使用 MARK_PROPERTY_DIRTY_FROM_NAME
		//     等旧宏的代码自动把 dirty 信号转发到 Iris 的 FDirtyNetObjectTracker。
		//     这是 Iris 兼容存量项目的关键钩子。
		UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL();
		
		// (6) 订阅模块热加载：用于在新模块装载后刷新 NetSerializer 注册表
		//     （插件、可选 GameFeature 等可能在运行期被加载）。
		ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FIrisCoreModule::OnModulesChanged);
		// (7) 订阅 ReplicationSystem Created/Destroyed：维护 RepSystemCount，
		//     在「新 RS 创建前」必须先 flush 待广播的模块更新（保证新 RS 看到
		//     最新注册表），并在「最后一个 RS 销毁后」允许批量再次广播。
		RepSysCreatedHandle = UE::Net::FReplicationSystemFactory::GetReplicationSystemCreatedDelegate().AddRaw(this, &FIrisCoreModule::OnRepSystemCreated);
		RepSysDestroyedHandle = UE::Net::FReplicationSystemFactory::GetReplicationSystemDestroyedDelegate().AddRaw(this, &FIrisCoreModule::OnRepSystemDestroyed);

		// Figure out how many ReplicationSystems there are so we start on a correct balance prior to getting callbacks.
		// (8) Iris 模块本身也可能被「事后」加载（典型如 commandlet / dev 工具），
		//     此时已有 RS 实例存在；为让 RepSystemCount 自启动起就处于正确平衡，
		//     先扫一遍所有现存实例做计数初始化。
		for (TArrayView<UReplicationSystem*> RepSystems = UE::Net::FReplicationSystemFactory::GetAllReplicationSystems(); const UReplicationSystem* RepSystem : RepSystems)
		{
			RepSystemCount += (RepSystem != nullptr ? 1 : 0);
		}

#if UE_NET_TRACE_ENABLED
		// (9) NetTrace 在调用 ResetPersistentNetDebugNames 时会清掉持久字符串池；
		//     Iris 的 LifetimeCondition 调试名映射也必须同步重置，否则下次 trace
		//     输出会引用悬空的名字 ID。
		FNetTrace::OnResetPersistentNetDebugNames.AddLambda([]() 
		{
			UE::Net::ResetLifetimeConditionDebugNames();
		});
#endif
	}

	// -------------------------------------------------------------------------
	// ShutdownModule —— 反订阅顺序与 StartupModule 顺序对称（先解模块委托，
	// 再解 RS 委托，最后关闭旧 PushModel 桥接）。
	// 不需要解订 OnAllModuleLoadingPhasesComplete：该委托只触发一次，回调里
	// 会自行 RemoveAll(this)。
	// -------------------------------------------------------------------------
	virtual void ShutdownModule() override
	{
		// 解订模块热加载，避免在卸载途中再被拉去执行 OnModulesChanged。
		if (ModulesChangedHandle.IsValid())
		{
			FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);
			ModulesChangedHandle.Reset();
		}

	
		// 解订 RS Created/Destroyed —— 即使尚有 RS 残留，也不再做 broadcast。
		UE::Net::FReplicationSystemFactory::GetReplicationSystemCreatedDelegate().Remove(RepSysCreatedHandle);
		UE::Net::FReplicationSystemFactory::GetReplicationSystemDestroyedDelegate().Remove(RepSysDestroyedHandle);
		RepSysCreatedHandle.Reset();
		RepSysDestroyedHandle.Reset();

		// 关闭旧 PushModel 桥接：把全局函数指针置回旧实现 / nullptr，
		// 防止 Iris 模块卸载后 MARK_PROPERTY_DIRTY 仍调用已失效函数。
		UE_NET_IRIS_SHUTDOWN_LEGACY_PUSH_MODEL();
	}

	// Iris 含有大量在启动期一次性构建、Freeze 后只读的全局表（NetSerializer
	// 注册表、PolymorphicMapping…）。允许 hot reload 会让这些表与旧 dll 中的
	// 实例错位，因此显式禁止动态 reload。
	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	// -------------------------------------------------------------------------
	// OnAllModuleLoadingPhasesComplete
	// 所有模块加载阶段（Default/PostConfigInit/PreLoadingScreen/...）跑完后触发。
	// 此时：
	//   * 启用 LoadedModulesUpdated 广播开关（StartupModule 期间为防早发已置 false）；
	//   * 若期间已经创建过 RS 且有「待广播的模块更新」，立即强制广播一次，
	//     让 RS 看到最终的注册表快照。
	// 自身委托用 RemoveAll 反订阅，因为该委托一生中只触发一次，无需常驻。
	// -------------------------------------------------------------------------
	void OnAllModuleLoadingPhasesComplete()
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.RemoveAll(this);

		bAllowLoadedModulesUpdatedCallback = true;
		if (RepSystemCount > 0 && ShouldBroadcastLoadedModulesUpdated())
		{
			ForceBroadcastLoadedModulesUpdated();
		}
	}

	// -------------------------------------------------------------------------
	// OnModulesChanged —— FModuleManager 在每次模块加载/卸载时回调。
	// 我们关心 ModuleLoaded：
	//   1) 计数 ++LoadedModulesCount；
	//   2) 若已开放广播且当前有活跃 RS，挂一个 FTSTicker。Ticker 的存在用于
	//      把「连续多次模块加载」合并为单次广播（见 BroadcastLoadedModulesUpdated）；
	//   3) 立即跑一对 PreFreeze/PostFreeze 广播——这两次广播即使在 Freeze 之后
	//      也会被分发给订阅者，由订阅者自行决定是否做「补充注册」（实际上
	//      Freeze 后注册表本身不可写，但 Polymorphic 子类型映射等仍可在
	//      PostFreeze 阶段重建）。
	// 模块卸载（ModuleUnloaded）目前不做处理：Iris 假设 NetSerializer 一旦注册就
	// 不会消失（卸载插件需要重启进程），见 SupportsDynamicReloading() == false。
	// -------------------------------------------------------------------------
	void OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange)
	{
		switch (ReasonForChange)
		{
			case EModuleChangeReason::ModuleLoaded:
			{
				++LoadedModulesCount;
				if (bAllowLoadedModulesUpdatedCallback && (RepSystemCount > 0))
				{
					if (!BroadcastModulesUpdatedHandle.IsValid())
					{
						// 首次进入合并窗口：记下窗口起点，并注册一个 Ticker。
						// Ticker 每帧检查「窗口期内是否还有新模块加载」，是
						// 则继续推迟一帧，直到一帧内没有新模块加载为止才广播。
						LoadedModulesCountAtTickerCreation = LoadedModulesCount;
						BroadcastModulesUpdatedHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FIrisCoreModule::BroadcastLoadedModulesUpdated));
					}
				}

				// 即时广播一次 Pre/Post Freeze：让订阅者得到「新模块已加载」的
				// 信号，便于做注册表层面的依赖刷新（如重建 Polymorphic 映射）。
				UE::Net::Private::FInternalNetSerializerDelegates::BroadcastPreFreezeNetSerializerRegistry();
				UE::Net::Private::FInternalNetSerializerDelegates::BroadcastPostFreezeNetSerializerRegistry();
			}
			break;

			default:
			{
				// 其他 reason（Unloading / PluginDirectoryChanged 等）忽略。
			}
			break;
		}
	}

	// -------------------------------------------------------------------------
	// BroadcastLoadedModulesUpdated（FTSTicker 回调）
	// Ticker 返回 true 表示「下帧继续 tick」、返回 false 表示「移除 ticker」。
	// 算法：
	//   * 若上帧记录的计数 != 当前计数 → 期间又有模块加载 → 把记录对齐并
	//     继续 tick 一帧（return true），形成「等待安静期」效果；
	//   * 若两次计数相同 → 已经一帧内无新模块加载 → 真正广播
	//     BroadcastLoadedModulesUpdated()，并通过 BroadcastModulesUpdatedHandle.Reset()
	//     表示 ticker 自行解除（return false）。
	// 警告：若广播时仍存在活跃 RS（RepSystemCount>0），并且业务有 Polymorphic
	//   类型在新模块中注册，可能导致已 Connect 的 baseline 失配，因此输出
	//   warning 提示需重启 NetDriver / RS。
	// -------------------------------------------------------------------------
	bool BroadcastLoadedModulesUpdated(float /* DeltaTime */)
	{
		// If we're still loading modules check again next frame
		if (LoadedModulesCountAtTickerCreation != LoadedModulesCount)
		{
			LoadedModulesCountAtTickerCreation = LoadedModulesCount;
			return true;
		}
		else
		{
			if (RepSystemCount > 0)
			{
				UE_LOG(LogIris, Warning, TEXT("FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated() called while there are %d active ReplicationSystems. If polymorphic types are registered we may have corrupt data. A restart of the ReplicationSystem or NetDriver is recommended. Total loaded modules: %u."), RepSystemCount, LoadedModulesCount);
			}
			else
			{
				UE_LOG(LogIris, Display, TEXT("FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated() called while there are %d active ReplicationSystems. This is %s. Total loaded modules: %u."), RepSystemCount, (RepSystemCount == 0 ? TEXT("good") : TEXT("unexpected")), LoadedModulesCount);
			}

			// 标记 ticker 已自行解除（FTSTicker 返回 false 时框架会移除 entry，
			// 这里同步把 handle 清零让 OnModulesChanged 下次能再注册新 ticker）。
			BroadcastModulesUpdatedHandle.Reset();
			UE::Net::Private::FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated();
			return false;
		}
	}

	// -------------------------------------------------------------------------
	// ForceBroadcastLoadedModulesUpdated
	// 跳过 Ticker 合并窗口，立即广播。典型调用点：
	//   * OnAllModuleLoadingPhasesComplete：阶段加载完毕后立刻发首次广播；
	//   * OnRepSystemCreated：新 RS 创建前必须保证它看到最新注册表。
	// 实现：先移除尚未触发的 ticker（避免重复广播），把窗口起点对齐，再调用
	// BroadcastLoadedModulesUpdated(0.0f) 让其走「计数相等→直接广播」分支。
	// -------------------------------------------------------------------------
	void ForceBroadcastLoadedModulesUpdated()
	{
		ResetBroadcastLoadedModulesTicker();
		LoadedModulesCountAtTickerCreation = LoadedModulesCount;
		BroadcastLoadedModulesUpdated(0.0f);
	}

	// 是否「需要广播」：要么有活跃 ticker（说明窗口里有未发的更新），要么
	// 上次广播以来计数已变（说明在合并窗口外又有模块加载未触发广播）。
	bool ShouldBroadcastLoadedModulesUpdated() const
	{
		return BroadcastModulesUpdatedHandle.IsValid() || (LoadedModulesCountAtTickerCreation != LoadedModulesCount);
	}

	// -------------------------------------------------------------------------
	// OnRepSystemCreated —— FReplicationSystemFactory 在创建 UReplicationSystem
	// 后回调。这里必须在 ++RepSystemCount 之前先做广播判定，原因：
	//   * 期望日志输出体现「广播时还没有此 RS」，便于和 Destroyed 路径对账；
	//   * 新 RS 在自己的初始化中可能立刻读取注册表，必须保证「读之前已广播」。
	// 若没有未广播的更新（计数与上次广播一致），仅打 Display 日志。
	// 多 PIE / Server+Client 同进程：每个实例各自创建 RS，会多次进入此函数，
	// RepSystemCount 因此可能 >1。
	// -------------------------------------------------------------------------
	void OnRepSystemCreated(UReplicationSystem*)
	{
		if (ShouldBroadcastLoadedModulesUpdated())
		{
			ForceBroadcastLoadedModulesUpdated();
		}
		else if (RepSystemCount == 0)
		{
			UE_LOG(LogIris, Display, TEXT("FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated() not called when creating ReplicationSystem since no additional modules have been loaded since last broadcast. This is good. Total loaded modules: %u."), RepSystemCount, LoadedModulesCount);
		}

		// Update RepSystemCount after broadcasting such that we get logging ideally saying there weren't any active replication systems.
		// 注意此处 ++ 必须在广播之后；调换次序会让上面的「广播时仍有 N 个 RS」
		// 警告把刚刚创建的这个也算进去，污染日志含义。
		++RepSystemCount;
	}

	// RS 销毁回调：仅做计数。GC 阶段也可能触发；ensure 用于及早发现失衡。
	void OnRepSystemDestroyed(UReplicationSystem*)
	{
		--RepSystemCount;
		ensure(RepSystemCount >= 0);
	}

	// 安全移除 ticker：FTSTicker 接口要求传 handle，移除后 handle 必须 Reset
	// 以让 IsValid() 返回 false（防止 ForceBroadcast 误判仍有 ticker）。
	void ResetBroadcastLoadedModulesTicker()
	{
		if (BroadcastModulesUpdatedHandle.IsValid())
		{
			FTSTicker::RemoveTicker(BroadcastModulesUpdatedHandle);
			BroadcastModulesUpdatedHandle.Reset();
		}
	}

private:
	// FModuleManager::OnModulesChanged 的订阅句柄；ShutdownModule 中解订。
	FDelegateHandle ModulesChangedHandle;
	// FReplicationSystemFactory 的 Created / Destroyed 委托句柄。
	FDelegateHandle RepSysCreatedHandle;
	FDelegateHandle RepSysDestroyedHandle;
	// 合并广播 ticker 的句柄；IsValid() == true 表示当前正处于「合并窗口」期。
	FTSTicker::FDelegateHandle BroadcastModulesUpdatedHandle;
	// 当前活跃 ReplicationSystem 数量（多 PIE / Server+Client 同进程时 >1）。
	int32 RepSystemCount = 0;
	// 自模块启动起累计加载的模块总数（单调递增）。
	uint32 LoadedModulesCount = 0;
	// Ticker 注册时刻的 LoadedModulesCount 快照；与最新值比较以判断「窗口期内
	// 是否仍有新模块加载」，从而决定是再等一帧还是立刻广播。
	uint32 LoadedModulesCountAtTickerCreation = 0;
	// 是否允许触发 LoadedModulesUpdated 广播。
	// StartupModule 期间为 false（防止首批引擎模块加载时触发不必要广播）；
	// OnAllModuleLoadingPhasesComplete 之后置 true，正常运行期合并广播策略生效。
	bool bAllowLoadedModulesUpdatedCallback = false;
};
// IMPLEMENT_MODULE 在 IrisCore.dll/so 加载时自动构造 FIrisCoreModule 实例并
// 调用 StartupModule；卸载时调用 ShutdownModule 后析构。模块名 "IrisCore"
// 必须与 .Build.cs 中的 ModuleName 一致。
IMPLEMENT_MODULE(FIrisCoreModule, IrisCore);
