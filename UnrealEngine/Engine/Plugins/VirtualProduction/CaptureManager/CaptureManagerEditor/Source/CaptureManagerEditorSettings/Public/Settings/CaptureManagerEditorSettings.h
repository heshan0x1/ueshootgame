// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "Engine/DeveloperSettings.h"
#include "Engine/TimerHandle.h"

#include "CaptureManagerEditorSettings.generated.h"

#define UE_API CAPTUREMANAGEREDITORSETTINGS_API

class UCaptureManagerIngestNamingTokens;
class UCaptureManagerVideoNamingTokens;
class UCaptureManagerAudioNamingTokens;
class UCaptureManagerCalibrationNamingTokens;
class UCaptureManagerLensFileNamingTokens;

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings, meta = (DisplayName = "Capture Manager"), defaultconfig)
class UCaptureManagerEditorSettings : public UDeveloperSettings
{
public:
	GENERATED_BODY()

	UE_API void Initialize();

	/** Get the import naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerIngestNamingTokens> GetGeneralNamingTokens() const;

	/** Get the video naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerVideoNamingTokens> GetVideoNamingTokens() const;

	/** Get the audio naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerAudioNamingTokens> GetAudioNamingTokens() const;

	/** Get the calibration naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerCalibrationNamingTokens> GetCalibrationNamingTokens() const;

	/** Get the calibration naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerLensFileNamingTokens> GetLensFileNamingTokens() const;

	/** Location to store ingested media data. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	FDirectoryPath MediaDirectory;

	/** Content Browser location where assets will be created. */
	UPROPERTY(config, EditAnywhere, Category = "Import", meta = (ContentDir, RelativeToGameContentDir))
	FDirectoryPath ImportDirectory;

	/** Option to automatically save the assets after the ingest process. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	bool bAutoSaveAssets = true;

	/** Name for created Capture Data assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	FString CaptureDataAssetName;

	/** Name for created Image Media Source video assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Video")
	FString ImageSequenceAssetName;

	/** Name for created Image Media Source depth assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Video")
	FString DepthSequenceAssetName;

	/** Tokens compatible with video properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Video")
	FText VideoTokens;

	/** Name for created Soundwave assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Audio")
	FString SoundwaveAssetName;

	/** Tokens compatible with audio properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Audio")
	FText AudioTokens;

	/** Name for created Camera Calibration assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FString CalibrationAssetName;

	/** Tokens compatible with calibration properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FText CalibrationTokens;

	/** Name for created Lens File assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FString LensFileAssetName;

	/** Tokens compatible with calibration properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FText LensFileTokens;

	/** Option to launch the Ingest Server when a Live Link Hub connection is made. */
	UPROPERTY(config, EditAnywhere, Category = "Ingest Server")
	bool bLaunchIngestServerOnLiveLinkHubConnection = true;

	/** Option to choose a listening port for the Ingest Server. Leave 0 for automatic selection of the port. */
	UPROPERTY(config, EditAnywhere, Category = "Ingest Server")
	uint16 IngestServerPort = 0;

	/** Tokens compatible with import properties */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	FText ImportTokens;

	/** Global tokens. */
	UPROPERTY(VisibleAnywhere, Category = "Templates")
	FText GlobalTokens;

	/** Returns verified import directory. Avoid accessing Import Directory property directly. */
	UE_API FString GetVerifiedImportDirectory();

private:

	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;

	UE_API FString GetBaseImportDirectory() const;
	UE_API void ResetImportDirectory();
	UE_API void InitializeValuesIfNotSet();

	/** Handler used to update the connection state and source id when a connection with a hub instance is established. */
	UE_API void OnHubConnectionEstablished(FGuid SourceId);

	/** Check whether the hub connection is still active. */
	UE_API void CheckHubConnection();

	/** Start the ingest server */
	UE_API bool StartIngestServer();

	/**
	* Naming tokens for Capture Manager Editor, instantiated each load based on the naming tokens class.
	* This isn't serialized to the config file, and exists here for singleton-like access.
	*/
	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerIngestNamingTokens> GeneralNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerVideoNamingTokens> VideoNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerAudioNamingTokens> AudioNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerCalibrationNamingTokens> CalibrationNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerLensFileNamingTokens> LensFileNamingTokens;

	class ILiveLinkHubMessagingModule* HubMessagingModule;

	/** LiveLink client used to retrieve the status of the hub connection. */
	class ILiveLinkClient* LiveLinkClient = nullptr;

	/** Cached list of detected LLH instance ids. */
	TArray<FGuid> DetectedHubsArray;

	/** Handle to the timer responsible for triggering CheckHubConnection. */
	FTimerHandle TimerHandle;

	/** Interval of the timer to check for connection validity. */
	static constexpr float CheckConnectionIntervalSeconds = 1.0f;

	/** Cached base of the import directory. The base directory differs when used in UE or UEFN */
	FString CachedBaseImportDirectory;
};

#undef UE_API
