// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeGPU.h"

#include "MetaHumanFaceContourTrackerAsset.generated.h"

#define UE_API METAHUMANFACECONTOURTRACKER_API

/** Face Contour Tracker Asset
* 
*   Contains trackers for different facial features
*   Used in MetaHuman Identity and Performance assets
* 
**/
UCLASS(MinimalAPI, BlueprintType)
class UMetaHumanFaceContourTrackerAsset : public UObject
{
	GENERATED_BODY()

public:

	//~Begin UObject interface
#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
	UE_API virtual void PostTransacted(const FTransactionObjectEvent& InTransactionEvent) override;
#endif
	UE_API virtual void PostLoad() override;
	//~End UObject interface

	TSharedPtr<UE::NNE::IModelInstanceGPU> FaceDetector;
	TSharedPtr<UE::NNE::IModelInstanceGPU> FullFaceTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> BrowsDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> EyesDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> NasioLabialsDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> MouthDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> LipzipDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> ChinDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> TeethDenseTracker;
	TSharedPtr<UE::NNE::IModelInstanceGPU> TeethConfidenceTracker;

public:

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> FaceDetectorModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> FullFaceTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> BrowsDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> EyesDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> NasioLabialsDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> MouthDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> LipzipDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> ChinDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> TeethDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> TeethConfidenceTrackerModelData;

public:

	UE_API bool CanProcess() const;

	UE_API void LoadTrackers(bool bInShowProgressNotification, TFunction<void(bool)>&& Callback);

	UE_API void CancelLoadTrackers();

	UE_API bool LoadTrackersSynchronous();

	UE_API bool IsLoadingTrackers() const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNNEModelData>> LoadedTrackerModelData;

	TArray<TSharedPtr<UE::NNE::IModelInstanceGPU>> LoadedTrackerModels;

	TWeakPtr<class SNotificationItem> LoadNotification;
	TSharedPtr<struct FStreamableHandle> TrackersLoadHandle;

	UE_API TArray<TSoftObjectPtr<UNNEModelData>> GetTrackerModelData() const;
	UE_API TArray<TSharedPtr<UE::NNE::IModelInstanceGPU>> GetTrackerModels() const;
	UE_API bool SetTrackerModels();

	UE_API TArray<FSoftObjectPath> GetTrackerModelDataAsSoftObjectPaths() const;
	UE_API bool AreTrackerModelsLoaded() const;

	UE_API bool CreateTrackerModels();
};

#undef UE_API
