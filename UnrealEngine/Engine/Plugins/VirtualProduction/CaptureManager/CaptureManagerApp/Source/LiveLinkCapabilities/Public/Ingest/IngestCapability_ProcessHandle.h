// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkDevice.h"

#include "Templates/ValueOrError.h"
#include "Async/ManagedDelegate.h"

#include "IngestCapability_Options.h"
#include "IngestCapability_TakeInformation.h"

#include "Async/TaskProgress.h"

#include "IngestCapability_ProcessHandle.generated.h"

class LIVELINKCAPABILITIES_API FIngestCapability_Error
{
public:

	enum ECode
	{
		Ok = 0,
		AbortedByUser = 1,
		InternalError,
		InvalidArgument,
		DownloaderError,
		UnrealEndpointNotFound,
		UnrealEndpointConnectionTimedOut,
		UnrealEndpointUploadError,
		ConversionError
	};

	FIngestCapability_Error(ECode InCode, FString InMessage);

	ECode GetCode() const;
	const FString& GetMessage() const;

private:

	ECode Code;
	FString Message;
};

UENUM(BlueprintType)
enum class EIngestCapability_ProcessConfig : uint8
{
	DownloadStep = 1 << 0 UMETA(Hidden),
	ConvertAndUploadStep = 1 << 1 UMETA(Hidden),

	None = 0 UMETA(Hidden),
	Download = DownloadStep  UMETA(DisplayName = "Download", ToolTip = "Download only. Copies data to specified download directory\n\nA Take Archive device can be used to ingest the downloaded take at another time"),
	Ingest = DownloadStep | ConvertAndUploadStep UMETA(DisplayName = "Ingest", ToolTip = "Ingest data to specified UE/UEFN client")
};

ENUM_CLASS_FLAGS(EIngestCapability_ProcessConfig);

UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_TwoParams(FProcessFinishReporter, const UIngestCapability_ProcessHandle*, ProcessHandle, UIngestCapability_ProcessResult*, IngestProcessResult);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FProcessProgressReporter, const UIngestCapability_ProcessHandle*, ProcessHandle, double, Progress);

using FIngestProcessProgressReporter = UE::CaptureManager::TManagedDelegate<const UIngestCapability_ProcessHandle*, double>;
using FIngestProcessFinishReporter = UE::CaptureManager::TManagedDelegate<const UIngestCapability_ProcessHandle*, TValueOrError<void, FIngestCapability_Error>>;

UCLASS(BlueprintType)
class LIVELINKCAPABILITIES_API UIngestCapability_ProcessResult : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live Link Device|Ingest")
	FText Message;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live Link Device|Ingest")
	int32 Code = 0;

	UFUNCTION(BlueprintCallable, Category = "Live Link Device|Ingest")
	bool IsValid() const;

	UFUNCTION(BlueprintCallable, Category = "Live Link Device|Ingest")
	bool IsError() const;

	static UIngestCapability_ProcessResult* Success();
	static UIngestCapability_ProcessResult* Error(FText InMessage, int32 InCode = -1);
};

class LIVELINKCAPABILITIES_API FIngestCapability_ProcessContext
{
private:

	struct FPrivateToken { explicit FPrivateToken() = default; };

public:

	explicit FIngestCapability_ProcessContext(int32 InTakeId,
											  EIngestCapability_ProcessConfig InProcessConfig,
											  class ILiveLinkDeviceCapability_Ingest* InOwner,
											  FPrivateToken);

	bool IsDone() const;

	FProcessFinishReporter ProcessFinishedReporterDynamic;
	FIngestProcessFinishReporter ProcessFinishedReporter;

	FProcessProgressReporter ProcessProgressReporterDynamic;
	FIngestProcessProgressReporter ProcessProgressReporter;

	int32 TakeId = INDEX_NONE;

private:

	TStrongObjectPtr<const UIngestCapability_Options> IngestOptions;
	TSharedPtr<UE::CaptureManager::FTaskProgress> TaskProgress;
	UE::CaptureManager::FTaskProgress::FTask CurrentTask;

	EIngestCapability_ProcessConfig ProcessConfig = EIngestCapability_ProcessConfig::None;
	EIngestCapability_ProcessConfig CurrentStep = EIngestCapability_ProcessConfig::None;
	int32 NumberOfSteps = 0;
	class ILiveLinkDeviceCapability_Ingest* Owner;

	friend class ILiveLinkDeviceCapability_Ingest;
};

UCLASS(BlueprintType)
class LIVELINKCAPABILITIES_API UIngestCapability_ProcessHandle : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Live Link Device|Ingest")
	int32 GetTakeId() const;

	UFUNCTION(BlueprintCallable, Category = "Live Link Device|Ingest")
	bool IsDone() const;

	UFUNCTION(BlueprintCallable, Category = "Live Link Device|Ingest")
	FProcessFinishReporter& OnProcessFinishReporterDynamic();

	FIngestProcessFinishReporter& OnProcessFinishReporter();

	UFUNCTION(BlueprintCallable, Category = "Live Link Device|Ingest")
	FProcessProgressReporter& OnProcessProgressReporterDynamic();

	FIngestProcessProgressReporter& OnProcessProgressReporter();

private:

	UIngestCapability_ProcessHandle();

	void Initialize(TUniquePtr<FIngestCapability_ProcessContext> InContext);

	TUniquePtr<FIngestCapability_ProcessContext> Context;

	friend class ILiveLinkDeviceCapability_Ingest;
};