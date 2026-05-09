// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationBridgeTypes.h
// 角色：UReplicationBridge / UObjectReplicationBridge 与上层（NetDriver、游戏代码）
//       之间共享的小型枚举与结果结构。
//
// 关键内容：
//   * EEndReplicationFlags                    : "停止复制"行为的位掩码
//   * EReplicationBridgeCreateNetRefHandleResultFlags : Bridge 创建 NetRefHandle 后向上返回的标志
//   * FReplicationBridgeCreateNetRefHandleResult      : 上面的"句柄+flags"的元组
//   * EReplicationBridgeDestroyInstanceReason         : 客户端销毁/分离对象的原因
//   * EReplicationBridgeDestroyInstanceFlags          : 销毁动作的细节标志
//
// 详见 ReplicationBridge.cpp / ObjectReplicationBridge.cpp。
// =====================================================================================

#pragma once

#include "Iris/ReplicationSystem/NetRefHandle.h"

#include "Misc/EnumClassFlags.h"

//------------------------------------------------------------------------

/**
 * "停止复制"标志位。组合表达多种语义：
 *   None                                : 单纯停止追踪（不销毁、不 TearOff）
 *   Destroy                             : 远端销毁实例（动态对象默认行为）
 *   TearOff                             : 远端"撕断"——保留实例但不再同步状态
 *   Flush                               : 在结束前先把脏状态推送给所有连接（保证最终态一致）
 *   DestroyNetHandle                    : 同时销毁绑定的 FNetHandle（仅在不再被任何 RS 复制时使用）
 *   ClearNetPushId                      : 清除对象上的 PushId（旧 PushModel 兼容；同上谨慎使用）
 *   SkipPendingEndReplicationValidation : 跳过 PendingEndReplication 的合法性校验（关闭流程兜底）
 */
enum class EEndReplicationFlags : uint32
{
	None								= 0U,
	/** Destroy remote instance. Default for dynamic objects unless they have TearOff flag set. */
	Destroy								= 1U,				
	/** Stop replication object without destroying instance on the remote end. */
	TearOff								= Destroy << 1U,
	/** Complete replication of pending state to all clients before ending replication. */
	Flush								= TearOff << 1U,
	/** Destroy NetHandle if one is associated with the replicated object. This should only be done if the object should not be replicated by any other replication system. */
	DestroyNetHandle					= Flush << 1U,
	/** Clear net push ID to prevent this object and its subobjects from being marked as dirty in the networking system. This should only be done if the object should not be replicated by any other replication system. */
	ClearNetPushId						= DestroyNetHandle << 1U,
	/** Skip bPendingEndReplication Validation, In some cases we want to allow detaching instance from replicated object on clients, such as when shutting down */
	SkipPendingEndReplicationValidation = ClearNetPushId << 1U,
};
ENUM_CLASS_FLAGS(EEndReplicationFlags);

/** EEndReplicationFlags 的逗号分隔文本表示，便于日志阅读。 */
IRISCORE_API FString LexToString(EEndReplicationFlags EndReplicationFlags);

//------------------------------------------------------------------------

/**
 * Bridge 在收到/创建一个 NetRefHandle 后，给调用者（或上层）的额外结果标志。
 * 由派生 Bridge 的 Instantiate 流程返回。
 */
enum class EReplicationBridgeCreateNetRefHandleResultFlags : uint32
{
	None = 0U,
	/** Whether the instance may be destroyed due to the remote peer requesting the object to be destroyed. If not then the object itself must not be destroyed. */
	/** 是否允许在收到远端销毁请求时真正销毁本地实例（关卡放置对象常常关闭此项）。 */
	AllowDestroyInstanceFromRemote = 1U << 0U,
	/** Set this flag if you created a subobject and want the RootObject to be notified of the subobject's creation. */
	/** SubObject 创建后，是否要回调 RootObject 的 SubObjectCreatedFromReplication。 */
	ShouldCallSubObjectCreatedFromReplication = AllowDestroyInstanceFromRemote << 1U,
	/** Bind the static netrefhandle to an object. Needed when the object wasn't found via ResolveObjectReference */
	/** 当通过 ResolveObjectReference 找不到 static 对象时，强制将 NetRefHandle 绑定到提供的实例上。 */
	BindStaticObjectInReferenceCache = ShouldCallSubObjectCreatedFromReplication << 1U,
};
ENUM_CLASS_FLAGS(EReplicationBridgeCreateNetRefHandleResultFlags);

//------------------------------------------------------------------------

/**
 * Instantiate 的"返回三元组"——句柄 + 后续 Bridge 应做哪些事（Flags）。
 */
struct FReplicationBridgeCreateNetRefHandleResult
{
	UE::Net::FNetRefHandle NetRefHandle;
	EReplicationBridgeCreateNetRefHandleResultFlags Flags = EReplicationBridgeCreateNetRefHandleResultFlags::None;
};

//------------------------------------------------------------------------

/**
 * 客户端销毁/分离实例的"原因"。决定 Factory 走 DetachOnly / Destroy / TearOff 哪条路径。
 *   - DoNotDestroy : 仅解绑（实例仍由游戏侧持有，常用于 Pool）
 *   - TearOff      : 撕断同步（按 EReplicationBridgeDestroyInstanceFlags 决定是否真的销毁）
 *   - Destroy      : 真正销毁
 */
enum class EReplicationBridgeDestroyInstanceReason : uint32
{
	DoNotDestroy,
	TearOff,
	Destroy,
};
IRISCORE_API const TCHAR* LexToString(EReplicationBridgeDestroyInstanceReason Reason);

//------------------------------------------------------------------------

/**
 * 销毁动作的细节标志。
 *   AllowDestroyInstanceFromRemote : 当 Reason 为 TearOff（或 Destroy）时，允许把实例真正销毁；
 *                                    若关闭，则即使被远端要求销毁也只解绑不销毁。
 */
enum class EReplicationBridgeDestroyInstanceFlags : uint32
{
	None = 0U,
	/** Whether the instance may be destroyed when instructed from the remote peer. This flag applies when the destroy reason is TearOff and torn off actors are to be destroyed as well as regular Destroy. */
	AllowDestroyInstanceFromRemote = 1U << 0U,
};
ENUM_CLASS_FLAGS(EReplicationBridgeDestroyInstanceFlags);

IRISCORE_API const TCHAR* LexToString(EReplicationBridgeDestroyInstanceFlags DestroyFlags);

