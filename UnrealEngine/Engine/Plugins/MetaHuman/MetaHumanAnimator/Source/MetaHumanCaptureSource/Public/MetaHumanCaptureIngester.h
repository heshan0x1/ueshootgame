// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCaptureSource.h"

#include "Async/EventSourceUtils.h"

#define UE_API METAHUMANCAPTURESOURCE_API

PRAGMA_DISABLE_DEPRECATION_WARNINGS

class IFootageIngestAPI;
class FBaseCommandArgs;

namespace UE::MetaHuman
{

struct UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FIngesterParams
{
	UE_API FIngesterParams(
		EMetaHumanCaptureSourceType InCaptureSourceType,
		FDirectoryPath InStoragePath,
		FDeviceAddress InDeviceAddress,
		uint16 InDeviceControlPort,
		bool InShouldCompressDepthFiles,
		bool InCopyImagesToProject,
		float InMinDistance,
		float InMaxDistance,
		EMetaHumanCaptureDepthPrecisionType InDepthPrecision,
		EMetaHumanCaptureDepthResolutionType InDepthResolution
	);

	EMetaHumanCaptureSourceType CaptureSourceType;
	FDirectoryPath StoragePath;
	FDeviceAddress DeviceAddress;
	uint16 DeviceControlPort;
	bool ShouldCompressDepthFiles;
	bool CopyImagesToProject;
	float MinDistance;
	float MaxDistance;
	EMetaHumanCaptureDepthPrecisionType DepthPrecision;
	EMetaHumanCaptureDepthResolutionType DepthResolution;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FIngester : public FCaptureEventSource
{
public:
	using FRefreshCallback = TManagedDelegate<FMetaHumanCaptureVoidResult>;
	using FGetTakesCallbackPerTake = TManagedDelegate<FMetaHumanCapturePerTakeVoidResult>;

	UE_API explicit FIngester(FIngesterParams InIngesterParams);
	UE_API virtual ~FIngester();

	UE_API void SetParams(FIngesterParams InIngesterParams);

	UE_API bool CanStartup() const;
	UE_API bool CanIngestTakes() const;
	UE_API bool CanCancel() const;

	/**
	 * @brief Startup the footage ingest API. Get information on the available takes based on the type of this Capture Source
	 * @param bSynchronous If true, this will be a blocking function. Useful when initializing from blueprints or python
	 */
	UE_API void Startup(ETakeIngestMode InMode = ETakeIngestMode::Async);
	UE_API void Refresh(FRefreshCallback InCallback);

	UE_API void SetTargetPath(const FString& InTargetIngestDirectory, const FString& InTargetFolderAssetPath);
	UE_API void Shutdown();
	UE_API bool IsProcessing() const;
	UE_API bool IsCancelling() const;

	UE_API void CancelProcessing(const TArray<TakeId>& InTakeIdList);
	UE_API int32 GetNumTakes() const;
	UE_API TArray<TakeId> GetTakeIds() const;
	UE_API bool GetTakeInfo(TakeId InTakeId, FMetaHumanTakeInfo& OutTakeInfo) const;

	UE_API bool GetTakes(const TArray<TakeId>& InTakeIdList, FGetTakesCallbackPerTake InCallback);
	UE_API TOptional<float> GetProcessingProgress(TakeId InTakeId) const;
	UE_API FText GetProcessName(TakeId InTakeId) const;
	UE_API bool ExecuteCommand(class TSharedPtr<class FBaseCommandArgs> InCommand);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGetTakesFinished, const TArray<FMetaHumanTake>& InTakes)
	FOnGetTakesFinished OnGetTakesFinishedDelegate;

	UE_API EMetaHumanCaptureSourceType GetCaptureSourceType() const;

private:
	UE_API void ProxyEvent(TSharedPtr<const FCaptureEvent> Event);

	TUniquePtr<class IFootageIngestAPI> FootageIngestAPI;
	FIngesterParams Params;
};

}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef UE_API
