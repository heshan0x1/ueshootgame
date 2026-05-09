// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationBridge.h
// 角色：UReplicationBridge —— Iris "游戏世界 ↔ 复制核心"的抽象桥接基类。
//
// 职责：
//   * 维护 NetRefHandle 的本地登记 / 销毁；
//   * 把游戏对象的 PreSendUpdate / 状态拷贝 / WorldLocation 等钩到 RS 的帧循环；
//   * 处理 PendingEndReplication / TearOff / Flush 等"延迟结束复制"动作；
//   * 维护 LevelGroup（基于 ULevel 的 Filter Group），用于关卡级流式过滤；
//   * 接收回调：DetachInstance / CacheNetRefHandleCreationInfo / OnPostXxxUpdate；
//
// 派生：
//   * UObjectReplicationBridge —— 标准 UObject Bridge（StartReplicatingRootObject 等）。
//   * 工程也可派生自定义 Bridge 来支持特殊对象家族。
//
// 帧循环钩子（与 §4 帧循环一一对应）：
//   * OnStartPreSendUpdate / PreSendUpdate / PreSendUpdateSingleHandle / UpdateInstancesWorldLocation
//   * OnPostSendUpdate / OnPostReceiveUpdate
//   * PreReceiveUpdate / PostReceiveUpdate（私有，由 RS 触发）
//
// 关键内部状态：
//   * HandlesPendingEndReplication : 待延迟销毁的 handle 队列
//   * HandlesToStopReplicating     : 在 ReceiveUpdate 期间收到 StopReplicate 请求的暂存表
//   * LevelGroups                  : ULevel -> 与之关联的 NetObjectGroupHandle
//   * bInReceiveUpdate             : 是否正在处理收包；用于决定是否延迟一些动作
// =====================================================================================

#pragma once

#include "Containers/ContainersFwd.h"
#include "Containers/Map.h"

#include "HAL/Platform.h"

#include "Iris/Core/NetObjectReference.h"

#include "Iris/IrisConfig.h"

#include "Iris/ReplicationSystem/NetObjectFactory.h"
#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/ObjectReferenceTypes.h"
#include "Iris/ReplicationSystem/ReplicationBridgeTypes.h"

#include "Misc/EnumClassFlags.h"

#include "Net/Core/NetHandle/NetHandle.h"

#include "UObject/ObjectKey.h"
#include "UObject/ObjectMacros.h"

#include "ReplicationBridge.generated.h"

class UObjectReplicationBridge;
class UReplicationSystem;
class UNetDriver;

namespace UE::Net
{
	enum class ENetRefHandleError : uint32;

	struct FNetDependencyInfo;
	struct FReplicationInstanceProtocol;
	struct FReplicationProtocol;

	class FNetBitStreamReader;
	class FNetBitStreamWriter;
	class FNetSerializationContext;
	class FNetTokenStoreState;	
	class FReplicationFragment;

	namespace Private
	{
		typedef uint32 FInternalNetRefIndex;
		class FNetRefHandleManager;
		class FNetObjectGroups;
		class FNetPushObjectHandle;
		class FObjectReferenceCache;
		class FReplicationProtocolManager;
		class FReplicationReader;
		class FReplicationStateDescriptorRegistry;
		class FReplicationSystemImpl;
		class FReplicationSystemInternal;
		class FReplicationWriter;
		
		struct FChangeMaskCache;
		struct FCreateNetObjectParams;
		struct FAsyncLoadingSimulator;
	}

	typedef TArray<FNetDependencyInfo, TInlineAllocator<32> > FNetDependencyInfoArray;
}

/** Bridge 的日志助手，自动带上 RS Id 前缀，便于多 RS 时追溯归属。 */
#define UE_LOG_BRIDGEID(Category, Verbosity, Format, ...)  UE_LOG(Category, Verbosity, TEXT("RepBridge(%u)::") Format, GetReplicationSystem()->GetId(), ##__VA_ARGS__)

/**
 * Bridge 序列化上下文：把"位流上下文 + 当前连接 + 是否在写销毁信息"打包传递。
 * Reader/Writer 在调用 Bridge 序列化函数（CreateNetRefHandleFromRemote 等）时使用。
 */
struct FReplicationBridgeSerializationContext
{
	FReplicationBridgeSerializationContext(UE::Net::FNetSerializationContext& InSerialiazationContext, uint32 InConnectionId, bool bInIsDestructionInfo = false);

	UE::Net::FNetSerializationContext& SerializationContext;
	uint32 ConnectionId;
	bool bIsDestructionInfo;
};

//------------------------------------------------------------------------

namespace UE::Net
{
	/** The destruction info needed to replicate the destruction event later. */
	/** 用于"静态对象提前销毁后再补告知客户端"的销毁信息描述。 */
	struct FDestructionParameters
	{
		/** The location of the object. Used for distance based prioritization. */
		FVector Location;
		/** The level the object is placed in. */
		const UObject* Level = nullptr;
		/** Whether to use distance based priority for the destruction of the object. */
		bool bUseDistanceBasedPrioritization = false;
		/** The NetFactory that the replicated object would be assigned to. */
		UE::Net::FNetObjectFactoryId NetFactoryId = UE::Net::InvalidNetObjectFactoryId;
	};

	/** SubObject 加入 RootObject 的"插入位置"提示。 */
	enum class ESubObjectInsertionOrder : uint8
	{
		None,
		/** Insert the subobject so it will replicate before the other subobject. */
		ReplicateWith,
		/** Insert the subobject at the start of the list so it can be created and replicated first */
		InsertAtStart,
	};

} // end namespace UE::Net

//------------------------------------------------------------------------
/**
 * Base replication bridge
 * Should only be used by internal Iris classes, public access should be restricted to the UObjectReplicationBridge.
 */
/**
 * Iris 复制桥接抽象基类。仅 Iris 内部使用，业务代码应使用 UObjectReplicationBridge 派生。
 *
 * 关键内部成员：
 *   ReplicationSystem               : 拥有该 Bridge 的 RS
 *   ReplicationProtocolManager      : 协议管理器（共享的 FReplicationProtocol 池）
 *   NetRefHandleManager             : NetRefHandle 中央登记表
 *   ObjectReferenceCache            : UObject 引用 ↔ FNetObjectReference 缓存
 *   Groups                          : NetObject 分组管理（含 LevelGroup）
 *   LevelGroups                     : ULevel ObjectKey -> Group 句柄
 *   HandlesPendingEndReplication    : 延迟结束复制的句柄队列
 *   bInReceiveUpdate                : 帧内收包阶段标志（影响 StopReplicate 时机）
 */
UCLASS(Transient, MinimalAPI)
class UReplicationBridge : public UObject
{
	GENERATED_BODY()

protected:
	using FNetHandle = UE::Net::FNetHandle;
	using FNetRefHandle = UE::Net::FNetRefHandle;
	using FNetDependencyInfoArray = UE::Net::FNetDependencyInfoArray;

public:
	IRISCORE_API UReplicationBridge();
	IRISCORE_API virtual ~UReplicationBridge();


	/**
	* Stop replicating the NetObject associated with the handle and mark the handle to be destroyed.
	* If EEndReplication::TearOff is set the remote instance will be Torn-off rather than being destroyed on the receiving end, after the call, any state changes will not be replicated
	* If EEndReplication::Flush is set all pending states will be delivered before the remote instance is destroyed, final state will be immediately copied so it is safe to remove the object after this call
	* If EEndReplication::Destroy is set the remote instance will be destroyed, if this is set for a static instance and the EndReplicationParameters are set a permanent destruction info will be added
	* Dynamic instances are always destroyed unless the TearOff flag is set.
	*/
	/**
	 * 停止复制并将句柄标记为待销毁。
	 *   * TearOff : 远端不销毁实例，仅停止状态同步
	 *   * Flush   : 先把所有 pending 状态推送给所有连接，再结束（保证最终态一致）
	 *   * Destroy : 远端销毁实例（动态对象默认行为）
	 *   * 对静态实例 + Destroy + 提供 DestructionParameters 时，会写入永久 DestructionInfo，
	 *     用于晚加入或流式装载关卡时通知客户端"该静态对象已不再存在"。
	 */
	IRISCORE_API void StopReplicatingNetRefHandle(FNetRefHandle Handle, EEndReplicationFlags EndReplicationFlags);

	/** Returns true if the handle is replicated. */
	IRISCORE_API bool IsReplicatedHandle(FNetRefHandle Handle) const;

	/** Get the group associated with the level in order to control connection filtering for it. */
	/** 获取 ULevel 关联的 LevelGroup，用于"按关卡过滤连接"。 */
	IRISCORE_API UE::Net::FNetObjectGroupHandle GetLevelGroup(const UObject* Level) const;

	/** Get all level groups. The object key is to a ULevel. The TMap must be processed immediately or copied- DO NOT CACHE. */
	/** 取所有 LevelGroup（仅当下立即使用，不要缓存：bridge 内部会修改）。 */
	IRISCORE_API const TMap<FObjectKey, UE::Net::FNetObjectGroupHandle>& GetAllLevelGroups() const;

	/** Returns true when we are in the middle of processing incoming data. */
	bool IsInReceiveUpdate() const { return bInReceiveUpdate; }

	/** Print common information about this handle and the object it is mapped to */
	[[nodiscard]] IRISCORE_API FString PrintObjectFromNetRefHandle(FNetRefHandle RefHandle) const;

protected:
	/** Initializes the bridge. Is called during ReplicationSystem initialization. */
	IRISCORE_API virtual void Initialize(UReplicationSystem* InReplicationSystem);

	/** Deinitializes the bridge. Is called during ReplicationSystem deinitialization. */
	IRISCORE_API virtual void Deinitialize();

	/** Invoked before ReplicationSystem copies dirty state data. */
	/** RS 在拷贝脏状态前回调（每帧 PreSendUpdate 阶段）。派生在此做批量预处理。 */
	virtual void PreSendUpdate() {}

	/** Invoked when the ReplicationSystem starts the PreSendUpdate tick. */
	/** PreSendUpdate 起点回调；早于 PreSendUpdate（用于 sync NetRefHandleManager 等）。 */
	virtual void OnStartPreSendUpdate() {}
	
	/** Invoked after we sent data to all connections. */
	/** SendUpdate 结束后回调，可在此做清理 / 延迟销毁推进。 */
	virtual void OnPostSendUpdate() {}

	/** Invoked after we processed all incoming data */
	/** 接收完所有连接的数据后回调。 */
	virtual void OnPostReceiveUpdate() {}
	
	/** Invoked before ReplicationSystem copies dirty state data for a single replicated object. */
	/** 单对象 PreSendUpdate（被 ForceNetUpdate / 立即发送路径调用）。 */
	virtual void PreSendUpdateSingleHandle(FNetRefHandle Handle) {}

	/** Update world locations in FWorldLocations for objects that support it. */
	/** 在 §4 帧循环 step 5 调用，刷新 FWorldLocations 供 Filter / Prio 使用。 */
	virtual void UpdateInstancesWorldLocation() {}

	// Remote interface, invoked from Replication code during serialization
	
	/**
	 * Cache info required to allow deferred writing of NetRefHandleCreationInfo
	 * @param Handle The handle of the object to store creation data for.
	 * return whether cached data is stored or not.
	*/
	/**
	 * 缓存 NetRefHandle 的创建信息——在 Flush 路径中需要先 cache 创建头信息，
	 * 之后即使原对象销毁仍能把"创建+终态"序列发送给延迟的连接。
	 */
	IRISCORE_API virtual bool CacheNetRefHandleCreationInfo(FNetRefHandle Handle);

	/** Called when we detach instance protocol from the local instance */
	IRISCORE_API virtual void DetachInstance(FNetRefHandle Handle);

	/** Invoked post garbage collect to allow us to detect stale objects */
	/** GC 后回调：派生类应清理已被 GC 但仍在登记表里的 stale 对象。 */
	IRISCORE_API virtual void PruneStaleObjects();

	/** Invoked when we start to replicate an object for a specific connection to fill in any initial dependencies */
	/** 首次复制某对象到某连接时收集"必须先存在"的依赖（CreationDependency）。 */
	IRISCORE_API virtual void GetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const;

	/** Returns if the bridge is allowed to create new destruction info at this moment. */
	/** 现在是否允许新建 DestructionInfo（Seamless travel 等场景会临时关闭）。 */
	virtual bool CanCreateDestructionInfo() const { return true; }

	/** Called just prior to garbage collecting/destroying the previous world tied to a NetDriver during seamless travel. Subclasses can implement virtual method OnPreSeamlessTravelGarbageCollect. */
	void PreSeamlessTravelGarbageCollect();

	/** Called just after garbage collecting/destroying the previous world tied to a NetDriver during seamless travel. Subclasses can implement virtual method OnPostSeamlessTravelGarbageCollect. */
	void PostSeamlessTravelGarbageCollect();

protected:

	// Forward calls to internal operations that we allow replication bridges to access
	// 以下"Internal"系列：派生 Bridge 可调用，但保持入口收敛在基类，避免直接戳 NetRefHandleManager。

	/** Create a local NetRefHandle / NetObject using the ReplicationProtocol. */
	IRISCORE_API FNetRefHandle InternalCreateNetObject(FNetRefHandle AllocatedHandle, FNetHandle GlobalHandle, const UE::Net::Private::FCreateNetObjectParams& Params);

	/** Create a NetRefHandle / NetObject on request from the authoritative end. */
	IRISCORE_API FNetRefHandle InternalCreateNetObjectFromRemote(FNetRefHandle WantedNetHandle, const UE::Net::Private::FCreateNetObjectParams& Params);

	/** Attach instance to NetRefHandle. */
	IRISCORE_API void InternalAttachInstanceToNetRefHandle(FNetRefHandle RefHandle, bool bBindInstanceProtocol, UE::Net::FReplicationInstanceProtocol* InstanceProtocol, UObject* Instance, FNetHandle NetHandle);

	/** Detach instance from NetRefHandle and destroy the instance protocol. */
	IRISCORE_API void InternalDetachInstanceFromNetRefHandle(UE::Net::Private::FInternalNetRefIndex InternalObjectIndex);

	/** Destroy the handle and all internal book keeping associated with it. */
	IRISCORE_API void InternalDestroyNetObject(FNetRefHandle Handle);
	
	/** Add SubObjectHandle as SubObject to OwnerHandle. */
	IRISCORE_API void InternalAddSubObject(FNetRefHandle OwnerHandle, FNetRefHandle SubObjectHandle, FNetRefHandle InsertRelativeToSubObjectHandle, UE::Net::ESubObjectInsertionOrder InsertionOrder);

	inline UE::Net::Private::FReplicationProtocolManager* GetReplicationProtocolManager() const { return ReplicationProtocolManager; }
	inline UReplicationSystem* GetReplicationSystem() const { return ReplicationSystem; }
	inline UE::Net::Private::FReplicationStateDescriptorRegistry* GetReplicationStateDescriptorRegistry() const { return ReplicationStateDescriptorRegistry; }
	inline UE::Net::Private::FObjectReferenceCache* GetObjectReferenceCache() const { return ObjectReferenceCache; }

	/** Return the NetFactoryId assigned to a replicated object. */
	IRISCORE_API UE::Net::FNetObjectFactoryId GetNetObjectFactoryId(FNetRefHandle RefHandle) const;

	/** Creates a group for a level for object filtering purposes. */
	/** 为某个 ULevel 创建过滤分组。注：Group 名通常为 PackageName，便于跨连接复用。 */
	IRISCORE_API UE::Net::FNetObjectGroupHandle CreateLevelGroup(const UObject* Level, FName PackageName);

	/** Destroys the group associated with the level. */
	IRISCORE_API void DestroyLevelGroup(const UObject* Level);

	/** Called when destruction info is received to determine whether the instance may be destroyed. */
	IRISCORE_API virtual bool IsAllowedToDestroyInstance(const UObject* Instance) const;

	/** Called when a remote connection detected a protocol mismatch when trying to instantiate the NetRefHandle replicated object. */
	virtual void OnProtocolMismatchReported(FNetRefHandle RefHandle, uint32 ConnectionId) {}

	/** Called when a remote connection has a critical error caused by a specific NetRefHandle */
	virtual void OnErrorWithNetRefHandleReported(UE::Net::ENetRefHandleError ErrorType, FNetRefHandle RefHandle, uint32 ConnectionId, const TArray<FNetRefHandle>& ExtraNetRefHandle) {}

	/** Called from PreSeamlessTravelGarbageCollect. */
	IRISCORE_API virtual void OnPreSeamlessTravelGarbageCollect();

	/** Called from PostSeamlessTravelGarbageCollect. */
	IRISCORE_API virtual void OnPostSeamlessTravelGarbageCollect();

	/**
	 * Remove destruction infos associated with group
	 * Passing in an invalid group handle indicates that we should remove all destruction infos
	 */
	virtual void RemoveDestructionInfosForGroup(UE::Net::FNetObjectGroupHandle GroupHandle) {}

private:

	// Internal operations invoked by ReplicationSystem/ReplicationWriter
	
	/** PendingEndReplication 是否需要"立即销毁"。决定 Update 阶段是直接清理还是等待 ref-count 归零。 */
	enum class EPendingEndReplicationImmediate : uint8
	{
		Yes,
		No,
	};

	// Adds the Handle to the list of handles pending deferred EndReplication, if bIsImmediate is true the object will be destroyed after the next update, otherwise
	// it will be kept around until the handle is no longer ref-counted by any connection. It will however be removed from the set of scopeable objects after the first update so new connections will not add it to their scope.
	/**
	 * 把 Handle 入队"待延迟 EndReplication"。Immediate=Yes 时下次 Update 立即销毁；
	 * Immediate=No 时保留直到所有连接的 ref-count 归零（首次 Update 即从 Scope 移除，
	 * 新连接不会把它加入 Scope）。
	 */
	void AddPendingEndReplication(FNetRefHandle Handle, EEndReplicationFlags DestroyFlags, EPendingEndReplicationImmediate Immediate = EPendingEndReplicationImmediate::No);

	void CallPreSendUpdate(float DeltaSeconds);
	void CallPreSendUpdateSingleHandle(FNetRefHandle Handle);
	void CallUpdateInstancesWorldLocation();
	bool CallCacheNetRefHandleCreationInfo(FNetRefHandle Handle);
	void CallPruneStaleObjects();
	void CallDetachInstance(FNetRefHandle Handle);

	void PreReceiveUpdate();
	void PostReceiveUpdate();

private:

	/** 把对象的当前完整状态量化写入 ChangeMaskCache（用于 Flush 路径"补齐最终态"）。 */
	void InternalFlushStateData(UE::Net::FNetSerializationContext& SerializationContext, UE::Net::Private::FChangeMaskCache& ChangeMaskCache, UE::Net::FNetBitStreamWriter& ChangeMaskWriter, uint32 InternalObjectIndex);
	// Internal method to copy state data for Handle
	void InternalFlushStateData(FNetRefHandle Handle);

	// Internal method to copy state data for Handle and any SubObjects and mark them as being torn-off
	/** TearOff 路径：拷贝 OwnerHandle 及其所有 SubObject 的最终状态并标记为"撕断"。 */
	void InternalTearOff(FNetRefHandle OwnerHandle);

	// Destroy all SubObjects owned by provided handle
	void InternalDestroySubObjects(FNetRefHandle OwnerHandle, EEndReplicationFlags Flags);

	/**
	 * Called from ReplicationSystem when a streaming level is about to unload.
	 * Will remove the group associated with the level and remove destruction infos.
	 */
	void NotifyStreamingLevelUnload(const UObject* Level);

	void DestroyLocalNetHandle(FNetRefHandle Handle, EEndReplicationFlags Flags);

	// Tear-off all handles in the PendingTearOff list that has not yet been torn-off
	void TearOffHandlesPendingTearOff();

	// Update all the handles pending EndReplication
	/** 推进 HandlesPendingEndReplication：根据 Immediate / refcount 情况清理或保留。 */
	void UpdateHandlesPendingEndReplication();

	void SetNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments, const UE::Net::Private::FNetPushObjectHandle& PushHandle);
	void ClearNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments);

	void DestroyGlobalNetHandle(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex);
	void ClearNetPushIds(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex);

	friend UE::Net::Private::FReplicationSystemImpl;
	friend UE::Net::Private::FReplicationSystemInternal;

	friend UObjectReplicationBridge;
	friend UReplicationSystem;

	UReplicationSystem* ReplicationSystem;                                            // 拥有该 Bridge 的 RS
	UE::Net::Private::FReplicationProtocolManager* ReplicationProtocolManager;        // 协议管理器
	UE::Net::Private::FReplicationStateDescriptorRegistry* ReplicationStateDescriptorRegistry; // Descriptor 注册/复用
	UE::Net::Private::FNetRefHandleManager* NetRefHandleManager;                      // 中央登记表
	UE::Net::Private::FObjectReferenceCache* ObjectReferenceCache;                    // UObject<->NetRef 缓存
	UE::Net::Private::FNetObjectGroups* Groups;                                       // 分组容器（含 LevelGroup）

	TMap<FObjectKey, UE::Net::FNetObjectGroupHandle> LevelGroups;                     // ULevel -> Group 句柄

	/** Tracks if we are in the middle of processing incoming data */
	bool bInReceiveUpdate = false;

	/** List of replicated objects that requested to stop replicating while we were in ReceiveUpdate */
	/** 在收包阶段收到 StopReplicate 的暂存——必须延后处理（避免破坏正在解析的 batch）。 */
	TMap<FNetRefHandle, EEndReplicationFlags> HandlesToStopReplicating;

private:

	/** "待延迟结束复制"的入队条目。 */
	struct FPendingEndReplicationInfo
	{
		FPendingEndReplicationInfo(FNetRefHandle InHandle, EEndReplicationFlags InDestroyFlags, EPendingEndReplicationImmediate InImmediate) : Handle(InHandle), DestroyFlags(InDestroyFlags), Immediate(InImmediate) {}

		FNetRefHandle Handle;
		EEndReplicationFlags DestroyFlags;
		EPendingEndReplicationImmediate Immediate;
	};
	TArray<FPendingEndReplicationInfo> HandlesPendingEndReplication;                  // 队列：每帧 UpdateHandlesPendingEndReplication 推进

};


inline FReplicationBridgeSerializationContext::FReplicationBridgeSerializationContext(UE::Net::FNetSerializationContext& InSerialiazationContext, uint32 InConnectionId, bool bInIsDestructionInfo)
: SerializationContext(InSerialiazationContext)
, ConnectionId(InConnectionId)
, bIsDestructionInfo(bInIsDestructionInfo)
{
}
