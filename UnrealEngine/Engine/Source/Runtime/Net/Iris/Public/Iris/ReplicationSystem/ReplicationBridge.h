// Copyright Epic Games, Inc. All Rights Reserved.

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

#define UE_LOG_BRIDGEID(Category, Verbosity, Format, ...)  UE_LOG(Category, Verbosity, TEXT("RepBridge(%u)::") Format, GetReplicationSystem()->GetId(), ##__VA_ARGS__)

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
	IRISCORE_API void StopReplicatingNetRefHandle(FNetRefHandle Handle, EEndReplicationFlags EndReplicationFlags);

	/** Returns true if the handle is replicated. */
	IRISCORE_API bool IsReplicatedHandle(FNetRefHandle Handle) const;

	/** Get the group associated with the level in order to control connection filtering for it. */
	IRISCORE_API UE::Net::FNetObjectGroupHandle GetLevelGroup(const UObject* Level) const;

	/** Get all level groups. The object key is to a ULevel. The TMap must be processed immediately or copied- DO NOT CACHE. */
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
	virtual void PreSendUpdate() {}

	/** Invoked when the ReplicationSystem starts the PreSendUpdate tick. */
	virtual void OnStartPreSendUpdate() {}
	
	/** Invoked after we sent data to all connections. */
	virtual void OnPostSendUpdate() {}

	/** Invoked after we processed all incoming data */
	virtual void OnPostReceiveUpdate() {}
	
	/** Invoked before ReplicationSystem copies dirty state data for a single replicated object. */
	virtual void PreSendUpdateSingleHandle(FNetRefHandle Handle) {}

	/** Update world locations in FWorldLocations for objects that support it. */
	virtual void UpdateInstancesWorldLocation() {}

	// Remote interface, invoked from Replication code during serialization
	
	/**
	 * Cache info required to allow deferred writing of NetRefHandleCreationInfo
	 * @param Handle The handle of the object to store creation data for.
	 * return whether cached data is stored or not.
	*/
	IRISCORE_API virtual bool CacheNetRefHandleCreationInfo(FNetRefHandle Handle);

	/** Called when we detach instance protocol from the local instance */
	IRISCORE_API virtual void DetachInstance(FNetRefHandle Handle);

	/** Invoked post garbage collect to allow us to detect stale objects */
	IRISCORE_API virtual void PruneStaleObjects();

	/** Invoked when we start to replicate an object for a specific connection to fill in any initial dependencies */
	IRISCORE_API virtual void GetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const;

	/** Returns if the bridge is allowed to create new destruction info at this moment. */
	virtual bool CanCreateDestructionInfo() const { return true; }

	/** Called just prior to garbage collecting/destroying the previous world tied to a NetDriver during seamless travel. Subclasses can implement virtual method OnPreSeamlessTravelGarbageCollect. */
	void PreSeamlessTravelGarbageCollect();

	/** Called just after garbage collecting/destroying the previous world tied to a NetDriver during seamless travel. Subclasses can implement virtual method OnPostSeamlessTravelGarbageCollect. */
	void PostSeamlessTravelGarbageCollect();

protected:

	// Forward calls to internal operations that we allow replication bridges to access

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
	
	enum class EPendingEndReplicationImmediate : uint8
	{
		Yes,
		No,
	};

	// Adds the Handle to the list of handles pending deferred EndReplication, if bIsImmediate is true the object will be destroyed after the next update, otherwise
	// it will be kept around until the handle is no longer ref-counted by any connection. It will however be removed from the set of scopeable objects after the first update so new connections will not add it to their scope.
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

	void InternalFlushStateData(UE::Net::FNetSerializationContext& SerializationContext, UE::Net::Private::FChangeMaskCache& ChangeMaskCache, UE::Net::FNetBitStreamWriter& ChangeMaskWriter, uint32 InternalObjectIndex);
	// Internal method to copy state data for Handle
	void InternalFlushStateData(FNetRefHandle Handle);

	// Internal method to copy state data for Handle and any SubObjects and mark them as being torn-off
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
	void UpdateHandlesPendingEndReplication();

	void SetNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments, const UE::Net::Private::FNetPushObjectHandle& PushHandle);
	void ClearNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments);

	void DestroyGlobalNetHandle(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex);
	void ClearNetPushIds(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex);

	friend UE::Net::Private::FReplicationSystemImpl;
	friend UE::Net::Private::FReplicationSystemInternal;

	friend UObjectReplicationBridge;
	friend UReplicationSystem;

	UReplicationSystem* ReplicationSystem;
	UE::Net::Private::FReplicationProtocolManager* ReplicationProtocolManager;
	UE::Net::Private::FReplicationStateDescriptorRegistry* ReplicationStateDescriptorRegistry;
	UE::Net::Private::FNetRefHandleManager* NetRefHandleManager;
	UE::Net::Private::FObjectReferenceCache* ObjectReferenceCache;
	UE::Net::Private::FNetObjectGroups* Groups;

	TMap<FObjectKey, UE::Net::FNetObjectGroupHandle> LevelGroups;

	/** Tracks if we are in the middle of processing incoming data */
	bool bInReceiveUpdate = false;

	/** List of replicated objects that requested to stop replicating while we were in ReceiveUpdate */
	TMap<FNetRefHandle, EEndReplicationFlags> HandlesToStopReplicating;

private:

	struct FPendingEndReplicationInfo
	{
		FPendingEndReplicationInfo(FNetRefHandle InHandle, EEndReplicationFlags InDestroyFlags, EPendingEndReplicationImmediate InImmediate) : Handle(InHandle), DestroyFlags(InDestroyFlags), Immediate(InImmediate) {}

		FNetRefHandle Handle;
		EEndReplicationFlags DestroyFlags;
		EPendingEndReplicationImmediate Immediate;
	};
	TArray<FPendingEndReplicationInfo> HandlesPendingEndReplication;

};


inline FReplicationBridgeSerializationContext::FReplicationBridgeSerializationContext(UE::Net::FNetSerializationContext& InSerialiazationContext, uint32 InConnectionId, bool bInIsDestructionInfo)
: SerializationContext(InSerialiazationContext)
, ConnectionId(InConnectionId)
, bIsDestructionInfo(bInIsDestructionInfo)
{
}
