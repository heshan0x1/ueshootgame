// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionStreamingGenerationContext.h"
#include "WorldPartition/DataLayer/DataLayersID.h"

#if WITH_EDITOR

#include "WorldPartition/DataLayer/ExternalDataLayerAsset.h"
#include "WorldPartition/DataLayer/ExternalDataLayerInstance.h"

class AActor;
class UWorldPartition;
class UHLODLayer;
class UHLODBuilder;
class UHLODBuilderSettings;
class AWorldPartitionHLOD;

struct FHLODCreationContext
{
	TMap<FName, FWorldPartitionHandle> HLODActorDescs;
	TArray<FWorldPartitionReference> ActorReferences;
};

struct FHLODCreationParams
{
	UWorldPartition* WorldPartition;
	UWorld* TargetWorld;

	FGuid CellGuid;
	FString CellName;
	TUniqueFunction<FName(const UHLODLayer*)> GetRuntimeGrid;
	uint32 HLODLevel;
	FGuid ContentBundleGuid;
	TArray<const UDataLayerInstance*> DataLayerInstances;
	bool bIsStandalone;

	const UExternalDataLayerAsset* GetExternalDataLayerAsset() const
	{
		auto IsAnExternalDataLayerPred = [](const UDataLayerInstance* DataLayerInstance) { return DataLayerInstance->IsA<UExternalDataLayerInstance>(); };
		if (const UDataLayerInstance* const* ExternalDataLayerInstance = DataLayerInstances.FindByPredicate(IsAnExternalDataLayerPred))
		{
			return CastChecked<UExternalDataLayerInstance>(*ExternalDataLayerInstance)->GetExternalDataLayerAsset();
		}
		return nullptr;
	}

	double MinVisibleDistance;
	
	UE_DEPRECATED(5.7, "CellBounds member is not used anymore.")
	FBox CellBounds;
};

/**
 * Tools for building HLODs in WorldPartition
 */
class IWorldPartitionHLODUtilities
{
public:
	virtual ~IWorldPartitionHLODUtilities() {}

	/**
	 * Create HLOD actors for a given cell
	 *
	 * @param	InCreationContext	HLOD creation context object
	 * @param	InCreationParams	HLOD creation parameters object
	 * @param	InActors			The actors for which we'll build an HLOD representation
	 * @param	InDataLayers		The data layers to assign to the newly created HLOD actors
	 */
	virtual TArray<AWorldPartitionHLOD*> CreateHLODActors(FHLODCreationContext& InCreationContext, const FHLODCreationParams& InCreationParams, const TArray<IStreamingGenerationContext::FActorInstance>& InActors) = 0;

	/**
	 * Build HLOD for the specified AWorldPartitionHLOD actor.
	 *
	 * @param 	InHLODActor		The HLOD actor for which we'll build the HLOD
	 * @return An hash that represent the content used to build this HLOD.
	 */
	virtual uint32 BuildHLOD(AWorldPartitionHLOD* InHLODActor) = 0;

	/**
	 * Compute the HLOD hash for the specified AWorldPartitionHLOD actor.
	 *
	 * @param 	InHLODActor		The HLOD actor for which we'll compute the hash
	 * @return An hash that represent the content used to build this HLOD.
	 */
	virtual uint32 ComputeHLODHash(const AWorldPartitionHLOD* InHLODActor) = 0;

	/**
	 * Retrieve the HLOD Builder class to use for the given HLODLayer.
	 * 
	 * @param	InHLODLayer		HLODLayer
	 * @return The HLOD builder subclass to use for building HLODs for the provided HLOD layer.
	 */
	virtual TSubclassOf<UHLODBuilder> GetHLODBuilderClass(const UHLODLayer* InHLODLayer) = 0;

	/**
	 * Create the HLOD builder settings for the provided HLOD layer object. The type of settings created will depend on the HLOD layer type.
	 *
	 * @param 	InHLODLayer		The HLOD layer for which we'll create a setting object
	 * @return A newly created UHLODBuilderSettings object, outered to the provided HLOD layer.
	 */
	virtual UHLODBuilderSettings* CreateHLODBuilderSettings(UHLODLayer* InHLODLayer) = 0;

	/**
	 * HLOD build evaluator delegate
	 * @param HLODActor			The HLOD actor to be rebuilt.
	 * @param OldHash			The previously stored hash of the inputs to the HLOD build.
	 * @param NewHash			The newly computed hash of the inputs to the HLOD build.
	 * @return true if the HLOD build should be performed, false otherwise.
	 */
	UE_EXPERIMENTAL(5.7, "Experimental API, HLOD build evaluator logic may be moved in a future release.")
	DECLARE_DELEGATE_RetVal_ThreeParams(bool, FHLODBuildEvaluator, AWorldPartitionHLOD*, uint32, uint32);

	/**
	 * Provide a delegate that will be used to evaluate whether an HLOD build should be performed.
	 * @param The evaluator delegate.
	 */
	UE_EXPERIMENTAL(5.7, "Experimental API, HLOD build evaluator logic may be moved in a future release.")
	virtual void SetHLODBuildEvaluator(FHLODBuildEvaluator BuildEvaluatorDelegate) = 0;


	UE_DEPRECATED(5.2, "Use the overload that passes the DataLayersInstances via InCreationParams")
	virtual TArray<AWorldPartitionHLOD*> CreateHLODActors(FHLODCreationContext& InCreationContext, const FHLODCreationParams& InCreationParams, const TArray<IStreamingGenerationContext::FActorInstance>& InActors, const TArray<const UDataLayerInstance*>& InDataLayerInstances)
	{
		const_cast<FHLODCreationParams&>(InCreationParams).DataLayerInstances = InDataLayerInstances;
		return CreateHLODActors(InCreationContext, InCreationParams, InActors);
	}
};
#endif
