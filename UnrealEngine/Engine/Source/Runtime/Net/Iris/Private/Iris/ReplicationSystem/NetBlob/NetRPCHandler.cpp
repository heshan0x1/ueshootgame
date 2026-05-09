// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NetRPCHandler.cpp —— Iris RPC 的 NetBlobHandler 实现
// -----------------------------------------------------------------------------
// 主要承担三件事：
//   1) CreateRPC：将上层（UReplicationSystem::SendRPC）传入的 UFunction* + 参数
//      包成一个 FNetRPC，标志位由 UFunction 的 FUNC_NetReliable / FUNC_NetMulticast
//      推导（reliable 进 ReliableNetBlobQueue；非 multicast 加 Ordered 保证 unicast
//      之间顺序）。
//   2) CreateNetBlob：接收侧反序列化前框架先 new 一个空 FNetRPC 实例供其填充。
//   3) OnNetBlobReceived：反序列化完毕后回调 → 取出 ForwardNetRPCCallDelegate
//      → 构造 FNetRPCCallContext → FNetRPC::CallFunction 真正触发 ProcessEvent。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/NetRPCHandler.h"
#include "Iris/ReplicationSystem/NetBlob/NetRPC.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetRPCHandler)

UNetRPCHandler::UNetRPCHandler()
{
}

UNetRPCHandler::~UNetRPCHandler()
{
}

// 中文：缓存 ReplicationSystem 指针，OnNetBlobReceived 时通过它拿到
// ForwardNetRPCCallDelegate。
void UNetRPCHandler::Init(UReplicationSystem& InReplicationSystem)
{
	ReplicationSystem = &InReplicationSystem;
}

// 中文：发送侧便利构造。CreationInfo 的标志按 UFunction 元属性派生：
//   * FUNC_NetReliable → Reliable
//   * 非 FUNC_NetMulticast → Ordered（unicast 之间需保序，multicast 不需要）
//   FNetRPC::Create 内部完成参数 Quantize、reference 收集与 Locator 解析。
TRefCountPtr<UE::Net::Private::FNetRPC> UNetRPCHandler::CreateRPC(const UE::Net::FNetObjectReference& ObjectReference, const UFunction* Function, const void* Parameters) const
{
	FNetBlobCreationInfo CreationInfo;
	CreationInfo.Type = GetNetBlobType();
	CreationInfo.Flags = ((Function->FunctionFlags & FUNC_NetReliable) != 0) ? UE::Net::ENetBlobFlags::Reliable : UE::Net::ENetBlobFlags::None;
	// Unicast RPCs should be ordered with respect to other reliable and unicast RPCs.
	// 中文：unicast RPC（非 multicast）需要与同对象其他 reliable/unicast RPC 保序。
	if ((Function->FunctionFlags & FUNC_NetMulticast) == 0)
	{
		CreationInfo.Flags |= UE::Net::ENetBlobFlags::Ordered;
	}

	FNetRPC* RPC = FNetRPC::Create(ReplicationSystem, CreationInfo, ObjectReference, Function, Parameters);
	return RPC;
}

// 中文：接收侧——框架根据 typeId 找到本 handler 后调用此函数 new 出空壳，
// 后续 FNetRPC::Deserialize 会读 header/locator/payload 进行填充。
TRefCountPtr<UE::Net::FNetBlob> UNetRPCHandler::CreateNetBlob(const FNetBlobCreationInfo& CreationInfo) const
{
	FNetRPC* RPC = new FNetRPC(CreationInfo);
	return RPC;
}

// 中文：反序列化完成回调 —— 触发 RPC 执行。
//   * 取系统级 ForwardNetRPCCallDelegate（Hook 委托，外部可订阅做拦截）
//   * 构造 CallContext 后调用 FNetRPC::CallFunction：
//       - 校验对象/UFunction 仍存在、是 Net 函数；
//       - 校验 server/client 方向；
//       - Dequantize 参数 → 广播 ForwardNetRPCCallDelegate → ProcessEvent
void UNetRPCHandler::OnNetBlobReceived(UE::Net::FNetSerializationContext& NetContext, const TRefCountPtr<FNetBlob>& NetBlob)
{
	const UE::Net::FForwardNetRPCCallMulticastDelegate& ForwardNetRPCCallDelegate = ReplicationSystem->GetReplicationSystemInternal()->GetForwardNetRPCCallMulticastDelegate();
	UE::Net::FNetRPCCallContext CallContext(NetContext, ForwardNetRPCCallDelegate);
	FNetRPC* RPC = static_cast<FNetRPC*>(NetBlob.GetReference());
	RPC->CallFunction(CallContext);
}
