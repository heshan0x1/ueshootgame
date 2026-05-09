// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobManager.cpp —— Iris RPC + 附件 + HugeObject 入队/派发的核心实现。
//
// 一帧的发送流程（典型）：
//   1) 游戏线程：UReplicationSystem::SendRPC / QueueNetObjectAttachment
//      → FNetBlobManager::SendUnicastRPC / SendMulticastRPC / QueueNetObjectAttachment
//      → 解析 root/sub InternalIndex → 调用 NetRPCHandler::CreateRPC
//      → AttachmentSendQueue.Enqueue(...)（写入 AttachmentQueue 或 ScheduleAsOOBAttachmentQueue）
//
//   2) 帧末 PostTickDispatch：
//      → ProcessOOBNetObjectAttachmentSendQueue：仅处理 ScheduleAsOOBAttachmentQueue
//        → 调用 ReplicationWriter->QueueNetObjectAttachments → 立即 flush 到网络层
//
//   3) 帧后台 PostUpdate（实际位于 ReplicationSystem 的 SendUpdate 阶段）：
//      → ProcessNetObjectAttachmentSendQueue(ProcessObjectsGoingOutOfScope)
//        把"对象本帧出 scope 但尚有可靠 attachment"的条目优先送出；
//      → ProcessNetObjectAttachmentSendQueue(ProcessObjectsInScope)
//        处理其余仍 in-scope 的条目，正常进入 ReplicationWriter；
//      → ResetNetObjectAttachmentSendQueue 清空队列与上下文。
//
// 三类发送策略 (ENetObjectAttachmentSendPolicyFlags)：
//   - 无标志            → 写入 AttachmentQueue（普通流，下一帧 PostUpdate 排空）。
//   - ScheduleAsOOB     → 写入 ScheduleAsOOBAttachmentQueue（OOB 流，PostTickDispatch 立即排空）。
//   - SendInPostTickDispatch → 入队后把 ConnectionId 标记为"待立即发送"，由更高层
//     把该连接的 packet 立即 push 到网络层，避免再等一帧。
//
// 关键 CVar：
//   - net.Iris.EnableRPCs                    ：全局开关；为 0 时所有 RPC 无效。
//   - net.Iris.ThrottleRPCWarnings           ：失败 warn 限频，每个 UFunction 仅一次。
//   - net.Iris.RPC.AllowOnDormantObjects     ：是否允许向 dormant 对象发 RPC。
//   - net.Iris.RPC.AutoNetFlushOnDormantObjects：发 RPC 时若对象 dormant，自动 NetFlush。
// =====================================================================================

#include "Iris/ReplicationSystem/NetBlob/NetBlobManager.h"
#include "HAL/IConsoleManager.h"
#include "Iris/ReplicationSystem/NetBlob/NetObjectBlobHandler.h"
#include "Iris/ReplicationSystem/NetBlob/NetRPCHandler.h"
#include "Iris/ReplicationSystem/NetBlob/PartialNetObjectAttachmentHandler.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/Core/IrisLog.h"
#include "Net/Core/Trace/NetDebugName.h"

namespace UE::Net::Private
{

// 全局 RPC 开关：CVar 设为 0 时 SendUnicastRPC/SendMulticastRPC 立刻返回 false。
static TAutoConsoleVariable<int32> CVarEnableIrisRPCs(TEXT("net.Iris.EnableRPCs"), 1, TEXT( "If > 0 let Iris replicate and execute RPCs."));

// RPC 失败 warn 限流：避免高频重复打印同一个 UFunction 的失败日志。
static bool bThrottleRPCWarnings = true;
static FAutoConsoleVariableRef CVarThrottleRPCWarnings(TEXT("net.Iris.ThrottleRPCWarnings"), bThrottleRPCWarnings, TEXT("Only log send failure warnings once per RPC type."));

// 默认允许向 dormant 对象发 RPC。关闭后 dormant 对象的 RPC 一律拒绝（除非该对象已请求 NetFlush）。
static bool bAllowRPCsOnDormantObjects = true;
static FAutoConsoleVariableRef CVarAllowRPCsOnDormantObjects(TEXT("net.Iris.RPC.AllowOnDormantObjects"), bAllowRPCsOnDormantObjects, TEXT("When true allow RPCs to be sent on dormant objects. When false block all RPCs from the moment the dormant change is requested."));

// 默认在 dormant 对象上发 RPC 时强制 NetFlush，以确保连同最新的 replicated 属性一起送出。
static bool bAutoNetFlushOnDormantRPC = true;
static FAutoConsoleVariableRef CVarAutoNetFlushOnDormantRPC(TEXT("net.Iris.RPC.AutoNetFlushOnDormantObjects"), bAutoNetFlushOnDormantRPC, TEXT("When true we will make an implicit NetFlush request when an RPC is sent on a dormant object. When false no replicated properties would be sent along with the RPC."));

FNetBlobManager::FNetBlobManager()
{
}

// 初始化：固化指针、读取 server/client 角色、注册三大默认 handler、初始化发送队列。
void FNetBlobManager::Init(FNetBlobManagerInitParams& InitParams)
{
	BlobHandlerManager.Init();

	ReplicationSystem = InitParams.ReplicationSystem;
	Connections = &InitParams.ReplicationSystem->GetReplicationSystemInternal()->GetConnections();
	NetRefHandleManager = &InitParams.ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
	ObjectReferenceCache = &InitParams.ReplicationSystem->GetReplicationSystemInternal()->GetObjectReferenceCache();
	bIsServer = ReplicationSystem->IsServer();
	bSendAttachmentsWithObject = InitParams.bSendAttachmentsWithObject; 
	bAllowObjectReplication = ReplicationSystem->AllowObjectReplication();

	RegisterDefaultHandlers();

	AttachmentSendQueue.Init(this);
}

// 注册外部 handler：转发到 BlobHandlerManager.RegisterHandler。
bool FNetBlobManager::RegisterNetBlobHandler(UNetBlobHandler* Handler)
{
	return BlobHandlerManager.RegisterHandler(Handler);
}

// ----------------------------------------------------------------------------------
// QueueNetObjectAttachment：通用附件入队（非 RPC 路径）。
// 1) 校验连接存在；2) 解析 TargetRef → root/sub InternalIndex（必要时 fallback 到 ReplicatedOuter）；
// 3) 把 (caller, target) 写入 attachment；4) 入 AttachmentSendQueue。
// ----------------------------------------------------------------------------------
bool FNetBlobManager::QueueNetObjectAttachment(uint32 ConnectionId, const FNetObjectReference& TargetRef, const TRefCountPtr<FNetObjectAttachment>& Attachment, ENetObjectAttachmentSendPolicyFlags SendFlags)
{
	if (!Attachment.IsValid())
	{
		return false;
	}

	if (!Connections->IsValidConnection(ConnectionId))
	{
		// 连接已不存在：直接丢弃。返回 true 表示"已处理"（避免上层重试）。
		UE_LOG(LogIris, Warning, TEXT("Dropping attachment to invalid connection %u."), ConnectionId);
		return true;
	}

	FRPCOwner OwnerInfo;

	OwnerInfo.CallerRef = TargetRef;
	OwnerInfo.TargetRef = TargetRef;

	// 第一次尝试：直接用 TargetRef 解析 root/sub。
	bool bCanSendRpc = GetRootObjectAndSubObjectIndicesFromAnyHandle(TargetRef.GetRefHandle(), OwnerInfo.RootObjectIndex, OwnerInfo.SubObjectIndex);

	if (!bCanSendRpc)
	{
		if (!TargetRef.IsValid())
		{
			UE_LOG(LogIris, Warning, TEXT("QueueNetObjectAttachment %s Failed due to invalid Target. Unable to resolve target reference %s."), *Attachment->GetNetObjectReference().ToString(), *TargetRef.ToString());
			return false;
		}

		//$IRIS TODO: It's possible the Target and Caller will be differrent from the RootObjectIndex and SubObjectIndex since we use the ReplicatedOuter instead of the true Root.
		//This probably happens when the outer list looks like this: Actor->ActorComponent->SubObject1->SubObject2. 

		// If the TargetRef is a valid reference but not replicated, then the outer must be used instead
		// Fallback：Target 自身未复制，则尝试用 ReplicatedOuter（最近的复制 outer）作 caller。
		// 典型场景：把 attachment 发到一个非复制的 subobject，由其 outer 承载传输。
		OwnerInfo.CallerRef = ObjectReferenceCache->GetReplicatedOuter(TargetRef);

		// Can that outer send RPCs
		bCanSendRpc = GetRootObjectAndSubObjectIndicesFromAnyHandle(OwnerInfo.CallerRef.GetRefHandle(), OwnerInfo.RootObjectIndex, OwnerInfo.SubObjectIndex);
		if (!bCanSendRpc)
		{
			UE_LOG(LogIris, Warning, TEXT("QueueNetObjectAttachment %s Failed due to invalid Outer. Unable to resolve outer reference %s (index: %u) for target reference %s (index: %u)."), 
				*Attachment->GetNetObjectReference().ToString(), ToCStr(OwnerInfo.CallerRef.ToString()), OwnerInfo.RootObjectIndex, ToCStr(TargetRef.ToString()), OwnerInfo.SubObjectIndex);

			return false;
		}
	}

	// 设置 attachment 的 owner/target 引用，再入队。
	Attachment->SetNetObjectReference(OwnerInfo.CallerRef, OwnerInfo.TargetRef);
	AttachmentSendQueue.Enqueue(ConnectionId, OwnerInfo.RootObjectIndex, OwnerInfo.SubObjectIndex, Attachment, SendFlags);
	return true;
}

// ----------------------------------------------------------------------------------
// SendMulticastRPC：服务端 → 全部已 view 的客户端。
//   - 仅在服务端有意义：客户端调用属于"错误方向"。
//   - 若没有任何 valid 连接则直接成功返回（不算错误）。
//   - dormant 对象：默认允许；若 CVar 关闭且对象未请求 NetFlush，则丢弃。
// ----------------------------------------------------------------------------------
bool FNetBlobManager::SendMulticastRPC(const FSendRPCContext& Context, const void* Parameters, UE::Net::ENetObjectAttachmentSendPolicyFlags SendFlags)
{
	if (CVarEnableIrisRPCs.GetValueOnGameThread() <= 0)
	{
		return false;
	}

	UNetRPCHandler* Handler = RPCHandler.Get();
	if (Handler == nullptr)
	{
		return false;
	}

	// May the RPC be sent?
	// 方向校验：服务端只允许发 NetClient/NetMulticast；客户端只允许发 NetServer。
	if ((Context.Function->FunctionFlags & (bIsServer ? (FUNC_NetClient | FUNC_NetMulticast) : FUNC_NetServer)) == 0)
	{
		checkf(false, TEXT("Trying to call RPC %s in the wrong direction."), ToCStr(Context.Function->GetName()));
		return true;
	}

	// Check if there are any connections at all
	const FNetBitArray& ValidConnections = Connections->GetValidConnections();
	if (ValidConnections.FindFirstOne() == FNetBitArray::InvalidIndex)
	{
		// 没有连接：成功返回（避免后续创建 RPC 浪费）。
		return true;
	}

	FRPCOwner OwnerInfo;
	if (!GetRPCOwner(OwnerInfo, Context))
	{
		return false;
	}
	
	// If we prevent RPCs on dormant objects (allowed by default)
	const bool bIsObjectDormant = NetRefHandleManager->GetWantToBeDormantInternalIndices().IsBitSet(OwnerInfo.RootObjectIndex);
	if (!bAllowRPCsOnDormantObjects && bIsObjectDormant)
	{
		// If the object is dormant but requested a FlushNet we still allow him to send RPCs
		// This also means objects that went dormant on the current frame can also send RPCs.
		// 例外：本帧请求了 NetFlush 的对象仍允许发 RPC，使新转 dormant 的对象有机会
		// 把最后一次 RPC 与状态一起送出。
		if (!NetRefHandleManager->GetDormantObjectsPendingFlushNet().IsBitSet(OwnerInfo.RootObjectIndex))
		{
			return false;
		}
	}

	// 创建 FNetRPC（封装函数指针 + 参数副本）。
	const TRefCountPtr<FNetRPC>& RPC = Handler->CreateRPC(OwnerInfo.CallerRef, Context.Function, Parameters);
	if (!RPC.IsValid())
	{
		UE_LOG(LogIris, Warning, TEXT("Unable to create RPC for function %s."), ToCStr(Context.Function->GetName()));
		return true;
	}

	// Force a NetFlush when sending an RPC on a dormant object
	// dormant 对象发 RPC：自动触发 NetFlush，让 ReplicationBridge 把状态一并打包发送。
	if (bAutoNetFlushOnDormantRPC && bIsObjectDormant)
	{
		UObjectReplicationBridge* Bridge = ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>();
		Bridge->NetFlushDormantObject(ObjectReferenceCache->GetObjectReferenceHandleFromObject(Context.RootObject));
	}

	RPC->SetNetObjectReference(OwnerInfo.CallerRef, OwnerInfo.TargetRef);
	// 多播形式入队：connection id 留 0，记录入队时刻 OpenConnections 集合，
	// 后续 ProcessQueue 时只对这些连接发出（避免发给已关闭的连接）。
	// reinterpret_cast：FNetRPC 是 FNetObjectAttachment 派生，TRefCountPtr 布局兼容。
	AttachmentSendQueue.Enqueue(OwnerInfo.RootObjectIndex, OwnerInfo.SubObjectIndex, reinterpret_cast<const TRefCountPtr<FNetObjectAttachment>&>(RPC), SendFlags, Connections->GetOpenConnections());

	return true;
}

// ----------------------------------------------------------------------------------
// SendUnicastRPC：定向发送到指定 ConnectionId。
//   - 客户端 → 服务端：必须含 NetServer 标志。
//   - 服务端 → 单一客户端：必须含 NetClient 或 NetMulticast 标志。
//   - 连接正在关闭：拒绝新 RPC（仅允许 flush 既有 reliable 数据）。
// ----------------------------------------------------------------------------------
bool FNetBlobManager::SendUnicastRPC(uint32 ConnectionId, const FSendRPCContext& Context, const void* Parameters, UE::Net::ENetObjectAttachmentSendPolicyFlags SendFlags)
{
	if (CVarEnableIrisRPCs.GetValueOnGameThread() <= 0)
	{
		return false;
	}

	UNetRPCHandler* Handler = RPCHandler.Get();
	if (Handler == nullptr)
	{
		return false;
	}

	// If NetServer, NetClient, or NetMulticast flags are present, filter the RPC based on them. If not, send the RPC in either directon.
	// 方向校验：在 server 上调 NetServer 函数 / 在 client 上调 NetClient/Multicast 函数都视为方向错误。
	if ((Context.Function->FunctionFlags & FUNC_NetServer) && bIsServer)
	{
		checkf(false, TEXT("Trying to call server RPC %s in the wrong direction."), ToCStr(Context.Function->GetName()));
		return true;
	}

	if ((Context.Function->FunctionFlags & (FUNC_NetClient | FUNC_NetMulticast)) && !bIsServer)
	{
		checkf(false, TEXT("Trying to call client RPC %s in the wrong direction."), ToCStr(Context.Function->GetName()));
		return true;
	}

	if (!Connections->IsValidConnection(ConnectionId))
	{
		UE_LOG(LogIris, Warning, TEXT("Trying to call RPC on non-existing connection %u."), ConnectionId);
		return true;
	}

	if (!Connections->IsOpenConnection(ConnectionId))
	{
		// This connection is shutting down and only flushing existing reliable data, not sending new RPCs.
		// 连接处于"关闭中但仍 valid"状态：只 flush 既有可靠数据，不接受新 RPC。
		return true;
	}

	FRPCOwner OwnerInfo;
	if (!GetRPCOwner(OwnerInfo, Context))
	{
		return false;
	}

	// If we prevent RPCs on dormant objects (allowed by default)
	const bool bIsObjectDormant = NetRefHandleManager->GetWantToBeDormantInternalIndices().IsBitSet(OwnerInfo.RootObjectIndex);
	if (!bAllowRPCsOnDormantObjects && bIsObjectDormant)
	{
		// If the object is dormant but requested a FlushNet we still allow him to send RPCs
		// This also means objects that went dormant on the current frame can also send RPCs.
		if (!NetRefHandleManager->GetDormantObjectsPendingFlushNet().IsBitSet(OwnerInfo.RootObjectIndex))
		{
			return false;
		}
	}

	const TRefCountPtr<FNetRPC>& RPC = Handler->CreateRPC(OwnerInfo.CallerRef, Context.Function, Parameters);
	if (!RPC.IsValid())
	{
		UE_LOG(LogIris, Warning, TEXT("Unable to create RPC for function %s."), ToCStr(Context.Function->GetName()));
		return true;
	}

	// Force a NetFlush when sending an RPC on a dormant object
	if (bAutoNetFlushOnDormantRPC && bIsObjectDormant)
	{
		UObjectReplicationBridge* Bridge = ReplicationSystem->GetReplicationBridgeAs<UObjectReplicationBridge>();
		Bridge->NetFlushDormantObject(ObjectReferenceCache->GetObjectReferenceHandleFromObject(Context.RootObject));
	}

	RPC->SetNetObjectReference(OwnerInfo.CallerRef, OwnerInfo.TargetRef);
	// 单播入队（含 connection id）。
	AttachmentSendQueue.Enqueue(ConnectionId, OwnerInfo.RootObjectIndex, OwnerInfo.SubObjectIndex, reinterpret_cast<const TRefCountPtr<FNetObjectAttachment>&>(RPC), SendFlags);
	return true;
}

// ----------------------------------------------------------------------------------
// GetRPCOwner：解析 RPC 调用者 / 目标 → root/sub InternalIndex。
//   - SubObject == nullptr：root 直接发；caller == target == root。
//   - SubObject != nullptr 且已复制：subobject 发；caller == target == sub。
//   - SubObject != nullptr 但未复制：fallback 由 root 发；caller=root，target=sub。
//   失败时按 ThrottleRPCWarnings 控制日志频率。
// ----------------------------------------------------------------------------------
bool FNetBlobManager::GetRPCOwner(FRPCOwner& OutOwnerInfo, const FSendRPCContext& Context) const
{
	bool bCanSendRpc = false;

	// If a root object is sending an RPC
	if (Context.SubObject == nullptr)
	{
		OutOwnerInfo.TargetRef = ObjectReferenceCache->GetOrCreateObjectReference(Context.RootObject);
		OutOwnerInfo.CallerRef = OutOwnerInfo.TargetRef;

		bCanSendRpc = GetRootObjectIndicesFromHandle(OutOwnerInfo.TargetRef.GetRefHandle(), OutOwnerInfo.RootObjectIndex);

		if (!bCanSendRpc)
		{
			// 失败：可能是对象尚未被复制。按 throttle 配置仅打一次。
			bool bLogRPCFailed = true;
			if (UE::Net::Private::bThrottleRPCWarnings)
			{
				bool& bWasAlreadyLogged = RPCWarningThrottler.FindOrAdd(Context.Function->GetFName(), false);
				bLogRPCFailed = !bWasAlreadyLogged;
				bWasAlreadyLogged = true;
			}
			UE_CLOG(bLogRPCFailed, LogIris, Warning, TEXT("SendRPC %s for %s Failed. This rootobject is not yet replicated (RefHandle: %s Index: %u)."),
					ToCStr(Context.Function->GetName()), *GetNameSafe(Context.RootObject), ToCStr(OutOwnerInfo.CallerRef.GetRefHandle().ToString()), OutOwnerInfo.RootObjectIndex);
		}
	}
	// If a subobject is sending an RPC
	else
	{
		const FNetRefHandle SubObjectNetRef = ObjectReferenceCache->GetObjectReferenceHandleFromObject(Context.SubObject);

		// If the subobject can be referenced
		if (SubObjectNetRef.IsValid())
		{
			OutOwnerInfo.TargetRef = ObjectReferenceCache->GetOrCreateObjectReference(Context.SubObject);
			OutOwnerInfo.CallerRef = OutOwnerInfo.TargetRef;

			check(OutOwnerInfo.TargetRef.GetRefHandle() == SubObjectNetRef);

			bCanSendRpc = GetRootObjectAndSubObjectIndicesFromSubObjectHandle(OutOwnerInfo.TargetRef.GetRefHandle(), OutOwnerInfo.RootObjectIndex, OutOwnerInfo.SubObjectIndex);
		}
		
		// Send the RPC via the Root object if the subobject is not capable
		// Fallback：subobject 不能直接承载（未复制）→ 改由 root 充当 caller，sub 仅作 target。
		if (!bCanSendRpc)
		{
			OutOwnerInfo.TargetRef = ObjectReferenceCache->GetOrCreateObjectReference(Context.SubObject);
			OutOwnerInfo.CallerRef = ObjectReferenceCache->GetOrCreateObjectReference(Context.RootObject);

			bCanSendRpc = GetRootObjectIndicesFromHandle(OutOwnerInfo.CallerRef.GetRefHandle(), OutOwnerInfo.RootObjectIndex);
		}
		
		if (!bCanSendRpc)
		{
			bool bLogRPCFailed = true;
			if (UE::Net::Private::bThrottleRPCWarnings)
			{
				bool& bWasAlreadyLogged = RPCWarningThrottler.FindOrAdd(Context.Function->GetFName(), false);
				bLogRPCFailed = !bWasAlreadyLogged;
				bWasAlreadyLogged = true;
			}

			UE_CLOG(bLogRPCFailed, LogIris, Warning, TEXT("SendRPC %s for %s::%s Failed. The root object (RefHandle: %s Index: %u) and subobject (RefHandle: %s Index: %u) is not yet replicated."),
					ToCStr(Context.Function->GetName()), *GetNameSafe(Context.RootObject), *GetNameSafe(Context.SubObject), ToCStr(OutOwnerInfo.CallerRef.ToString()), OutOwnerInfo.RootObjectIndex, ToCStr(OutOwnerInfo.TargetRef.ToString()), OutOwnerInfo.SubObjectIndex);
		}
	}

	return bCanSendRpc;
}

// 校验 handle 是 root 对象（无 SubObjectRootIndex），返回其 InternalIndex。
bool FNetBlobManager::GetRootObjectIndicesFromHandle(FNetRefHandle RootObjectRefHandle, FInternalNetRefIndex& OutRootObjectIndex) const
{
	if (!RootObjectRefHandle.IsValid())
	{
		return false;
	}

	const FInternalNetRefIndex ObjectIndex = NetRefHandleManager->GetInternalIndex(RootObjectRefHandle);
	if (ObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return false;
	}

	// 非法情况：传入的是 subobject。断言失败说明上层选错入口。
	checkf(NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex).SubObjectRootIndex == FNetRefHandleManager::InvalidInternalIndex, TEXT("Object %s (index:%u) (netref:%s) is not a rootobject"),
		*GetNameSafe(NetRefHandleManager->GetReplicatedObjectInstance(ObjectIndex)), ObjectIndex, ToCStr(RootObjectRefHandle.ToString()));

	OutRootObjectIndex = ObjectIndex;

	return true;
}

// 校验 handle 是 subobject（必须有 SubObjectRootIndex），同时返回 root + sub 的 InternalIndex。
bool FNetBlobManager::GetRootObjectAndSubObjectIndicesFromSubObjectHandle(FNetRefHandle SubObjectRefHandle, FInternalNetRefIndex& OutRootObjectIndex, FInternalNetRefIndex& OutSubObjectIndex) const
{
	if (!SubObjectRefHandle.IsValid())
	{
		return false;
	}

	const FInternalNetRefIndex ObjectIndex = NetRefHandleManager->GetInternalIndex(SubObjectRefHandle);
	if (ObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return false;
	}

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);

	checkf(ObjectData.SubObjectRootIndex != FNetRefHandleManager::InvalidInternalIndex, TEXT("SubObject %s (index:%u) (netref:%s) does not have a rootobject"),
		*GetNameSafe(NetRefHandleManager->GetReplicatedObjectInstance(ObjectIndex)), ObjectIndex, ToCStr(SubObjectRefHandle.ToString()));

	OutRootObjectIndex = ObjectData.SubObjectRootIndex;
	OutSubObjectIndex = ObjectIndex;

	return OutRootObjectIndex != FNetRefHandleManager::InvalidInternalIndex;
}

// 通用版：自动判别 handle 是 root 还是 subobject，并填两个 index（不是 sub 时 SubIndex = Invalid）。
bool FNetBlobManager::GetRootObjectAndSubObjectIndicesFromAnyHandle(FNetRefHandle AnyRefHandle, FInternalNetRefIndex& OutRootObjectIndex, FInternalNetRefIndex& OutSubObjectIndex) const
{
	if (!AnyRefHandle.IsValid())
	{
		return false;
	}

	const FInternalNetRefIndex ObjectIndex = NetRefHandleManager->GetInternalIndex(AnyRefHandle);
	if (ObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return false;
	}

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectIndex);
	// If it's a sub object
	if (ObjectData.SubObjectRootIndex != FNetRefHandleManager::InvalidInternalIndex)
	{
		OutRootObjectIndex = ObjectData.SubObjectRootIndex;
		OutSubObjectIndex = ObjectIndex;
	}
	// If it's a root object
	else
	{
		OutRootObjectIndex = ObjectIndex;
		OutSubObjectIndex = FNetRefHandleManager::InvalidInternalIndex;
	}

	return true;
}

// OOB 队列处理入口：仅处理 ScheduleAsOOBAttachmentQueue，输出立即发送的连接位图。
// 由 ReplicationSystem 在 PostTickDispatch 中调用。
void FNetBlobManager::ProcessOOBNetObjectAttachmentSendQueue(FNetBitArray& OutConnetionsPendingImmediateSend)
{
	AttachmentSendQueue.PrepareAndProcessOOBAttachmentQueue(Connections, NetRefHandleManager, OutConnetionsPendingImmediateSend);
}

// 普通队列处理入口：先 prepare（合并 OOB 残留 + scope 分类）→ ProcessQueue。
void FNetBlobManager::ProcessNetObjectAttachmentSendQueue(EProcessMode ProcessMode)
{
	AttachmentSendQueue.PrepareProcessQueue(Connections, NetRefHandleManager);
	AttachmentSendQueue.ProcessQueue(ProcessMode);
}

// 一帧的 SendUpdate 完成后清空队列与上下文。
void FNetBlobManager::ResetNetObjectAttachmentSendQueue()
{
	AttachmentSendQueue.ResetProcessQueue();
}

// 新连接广播给所有 handler；RemoveConnection 同理（当前所有 handler 重写为空）。
void FNetBlobManager::AddConnection(uint32 ConnectionId)
{
	BlobHandlerManager.AddConnection(ConnectionId);
}

void FNetBlobManager::RemoveConnection(uint32 ConnectionId)
{
	BlobHandlerManager.RemoveConnection(ConnectionId);
}

// ----------------------------------------------------------------------------------
// RegisterDefaultHandlers：注册三大基础 handler。
//   - NetRPCHandler                       ：RPC 编解码与执行。
//   - PartialNetObjectAttachmentHandler   ：把过大 attachment 切成多片 FPartialNetBlob。
//   - NetObjectBlobHandler                ：HugeObject 状态承载（仅切片，不直接 OnReceived）。
// 注册失败一般意味着 ini 配置缺失；非自动化测试下视为致命错误。
// ----------------------------------------------------------------------------------
void FNetBlobManager::RegisterDefaultHandlers()
{
	// NetRPCHandler
	{
		RPCHandler = TStrongObjectPtr<UNetRPCHandler>(NewObject<UNetRPCHandler>());
		RPCHandler->Init(*ReplicationSystem);
		if (!RegisterNetBlobHandler(RPCHandler.Get()))
		{
#if !WITH_AUTOMATION_WORKER
			checkf(false, TEXT("%s"), TEXT("Unable to register RPC handler. RPCs cannot be sent or received."));
#endif
			RPCHandler.Reset();
		}
	}

	// PartialNetObjectAttachmentHandler
	{
		// 取 Config CDO（含 MaxPartCount/MaxPartBitCount 等切片阈值）。
		PartialNetObjectAttachmentHandlerConfig = GetDefault<UPartialNetObjectAttachmentHandlerConfig>();
		FPartialNetObjectAttachmentHandlerInitParams InitParams = {};
		InitParams.ReplicationSystem  = ReplicationSystem;
		InitParams.Config = PartialNetObjectAttachmentHandlerConfig;
		PartialNetObjectAttachmentHandler = TStrongObjectPtr<UPartialNetObjectAttachmentHandler>(NewObject<UPartialNetObjectAttachmentHandler>());
		PartialNetObjectAttachmentHandler->Init(InitParams);
		if (!RegisterNetBlobHandler(PartialNetObjectAttachmentHandler.Get()))
		{
#if !WITH_AUTOMATION_WORKER
			checkf(false, TEXT("%s"), TEXT("Unable to register PartialNetObjectAttachment handler. Attachments cannot be split."));
#endif
			PartialNetObjectAttachmentHandler.Reset();
		}
	}

	// NetObjectBlobHandler
	{
		NetObjectBlobHandler = TStrongObjectPtr<UNetObjectBlobHandler>(NewObject<UNetObjectBlobHandler>());
		if (!RegisterNetBlobHandler(NetObjectBlobHandler.Get()))
		{
#if !WITH_AUTOMATION_WORKER
			checkf(false, TEXT("%s"), TEXT("Unable to register NetObjectBlobHandler handler. Replicated objects cannot be split."));
#endif
			NetObjectBlobHandler.Reset();
		}
	}
}

// ==================================================================================
// FNetObjectAttachmentSendQueue 实现
// ==================================================================================

// FNetObjectAttachmentQueue
FNetBlobManager::FNetObjectAttachmentSendQueue::FNetObjectAttachmentSendQueue()
: Manager(nullptr)
, bHasMulticastAttachments(false)
{
}

void FNetBlobManager::FNetObjectAttachmentSendQueue::Init(FNetBlobManager* InManager)
{
	Manager = InManager;
}

// 单播入队：根据 SendFlags 选择目标队列。
//   - 含 ScheduleAsOOB → 进 ScheduleAsOOBAttachmentQueue（立即发送）。
//   - 否则           → 进 AttachmentQueue（普通流）。
void FNetBlobManager::FNetObjectAttachmentSendQueue::Enqueue(uint32 ConnectionId, FInternalNetRefIndex OwnerIndex, FInternalNetRefIndex SubObjectIndex, const TRefCountPtr<FNetObjectAttachment>& Attachment, ENetObjectAttachmentSendPolicyFlags SendFlags)
{
	const bool bScheduleUsingOOBAttachmentQueue = EnumHasAnyFlags(SendFlags, ENetObjectAttachmentSendPolicyFlags::ScheduleAsOOB);
	FQueue& TargetQueue = bScheduleUsingOOBAttachmentQueue ? ScheduleAsOOBAttachmentQueue : AttachmentQueue;

	FNetObjectAttachmentQueueEntry& QueueEntry = TargetQueue.AddDefaulted_GetRef();
	QueueEntry.ConnectionId = ConnectionId;
	QueueEntry.OwnerIndex = OwnerIndex;
	QueueEntry.SubObjectIndex = SubObjectIndex;
	QueueEntry.SendFlags = SendFlags;
	QueueEntry.Attachment = Attachment;
}

// 多播入队：ConnectionId = 0 标记为多播；OpenConnections 位图记录入队时刻可用连接集，
// 防止处理时把消息发给"入队后才关闭"的连接。
void FNetBlobManager::FNetObjectAttachmentSendQueue::Enqueue(FInternalNetRefIndex OwnerIndex, FInternalNetRefIndex SubObjectIndex, const TRefCountPtr<FNetObjectAttachment>& Attachment, ENetObjectAttachmentSendPolicyFlags SendFlags, FNetBitArray OpenConnections)
{
	const bool bScheduleUsingOOBAttachmentQueue = EnumHasAnyFlags(SendFlags, ENetObjectAttachmentSendPolicyFlags::ScheduleAsOOB);
	FQueue& TargetQueue = bScheduleUsingOOBAttachmentQueue ? ScheduleAsOOBAttachmentQueue : AttachmentQueue;

	FNetObjectAttachmentQueueEntry& QueueEntry = TargetQueue.AddDefaulted_GetRef();
	QueueEntry.ConnectionId = 0;
	QueueEntry.OwnerIndex = OwnerIndex;
	QueueEntry.SubObjectIndex = SubObjectIndex;
	QueueEntry.SendFlags = SendFlags;
	QueueEntry.Attachment = Attachment;
	QueueEntry.MulticastConnections = MoveTemp(OpenConnections);

	bHasMulticastAttachments = true;
}

// OOB 路径准备 + 处理：仅处理 ScheduleAsOOBAttachmentQueue，输出 ConnectionsPendingImmediateSend。
//   - 仅以 ProcessObjectsInScope 模式处理（OOB 不区分 going-out-of-scope）。
//   - 处理完毕后清空 OOB 队列，并 reset 上下文。
void FNetBlobManager::FNetObjectAttachmentSendQueue::PrepareAndProcessOOBAttachmentQueue(FReplicationConnections* InConnections, const FNetRefHandleManager* InNetRefHandleManager, FNetBitArray& OutConnectionsPendingImmediateSend)
{
	// 不可重入：上次的 ProcessContext 必须已 Reset。
	check(!ProcessContext.IsValid());

	if (ProcessContext.IsValid())
	{
		return;
	}

	if (ScheduleAsOOBAttachmentQueue.Num() <= 0)
	{
		// OOB 队列为空：直接清空输出位图返回。
		OutConnectionsPendingImmediateSend.ClearAllBits();
		return;
	}

	// Init context to process OOBAttachmentQueue
	// 准备处理上下文：把 QueueToProcess 指向 OOB 队列；
	// 仅初始化 InScope 位图（OOB 路径不区分 out-of-scope）。
	ProcessContext.QueueToProcess = &ScheduleAsOOBAttachmentQueue;
	ProcessContext.Connections = InConnections;
	ProcessContext.NetRefHandleManager = InNetRefHandleManager;	
	ProcessContext.AttachmentsToObjectsInScope.Init(ScheduleAsOOBAttachmentQueue.Num());
	ProcessContext.ConnectionsPendingSendInPostDispatch.Init(InConnections->GetValidConnections().GetNumBits());

	// 多播条目存在 → 收集所有 valid 连接 ID 列表，便于 ProcessQueue 中遍历。
	if (bHasMulticastAttachments)
	{
		const FNetBitArray& ValidConnections = InConnections->GetValidConnections();
		const FNetBitArrayView ReplicatingConnections = MakeNetBitArrayView(ValidConnections);

		ProcessContext.ConnectionIds.SetNum(ReplicatingConnections.CountSetBits());
		if (!ProcessContext.ConnectionIds.IsEmpty())
		{
			ReplicatingConnections.GetSetBitIndices(0, ~0, ProcessContext.ConnectionIds.GetData(), ProcessContext.ConnectionIds.Num());
		}
	}

	// OOB 队列内全部条目都视为 in-scope，逐一置位。
	uint32 CurrentEntryIndex = 0U;
	for (const FNetObjectAttachmentQueueEntry& Entry : MakeArrayView(ScheduleAsOOBAttachmentQueue))
	{
		ProcessContext.AttachmentsToObjectsInScope.SetBit(CurrentEntryIndex);
		++CurrentEntryIndex;
	}

	ProcessQueue(EProcessMode::ProcessObjectsInScope);

	// 输出处理后需立即发包的连接位图。
	OutConnectionsPendingImmediateSend.InitAndCopy(ProcessContext.ConnectionsPendingSendInPostDispatch);

	// Reset Context
	// OOB 队列处理完毕后清空（条目已移交 ReplicationWriter）。
	ProcessContext.Reset();
	ScheduleAsOOBAttachmentQueue.Reset();	
}

// 普通路径准备：把上次未及时处理的 OOB 残留并入 AttachmentQueue → 按 scope 分类。
void FNetBlobManager::FNetObjectAttachmentSendQueue::PrepareProcessQueue(FReplicationConnections* InConnections, const FNetRefHandleManager* InNetRefHandleManager)
{
	if (ProcessContext.IsValid())
	{
		return;
	}

	// If we have entries in the ScheduleAsOOBAttachmentQueue that was not processed during PostTickDispatch (might have been posted after PostTickDispatch) we make sure to process them now.
	// 把 PostTickDispatch 之后又入队的 OOB 残余项并入普通队列（后到的 OOB 等同于普通发送）。
	AttachmentQueue.Append(ScheduleAsOOBAttachmentQueue);
	ScheduleAsOOBAttachmentQueue.Reset();

	if (AttachmentQueue.Num() <= 0)
	{
		return;
	}

	// Init context
	ProcessContext.QueueToProcess = &AttachmentQueue;
	ProcessContext.Connections = InConnections;
	ProcessContext.NetRefHandleManager = InNetRefHandleManager;	
	ProcessContext.AttachmentsToObjectsGoingOutOfScope.Init(AttachmentQueue.Num());
	ProcessContext.AttachmentsToObjectsInScope.Init(AttachmentQueue.Num());
	ProcessContext.ConnectionsPendingSendInPostDispatch.Init(InConnections->GetValidConnections().GetNumBits());

	if (bHasMulticastAttachments)
	{
		// 收集所有 valid 连接 ID。
		const FNetBitArray& ValidConnections = InConnections->GetValidConnections();
		const FNetBitArrayView ReplicatingConnections = MakeNetBitArrayView(ValidConnections);

		ProcessContext.ConnectionIds.SetNum(ReplicatingConnections.CountSetBits());
		if (!ProcessContext.ConnectionIds.IsEmpty())
		{
			ReplicatingConnections.GetSetBitIndices(0, ~0, ProcessContext.ConnectionIds.GetData(), ProcessContext.ConnectionIds.Num());
		}
	}

	if (Manager->AllowObjectReplication())
	{
		// Figure out if we have any attachments to objects going out of scope.
		// 利用 NetRefHandleManager 的"本帧 scope 集" vs "上一帧 scope 集"判定每条 attachment：
		//   - 本帧不在 scope 但上一帧在 → going out of scope（最后一次机会送可靠数据）。
		//   - 否则 → in scope（正常处理）。
		const FNetBitArrayView ScopableObjects = InNetRefHandleManager->GetCurrentFrameScopableInternalIndices();
		const FNetBitArrayView PrevScopableObjects = InNetRefHandleManager->GetPrevFrameScopableInternalIndices();

		uint32 CurrentEntryIndex = 0U;
		for (const FNetObjectAttachmentQueueEntry& Entry : MakeArrayView(AttachmentQueue))
		{
			// 取目标对象 InternalIndex：优先 subobject，否则 root。
			const uint32 TargetInternalObjectIndex = Entry.SubObjectIndex != FNetRefHandleManager::InvalidInternalIndex ? Entry.SubObjectIndex : Entry.OwnerIndex;

			const bool bIsAttachmentToObjectGoingOutOfScope = !ScopableObjects.GetBit(TargetInternalObjectIndex) && PrevScopableObjects.GetBit(TargetInternalObjectIndex);
			if (bIsAttachmentToObjectGoingOutOfScope)
			{
				ProcessContext.AttachmentsToObjectsGoingOutOfScope.SetBit(CurrentEntryIndex);
			}
			else
			{
				ProcessContext.AttachmentsToObjectsInScope.SetBit(CurrentEntryIndex);
			}
			++CurrentEntryIndex;
		}
	}
	else
	{
		// 不复制对象状态时不存在 scope 概念：所有条目一律视为 in-scope。
		uint32 CurrentEntryIndex = 0U;
		for (const FNetObjectAttachmentQueueEntry& Entry : MakeArrayView(AttachmentQueue))
		{
			ProcessContext.AttachmentsToObjectsInScope.SetBit(CurrentEntryIndex);
			++CurrentEntryIndex;
		}
	}
}

// 清空普通队列与上下文（一帧两次 ProcessQueue 调用之后调用）。
void FNetBlobManager::FNetObjectAttachmentSendQueue::ResetProcessQueue()
{
	// Clear queue
	AttachmentQueue.Reset();
	bHasMulticastAttachments = false;	
	ProcessContext.Reset();
}

// 是否对该对象（root 或 sub）仍有未处理的可靠 attachment。
// 用于"对象正待销毁但需保证可靠数据送达"的延迟判定。
bool FNetBlobManager::HasUnprocessedReliableAttachments(FInternalNetRefIndex InternalIndex) const
{
	return AttachmentSendQueue.HasUnprocessedReliableAttachments(InternalIndex);
}

bool FNetBlobManager::HasAnyUnprocessedReliableAttachments() const
{
	return AttachmentSendQueue.HasAnyUnprocessedReliableAttachments();
}

// 仅扫描 AttachmentQueue：可靠 attachment 不会进 OOB 队列（OOB 立即处理掉）。
bool FNetBlobManager::FNetObjectAttachmentSendQueue::HasUnprocessedReliableAttachments(FInternalNetRefIndex InternalIndex)  const
{
	// For the moment we only need to check the AttachmentQueue as reliable attachments are not schedules as immediate.
	return AttachmentQueue.ContainsByPredicate([&InternalIndex](const FNetObjectAttachmentQueueEntry& Entry) { return (Entry.OwnerIndex == InternalIndex || Entry.SubObjectIndex == InternalIndex) && Entry.Attachment->IsReliable();} );
}

bool FNetBlobManager::FNetObjectAttachmentSendQueue::HasAnyUnprocessedReliableAttachments()  const
{
	// For the moment we only need to check the AttachmentQueue as reliable attachments are not schedules as immediate.
	return AttachmentQueue.ContainsByPredicate([](const FNetObjectAttachmentQueueEntry& Entry) { return Entry.Attachment->IsReliable(); });
}

// ----------------------------------------------------------------------------------
// ProcessQueue：对处理上下文中筛选出的条目执行实际入队动作。
//   - 对每条 entry 调用 PreSerializeAndSplitNetBlob：若 attachment 过大则切成多个 PartialNetBlob。
//   - 多播：遍历 ConnectionIds → 跳过无 view（非可靠）/ 已关闭的连接 → 必要时按 connection 重做切片。
//   - 单播：直接送给目标连接的 ReplicationWriter->QueueNetObjectAttachments。
//   - 入队成功且带 SendInPostTickDispatch 标志 → 在 ConnectionsPendingSendInPostDispatch 中置位。
// ----------------------------------------------------------------------------------
void FNetBlobManager::FNetObjectAttachmentSendQueue::ProcessQueue(EProcessMode ProcessMode)
{
	if (!ProcessContext.IsValid())
	{
		return;
	}

	// 根据 ProcessMode 选择条目子集（in-scope 或 going-out-of-scope）。
	const FNetBitArray& IndicesToProcess = (ProcessMode == EProcessMode::ProcessObjectsGoingOutOfScope) ? ProcessContext.AttachmentsToObjectsGoingOutOfScope : ProcessContext.AttachmentsToObjectsInScope;

	TArray<TRefCountPtr<FNetBlob>> PartialNetBlobs;
	const FNetObjectAttachmentQueueEntry* Entries = ProcessContext.QueueToProcess->GetData();
	const uint32 NumEntries = ProcessContext.QueueToProcess->Num();

	// Verify that we have not missed to prepare the process context
	check(ProcessContext.IsValid() && IndicesToProcess.GetNumBits() == NumEntries);

	IndicesToProcess.ForAllSetBits([this, Entries, &PartialNetBlobs](uint32 Index)
	{
		const FNetObjectAttachmentQueueEntry& Entry = Entries[Index];

		// 复用临时数组：每条 entry 处理前清空。
		PartialNetBlobs.Reset();

		const TRefCountPtr<FNetObjectAttachment>& Attachment = Entry.Attachment;
		const FReplicationStateDescriptor* ReplicationStateDescriptor = Attachment->GetReplicationStateDescriptor();

		const bool bMulticast = Entry.ConnectionId == 0;
		// connection-specific 序列化（PerConnection NetSerializer）需要每个连接单独切片。
		const bool bHasConnectionSpecificSerialization = ReplicationStateDescriptor && EnumHasAnyFlags(ReplicationStateDescriptor->Traits, EReplicationStateTraits::HasConnectionSpecificSerialization);
		const bool bSendInPostTickDispatch = EnumHasAnyFlags(Entry.SendFlags, ENetObjectAttachmentSendPolicyFlags::SendInPostTickDispatch);

		// OOB 路径不能与对象状态打包（必须独立成包发送）；普通路径按 manager 配置决定。
		const bool bShouldSendAttachmentsWithObject = EnumHasAnyFlags(Entry.SendFlags, ENetObjectAttachmentSendPolicyFlags::ScheduleAsOOB) ? false : Manager->bSendAttachmentsWithObject;

		// 一次性切片：仅当不是 multicast + connection specific 时才在外层做（否则需按连接逐个切）。
		if (!(bMulticast && bHasConnectionSpecificSerialization) && !PreSerializeAndSplitNetBlob(Entry.ConnectionId, Attachment, PartialNetBlobs, bShouldSendAttachmentsWithObject))
		{
			checkf(false, TEXT("Unable to split %s NetObjectAttachment."), (EnumHasAnyFlags(Attachment->GetCreationInfo().Flags, ENetBlobFlags::Reliable) ? TEXT("reliable") : TEXT("unreliable")));
			return;
		}

		const bool bIsMultiPartAttachment = (PartialNetBlobs.Num() > 1);
		TArrayView<const TRefCountPtr<FNetBlob>> AttachmentsView = MakeArrayView(PartialNetBlobs);
		if (bMulticast)
		{
			// 多播切片告警：单条 multicast attachment 需要切片说明数据偏大，开发期应优化。
			if (bIsMultiPartAttachment)
			{
				UE_LOG(LogIris, Warning, TEXT("Splitting multicast net object attachment %s."), ReplicationStateDescriptor != nullptr && ReplicationStateDescriptor->DebugName ? ReplicationStateDescriptor->DebugName->Name : TEXT("Unknown"));
			}

			const FNetBlobCreationInfo& BlobCreationInfo = Attachment->GetCreationInfo();
			const bool bIsReliableRPC = EnumHasAnyFlags(BlobCreationInfo.Flags, ENetBlobFlags::Reliable);
			for (uint32 ConnectionId : MakeArrayView(ProcessContext.ConnectionIds))
			{
				// Objects won't be prioritized until there's a view so let's avoid queuing multicast attachments.
				// 不可靠 multicast：连接尚无 view（玩家未 spawn 完成）→ 跳过，避免堆积。
				if (!bIsReliableRPC && ProcessContext.Connections->GetReplicationView(ConnectionId).Views.Num() <= 0)
				{
					continue;
				}

				// Don't send RPCs to connections that were already closing when the RPC was called/queued
				// 入队时刻已不在 OpenConnections 集合 → 那时已在关闭流程，跳过。
				if (!Entry.MulticastConnections.IsBitSet(ConnectionId))
				{
					continue;
				}

				if (bHasConnectionSpecificSerialization)
				{
					// 每个连接独立切片（per-conn 序列化）：重做 PreSerializeAndSplitNetBlob。
					PartialNetBlobs.Reset();
					if (!PreSerializeAndSplitNetBlob(ConnectionId, Attachment, PartialNetBlobs, bShouldSendAttachmentsWithObject))
					{
						checkf(false, TEXT("Unable to split %s NetObjectAttachment with connection specific serialization."), (EnumHasAnyFlags(Attachment->GetCreationInfo().Flags, ENetBlobFlags::Reliable) ? TEXT("reliable") : TEXT("unreliable")));
						continue;
					}

					AttachmentsView = MakeArrayView(PartialNetBlobs);
				}

				FReplicationConnection* Connection = ProcessContext.Connections->GetConnection(ConnectionId);
				// We're only iterating over valid connections so the Connection pointer must be valid.
				// 真正提交给 ReplicationWriter：写入它的 attachment record，待下次 SendUpdate 时打包。
				const bool bWasEnqueued = Connection->ReplicationWriter->QueueNetObjectAttachments(Entry.OwnerIndex, Entry.SubObjectIndex, AttachmentsView, Entry.SendFlags);
				if (bWasEnqueued && bSendInPostTickDispatch)
				{
					ProcessContext.ConnectionsPendingSendInPostDispatch.SetBit(ConnectionId);
				}
			}
		}
		else
		{
			// 单播：直接发给 Entry.ConnectionId。
			if (FReplicationConnection* Connection = ProcessContext.Connections->GetConnection(Entry.ConnectionId))
			{
				const bool bWasEnqueued = Connection->ReplicationWriter->QueueNetObjectAttachments(Entry.OwnerIndex, Entry.SubObjectIndex, AttachmentsView, Entry.SendFlags);
				if (bWasEnqueued && bSendInPostTickDispatch)
				{
					ProcessContext.ConnectionsPendingSendInPostDispatch.SetBit(Entry.ConnectionId);
				}	
			}
		}
	});
}

// 切片入口：调用 PartialNetObjectAttachmentHandler 按 MTU/MaxPartBitCount 把 attachment 切成多片。
//   - handler 不可用时退化：把 attachment 自身视作一个完整 blob 直接发送。
//   - 同时执行预序列化以便后续按位拷贝（避免每次发送重新量化）。
bool FNetBlobManager::FNetObjectAttachmentSendQueue::PreSerializeAndSplitNetBlob(uint32 ConnectionId, const TRefCountPtr<FNetObjectAttachment>& Attachment, TArray<TRefCountPtr<FNetBlob>>& OutPartialNetBlobs, bool bInSendAttachmentsWithObject) const
{
	if (!Manager->PartialNetObjectAttachmentHandler.IsValid())
	{
		// 退化路径：当作单片直接发送。reinterpret_cast 利用 TRefCountPtr 布局兼容性。
		OutPartialNetBlobs.Add(reinterpret_cast<const TRefCountPtr<FNetBlob>&>(Attachment));
		return true;
	}

	return Manager->PartialNetObjectAttachmentHandler->PreSerializeAndSplitNetBlob(ConnectionId, Attachment, OutPartialNetBlobs, bInSendAttachmentsWithObject);
}

}
