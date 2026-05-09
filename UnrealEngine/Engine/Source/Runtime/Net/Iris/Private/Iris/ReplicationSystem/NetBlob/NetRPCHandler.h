// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NetRPCHandler.h —— Iris RPC 的 NetBlobHandler
// -----------------------------------------------------------------------------
// 角色：注册到 NetBlobHandlerManager 的"RPC 类型"handler。
//   * 发送侧：UReplicationSystem::SendRPC → FNetBlobManager → UNetRPCHandler::CreateRPC
//     构造 FNetRPC（Quantize 函数参数 + 收集 reference exports）→ 入 attachment 队列。
//   * 接收侧：FReplicationReader 反序列化出 FNetRPC → 调用本 handler 的
//     OnNetBlobReceived → FNetRPC::CallFunction → ProcessEvent 触发实际 UFunction。
// 配套：
//   - FNetRPCCallContext 是处理时给 FNetRPC 的上下文，包含底层序列化上下文与
//     ForwardNetRPCCallDelegate（外部 hook，可拦截/转发未执行前的 RPC）。
// =============================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/NetRPC.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandler.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"
#include "NetRPCHandler.generated.h"

namespace UE::Net
{

/**
 * 中文：FNetRPC 调用上下文，封装"序列化上下文 + ForwardNetRPCCallDelegate"。
 *  - NetContext 是底层 bitstream/InternalContext，CallFunction 内部用它解析 root/sub object。
 *  - ForwardNetRPCCallDelegate 是 ReplicationSystemInternal 持有的多播委托：
 *    在 UFunction 真正 ProcessEvent 之前，可被外部模块（如回放系统、anti-cheat）
 *    拦截或镜像调用。
 */
class FNetRPCCallContext
{
public:
	FNetRPCCallContext(FNetSerializationContext& NetContext, const FForwardNetRPCCallMulticastDelegate& ForwardNetRPCCallDelegate);

	FNetSerializationContext& GetNetSerializationContext();
	const FForwardNetRPCCallMulticastDelegate& GetForwardNetRPCCallDelegate() const;

private:
	FNetSerializationContext& NetContext;
	const FForwardNetRPCCallMulticastDelegate& ForwardNetRPCCallDelegate;
};

}

/**
 * 中文：UNetRPCHandler —— Iris RPC 专用 NetBlob handler。
 *   * CreateNetBlob：接收方根据 typeId 反查到本 handler，框架调用此函数 new 一个
 *     空的 FNetRPC 用于反序列化。
 *   * OnNetBlobReceived：反序列化完毕后框架回调 → 触发 RPC 执行。
 *   * CreateRPC：发送侧便利方法，Quantize 参数 + 设置 Reliable/Ordered 标志。
 */
UCLASS(transient, MinimalAPI)
class UNetRPCHandler final : public UNetBlobHandler
{
	GENERATED_BODY()

	using FNetRPC = UE::Net::Private::FNetRPC;

public:
	UNetRPCHandler();
	virtual ~UNetRPCHandler();

	// 中文：被 ReplicationSystemImpl 在系统初始化时调用，缓存 ReplicationSystem 指针。
	void Init(UReplicationSystem& ReplicationSystem);

	// 中文：发送侧入口。Function 标志位决定 ENetBlobFlags：
	//   - FUNC_NetReliable        → Reliable（进入 ReliableNetBlobQueue）
	//   - !FUNC_NetMulticast      → Ordered（unicast 与同对象其它 reliable/unicast RPC
	//                                       保持顺序；multicast 不要求顺序）
	TRefCountPtr<UE::Net::Private::FNetRPC> CreateRPC(const UE::Net::FNetObjectReference& ObjectReference, const UFunction* Function, const void* Parameters) const;

private:
	// 中文：UNetBlobHandler 接口 —— 反序列化时框架先用此函数构造空 FNetRPC，
	//       再让 FNetRPC::Deserialize 把字段读回。
	virtual TRefCountPtr<FNetBlob> CreateNetBlob(const FNetBlobCreationInfo&) const override;
	// 中文：UNetBlobHandler 接口 —— blob 反序列化完成后驱动 RPC 执行。
	virtual void OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& NetBlob) override;

	UReplicationSystem* ReplicationSystem = nullptr;
};

namespace UE::Net
{

inline FNetRPCCallContext::FNetRPCCallContext(FNetSerializationContext& InNetContext, const FForwardNetRPCCallMulticastDelegate& InForwardNetRPCCallDelegate)
: NetContext(InNetContext)
, ForwardNetRPCCallDelegate(InForwardNetRPCCallDelegate)
{
}

inline FNetSerializationContext& FNetRPCCallContext::GetNetSerializationContext()
{
	return NetContext;
}

inline const FForwardNetRPCCallMulticastDelegate& FNetRPCCallContext::GetForwardNetRPCCallDelegate() const
{
	return ForwardNetRPCCallDelegate;
}

}
