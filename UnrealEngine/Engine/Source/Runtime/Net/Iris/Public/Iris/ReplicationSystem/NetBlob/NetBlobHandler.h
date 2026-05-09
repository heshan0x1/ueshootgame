// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobHandler.h —— NetBlob 类型注册器抽象基类。
//
// 设计动机：
//   FNetBlob 仅是数据载体；每种 blob 类型都需要一段"如何创建实例 + 如何接收处理"的
//   逻辑。这段逻辑被建模为 UNetBlobHandler 派生类（UCLASS，便于 ini 配置 + 反射）。
//   每个 handler 在 FNetBlobHandlerManager 中按 ini 顺序登记，分配到唯一 typeId。
//
// 双侧对称：
//   发送端：调用 UNetBlobHandler::CreateNetBlob(flags) → 通过 NetBlobType 反查发送
//           对应类型实例。
//   接收端：FReplicationReader 反序列化 CreationInfo 后用 typeId 反查 handler，
//           调用 handler 的虚函数 CreateNetBlob 创建空 blob，再 Deserialize，最后
//           OnNetBlobReceived 派发到游戏逻辑。
//
// 典型派生：
//   - UNetRPCHandler                        ：处理 FNetRPC（RPC 调用载荷）
//   - UNetObjectBlobHandler                 ：处理 FNetObjectBlob（巨型对象状态）
//   - UPartialNetObjectAttachmentHandler    ：把超长 blob 切片为 FPartialNetBlob
//   - USequentialPartialNetBlobHandler      ：按序组装 HugeObject
// =====================================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Templates/RefCounting.h"
#include "NetBlobHandler.generated.h"

namespace UE::Net::Private
{
	class FNetBlobHandlerManager;
}

namespace UE::Net
{

// 网络错误：blob typeId 找不到对应 handler 或 handler 不存在时使用。
IRISCORE_API extern const FName GNetError_UnsupportedNetBlob;

}

/** Interface for being able to receive a NetBlob and forward it to the appropriate UNetBlobHandler. */
// 抽象接收器接口：FNetBlobHandlerManager 与 UNetBlobHandler 都实现它。
//   - CreateNetBlob       ：按 typeId 创建对应派生类的空 blob 实例（用于反序列化）。
//   - OnNetBlobReceived   ：blob 反序列化后被调用，负责派发到游戏逻辑或重组流程。
class INetBlobReceiver
{
protected:
	using FNetBlobCreationInfo = UE::Net::FNetBlobCreationInfo;
	using FNetBlob = UE::Net::FNetBlob;

public:
	virtual TRefCountPtr<UE::Net::FNetBlob> CreateNetBlob(const FNetBlobCreationInfo&) const = 0;
	virtual void OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>&) = 0;

};

/**
 * A UNetBlobHandler is responsible for creating and processing a single type of NetBlob.
 * If the handler should be able to receive blobs it needs to be configured in
 * UNetBlobHandlerDefinitions and registered to the UReplicationSystem on both the
 * sending and receiving side.
 * @see UReplicationSystem::RegisterNetBlobHandler
 * @note Certain handlers such as NetRPCHandler, PartialNetObjectAttachmentHandler and NetObjectBlobHandler will be registered automatically.
 */
// UNetBlobHandler：负责"一种"NetBlob 的创建与接收处理。
// 注册要点：
//   1. 必须在 [/Script/IrisCore.NetBlobHandlerDefinitions] ini 段中按 ClassName 列出，
//      且发送端与接收端 ini 顺序必须一致（顺序即 typeId）。
//   2. 在 UReplicationSystem 上调用 RegisterNetBlobHandler 注册后才会被赋予 typeId。
//   3. NetRPCHandler / PartialNetObjectAttachmentHandler / NetObjectBlobHandler
//      由 FNetBlobManager::RegisterDefaultHandlers 自动注册。
UCLASS(transient, MinimalApi, Abstract)
class UNetBlobHandler : public UObject, public INetBlobReceiver
{
	GENERATED_BODY()

public:
	virtual ~UNetBlobHandler();

	/** Create a blob that the handler can process. Forwards to the virtual CreateNetBlob. */
	// 便利包装：根据外部 flags 构造 FNetBlobCreationInfo，使用本 handler 的 typeId
	// 调用虚函数 CreateNetBlob 生成 blob 实例。
	TRefCountPtr<FNetBlob> CreateNetBlob(UE::Net::ENetBlobFlags Flags) const;

	/** Get the net blob type. The blob type is determined at runtime and can differ from run to run. */
	// 运行期 typeId（由 FNetBlobHandlerManager 在 RegisterHandler 时按 ini 索引分配）。
	UE::Net::FNetBlobType GetNetBlobType() const { return NetBlobType; }

	/** Called when a connection is added. For handler specific connection handling. */
	// 新连接事件：派生 handler 可重写以维护 per-conn 状态（如 NetRPCHandler 的 RPC 队列）。
	IRISCORE_API virtual void AddConnection(uint32 ConnectionId);

	/** Called when a connection is removed. For handler specific connection handling. */
	IRISCORE_API virtual void RemoveConnection(uint32 ConnectionId);

protected:
	UNetBlobHandler();	

private:
	// INetBlobReceiver

	/** Override to create the NetBlob. This will be called when receiving a NetBlob of the type this handler is responsible for. */
	// 纯虚：派生类必须实现，按 CreationInfo 创建对应派生类型的空 blob 用于反序列化。
	virtual TRefCountPtr<FNetBlob> CreateNetBlob(const FNetBlobCreationInfo&) const override PURE_VIRTUAL(CreateNetBlob, return nullptr;);

	/** Override to process the NetBlob when it's received. */
	// 纯虚：派生类必须实现，处理已反序列化的 blob（通常派发到游戏逻辑或加入重组队列）。
	virtual void OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>&) override PURE_VIRTUAL(OnNetBlobReceived,);

private:
	// FNetBlobHandlerManager 在 RegisterHandler 中需要写入 NetBlobType。
	friend class UE::Net::Private::FNetBlobHandlerManager;

	/** The type is assigned by the FNetBlobHandlerManager when the handler is created. */
	// 运行期 typeId；初值为 InvalidNetBlobType，注册后会被改写为 ini 中的索引。
	UE::Net::FNetBlobType NetBlobType;
};

// 便利重载：仅传 flags 的入口，自动填入本 handler 的 typeId。
inline TRefCountPtr<UE::Net::FNetBlob> UNetBlobHandler::CreateNetBlob(UE::Net::ENetBlobFlags Flags) const
{
	FNetBlobCreationInfo CreationInfo;
	CreationInfo.Flags = Flags;
	CreationInfo.Type = GetNetBlobType();
	return CreateNetBlob(CreationInfo);
}
