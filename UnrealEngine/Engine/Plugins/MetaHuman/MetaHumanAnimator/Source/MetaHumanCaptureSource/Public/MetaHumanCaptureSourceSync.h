// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCaptureSource.h"

#include "MetaHumanCaptureSourceSync.generated.h"

#define UE_API METAHUMANCAPTURESOURCE_API

namespace UE::MetaHuman
{
class FIngester;
}

UCLASS(MinimalAPI, BlueprintType, meta = (Deprecated = "5.7", DeprecationMessage = "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module"))
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	UMetaHumanCaptureSourceSync : public UObject
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanCaptureSourceSync();
	UE_API UMetaHumanCaptureSourceSync(FVTableHelper& Helper);
	UE_API virtual ~UMetaHumanCaptureSourceSync();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API bool CanStartup() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API bool CanIngestTakes() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API bool CanCancel() const;

	/**
	 * @brief Startup the MetaHuman|Footage Ingest API. Get information on the available takes based on the type of this Capture Source
	 * @param bSynchronous If true, this will be a blocking function. Useful when initializing from blueprints or python
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API void Startup();

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API TArray<FMetaHumanTakeInfo> Refresh();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API void SetTargetPath(const FString& InTargetIngestDirectory, const FString& InTargetFolderAssetPath);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API void Shutdown();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API bool IsProcessing() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API bool IsCancelling() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API void CancelProcessing(const TArray<int32>& InTakeIdList);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API int32 GetNumTakes() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API TArray<int32> GetTakeIds() const;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API bool GetTakeInfo(int32 InTakeId, FMetaHumanTakeInfo& OutTakeInfo) const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Footage Ingest API")
	UE_API TArray<FMetaHumanTake> GetTakes(const TArray<int32>& InTakeIdList);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

public:
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY(EditAnywhere, Category = "Capture Source")
	EMetaHumanCaptureSourceType CaptureSourceType = EMetaHumanCaptureSourceType::Undefined;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UPROPERTY(EditAnywhere, Category = "Capture Source", meta = (EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceArchives || CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceArchivesRGB || CaptureSourceType == EMetaHumanCaptureSourceType::MonoArchives", EditConditionHides))
	FDirectoryPath StoragePath;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property has changed its type"))
	FString DeviceAddress_DEPRECATED;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY(EditAnywhere, DisplayName = "Device Address", Category = "Capture Source", meta = (EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceConnection", EditConditionHides))
	FDeviceAddress DeviceIpAddress;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UPROPERTY(EditAnywhere, Category = "Capture Source", meta = (EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceConnection", EditConditionHides))
	uint16 DeviceControlPort = 14785;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property isn't used anymore as the port is being provided automatically by the OS"))
	uint16 ExportListeningPort_DEPRECATED = 8000;

	// TODO: Expose this parameter once uncompressed EXR's are supported
	UPROPERTY(/*EditAnywhere, DisplayName = "Compress Depth Files", Category = "Capture Source", meta = (EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceArchives || CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceConnection", EditConditionHides)*/)
	bool ShouldCompressDepthFiles = true;

	UPROPERTY(EditAnywhere, Category = "Capture Source", meta = (EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives", EditConditionHides))
	bool CopyImagesToProject = true;

	UPROPERTY(EditAnywhere, Category = "Capture Source",
			  meta = (ToolTip = "The minimum cm from the camera expected for valid depth information.\n Depth information closer than this will be ignored to help filter out noise.",
					  ClampMin = "0.0", ClampMax = "200.0",
					  UIMin = "0.0", UIMax = "200.0",
					  EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives",
					  EditConditionHides))
	float MinDistance = 10.0;

	UPROPERTY(EditAnywhere, Category = "Capture Source",
			  meta = (ToolTip = "The maximum cm from the camera expected for valid depth information.\n Depth information beyond this will be ignored to help filter out noise.",
					  ClampMin = "0.0", ClampMax = "200.0",
					  UIMin = "0.0", UIMax = "200.0",
					  EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives",
					  EditConditionHides))
	float MaxDistance = 25.0;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY(EditAnywhere, Category = "Capture Source",
			  meta = (ToolTip = "Precision of the calculated depth data. Full precision is more accurate, but requires more disk space to store.",
					  EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceArchivesRGB || CaptureSourceType == EMetaHumanCaptureSourceType::MonoArchives",
					  EditConditionHides))
	EMetaHumanCaptureDepthPrecisionType DepthPrecision = EMetaHumanCaptureDepthPrecisionType::Eightieth;

	UPROPERTY(EditAnywhere, Category = "Capture Source",
			  meta = (ToolTip = "Resolution scaling applied to the calculated depth data. Full resolution is more accurate, but requires more disk space to store.",
					  EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceArchivesRGB || CaptureSourceType == EMetaHumanCaptureSourceType::MonoArchives",
					  EditConditionHides))
	EMetaHumanCaptureDepthResolutionType DepthResolution = EMetaHumanCaptureDepthResolutionType::Full;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif

private:
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TUniquePtr<UE::MetaHuman::FIngester> Ingester;

	// Do not expose this property to the editor or blueprints, it is only for garbage collection purposes.
	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCaptureSource> MetaHumanCaptureSource;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

#undef UE_API
