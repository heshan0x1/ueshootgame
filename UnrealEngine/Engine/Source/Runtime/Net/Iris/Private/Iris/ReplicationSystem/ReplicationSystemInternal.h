// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationSystemInternal.h（Private）
// 角色：FReplicationSystemInternal —— RS 的"聚合根 / 子系统容器"。
//
// 一切运行期数据都在这里。FReplicationSystemImpl 通过它访问所有子系统，
// UReplicationSystem 公开 API 也通过 GetReplicationSystemInternal() 桥接到此。
//
// 持有的子系统（按调用频次排序）：
//   * FReplicationProtocolManager       : 协议形态的全局缓存（去重）
//   * FNetRefHandleManager              : 中央登记表（所有 NetObject）
//   * FReplicationStateDescriptorRegistry : Descriptor 复用 / 烘焙缓存
//   * FReplicationStateStorage          : per-object SendState/RecvState/Baseline 分配池
//   * FDirtyNetObjectTracker            : 全局脏对象追踪（合并 GlobalDirty + LegacyPushModel）
//   * FChangeMaskCache                  : 本帧脏字段位图缓存
//   * FReplicationConnections           : 所有连接的聚合容器
//   * FReplicationFiltering             : Scope 计算（Exclusion -> Dynamic -> Inclusion）
//   * FNetObjectGroups                  : 分组容器（含 LevelGroup / SubObject Group）
//   * FReplicationConditionals          : LifetimeCondition + Dynamic Condition 求解
//   * FReplicationPrioritization        : 打分排序
//   * FObjectReferenceCache             : UObject<->FNetObjectReference 缓存
//   * FNetBlobManager (含 NetBlobHandlerManager) : RPC / 附件
//   * FWorldLocations                   : 世界位置 + 距离剔除
//   * FDeltaCompressionBaselineManager  : Delta 压缩基线管理
//   * FDeltaCompressionBaselineInvalidationTracker : 基线作废追踪
//   * FNetSendStats / FNetTypeStats     : 性能统计 / 按类汇总
//   * UObjectReplicationBridge*         : 桥接（裸指针，保 UPROPERTY 在 RS 外层）
//   * UIrisObjectReferencePackageMap*   : 与旧 NetSerialize 的 PackageMap 兼容桥接
//
// 并行任务安全：
//   * StartParallelPhase / StopParallelPhase : 标志位，被 ObjectPoller 等子系统使用
//     用于切换 thread-safe code path（NetTypeStats 等会切换 SyncLog 写入方式）。
//
// PImpl 解耦的好处：
//   * UReplicationSystem 头文件不暴露这堆子系统类型，编译期低耦合；
//   * 子系统类型变更不会触发上层重编译；
//   * 测试时可以用 Pimpl friend 访问内部状态。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Iris/ReplicationState/ReplicationStateStorage.h"
#include "Iris/ReplicationSystem/DirtyNetObjectTracker.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ChangeMaskCache.h"
#include "Iris/ReplicationSystem/Conditionals/ReplicationConditionals.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineManager.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineInvalidationTracker.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFiltering.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectGroups.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/Prioritization/ReplicationPrioritization.h"
#include "Iris/ReplicationSystem/ReplicationProtocolManager.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobManager.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Iris/ReplicationSystem/StringTokenStore.h"
#include "Iris/ReplicationSystem/NameTokenStore.h"
#include "Iris/ReplicationSystem/WorldLocations.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorRegistry.h"
#include "Iris/Stats/NetStats.h"

class UIrisObjectReferencePackageMap;

namespace UE::Net::Private
{

/** 创建 FReplicationSystemInternal 时所需的最小参数集（由 FReplicationSystemImpl 构造时填入）。 */
struct FReplicationSystemInternalInitParams
{
	uint32 ReplicationSystemId;
	uint32 MaxReplicatedObjectCount;
	uint32 NetChunkedArrayCount;
	uint32 MaxReplicationWriterObjectCount;
	bool bUseRemoteObjectReferences;
	bool bAllowParallelTasks;
	bool bAllowMinimalUpdateIfNoConnections;
};

/**
 * RS 内部聚合根。所有子系统的实例都在这里——不通过指针/继承，而是直接成员，
 * 这样 cache 局部性最佳、生命周期最简单（与 RS 同寿）。
 */
class FReplicationSystemInternal
{
public:
	explicit FReplicationSystemInternal(const FReplicationSystemInternalInitParams& Params)
	: NetRefHandleManager(ReplicationProtocolManager)
	, InternalInitParams(Params)
	, DirtyNetObjectTracker()
	, ReplicationBridge(nullptr)
	, IrisObjectReferencePackageMap(nullptr)
	, Id(Params.ReplicationSystemId)
	, bAllowParallelTasks(Params.bAllowParallelTasks)
	{}

	FReplicationProtocolManager& GetReplicationProtocolManager() { return ReplicationProtocolManager; }

	FNetRefHandleManager& GetNetRefHandleManager() { return NetRefHandleManager; }
	const FNetRefHandleManager& GetNetRefHandleManager() const { return NetRefHandleManager; }

	void InitDirtyNetObjectTracker(const struct FDirtyNetObjectTrackerInitParams& Params) { DirtyNetObjectTracker.Init(Params); }
	bool IsDirtyNetObjectTrackerInitialized() const { return DirtyNetObjectTracker.IsInit(); }
	FDirtyNetObjectTracker& GetDirtyNetObjectTracker() { checkf(DirtyNetObjectTracker.IsInit(), TEXT("Not allowed to access the DirtyNetObjectTracker unless object replication is enabled.")); return DirtyNetObjectTracker; }

	FReplicationStateDescriptorRegistry& GetReplicationStateDescriptorRegistry() { return ReplicationStateDescriptorRegistry; }

	FReplicationStateStorage& GetReplicationStateStorage() { return ReplicationStateStorage; }

	FObjectReferenceCache& GetObjectReferenceCache() { return ObjectReferenceCache; }

	void SetReplicationBridge(UObjectReplicationBridge* InReplicationBridge) { ReplicationBridge = InReplicationBridge; }
	UObjectReplicationBridge* GetReplicationBridge() const { return ReplicationBridge; }
	UObjectReplicationBridge* GetReplicationBridge(FNetRefHandle Handle) const { return ReplicationBridge; }

	void SetIrisObjectReferencePackageMap(UIrisObjectReferencePackageMap* InIrisObjectReferencePackageMap) { IrisObjectReferencePackageMap = InIrisObjectReferencePackageMap; }
	UIrisObjectReferencePackageMap* GetIrisObjectReferencePackageMap() { return IrisObjectReferencePackageMap; }

	FChangeMaskCache& GetChangeMaskCache() { return ChangeMaskCache; }

	FReplicationConnections& GetConnections() { return Connections; }

	FReplicationFiltering& GetFiltering() { return Filtering; }
	const FReplicationFiltering& GetFiltering() const { return Filtering; }

	FNetObjectGroups& GetGroups() { return Groups; }

	FReplicationConditionals& GetConditionals() { return Conditionals; }

	FReplicationPrioritization& GetPrioritization() { return Prioritization; }

	FNetBlobManager& GetNetBlobManager() { return NetBlobManager; }
	FNetBlobHandlerManager& GetNetBlobHandlerManager() { return NetBlobManager.GetNetBlobHandlerManager(); }
	const FNetBlobHandlerManager& GetNetBlobHandlerManager() const { return NetBlobManager.GetNetBlobHandlerManager(); }

	FWorldLocations& GetWorldLocations() { return WorldLocations; }

	FDeltaCompressionBaselineManager& GetDeltaCompressionBaselineManager() { return DeltaCompressionBaselineManager; }
	FDeltaCompressionBaselineInvalidationTracker& GetDeltaCompressionBaselineInvalidationTracker() { return DeltaCompressionBaselineInvalidationTracker; }

	FNetTypeStats& GetNetTypeStats() { return TypeStats; }

	FReplicationSystemInternalInitParams& GetInitParams() { return InternalInitParams; }

	FNetSendStats& GetSendStats()
	{ 
		return SendStats;
	}

	FReplicationStats& GetTickReplicationStats()
	{
		return TickReplicationStats;
	}

	const FReplicationStats& GetTickReplicationStats() const
	{
		return TickReplicationStats;
	}

	FReplicationStats& GetAccumulatedReplicationStats()
	{
		return AccumulatedReplicationStats;
	}

	const FReplicationStats& GetAccumulatedReplicationStats() const
	{
		return AccumulatedReplicationStats;
	}

	FForwardNetRPCCallMulticastDelegate& GetForwardNetRPCCallMulticastDelegate()
	{
		return ForwardNetRPCCallMulticastDelegate;
	}

	void SetBlockFilterChanges(bool bBlock) { bBlockFilterChanges = bBlock; }

	bool AreFilterChangesBlocked() const { return bBlockFilterChanges; }

	bool AreParallelTasksAllowed() const { return bAllowParallelTasks; }

	/** Used by subsystems such as ObjectPoller to indicate when we're running tasks simultaneously and need to be thread-safe
		Must be called in-order and exclusively. We currently do not support simultaneous parallel phases (e.g running Write tasks whilst Polling is running) */
	/**
	 * 子系统（如 FObjectPoller）在进入"并行任务阶段"前/后调用，用于切到 thread-safe 路径。
	 * 注意：当前不支持多个并行阶段同时进行（例如 Poll 与 Write 不能并行）。
	 */
	void StartParallelPhase() 
	{ 
		check(bAllowParallelTasks);
		check(!bIsInParallelPhase);
		bIsInParallelPhase = true;
		TypeStats.SetIsInParallelPhase(true);
	}

	void StopParallelPhase()
	{ 
		check(bAllowParallelTasks); 
		check(bIsInParallelPhase); 
		bIsInParallelPhase = false; 
		TypeStats.SetIsInParallelPhase(false); 
	}

	bool GetIsInParallelPhase() const
	{
		return bIsInParallelPhase;
	}

	void PreSeamlessTravelGarbageCollect()
	{
		GetReplicationBridge()->PreSeamlessTravelGarbageCollect();
	}

	void PostSeamlessTravelGarbageCollect()
	{
		GetReplicationBridge()->PostSeamlessTravelGarbageCollect();
	}

private:
	// 所有子系统实例：直接成员，与 RS 同生共死。顺序与构造/析构顺序相关。
	FReplicationProtocolManager ReplicationProtocolManager;                          // 协议形态全局缓存
	FNetRefHandleManager NetRefHandleManager;                                        // 中央登记表
	FReplicationSystemInternalInitParams InternalInitParams;                         // 创建参数（用于后续子系统延迟初始化）
	FDirtyNetObjectTracker DirtyNetObjectTracker;                                    // 脏对象追踪
	FReplicationStateStorage ReplicationStateStorage;                                // per-object 状态存储池
	FReplicationStateDescriptorRegistry ReplicationStateDescriptorRegistry;          // Descriptor 注册/复用
	UObjectReplicationBridge* ReplicationBridge;                                     // Bridge（裸指针；UPROPERTY 在 RS 外层）
	UIrisObjectReferencePackageMap* IrisObjectReferencePackageMap;                   // PackageMap 兼容
	FChangeMaskCache ChangeMaskCache;                                                // 本帧脏字段缓存
	FReplicationConnections Connections;                                             // 所有连接
	FReplicationFiltering Filtering;                                                 // Scope 计算
	FNetObjectGroups Groups;                                                         // 分组
	FReplicationConditionals Conditionals;                                           // 条件求解
	FReplicationPrioritization Prioritization;                                       // 优先级
	FObjectReferenceCache ObjectReferenceCache;                                      // UObject 引用缓存
	FNetBlobManager NetBlobManager;                                                  // RPC / Attachment
	FWorldLocations WorldLocations;                                                  // 世界位置 / 距离
	FDeltaCompressionBaselineManager DeltaCompressionBaselineManager;                // DC 基线池
	FDeltaCompressionBaselineInvalidationTracker DeltaCompressionBaselineInvalidationTracker; // DC 失效跟踪
	FNetSendStats SendStats;                                                         // 发送侧统计
	FNetTypeStats TypeStats;                                                         // 按类性能桶
	FReplicationStats TickReplicationStats = {};                                     // 本帧统计
	FReplicationStats AccumulatedReplicationStats = {};                              // 累积统计
	FForwardNetRPCCallMulticastDelegate ForwardNetRPCCallMulticastDelegate;          // RPC 转发多播
	uint32 Id;

	/** When true this prevents any changes to the filter system. Enabled during times where adding filter options is unsupported. */
	/** 在 PreSendUpdate 阶段置 true，禁止外部修改 Filter（避免一致性问题）。 */
	bool bBlockFilterChanges = false;

public:
	/**
	*   When true, allow subsystems to run parallel workloads, such as the PollAndCopy step running several asynchronous tasks to speed up game thread execution time.
	*   Only supported when bIsServer = true and bAllowObjectReplication = true
	*/
	bool bAllowParallelTasks = false;

	/** Is true whilst a phase is running parallel tasks. If bAllowParallelTasks is false, this can never be true. */
	bool bIsInParallelPhase = false;
};

}
