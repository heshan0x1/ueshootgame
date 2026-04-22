// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "UObject/SoftObjectPath.h"

#include "Templates/ValueOrError.h"

#include "MetaHumanCalibrationGeneratorConfig.generated.h"

/** Options that will used as part of the camera calibration process */
UCLASS(BlueprintType, Blueprintable)
class UMetaHumanCalibrationGeneratorConfig
	: public UObject
{
public:

	GENERATED_BODY()

	/** The width of the chessboard used to record the calibration footage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (ClampMin = "1", UIMin = "1", ClampMax = "50", UIMax = "50", NoSpinbox = "true"))
	int32 BoardPatternWidth = 11;

	/** The height of the chessboard used to record the calibration footage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (ClampMin = "1", UIMin = "1", ClampMax = "50", UIMax = "50", NoSpinbox = "true"))
	int32 BoardPatternHeight = 16;

	/** The square size of the chessboard used to record the calibration footage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (Units = "Centimeters"))
	float BoardSquareSize = 0.75f;

	TValueOrError<void, FString> CheckConfigValidity() const;
};