// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanTakeData.h"
#include "Async/EventSourceUtils.h"

#include "UObject/ObjectMacros.h"

#include "Error/Result.h"
#include "MetaHumanCaptureError.h"

#include "IpAddressDetailsCustomization.h"

#include "MetaHumanCaptureSource.generated.h"

#define UE_API METAHUMANCAPTURESOURCE_API

/** Capture Source Asset Type */
UENUM(meta = (Deprecated = "5.7", DeprecationMessage = "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module"))
enum class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	EMetaHumanCaptureSourceType : uint8
{
	Undefined				UMETA(DisplayName = "Undefined"),
	LiveLinkFaceConnection	UMETA(DisplayName = "LiveLinkFace Connection"),
	LiveLinkFaceArchives	UMETA(DisplayName = "LiveLinkFace Archives"),
	HMCArchives				UMETA(DisplayName = "Stereo HMC Archives"),
};

UENUM()
enum class EMetaHumanCaptureDepthPrecisionType : uint8
{
	Eightieth				UMETA(DisplayName = "0.125 mm"), // Fraction of a cm
	Full
};

UENUM()
enum class EMetaHumanCaptureDepthResolutionType : uint8
{
	Full,
	Half,
	Quarter
};

USTRUCT(BlueprintType, meta = (Deprecated = "5.7", DeprecationMessage = "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module"))
// DEPRECATED: This functionality is now available in the CaptureManager/CaptureManagerDevices module.
struct FMetaHumanCaptureVoidResult
{
	GENERATED_BODY()

	// Note: We can't use UE_DEPRECATED to deprecate this type unfortunately. If we try, we hit warnings in TTuple which
	// we can't suppress, or else warnings in UHT generated files which we likewise can't suppress.

	UPROPERTY(VisibleAnywhere, Category = "Capture Void Result")
	bool bIsValid = true;

	UPROPERTY(VisibleAnywhere, Category = "Capture Void Result")
	int32 Code = 0;

	UPROPERTY(VisibleAnywhere, Category = "Capture Void Result")
	FString Message = TEXT("");

public:
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	void SetResult(TResult<void, FMetaHumanCaptureError> InResult);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

USTRUCT(BlueprintType, meta = (Deprecated = "5.7", DeprecationMessage = "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module"))
// DEPRECATED: This functionality is now available in the CaptureManager/CaptureManagerDevices module.
struct FMetaHumanCapturePerTakeVoidResult
{
	GENERATED_BODY()

	// Note: We can't use UE_DEPRECATED to deprecate this type unfortunately. If we try, we hit warnings in TTuple which
	// we can't suppress, or else warnings in UHT generated files which we likewise can't suppress.

	UPROPERTY(VisibleAnywhere, Category = "Capture Per Take Void Result")
	FMetaHumanCaptureVoidResult Result;

	UPROPERTY(VisibleAnywhere, Category = "Capture Per Take Void Result")
	int32 TakeId = INVALID_ID;
};

/** Capture Source Asset
*
*   An asset representing a physical device or an archive
*   that can be used to import the footage data into Unreal Editor.
*
*   A footage of live performance, in combination with a Skeletal Mesh
*   obtained through MetaHuman Identity asset toolkit. Used in MetaHuman
*   Performance asset to generate an Animation Sequence by automatically
*   tracking facial features of the actor in the performance
**/
UCLASS(MinimalAPI, BlueprintType, meta = (Deprecated = "5.7", DeprecationMessage = "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module"))
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	UMetaHumanCaptureSource : public UObject
{
	GENERATED_BODY()

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

	UPROPERTY(EditAnywhere, Category = "Capture Source", meta = (EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::MonoArchives", EditConditionHides))
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
					  EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::MonoArchives || CaptureSourceType == EMetaHumanCaptureSourceType::LiveLinkFaceArchivesRGB ",
					  EditConditionHides))
	EMetaHumanCaptureDepthPrecisionType DepthPrecision = EMetaHumanCaptureDepthPrecisionType::Eightieth;

	UPROPERTY(EditAnywhere, Category = "Capture Source",
			  meta = (ToolTip = "Resolution scaling applied to the calculated depth data. Full resolution is more accurate, but requires more disk space to store.",
					  EditCondition = "CaptureSourceType == EMetaHumanCaptureSourceType::HMCArchives || CaptureSourceType == EMetaHumanCaptureSourceType::MonoArchives",
					  EditConditionHides))
	EMetaHumanCaptureDepthResolutionType DepthResolution = EMetaHumanCaptureDepthResolutionType::Full;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

private:

#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif

	UE_API virtual void PostLoad();
};

#undef UE_API
