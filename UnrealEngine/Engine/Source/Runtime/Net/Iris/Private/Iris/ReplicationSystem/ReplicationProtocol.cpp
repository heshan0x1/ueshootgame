// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationProtocol.cpp —— FReplicationProtocol 引用计数实现
// -----------------------------------------------------------------------------
// 注意：当前实现是**非原子**自增/自减（依赖 Iris 自身的串行化保证：
//   • 创建/销毁 Protocol 由 FReplicationProtocolManager 在主线程完成；
//   • AddRef/Release 由 NetRefHandleManager 在 PreSendUpdate/PostSendUpdate 串行执行）。
// 若未来跨线程持有 Protocol，需切换到 FPlatformAtomics::InterlockedIncrement/Decrement。
// =============================================================================

#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "HAL/PlatformAtomics.h"

namespace UE::Net
{

// 增加引用：调用方 = NetObject 开始使用此协议（典型：StartReplication）
void FReplicationProtocol::AddRef() const
{
	++RefCount;
}

// 减少引用：调用方 = NetObject 停止使用此协议（典型：StopReplication / 析构）
// 当 RefCount 归零时，FReplicationProtocolManager::PruneProtocolsPendingDestroy 才会真正释放 Protocol。
void FReplicationProtocol::Release() const
{
	--RefCount;
}

}
