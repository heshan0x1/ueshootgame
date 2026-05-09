// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetRefHandleManagerTypes.h（Private）
// 角色：FNetRefHandleManager 内部用到的小型参数结构。
//
// 当前仅定义 FCreateNetObjectParams：
//   作为 Bridge 与 NetRefHandleManager::CreateNetObject(...) 之间的参数包，
//   集中描述"创建一个本地 NetObject 所需的最小信息"——
//     * NetFactoryId          : 决定客户端如何重建对象（Actor / Component / 自定义 Factory）
//     * IrisAsyncLoadingPriority : 该对象引用的资源 async load 优先级
//     * ReplicationProtocol      : 对象绑定的不可变协议（共享形态描述）
// =====================================================================================

#pragma once

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Iris/ReplicationSystem/ObjectReferenceTypes.h"

namespace UE::Net
{
	struct FReplicationProtocol;
}

namespace UE::Net::Private
{

	/** Holds important parameters needed to create a NetObject */
	/**
	 * NetRefHandleManager 创建一个 NetObject 时所需的最小参数集。
	 * 由 Bridge / Factory 在创建路径上构造并传入。
	 */
	struct FCreateNetObjectParams
	{
		FNetObjectFactoryId NetFactoryId = InvalidNetObjectFactoryId;
		EIrisAsyncLoadingPriority IrisAsyncLoadingPriority = EIrisAsyncLoadingPriority::Default;
		const UE::Net::FReplicationProtocol* ReplicationProtocol = nullptr;
	};

}