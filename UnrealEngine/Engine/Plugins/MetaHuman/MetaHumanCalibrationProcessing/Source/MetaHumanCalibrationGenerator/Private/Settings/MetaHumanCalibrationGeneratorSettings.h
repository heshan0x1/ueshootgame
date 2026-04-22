// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "MetaHumanCalibrationGeneratorConfig.h"

#include "Math/Vector.h"

#include "MetaHumanCalibrationGeneratorSettings.generated.h"

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UMetaHumanCalibrationGeneratorSettings : public UObject
{
public:

	GENERATED_BODY()

	DECLARE_MULTICAST_DELEGATE(FCoverageMapChanged);

	/** Image scaling factor applied during grid detection for display purposes only. This does not affect the accuracy of grid detection during calibration generation */
	UPROPERTY(config, EditAnywhere, Category = "Visualization", meta = (ClampMin = "0.1", UIMin = "0.1", ClampMax = "1.0", UIMax = "1.0", NoSpinbox = "true"))
	float ScaleFactor = 0.5;

	/** Maximum number of points for which the block from the grid is considered covered 100% */
	UPROPERTY(config, EditAnywhere, Category = "Visualization", meta = (ClampMin = "1", UIMin= "1"))
	int32 NumberOfPointsThreshold = 50;

	/** Size of the map to be used for getting an estimate of the coverage of the chessboard in the footage */
	UPROPERTY(config, EditAnywhere, Category = "Visualization", meta = (ClampMin = "1", UIMin = "1", ClampMax = "64", UIMax = "64", NoSpinbox = "true"))
	FIntVector2 CoverageMap = FIntVector2(8, 8);

	/** The parameter used to determine which frames will be involved in the automatic frame selection (N-th frame) */
	UPROPERTY(config, EditAnywhere, Category = "Frame Selection", meta = (ClampMin = "1", UIMin = "1"))
	int32 AutomaticFrameSelectionSampleRate = 10;

	/** The number of frames that will be an output from the automatic frame selection process */
	UPROPERTY(config, EditAnywhere, Category = "Frame Selection")
	int32 TargetNumberOfFrames = 30;

	/** Minimum number of points for which the block from the grid will be considerd occupied for a single frame */
	UPROPERTY(config, EditAnywhere, Category = "Frame Selection", meta = (ClampMin = "1", UIMin = "1"))
	int32 MinimumPointsPerBlock = 5;

	/** Size of the map for getting an estimate of the coverage for the automated frame selection process */
	UPROPERTY(config, EditAnywhere, Category = "Frame Selection", meta = (ClampMin = "1", UIMin = "1", ClampMax = "64", UIMax = "64", NoSpinbox = "true"))
	FIntVector2 AutomaticFrameSelectionCoverageMap = FIntVector2(6, 8);

	/** Last config used to perform the calibration process */
	UPROPERTY(config)
	FString LastConfigUsed;

	FCoverageMapChanged& OnCoverageMapChanged()
	{
		return CoverageMapChanged;
	}

private:

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override final
	{
		if (InPropertyChangedEvent.MemberProperty->GetName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationGeneratorSettings, CoverageMap))
		{
			CoverageMapChanged.Broadcast();
		}
	}

	FCoverageMapChanged CoverageMapChanged;
};