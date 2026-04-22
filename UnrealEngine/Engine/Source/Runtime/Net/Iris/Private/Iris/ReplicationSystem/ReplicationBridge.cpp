// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/ReplicationBridge.h"

#include "Containers/ArrayView.h"

#include "HAL/IConsoleManager.h"

#include "Iris/Core/IrisCsv.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/IrisConfigInternal.h"

#include "Iris/ReplicationState/ReplicationStateUtil.h"

#include "Iris/ReplicationSystem/ChangeMaskCache.h"
#include "Iris/ReplicationSystem/LegacyPushModel.h"
#include "Iris/ReplicationSystem/NetRefHandleManagerTypes.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationFragmentInternal.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/ReplicationWriter.h"
#include "Iris/ReplicationSystem/RepTag.h"
#include "Iris/ReplicationSystem/WorldLocations.h"

#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"

#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/ObjectNetSerializer.h"

#include "Net/Core/NetBitArray.h"
#include "Net/Core/NetHandle/NetHandleManager.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Net/Core/Trace/NetTrace.h"

#include "ReplicationFragmentInternal.h"
#include "ReplicationOperationsInternal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ReplicationBridge)

#define UE_LOG_REPLICATIONBRIDGE(Category, Format, ...)  UE_LOG(LogIris, Category, TEXT("RepBridge(%u)::") Format, ReplicationSystem->GetId(), ##__VA_ARGS__)

static bool bEnableFlushReliableRPCOnDestroy = true;
static FAutoConsoleVariableRef CVarEnableFlushReliableRPCOnDestroy(
	TEXT("net.Iris.EnableFlushReliableRPCOnDestroy"),
	bEnableFlushReliableRPCOnDestroy,
	TEXT("When true EEndReplicationFlags::Flush flag will be appended in EndReplication if we have pending unprocessed attachments/RPC:s when destroying a replicated object.")
);

static bool bAllowDestroyToCancelFlushAndTearOff = false;
static FAutoConsoleVariableRef CVarAllowDestroyToCancelFlushAndTearOff(
	TEXT("net.Iris.AllowDestroyToCancelFlushAndTearOff"),
	bAllowDestroyToCancelFlushAndTearOff,
	TEXT("When true issuing a EndReplication on an object that is already Tear-off or pending endreplication will cancel destroy/flush and destroy the replicated objects.")
);

/**
 * ReplicationBridge Implementation
 */
UReplicationBridge::UReplicationBridge()
: ReplicationSystem(nullptr)
, ReplicationProtocolManager(nullptr)
, ReplicationStateDescriptorRegistry(nullptr)
, NetRefHandleManager(nullptr)
{
}

void UReplicationBridge::PreReceiveUpdate()
{
	check(bInReceiveUpdate == false);
	bInReceiveUpdate = true;
}

void UReplicationBridge::PostReceiveUpdate()
{
	check(bInReceiveUpdate == true);
	bInReceiveUpdate = false;

	// Now process all StopReplication calls done while inside ReceiveUpdate
	for (const auto& It : HandlesToStopReplicating)
	{
		StopReplicatingNetRefHandle(It.Key, It.Value);
	}
	HandlesToStopReplicating.Reset();
	
	OnPostReceiveUpdate();
}

bool UReplicationBridge::CacheNetRefHandleCreationInfo(FNetRefHandle Handle)
{
	return false;
}

void UReplicationBridge::DetachInstance(FNetRefHandle Handle)
{
}

void UReplicationBridge::PruneStaleObjects()
{
}

void UReplicationBridge::GetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const
{
	return;
}

void UReplicationBridge::CallPreSendUpdate(float DeltaSeconds)
{
	// Tear-off all handles pending tear-off
	TearOffHandlesPendingTearOff();

	PreSendUpdate();
}

void UReplicationBridge::CallPreSendUpdateSingleHandle(FNetRefHandle Handle)
{
	PreSendUpdateSingleHandle(Handle);
}

void UReplicationBridge::CallUpdateInstancesWorldLocation()
{
	UpdateInstancesWorldLocation();
}

void UReplicationBridge::CallDetachInstance(FNetRefHandle Handle)
{
	DetachInstance(Handle);
}

void UReplicationBridge::CallPruneStaleObjects()
{
	PruneStaleObjects();
}

bool UReplicationBridge::CallCacheNetRefHandleCreationInfo(FNetRefHandle Handle)
{
	return CacheNetRefHandleCreationInfo(Handle);
}

bool UReplicationBridge::IsAllowedToDestroyInstance(const UObject* Instance) const
{
	return true;
}

UReplicationBridge::~UReplicationBridge()
{
}

void UReplicationBridge::Initialize(UReplicationSystem* InReplicationSystem)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	Private::FReplicationSystemInternal* ReplicationSystemInternal = InReplicationSystem->GetReplicationSystemInternal();

	ReplicationSystem = InReplicationSystem;
	ReplicationProtocolManager = &ReplicationSystemInternal->GetReplicationProtocolManager();
	ReplicationStateDescriptorRegistry = &ReplicationSystemInternal->GetReplicationStateDescriptorRegistry();
	NetRefHandleManager = &ReplicationSystemInternal->GetNetRefHandleManager();
	ObjectReferenceCache = &ReplicationSystemInternal->GetObjectReferenceCache();
	Groups = &ReplicationSystemInternal->GetGroups();
}

void UReplicationBridge::Deinitialize()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	// Detach all replicated instances that have not yet been destroyed as part of shutting down the rest of the game.
	NetRefHandleManager->GetAssignedInternalIndices().ForAllSetBits([this](uint32 InternalObjectIndex) 
	{
		if (InternalObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
		{
			return;
		}

		FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalObjectIndex);
		if (ObjectData.InstanceProtocol)
		{
			// Clear out, tracking data or not?, currently we only have a single replication-system so it should not be a big issue if we do.
			// Note: Currently we opted to leave it as is.
			// -- This only applies to server. If it is a restart of ReplicationSystem, the same actors will be re-registered and global handle will be destroyed later, otherwise it does not matter what we do.
			// DestroyGlobalNetHandle(InternalObjectIndex);
			// ClearNetPushIds(InternalObjectIndex);

			// Detach and destroy instance protocol
			ObjectData.bPendingEndReplication = 1U;
			InternalDetachInstanceFromNetRefHandle(InternalObjectIndex);
		}
	});

	ReplicationSystem = nullptr;
	ReplicationProtocolManager = nullptr;
	ReplicationStateDescriptorRegistry = nullptr;
	NetRefHandleManager = nullptr;
	ObjectReferenceCache = nullptr;
	Groups = nullptr;
}

UE::Net::FNetRefHandle UReplicationBridge::InternalCreateNetObject(FNetRefHandle AllocatedHandle, FNetHandle GlobalHandle, const UE::Net::Private::FCreateNetObjectParams& Params)
{
	check(AllocatedHandle.IsValid() && AllocatedHandle.IsCompleteHandle());

	FNetRefHandle Handle = NetRefHandleManager->CreateNetObject(AllocatedHandle, GlobalHandle, Params);

	if (Handle.IsValid())
	{
		UE_NET_TRACE_NETHANDLE_CREATED(Handle, Params.ReplicationProtocol->DebugName, (uint64)Params.ReplicationProtocol->ProtocolIdentifier, 0/*Local*/);
	}

	return Handle;
}

UE::Net::FNetRefHandle UReplicationBridge::InternalCreateNetObjectFromRemote(FNetRefHandle WantedNetHandle, const UE::Net::Private::FCreateNetObjectParams& Params)
{
	FNetRefHandle Handle = NetRefHandleManager->CreateNetObjectFromRemote(WantedNetHandle, Params);

	if (Handle.IsValid())
	{
		UE_NET_TRACE_NETHANDLE_CREATED(Handle, Params.ReplicationProtocol->DebugName, (uint64)Params.ReplicationProtocol->ProtocolIdentifier, 1/*Remote*/);
	}

	return Handle;
}

void UReplicationBridge::InternalAttachInstanceToNetRefHandle(FNetRefHandle RefHandle, bool bBindInstanceProtocol, UE::Net::FReplicationInstanceProtocol* InstanceProtocol, UObject* Instance, FNetHandle NetHandle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const uint32 ReplicationSystemId = RefHandle.GetReplicationSystemId();
	const FInternalNetRefIndex InternalReplicationIndex = NetRefHandleManager->GetInternalIndex(RefHandle);

	NetRefHandleManager->AttachInstanceProtocol(InternalReplicationIndex, InstanceProtocol, Instance);
	UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("InternalAttachInstanceToNetHandle Attached: %s %s to (InternalIndex: %u)"), *Instance->GetName(), *RefHandle.ToString(), InternalReplicationIndex);

	// Bind instance protocol to dirty state tracking
	if (bBindInstanceProtocol)
	{
		FReplicationInstanceOperationsInternal::BindInstanceProtocol(NetHandle, InstanceProtocol, NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalReplicationIndex).Protocol);
		ForceNetUpdate(ReplicationSystemId, InternalReplicationIndex);
	}
}

void UReplicationBridge::InternalDetachInstanceFromNetRefHandle(UE::Net::Private::FInternalNetRefIndex InternalObjectIndex)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	if (FReplicationInstanceProtocol* InstanceProtocol = const_cast<FReplicationInstanceProtocol*>(NetRefHandleManager->DetachInstanceProtocol(InternalObjectIndex)))
	{
		UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("InternalDetachInstanceToNetHandle Detached: %s"), *NetRefHandleManager->PrintObjectFromIndex(InternalObjectIndex));

		if (EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::IsBound))
		{
			FReplicationInstanceOperationsInternal::UnbindInstanceProtocol(InstanceProtocol, NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalObjectIndex).Protocol);
		}
		ReplicationProtocolManager->DestroyInstanceProtocol(InstanceProtocol);
	}
}

void UReplicationBridge::InternalDestroyNetObject(FNetRefHandle Handle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	if (const FInternalNetRefIndex ObjectInternalIndex = NetRefHandleManager->GetInternalIndex(Handle))
	{
		FReplicationSystemInternal* ReplicationSystemInternal = ReplicationSystem->GetReplicationSystemInternal();

		FWorldLocations& WorldLocations = ReplicationSystemInternal->GetWorldLocations();
		WorldLocations.RemoveObjectInfoCache(ObjectInternalIndex);

		// Remove from handles to stop replicating as we might respawn the same object again in a later packet
		HandlesToStopReplicating.Remove(Handle);

		FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectInternalIndex);

		// At this point we can no longer instantiate objects and must explicitly clear out objects pending create from ReplicationWriter
		if (ObjectData.bHasCachedCreationInfo && NetRefHandleManager->GetNetObjectRefCount(ObjectInternalIndex) > 0U)
		{
			// We need to explicitly notify all ReplicationWriters that we are destroying objects pending tearoff
			// The handle will automatically be removed from HandlesPendingEndReplication after the next update
			FReplicationConnections& Connections = ReplicationSystemInternal->GetConnections();

			auto NotifyDestroyedObjectPendingEndReplication = [&Connections, &ObjectInternalIndex](uint32 ConnectionId)
			{
				FReplicationConnection* Conn = Connections.GetConnection(ConnectionId);			
				Conn->ReplicationWriter->NotifyDestroyedObjectPendingEndReplication(ObjectInternalIndex);
			};

			const FNetBitArray& ValidConnections = Connections.GetValidConnections();
			ValidConnections.ForAllSetBits(NotifyDestroyedObjectPendingEndReplication);					

			// As we have release cached creation headers before calling InternalDestroyNetObject it is critical that we also clear out the flag.
			ObjectData.bHasCachedCreationInfo = 0U;
		}
	}

	NetRefHandleManager->DestroyNetObject(Handle);
}

void UReplicationBridge::DestroyLocalNetHandle(FNetRefHandle Handle, EEndReplicationFlags EndReplicationFlags)
{
	UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("DestroyLocalNetHandle for %s | EndReplicationFlags: %s"), *NetRefHandleManager->PrintObjectFromNetRefHandle(Handle), *LexToString(EndReplicationFlags));

	const UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex = NetRefHandleManager->GetInternalIndex(Handle);

	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::DestroyNetHandle | EEndReplicationFlags::ClearNetPushId))
	{
		if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::DestroyNetHandle))
		{
			DestroyGlobalNetHandle(InternalReplicationIndex);
		}

		if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::ClearNetPushId))
		{
			ClearNetPushIds(InternalReplicationIndex);
		}
	}

	// Detach instance protocol
	InternalDetachInstanceFromNetRefHandle(InternalReplicationIndex);

	// Allow derived bridges to cleanup any instance info they have stored
	CallDetachInstance(Handle);

	// If the object is in any groups we need to remove it to make sure that we update filtering
	GetReplicationSystem()->RemoveFromAllGroups(Handle);

	// If we have any attached SubObjects, tag them for destroy as well
	InternalDestroySubObjects(Handle, EndReplicationFlags);

	// Tell ReplicationSystem to destroy the handle
	InternalDestroyNetObject(Handle);
}

void UReplicationBridge::InternalAddSubObject(FNetRefHandle OwnerHandle, FNetRefHandle SubObjectHandle, FNetRefHandle InsertRelativeToSubObjectHandle, UE::Net::ESubObjectInsertionOrder InsertionOrder)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	EAddSubObjectFlags AddSubObjectFlags = EAddSubObjectFlags::Default;

	switch(InsertionOrder)
	{
		case ESubObjectInsertionOrder::None: 
			break;
		case ESubObjectInsertionOrder::ReplicateWith:
			AddSubObjectFlags |= EAddSubObjectFlags::ReplicateWithSubObject;
			break;
		case ESubObjectInsertionOrder::InsertAtStart:
			AddSubObjectFlags |= EAddSubObjectFlags::InsertAtStart;
			break;
		default:
			checkf(false, TEXT("Missing implementation of ESubObjectInsertionOrder enum"));
			break;
	}

	if (NetRefHandleManager->AddSubObject(OwnerHandle, SubObjectHandle, InsertRelativeToSubObjectHandle, AddSubObjectFlags))
	{
		// If the subobject is new we need to update it immediately to pick it up for replication with its new parent
		ForceNetUpdate(ReplicationSystem->GetId(), NetRefHandleManager->GetInternalIndex(SubObjectHandle));

		// We set the priority of subobjects to be static as they will be prioritized with owner
		ReplicationSystem->SetStaticPriority(SubObjectHandle, 1.0f);
	}
}

void UReplicationBridge::InternalDestroySubObjects(FNetRefHandle OwnerHandle, EEndReplicationFlags Flags)
{
	using namespace UE::Net::Private;

	// Destroy SubObjects
	FInternalNetRefIndex OwnerInternalIndex = NetRefHandleManager->GetInternalIndex(OwnerHandle);
	if (OwnerInternalIndex)
	{
		for (FInternalNetRefIndex SubObjectInternalIndex : NetRefHandleManager->GetChildSubObjects(OwnerInternalIndex))
		{
			FNetRefHandleManager::FReplicatedObjectData& SubObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(SubObjectInternalIndex);

			const FNetRefHandle SubObjectHandle = SubObjectData.RefHandle;
			const bool bDestroySubObjectWithOwner = SubObjectData.bDestroySubObjectWithOwner;
				
			// Tag subobject for destroy. The check against the scope is needed since the subobjects array might contain subobjects already pending destroy.
			if (bDestroySubObjectWithOwner && NetRefHandleManager->IsScopableIndex(SubObjectInternalIndex))
			{
				SubObjectData.bPendingEndReplication = 1U;
				UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("InternalDestroySubObjects %s"), *NetRefHandleManager->PrintObjectFromNetRefHandle(SubObjectHandle));
				DestroyLocalNetHandle(SubObjectHandle, Flags);
			}
		}
	}
}

void UReplicationBridge::StopReplicatingNetRefHandle(FNetRefHandle Handle, EEndReplicationFlags EndReplicationFlags)
{
	using namespace UE::Net::Private;

	if (!IsReplicatedHandle(Handle))
	{
		return;
	}

	const FInternalNetRefIndex InternalReplicationIndex = NetRefHandleManager->GetInternalIndex(Handle);
	if (NetRefHandleManager->IsLocal(InternalReplicationIndex))
	{
		FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalReplicationIndex);

		if (ObjectData.bPendingEndReplication && !bAllowDestroyToCancelFlushAndTearOff)
		{
			UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("Ignoring EndReplication called on object already PendingEndReplication %s."), *NetRefHandleManager->PrintObjectFromNetRefHandle(Handle));
			return;
		}

		if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::TearOff))
		{
			// Add handle to list of objects pending EndReplication indicate that it should be destroyed during next update
			// We need to do this to cover the case where the torn off object not yet has been added to the scope.
			AddPendingEndReplication(Handle, EndReplicationFlags);

			// We do however copy the final state data and mark object to stop propagating state changes
			InternalTearOff(Handle);

			// Detach instance as we must assume that we should not access the object after this call.
			ObjectData.bPendingEndReplication = 1U;
			InternalDetachInstanceFromNetRefHandle(InternalReplicationIndex);
		}
		else 
		{
			// New objects, destroyed during the same frame with posted attachments(RPC):s needs to request a flush to ensure that they get a scope update
			const UE::Net::Private::FNetBlobManager& NetBlobManager = GetReplicationSystem()->GetReplicationSystemInternal()->GetNetBlobManager();
			const bool bAllowAutoFlushOfUnProcessedReliableRPCs = bEnableFlushReliableRPCOnDestroy && ObjectData.bNeedsFullCopyAndQuantize;
			if (bAllowAutoFlushOfUnProcessedReliableRPCs && NetBlobManager.HasUnprocessedReliableAttachments(InternalReplicationIndex))
			{
				EnumAddFlags(EndReplicationFlags, EEndReplicationFlags::Flush);
			}

			if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::Flush))
			{
				// Defer destroy until after scope update to allow create/destroy on same frame
				AddPendingEndReplication(Handle, EndReplicationFlags);
			
				// Capture final state
				InternalFlushStateData(Handle);

				// Detach instance as we must assume that we should not access the object after this call.
				ObjectData.bPendingEndReplication = 1U;
				InternalDetachInstanceFromNetRefHandle(InternalReplicationIndex);
			}
			else
			{
				ObjectData.bPendingEndReplication = 1U;
				DestroyLocalNetHandle(Handle, EndReplicationFlags);	
			}	
		}
	}
	else
	{

		// While we are inside ReceiveUpdate, queue stop replication requests instead of immediately stopping replication
		// This allows us the apply any received updates before we cut off this object
		if (IsInReceiveUpdate())
		{
			UE_LOG(LogIris, Verbose, TEXT("Delayed request to StopReplicating %s (flags: %s) because it was called while inside ReceiveUpdate"), *NetRefHandleManager->PrintObjectFromNetRefHandle(Handle), *LexToString(EndReplicationFlags));
		
			// Detect if we have diverging EndReplicationFlags for the same netobject
	#if DO_ENSURE
			{
				EEndReplicationFlags* PreviousFlags = HandlesToStopReplicating.Find(Handle);
				const bool bPreviousFlagsMatch = PreviousFlags ? (*PreviousFlags) == EndReplicationFlags : true;
				ensureMsgf(bPreviousFlagsMatch, TEXT("Received multiple StopReplicating calls for %s with different EndReplicationFlags: Previous: %s | Newest: %s"),
					*NetRefHandleManager->PrintObjectFromNetRefHandle(Handle), *LexToString(*PreviousFlags), *LexToString(EndReplicationFlags));
			}
	#endif

			HandlesToStopReplicating.Add(Handle, EndReplicationFlags);
			return;
		}

		if (InternalReplicationIndex != FNetRefHandleManager::InvalidInternalIndex && EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::SkipPendingEndReplicationValidation))
		{
			NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalReplicationIndex).bPendingEndReplication = 1U;
		}
		// If we get a call to end replication on the client, we need to detach the instance as it might be garbage collected
		InternalDetachInstanceFromNetRefHandle(InternalReplicationIndex);
	}
}



void UReplicationBridge::TearOffHandlesPendingTearOff()
{
	// Initiate tear off
	for (FPendingEndReplicationInfo Info : MakeArrayView(HandlesPendingEndReplication))
	{
		if (EnumHasAnyFlags(Info.DestroyFlags, EEndReplicationFlags::TearOff))
		{
			InternalTearOff(Info.Handle);
		}
	}
}

void UReplicationBridge::UpdateHandlesPendingEndReplication()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	TArray<FPendingEndReplicationInfo, TInlineAllocator<32>> ObjectsStillPendingEndReplication;
	for (FPendingEndReplicationInfo Info : MakeArrayView(HandlesPendingEndReplication))
	{
		if (FInternalNetRefIndex ObjectInternalIndex = NetRefHandleManager->GetInternalIndex(Info.Handle))
		{
			// Immediate destroy or objects that are no longer are referenced by any connections are destroyed
			if (NetRefHandleManager->GetNetObjectRefCount(ObjectInternalIndex) == 0U || Info.Immediate == EPendingEndReplicationImmediate::Yes)
			{
				FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(ObjectInternalIndex);
				ObjectData.bPendingEndReplication = 1U;
				DestroyLocalNetHandle(Info.Handle, Info.DestroyFlags);
			}
			else
			{
				// If the object is still in scope remove it from scope as objects pending EndReplication should not be added to new connections after the first update.
				if (NetRefHandleManager->IsScopableIndex(ObjectInternalIndex))
				{
					// Mark object and subobjects as no longer scopeable, and that we should not propagate changed states
					NetRefHandleManager->RemoveFromScope(ObjectInternalIndex);
					for (FInternalNetRefIndex SubObjectIndex : NetRefHandleManager->GetSubObjects(ObjectInternalIndex))
					{
						NetRefHandleManager->RemoveFromScope(SubObjectIndex);
					}
				}
			
				// Keep object in the pending EndReplication-list until the object is no longer referenced by any ReplicationWriter
				ObjectsStillPendingEndReplication.Add(FPendingEndReplicationInfo(Info.Handle, Info.DestroyFlags, EPendingEndReplicationImmediate::No));
			}
		}
	}

	HandlesPendingEndReplication.Reset();
	HandlesPendingEndReplication.Insert(ObjectsStillPendingEndReplication.GetData(), ObjectsStillPendingEndReplication.Num(), 0);

	CSV_CUSTOM_STAT(Iris, NumHandlesPendingEndRepliction, (float)HandlesPendingEndReplication.Num(), ECsvCustomStatOp::Set);
}

void UReplicationBridge::AddPendingEndReplication(FNetRefHandle Handle, EEndReplicationFlags DestroyFlags, EPendingEndReplicationImmediate Immediate)
{
	if (ensure(EnumHasAnyFlags(DestroyFlags, EEndReplicationFlags::Flush | EEndReplicationFlags::TearOff)))
	{
		if (!HandlesPendingEndReplication.FindByPredicate([&](const FPendingEndReplicationInfo& Info){ return Info.Handle == Handle; }))
		{
			HandlesPendingEndReplication.Add(FPendingEndReplicationInfo(Handle, DestroyFlags, Immediate));
		}
	}
}

void UReplicationBridge::InternalFlushStateData(UE::Net::FNetSerializationContext& SerializationContext, UE::Net::Private::FChangeMaskCache& ChangeMaskCache, UE::Net::FNetBitStreamWriter& ChangeMaskWriter, uint32 InternalObjectIndex)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	if (InternalObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalObjectIndex);

	// Copy state data, if object already is torn off there is nothing to do
	if (ObjectData.bTearOff)
	{
		return;
	}

	UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("InternalFlushStateData Initiating flush for %s (InternalIndex: %u)"), *ObjectData.RefHandle.ToString(), InternalObjectIndex);

	if (ObjectData.InstanceProtocol)
	{
		if (EnumHasAnyFlags(ObjectData.InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::NeedsPoll | EReplicationInstanceProtocolTraits::NeedsPreSendUpdate))
		{
			CallPreSendUpdateSingleHandle(ObjectData.RefHandle);
		}

		// Cache creation info
		ObjectData.bHasCachedCreationInfo =  CallCacheNetRefHandleCreationInfo(ObjectData.RefHandle) ? 1U : 0U;

		FReplicationInstanceOperationsInternal::QuantizeObjectStateData(ChangeMaskWriter, ChangeMaskCache, *NetRefHandleManager, SerializationContext, InternalObjectIndex);

		// Clear the quantize flag since it was done directly here.
		NetRefHandleManager->GetDirtyObjectsToQuantize().ClearBit(InternalObjectIndex);
	}

	for (FInternalNetRefIndex SubObjectInternalIndex : NetRefHandleManager->GetChildSubObjects(InternalObjectIndex))
	{
		InternalFlushStateData(SerializationContext, ChangeMaskCache, ChangeMaskWriter, SubObjectInternalIndex);
	}

	// $IRIS TODO:  Should we also clear the DirtyTracker flags for this flushed object ?
}

void UReplicationBridge::InternalFlushStateData(FNetRefHandle Handle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(InternalFlushStateData);

	const uint32 InternalObjectIndex = NetRefHandleManager->GetInternalIndex(Handle);
	if (InternalObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalObjectIndex);

	FChangeMaskCache ChangeMaskCache;
	FNetBitStreamWriter ChangeMaskWriter;

	// Setup context
	FNetSerializationContext SerializationContext;
	FInternalNetSerializationContext InternalContext(ReplicationSystem);
	SerializationContext.SetInternalContext(&InternalContext);
	SerializationContext.SetNetStatsContext(ReplicationSystem->GetReplicationSystemInternal()->GetNetTypeStats().GetNetStatsContext());

	InternalFlushStateData(SerializationContext, ChangeMaskCache, ChangeMaskWriter, InternalObjectIndex);

	// Iterate over connections and propagate dirty changemasks to all connections already scoping this object
	if (ChangeMaskCache.Indices.Num() > 0)
	{
		FReplicationConnections& Connections = ReplicationSystem->GetReplicationSystemInternal()->GetConnections();

		auto&& UpdateDirtyChangeMasks = [&Connections, &ChangeMaskCache](uint32 ConnectionId)
		{
			FReplicationConnection* Conn = Connections.GetConnection(ConnectionId);			

			const bool bMarkForTearOff = false;
			Conn->ReplicationWriter->ForceUpdateDirtyChangeMasks(ChangeMaskCache, FReplicationWriter::FlushFlags_FlushState, bMarkForTearOff);
		};
		const FNetBitArray& ValidConnections = Connections.GetValidConnections();
		ValidConnections.ForAllSetBits(UpdateDirtyChangeMasks);		
	}
}

void UReplicationBridge::InternalTearOff(FNetRefHandle Handle)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(InternalTearOff);

	const uint32 InternalObjectIndex = NetRefHandleManager->GetInternalIndex(Handle);
	if (InternalObjectIndex == FNetRefHandleManager::InvalidInternalIndex)
	{
		return;
	}

	FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalObjectIndex);

	if (ObjectData.bTearOff)
	{
		// Already torn off
		return;
	}

	// Copy state data and tear off now
	UE_LOG_REPLICATIONBRIDGE(Verbose, TEXT("TearOff: %s"), *PrintObjectFromNetRefHandle(Handle));

	// Force copy of final state data as we will detach the object after scope update
	FChangeMaskCache ChangeMaskCache;
	FNetBitStreamWriter ChangeMaskWriter;

	// Setup context
	FNetSerializationContext SerializationContext;
	FInternalNetSerializationContext InternalContext(ReplicationSystem);
	SerializationContext.SetInternalContext(&InternalContext);
	SerializationContext.SetNetStatsContext(ReplicationSystem->GetReplicationSystemInternal()->GetNetTypeStats().GetNetStatsContext());

	if (ObjectData.InstanceProtocol)
	{
		if (EnumHasAnyFlags(ObjectData.InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::NeedsPoll | EReplicationInstanceProtocolTraits::NeedsPreSendUpdate))
		{
			CallPreSendUpdateSingleHandle(Handle);
		} 

		// Cache creation info
		ObjectData.bHasCachedCreationInfo =  CallCacheNetRefHandleCreationInfo(Handle) ? 1U : 0U;
	}

	if (ObjectData.InstanceProtocol && ObjectData.Protocol->InternalTotalSize > 0U)
	{
		FReplicationInstanceOperationsInternal::QuantizeObjectStateData(ChangeMaskWriter, ChangeMaskCache, *NetRefHandleManager, SerializationContext, InternalObjectIndex);

		// Clear the quantize flag since it was done directly here.
		NetRefHandleManager->GetDirtyObjectsToQuantize().ClearBit(InternalObjectIndex);
	}
	else
	{
		// Nothing to copy, but we must still propagate the tear-off state.
		FChangeMaskCache::FCachedInfo& Info = ChangeMaskCache.AddEmptyChangeMaskForObject(InternalObjectIndex);
		// If we are a subobject we must also mark owner as dirty.
		const uint32 SubObjectOwnerIndex = ObjectData.SubObjectRootIndex;
		if (SubObjectOwnerIndex != FNetRefHandleManager::InvalidInternalIndex) 
		{
			ChangeMaskCache.AddSubObjectOwnerDirty(SubObjectOwnerIndex);
		}			
	}

	// Propagate changes to all connections that we currently have in scope
	FReplicationConnections& Connections = ReplicationSystem->GetReplicationSystemInternal()->GetConnections();

	// Iterate over connections and propagate dirty changemasks to all connections already scoping this object
	auto UpdateDirtyChangeMasks = [&Connections, &ChangeMaskCache](uint32 ConnectionId)
	{
		FReplicationConnection* Conn = Connections.GetConnection(ConnectionId);
		const bool bMarkForTearOff = true;
		Conn->ReplicationWriter->ForceUpdateDirtyChangeMasks(ChangeMaskCache, FReplicationWriter::FlushFlags_None, bMarkForTearOff);
	};
	const FNetBitArray& ValidConnections = Connections.GetValidConnections();
	ValidConnections.ForAllSetBits(UpdateDirtyChangeMasks);		

	// TearOff subobjects as well.
	for (FInternalNetRefIndex SubObjectInternalIndex : NetRefHandleManager->GetChildSubObjects(InternalObjectIndex))
	{
		InternalTearOff(NetRefHandleManager->GetNetRefHandleFromInternalIndex(SubObjectInternalIndex));
	}	

	// Mark object as being torn-off and that we should no longer propagate state changes
	ObjectData.bTearOff = 1U;
	ObjectData.bShouldPropagateChangedStates = 0U;
}

bool UReplicationBridge::IsReplicatedHandle(FNetRefHandle Handle) const
{
	return Handle.IsValid() && NetRefHandleManager->IsNetRefHandleAssigned(Handle);
}

void UReplicationBridge::SetNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments, const UE::Net::Private::FNetPushObjectHandle& PushHandle)
{
#if WITH_PUSH_MODEL
	using namespace UE::Net;

	constexpr uint32 MaxFragmentOwnerCount = 1U;
	UObject* FragmentOwners[MaxFragmentOwnerCount] = {};
	FReplicationStateOwnerCollector FragmentOwnerCollector(FragmentOwners, MaxFragmentOwnerCount);
	UObject* Instance = nullptr;
	for (const FReplicationFragment* Fragment : Fragments)
	{
		if (!EnumHasAnyFlags(Fragment->GetTraits(), EReplicationFragmentTraits::HasPushBasedDirtiness))
		{
			continue;
		}

		FragmentOwnerCollector.Reset();
		Fragment->CollectOwner(&FragmentOwnerCollector);
		UObject* FragmentOwner = (FragmentOwnerCollector.GetOwnerCount() > 0 ? FragmentOwnerCollector.GetOwners()[0] : static_cast<UObject*>(nullptr));
		if (FragmentOwner != nullptr && FragmentOwner != Instance)
		{
			Instance = FragmentOwner;
			UE_NET_IRIS_SET_PUSH_ID(Instance, PushHandle);
		}
	}
#endif
}

void UReplicationBridge::ClearNetPushIdOnFragments(const TArrayView<const UE::Net::FReplicationFragment*const>& Fragments)
{
#if WITH_PUSH_MODEL
	using namespace UE::Net;

	constexpr uint32 MaxFragmentOwnerCount = 1U;
	UObject* FragmentOwners[MaxFragmentOwnerCount] = {};
	FReplicationStateOwnerCollector FragmentOwnerCollector(FragmentOwners, MaxFragmentOwnerCount);
	UObject* Instance = nullptr;
	for (const FReplicationFragment* Fragment : Fragments)
	{
		if (!EnumHasAnyFlags(Fragment->GetTraits(), EReplicationFragmentTraits::HasPushBasedDirtiness))
		{
			continue;
		}

		FragmentOwnerCollector.Reset();
		Fragment->CollectOwner(&FragmentOwnerCollector);
		UObject* FragmentOwner = (FragmentOwnerCollector.GetOwnerCount() > 0 ? FragmentOwnerCollector.GetOwners()[0] : static_cast<UObject*>(nullptr));
		if (FragmentOwner != nullptr && FragmentOwner != Instance)
		{
			Instance = FragmentOwner;
			if (IsValid(FragmentOwner))
			{
				UE_NET_IRIS_CLEAR_PUSH_ID(FragmentOwner);
			}
		}
	}
#endif
}

void UReplicationBridge::NotifyStreamingLevelUnload(const UObject* Level)
{
	// Destroy group associated with level
	UE::Net::FNetObjectGroupHandle LevelGroupHandle;
	if (LevelGroups.RemoveAndCopyValue(FObjectKey(Level), LevelGroupHandle))
	{
		RemoveDestructionInfosForGroup(LevelGroupHandle);
		ReplicationSystem->DestroyGroup(LevelGroupHandle);
	}
}

UE::Net::FNetObjectGroupHandle UReplicationBridge::CreateLevelGroup(const UObject* Level, FName PackageName)
{
	using namespace UE::Net;

	FNetObjectGroupHandle LevelGroupHandle = ReplicationSystem->CreateGroup(PackageName);
	if (ensure(LevelGroupHandle.IsValid()))
	{
		ReplicationSystem->AddExclusionFilterGroup(LevelGroupHandle);
		LevelGroups.Emplace(FObjectKey(Level), LevelGroupHandle);
	}

	return LevelGroupHandle;
}

UE::Net::FNetObjectFactoryId UReplicationBridge::GetNetObjectFactoryId(FNetRefHandle RefHandle) const
{
	return NetRefHandleManager->GetReplicatedObjectData(NetRefHandleManager->GetInternalIndex(RefHandle)).NetFactoryId;
}

UE::Net::FNetObjectGroupHandle UReplicationBridge::GetLevelGroup(const UObject* Level) const
{
	const UE::Net::FNetObjectGroupHandle* LevelGroupHandle = LevelGroups.Find(FObjectKey(Level));
	return (LevelGroupHandle != nullptr ? *LevelGroupHandle : UE::Net::FNetObjectGroupHandle());
}

const TMap<FObjectKey, UE::Net::FNetObjectGroupHandle>& UReplicationBridge::GetAllLevelGroups() const
{
	return LevelGroups;
}

void UReplicationBridge::DestroyGlobalNetHandle(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalReplicationIndex);
	if (ObjectData.NetHandle.IsValid())
	{
		FNetHandleDestroyer::DestroyNetHandle(ObjectData.NetHandle);
	}
}

void UReplicationBridge::ClearNetPushIds(UE::Net::Private::FInternalNetRefIndex InternalReplicationIndex)
{
#if WITH_PUSH_MODEL
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager->GetReplicatedObjectDataNoCheck(InternalReplicationIndex);
	if (const FReplicationInstanceProtocol* InstanceProtocol = ObjectData.InstanceProtocol)
	{
		if (EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::HasPushBasedDirtiness))
		{
			TArrayView<const FReplicationFragment* const> Fragments(InstanceProtocol->Fragments, InstanceProtocol->FragmentCount);
			ClearNetPushIdOnFragments(Fragments);
		}
	}
#endif
}

FString UReplicationBridge::PrintObjectFromNetRefHandle(FNetRefHandle RefHandle) const
{
	return NetRefHandleManager->PrintObjectFromNetRefHandle(RefHandle);
}

void UReplicationBridge::PreSeamlessTravelGarbageCollect()
{
	RemoveDestructionInfosForGroup(UE::Net::FNetObjectGroupHandle());

	for (const TPair<FObjectKey, UE::Net::FNetObjectGroupHandle>& LevelAndGroup : LevelGroups)
	{
		ReplicationSystem->DestroyGroup(LevelAndGroup.Value);
	}
	LevelGroups.Empty();

	OnPreSeamlessTravelGarbageCollect();
}

void UReplicationBridge::OnPreSeamlessTravelGarbageCollect()
{
}

void UReplicationBridge::PostSeamlessTravelGarbageCollect()
{
	OnPostSeamlessTravelGarbageCollect();
}

void UReplicationBridge::OnPostSeamlessTravelGarbageCollect()
{
}
