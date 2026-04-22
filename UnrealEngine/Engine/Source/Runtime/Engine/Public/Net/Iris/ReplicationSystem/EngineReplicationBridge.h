// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Engine/EngineTypes.h"
#include "UObject/ObjectPtr.h"
#include "EngineReplicationBridge.generated.h"

class UNetDriver;
class UIrisObjectReferencePackageMap;
struct FAnalyticsEventAttribute;

class UActorComponent;
class UWorld;

namespace UE::Net
{
	enum class ENetRefHandleError : uint32;
}

namespace UE::Net
{

// If actor should be replicated using IRIS or old replication system
ENGINE_API bool ShouldUseIrisReplication(const UObject* Actor);

// Currently just a direct mapping of EEEndPlayReason::Type but we might want to add more specific reasons later on
enum class EStopReplicatingReason : uint32
{
	/** When the Actor or Component is explicitly destroyed. */
	Destroyed = 0,
	/** When the world is being unloaded for a level transition. */
	LevelTransition,
	/** When the world is being unloaded because PIE is ending. */
	EndPlayInEditor,
	/** When the level it is a member of is streamed out. */
	RemovedFromWorld,
	/** When the application is being exited. */
	Quit,
};

ENGINE_API const TCHAR* LexToString(EStopReplicatingReason Reason);

}

struct FActorReplicationParams
{
	enum EFilterType
	{
		/** Let the config filter configs assign a filter based on the class type. */
		ConfigFilter = 0,
		/** When set don't assign any dynamic filter and default to being always relevant. */
		AlwaysRelevant,
		/** When set use the default spatial filter of the bridge. Generally that is the NetObjectGridFilter. */
		DefaultSpatial,
		/** When set use filter defined by ExplicitDynamicFilterName. */
		ExplicitFilter,
	};

	/**
	 * The default behavior for actors (e.g. ConfigFilter) is that they are automatically assigned a filter based on the class type via the engine config and UObjectReplicationBridgeConfig::FilterConfigs.
	 * Choosing a different option allows you to ignore the automatic assignment and select a specific filter for the replicated actor.
	 * @see FObjectReplicationBridgeFilterConfig
	 */
	EFilterType FilterType = ConfigFilter;

	/** Only used when ExplicitFilter is the type used. The dynamic filter to assign to this actor. */
	FName ExplicitDynamicFilterName;

	/** Optional  factory name if the actor is not using the default NetActorFactory */
	FName NetFactoryName;
};

struct FStopReplicatingActorParams
{
	explicit ENGINE_API FStopReplicatingActorParams(EEndPlayReason::Type);

	UE::Net::EStopReplicatingReason StopReplicatingReason = UE::Net::EStopReplicatingReason::Destroyed;

	//$TODO: Remove when we can remove EndReplication
	EEndPlayReason::Type EndPlayReason = EEndPlayReason::Type::Destroyed;
};

UCLASS(Transient, MinimalAPI, config=Engine)
class UEngineReplicationBridge final : public UObjectReplicationBridge
{
	GENERATED_BODY()

public:
	ENGINE_API UEngineReplicationBridge();
	virtual ENGINE_API ~UEngineReplicationBridge() override;

	ENGINE_API static UEngineReplicationBridge* Create(UNetDriver* NetDriver);

	/** Sets the net driver for the bridge. */
	ENGINE_API void SetNetDriver(UNetDriver* const InNetDriver);
	
	/** Get net driver used by the bridge .*/
	inline UNetDriver* GetNetDriver() const { return NetDriver; }

	/** Begin replication of an actor and its registered ActorComponents and SubObjects. */
	ENGINE_API FNetRefHandle StartReplicatingActor(AActor* Instance);

	/** Stop replicating an actor. This will destroy the handle of the actor and those of it's compponents and subobjects. */
	ENGINE_API void StopReplicatingActor(AActor* Actor, const FStopReplicatingActorParams& Params);

	/** Convert StopReplication reason to EndReplicationFlags */
	ENGINE_API EEndReplicationFlags ConvertToEndReplicationFlags(UE::Net::EStopReplicatingReason StopReplicatingReason) const;
		
	/**
	 * Begin replication of an ActorComponent and its registered SubObjects, 
	 * if the ActorComponent already is replicated any set NetObjectConditions will be updated.
	*/
	ENGINE_API FNetRefHandle StartReplicatingComponent(FNetRefHandle RootObjectHandle, UActorComponent* ActorComponent);

	/** Begin replication of a subobject. */
	ENGINE_API FNetRefHandle StartReplicatingSubObject(UObject* SubObject, const FSubObjectReplicationParams& Params);

	/** Stop replicating an ActorComponent and its associated SubObjects. */
	ENGINE_API void StopReplicatingComponent(UActorComponent* ActorComponent, EEndReplicationFlags EndReplicationFlags = EEndReplicationFlags::None);

	/** Get object reference packagemap. Used in special cases where serialization hasn't been converted to use NetSerializers.  */
	UIrisObjectReferencePackageMap* GetObjectReferencePackageMap() const { return ObjectReferencePackageMap; }

	/** Tell the remote connection that we detected a reading error with a specific replicated object */
	ENGINE_API virtual void SendErrorWithNetRefHandle(UE::Net::ENetRefHandleError ErrorType, FNetRefHandle RefHandle, uint32 ConnectionId, TConstArrayView<const FNetRefHandle> ExtraNetRefHandle = {}) override;

	/** Add the rootobject to the level's filter group so it will only be relevant if the connection has that level streamed in. */
	ENGINE_API void AddRootObjectToLevelGroup(const UObject* RootObject, const ULevel* Level);
	
	/** Updates the level group for an actor that changed levels */
	void ActorChangedLevel(const AActor* Actor, const ULevel* PreviousLevel);

	/** Called when NetUpdateFrequency has changed on the Actor. */
	void OnNetUpdateFrequencyChanged(const AActor* Actor);

	void WakeUpObjectInstantiatedFromRemote(AActor* Actor) const;

	/**
	 * Add relevant network metrics gathered since the last call to ConsumeNetMetrics.
	 * Any periodic stat will be reset here too.
	 * @param OutAttrs A list of Name/Value pairings that will be sent to an AnalyticsProvider
	 */
	ENGINE_API void ConsumeNetMetrics(TArray<FAnalyticsEventAttribute>& OutAttrs);

	/** Access to the factory id that handles actors */
	UE::Net::FNetObjectFactoryId GetActorFactoryId() const { return ActorFactoryId; }

	/** Access to the factory id that handles subobjects */
	UE::Net::FNetObjectFactoryId GetSubObjectFactoryId() const { return SubObjectFactoryId; }

protected:
	// UReplicationBridge
	virtual void OnPreSeamlessTravelGarbageCollect() override;

	// UObjectReplicationBridge
	virtual void Initialize(UReplicationSystem* ReplicationSystem) override;
	virtual void Deinitialize() override;
	virtual void GetInitialDependencies(FNetRefHandle Handle, FNetDependencyInfoArray& OutDependencies) const override;
	virtual bool RemapPathForPIE(uint32 ConnectionId, FString& Path, bool bReading) const override;
	virtual bool ObjectLevelHasFinishedLoading(UObject* Object) const override;
	virtual bool IsAllowedToDestroyInstance(const UObject* Instance) const override;
	virtual void OnProtocolMismatchDetected(FNetRefHandle ObjectHandle) override;
	virtual void OnProtocolMismatchReported(FNetRefHandle RefHandle, uint32 ConnectionId) override;
	virtual bool CanCreateDestructionInfo() const override;

	/** Returns true if the class is derived from Actor and its CDO has set bReplicates. */
	virtual bool IsClassReplicatedByDefault(const UClass* Class) const;

	[[nodiscard]] virtual FString PrintConnectionInfo(uint32 ConnectionId) const override;

private:
	
	void OnMaxTickRateChanged(UNetDriver* InNetDriver, int32 NewMaxTickRate, int32 OldMaxTickRate);

private:

	UE::Net::FNetObjectFactoryId ActorFactoryId;
	UE::Net::FNetObjectFactoryId SubObjectFactoryId;

	UNetDriver* NetDriver = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UIrisObjectReferencePackageMap> ObjectReferencePackageMap = nullptr;

	UPROPERTY(Config)
	/** Name of the NetObjectFactory to use for default replicated actors */
	FName ActorFactoryName;

	UPROPERTY(Config)
	/** Name of the NetObjectFactory to use for default replicated subobjects */
	FName SubObjectFactoryName;
};
