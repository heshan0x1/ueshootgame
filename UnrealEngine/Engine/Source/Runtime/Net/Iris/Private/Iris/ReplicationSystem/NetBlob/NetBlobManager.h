// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobManager.h —— Iris RPC / 附件 / HugeObject 的总入队中枢。
//
// 总览：
//   FNetBlobManager 是 ReplicationSystem 内的一个子系统（Internal 持有），负责：
//     1) 默认 handler 注册：NetRPCHandler / PartialNetObjectAttachmentHandler /
//        NetObjectBlobHandler；
//     2) 对外 RPC 入口：SendMulticastRPC / SendUnicastRPC；
//     3) 对外通用附件入口：QueueNetObjectAttachment；
//     4) 一帧内的发送队列处理：ProcessNetObjectAttachmentSendQueue（普通流）+
//        ProcessOOBNetObjectAttachmentSendQueue（OOB 立即发送流）。
//
// 三种 Send Queue 的语义（FNetObjectAttachmentSendQueue）：
//   ┌────────────────────────────────────────────────────────────────────────────┐
//   │ Normal AttachmentQueue            │ OOB(ScheduleAsOOB)队列      │ Multicast │
//   ├────────────────────────────────────────────────────────────────────────────┤
//   │ 默认 Send 路径，参与正常 Tick 流：│ 立即发送：当前帧 PostTick    │ 同上两种   │
//   │ PrepareProcessQueue + ProcessQueue│ Dispatch 阶段被推送，绕过常 │ 队列均支持 │
//   │ 在 PostUpdate 期间排空。           │ 规优先级路径。              │ 多播复制   │
//   │ 区分对象 in scope / out of scope。│                              │           │
//   └────────────────────────────────────────────────────────────────────────────┘
//   "HugeObject" 路径并不在这里另起队列：它通过 NetObjectBlobHandler + 切片 handler
//   生成 FPartialNetBlob，然后走 ReplicationWriter 的 batch attachment 机制入帧。
//
// ENetObjectAttachmentSendPolicyFlags：
//   - None               ：默认走普通队列；下一帧 PostUpdate 排空。
//   - ScheduleAsOOB      ：进 OOB 队列；ProcessOOBNetObjectAttachmentSendQueue 立即发送。
//   - SendInPostTickDispatch：发送后把 ConnectionId 写入 PendingSendInPostDispatch
//     位图，让连接尽快 flush 到网络层（在下一 Dispatch 时立即发包）。
//
// 与 ReplicationWriter / Filtering 协作：
//   - 入队后由 Connection->ReplicationWriter->QueueNetObjectAttachments 接管，
//     attachment 才会真正进入 packet writer 的 attachment record。
//   - 多播路径需要遍历 Connections->GetReplicationView（被 Filtering 决定的可见连接）；
//     不可靠 multicast 在没有 view 的连接上会被静默丢弃，避免堆积。
// =====================================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandlerManager.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h" // For FInternalNetRefIndex
#include "UObject/StrongObjectPtr.h"

class UNetBlobHandler;
class UNetObjectBlobHandler;
class UPartialNetObjectAttachmentHandler;
class UPartialNetObjectAttachmentHandlerConfig;
class UNetRPCHandler;
namespace UE::Net
{
	class FNetObjectReference;

	namespace Private
	{
		class FNetRefHandleManager;
		class FObjectReferenceCache;
		class FReplicationConnections;
	}
}

namespace UE::Net::Private
{

// 初始化参数：
//   - ReplicationSystem            ：拥有此 Manager 的系统门面，用于反查 Internal/Connections 等。
//   - bSendAttachmentsWithObject   ：true 时尝试与对象状态打包到同一 batch；false 走独立 attachment record。
struct FNetBlobManagerInitParams
{
	UReplicationSystem* ReplicationSystem = nullptr;
	bool bSendAttachmentsWithObject = false;
};

class FNetBlobManager
{
public:
	FNetBlobManager();
	
	// 初始化：固化各项依赖指针、注册三大默认 handler、初始化 AttachmentSendQueue。
	void Init(FNetBlobManagerInitParams& InitParams);

	// 注册外部 handler（典型来自游戏代码）：转发到内部 BlobHandlerManager。
	bool RegisterNetBlobHandler(UNetBlobHandler* Handler);

	// 是否允许 Object 复制：用于 PrepareProcessQueue 中决定是否做 in-scope/out-of-scope 区分。
	bool AllowObjectReplication() const { return bAllowObjectReplication; }

	// 通用附件入队（非 RPC）：游戏代码通过 UReplicationSystem::QueueNetObjectAttachment 走到这里。
	//   - 自动解析 TargetRef 的 root/sub 对象 InternalIndex；如果 Target 不是被复制的对象，
	//     则尝试用 ReplicatedOuter 兜底。
	//   - 解析失败 → 记录警告并返回 false（调用方决定是否重试）。
	bool QueueNetObjectAttachment(uint32 ConnectionId, const FNetObjectReference& TargetRef, const TRefCountPtr<FNetObjectAttachment>& Attachment, ENetObjectAttachmentSendPolicyFlags SendFlags = ENetObjectAttachmentSendPolicyFlags::None);

	// SendRPC 上下文：调用者 + 子对象 + UFunction。
	struct FSendRPCContext
	{
		// 真正持有 RPC 的根对象（必须是已复制对象）。
		const UObject* RootObject = nullptr;
		// 触发 RPC 的子对象，可为空（此时 RPC 由 RootObject 直接发出）。
		const UObject* SubObject = nullptr;
		// UFunction：用其 FunctionFlags 校验方向（NetServer/NetClient/NetMulticast）。
		const UFunction* Function = nullptr;
	};

	// Multicast RPC
	// 多播 RPC：服务端 → 全部已 view 的客户端。
	// 流程：方向校验 → GetRPCOwner 解析 root/sub → CreateRPC → Enqueue(multicast 形式)。
	bool SendMulticastRPC(const FSendRPCContext& Context, const void* Parameters, ENetObjectAttachmentSendPolicyFlags SendFlags = ENetObjectAttachmentSendPolicyFlags::None);

	// Unicast RPC
	// 单播 RPC：定向到指定 ConnectionId（客户端 → 服务端常用，或服务端 → 单一客户端）。
	bool SendUnicastRPC(uint32 ConnectionId, const FSendRPCContext& Context, const void* Parameters, ENetObjectAttachmentSendPolicyFlags SendFlags = ENetObjectAttachmentSendPolicyFlags::None);

	// 是否还有针对该对象（root 或 subobject）的可靠 attachment 未处理。
	// 用于"对象退出 scope 但仍有可靠数据要送达"的延迟销毁判定。
	bool HasUnprocessedReliableAttachments(FInternalNetRefIndex InternalIndex) const;
	bool HasAnyUnprocessedReliableAttachments() const;

	// 处理模式：决定本次 ProcessQueue 走 in-scope 集还是 going-out-of-scope 集。
	//   - ProcessObjectsGoingOutOfScope：仅处理 attachment 目标对象本帧不再 scope 的条目；
	//     用于在对象消失前把残余可靠 attachment 一次性送出。
	//   - ProcessObjectsInScope        ：处理目标对象仍在 scope 的条目（绝大多数）。
	enum class EProcessMode 
	{
		ProcessObjectsGoingOutOfScope,
		ProcessObjectsInScope,
	};

	// 返回上次处理后需要立即发包的连接位图（SendInPostTickDispatch 命中）。
	FNetBitArrayView GetConnectionsPendingImmediateSend() const;

	// OOB 队列处理：仅排空 ScheduleAsOOBAttachmentQueue，输出需要立即 flush 的 ConnectionIds。
	// 通常在 PostTickDispatch 中被调用，以便在帧末尾立即把 OOB attachment 发出。
	void ProcessOOBNetObjectAttachmentSendQueue(FNetBitArray& OutConnectionsPendingImmediateSend);
	// 普通队列处理：把上次未及时处理的 OOB 条目并入 AttachmentQueue 一起排空。
	// ProcessMode 决定本次走 in-scope 还是 going-out-of-scope。
	void ProcessNetObjectAttachmentSendQueue(EProcessMode ProcessMode);
	// 清空当前帧的处理上下文与 AttachmentQueue。
	void ResetNetObjectAttachmentSendQueue();

	FNetBlobHandlerManager& GetNetBlobHandlerManager() { return BlobHandlerManager; }
	const FNetBlobHandlerManager& GetNetBlobHandlerManager() const { return BlobHandlerManager; }

	// 默认 handler 访问器：供 ReplicationWriter / ReplicationReader 在切分 / 重组流程中使用。
	const UPartialNetObjectAttachmentHandler* GetPartialNetObjectAttachmentHandler() const { return PartialNetObjectAttachmentHandler.Get(); }
	const UNetObjectBlobHandler* GetNetObjectBlobHandler() const { return NetObjectBlobHandler.Get(); }

	// Connection handling
	// 转发到 BlobHandlerManager 广播给所有 handler。
	void AddConnection(uint32 ConnectionId);
	void RemoveConnection(uint32 ConnectionId);

private:

	// 注册 NetRPCHandler / PartialNetObjectAttachmentHandler / NetObjectBlobHandler 三大默认 handler。
	// 三者均为强引用持有（TStrongObjectPtr），保证 GC 不会释放。
	void RegisterDefaultHandlers();

	// RPC 主体定位结果：CallerRef 是承载 RPC 的对象（root），TargetRef 是真正应用 RPC 的对象。
	struct FRPCOwner
	{
		// The replicated object responsible for carrying (sending) the RPC.
		// 负责承载该 RPC 的复制对象（一定是已复制对象）。
		FNetObjectReference CallerRef;

		// The object the RPC will be applied to.
		// RPC 真正作用的对象（可能与 CallerRef 不同，如非复制 subobject）。
		FNetObjectReference TargetRef;

		FInternalNetRefIndex RootObjectIndex = FNetRefHandleManager::InvalidInternalIndex;
		FInternalNetRefIndex SubObjectIndex = FNetRefHandleManager::InvalidInternalIndex;
	};
	// 解析 RPC 的 owner：
	//   - root 直接发：CallerRef == TargetRef = RootObject。
	//   - subobject 发：若 sub 已复制，CallerRef == TargetRef = SubObject；
	//     否则 fallback 用 RootObject 作 caller，subobject 作 target。
	bool GetRPCOwner(FRPCOwner& OutOwnerInfo, const FSendRPCContext& Context) const;

	/** 
	* Validates that RootObjectRefHandle is a true root object and return it's index
	* @return True if the root object handle is valid and has been replicated.
	*/
	// 校验：参数是否真为 root（无 SubObjectRootIndex），并返回其 InternalIndex。
	bool GetRootObjectIndicesFromHandle(FNetRefHandle RootObjectRefHandle, FInternalNetRefIndex& OutRootObjectIndex) const;

	/**
	* Validates that the SubObjectRefHandle is a true subobject and return it's index and the index of it's root object.
	* @return True if the subobject handle is valid and has been replicated.
	*/
	// 校验：参数是否真为 subobject（必须有 SubObjectRootIndex），并同时返回 root 与 sub 的 InternalIndex。
	bool GetRootObjectAndSubObjectIndicesFromSubObjectHandle(FNetRefHandle SubObjectRefHandle, FInternalNetRefIndex& OutRootObjectIndex, FInternalNetRefIndex& OutSubObjectIndex) const;

	/**
	 * Receives the handle of a root object or a sub object and returns the index of the root object and subobject if there is one.
	 * @return True if the object handle is valid has been replicated.
	 */
	// 通用版本：传入任意 handle，自动判别 root / subobject 并填两个 index。
	bool GetRootObjectAndSubObjectIndicesFromAnyHandle(FNetRefHandle AnyRefHandle, FInternalNetRefIndex& OutRootObjectIndex, FInternalNetRefIndex& OutSubObjectIndex) const;

	// ----------------------------------------------------------------------------------
	// 内部发送队列：管理 Normal / OOB / Multicast 三类条目。
	// ----------------------------------------------------------------------------------
	class FNetObjectAttachmentSendQueue
	{
	public:
		FNetObjectAttachmentSendQueue();

		void Init(FNetBlobManager* Manager);

		// Unicast
		// 单播入队：根据 SendFlags 写入 AttachmentQueue 或 ScheduleAsOOBAttachmentQueue。
		void Enqueue(uint32 ConnectionId, FInternalNetRefIndex OwnerIndex, FInternalNetRefIndex SubObjectIndex, const TRefCountPtr<FNetObjectAttachment>& Attachment, ENetObjectAttachmentSendPolicyFlags SendFlags);

		// Multicast
		// 多播入队：ConnectionId 留空（=0），OpenConnections 位图记录入队时刻可见的连接集；
		// ProcessQueue 时遍历该集合 + 当前 ConnectionIds 求交，避免发送给已关闭连接。
		void Enqueue(FInternalNetRefIndex OwnerIndex, FInternalNetRefIndex SubObjectIndex, const TRefCountPtr<FNetObjectAttachment>& Attachment, ENetObjectAttachmentSendPolicyFlags SendFlags, FNetBitArray OpenConnections);

		// 普通流程准备：把 OOB 队列遗留项并入 Attachment 队列；按对象 scope 分类条目。
		void PrepareProcessQueue(FReplicationConnections* InConnections, const FNetRefHandleManager* InNetRefHandleManager);
		// 真正执行：按 ProcessMode 选择处理 in-scope/out-of-scope 子集，调用 ReplicationWriter 入队。
		void ProcessQueue(EProcessMode ProcessMode);
		// 清空队列与处理上下文。
		void ResetProcessQueue();
		// OOB 专用：仅排空 ScheduleAsOOBAttachmentQueue，并输出需要立即发包的连接位图。
		void PrepareAndProcessOOBAttachmentQueue(FReplicationConnections* InConnections, const FNetRefHandleManager* InNetRefHandleManager, FNetBitArray& OutConnetionsPendingImmediateSend);

		bool HasUnprocessedReliableAttachments(FInternalNetRefIndex InternalIndex)  const;
		bool HasAnyUnprocessedReliableAttachments()  const;
	
	private:
		// 单条入队记录。
		struct FNetObjectAttachmentQueueEntry
		{
			// 0 表示 multicast；> 0 表示 unicast 目标连接。
			uint32 ConnectionId;
			FInternalNetRefIndex OwnerIndex;
			FInternalNetRefIndex SubObjectIndex;
			ENetObjectAttachmentSendPolicyFlags SendFlags;
			TRefCountPtr<FNetObjectAttachment> Attachment;
			// 仅 multicast 使用：入队时刻处于 open 状态的连接集合（防止发送给已关闭/正在关闭的连接）。
			FNetBitArray MulticastConnections;
		};
		typedef TArray<FNetObjectAttachmentQueueEntry> FQueue;

		// 预序列化 + 切片：调用 PartialNetObjectAttachmentHandler 把单个 attachment 切成
		// 多个 FPartialNetBlob 输出到 OutPartialNetBlobs；对每个连接独立调用以处理
		// connection-specific serialization。
		bool PreSerializeAndSplitNetBlob(uint32 ConnectionId, const TRefCountPtr<FNetObjectAttachment>& Attachment, TArray<TRefCountPtr<FNetBlob>>& OutPartialNetBlobs, bool bInSendAttachmentsWithObject) const;

		FNetBlobManager* Manager;
		// 普通发送队列：当帧 PostUpdate 流程排空。
		FQueue AttachmentQueue;
		// OOB 立即发送队列：在 PostTickDispatch 阶段独立排空。
		FQueue ScheduleAsOOBAttachmentQueue;		
		// 标记当前队列中是否含有 multicast 条目（决定是否需要枚举 ConnectionIds）。
		bool bHasMulticastAttachments;

		// 一次 Process 的瞬时上下文（PrepareProcessQueue 填入 → ProcessQueue 消费 → ResetProcessQueue 复位）。
		struct FProcessQueueContext
		{
			// 第 i 位 = 第 i 条目的 attachment 指向的对象本帧"刚出 scope"。
			FNetBitArray AttachmentsToObjectsGoingOutOfScope;
			// 第 i 位 = 第 i 条目的 attachment 指向的对象本帧仍 in scope。
			FNetBitArray AttachmentsToObjectsInScope;
			// 入队完成后需要在 PostDispatch 阶段立即发包的 ConnectionIds 位图。
			FNetBitArray ConnectionsPendingSendInPostDispatch;

			// 当前所有 valid 连接的 id 列表（multicast 时遍历此列表）。
			TArray<uint32> ConnectionIds;
			FReplicationConnections* Connections = nullptr;
			const FNetRefHandleManager* NetRefHandleManager = nullptr;
			// 指向本次要处理的队列（AttachmentQueue 或 ScheduleAsOOBAttachmentQueue）。
			FQueue* QueueToProcess = nullptr;

			void Reset()
			{
				Connections = nullptr;
				NetRefHandleManager = nullptr;
				QueueToProcess = nullptr;
				ConnectionsPendingSendInPostDispatch.ClearAllBits();
			}

			bool IsValid() const { return NetRefHandleManager != nullptr; }
		};
		FProcessQueueContext ProcessContext;
	};


	// ---------------------------------------------------------------------------------
	// 字段
	// ---------------------------------------------------------------------------------

	// typeId ↔ Handler 注册表，包含全部 NetBlob 类型。
	FNetBlobHandlerManager BlobHandlerManager;
	// 单一 attachment 队列实例：内部进一步区分普通 / OOB 两条 sub-queue。
	FNetObjectAttachmentSendQueue AttachmentSendQueue;
	// 三大默认 handler：强引用确保 GC 不会回收。
	TStrongObjectPtr<UNetRPCHandler> RPCHandler;
	TStrongObjectPtr<UPartialNetObjectAttachmentHandler> PartialNetObjectAttachmentHandler;
	TStrongObjectPtr<UNetObjectBlobHandler> NetObjectBlobHandler;

	/** Track if a warning was already logged for a specific RPC. */
	// RPC 失败 warn 限频：仅每个 UFunction 名第一次失败打日志（受 net.Iris.ThrottleRPCWarnings 控制）。
	mutable TMap<FName, bool> RPCWarningThrottler;

	UReplicationSystem* ReplicationSystem = nullptr;
	FObjectReferenceCache* ObjectReferenceCache = nullptr;
	FReplicationConnections* Connections = nullptr;
	const UPartialNetObjectAttachmentHandlerConfig* PartialNetObjectAttachmentHandlerConfig = nullptr;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	bool bIsServer = false;
	bool bSendAttachmentsWithObject = false;
	bool bAllowObjectReplication = false;
};

}
