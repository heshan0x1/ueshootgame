// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SubclassOf.h"
#include "EditorFramework/ThumbnailInfo.h"
#include "Templates/SharedPointer.h"
#include "Misc/FrameTime.h"
#include "MetaHumanIdentityErrorCode.h"
#include "MetaHumanIdentityParts.h"
#include "CameraCalibration.h"
#include "Pipeline/Pipeline.h"
#include "DNACommon.h"

#if WITH_EDITOR
#include "Cloud/MetaHumanServiceRequest.h"
#endif // WITH_EDITOR

#include "MetaHumanIdentity.generated.h"


enum class ESolveRequestResult;
enum class EIdentityPoseType : uint8;

UENUM()
enum class EIdentityInvalidationState : uint8
{
	Solve, AR, FitTeeth, PrepareForPerformance, Valid, None
};

/////////////////////////////////////////////////////
// UMetaHumanIdentityThumbnailInfo
UCLASS(MinimalAPI)
class UMetaHumanIdentityThumbnailInfo
	: public UThumbnailInfo
{
	GENERATED_BODY()

public:
	UMetaHumanIdentityThumbnailInfo();

	/** Override the Promoted Frame index used to generate the MetaHuman Identity thumbnail */
	UPROPERTY(EditAnywhere, Category = "Thumbnail")
	int32 OverridePromotedFrame;
};

/////////////////////////////////////////////////////
// UMetaHumanIdentity

/** MetaHuman Identity Asset
*
*   Provides the tools to auto-generate a fully rigged Skeletal Mesh
*   of a human face from Capture Data (Mesh or Footage) by tracking
*   the facial features, fitting a Template Mesh having MetaHuman
*   topology to the tracked curves, and sending the resulting mesh
*   to MetaHuman Service, which returns an auto-rigged SkeletalMesh
*   resembling the person from the Capture Data.
*
*   The obtained Skeletal Mesh can be used by MetaHuman Performance
*   asset to generate an Animation Sequence from video footage.
*
*   MetaHuman Identity Asset Toolkit can also create a full MetaHuman in MetaHuman
*   Creator, downloadable through Quixel Bridge.
*/
UCLASS(MinimalAPI, BlueprintType)
class UMetaHumanIdentity
	: public UObject
{
	GENERATED_BODY()

public:

	METAHUMANIDENTITY_API UMetaHumanIdentity();

	//~Begin UObject interface
	METAHUMANIDENTITY_API virtual void PostLoad() override;
	METAHUMANIDENTITY_API virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~End UObject interface

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutoRigServiceFinishedDelegate, bool);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAutoRigServiceFinishedDynamicDelegate, bool, bInSuccess);


	// Dynamic delegate called when the pipeline finishes running
	UPROPERTY(BlueprintAssignable, Category = "Processing")
	FOnAutoRigServiceFinishedDynamicDelegate OnAutoRigServiceFinishedDynamicDelegate;

	// Delegate called when the pipeline finishes running (used by toolkit)
	FOnAutoRigServiceFinishedDelegate OnAutoRigServiceFinishedDelegate;

	/** Looks for a Part of the given class in the array of parts. Returns nullptr if no Part was found */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parts")
	METAHUMANIDENTITY_API class UMetaHumanIdentityPart* FindPartOfClass(TSubclassOf<class UMetaHumanIdentityPart> InPartClass) const;

	/** Looks for a Part of the given class in the array of parts. Creates and return a new one if not found */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parts")
	METAHUMANIDENTITY_API class UMetaHumanIdentityPart* GetOrCreatePartOfClass(TSubclassOf<class UMetaHumanIdentityPart> InPartClass);

	/**
	 * Searches for a Part of the given class in the array of parts.
	 * The class being searched must be a child of UMetaHumanIdentityPart.
	 */
	template<typename SearchType>
	SearchType* FindPartOfClass() const
	{
		ensure(SearchType::StaticClass()->IsChildOf(UMetaHumanIdentityPart::StaticClass()));
		return Cast<SearchType>(FindPartOfClass(SearchType::StaticClass()));
	}

	/** Returns true if the given Part class can be added to the MetaHuman Identity being edited */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parts")
	METAHUMANIDENTITY_API bool CanAddPartOfClass(TSubclassOf<class UMetaHumanIdentityPart> InPartClass) const;

	/** Returns true if the given Pose class can be added to the MetaHuman Identity being edited */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parts")
	METAHUMANIDENTITY_API bool CanAddPoseOfClass(TSubclassOf<class UMetaHumanIdentityPose> InPoseClass, EIdentityPoseType InPoseType) const;

#if WITH_EDITOR

	/** Initialize the MetaHuman Identity from a DNA file. The MetaHuman Identity must already have a face for this to succeeded */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|MetaHuman Identity Creation")
	METAHUMANIDENTITY_API EIdentityErrorCode ImportDNAFile(const FString& InDNAFilePath, EDNADataLayer InDnaDataLayer, const FString& InBrowsFilePath);

	/** Export DNA and brows data to files at selected location */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|MetaHuman Identity Creation")
	METAHUMANIDENTITY_API bool ExportDNADataToFiles(const FString& InDnaPathWithName, const FString& InBrowsPathWithName);

	/** Initialize the MetaHuman Identity from a DNA. The MetaHuman Identity must already have a face for this to succeeded */
	METAHUMANIDENTITY_API EIdentityErrorCode ImportDNA(TSharedPtr<class IDNAReader> InDNAReader, const TArray<uint8>& InBrowsBuffer);

#endif // WITH_EDITOR

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANIDENTITY_API void StartFrameTrackingPipeline(const TArray<FColor>& InImageData, int32 InWidth, int32 InHeight, const FString& InDepthFramePath,
		UMetaHumanIdentityPose* InPose, UMetaHumanIdentityPromotedFrame* InPromotedFrame, bool bInShowProgress);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANIDENTITY_API void SetBlockingProcessing(bool bInBlockingProcessing);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANIDENTITY_API bool IsFrameTrackingPipelineProcessing() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AutoRigging")
	METAHUMANIDENTITY_API void LogInToAutoRigService();

	/** This function checks if there's a session stored. There is NO request sent to check if the token is actually valid */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AutoRigging")
	METAHUMANIDENTITY_API bool IsLoggedInToService();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AutoRigging")
	METAHUMANIDENTITY_API bool IsAutoRiggingInProgress();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AutoRigging")
	METAHUMANIDENTITY_API void CreateDNAForIdentity(bool bInLogOnly);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Diagnostics")
	METAHUMANIDENTITY_API bool DiagnosticsIndicatesProcessingIssue(FText& OutDiagnosticsWarningMessage) const;

public:

	/** The list of Parts the make this Identity. See UMetaHumanIdentityPart */
	UPROPERTY(BlueprintReadOnly, Category = "Parts")
	TArray<TObjectPtr<class UMetaHumanIdentityPart>> Parts;

	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, AdvancedDisplay, Category = "Thumbnail")
	TObjectPtr<class UThumbnailInfo> ThumbnailInfo;

	/** Stores the viewport settings for this MetaHuman Identity */
	UPROPERTY(BlueprintReadWrite, Instanced, Category = "Viewport Settings")
	TObjectPtr<class UMetaHumanIdentityViewportSettings> ViewportSettings;

	UPROPERTY()
	EIdentityInvalidationState InvalidationState = EIdentityInvalidationState::None;

public:

	/** The transaction context identifier for transactions done in the MetaHuman Identity being edited */
	static METAHUMANIDENTITY_API const TCHAR* IdentityTransactionContext;

	/** Deals with error produced by the MetaHuman Identity process - logs message and optionally show user dialog */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	static METAHUMANIDENTITY_API bool HandleError(EIdentityErrorCode InErrorCode, bool bInLogOnly = false);

	static METAHUMANIDENTITY_API const FText AutoRigServiceTitleError;
	static METAHUMANIDENTITY_API const FText AutoRigServiceTitleSuccess;

	METAHUMANIDENTITY_API UCaptureData* GetPoseCaptureData(EIdentityPoseType InPoseType) const;

	/** Returns a hashed PrimaryAssetType/PrimaryAssetName identifier. Used for telemetry */
	METAHUMANIDENTITY_API FString GetHashedIdentityAssetID();

	METAHUMANIDENTITY_API bool GetMetaHumanAuthoringObjectsPresent() const;

private:

	METAHUMANIDENTITY_API void StartPipeline(const TArray<FColor>& InImageData, int32 InWidth, int32 InHeight, const FString& InDepthFramePath,
		const TArray<FCameraCalibration>& InCalibrations, const FString& InCamera,
		UMetaHumanIdentityPromotedFrame* InPromotedFrame, bool bInShowProgress, bool bInSkipDiagnostics);

	METAHUMANIDENTITY_API void AutoRigProgressEnd(bool bSuccess) const;

	METAHUMANIDENTITY_API void AutoRigSolveFinished(bool bSuccess, bool bInLogOnly);

	METAHUMANIDENTITY_API void HandleIdentityForAutoRigValidation(EAutoRigIdentityValidationError InErrorCode, bool bInLogOnly = false);

#if WITH_EDITOR
	METAHUMANIDENTITY_API void HandleAutoRigServiceError(EMetaHumanServiceRequestResult InServiceError, bool bInLogOnly);
#endif // WITH_EDITOR

	METAHUMANIDENTITY_API bool IdentityIsReadyForAutoRig(TArray<FVector>& OutConformedFaceVertices, 
								   TArray<FVector>& OutConformedLeftEyeVertices,
								   TArray<FVector>& OutConformedRightEyeVertices, 
								   bool bInLogOnly);
	
	/** Pipeline for tracking Promoted Frames */
	UE::MetaHuman::Pipeline::FPipeline TrackPipeline;

	bool bBlockingProcessing = false;

	/** True if the auto rigging service has been called */
	bool bIsAutorigging = false;

#if WITH_EDITOR
	/** Sends a telemetry event when the user invokes MeshToMetaHuman command */
	METAHUMANIDENTITY_API void SendTelemetryForIdentityAutorigRequest(bool bIsFootageData);
	
	/** A reference to the notification dialog that shows the autorigging progress */
	TWeakPtr<class SNotificationItem> AutoRigProgressNotification;
#endif // WITH_EDITOR

	bool bMetaHumanAuthoringObjectsPresent = false;
};
