// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/RemoteObject.h"

#include "UObject/RemoteExecutor.h"
#include "UObject/ObjectPtr.h"
#include "UObject/Class.h"
#include "UObject/GCObject.h"
#include "Serialization/ArchiveUObject.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/GarbageCollection.h"
#include "UObject/RemoteObjectTransfer.h"
#include "UObject/RemoteObjectSerialization.h"
#include "UObject/RemoteObjectPrivate.h"
#include "UObject/ObjectHandlePrivate.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectMigrationContext.h"
#include "UObject/UObjectThreadContext.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "String/LexFromString.h"
#include "Templates/Casts.h"
#include "Hash/CityHash.h"
#include "Modules/VisualizerDebuggingState.h"
#include <atomic>

DEFINE_LOG_CATEGORY(LogRemoteObject);

int32 GInitRemoteServerIdBeforeUObjectInit = 0;
static FAutoConsoleVariableRef CVarInitRemoteServerIdBeforeUObjectInit(
	TEXT("ro.InitRemoteServerIdBeforeUObjectInit"),
	GInitRemoteServerIdBeforeUObjectInit,
	TEXT("Initializes remote server id before UObject system is initialized"));

int32 GRemoteIdToStringVerbosity = (int32)ERemoteIdToStringVerbosity::Id;
static FAutoConsoleVariableRef CVarRemoteIdToStringVerbosity(
	TEXT("ro.IdToStringVerbosity"),
	GRemoteIdToStringVerbosity,
	TEXT("Sets the verbosity FRemoteObjectId::ToString() prints the id to log with (1: id only, 2: id + name, 3: id + pathname, 4: id + fullname, 5: id + fullname + attributes"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			if (GRemoteIdToStringVerbosity <= (int32)ERemoteIdToStringVerbosity::Default || GRemoteIdToStringVerbosity > (int32)ERemoteIdToStringVerbosity::Max)
			{
				GRemoteIdToStringVerbosity = (int32)ERemoteIdToStringVerbosity::Id;
			}
		}));

FRemoteServerId FRemoteServerId::GlobalServerId(ERemoteServerIdConstants::Local); // Here Local means uninitialized

void AssignGlobalServerIdDebuggingState()
{
	// CF8B6D3D-3185-453C-AF12-88EB19245359 => cf8b6d3d3185453caf1288eb19245359
	constexpr FGuid GGlobalServerIdGuid = FGuid(0xCF8B6D3D, 0x3185453C, 0xAF1288EB, 0x19245359);

	(void)::UE::Core::FVisualizerDebuggingState::Assign(GGlobalServerIdGuid, &FRemoteServerId::GlobalServerId);
}

void FRemoteServerId::InitGlobalServerId(FRemoteServerId Id)
{
	// Guard against re-initializing the global id unless remote object support is disabled and the id has been initialized to invalid value
	checkf(GlobalServerId.Id == (uint32)ERemoteServerIdConstants::Local || (!FRemoteObjectId::RemoteObjectSupportCompiledIn && !GlobalServerId.IsValid()), 
		TEXT("Global server id has already been initialized (%s)"), *GlobalServerId.ToString());
	GlobalServerId = Id;
}

bool FRemoteServerId::IsGlobalServerIdInitialized()
{
	// This is the only place we check if the id is valid.
	// We can be running a build with remote object support compiled in but disabled in which case we initialize GlobalServerId to an invalid value
	// but we still want to return false in this case because of existing use cases that check this value to determine if remote objects are enabled
	return GlobalServerId.Id != (uint32)ERemoteServerIdConstants::Local;
}


namespace UE::RemoteObject::Private
{

std::atomic<uint64> RemoteObjectSerialNumber(1);
std::atomic<uint64> AssetObjectSerialNumber(1);
int32 UnsafeToMigrateObjects = 0; // This should go into TLS

class FRemoteObjectStubMap : private TMap<FRemoteObjectId, UE::RemoteObject::Handle::FRemoteObjectStub*>
{
	using Super = TMap<FRemoteObjectId, UE::RemoteObject::Handle::FRemoteObjectStub*>;

public:

	using Super::begin;
	using Super::end;

	virtual ~FRemoteObjectStubMap()
	{
		for (TPair<FRemoteObjectId, UE::RemoteObject::Handle::FRemoteObjectStub*>& Pair : *this)
		{
			delete Pair.Value;
		}
	}

	UE::RemoteObject::Handle::FRemoteObjectStub*& FindOrAdd(FRemoteObjectId Id)
	{		
		return Super::FindOrAdd(UE::RemoteObject::Private::FRemoteIdLocalizationHelper::GetLocalized(Id));
	}

	UE::RemoteObject::Handle::FRemoteObjectStub* Find(FRemoteObjectId Id)
	{
		using namespace UE::RemoteObject::Handle;

		UE::RemoteObject::Handle::FRemoteObjectStub** ExistingStub = Super::Find(UE::RemoteObject::Private::FRemoteIdLocalizationHelper::GetLocalized(Id));
		return ExistingStub ? *ExistingStub : nullptr;
	}
};

class FRemoteObjectMaps
{
	mutable FTransactionallySafeCriticalSection ObjectMapCritical;
	// Maps remote object id to to a stub. Note that at the moment stubs are never destroyed (this is required by FRemoteObjectHandlePrivate)
	FRemoteObjectStubMap RemoteObjects;
	// Maps remote object id to to a pathname. Note that at the moment these pathnames are never destroyed (this is required by FRemoteObjectClass)
	TMap<FRemoteObjectId, FRemoteObjectPathName*> AssetPaths;

public:

	virtual ~FRemoteObjectMaps()
	{
		for (TPair<FRemoteObjectId, FRemoteObjectPathName*>& Pair : AssetPaths)
		{
			delete Pair.Value;
		}

	}
	UE::RemoteObject::Handle::FRemoteObjectStub* FindRemoteObjectStub(FRemoteObjectId Id)
	{
		using namespace UE::RemoteObject::Handle;

		UE::TScopeLock MapLock(ObjectMapCritical);
		return RemoteObjects.Find(Id);
	}

	UE::RemoteObject::Handle::FRemoteObjectStub* FindOrAddRemoteObjectStub(FRemoteObjectId Id, FRemoteServerId ResidentServerId = FRemoteServerId())
	{
		using namespace UE::RemoteObject::Handle;

		UE::TScopeLock MapLock(ObjectMapCritical);
		FRemoteObjectStub*& Stub = RemoteObjects.FindOrAdd(Id);
		if (!Stub)
		{
			Stub = new FRemoteObjectStub();
			Stub->Id = Id;
			Stub->ResidentServerId = ResidentServerId.IsValid() ? ResidentServerId : Id.GetServerId();

			// if we are creating the stub, then this object's owner is deduced from its ID
			// if the server ID is invalid then it's a local native object that was created before the local server had its ID assigned
			FRemoteServerId ObjectServerId = Id.GetServerId();
			Stub->OwningServerId = ObjectServerId.IsValid() ? ObjectServerId : FRemoteServerId::GetLocalServerId();
		}		
		return Stub;
	}

	UE::RemoteObject::Handle::FRemoteObjectStub* FindOrAddRemoteObjectStub(UObject* Object, FRemoteServerId DestinationServerId)
	{
		using namespace UE::RemoteObject::Handle;

		UE::TScopeLock MapLock(ObjectMapCritical);
		FRemoteObjectStub*& Stub = RemoteObjects.FindOrAdd(FRemoteObjectId(Object));
		if (!Stub)
		{
			Stub = new FRemoteObjectStub(Object);

			// if we are creating the stub, then this object's owner is deduced from its ID
			// if the server ID is invalid then it's a local native object that was created before the local server had its ID assigned
			FRemoteServerId ObjectServerId = Stub->Id.GetServerId();
			Stub->OwningServerId = ObjectServerId.IsValid() ? ObjectServerId : FRemoteServerId::GetLocalServerId();
		}
		else if (!Stub->Class.IsValid())
		{
			// Always make sure we store the object class in the stub (it's possible the stub was created only with a remote id in which case the class wouldn't be stored)
			Stub->Class = FRemoteObjectClass(Object->GetClass());
		}

		FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
		checkf(ObjectItem, TEXT("Attempting to get a serial number for an object that does not exist in the global UObject array (it's possible GUObjectArray is not initialized yet, ObjectIndex=%d)"), GUObjectArray.ObjectToIndex(Object));
		Stub->SerialNumber = ObjectItem->GetSerialNumber();
		Stub->Name = Object->GetFName();
		Stub->ResidentServerId = DestinationServerId;

		return Stub;
	}

	void ClearAllPhysicsIds()
	{
#if UE_WITH_REMOTE_OBJECT_HANDLE
		for (TPair<FRemoteObjectId, UE::RemoteObject::Handle::FRemoteObjectStub*>& Elem : RemoteObjects)
		{
			if (Elem.Value)
			{
				Elem.Value->PhysicsIslandId.PhysicsServerId = FRemoteServerId();
				Elem.Value->PhysicsIslandId.PhysicsLocalIslandId = 0;
			}
		}
#endif
	}

	FRemoteObjectPathName* StoreAssetPath(UObject* InObject)
	{
		FRemoteObjectId ObjectId(InObject);
		FRemoteObjectPathName*& Path = AssetPaths.FindOrAdd(ObjectId);
		if (!Path)
		{
			Path = new FRemoteObjectPathName(InObject);
		}
		return Path;
	}

	FRemoteObjectPathName* FindAssetPath(FRemoteObjectId ObjectId)
	{
		FRemoteObjectPathName** PathName = AssetPaths.Find(ObjectId);
		return PathName ? *PathName : nullptr;
	}

	/**
	* This function is called by UE::RemoteObject::Handle::UpdateAllPhysicsServerId. More info is shown in that function's description.
	*/
	void UpdateAllPhysicsServerId(const TMap<UE::RemoteObject::Handle::FPhysicsIslandId, UE::RemoteObject::Handle::FPhysicsIslandId>& PhysicsIdMergingMap)
	{
#if UE_WITH_REMOTE_OBJECT_HANDLE
		using UE::RemoteObject::Handle::FPhysicsIslandId;
		for (TPair<FRemoteObjectId, UE::RemoteObject::Handle::FRemoteObjectStub*>& Elem : RemoteObjects)
		{
			if (Elem.Value)
			{
				const FPhysicsIslandId OriginalPhysicsId = Elem.Value->PhysicsIslandId;
				if (const FPhysicsIslandId* MergedPhysicsId = PhysicsIdMergingMap.Find(OriginalPhysicsId))
				{
					Elem.Value->PhysicsIslandId.PhysicsServerId = MergedPhysicsId->PhysicsServerId;
				}
			}
		}
#endif
	}

	void UpdateAllPhysicsLocalIslandId(const TMap<uint32, uint32>& PhysicsLocalIslandMergingMap)
	{
#if UE_WITH_REMOTE_OBJECT_HANDLE
		for (TPair<FRemoteObjectId, UE::RemoteObject::Handle::FRemoteObjectStub*>& Elem : RemoteObjects)
		{
			if (Elem.Value)
			{
				const uint32 OriginalLocalIslandId = Elem.Value->PhysicsIslandId.PhysicsLocalIslandId;
				if (const uint32* MergedLocalIslandId = PhysicsLocalIslandMergingMap.Find(OriginalLocalIslandId))
				{
					Elem.Value->PhysicsIslandId.PhysicsLocalIslandId = *MergedLocalIslandId;
				}
			}
		}
#endif
	}
};
FRemoteObjectMaps* ObjectMaps = nullptr;

void InitServerId()
{
	FRemoteServerId GlobalServerId;
	FString ServerId;
	const TCHAR* CommandLine = FCommandLine::Get();
	if (!FParse::Value(CommandLine, TEXT("MultiServerLocalId="), ServerId))
	{
		if (!FParse::Value(CommandLine, TEXT("LocalPeerId="), ServerId))
		{
			int ListenPort = 0;
			if (FParse::Param(CommandLine, TEXT("MultiServerLocalHost")) && FParse::Value(CommandLine, TEXT("Port="), ListenPort))
			{
				if (ListenPort > 0)
				{
					ServerId = FString::FromInt(ListenPort % 1000);
				}
			}
		}
	}
	if (!ServerId.IsEmpty())
	{
		GlobalServerId = FRemoteServerId::FromString(ServerId);
		checkf(GlobalServerId.IsValid(), TEXT("Remote ServerId is not valid"));

		FRemoteServerId::InitGlobalServerId(GlobalServerId);
	}

	UE_LOG(LogRemoteObject, Display, TEXT("Global Server Id: %s"), *FRemoteServerId::GetLocalServerId().ToString());
}

void InitRemoteObjects()
{
	AssignGlobalServerIdDebuggingState();

	if (!FRemoteObjectId::RemoteObjectSupportCompiledIn)
	{
		FRemoteServerId::InitGlobalServerId(FRemoteServerId());
		// Always init global server id debug visualizer support but early out if remote object support is not compiled in (UE_WITH_REMOTE_OBJECT_HANDLE is 0)
		return;
	}

	ObjectMaps = new FRemoteObjectMaps();
	
	if (GConfig)
	{
		// Get the config value now because console vars are updated automatically after InitRemoteObjects is called
		GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("ro.InitRemoteServerIdBeforeUObjectInit"), GInitRemoteServerIdBeforeUObjectInit, GEngineIni);
	}
	if (GInitRemoteServerIdBeforeUObjectInit)
	{
		InitServerId();
	}

	UE::RemoteObject::Transfer::InitRemoteObjectTransfer();

	if (!UE::RemoteObject::Transfer::RemoteObjectTransferDelegate.IsBound())
	{
		UE::RemoteObject::Transfer::RemoteObjectTransferDelegate.BindStatic(&UE::RemoteObject::Serialization::Disk::SaveObjectToDisk);
	}
	if (!UE::RemoteObject::Transfer::RequestRemoteObjectDelegate.IsBound())
	{
		UE::RemoteObject::Transfer::RequestRemoteObjectDelegate.BindLambda(
			[](FRemoteWorkPriority RequestPriority, FRemoteObjectId ObjectId, FRemoteServerId LastKnownResidentServerId, FRemoteServerId DestinationServerId)
			{
				// Turns a request into an immediate load
				FUObjectMigrationContext MigrationContext {
					.ObjectId = ObjectId, .RemoteServerId = DestinationServerId, .OwnerServerId = LastKnownResidentServerId,
					.PhysicsServerId = LastKnownResidentServerId, .MigrationSide = EObjectMigrationSide::Receive
				};
				UE::RemoteObject::Serialization::Disk::LoadObjectFromDisk(MigrationContext);
			}
		);
	}
	if (!UE::RemoteObject::Transfer::StoreRemoteObjectDataDelegate.IsBound())
	{
		UE::RemoteObject::Transfer::StoreRemoteObjectDataDelegate.BindStatic(&UE::RemoteObject::Serialization::Disk::SaveObjectToDisk);
	}
	if (!UE::RemoteObject::Transfer::RestoreRemoteObjectDataDelegate.IsBound())
	{
		UE::RemoteObject::Transfer::RestoreRemoteObjectDataDelegate.BindStatic(&UE::RemoteObject::Serialization::Disk::LoadObjectFromDisk);
	}

	if (!UE::RemoteExecutor::FetchNextDeferredRPCDelegate.IsBound())
	{
		UE::RemoteExecutor::FetchNextDeferredRPCDelegate.BindStatic([]() { return TOptional<TTuple<FName, FRemoteWorkPriority, bool, TFunction<void(void)>>>(); });
	}

#if UE_WITH_REMOTE_OBJECT_HANDLE
	// We rely on garbage elimination begin disabled because we don't allow it inside of borrowed objects
	checkf(UObjectBaseUtility::IsGarbageEliminationEnabled() == false, TEXT("Remote object support requires garbage elimination to be disabled"));
#endif
}

void ShutdownRemoteObjects()
{
	delete ObjectMaps;
	ObjectMaps = nullptr;
}

void RegisterRemoteObjectId(FRemoteObjectId ObjectId, FRemoteServerId ResidentServerId)
{
	ObjectMaps->FindOrAddRemoteObjectStub(ObjectId, ResidentServerId);
}

void RegisterSharedObject(UObject* Object)
{
	FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
#if UE_WITH_REMOTE_OBJECT_HANDLE
	ObjectItem->SetFlags(EInternalObjectFlags::RemoteReference);
#endif
}

void MarkAsRemote(UObject* Object, FRemoteServerId DestinationServerId)
{
	static_assert(sizeof(FObjectHandle) == sizeof(UObject*));
	checkf(!Object->IsTemplate(), TEXT("Attempted to Migrate Template Object '%s' which is considered an asset and never allowed to migrate"), *GetNameSafe(Object));

	FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
#if UE_WITH_REMOTE_OBJECT_HANDLE
	ObjectItem->SetFlags(EInternalObjectFlags::Remote);
	ObjectItem->ClearFlags(EInternalObjectFlags_RootFlags | EInternalObjectFlags::RemoteReference | EInternalObjectFlags::Borrowed);
#endif
	ObjectMaps->FindOrAddRemoteObjectStub(Object, DestinationServerId);
}

void MarkAsRemoteReference(UObject* Object)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
	ObjectItem->SetFlags(EInternalObjectFlags::RemoteReference);
#endif
}

bool IsRemoteReference(const UObject* Object)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	return Object->HasAnyInternalFlags(EInternalObjectFlags::RemoteReference);
#else
	return false;
#endif
}

void MarkAsBorrowed(UObject* Object)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
	ObjectItem->SetFlags(EInternalObjectFlags::Borrowed);
#endif
}

bool IsBorrowed(const UObject* Object)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	return Object->HasAnyInternalFlags(EInternalObjectFlags::Borrowed);
#else
	return false;
#endif
}

void MarkAsLocal(UObject* Object)
{
	ensureMsgf(!Object->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject | EObjectFlags::RF_ArchetypeObject), TEXT("We're about to set an ArchetypeObject %s as remote reference"), *GetNameSafe(Object));

#if UE_WITH_REMOTE_OBJECT_HANDLE
	FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
	ObjectItem->ClearFlags(EInternalObjectFlags::Remote);
	ObjectItem->SetFlags(EInternalObjectFlags::RemoteReference);
#endif
	ObjectMaps->FindOrAddRemoteObjectStub(Object, FRemoteServerId::GetLocalServerId());
}

void StoreAssetPath(UObject* Object)
{
	// Make sure the asset has a stub and that the stub knows the owner if this asset is the asset server (disk / content)
	ObjectMaps->FindOrAddRemoteObjectStub(Object, FRemoteServerId(ERemoteServerIdConstants::Asset));
	ObjectMaps->StoreAssetPath(Object);
}

FRemoteObjectPathName* FindAssetPath(FRemoteObjectId RemoteId)
{
	return ObjectMaps->FindAssetPath(RemoteId);
}

UE::RemoteObject::Handle::FRemoteObjectStub* FindRemoteObjectStub(FRemoteObjectId ObjectId)
{
	return ObjectMaps->FindRemoteObjectStub(ObjectId);
}

FName GetServerBaseNameForUniqueName(const UClass* Class)
{
	using namespace UE::RemoteObject;

	checkf(Class, TEXT("Unable to generate base name for a unique object name without the object's Class"));

	// Packages follow different naming rules than other UObjects and ATM we're not migrating packages so fall back to Class->GetFName()
	if (FRemoteServerId::IsGlobalServerIdInitialized() && Class->GetFName() != NAME_Package)
	{
		return *FString::Printf(TEXT("%s_S%s"), *Class->GetFName().GetPlainNameString(), *FRemoteServerId::GetLocalServerId().ToString());
	}
	return Class->GetFName();
}

FUnsafeToMigrateScope::FUnsafeToMigrateScope()
{
	UnsafeToMigrateObjects++;
}
FUnsafeToMigrateScope::~FUnsafeToMigrateScope()
{
	UnsafeToMigrateObjects--;
	check(UnsafeToMigrateObjects >= 0);
}

bool IsSafeToMigrateObjects()
{
	// Not a thread safe test but atm we assume we're running single-threaded
	return !(GIsGarbageCollecting || UnsafeToMigrateObjects);
}

} // namespace UE::RemoteObject::Private

namespace UE::RemoteObject::Handle
{

FRemoteObjectClass::FRemoteObjectClass(UClass* InClass)
{
	checkf(InClass, TEXT("FRemoteClassStub requires a valid class"));
	
	if (InClass->IsNative())
	{
		// Native classes are never GC'd so we can just store a raw pointer to a class object
		PathNameOrClass = UPTRINT(InClass);
	}
	else
	{
		// Blueprints and Verse classes are assets that can be GC'd after an object of such class is sent to another server
		// Since we don't want a strong reference to an asset class we store a pathname instead (here we take advantage of the fact that pathnames are never destroyed, see FRemoteObjectMaps)
		// We could cache the class object here for faster access but it wouldn't remove the requirement of storing its pathname (because the class may get GC'd and we want to be able to load it)
		// An alternative to this approach would be to store the class object's remote id but that would require one more lookup (id -> pathname)
		// It's also important to keep this structure small so that FRemoteObjectStub is also small because stubs are never destroyed atm
		PathNameOrClass = UPTRINT(UE::RemoteObject::Private::ObjectMaps->StoreAssetPath(InClass)) | 1;
	}
}

UClass* FRemoteObjectClass::GetClass() const
{
	if (IsNative())
	{
		return reinterpret_cast<UClass*>(PathNameOrClass);
	}
	else
	{
		const FRemoteObjectPathName* PathName = reinterpret_cast<const FRemoteObjectPathName*>(PathNameOrClass & ~UPTRINT(1));
		// Resolve() will first attempt to find the class in memory and if it doesn't exist, load it.
		return Cast<UClass>(PathName->Resolve());
	}
}

FRemoteObjectStub::FRemoteObjectStub(UObject* Object)
{
	using namespace UE::CoreUObject::Private;

	if (Object)
	{
		Id = FObjectHandleUtils::GetRemoteId(Object);
		Class = FRemoteObjectClass(Object->GetClass());

		if (UObject* Outer = FObjectHandleUtils::GetNonAccessTrackedOuterNoResolve(Object))
		{
			OuterId = FObjectHandleUtils::GetRemoteId(Outer);
		}
	}
}

bool IsRemote(FRemoteObjectId ObjectId)
{
	using namespace UE::RemoteObject::Private;

	if (!ObjectId.IsValid())
	{
		return false;
	}

	if (UObject* Object = StaticFindObjectFastInternal(ObjectId))
	{
		return IsRemote(Object);
	}

	if (FindRemoteObjectStub(ObjectId))
	{
		return true;
	}

	const FRemoteServerId ServerId = ObjectId.GetServerId();
	// Invalid server Id means local native classes which are created before a server has a chance to have an id assigned
	return ServerId.IsValid() && !ServerId.IsLocal() && !ServerId.IsAsset();
}

bool IsRemote(const UObject* Object)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	const int32 InternalIndex = GUObjectArray.ObjectToIndex(Object);
	const bool bIsRemote = InternalIndex >= 0 && GUObjectArray.IndexToObject(InternalIndex)->HasAnyFlags(EInternalObjectFlags::Remote);
	return bIsRemote;
#else
	return false;
#endif
}

bool IsOwned(const UObject* Object)
{
	using namespace UE::CoreUObject::Private;
	return IsOwned(FObjectHandleUtils::GetRemoteId(Object));
}

bool IsOwned(FRemoteObjectId ObjectId)
{
	using namespace UE::CoreUObject::Private;
	using namespace UE::RemoteObject::Private;

	bool bResult = true;
#if UE_WITH_REMOTE_OBJECT_HANDLE
	UE::RemoteObject::Handle::FRemoteObjectStub* RemoteStub = FindRemoteObjectStub(ObjectId);
	if (RemoteStub)
	{
		bResult = (RemoteStub->OwningServerId == FRemoteServerId::GetLocalServerId() || RemoteStub->OwningServerId.IsAsset());
	}
	else
	{
		const FRemoteServerId ServerId = ObjectId.GetServerId();
		// Invalid server Id means local native objects which were created before the local server had a chance to have an id assigned
		bResult = (!ServerId.IsValid() || ServerId.IsAsset() || ServerId.IsLocal());
	}
#endif
	return bResult;
}

FRemoteServerId GetOwnerServerId(const UObject* Object)
{
	using namespace UE::CoreUObject::Private;
	using namespace UE::RemoteObject::Private;

	FRemoteServerId Result;
#if UE_WITH_REMOTE_OBJECT_HANDLE
	UE::RemoteObject::Handle::FRemoteObjectStub* RemoteStub = FindRemoteObjectStub(FObjectHandleUtils::GetRemoteId(Object));
	if (RemoteStub)
	{
		Result = RemoteStub->OwningServerId;
	}
	else
	{
		// if the object wasn't received or ever migrated, we own it locally
		Result = FRemoteServerId::GetLocalServerId();
	}
#endif
	return Result;
}

void ChangeOwnerServerId(const UObject* Object, FRemoteServerId NewOwnerServerId)
{
	using namespace UE::CoreUObject::Private;
	using namespace UE::RemoteObject::Private;
	
#if UE_WITH_REMOTE_OBJECT_HANDLE
	UE::RemoteObject::Handle::FRemoteObjectStub* RemoteStub = FindRemoteObjectStub(FObjectHandleUtils::GetRemoteId(Object));

	// The remote stub is always expected to be found for this object.
	if (ensureMsgf(RemoteStub, TEXT("Missing stub for %s (%s / 0x%016llx)"), *GetPathNameSafe(Object), *FRemoteObjectId(Object).ToString(), (int64)(PTRINT)Object))
	{
		RemoteStub->OwningServerId = NewOwnerServerId;
	}
#endif
}

FPhysicsIslandId GetPhysicsIslandId(const UObject* Object)
{
	using namespace UE::CoreUObject::Private;
	using namespace UE::RemoteObject::Private;

	FPhysicsIslandId Result;
#if UE_WITH_REMOTE_OBJECT_HANDLE
	UE::RemoteObject::Handle::FRemoteObjectStub* RemoteStub = FindRemoteObjectStub(FObjectHandleUtils::GetRemoteId(Object));
	if (RemoteStub)
	{
		Result = RemoteStub->PhysicsIslandId;
	}
#endif
	return Result;
}

void ChangePhysicsIslandId(const UObject* Object, FPhysicsIslandId NewPhysicsIslandId)
{
	using namespace UE::CoreUObject::Private;
	using namespace UE::RemoteObject::Private;

#if UE_WITH_REMOTE_OBJECT_HANDLE
	UE::RemoteObject::Handle::FRemoteObjectStub* RemoteStub = ObjectMaps->FindOrAddRemoteObjectStub(FObjectHandleUtils::GetRemoteId(Object));
	if (RemoteStub)
	{
		RemoteStub->PhysicsIslandId = NewPhysicsIslandId;
	}
#endif
}

void ClearAllPhysicsServerId()
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	if (Private::ObjectMaps)
	{
		Private::ObjectMaps->ClearAllPhysicsIds();
	}
#endif
}

void UpdateAllPhysicsServerId(const TMap<FPhysicsIslandId, FPhysicsIslandId>& PhysicsServerMergingMap)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	if (Private::ObjectMaps)
	{
		Private::ObjectMaps->UpdateAllPhysicsServerId(PhysicsServerMergingMap);
	}
#endif
}

/**
 Updates all FPhysicsIslandId::PhysicsLocalIslandId based on input map. Usage can be found in description of UE::RemoteObject::Handle::UpdateAllPhysicsLocalIslandId
*/
void UpdateAllPhysicsLocalIslandId(const TMap<uint32, uint32>& PhysicsLocalIslandMergingMap)
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	if (Private::ObjectMaps)
	{
		Private::ObjectMaps->UpdateAllPhysicsLocalIslandId(PhysicsLocalIslandMergingMap);
	}
#endif
}

UObject* ResolveObject(const FRemoteObjectStub* Stub, ERemoteReferenceType RefType /*= ERemoteReferenceType::Strong*/)
{
	using namespace UE::RemoteObject::Transfer;
	using namespace UE::RemoteObject::Private;

	// This is a slightly faster version of IsRemote(FRemoteObjectId) because we already know a Stub exists and we are going to re-use the Object pointer
	UObject* Object = StaticFindObjectFastInternal(Stub->Id);

	if (!Object && Stub->OwningServerId == FRemoteServerId(ERemoteServerIdConstants::Asset))
	{
		if (FRemoteObjectPathName* AssetPath = FindAssetPath(Stub->Id))
		{
			Object = AssetPath->Resolve();
		}
	}

	if (!IsSafeToMigrateObjects() && (Object || RefType == ERemoteReferenceType::Weak)) // Not a thread-safe test
	{
		// Begin/FinishDestroy overrides may attempt to access subobjects of objects that have been migrated in which case we
		// don't want to accidentally migrate them back mid-purge and if the Object memory is still valid (but has EInternalObjectFlags::Remote flag)
		// we can just return it and let the owner finish its cleanup.
		// In case of weak object ptrs it's relatively safe to just return null if the object doesn't exist on this server (see CanResolveObject)
		TouchResidentObject(Object);
		return Object;
	}

	bool bRemoteObject = !Object || IsRemote(Object);
	if (bRemoteObject)
	{
		checkf(!GIsGarbageCollecting, TEXT("Resolving remote objects while collecting garbage is not allowed (trying to resolve object %s (%s)"), *Stub->Id.ToString(), *Stub->Name.ToString());

		UObject* Outer = Object ? Object->GetOuter() : StaticFindObjectFastInternal(Stub->OuterId);

		MigrateObjectFromRemoteServer(Stub->Id, Stub->ResidentServerId, Outer);

		// if running transactionally, we will have aborted and not reached here

		// if running non-transactionally, object migrated immediately, so we just re-resolve
		Object = StaticFindObjectFastInternal(Stub->Id);
		bRemoteObject = !Object || IsRemote(Object);
		checkf(!bRemoteObject, TEXT("Failed to resolve remote object %s, either this code is not running in a transaction and should be, or the transaction failed to abort"), *Stub->Id.ToString());
	}

	return Object;
}

UObject* ResolveObject(UObject* Object, ERemoteReferenceType RefType /*= ERemoteReferenceType::Strong*/)
{
	using namespace UE::RemoteObject::Transfer;
	using namespace UE::RemoteObject::Private;

	// Begin/FinishDestroy overrides may attempt to access subobjects of objects that have been migrated in which case we
	// don't want to accidentally migrate them back mid-purge and if the Object memory is still valid (but has EInternalObjectFlags::Remote flag)
	// we can just return it and let the owner finish its cleanup.
	if (IsSafeToMigrateObjects()) // Note: this is not a thread-safe check
	{
		FRemoteObjectStub* Stub = ObjectMaps->FindRemoteObjectStub(UE::CoreUObject::Private::FObjectHandleUtils::GetRemoteId(Object));
		checkf(Stub, TEXT("Failed to find remote object stub for %s"), *GetPathNameSafe(Object));
		return ResolveObject(Stub, RefType);
	}

	TouchResidentObject(Object);
	return Object;
}

void TouchResidentObject(UObject* Object)
{
	UE::RemoteObject::Transfer::TouchResidentObject(Object);
}

bool CanResolveObject(FRemoteObjectId ObjectId)
{
	using namespace UE::RemoteObject::Private;

	// Note: this function needs to reflect the logic of ResolveObject(...) functions
	
	if (UObject* Object = StaticFindObjectFastInternal(ObjectId))
	{
		// Object memory is local and even if it's already been migrated we can resolve it
		return true;
	}

	if (FRemoteObjectStub* Stub = ObjectMaps->FindRemoteObjectStub(ObjectId))
	{
		// A stub exists so the object memory is not local but we can (attempt to) migrate it back if we're not garbage collecting
		// Note: GIsGarbageCollecting checks are not thread safe
		return IsSafeToMigrateObjects();
	}

	// ObjectId is local or represents an object that has never been migrated
	return false;
}

UClass* GetClass(FRemoteObjectId ObjectId, ERemoteObjectGetClassBehavior GetClassBehavior)
{
	if (FRemoteObjectStub* Stub = Private::FindRemoteObjectStub(ObjectId))
	{
		if (Stub->Class.IsValid())
		{
			return Stub->Class.GetClass();
		}
		else if (GetClassBehavior == ERemoteObjectGetClassBehavior::ReturnNullIfNeverLocal)
		{
			return nullptr;
		}
		else if (GetClassBehavior == ERemoteObjectGetClassBehavior::MigrateIfNeverLocal)
		{
			UObject* ResolvedObject = ResolveObject(Stub, ERemoteReferenceType::Strong);
			return ResolvedObject->GetClass();
		}
	}

	return nullptr;
}


} // namespace UE::RemoteObject::Handle

namespace UE::CoreUObject::Private
{
#if UE_WITH_REMOTE_OBJECT_HANDLE
	void FObjectHandleUtils::ChangeRemoteId(UObjectBase* Object, FRemoteObjectId Id)
	{
		using namespace UE::RemoteObject::Private;
		UnhashObject(Object);
		FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
		// ObjectItem may not exist when the UObject system hasn't been initialized yet but theoretically this function should only get called when
		// something attempts to re-construct a default subobject that already exists so ObjectItem should always be valid
		checkf(ObjectItem, TEXT("Attempting to change remote ID for an object that does not exist in the global UObject array (it's possible GUObjectArray is not initialized yet, ObjectIndex=%d)"), GUObjectArray.ObjectToIndex(Object));
		ObjectItem->SetRemoteId(Id);
		HashObject(Object);
	}

	FRemoteObjectId FRemoteObjectHandlePrivate::GetRemoteId() const
	{
		if ((PointerOrHandle & UPTRINT(1)))
		{
			return ToStub()->Id;
		}
		return FRemoteObjectId(reinterpret_cast<const UObjectBase*>(PointerOrHandle));
	}

	FRemoteObjectHandlePrivate FRemoteObjectHandlePrivate::ConvertToRemoteHandle(UObject* Object)
	{
		using namespace UE::RemoteObject::Handle;
		using namespace UE::RemoteObject::Private;		

		FRemoteObjectStub* Stub = ObjectMaps->FindRemoteObjectStub(UE::CoreUObject::Private::FObjectHandleUtils::GetRemoteId(Object));
		checkf(Stub, TEXT("Failed to find remote object stub for %s"), *GetPathNameSafe(Object));
		return FRemoteObjectHandlePrivate(Stub);
	}

	FRemoteObjectHandlePrivate FRemoteObjectHandlePrivate::FromIdNoResolve(FRemoteObjectId ObjectId)
	{
		using namespace UE::RemoteObject::Handle;
		using namespace UE::RemoteObject::Private;

		UObject* Obj = nullptr;
		if (ObjectId.IsValid())
		{
			Obj = StaticFindObjectFastInternal(ObjectId);
			if (Obj && !Obj->HasAnyInternalFlags(EInternalObjectFlags::Remote))
			{
				return FRemoteObjectHandlePrivate(Obj);
			}
			else if (FRemoteObjectStub* Stub = ObjectMaps->FindOrAddRemoteObjectStub(ObjectId))
			{
				return FRemoteObjectHandlePrivate(Stub);
			}
			check(false);
		}
		return FRemoteObjectHandlePrivate(Obj);
	}
#endif // UE_WITH_REMOTE_OBJECT_HANDLE

} // namespace UE::CoreUObject::Private

#ifndef UE_WITH_REMOTE_ASSET_ID
#define UE_WITH_REMOTE_ASSET_ID 1 // set this to 0 to disable remote asset IDs
#endif

FRemoteObjectId FRemoteObjectId::Generate(UObjectBase* InObject, const TCHAR* InName, EInternalObjectFlags InInitialFlags /*= EInternalObjectFlags::None*/)
{
	using namespace UE::RemoteObject::Private;
	using namespace UE::CoreUObject::Private;

	bool bIsAsset = false;
#if UE_WITH_REMOTE_ASSET_ID
	if (GIsInitialLoad || !!(InInitialFlags & EInternalObjectFlags::Native) || !!(InObject->GetFlags() & RF_ArchetypeObject))
	{
		// Native objects (classes, CDOs etc) or objects loaded during initial load are always in memory and are considered assets any server can find locally
		// Note that this first condition can not touch too much of UObject API because we might literarlly be constructing the first StaticClass() etc.
		// Hopefull GIsInitialLoad and the Native flag will filter most of the initially created objects and most of the native objects will be constructed before we ever hit the 'else' block
		bIsAsset = true;
	}
	else
	{
		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
		if (ThreadContext.AsyncPackageLoader || (ThreadContext.GetSerializeContext() && ThreadContext.GetSerializeContext()->GetBeginLoadCount() > 0) || !!(InObject->GetFlags() & RF_WasLoaded))
		{
			// If we're constructing this object when loading content then this object is an asset if:
			// its class or any of its outers' class is NOT marked as MigratingAsset
			// OR it's an archetype or subobject of an archetype 
			// OR it's a subobject of a UStruct (class)
			bool bIsMigratingAsset = false;
			bool bIsClassOrArchetypeSubobject = false;

			for (UObjectBase* OuterIt = InObject; OuterIt; OuterIt = FObjectHandleUtils::GetNonAccessTrackedOuterNoResolve(OuterIt))
			{
				UClass* Class = OuterIt->GetClass();
				if (!!(OuterIt->GetFlags() & RF_ArchetypeObject) || Class->IsChildOf(UStruct::StaticClass()))
				{
					bIsClassOrArchetypeSubobject = true;
					break;
				}
				if (!!(OuterIt->GetFlags() & RF_MigratingAsset))
				{
					bIsMigratingAsset = true;
				}
			}

			bIsAsset = bIsClassOrArchetypeSubobject || !bIsMigratingAsset;
		}
	}
#endif // UE_WITH_REMOTE_ASSET_ID

	FRemoteObjectId Result;

	if (!bIsAsset)
	{
		Result = FRemoteObjectId(FRemoteServerId(ERemoteServerIdConstants::Local), RemoteObjectSerialNumber.fetch_add(1));
		UE_LOG(LogRemoteObject, Verbose, TEXT("FRemoteObjectId::Generate: Type=Dynamic Object=%s RemoteId=%s"), InObject ? *InObject->GetFName().ToString() : TEXT("None"), *Result.ToString());
	}
	else
	{
		// An asset must have the same remote object id on each game server. In order to achieve this the object's full path is converted into a 53-bit hash (the lower
		// 53 bits of a 64-bit hash) and used as the remote id.

		FString ObjectOuterPath = (InObject && InObject->GetOuter()) ? InObject->GetOuter()->GetPathName() : TEXT("");
		FString ObjectName(InName);

		FStringBuilderBase ObjectPathBuilder;
		if (ObjectOuterPath.Len() > 0)
		{
			ObjectPathBuilder << ObjectOuterPath;
			ObjectPathBuilder << SUBOBJECT_DELIMITER_CHAR;
		}
		ObjectPathBuilder << ObjectName;

		const uint64 ObjectPathHash64 = CityHash64(
			reinterpret_cast<const char*>(ObjectPathBuilder.GetData()), 
			ObjectPathBuilder.Len() * sizeof(FStringBuilderBase::ElementType)
		);

		const uint64 MaskLowBits = (1ULL << REMOTE_OBJECT_SERIAL_NUMBER_BIT_SIZE) - 1;
		const uint64 ObjectPathHash53 = ObjectPathHash64 & MaskLowBits;

		Result = FRemoteObjectId(FRemoteServerId(ERemoteServerIdConstants::Asset), ObjectPathHash53);
		UE_LOG(LogRemoteObject, Verbose, TEXT("FRemoteObjectId::Generate: Type=Asset Object=%s RemoteId=%s Base64BitHash=%llu"), ObjectPathBuilder.ToString(), *Result.ToString(), ObjectPathHash64);
	}

	return Result;
}

FString FRemoteObjectId::ToString(ERemoteIdToStringVerbosity InVerbosityOverride /*= ERemoteIdToStringVerbosity::Default*/) const
{
	using namespace UE::RemoteObject::Handle;

	const int32 Verbosity = (InVerbosityOverride == ERemoteIdToStringVerbosity::Default) ? 
		FMath::Clamp(GRemoteIdToStringVerbosity, (int32)ERemoteIdToStringVerbosity::Id, (int32)ERemoteIdToStringVerbosity::Max) : (int32)InVerbosityOverride;

	if (Verbosity <= (int32)ERemoteIdToStringVerbosity::Id)
	{
		return FString::Printf(TEXT("%s-%llu"), *GetServerId().ToString(), SerialNumber);
	}
	else
	{
		FString AdditionalInfo;
		bool bStubOnly = false;

		if (UObject* ExistingObject = StaticFindObjectFastInternal(*this))
		{
			switch ((ERemoteIdToStringVerbosity)Verbosity)
			{
			case ERemoteIdToStringVerbosity::Name:
				AdditionalInfo = ExistingObject->GetName();
				break;
			case ERemoteIdToStringVerbosity::PathName:
				AdditionalInfo = ExistingObject->GetPathName();
				break;
			case ERemoteIdToStringVerbosity::FullName:
			case ERemoteIdToStringVerbosity::FullNameAttributes:
				AdditionalInfo = ExistingObject->GetFullName();
				break;
			}
		}
		else
		{
			FRemoteObjectPathName RemotePathName(*this);
			if (RemotePathName.Num())
			{
				bStubOnly = true;
				switch ((ERemoteIdToStringVerbosity)Verbosity)
				{
				case ERemoteIdToStringVerbosity::Name:
					AdditionalInfo = RemotePathName.GetObjectName().ToString();
					break;
				case ERemoteIdToStringVerbosity::PathName:
					AdditionalInfo = RemotePathName.ToString();
					break;
				case ERemoteIdToStringVerbosity::FullName:
				case ERemoteIdToStringVerbosity::FullNameAttributes:
				{
					UClass* Class = GetClass(*this, ERemoteObjectGetClassBehavior::ReturnNullIfNeverLocal);
					AdditionalInfo = FString::Printf(TEXT("%s %s"), *GetNameSafe(Class), *RemotePathName.ToString());
				}
				break;
				}
			}
			else
			{
				AdditionalInfo = TEXT("Unknown object");
			}
		}

		if (Verbosity >= (int32)ERemoteIdToStringVerbosity::FullNameAttributes)
		{
			if (IsRemote(*this))
			{
				AdditionalInfo += TEXT(" (remote)");
			}
			if (IsOwned(*this))
			{
				AdditionalInfo += TEXT(" (owned)");
			}
			if (bStubOnly)
			{
				AdditionalInfo += TEXT(" (stub)");
			}
		}

		return FString::Printf(TEXT("%s-%llu %s"), *GetServerId().ToString(), SerialNumber, *AdditionalInfo);
	}
}

FRemoteObjectId FRemoteObjectId::FromString(const FStringView& InText)
{
	int32 ServerDelimiterIndex = -1;
	if (InText.FindChar('-', ServerDelimiterIndex))
	{
		// Parse formatted id (as returned from FRemoteObjectId::ToString()): 'ServerId-SerialNumber', e.g. 'Asset-12345'
		FStringView ServerIdText(InText.GetData(), ServerDelimiterIndex);
		const int32 ObjectSerialStartIndex = ServerDelimiterIndex + 1;
		FStringView ObjectSerialText(InText.GetData() + ObjectSerialStartIndex, InText.Len() - ObjectSerialStartIndex);
		FRemoteServerId ObjectServerId = FRemoteServerId::FromString(ServerIdText);
		uint64 ObjectSerial = 0;
		LexFromString(ObjectSerial, ObjectSerialText);

		return FRemoteObjectId(ObjectServerId, ObjectSerial);
	}
	else
	{
		// Parse the string as a number as if it was the memory image of an id
		FRemoteObjectId RemoteId;
		LexFromString(RemoteId.Id, InText);
		if (RemoteId.ServerId != (uint32)ERemoteServerIdConstants::Invalid)
		{
			return RemoteId;
		}
	}		
	return FRemoteObjectId();
}

FRemoteObjectId::FRemoteObjectId(const UObjectBase* Object)
	: FRemoteObjectId(UE::CoreUObject::Private::FObjectHandleUtils::GetRemoteId(Object))
{
}

bool FRemoteObjectId::Serialize(FArchive& Ar)
{
	static_assert(sizeof(uint64) == sizeof(Id));

	uint64 SerializedID = Id;
	if (Ar.IsSaving())
	{
		FRemoteObjectId GlobalizedId = GetGlobalized();
		SerializedID = GlobalizedId.Id;
	}

	Ar << SerializedID;
	{
		FRemoteObjectId LocalizedId;
		LocalizedId.Id = SerializedID;
		checkf(LocalizedId.GetServerId().Id != (uint32)ERemoteServerIdConstants::Local, TEXT("Local server id should never be serialized"));
	}

	if (Ar.IsLoading())
	{
		FRemoteObjectId LocalizedId;
		LocalizedId.Id = SerializedID;
		checkf(LocalizedId.GetServerId().Id != (uint32)ERemoteServerIdConstants::Local, TEXT("Local server id should never be serialized"));
		*this = LocalizedId.GetLocalized();
	}

	return true;
}

bool FRemoteObjectId::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return Serialize(Ar);
}

FArchive& operator<<(FArchive& Ar, FRemoteObjectId& Id)
{
	Id.Serialize(Ar);
	return Ar;
}

FRemoteServerId FRemoteServerId::FromIdNumber(uint32 InNumber)
{
	checkf(InNumber < (uint32)ERemoteServerIdConstants::FirstReserved, TEXT("Remote server id can not be greater than %u, got: %u"), (uint32)ERemoteServerIdConstants::FirstReserved - 1, InNumber);
	FRemoteServerId Result;
	Result.Id = InNumber;
	return Result;
}

FRemoteServerId::FRemoteServerId(const FString& InText)
{
	*this = FromString(InText);
}

FRemoteServerId FRemoteServerId::FromString(const FStringView& InText)
{
	FRemoteServerId Result;

	if (InText == TEXT("Invalid"))
	{
		Result.Id = (uint32)ERemoteServerIdConstants::Invalid;
	}
	else if (InText == TEXT("Asset"))
	{
		Result.Id = (uint32)ERemoteServerIdConstants::Asset;
	}
	else if (InText == TEXT("Database"))
	{
		Result.Id = (uint32)ERemoteServerIdConstants::Database;
	}
	else if (InText == TEXT("Local"))
	{
		Result.Id = (uint32)ERemoteServerIdConstants::Local;
	}
	else
	{
		uint32 ServerIdNumber = (uint32)ERemoteServerIdConstants::Invalid;
		LexFromString(ServerIdNumber, InText);
		if (ensureMsgf(ServerIdNumber <= (uint32)ERemoteServerIdConstants::Max, TEXT("Parsed Remote Server Id value %u that is bigger than allowed max %u"), ServerIdNumber, (uint32)ERemoteServerIdConstants::Max))
		{
			Result.Id = ServerIdNumber;
		}
		else
		{
			UE_LOG(LogRemoteObject, Warning, TEXT("Clamping ServerId number %u to the maximum allowed %u"), ServerIdNumber, (uint32)ERemoteServerIdConstants::Max);
			Result.Id = (uint32)ERemoteServerIdConstants::Max;
		}		
	}

	return Result;
}

FString FRemoteServerId::ToString() const
{
	switch (Id)
	{
		case (uint32)ERemoteServerIdConstants::Asset:
			return TEXT("Asset");

		case (uint32)ERemoteServerIdConstants::Database:
			return TEXT("Database");

		case (uint32)ERemoteServerIdConstants::Local:
			return IsGlobalServerIdInitialized() ? GetGlobalized().ToString() : TEXT("Local");

		default:
			return FString::FromInt(Id);
	}
}

bool FRemoteServerId::Serialize(FArchive& Ar)
{
	static_assert(sizeof(uint32) == sizeof(Id));

	uint32 SerializedId = Id;
	if (Ar.IsSaving())
	{
		FRemoteServerId GlobalizedId = GetGlobalized();
		SerializedId = GlobalizedId.Id;
	}

	Ar << SerializedId;
	checkf(SerializedId != (uint32)ERemoteServerIdConstants::Local, TEXT("Local server id should never be serialized"));

	if (Ar.IsLoading())
	{
		FRemoteServerId LocalizedId;
		LocalizedId.Id = SerializedId;
		*this = LocalizedId.GetLocalized();
	}	

	return true;
}

bool FRemoteServerId::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return Serialize(Ar);
}

FArchive& operator<<(FArchive& Ar, FRemoteServerId& Id)
{
	Id.Serialize(Ar);
	return Ar;
}

void UE::RemoteObject::Handle::FPhysicsIslandId::Reset()
{
	PhysicsServerId = FRemoteServerId();
	PhysicsLocalIslandId = 0;
}


// Debugging functionality to help us find these objects in debug builds
#if !UE_BUILD_SHIPPING && UE_WITH_REMOTE_OBJECT_HANDLE
/**
 * Put this in a Debug Watch Window on a specific UObject.  You may have to forcibly cast the UObject to UObjectBase*
 * e.g. DebugFindRemoteObjectStub((UObjectBase*)Header.Class.DebugPtr)
 */
COREUOBJECT_API UE::RemoteObject::Handle::FRemoteObjectStub* DebugFindRemoteObjectStub(const UObjectBase* Object)
{
	if (!Object)
	{
		return nullptr;
	}

	uintptr_t Pointer = reinterpret_cast<uintptr_t>(Object);
	if (Pointer & 0x1)
	{
		return reinterpret_cast<UE::RemoteObject::Handle::FRemoteObjectStub*>(Pointer & ~UPTRINT(1));
	}

	FRemoteObjectId ObjId { Object };
	return UE::RemoteObject::Private::ObjectMaps->FindRemoteObjectStub(ObjId);
}

/**
 * Attempt to find a UObject in the currently debugged process by its FRemoteObjectId constituents.
 * Once you know a FRemoteObjectId, take its ServerId and SerialNumber and pass them into Debug Watch Window as arguments (in that order)
 * e.g. DebugFindObjectLocallyFromRemoteId( 2, 1234 )
 */
COREUOBJECT_API UObject* DebugFindObjectLocallyFromRemoteId(uint16 ServerId, uint64 SerialNumber)
{
	return StaticFindObjectFastInternal(FRemoteObjectId(FRemoteServerId::FromIdNumber(static_cast<uint32>(ServerId)), SerialNumber));
}

/**
 * Attempt to find a UObject in the currently debugged process by its FRemoteObjectId's Full uint64 Id
 * Once you find a FRemoteObjectId, copy its Id and pass them into Debug Watch Window as the argument
 * e.g. DebugFindObjectLocallyFromRemoteId( 1234567890 )
 */
COREUOBJECT_API UObject* DebugFindObjectLocallyFromRemoteId(uint64 FullId)
{
	constexpr uint64 SerialBitMask = (1ull << 54ull) - 1ull;
	FRemoteObjectId RemoteId(FRemoteServerId::FromIdNumber(static_cast<uint32>((FullId >> 54ull) & 1023ull)), FullId & SerialBitMask);
	ensure(RemoteId.GetIdNumber() == FullId);

	return StaticFindObjectFastInternal(RemoteId);
}
#endif // !UE_BUILD_SHIPPING && UE_WITH_REMOTE_OBJECT_HANDLE