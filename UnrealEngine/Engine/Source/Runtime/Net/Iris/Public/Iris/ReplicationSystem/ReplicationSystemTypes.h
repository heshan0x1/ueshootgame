// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationSystemTypes.h
// 角色：ReplicationSystem 模块面向公共的"小型枚举/Trait/委托类型"集合。
//       本文件不包含子系统类，仅集中定义跨 Bridge / Filter / Prioritizer / Conditional /
//       NetBlob / DeltaCompression 等子系统都会用到的"传值契约"。
//
// 本文件包含：
//   * ENetObjectDeltaCompressionStatus  : 对象级 Delta 压缩开关
//   * EGetRefHandleFlags                : 查询 NetRefHandle 时的额外语义
//   * EReplicationSystemSendPass        : 帧循环里"哪一次发送"
//   * EDependentObjectSchedulingHint    : 依赖对象的调度时机提示
//   * FForwardNetRPCCallDelegate        : 本地 RPC 转发给上层逻辑的委托类型
//   * ENetObjectAttachmentSendPolicyFlags : 附件/RPC 发送策略位掩码
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Delegates/Delegate.h"
#include "Misc/EnumClassFlags.h"

namespace UE::Net
{

/**
 * 单个 NetObject 是否允许参与 DeltaCompression。
 * 注意：仅是一个"许可标志"——是否真的进入 DeltaCompression Pipeline，还要看
 * MaxDeltaCompressedObjectCount 限额、连接存活、基线池容量等多重约束。
 *   - Disallow : 永远走全量量化路径
 *   - Allow    : 由 FDeltaCompressionBaselineManager 决定何时建立基线
 */
enum class ENetObjectDeltaCompressionStatus : unsigned
{
	Disallow,
	Allow,
};

/**
 * 在 Bridge::GetReplicatedRefHandle 这类查询里使用的标志位。
 *   - None         : 仅返回当前活跃对象的 handle
 *   - EvenIfGarbage: 即便对象已被 GC 标记仍返回 handle（用于诊断 / 强制清理）
 */
enum class EGetRefHandleFlags : uint32
{
	None,
	EvenIfGarbage
};
ENUM_CLASS_FLAGS(EGetRefHandleFlags);

/**
 * 标识当前帧里属于"哪一次"发送动作（参见 Iris_Architecture.md §4 帧循环）。
 *   - PostTickDispatch : Dispatch 紧随其后的"小幅度"发送，通常仅推送 RPC/Attachment，
 *                       不重新计算 Scope/Filtering，不 Poll 状态。
 *   - TickFlush        : 完整 Send 阶段——经过 Filter/Prio/Quantize/Write 全流程，
 *                       面向所有连接做一次完整复制更新。
 */
enum class EReplicationSystemSendPass : unsigned
{
	Invalid,

	// Sending data directly after PostDispatch, this is a partial update only, not updating scope and filtering and will only process RPC/Attachmeents
	PostTickDispatch,

	// Sending data part of TickFlush, this will do a full update and replicate data to all connections
	TickFlush,
};

/**
 * "依赖对象（DependentObject）"的调度时机提示。控制依赖对象与父对象在写包优先级上的关系。
 *   - Default                          : 跟随父对象一起复制；首次复制可与父在同 batch
 *   - ScheduleBeforeParent             : 调度时尽量先于父；只有同包时父也会被一起调度
 *   - ScheduleBeforeParentIfInitialState: 仅当依赖对象首次发送时表现为 BeforeParent，
 *                                        之后退化为 Default（先父再子）
 */
enum class EDependentObjectSchedulingHint : uint8
{
	// Default behavior, dependent object will be scheduled to replicate if parent is replicated, if the dependent object has not yet been replicated it will be replicated in the same batch as the parent
	Default = 0,

	// Dependent object will be scheduled to replicate before parent is replicated, if the dependent has data to send and has not yet been replicated the parent will only be scheduled if they both fit in same packet
	ScheduleBeforeParent,

	// Not yet replicated dependent object will behave as ReplicateBeforeParent otherwise it will be scheduled to replicate if the parent is replicated and scheduled after the parent
	ScheduleBeforeParentIfInitialState,
};

/**
 * RPC 转发委托：本地执行 RPC 时，让上层（如 NetDriver）接管最终的 Forward 调用。
 * 在 FReplicationSystemParams::ForwardNetRPCCallDelegate 注入；
 * Multicast 版本则保存在 FReplicationSystemInternal 内部供多个监听者订阅。
 */
using FForwardNetRPCCallDelegate = TDelegate<void(UObject* RootObject,UObject* SubObject, UFunction* Function, void* Params)>;
using FForwardNetRPCCallMulticastDelegate = TMulticastDelegate<typename FForwardNetRPCCallDelegate::TFuncType>;

/**
 * 附件 / RPC 的发送策略位掩码。控制能否走 OOB（Out-of-Band）通道、能否在
 * PostTickDispatch 阶段提前发送等。
 *
 * 位含义：
 *   None                  : 默认（跟随对象的常规发送时机，可靠/不可靠按消息自身决定）
 *   ScheduleAsOOB         : 进入 OOB 通道，争取尽早发出。仅对 unreliable 消息合法
 *   SendInPostTickDispatch: 提示倾向在 PostTickDispatch 发送（与 ScheduleAsOOB 协作）
 *                           只要队列里有任意条带此标志，已排队的 OOB unreliable 消息
 *                           都会一起在 PostTickDispatch 发送
 *   SendImmediate         : OOB + PostTickDispatch 的合成标志，含义"立即发送"
 *
 * 触发场景：
 *   * RPC 标记为 Reliable 是不能进入 OOB 的（限制位见 NetBlobManager 相关代码）
 *   * 客户端 SetRPCSendPolicyFlags 通常用于"高优先级响应"型的 RPC
 */
enum class ENetObjectAttachmentSendPolicyFlags : uint32
{
	// Default
	None = 0,

	// Schedule attachment to use the Out of bounds channel, essentially schedule the attachment to be sent as early as possible. Note: Only valid for unreliable attachments.
	ScheduleAsOOB = 1U << 0U,

	// Hint that this attachment like to be sent during PostTickDispatch. 
	// If one enqueued attachment has this flag all currently unreliable attachments scheduled to use the OOB channel will be sent during PostTickDispatch.
	SendInPostTickDispatch = ScheduleAsOOB << 1U,	

	// SendImmediate, Attachment should be sent using OOB channel and from PostTickDispatch.
	SendImmediate = ScheduleAsOOB | SendInPostTickDispatch,
};

ENUM_CLASS_FLAGS(ENetObjectAttachmentSendPolicyFlags);

/** ENetObjectAttachmentSendPolicyFlags -> 文本（日志/Trace 友好）。 */
inline const TCHAR* LexToString(ENetObjectAttachmentSendPolicyFlags SendFlags)
{
	switch (SendFlags)
	{
	case ENetObjectAttachmentSendPolicyFlags::None: return TEXT("None");
	case ENetObjectAttachmentSendPolicyFlags::ScheduleAsOOB: return TEXT("ScheduleAsOOB");
	case ENetObjectAttachmentSendPolicyFlags::SendInPostTickDispatch: return TEXT("SendInPostTickDispatch");
	case ENetObjectAttachmentSendPolicyFlags::SendImmediate: return TEXT("SendImmediate");
	default: ensure(false); return TEXT("Missing");
	}
}

/** EDependentObjectSchedulingHint -> 文本。 */
inline const TCHAR* LexToString(EDependentObjectSchedulingHint SchedulingHint)
{
	switch (SchedulingHint)
	{
	case EDependentObjectSchedulingHint::Default: return TEXT("Default");
	case EDependentObjectSchedulingHint::ScheduleBeforeParent: return TEXT("BeforeParent");
	case EDependentObjectSchedulingHint::ScheduleBeforeParentIfInitialState: return TEXT("BeforeParentIfInitialState");
	default: ensure(false); return TEXT("Missing");
	}
}

} // end namespace UE::Net
