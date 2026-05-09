// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationSystemDelegates.h
// 角色：ReplicationSystem 的"系统级多播事件"集合。当前对外提供两个：
//          * OnConnectionAdded   连接加入（仅父连接）
//          * OnConnectionRemoved 连接移除（仅父连接）
//
// 用法：
//   通过 UReplicationSystem::GetDelegates() 访问该容器，然后在其中按需注册回调；
//   实际广播者是 FReplicationSystemImpl（友元访问私有委托对象）。
// 备注：
//   仅对"父连接（parent connection）"触发，子连接（split-screen 等）不会回调。
// =====================================================================================

#pragma once

#include "HAL/Platform.h"
#include "Delegates/Delegate.h"
#include "Net/Core/Connection/ConnectionHandle.h"

namespace UE::Net::Private
{
	class FReplicationSystemImpl;
}

namespace UE::Net
{

/** ReplicationSystem 暴露的多播委托集合。 */
class FReplicationSystemDelegates
{
public:
	using FConnectionAddedDelegate = TMulticastDelegate<void(FConnectionHandle ConnectionHandle)>;
	using FConnectionRemovedDelegate = TMulticastDelegate<void(FConnectionHandle ConnectionHandle)>;

	/** 
	 * Returns a delegate registration instance allowing the caller to register their FConnectionAddedDelegate. 
	 * The delegate will be called when a valid and not previously added connection is registered via a FReplicationSystem::AddConnection call.
	 * Currently only parent connections will call the delegates.
	 */
	/**
	 * 取得"连接加入"事件的注册接口（不会暴露 Broadcast 接口，避免外部误调）。
	 * 仅在新连接通过 UReplicationSystem::AddConnection 加入时触发，仅父连接广播。
	 */
	FConnectionAddedDelegate::RegistrationType& OnConnectionAdded(); 

	/**
	 * Returns a delegate registration instance allowing the caller to register their FConnectionRemovedDelegate.
	 * The delegate will be called when a previously successfully added connection is removed via a FReplicationSystem::RemoveConnection call. 
	 * Currently only parent connections will call the delegates.
	 */
	/**
	 * 取得"连接移除"事件的注册接口；UReplicationSystem::RemoveConnection 时触发，仅父连接。
	 */
	FConnectionAddedDelegate::RegistrationType& OnConnectionRemoved(); 

private:
	friend UE::Net::Private::FReplicationSystemImpl;

	FConnectionAddedDelegate ConnectionAddedDelegate;     // 由 Impl 内部 Broadcast
	FConnectionRemovedDelegate ConnectionRemovedDelegate; // 由 Impl 内部 Broadcast
};

inline FReplicationSystemDelegates::FConnectionAddedDelegate::RegistrationType& FReplicationSystemDelegates::OnConnectionAdded()
{
	return ConnectionAddedDelegate;
}

inline FReplicationSystemDelegates::FConnectionRemovedDelegate::RegistrationType& FReplicationSystemDelegates::OnConnectionRemoved()
{
	return ConnectionRemovedDelegate;
}

}
