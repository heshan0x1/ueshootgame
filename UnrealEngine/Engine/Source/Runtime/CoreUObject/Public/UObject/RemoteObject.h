// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/RemoteObjectTypes.h"
#include "UObject/ObjectMacros.h"

class UObject;
class UClass;

/**
 * Remote objects are unique UObjects that are referenced by a local server instance but their memory is actually owned by (exists on) another server.
 * 
 * It's possible that an object is remote but its memory hasn't been freed yet (UObject with EInternalObjectFlags::Remote flag that hasn't been GC'd yet). 
 * In such case any attempt to access that object through TObjectPtr will result in its memory being migrated from a remote server to a local server. 
 * Remote object memory is freed in the next GC pass after the object has been migrated and any existing references to that object (must be referenced by TObjectPtr) 
 * will be updated by GC to point to the remote object's FRemoteObjectStub. 
 */
namespace UE::RemoteObject
{
	/**
	* Returns a unique id associated with this (server) process
	*/
	UE_DEPRECATED(5.6, "Use FRemoteServerId::GetLocalServerId() instead.")
	UE_FORCEINLINE_HINT FRemoteServerId GetGlobalServerId()
	{
		return FRemoteServerId::GetLocalServerId();
	}
}

namespace UE::RemoteObject::Handle
{
	struct FPhysicsIslandId
	{
		friend struct FRemoteObjectId;

		FRemoteServerId PhysicsServerId;

		//IMPORTANT: LocalIslandIndex = 0 means it's invalid. 
		uint32 PhysicsLocalIslandId = 0;

		void Reset();

		FPhysicsIslandId() = default;

		FPhysicsIslandId(const FRemoteServerId& PhysicsServerIdIn, const uint32 PhysicsLocalIslandIdIn)
			: PhysicsServerId(PhysicsServerIdIn), PhysicsLocalIslandId(PhysicsLocalIslandIdIn)
		{
		}

		bool operator==(const FPhysicsIslandId& Other) const
		{
			return PhysicsServerId == Other.PhysicsServerId && PhysicsLocalIslandId == Other.PhysicsLocalIslandId;
		}

		bool IsValid() const
		{
			return PhysicsServerId.IsValid() && PhysicsLocalIslandId != INDEX_NONE;
		}

		bool operator<(const FPhysicsIslandId& Other) const
		{
			return PhysicsServerId < Other.PhysicsServerId ||
				(PhysicsServerId == Other.PhysicsServerId && PhysicsLocalIslandId < Other.PhysicsLocalIslandId);
		}

		bool operator<=(const FPhysicsIslandId& Other) const
		{
			return (*this) < Other || (*this) == Other;
		}

		friend FArchive& operator<<(FArchive& Ar, FPhysicsIslandId& ID)
		{
			Ar << ID.PhysicsServerId;
			Ar << ID.PhysicsLocalIslandId;
			return Ar;
		}

		FString ToString() const
		{
			return FString::Printf(TEXT("[%s, %d]"), *(PhysicsServerId.ToString()), PhysicsLocalIslandId);
		}
	};

	UE_FORCEINLINE_HINT uint32 GetTypeHash(const FPhysicsIslandId& Key)
	{
		// Combine hashes of the members
		return HashCombine(::GetTypeHash(Key.PhysicsServerId.GetIdNumber()), ::GetTypeHash(Key.PhysicsLocalIslandId));
	}

	/**
	* Structure that holds a pointer to a native class or to a pathname of a class on disk (Blueprint etc)
	*/
	struct FRemoteObjectClass
	{
		UPTRINT PathNameOrClass = 0;

		FRemoteObjectClass() = default;
		COREUOBJECT_API explicit FRemoteObjectClass(UClass* InClass);

		bool IsNative() const
		{
			return !(PathNameOrClass & 1);
		}

		bool IsValid() const
		{
			return !!PathNameOrClass;
		}
		
		COREUOBJECT_API UClass* GetClass() const;
	};

	// Structure that holds basic information about a remote object
	// This is what FObjectPtr that references a remote object actually points to after the remote object's memory has been claimed by GC
	struct FRemoteObjectStub
	{
		FRemoteObjectId Id;
		FRemoteObjectId OuterId;
		FName Name;
		FRemoteObjectClass Class;

		/** SerialNumber this object had on this server */
		int32 SerialNumber = 0;

		/** Server id that where the object currently resides */
		FRemoteServerId ResidentServerId;

		/** Server id of the server that has ownership of the object
			(note: only valid if the object is Local) */
		FRemoteServerId OwningServerId;

		FPhysicsIslandId PhysicsIslandId;

		FRemoteObjectStub() = default;
		COREUOBJECT_API explicit FRemoteObjectStub(UObject* Object);
	};

	enum class ERemoteReferenceType
	{
		Strong = 0,
		Weak = 1
	};

	enum class ERemoteObjectGetClassBehavior
	{
		/** If the object was never local, then simply return a null class */
		ReturnNullIfNeverLocal,

		/** If the object was never local, force a migration to enforce a correct return value */
		MigrateIfNeverLocal,
	};

	/**
	* Resolves a remote object given its stub, aborting the active transaction if the object is unavailable
	* @param Stub Basic data required to migrate a remote object
	* @param RefType Reference type that wants to resolve an object
	* @return Resolved object
	*/
	COREUOBJECT_API UObject* ResolveObject(const FRemoteObjectStub* Stub, ERemoteReferenceType RefType = ERemoteReferenceType::Strong);

	/**
	* Resolves a remote object, aborting the active transaction if the object is unavailable
	* @param Object Object to resolve (remote object memory that has not yet been GC'd)
	* @param RefType Reference type that wants to resolve an object
	* @return Resolved object
	*/
	COREUOBJECT_API UObject* ResolveObject(UObject* Object, ERemoteReferenceType RefType = ERemoteReferenceType::Strong);
	
	COREUOBJECT_API void TouchResidentObject(UObject* Object);

	/**
	 * Attempts to Get the Class of a given ObjectId without performing a migration.  This will succeed if the ObjectId has ever been local.
	 * If the RemoteObjectId has never been local, you can decide if a Migration will occur or Null should be returned.
	 */
	COREUOBJECT_API UClass* GetClass(FRemoteObjectId ObjectId, ERemoteObjectGetClassBehavior GetClassBehavior);

	/**
	* Resolves a remote object
	* @param Object Object id to resolve
	* @param RefType Reference type that wants to resolve an object
	* @return True if a remote object can be resolved
	*/
	COREUOBJECT_API bool CanResolveObject(FRemoteObjectId ObjectId);

	/**
	* Checks if an object associated with the specified unique id is remote
	* @return True if an object associated with the specified unique id is remote
	*/
	COREUOBJECT_API bool IsRemote(FRemoteObjectId ObjectId);

	/**
	* Checks if an object (memory that has not yet been GC'd) is remote
	* @return True if the object is remote
	*/
	COREUOBJECT_API bool IsRemote(const UObject* Object);

	/**
	* Checks if a locally resident object is owned by this server
	*/
	COREUOBJECT_API bool IsOwned(const UObject* Object);

	/**
	* Checks if an object id is owned by this server
	* We are only able to check if we own the object. If we don't
	* own the object then we don't have a reliable way of knowing
	* who the owner is, which is why the below GetOwnerServerId
	* function requires the object be locally resident
	*/
	COREUOBJECT_API bool IsOwned(FRemoteObjectId ObjectId);

	/**
	* Get the owner server id for a locally resident object
	*/
	COREUOBJECT_API FRemoteServerId GetOwnerServerId(const UObject* Object);

	/**
	* Sets the owner server id for a locally resident object
	*/
	COREUOBJECT_API void ChangeOwnerServerId(const UObject* Object, FRemoteServerId NewOwnerServerId);

	/**
	* Get PhysicsIslandId of the input object
	*/
	COREUOBJECT_API FPhysicsIslandId GetPhysicsIslandId(const UObject* Object);

	COREUOBJECT_API void ChangePhysicsIslandId(const UObject* Object, FPhysicsIslandId NewPhysicsIslandId);

	COREUOBJECT_API void ClearAllPhysicsServerId();

	/**
	* Updates FPhysicsIslandId::PhysicsServerId using the input map. Updating PhysicsServerId only because it is the only data needed for physics migration. (SeeDstmPhysicsMigration.cpp)
	*/
	COREUOBJECT_API void UpdateAllPhysicsServerId(const TMap<FPhysicsIslandId, FPhysicsIslandId>& PhysicsServerMergingMap);

	/**
	* Updates PhysicsLocalIslandId of FPhysicsIslandId using the input map. Usage in DstmPhysicsMigration.cpp for local island computation.
	*/
	COREUOBJECT_API void UpdateAllPhysicsLocalIslandId(const TMap<uint32, uint32>& PhysicsIslandMergingMap);
}
