// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobHandlerManager.h —— 全局 typeId ↔ Handler 注册表 + 接收派发器。
//
// 职责：
//   1. 读 ini（UNetBlobHandlerDefinitions）按下标分配 typeId 槽位（Handlers 数组大小固定）。
//   2. 提供 RegisterHandler：按 ClassName 反查下标，写入 handler->NetBlobType。
//   3. 实现 INetBlobReceiver：
//      - CreateNetBlob(typeId)        ：用于接收端反序列化时根据 typeId 创建空 blob。
//      - OnNetBlobReceived(blob)      ：把已反序列化的 blob 派发到对应 handler。
//   4. 广播 AddConnection / RemoveConnection 到所有已注册 handler。
//
// 与 FNetBlobManager 关系：
//   - FNetBlobManager 内嵌一个 FNetBlobHandlerManager（BlobHandlerManager 字段），
//     在 Init 时调用 RegisterDefaultHandlers 自动注册三大默认 handler。
//   - 外部 handler（游戏自定义）通过 UReplicationSystem::RegisterNetBlobHandler 间接调用
//     FNetBlobManager::RegisterNetBlobHandler 进入这里。
// =====================================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandler.h"

namespace UE::Net::Private
{

// final：不打算被继承；是 ReplicationSystem 内部专用的 handler 注册表。
class FNetBlobHandlerManager final : public INetBlobReceiver
{
public:
	FNetBlobHandlerManager();

	// 按 UNetBlobHandlerDefinitions 数组大小预分配 Handlers 槽位（typeId 总数固定）。
	void Init();

	/** Returns true if the handler was successfully registered. */
	// 用 handler->GetClass()->GetName() 在 ini 列表中查找 → 命中则把下标作为
	// typeId 写入 handler，并把 handler 指针记录到 Handlers[Index]。
	// 失败原因：handler==nullptr / 已注册过 / ini 中找不到对应 ClassName。
	bool RegisterHandler(UNetBlobHandler* Handler);

	/** Creates a NetBlob of the specific type. */
	// 接收端：按 CreationInfo.Type 反查 handler，再调用其 CreateNetBlob。
	virtual TRefCountPtr<FNetBlob> CreateNetBlob(const FNetBlobCreationInfo&) const override;

	/** Calls the appropriate blob handler's OnNetBlobReceived() method. */
	// 把 blob 派发到对应 handler 的 OnNetBlobReceived（典型场景：执行 RPC、
	// 把 partial blob 喂给 Assembler、应用 huge object 状态等）。
	virtual void OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& Blob) override;

	/** Calls AddConnection on each registered handler. */
	// 新连接广播：让每个 handler 有机会建立 per-conn 状态。
	void AddConnection(uint32 ConnectionId) const;

	/** Calls RemoveConnection on each registered handler. */
	// 注：当前实现为空（注释见 cpp）。
	void RemoveConnection(uint32 ConnectionId);

private:
	// 按 typeId 索引的 handler 弱引用数组；TWeakObjectPtr 防止 GC 后悬挂。
	// 数组大小在 Init 时固定为 ini 中的条目数（即 typeId 总数）。
	TArray<TWeakObjectPtr<UNetBlobHandler>> Handlers;
};

}
