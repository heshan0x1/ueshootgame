// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "UObject/ObjectKey.h"


#include "GeneratedNaniteDisplacedMeshEditorSubsystem.generated.h"

#define UE_API NANITEDISPLACEDMESHEDITOR_API

class UNaniteDisplacedMesh;
class UFactory;

struct FNaniteDisplacedMeshParams;
struct FPropertyChangedEvent;

template<typename Type>
class TSubclassOf;

UCLASS(MinimalAPI)
class UGeneratedNaniteDisplacedMeshEditorSubsystem : public UEditorSubsystem, public FUObjectArray::FUObjectDeleteListener
{
	GENERATED_BODY()

	/**
	 * Utility functions to setup an automatic update of the level actors that have a generated NaniteDisplacedMesh constructed some assets data.
	 */
public:
	using FOnActorDependencyChanged = TUniqueFunction<void (AActor* /*ActorToUpdate*/, UObject* /*AssetChanged*/, FPropertyChangedEvent& /*PropertyChangedEvent*/)>;
	
	struct FActorClassHandler
	{
		FOnActorDependencyChanged Callback;

		// If empty it will accept any change
		TMap<UClass*, TSet<FProperty*>> PropertiesToWatchPerAssetType;
	};

	/**
	 * Tell the system what to callback when a dependency was changed for an actor the specified type.
	 */
	UE_API void RegisterClassHandler(const TSubclassOf<AActor>& ActorClass, FActorClassHandler&& ActorClassHandler);
	UE_API void UnregisterClassHandler(const TSubclassOf<AActor>& ActorClass);

	/**
	 * Tell the system to track for change to the dependencies of the actor.
	 * The system will invoke a callback after a change to any asset that this actor has a dependency.
	 */
	UE_API void UpdateActorDependencies(AActor* Actor, TArray<TObjectKey<UObject>>&& Dependencies);

	/**
	 * Tell the system to stop tracking stuff for this actor.
	 */
	UE_API void RemoveActor(const AActor* ActorToRemove);

public:
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UE_API virtual void Deinitialize() override;

private:
	UE_API void OnObjectPreEditChange(UObject* Object, const class FEditPropertyChain& EditPropertyChain);
	UE_API void OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	UE_API void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
	UE_API void OnLevelActorDeleted(AActor* Actor);

	UE_API void OnAssetPreImport(UFactory* InFactory, UClass* InClass, UObject* InParent, const FName& Name, const TCHAR* Type);
	UE_API void OnAssetPostImport(UFactory* InFactory, UObject* InCreatedObject);

	UE_API bool CanObjectBeTracked(UObject* InObject);
	UE_API bool RemoveActor(TObjectKey<AActor> InActorToRemove, uint32 InWeakActorHash);

	UE_API FActorClassHandler* FindClassHandler(UClass* Class);
	UE_API bool ShouldCallback(UClass* AssetClass, const FActorClassHandler& ClassHandler, const FPropertyChangedEvent& PropertyChangedEvent);

	UE_API void UpdateDisplacedMeshesDueToAssetChanges(UObject* Asset);
	UE_API void UpdateDisplacementMeshToAssets(UNaniteDisplacedMesh* DisplacementMesh);
	UE_API void WaitForDependentDisplacedMeshesToFinishTheirCompilation(UObject* AssetAboutToChange);

private:
	// Track the change to asset for actors
	TMap<UClass*, FActorClassHandler> ActorClassHandlers;
	TMap<TObjectKey<AActor>, TArray<TObjectKey<UObject>>> ActorsToDependencies;
	TMap<TObjectKey<UObject>, TSet<TObjectKey<AActor>>> DependenciesToActors;

	// Track re import of asset for the displaced meshes
	struct FBidirectionalAssetsAndDisplacementMeshMap
	{
		void RemoveDisplacedMesh(UNaniteDisplacedMesh* DisplacedMesh);
		void RemoveAssetForReimportTracking(UObject* Object);

		void AddDisplacedMesh(UNaniteDisplacedMesh* Mesh, TSet<UObject*>&& AssetsToTrack);

		const TArray<TObjectKey<UNaniteDisplacedMesh>> GetMeshesThatUseAsset(UObject* Object);
		const TArray<TObjectKey<UNaniteDisplacedMesh>> GetMeshesThatUseAsset(UObject* Object, uint32 Hash);

		void ReplaceObject(UObject* OldObject, UObject* NewObject);

		SIZE_T GetAllocatedSize() const;

	private:
		TMap<UNaniteDisplacedMesh*, TSet<UObject*>> MeshToAssets;
		TMap<UObject*, TSet<UNaniteDisplacedMesh*>> AssetToMeshes;
	};

	FBidirectionalAssetsAndDisplacementMeshMap MeshesAndAssetsReimportTracking;

	bool bIsEngineCollectingGarbage = false;

	FDelegateHandle OnPreEditChangeHandle;
	FDelegateHandle OnPostEditChangeHandle;
	FDelegateHandle OnObjectsReplacedHandle;
	FDelegateHandle OnLevelActorDeletedHandle;
	FDelegateHandle OnAssetReimportHandle;
	FDelegateHandle OnAssetPostImportHandle;
	FDelegateHandle OnAssetPreImportHandle;
	FDelegateHandle OnInMemoryAssetDeletedHandle;
	FDelegateHandle OnPreGarbageCollectHandle;
	FDelegateHandle OnPostGarbageCollectHandle;

private:
	// Begin FUObjectArray::FUObjectDeleteListener Api
	UE_API virtual void NotifyUObjectDeleted(const UObjectBase *Object, int32 Index) override;
	UE_API virtual void OnUObjectArrayShutdown() override;
	UE_API virtual SIZE_T GetAllocatedSize() const override;
	// End FUObjectArray::FUObjectDeleteListener Api

	UE_API void UpdateIsEngineCollectingGarbage(bool bIsCollectingGarbage);
};

#undef UE_API
