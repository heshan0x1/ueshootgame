// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// IrisDebugging.cpp —— Iris 调试 helper（CVar + 控制台命令 + extern "C" 导出）
// ----------------------------------------------------------------------------
// 职责：为 Iris 运行期调试提供一整套"手动断点 + 状态导出"辅助接口，包含：
//
//   1) 四个过滤器 CVar / ConsoleCommand：
//        Net.Iris.DebugName            —— 按对象名（模糊包含匹配）设置断点；
//        Net.Iris.DebugRPCName         —— 按 RPC 名字（模糊包含匹配）设置断点；
//        Net.Iris.DebugNetRefHandle    —— 按 NetRefHandle Id（精确匹配）设置断点；
//        Net.Iris.DebugNetInternalIndex—— 按 InternalNetRefIndex（精确匹配）设置断点。
//      Iris 运行时在对象处理关键节点调用 BreakOnObjectName / BreakOnRPCName /
//      BreakOnNetRefHandle / BreakOnInternalNetRefIndex 判断，命中则
//      UE_DEBUG_BREAK 触发中断——这样不用改代码就能精确停到"某对象/某 RPC"
//      第一次到达某阶段的位置。
//
//   2) 一组 extern "C" 导出函数（在 VS Watch/Immediate Window 直接可调）：
//        DebugNetObject(UObject*)                   —— 从 UObject* 查全部 RS 拿到复制信息；
//        DebugNetObjectById(UObject*, RSId)         —— 同上，限定 RS；
//        DebugNetRefHandle(Handle)                  —— 按完整 Handle 查；
//        DebugNetRefHandleById(NetRefHandleId, RSId)—— 按 Id+RSId 查；
//        DebugInternalNetRefIndex(Idx, RSId)        —— 按内部索引查；
//        DebugOutputNetObjectState(NetRefHandleId, RSId)         —— 打印对象当前状态 → DebugOutput；
//        DebugNetObjectStateToString(NetRefHandleId, RSId)       —— 返回 TCHAR*（共享静态 buffer，仅调试用）；
//        DebugOutputNetObjectProtocolReferences(ProtocolId, RSId)—— 打印某 Protocol 被哪些对象使用；
//        DebugNetObjectProtocolReferencesToString(ProtocolId, RSId) —— 同上字符串版本。
//        SetIrisDebugObjectName / SetIrisDebugNetRefHandle /
//        SetIrisDebugInternalNetRefIndex / SetIrisDebugInternalNetRefIndexViaNetHandle /
//        SetIrisDebugInternalNetRefIndexViaObject / SetIrisDebugRPCName
//                 —— 在 Watch 窗口直接调用以设置上述"断点条件"，等价于 CVar 赋值。
//
//   3) Init()：纯"防符号被链接器裁剪"的辅助——把所有 Debug* 函数地址累加到
//      一个 volatile 变量返回，阻止 LTO/死代码消除把这些"只在调试期被 Watch
//      窗口用到"的符号剥离。
//
// 对上层的引用：
//   * UE::Net::Private::FNetRefHandleManager
//   * UE::Net::Private::FReplicationProtocolManager
//   * UE::Net::Private::FReplicationSystemInternal / UReplicationSystem
//   * UE::Net::FReplicationInstanceOperations::OutputInternalStateToString
//   * UE::Net::FReplicationSystemFactory::GetAllReplicationSystems
//   * UObjectReplicationBridge::GetReplicatedRefHandle
//   均用于"从 Handle → InternalIndex → ReplicatedObjectData → Protocol → 状态"
//   的一步步下钻打印。
//
// 典型使用流程（调试器 Watch 窗口）：
//   1) 在 Watch 中输入  SetIrisDebugInternalNetRefIndexViaObject((UObject*)0x...)
//      → 设置"遇到该对象时断点"；
//   2) 继续运行，Iris 内部调用 BreakOnInternalNetRefIndex 命中 → UE_DEBUG_BREAK；
//   3) 在断下来的位置再 Watch  DebugNetObjectStateToString(NetRefHandleId, RSId)
//      → 打印当前完整状态快照。
//
// 门控：整个文件被 #if !UE_BUILD_SHIPPING 包围——Shipping 下不编译任何调试功能。
// ============================================================================

#include "Iris/Core/IrisDebugging.h"

#if !UE_BUILD_SHIPPING

#include "UObject/Field.h"
#include "HAL/IConsoleManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationProtocolManager.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Containers/StringFwd.h"

namespace UE::Net::IrisDebugHelper
{

namespace IrisDebugHelperInternal
{
	// -------------------------------------------------------------------------
	// 匿名 namespace 内部的全局"断点过滤器"变量集合。
	// 这些变量被 BreakOn* 系列函数读取：当 Iris 框架在关键节点调用
	// BreakOn*，若传入对象/RPC/Handle/Index 与这里设置的值匹配，则
	// UE_DEBUG_BREAK 触发中断。
	// 修改方式：
	//   - 通过 CVar / 控制台命令（玩家或脚本触发）；
	//   - 通过 Set* extern "C" 函数（调试器 Watch 窗口触发）。
	// -------------------------------------------------------------------------

	/** 对象名模糊匹配用：GetNameSafe(Obj).Contains(GIrisDebugName) 命中即断。 */
	static FString GIrisDebugName;
	/**
	 * CVar：Net.Iris.DebugName
	 * - 用途：运行期设置"对象名包含某子串就断点"；
	 * - 默认：空（不启用）；
	 * - 修改影响：下次 BreakOnObjectName(Object) 调用时命中新规则，立即生效；
	 * - 典型场景：`Net.Iris.DebugName MyPickupActor` → 所有名字含 "MyPickupActor"
	 *   的对象在 Iris 主流程中"关键点"触发 DEBUG BREAK，便于在未知调用栈的情况下
	 *   定位问题对象。
	 */
	FAutoConsoleVariableRef NetIrisDebugName(
		TEXT("Net.Iris.DebugName"),
		GIrisDebugName,
		TEXT("Set a class name or object name to break on."),
		ECVF_Default);

	/** RPC 名模糊匹配用：RPCName.Contains(GIrisDebugRPCName) 命中即断。 */
	static FString GIrisDebugRPCName;
	/**
	 * CVar：Net.Iris.DebugRPCName
	 * - 用途：运行期设置"RPC 名字包含某子串就断点"；
	 * - 默认：空；
	 * - 修改影响：下次 BreakOnRPCName(FName) 调用时命中；
	 * - 典型场景：`Net.Iris.DebugRPCName ServerFire` → 调用所有名字包含
	 *   "ServerFire" 的 RPC 时停下来，观察参数与上下文。
	 */
	FAutoConsoleVariableRef NetRPCSetDebugRPCName(
		TEXT("Net.Iris.DebugRPCName"),
		GIrisDebugRPCName,
		TEXT("Set the name of an RPC to break on."),
		ECVF_Default);

	/** NetRefHandle 精确匹配用：命中 GIrisDebugNetRefHandle == NetRefHandle 即断。 */
	static FNetRefHandle GIrisDebugNetRefHandle;
	/**
	 * Console Command：Net.Iris.DebugNetRefHandle <NetRefHandleId>
	 * - 参数：一个 uint32 整数，表示 NetRefHandle 的 Id 部分；
	 *         不传参数则清空设置（关闭断点）。
	 * - 用途：运行期设置"遇到某 NetRefHandle 就断点"；
	 *         Id 通常来自日志（例如 "NetRefHandle 12345"）或 Watch 窗口。
	 * - 实现：FNetRefHandleManager::MakeNetRefHandleFromId 把纯 Id 还原成
	 *   一个"不限 ReplicationSystemId"的 Handle，供 BreakOnNetRefHandle 比较。
	 * - 典型场景：日志中看到某个对象引发异常 → 取 Id → 下一轮运行时直接
	 *   `Net.Iris.DebugNetRefHandle 12345` 精确断在它被处理的地方。
	 */
	static FAutoConsoleCommand NetIrisDebugNetRefHandle(
		TEXT("Net.Iris.DebugNetRefHandle"), 
		TEXT("Specify a NetRefHandle ID that we will break on (or none to turn off)."), 
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			// 有参数：解析第一个 arg 为 uint32 并构造 Handle。
			if (Args.Num() > 0)
			{
				uint32 NetId = 0;
				LexFromString(NetId, *Args[0]);
				GIrisDebugNetRefHandle = Private::FNetRefHandleManager::MakeNetRefHandleFromId(NetId);
			}
			else
			{
				// 无参数：关闭断点（置空 Handle，IsValid 返回 false）。
				GIrisDebugNetRefHandle = FNetRefHandle();
			}
		}));

	/** InternalNetRefIndex 精确匹配用：命中即断。初始值为"无效索引"。 */
	static UE::Net::Private::FInternalNetRefIndex GIrisDebugInternalIndex = UE::Net::Private::FNetRefHandleManager::InvalidInternalIndex;
	/**
	 * Console Command：Net.Iris.DebugNetInternalIndex <InternalIndex>
	 * - 参数：一个 uint32，表示 NetRefHandleManager 内部的稠密索引；
	 *         不传参数则清空。
	 * - 用途：与 DebugNetRefHandle 类似，但用的是 Iris 内部索引——该索引直接
	 *   对应数据数组的下标，在批量遍历/日志里更紧凑。
	 * - 典型场景：Stats / Profiler 输出里看到某个 InternalIndex 异常 → 直接
	 *   `Net.Iris.DebugNetInternalIndex 42` 精确断在它被处理处。
	 */
	static FAutoConsoleCommand NetIrisDebugNetInternalIndex(
		TEXT("Net.Iris.DebugNetInternalIndex"),
		TEXT("Specify an internal index that we will break on (or none to turn off)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				// 有参数：解析为 uint32 并存入 GIrisDebugInternalIndex。
				if (Args.Num() > 0)
				{
					uint32 InternalIndex = 0;
					LexFromString(InternalIndex, *Args[0]);
					GIrisDebugInternalIndex = InternalIndex;
				}
				else
				{
					// 无参数：置 0（Iris 约定 0 为无效索引）。
					GIrisDebugInternalIndex = 0;
				}
			}));
}; // namespace IrisDebugHelperInternal

/**
 * 若 Object 的名字包含 GIrisDebugName（且 GIrisDebugName 非空），触发 UE_DEBUG_BREAK。
 *
 * 被 Iris 在"对象进入 Scope / 创建 CreationHeader / RepNotify 前"等关键节点
 * 调用（需要调用方自己插入）。返回值 true 表示确实触发了断点，便于调用方
 * 做"断点时顺便打日志"之类的配合。
 */
bool BreakOnObjectName(UObject* Object)
{
	// 分支：仅当过滤器非空且名字匹配才触发，避免默认情况下每次调用都走 Contains。
	if (IrisDebugHelperInternal::GIrisDebugName.IsEmpty() == false && GetNameSafe(Object).Contains(IrisDebugHelperInternal::GIrisDebugName))
	{
		UE_DEBUG_BREAK();
		return true;
	}

	return false;
}

/**
 * 语义与 BreakOnObjectName 不同：用于"只关心匹配对象的日志"场景——
 *   - GIrisDebugName 空 → 所有对象都算"关心"（返回 true，不过滤）；
 *   - GIrisDebugName 非空 → 只有名字包含它的对象才算"关心"。
 * 典型用法：调试日志前置 if (FilterDebuggedObject(Obj)) { UE_LOG(...); }。
 */
bool FilterDebuggedObject(UObject* Object)
{
	if (IrisDebugHelperInternal::GIrisDebugName.IsEmpty() || GetNameSafe(Object).Contains(IrisDebugHelperInternal::GIrisDebugName))
	{
		return true;
	}

	return false;
}

/**
 * 若 NetRefHandle 与 GIrisDebugNetRefHandle 相等，触发断点。
 * 使用前置条件：GIrisDebugNetRefHandle.IsValid()（由 Set*/命令 赋值过）。
 */
bool BreakOnNetRefHandle(FNetRefHandle NetRefHandle)
{
	if (IrisDebugHelperInternal::GIrisDebugNetRefHandle.IsValid() && IrisDebugHelperInternal::GIrisDebugNetRefHandle == NetRefHandle)
	{
		UE_DEBUG_BREAK();
		return true;
	}

	return false;
}

/**
 * 若 RPCName.GetPlainNameString() 包含 GIrisDebugRPCName 子串，触发断点。
 * 允许模糊匹配：例如 GIrisDebugRPCName = "ServerFire" 会命中
 * "ServerFire"、"ServerFireWithLoc"、"ClientServerFireAck" 等。
 */
bool BreakOnRPCName(FName RPCName)
{
	if (IrisDebugHelperInternal::GIrisDebugRPCName.IsEmpty() == false && RPCName.GetPlainNameString().Contains(IrisDebugHelperInternal::GIrisDebugRPCName))
	{
		UE_DEBUG_BREAK();
		return true;
	}

	return false;
}


/**
 * 若 InternalIndex 等于 GIrisDebugInternalIndex（且后者非 0），触发断点。
 * 0 被视为"未设置"的 sentinel——这与 Iris 内部约定的"无效 InternalIndex"一致。
 */
bool BreakOnInternalNetRefIndex(UE::Net::Private::FInternalNetRefIndex InternalIndex)
{
	if (IrisDebugHelperInternal::GIrisDebugInternalIndex != 0 && IrisDebugHelperInternal::GIrisDebugInternalIndex == InternalIndex)
	{
		UE_DEBUG_BREAK();
		return true;
	}

	return false;
}

/** 读取当前设置的 InternalIndex 断点值（供调试器或上层查询）。 */
UE::Net::Private::FInternalNetRefIndex GetDebugInternalNetRefIndex()
{
	return IrisDebugHelperInternal::GIrisDebugInternalIndex;
}

/** 读取当前设置的 NetRefHandle 断点值。 */
FNetRefHandle GetDebugNetRefHandle()
{
	return IrisDebugHelperInternal::GIrisDebugNetRefHandle;
}

/**
 * 设置对象名断点（extern "C"，可在 Watch 窗口调用，等价于 Net.Iris.DebugName CVar 赋值）。
 * @param NameBuffer ANSI 字符串；传 nullptr 表示清空。
 */
void SetIrisDebugObjectName(const ANSICHAR* NameBuffer)
{
	if (NameBuffer)
	{
		IrisDebugHelperInternal::GIrisDebugName = ANSI_TO_TCHAR(NameBuffer);
	}
	else
	{
		IrisDebugHelperInternal::GIrisDebugName = FString();
	}
}

/**
 * 设置 NetRefHandle 断点（extern "C"，等价于 Net.Iris.DebugNetRefHandle <Id> 命令）。
 * 输入是 64-bit 的 Handle.Id，内部用 MakeNetRefHandleFromId 构造一个"仅 Id"
 * 的 Handle（ReplicationSystemId 不限），即跨 PIE 多实例都会命中同 Id 对象。
 */
void SetIrisDebugNetRefHandle(uint64 NetRefHandleId)
{
	IrisDebugHelperInternal::GIrisDebugNetRefHandle = Private::FNetRefHandleManager::MakeNetRefHandleFromId(NetRefHandleId);
}

/**
 * 设置 RPC 名断点（extern "C"，等价于 Net.Iris.DebugRPCName CVar 赋值）。
 * @param NameBuffer ANSI 子串；nullptr 表示清空。
 */
void SetIrisDebugRPCName(const ANSICHAR* NameBuffer)
{
	if (NameBuffer)
	{
		IrisDebugHelperInternal::GIrisDebugRPCName = ANSI_TO_TCHAR(NameBuffer);
	}
	else
	{
		IrisDebugHelperInternal::GIrisDebugRPCName = FString();
	}
}

/**
 * 直接设置 InternalIndex 断点（extern "C"，等价于 Net.Iris.DebugNetInternalIndex 命令）。
 * 调用方需保证 InternalIndex 是当前 RS 中有效的索引，否则永远不会命中。
 */
void SetIrisDebugInternalNetRefIndex(UE::Net::Private::FInternalNetRefIndex InternalIndex)
{
	IrisDebugHelperInternal::GIrisDebugInternalIndex = InternalIndex;
}

/**
 * 便捷工具：给定一个 FNetRefHandle，自动把它所在 RS 里的 InternalIndex 反查
 * 出来，并同时设置 NetRefHandle 和 InternalIndex 两个断点。
 *
 * 执行路径：
 *   Handle  → 按 Handle.GetReplicationSystemId() 拿到对应 UReplicationSystem
 *   RS      → GetReplicationSystemInternal() 获取内部聚合根
 *   Internal→ GetNetRefHandleManager() 取 NetRefHandleManager
 *   Mgr     → GetInternalIndex(Handle) 反查 InternalIndex
 *   写回 GIrisDebugNetRefHandle / GIrisDebugInternalIndex。
 *
 * 边界：
 *   - Handle 无效：清空两个断点并返回；
 *   - 找不到 RS：不修改任何设置（典型为 RS 已销毁场景）；
 *   - InternalIndex 为 0：代表 Handle 未注册到 RS（Bridge 还没收到），不设置。
 */
void SetIrisDebugInternalNetRefIndexViaNetHandle(FNetRefHandle RefHandle)
{
	using namespace UE::Net::Private;

	// 无效 Handle：视为"清空断点"。
	if (!RefHandle.IsValid())
	{
		IrisDebugHelperInternal::GIrisDebugNetRefHandle = RefHandle;
		IrisDebugHelperInternal::GIrisDebugInternalIndex = FNetRefHandleManager::InvalidInternalIndex;
		return;
	}

	// 按 RSId 查 RS——Handle 中 10-bit RSId 段用于多 PIE 实例区分。
	UReplicationSystem* ReplicationSystem = GetReplicationSystem(RefHandle.GetReplicationSystemId());
	if (!ReplicationSystem)
	{
		return;
	}

	// 下钻到内部聚合：RS → Internal → NetRefHandleManager。
	FReplicationSystemInternal* ReplicationSystemInternal = ReplicationSystem->GetReplicationSystemInternal();
	const FNetRefHandleManager& NetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	// Handle → InternalIndex：若对象还未注册，返回 0（InvalidInternalIndex）。
	const FInternalNetRefIndex InternalNetRefIndex = NetRefHandleManager.GetInternalIndex(RefHandle);
	if (!InternalNetRefIndex)
	{
		return;
	}

	// 同时设置两处断点，方便无论上层用 Handle 还是 InternalIndex 都能命中。
	IrisDebugHelperInternal::GIrisDebugNetRefHandle = RefHandle;
	IrisDebugHelperInternal::GIrisDebugInternalIndex = InternalNetRefIndex;
}

/**
 * 给定一个 UObject 实例，自动扫描所有活跃 ReplicationSystem，找到它正在被哪套
 * RS 复制，并设置断点。
 *
 * 扫描策略：遍历 FReplicationSystemFactory::GetAllReplicationSystems() 返回
 * 的全部 RS，对每套 RS 拿 UObjectReplicationBridge → GetReplicatedRefHandle
 * 看是否持有该对象；命中第一个就停。
 *
 * 适用场景：调试器 Watch 窗口里拿到了 UObject* 但不知道对应 NetRefHandle，
 * 直接调用本函数一步设置断点。
 */
void SetIrisDebugInternalNetRefIndexViaObject(UObject* Instance)
{
	using namespace UE::Net::Private;

	// 空指针：清空断点，直接返回。
	if (!Instance)
	{
		IrisDebugHelperInternal::GIrisDebugNetRefHandle = FNetRefHandle();
		IrisDebugHelperInternal::GIrisDebugInternalIndex = FNetRefHandleManager::InvalidInternalIndex;
		return;
	}

	// See if we can find the instance in any replication system
	// 遍历所有 RS（PIE 多客户端/服务器时会有多个），找到第一个在复制该对象的。
	for (const UReplicationSystem* ReplicationSystem : FReplicationSystemFactory::GetAllReplicationSystems())
	{
		if (ReplicationSystem)
		{
			// 要求 Bridge 是 UObjectReplicationBridge；Iris 游戏侧标准 Bridge。
			if (const UObjectReplicationBridge* Bridge = ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>())
			{
				FNetRefHandle Handle = Bridge->GetReplicatedRefHandle(Instance);
				if (Handle.IsValid())
				{
					// 命中：反查 InternalIndex 并写入断点变量后 break 出循环。
					const FNetRefHandleManager& NetRefHandleManager = ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
					FInternalNetRefIndex InternalIndex = NetRefHandleManager.GetInternalIndex(Handle);

					IrisDebugHelperInternal::GIrisDebugNetRefHandle = Handle;
					IrisDebugHelperInternal::GIrisDebugInternalIndex = InternalIndex;
					break;
				}
			}
		}
	}
}


/**
 * 根据 ReplicationSystemId 返回对应 UReplicationSystem。
 * 存在意义：仅把 UE::Net::GetReplicationSystem(Id) 暴露为 extern "C" 符号，
 * 供 VS Watch 窗口直接调用（C++ mangled 名字在 Watch 里不好写）。
 */
UReplicationSystem* GetReplicationSystemForDebug(uint32 Id)
{
	return GetReplicationSystem(Id);
}

// 宏：把函数地址累加到 64-bit 变量里，防止 Linker 裁剪掉未被业务代码引用的
// Debug* 函数（这些函数只在调试器 Watch 窗口中被间接调用，链接器看不到
// "使用者"会当成死代码剥离）。
#define UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(FunctionName) FunctionReferenceAccumulator += uint64(&FunctionName);

/**
 * 防剥离桩函数：把所有 debug 导出符号的地址相加后返回。
 *
 * 该函数会被 Iris 模块的某处（或 Public header 中的一个 force-link 钩子）调用
 * 一次——目的不是算什么有意义的值，而是让编译器/链接器认为这些函数被
 * "使用"了，从而阻止 LTO / --gc-sections 把它们剥离。
 *
 * 注意：返回值 uint64 会被调用方忽略；此处只是副作用。
 */
uint64 Init()
{
	uint64 FunctionReferenceAccumulator = uint64(0);

	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(GetReplicationSystemForDebug);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugOutputNetObjectState);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugNetObjectStateToString);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugOutputNetObjectProtocolReferences);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugNetObject);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugNetObjectById);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugNetRefHandle);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugNetRefHandleById);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugInternalNetRefIndex);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(DebugNetObjectProtocolReferencesToString);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(SetIrisDebugObjectName);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(SetIrisDebugNetRefHandle);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(SetIrisDebugInternalNetRefIndex);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(SetIrisDebugInternalNetRefIndexViaNetHandle);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(SetIrisDebugInternalNetRefIndexViaObject);
	UE_NET_FORCE_REFERENCE_DEBUGFUNCTION(SetIrisDebugRPCName);
	
	return FunctionReferenceAccumulator;
}

#undef UE_NET_FORCE_REFERENCE_DEBUGFUNCTION

/**
 * 把某 NetRefHandle 当前的完整复制状态打印到 StringBuilder。
 *
 * 执行路径（从 Handle 到状态缓冲）：
 *   1) Handle.GetReplicationSystemId() → GetReplicationSystem() 拿 RS；
 *   2) RS → ReplicationSystemInternal → NetRefHandleManager；
 *   3) NetRefHandleManager.GetInternalIndex(Handle) → InternalIndex；
 *   4) ReplicatedObjectDataNoCheck(InternalIndex) 取 FReplicatedObjectData；
 *   5) 依据 bIsServer 选 SendState（服务端的 QuantizedStateBuffer）或
 *      ReceiveStateBuffer（客户端）；
 *   6) 读 Protocol / InstanceProtocol；
 *   7) 若要正确 dequantize 含对象引用的字段，需要一个 RemoteTokenStoreState——
 *      服务端直接用本地；客户端则挑一条"第一个有效连接"作为远端 TokenStore 源；
 *   8) 构造 FInternalNetSerializationContext + FNetSerializationContext；
 *   9) 调用 FReplicationInstanceOperations::OutputInternalStateToString 做实际
 *      遍历各成员 → 调各自 Serializer 的 PrintQuantized → 拼接字符串。
 *
 * 所有"找不到"情形（Handle 无效 / RS 不存在 / InternalIndex 为 0 / Protocol
 * 或 StateBuffer 为空）都静默 return，不打印任何内容——符合"调试辅助"的
 * 容错要求。
 */
void NetObjectStateToString(FStringBuilderBase& StringBuilder, FNetRefHandle RefHandle)
{
	using namespace UE::Net::Private;

	// 1) Handle 合法性门槛。
	if (!RefHandle.IsValid())
	{
		return;
	}

	// 2) 反查 RS；PIE 多实例时通过 Handle 自带的 RSId 精确命中。
	UReplicationSystem* ReplicationSystem = GetReplicationSystem(RefHandle.GetReplicationSystemId());
	if (!ReplicationSystem)
	{
		return;
	}

	// 3) 下钻 → NetRefHandleManager → InternalIndex。
	FReplicationSystemInternal* ReplicationSystemInternal = ReplicationSystem->GetReplicationSystemInternal();
	const FNetRefHandleManager& NetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();

	const FInternalNetRefIndex InternalNetRefIndex = NetRefHandleManager.GetInternalIndex(RefHandle);
	if (!InternalNetRefIndex)
	{
		return;
	}

	// 4) 选择正确的状态缓冲源：
	//    - 服务端：对应的 QuantizedStateBuffer（待发送）；
	//    - 客户端：ReceiveStateBuffer（从网络接收并反量化后的）。
	const bool bIsServer = ReplicationSystem->IsServer();
	const FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalNetRefIndex);	
	const uint8* InternalStateBuffer = bIsServer ? NetRefHandleManager.GetReplicatedObjectStateBufferNoCheck(InternalNetRefIndex) : ReplicatedObjectData.ReceiveStateBuffer;
	// 若 InstanceProtocol 或 StateBuffer 缺一不可：对象尚未 Quantize 或已 TearOff。
	if (ReplicatedObjectData.InstanceProtocol == nullptr || InternalStateBuffer == nullptr)
	{
		return;
	}

	const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;

	// In order to be able to output object references we need the TokenStoreState, for the server we just use the local one but if we are a client we must use the remote token store state
	// If this is a client handle we assume that we only have a single connections and use the first valid connection to get the remote token store.
	// 5) 为了在打印"对象引用"字段时能解析出名字，需要一个 NetTokenStoreState。
	//    服务端用本地即可；客户端通常只有一条连接，直接挑第一条有效连接作为 Token 源。
	FReplicationConnections& Connections = ReplicationSystemInternal->GetConnections();
	const uint32 FirstValidConnectionId = Connections.GetValidConnections().FindFirstOne();

	// Setup Context
	// 6) 构造 FInternalNetSerializationContext，注入 RS / PackageMap / RemoteTokenStore。
	FInternalNetSerializationContext InternalContext;
	FInternalNetSerializationContext::FInitParameters InternalContextInitParams;
	InternalContextInitParams.ReplicationSystem = ReplicationSystem;
	InternalContextInitParams.PackageMap = ReplicationSystemInternal->GetIrisObjectReferencePackageMap();
	// 若找不到任何有效连接（服务端无客户端 / 客户端未握手）：RemoteNetTokenStoreState 置 null。
	InternalContextInitParams.ObjectResolveContext.RemoteNetTokenStoreState = FirstValidConnectionId == FNetBitArray::InvalidIndex ? nullptr :ReplicationSystem->GetNetTokenStore()->GetRemoteNetTokenStoreState(FirstValidConnectionId);
	InternalContextInitParams.ObjectResolveContext.ConnectionId = (FirstValidConnectionId == FNetBitArray::InvalidIndex ? InvalidConnectionId : FirstValidConnectionId);
	InternalContext.Init(InternalContextInitParams);

	// 7) 顶层 NetSerializationContext 挂上 InternalContext，供 Serializer 拿对象引用解析能力。
	FNetSerializationContext NetSerializationContext;
	NetSerializationContext.SetInternalContext(&InternalContext);
	NetSerializationContext.SetLocalConnectionId(InternalContextInitParams.ObjectResolveContext.ConnectionId);

	// 8) 打印头部："State for <RefHandle> of type <Protocol Name>\n"。
	StringBuilder << TEXT("State for ") << RefHandle;
	StringBuilder.Appendf(TEXT(" of type %s\n"), Protocol->DebugName->Name);

	// 9) 委托 FReplicationInstanceOperations 递归遍历 Descriptor / Protocol，
	//    对每个字段调用其 Serializer 的 PrintQuantized 拼字符串。
	FReplicationInstanceOperations::OutputInternalStateToString(NetSerializationContext, StringBuilder, nullptr, InternalStateBuffer, ReplicatedObjectData.InstanceProtocol, ReplicatedObjectData.Protocol);
}

/**
 * extern "C" 版本：把 (NetRefHandleId, ReplicationSystemId) 组合成 Handle，
 * 调 NetObjectStateToString 打印后，通过 FPlatformMisc::LowLevelOutputDebugString
 * 输出到 DebugOutput 窗口（VS Output / Xcode Debugger Output）。
 *
 * 使用场景：在 VS Watch 窗口里输入
 *   DebugOutputNetObjectState(12345ull, 0u)
 * 即可立刻在 Output 面板看到该对象当前状态——无需在代码里加 UE_LOG。
 */
void DebugOutputNetObjectState(uint64 NetRefHandleId, uint32 ReplicationSystemId)
{
	const FNetRefHandle RefHandle = Private::FNetRefHandleManager::MakeNetRefHandle(NetRefHandleId, ReplicationSystemId);

	// 4KB 栈上 StringBuilder：绝大多数对象状态可以一次装下。
	TStringBuilder<4096> StringBuilder;

	NetObjectStateToString(StringBuilder, RefHandle);

	// 直接输出到调试器；不走 UE_LOG 以便在 UE 日志被 mute 时也能看到。
	FPlatformMisc::LowLevelOutputDebugString(StringBuilder.ToString());
}

/**
 * extern "C" 版本：返回字符串指针供 Watch 窗口直接查看。
 *
 * 警告：为了能返回指针，内部用 static TStringBuilder。后果：
 *   - 非线程安全；多线程并发调用数据会互相覆盖；
 *   - 同一线程内再次调用会让上一次返回的指针失效。
 * 仅用于调试器 Watch/Immediate Window 的"单步查看一次"场景。
 */
const TCHAR* DebugNetObjectStateToString(uint32 NetRefHandleId, uint32 ReplicationSystemId)
{
	static TStringBuilder<4096> StringBuilder;

	// Reset 清空上一次内容，避免拼接到旧数据后面。
	StringBuilder.Reset();
	const FNetRefHandle RefHandle = Private::FNetRefHandleManager::MakeNetRefHandle(NetRefHandleId, ReplicationSystemId);
	NetObjectStateToString(StringBuilder, RefHandle);

	return StringBuilder.ToString();
}

/**
 * 从 UObject* 反查它在"任一 RS"中的复制信息。
 *
 * 扫描策略：FReplicationSystemFactory::GetAllReplicationSystems() 遍历所有活跃
 * RS，尝试在每套 RS 的 UObjectReplicationBridge 里查询 RefHandle；命中第一个
 * 即填充 FNetReplicatedObjectDebugInfo 返回，不继续查找后续 RS。
 *
 * 返回值：成员全部为空/0 表示"未找到"。
 */
FNetReplicatedObjectDebugInfo DebugNetObject(UObject* Instance)
{
	using namespace UE::Net::Private;
	FNetReplicatedObjectDebugInfo Info = {};

	// See if we can find the instance in any replication system
	// 循环：依次查每个 RS；命中则填 Info 并直接 return。
	for (const UReplicationSystem* ReplicationSystem : FReplicationSystemFactory::GetAllReplicationSystems())
	{
		if (ReplicationSystem)
		{
			// 必须是 UObjectReplicationBridge 子类（Iris 标准 game-side Bridge）才支持按 UObject* 查。
			if (const UObjectReplicationBridge* Bridge = ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>())
			{
				FNetRefHandle Handle = Bridge->GetReplicatedRefHandle(Instance);
				if (Handle.IsValid())
				{
					// 拿 InternalIndex 和 ObjectData 用来填充各字段。
					const FNetRefHandleManager& NetRefHandleManager = ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
					Info.InternalNetRefIndex = NetRefHandleManager.GetInternalIndex(Handle);

					const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(Info.InternalNetRefIndex);				

					Info.RefHandle = &ObjectData.RefHandle;
					Info.Protocol = ObjectData.Protocol;
					Info.InstanceProtocol = ObjectData.InstanceProtocol;
					Info.ReplicationSystem = ReplicationSystem;
					Info.Object = NetRefHandleManager.GetReplicatedInstances()[Info.InternalNetRefIndex];

					return Info;
				}
			}
		}
	}

	return Info;
}

/**
 * DebugNetObject 的"限定 RS"变体：只在指定 ReplicationSystemId 中查找。
 * 用在 PIE 多实例、想定位到具体某一路（例如只看服务器 RS）。
 */
FNetReplicatedObjectDebugInfo DebugNetObjectById(UObject* Instance, uint32 ReplicationSystemId)
{
	using namespace UE::Net::Private;
	FNetReplicatedObjectDebugInfo Info = {};

	const UReplicationSystem* ReplicationSystem = GetReplicationSystemForDebug(ReplicationSystemId);
	if (ReplicationSystem)
	{
		// 仅当 Bridge 是 UObjectReplicationBridge 才能按 UObject* 查。
		if (const UObjectReplicationBridge* Bridge = ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>())
		{
			FNetRefHandle Handle = Bridge->GetReplicatedRefHandle(Instance);
			if (Handle.IsValid())
			{
				const FNetRefHandleManager& NetRefHandleManager = ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
				Info.InternalNetRefIndex = NetRefHandleManager.GetInternalIndex(Handle);

				const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(Info.InternalNetRefIndex);

				Info.RefHandle = &ObjectData.RefHandle;
				Info.Protocol = ObjectData.Protocol;
				Info.InstanceProtocol = ObjectData.InstanceProtocol;
				Info.ReplicationSystem = ReplicationSystem;
				Info.Object = NetRefHandleManager.GetReplicatedInstances()[Info.InternalNetRefIndex];

				return Info;
			}
		}
	}

	return Info;
}

/**
 * 已有完整 Handle 时的便捷入口：拆出 Id 和 RSId 转发给 DebugNetRefHandleById。
 */
FNetReplicatedObjectDebugInfo DebugNetRefHandle(FNetRefHandle Handle)
{
	return DebugNetRefHandleById(Handle.GetId(), Handle.GetReplicationSystemId());
}

/**
 * 按 (NetRefHandleId, ReplicationSystemId) 查复制信息。
 *
 * 与 DebugNetObjectById 的区别：不需要 UObject*（调试时可能对象已被 GC 或
 * 还未创建），直接通过内部索引表查找。
 *
 * 校验：
 *   - InternalIndex == InvalidInternalIndex → 未注册；
 *   - ObjectData.RefHandle == IncompleteHandle → 验证是同一 Handle（防止 Id
 *     复用导致的误命中；Iris Handle 生成策略保证 Id 不会立即复用，但保险起见
 *     仍做等值校验）。
 */
FNetReplicatedObjectDebugInfo DebugNetRefHandleById(uint64 NetRefHandleId, uint32 ReplicationSystemId)
{
	using namespace UE::Net::Private;
	FNetReplicatedObjectDebugInfo Info = {};

	// See if we can find the instance in any replication system
	{
		const UReplicationSystem* ReplicationSystem = GetReplicationSystemForDebug(ReplicationSystemId);
		if (ReplicationSystem)
		{
			const FNetRefHandleManager& NetRefHandleManager = ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
			// 用 Id+RSId 构造一个"不完整 Handle"（不含内部生成计数），再让 Manager 反查内部索引。
			FNetRefHandle IncompleteHandle = FNetRefHandleManager::MakeNetRefHandle(NetRefHandleId, ReplicationSystemId);
			const uint32 InternalNetRefIndex = Info.InternalNetRefIndex = NetRefHandleManager.GetInternalIndex(IncompleteHandle);
			if (InternalNetRefIndex != FNetRefHandleManager::InvalidInternalIndex)
			{
				const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalNetRefIndex);
				// 分支：确认这个 InternalIndex 下存储的 Handle 确实就是查询 Handle（防止 Id 循环复用时误判）。
				if (ObjectData.RefHandle == IncompleteHandle)
				{
					Info.RefHandle = &ObjectData.RefHandle;
					Info.InternalNetRefIndex = InternalNetRefIndex;
					Info.Protocol = ObjectData.Protocol;
					Info.InstanceProtocol = ObjectData.InstanceProtocol;
					Info.ReplicationSystem = ReplicationSystem;
					Info.Object = NetRefHandleManager.GetReplicatedInstances()[InternalNetRefIndex];

					return Info;
				}
			}
		}
	}

	return Info;
}

/**
 * 按 InternalIndex 直接查：跳过 Handle 反查，更快。
 *
 * 流程：
 *   1) 按 RSId 查 RS；
 *   2) InternalIndex 合法性检查（非 Invalid）；
 *   3) 通过 GetNetRefHandleFromInternalIndex 反推 Handle；
 *   4) ObjectData.RefHandle == Handle 验证自洽；
 *   5) 填充 Info 返回。
 */
FNetReplicatedObjectDebugInfo DebugInternalNetRefIndex(uint32 InternalIndex, uint32 ReplicationSystemId)
{
	using namespace UE::Net::Private;
	FNetReplicatedObjectDebugInfo Info = {};

	// See if we can find the instance in any replication system
	{
		const UReplicationSystem* ReplicationSystem = GetReplicationSystemForDebug(ReplicationSystemId);
		if (ReplicationSystem)
		{
			const FNetRefHandleManager& NetRefHandleManager = ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
			if (InternalIndex != FNetRefHandleManager::InvalidInternalIndex)
			{
				// 从索引反推完整 Handle（包含正确的 RSId / 生成计数）。
				FNetRefHandle ObjectHandle = NetRefHandleManager.GetNetRefHandleFromInternalIndex(InternalIndex);
				const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalIndex);
				// 自洽校验：ObjectData 记录的 Handle 与反推 Handle 一致则是有效 entry。
				if (ObjectData.RefHandle == ObjectHandle)
				{
					Info.RefHandle = &ObjectData.RefHandle;
					Info.InternalNetRefIndex = InternalIndex;
					Info.Protocol = ObjectData.Protocol;
					Info.InstanceProtocol = ObjectData.InstanceProtocol;
					Info.ReplicationSystem = ReplicationSystem;
					Info.Object = NetRefHandleManager.GetReplicatedInstances()[InternalIndex];

					return Info;
				}
			}
		}
	}

	return Info;
}

/**
 * 把某个 ReplicationProtocol（按 ProtocolId 查）当前被哪些对象使用，全部列出。
 *
 * 场景：Iris 的 FReplicationProtocol 按 Shape 共享（ProtocolIdentifier 相同 = 形态相同）；
 * 调试 protocol 兼容性 / 形状冲突 / 内存占用时，需要看某个 Protocol 具体有多少
 * 个 NetRefHandle 在引用。
 *
 * 实现：
 *   - ProtocolManager.ForEachProtocol(ProtocolId, Fn) 遍历所有匹配 Id 的 protocol
 *     （Iris 允许"同 Id 多 Protocol"的情况，ForEach 提供这种枚举）；
 *   - 内层用 GetAssignedInternalIndices().ForAllSetBits 扫描所有已注册 Handle，
 *     把 ObjectData.Protocol 与当前 Protocol 相等者打印。
 *
 * 结果格式示例：
 *   Protocol: FooActor Id: 0x1234 Created From : 0xABCD1234 Used by :
 *   NetRefHandle:12345 ( InternalIndex: 42, )
 *   NetRefHandle:12346 ( InternalIndex: 43, )
 */
void NetObjectProtocolReferencesToString(FStringBuilderBase& StringBuilder, FReplicationProtocolIdentifier ProtocolId, uint32 ReplicationSystemId)
{
	using namespace UE::Net::Private;

	UReplicationSystem* ReplicationSystem = GetReplicationSystem(ReplicationSystemId);
	if (!ReplicationSystem)
	{
		return;
	}

	FReplicationSystemInternal* ReplicationSystemInternal = ReplicationSystem->GetReplicationSystemInternal();
	const FNetRefHandleManager& NetRefHandleManager = ReplicationSystemInternal->GetNetRefHandleManager();
	const FReplicationProtocolManager& ProtocolManager = ReplicationSystemInternal->GetReplicationProtocolManager();

	// As there might be multiple protocols sharing the same identifier
	// 外层循环：同一 ProtocolId 可能对应多个实际 Protocol（例如不同 UClass 模板），全部遍历。
	ProtocolManager.ForEachProtocol(ProtocolId, [&StringBuilder, &NetRefHandleManager](const FReplicationProtocol* Protocol, const FObjectKey TemplateKey)
		{
			// 打印 protocol 头：名字 / Id / 创建它所用模板对象 / 后面列使用者列表。
			StringBuilder << TEXT("Protocol: ") << ToCStr(Protocol->DebugName);
			StringBuilder.Appendf(TEXT("Id: 0x%x Created From : 0x%p"), Protocol->ProtocolIdentifier, TemplateKey.ResolveObjectPtrEvenIfGarbage()) << TEXT(" Used by : \n");

			// 匿名函数：每遍到一个已注册对象，若其 Protocol 与当前 Protocol 一致即打印。
			auto FindMatchingProtocol = [&StringBuilder, &Protocol, &NetRefHandleManager](uint32 InternalObjectIndex)
			{
				const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalObjectIndex);
				if (ObjectData.Protocol == Protocol)
				{
					StringBuilder << ObjectData.RefHandle << TEXT(" ( InternalIndex: ") << InternalObjectIndex << TEXT(", )\n");
				}
			};
			
			// 内层循环：扫描 AssignedInternalIndices 位图上所有置位的位——对应全部
			// 已注册对象的 InternalIndex。对每个索引执行 FindMatchingProtocol。
			// 终止条件：位图所有 set bit 都处理完毕。
			NetRefHandleManager.GetAssignedInternalIndices().ForAllSetBits(FindMatchingProtocol);
		}
	);
}

/**
 * extern "C" 版本：把 NetObjectProtocolReferencesToString 的结果输出到
 * DebugOutput 窗口（同 DebugOutputNetObjectState 的输出机制）。
 */
void DebugOutputNetObjectProtocolReferences(FReplicationProtocolIdentifier ProtocolId, uint32 ReplicationSystemId)
{
	TStringBuilder<4096> StringBuilder;
	StringBuilder.Reset();

	NetObjectProtocolReferencesToString(StringBuilder, ProtocolId, ReplicationSystemId);

	FPlatformMisc::LowLevelOutputDebugString(StringBuilder.ToString());
}

/**
 * extern "C" 版本：返回共享 static buffer 的 TCHAR*，供 Watch 窗口一次性查看。
 * 同样只在调试期可用；非线程安全。
 */
const TCHAR* DebugNetObjectProtocolReferencesToString(FReplicationProtocolIdentifier ProtocolId, uint32 ReplicationSystemId)
{
	static TStringBuilder<4096> StringBuilder;
	StringBuilder.Reset();

	NetObjectProtocolReferencesToString(StringBuilder, ProtocolId, ReplicationSystemId);

	return StringBuilder.ToString();
}

} // End of namespaces

#endif
