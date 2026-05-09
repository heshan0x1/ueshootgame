// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   ObjectReferenceCache 的“前向声明 + 跨模块共享小型结构体”头文件。
//   设计动机:
//     - FObjectReferenceCache 内部依赖大量 Iris 内部头(NetTokenStore / NetExportContext / ...),
//       直接 include 会引发循环依赖, 因此把 “只需要类型存在不需要类型完整定义” 的
//       前向声明集中放在本文件, 供 NetSerializer / Bridge / Filter 等使用。
//     - FNetObjectResolveContext 是“客户端解析对象引用” 时 把上下文(连接ID/远端 NetTokenStore 状态/
//       异步加载优先级/是否强制同步加载)透传给 ObjectReferenceCache 的最小信息体, 在
//       FObjectNetSerializer::Dequantize 等代码路径中被构建并传入 ResolveObjectReference()。
// =====================================================================================================

#pragma once

#include "Iris/IrisConstants.h"

// 复制系统主类(包含 ReplicationBridge / NetTokenStore 等)
class UReplicationSystem;
// Iris 默认 Bridge 实现, 负责对接 UObject 与 NetRefHandle
class UObjectReplicationBridge;

// 引擎 UObjectGlobals.h 中的同名 typedef, 在仅依赖前向声明时可以避免直接 include
// From UObjectGlobals.h
typedef int32 TAsyncLoadPriority;

// Forward declarations
namespace UE::Net
{
	// NetToken 远端状态(收到方持有的 token -> 字符串 映射快照)
	class FNetTokenStoreState;
	// 字符串 token 数据存储, 用于将路径字符串压缩成 NetToken 后再发送
	class FStringTokenStore;
}

namespace UE::Net::Private
{
	// 当前正在写入/读取的 batch 的导出上下文(已导出/待导出集合)
	class FNetExportContext;
	// 客户端尚未应用的批次队列容器
	class FNetPendingBatches;
}

// Definitions
namespace UE::Net
{
	/**
	 * “解析对象引用”时携带的上下文。
	 * - RemoteNetTokenStoreState: 远端连接持有的 NetTokenStoreState 快照, 用于把对端发过来的
	 *   FNetToken (path) 反查为字符串。
	 * - ConnectionId : 当前正在处理的连接 ID, 用于 PIE 路径重映射(RemapPathForPIE)。
	 * - AsyncLoadingPriority: 包级异步加载优先级 (从 must-be-mapped exports 中读出, 缺省 -1
	 *   是为了能 ensure 检测出忘记赋值的代码路径)。
	 * - bForceSyncLoad : 部分场景(如确定性回放/某些 RPC 解析) 必须同步阻塞加载。
	 */
	struct FNetObjectResolveContext
	{
		FNetTokenStoreState* RemoteNetTokenStoreState = nullptr;
		uint32 ConnectionId = InvalidConnectionId;
		// 默认置 -1 是为了让 “没有正确填充该字段” 的调用路径能在 ensure 上被发现
		TAsyncLoadPriority AsyncLoadingPriority = INDEX_NONE; // default to -1 to trap codepaths that are missing the assignment
		bool bForceSyncLoad = false;
	};
}
