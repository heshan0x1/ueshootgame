// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Net/Core/NetHandle/NetHandle.h"

// ============================================================================
// IrisDebugging —— 调试器（Visual Studio watch/immediate 窗口）友好的 helper
// ----------------------------------------------------------------------------
// 目的：
//   Iris 的运行时状态分散在多个子系统中（NetRefHandleManager、Protocol、
//   InstanceProtocol、Bridge、ObjectReferenceCache 等），用普通
//   `p/print` 很难快速拉出"某个 NetRefHandle 当前的状态、protocol、所属
//   ReplicationSystem"。该头文件为调试场景提供：
//
//   1) 一组 `extern "C"` 接口：C 链接符号、无 C++ 名字修饰，
//      可在 VS **Watch 窗口 / Immediate 窗口**里直接敲函数名调用，比如：
//          DebugNetObjectStateToString(0x1234, 0)
//          DebugOutputNetObjectState(0x1234, 0)
//          SetIrisDebugNetRefHandle(0x1234)
//
//   2) 一批 C++ 命名空间内的 helper（BreakOnXxx / FilterDebuggedObject /
//      NetObjectStateToString）供 Iris 内部"遇到目标 Handle/Name/RPC 则断点"
//      使用。它们会读取由 `Set...`/CVar 设置的"当前调试关注目标"，从而让
//      你把一条"长时间运行的连接"精确断到某个对象上。
//
//   3) 对应的 CVar（在 .cpp 注册）：
//      - Net.Iris.DebugName              —— 关注对象名字包含该子串
//      - Net.Iris.DebugRPCName           —— 关注 RPC 名字包含该子串
//      - Net.Iris.DebugNetRefHandle      —— 关注的 NetRefHandle id
//      - Net.Iris.DebugNetInternalIndex  —— 关注的内部索引
//
//   条件编译：整个命名空间只在 **非 Shipping** 构建下编译——Shipping 里
//   调试 helper 不存在，避免符号泄漏与代码膨胀。调用处必须自行 `#if !UE_BUILD_SHIPPING`
//   保护，或走上层抽象（FilterDebuggedObject 为主要入口）。
//
// 提醒：`extern "C"` 变体的字符串输出函数使用 **静态缓冲**，非线程安全，仅
//       用于交互式调试一次一次地"按 F5 看"，不应被游戏逻辑调用。
// ============================================================================

class UReplicationSystem;
namespace UE::Net
{
	// 前向声明：避免把 ReplicationProtocol.h 引入 Core 头。
	struct FReplicationProtocol;
	struct FReplicationInstanceProtocol;

	typedef uint32 FReplicationProtocolIdentifier;
}

namespace UE::Net::Private
{
	// 内部类型别名：NetRefHandle 在表中的连续索引（32 位）。
	typedef uint32 FInternalNetRefIndex;
}

#if !UE_BUILD_SHIPPING

namespace UE::Net::IrisDebugHelper
{

/**
 * Dummy methods binding pointers to external methods to avoid them from being stripped by compiler
 *
 * 占位初始化：在模块启动时调用，引用下面的 `extern "C"` 符号防止链接器
 * 把它们当成"未使用的导出函数"裁掉。返回值仅用于阻止 DCE（dead code elimination）。
 */
IRISCORE_API uint64 Init();

/**
 * Trigger a breakpoint and return true if the object contains the current debug name
 *
 * 如果 Object 的名字包含由 `Net.Iris.DebugName` 设置的子串，则立刻触发断点
 * 并返回 true；否则返回 false。用于在循环中"筛出"关注对象时中断。
 */
IRISCORE_API bool BreakOnObjectName(UObject* Object);

/**
 * Trigger a breakpoint and return true if the NetRefHandle is the current debug NetRefHandle
 *
 * 命中 `Net.Iris.DebugNetRefHandle` 指定 handle 时触发断点。
 */
IRISCORE_API bool BreakOnNetRefHandle(FNetRefHandle NetRefHandle);

/**
 * Trigger a breakpoint and return true if the name contains the debug RPC string
 *
 * 命中 `Net.Iris.DebugRPCName` 关键字时触发断点。
 */
IRISCORE_API bool BreakOnRPCName(FName RPCName);

/**
 * Trigger a breakpoint and return true if the index is the current debug index
 *
 * 命中 `Net.Iris.DebugNetInternalIndex` 时触发断点。
 */
IRISCORE_API bool BreakOnInternalNetRefIndex(UE::Net::Private::FInternalNetRefIndex InternalIndex);

/**
 * Returns true if the object name contains the current debug name, will return true if no debug name is set
 *
 * "过滤器"：若 CVar 未设置（空字符串），返回 true（不过滤）；否则按名字
 * 子串匹配。用于在日志输出前判断是否需要打印。
 */
IRISCORE_API bool FilterDebuggedObject(UObject* Object);

/** Returns the internal index set via SetIrisDebugInternalIndex */
/** 读取当前"关注的 InternalNetRefIndex"，由 SetIrisDebug* 设置。 */
IRISCORE_API UE::Net::Private::FInternalNetRefIndex GetDebugNetInternalIndex();

/** Returns the NetRefHandle set via SetIrisDebugNetRefHandle */
/** 读取当前"关注的 NetRefHandle"。 */
IRISCORE_API FNetRefHandle GetDebugNetRefHandle();

/**
 * Output state data to StringBuilder for the specified Handle
 *
 * 将指定 Handle 的当前状态（protocol / instance / 字段值）格式化到 StringBuilder。
 * 线程不安全的外部版本见下方 `extern "C"`。
 */
void NetObjectStateToString(FStringBuilderBase& StringBuilder, FNetRefHandle RefHandle);

/**
 * Find all handles references registered for a protocol and output to StringBuilder
 *
 * 按 ProtocolId + ReplicationSystemId 列出"所有注册了该协议的 Handle 引用"。
 */
void NetObjectProtocolReferencesToString(FStringBuilderBase& StringBuilder, FReplicationProtocolIdentifier ProtocolId, uint32 ReplicationSystemId);

// Helper functions for debugging state data exposed as extern "C" that are callable from immediate and watch window in the debugger
// -----------------------------------------------------------------------------
// 以下一组 `extern "C"` 函数专门为调试器 watch/immediate 窗口设计：
//   - 没有 C++ name-mangling，可以直接按函数名输入；
//   - 返回普通 POD / 指针 / 静态缓冲字符串，便于 watch 窗口展开；
//   - "Set*"系列会修改进程内的静态状态，从下一次断点开始生效，不需要重启。
// -----------------------------------------------------------------------------

/**
 * Get the ReplicationSystem with the Id
 *
 * 在 watch 窗口取指定 id 的 UReplicationSystem 实例指针，便于进一步展开。
 */
extern "C" IRISCORE_API UReplicationSystem* GetReplicationSystemForDebug(uint32 Id);

/**
 * DebugOutputObject state for Handle specified by NetRefHandleId and ReplicationSystemId.
 *
 * 将状态直接输出到调试器输出窗口（UE_LOG + GLog），不返回字符串。
 */
extern "C" IRISCORE_API void DebugOutputNetObjectState(uint64 NetRefHandleId, uint32 ReplicationSystemId);

/**
 * Variant of NetObjectStateToString that can be used from breakpoints and in watch window to print the current state of a NetRefHandle.
 * NOTE: Use only for debugging as this variant uses a static buffer which is not thread safe.
 *
 * 用于 watch 窗口的字符串返回版本：返回指向 **静态缓冲** 的 TCHAR*，
 * **非线程安全**，且下次调用会覆盖缓冲。仅供交互式查看，不可用于产品代码。
 */
 //$IRIS TODO: Make console command to log these for object X or class X
extern "C" IRISCORE_API const TCHAR* DebugNetObjectStateToString(uint32 NetRefHandleId, uint32 ReplicationSystemId);

/** Find all handles references registered for a protocol and output to DebugOutput in debugger */
/** 将指定 Protocol 的所有引用输出到调试器输出窗口。 */
extern "C" IRISCORE_API void DebugOutputNetObjectProtocolReferences(FReplicationProtocolIdentifier ProtocolId, uint32 ReplicationSystemId);

/**
 * Get info about replicated object
 *
 * 复制对象调试信息聚合结构。返回给 DebugNetObject* 一族函数，
 * 在 watch 窗口里可一次性展开：RefHandle / InternalIndex / RS / Protocol /
 * InstanceProtocol / Object pointer，极大提升定位效率。
 */
struct FNetReplicatedObjectDebugInfo
{
	const FNetRefHandle* RefHandle;                                 // 句柄指针（指向表中条目）
	UE::Net::Private::FInternalNetRefIndex InternalNetRefIndex;      // 内部连续索引
	const UReplicationSystem* ReplicationSystem;                     // 所属 RS
	const FReplicationProtocol* Protocol;                            // 形态协议（跨连接共享）
	const FReplicationInstanceProtocol* InstanceProtocol;            // 实例协议（持有 Fragment 指针）
	const UObject* Object;                                           // 对应的 UObject 实例
};

/**
 * Look up replicated handle from Instance pointer and return debug information. This variant searches all active replication systems and returns information for the first one replicating the Instance.
 *
 * 已知 UObject* 反查 NetRefHandle 信息：遍历所有活动 RS，命中即返回。PIE 多实例时请用带 Id 的重载。
 */
extern "C" IRISCORE_API FNetReplicatedObjectDebugInfo DebugNetObject(UObject* Instance);

/**
 * Look up replicated handle from Instance pointer and return debug information. This variant only searches the ReplicationSystem with the specified id.
 *
 * 带 RS id 限定的查询，PIE 场景使用。
 */
extern "C" IRISCORE_API FNetReplicatedObjectDebugInfo DebugNetObjectById(UObject* Instance, uint32 ReplicationSystemId);

/** Look up replicated handle and return debug information */
/** 通过完整 FNetRefHandle 查询。RS id 已编码在 handle 里，故无需额外参数。 */
extern "C" IRISCORE_API FNetReplicatedObjectDebugInfo DebugNetRefHandle(FNetRefHandle Handle);

/** Look up replicated handle specified by handle id and replicationsystem id and return debug information */
/** 只有 id 数字（例如从日志里拷出来的）时使用：分别传 handle id 与 RS id。 */
extern "C" IRISCORE_API FNetReplicatedObjectDebugInfo DebugNetRefHandleById(uint64 NetRefHandleId, uint32 ReplicationSystemId);

/** Look up replicated handle specified by an internal index and replicationsystem id and return debug information */
/** 通过内部连续索引查询——配合断点里看到的 InternalIndex 最方便。 */
extern "C" IRISCORE_API FNetReplicatedObjectDebugInfo DebugInternalNetRefIndex(UE::Net::Private::FInternalNetRefIndex InternalIndex, uint32 ReplicationSystemId);

/** Variant of DebugOutputNetObjectProtocolReferences that can be used from breakpoints or in watch window to find all handles references registered for a protocol and output to DebugOutput in debugger
 * NOTE: Use only for debugging as this variant uses a static buffer which is not thread safe	
 *
 * watch 窗口可调用的字符串返回版本，使用 **静态缓冲**，非线程安全。
 */
extern "C" IRISCORE_API const TCHAR* DebugNetObjectProtocolReferencesToString(FReplicationProtocolIdentifier ProtocolId, uint32 ReplicationSystemId);

/** Set the Object Name to break on */
/** 设置 BreakOnObjectName/FilterDebuggedObject 所使用的子串；等价于 `Net.Iris.DebugName` CVar。 */
extern "C" IRISCORE_API void SetIrisDebugObjectName(const ANSICHAR* NameBuffer);

/** Set the NetHandle to break on */
/** 设置 BreakOnNetRefHandle 所关注的 id；等价于 `Net.Iris.DebugNetRefHandle` CVar。 */
extern "C" IRISCORE_API void SetIrisDebugNetRefHandle(uint64 NetHandleId);

/** Set the InternalIndex to break on */
/** 设置关注的内部索引；等价于 `Net.Iris.DebugNetInternalIndex` CVar。 */
extern "C" IRISCORE_API void SetIrisDebugInternalNetRefIndex(UE::Net::Private::FInternalNetRefIndex InternalIndex);

/** Set the InternalIndex to break on via it's NetHandle */
/** 通过 NetRefHandle 间接设置 InternalIndex（内部做一次查表）。 */
extern "C" IRISCORE_API void SetIrisDebugInternalNetRefIndexViaNetHandle(FNetRefHandle RefHandle);

/** Set the InternalIndex to break on via an object pointer*/
/** 通过 UObject* 间接设置 InternalIndex——在调试器里点到对象指针时最方便。 */
extern "C" IRISCORE_API void SetIrisDebugInternalNetRefIndexViaObject(UObject* Instance);

/** Set the RPC Name to break on */
/** 设置 BreakOnRPCName 所关注的子串；等价于 `Net.Iris.DebugRPCName` CVar。 */
extern "C" IRISCORE_API void SetIrisDebugRPCName(const ANSICHAR* NameBuffer);

}; // end of UE::Net::IrisDebugHelper

#endif

