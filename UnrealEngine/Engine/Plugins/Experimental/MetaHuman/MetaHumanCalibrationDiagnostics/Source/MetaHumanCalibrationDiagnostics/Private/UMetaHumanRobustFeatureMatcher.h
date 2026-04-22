// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "CaptureData.h"

#include "MetaHumanRobustFeatureMatcher.h"
#include "MetaHumanCalibrationDiagnosticsOptions.h"

#include "UMetaHumanRobustFeatureMatcher.generated.h"

USTRUCT(BlueprintType, Blueprintable)
struct FCameraPoints
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman | Calibration Diagnostics")
	TArray<FVector2D> Points;
};

USTRUCT(BlueprintType, Blueprintable)
struct FDetectedFeatures
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman | Calibration Diagnostics")
	int32 FrameIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman | Calibration Diagnostics")
	TArray<FVector2D> Points3d;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman | Calibration Diagnostics")
	TArray<FCameraPoints> CameraPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman | Calibration Diagnostics")
	TArray<FCameraPoints> Points3dReprojected;

	bool IsValid() const;
};

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanRobustFeatureMatcher : public UObject
{
	GENERATED_BODY()
public:

	UMetaHumanRobustFeatureMatcher();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Diagnostics")
	bool Init(UFootageCaptureData* InCaptureData, UMetaHumanCalibrationDiagnosticsOptions* InOptions);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Diagnostics")
	bool DetectFeatures(int64 InFrame);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Diagnostics")
	FDetectedFeatures GetFeatures(int64 InFrame);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Diagnostics")
	TArray<FString> GetImagePaths(const FString& InCameraName);

private:

	TMap<FString, TArray<FString>> StereoPairImagePaths;
	TSharedPtr<UE::Wrappers::FMetaHumanRobustFeatureMatcher> FeatureMatcher;
};