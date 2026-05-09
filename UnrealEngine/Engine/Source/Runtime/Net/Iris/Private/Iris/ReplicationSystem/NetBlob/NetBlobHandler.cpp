// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobHandler.cpp —— UNetBlobHandler 抽象基类的最小实现。
// 只提供 typeId 默认值与连接钩子空实现；CreateNetBlob / OnNetBlobReceived
// 由派生类（NetRPCHandler / NetObjectBlobHandler / PartialNetObjectAttachmentHandler 等）实现。
// =====================================================================================

#include "Iris/ReplicationSystem/NetBlob/NetBlobHandler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetBlobHandler)

namespace UE::Net
{

// 网络层错误名：当 typeId 找不到对应 handler 或反序列化得不到合法 blob 时使用。
const FName GNetError_UnsupportedNetBlob("Unsupported NetBlob type");

}

// 默认 typeId 为 InvalidNetBlobType；FNetBlobHandlerManager::RegisterHandler 会改写。
UNetBlobHandler::UNetBlobHandler()
: NetBlobType(UE::Net::InvalidNetBlobType)
{
}

UNetBlobHandler::~UNetBlobHandler()
{
}

// 默认空实现。需要 per-conn 状态的 handler（如 NetRPCHandler）会重写：
//   - AddConnection   ：分配 per-conn 队列/缓存。
//   - RemoveConnection：清理资源。
void UNetBlobHandler::AddConnection(uint32 ConnectionId)
{
}

void UNetBlobHandler::RemoveConnection(uint32 ConnectionId)
{
}
