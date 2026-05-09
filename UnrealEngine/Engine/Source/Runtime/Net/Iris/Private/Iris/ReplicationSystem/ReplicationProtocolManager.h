// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationProtocolManager.h —— Iris 协议管理器（缓存/去重 + 实例协议工厂）
// -----------------------------------------------------------------------------
// 职责：
//  • CreateReplicationProtocol  根据 Fragment 列表创建一份共享 FReplicationProtocol，
//                              同 ProtocolId+TemplateKey 的协议复用（缓存）；
//  • CreateInstanceProtocol     为每个具体实例分配一份 FReplicationInstanceProtocol（per-instance 表）；
//  • CalculateProtocolIdentifier 基于 fragments 的 (DescriptorIdentifier, DefaultStateHash) 做 CityHash32；
//  • ValidateReplicationProtocol 校验既有 Protocol 与给定 Fragment 列表是否匹配
//                              （Server/Client 发现 mismatch 时报错并触发 ProtocolMismatch 上报）；
//  • InvalidateDescriptor       某个 Descriptor 失效（class 卸载等）时，递归销毁所有引用它的 Protocol；
//  • DestroyReplicationProtocol 延迟销毁（PendingDestroyProtocols），等到 RefCount 归零再实际释放
//                              —— 因为可能有 in-flight 的 NetObject 仍持有引用。
//
// 关键缓存数据结构：
//   - RegisteredProtocols   MultiMap<ProtocolId, {Protocol, TemplateKey}>  —— 主缓存，供 GetReplicationProtocol 查找；
//   - ProtocolToInfoMap     Map<Protocol*, info>                            —— 反向索引，便于 destroy 时定位条目；
//   - DescriptorToProtocolMap MultiMap<Descriptor*, Protocol*>              —— 跟踪哪些 Protocol 引用了某 Descriptor，
//                                                                            用于 InvalidateDescriptor 级联失效；
//   - PendingDestroyProtocols 延迟销毁队列。
//
// 与 FReplicationStateDescriptorRegistry 协作：Descriptor 是 Protocol 的成员，
// Builder 阶段先在 Registry 中查找/创建 Descriptor，再由 Protocol 管理器组装 Protocol。
// =============================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "UObject/ObjectKey.h"

#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"

namespace UE::Net::Private
{

// 协议创建参数：
//  - TemplateKey         "形态来源"对象（通常是 UClass*）作为缓存复用 key 的一部分；
//                        同 ProtocolId 但不同 TemplateKey 仍是不同 Protocol（处理同 hash 撞协议）。
//  - bValidateProtocolId 是否在创建时重算 hash 并比对（仅调试/校验路径）。
//  - bHasTemplateKey     某些场景下不需要 TemplateKey（例如系统协议）。
//  - TypeStatsIndex      Stats 模块分桶索引；-1 → 默认桶。
struct FCreateReplicationProtocolParameters
{
	const UObject* TemplateKey = nullptr;
	bool bValidateProtocolId = false;
	bool bHasTemplateKey = true;
	int32 TypeStatsIndex = INDEX_NONE;
};

class FReplicationProtocolManager
{
public:
	~FReplicationProtocolManager();

	/* Create protocol from registered fragment data with provided Id, verification is optional */
	// 从 Fragments 创建（或确认创建）一份 FReplicationProtocol。会：
	//  1) 可选校验 ProtocolId（Params.bValidateProtocolId）；
	//  2) 构造 InternalBuffer 布局并合并 traits；
	//  3) 为 PushModel owner 构建 RepIndex→FragmentIndex 表；
	//  4) 把 Protocol 加入缓存。
	IRISCORE_API const FReplicationProtocol* CreateReplicationProtocol(const FReplicationProtocolIdentifier ProtocolId, const FReplicationFragments& Fragments, const TCHAR* DebugName, const FCreateReplicationProtocolParameters& Params = FCreateReplicationProtocolParameters());

	/* Get an existing replication protocol */
	// 同 ProtocolId+TemplateKey 复用：典型场景下同一个 UClass 下所有实例共用同一份 Protocol。
	IRISCORE_API const FReplicationProtocol* GetReplicationProtocol(FReplicationProtocolIdentifier ProtocolId, FObjectKey TemplateKey);

	/** Iterate over all protocols matching the ProtocolId, mostly used by debug functionality with no sideeffects */
	template<typename T>
	void ForEachProtocol(FReplicationProtocolIdentifier ProtocolId, T&& Functor) const;

	/** Iterate over all protocols. */
	template<typename T>
	void ForEachProtocol(T&& Functor) const;
	
	/* Destroy existing replication protocol */
	// 标记销毁；真正释放在 PruneProtocolsPendingDestroy 中等 RefCount=0 时执行。
	IRISCORE_API void DestroyReplicationProtocol(const FReplicationProtocol* ReplicationProtocol);

	/** 
	 * Create instance protocol from registered fragment data
	 * Lifetime either the same as the lifetime of a remotely created instance (we are the client) or controlled by	calls to the bridge api to replicate an object or not
	 * @param ObjectTraits Traits that are passed by the object instance itself
	 * 
	 * 【实例协议工厂】单次连续分配（FReplicationInstanceProtocol + FragmentData[] + Fragments[]）。
	 *  - 客户端：与远端创建的对象同寿命；
	 *  - 服务器：由 Bridge 的 Start/StopReplicating 控制。
	 *  ObjectTraits 是实例侧补充的 traits（例如 NeedsLegacyCallbacks 由 Bridge 注入）。
	 */
	IRISCORE_API static FReplicationInstanceProtocol* CreateInstanceProtocol(const FReplicationFragments& Fragments, UE::Net::EReplicationFragmentTraits ObjectTraits);
	IRISCORE_API static void DestroyInstanceProtocol(FReplicationInstanceProtocol*);

	// 当 Descriptor 因 class GC/Hot-reload 失效时，找出所有引用它的 Protocol 并级联销毁。
	IRISCORE_API void InvalidateDescriptor(const FReplicationStateDescriptor* InvalidatedReplicationStateDescriptor);

public:

	/** Calculate protocol Identifier from registered fragment data */
	// 哈希算法：把每个 fragment 的 (DescriptorIdentifier.Value, DefaultStateHash) 按序写入数组后做 CityHash32。
	// 包含 DefaultStateHash 是为了校验默认值的完整性 —— 默认值不同也算不同形态。
	IRISCORE_API static FReplicationProtocolIdentifier CalculateProtocolIdentifier(const FReplicationFragments& Fragments);

	/** Validate that a existing protocol matches the FragmentList of an instance, returns true of it is a match */
	// 比对 Protocol->ReplicationStateDescriptors[] 与 Fragments 的 Descriptor 指针/Identifier。
	// 用于实例协议绑定时的 sanity check（典型场景：Client 收到来自 Server 的 ProtocolId 后本地复用）。
	IRISCORE_API static bool ValidateReplicationProtocol(const FReplicationProtocol*, const FReplicationFragments& Fragments, bool bLogFragmentErrors=true);

	/** Print the names of the fragments and their hash */
	IRISCORE_API static void FragmentListToString(FStringBuilderBase& StringBuilder, const FReplicationFragments& Fragments);

private:
	void InternalDestroyReplicationProtocol(const FReplicationProtocol* Protocol);     // 真正释放（RefCount=0 后）
	void InternalDeferDestroyReplicationProtocol(const FReplicationProtocol* Protocol);// 加入待销毁队列
	void PruneProtocolsPendingDestroy();                                               // 扫描队列，归零者出列释放

	struct FRegisteredProtocolInfo
	{
		const FReplicationProtocol* Protocol;
		FObjectKey TemplateKey;
	
		bool operator==(const FRegisteredProtocolInfo& Other) const { return Protocol == Other.Protocol && TemplateKey == Other.TemplateKey; };
	};

	// 主缓存（同 ProtocolId 可有多个不同 TemplateKey 的 Protocol，故用 MultiMap）
	TMultiMap<FReplicationProtocolIdentifier, FRegisteredProtocolInfo> RegisteredProtocols;
	// 反向索引（Protocol* → 缓存条目）
	TMap<const FReplicationProtocol*, FRegisteredProtocolInfo> ProtocolToInfoMap;
	
	// We use the pointer as key, we have full control over lifetime of descriptors so this should not be a problem
	// Descriptor → 哪些 Protocol 引用它，用于 InvalidateDescriptor 级联失效
	TMultiMap<const FReplicationStateDescriptor*, const FReplicationProtocol*> DescriptorToProtocolMap;
	// 等待 RefCount=0 后再真正销毁的队列
	TArray<const FReplicationProtocol*> PendingDestroyProtocols;
};

template<typename T>
void FReplicationProtocolManager::ForEachProtocol(FReplicationProtocolIdentifier ProtocolId, T&& Functor) const
{
	// Find protocols using the descriptor being invalidated
	for (auto It = RegisteredProtocols.CreateConstKeyIterator(ProtocolId); It; ++It)
	{
		const FRegisteredProtocolInfo& Info = It.Value();
		Functor(Info.Protocol, Info.TemplateKey);
	}
}

template<typename T>
void FReplicationProtocolManager::ForEachProtocol(T&& Functor) const
{
	for (auto It = RegisteredProtocols.CreateConstIterator(); It; ++It)
	{
		const FRegisteredProtocolInfo& Info = It.Value();
		Functor(Info.Protocol, Info.TemplateKey);
	}
}

}
