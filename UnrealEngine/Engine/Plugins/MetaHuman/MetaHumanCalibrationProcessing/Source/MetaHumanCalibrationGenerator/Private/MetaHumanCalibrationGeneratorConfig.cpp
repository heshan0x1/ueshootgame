// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationGeneratorConfig.h"

TValueOrError<void, FString> UMetaHumanCalibrationGeneratorConfig::CheckConfigValidity() const
{
	if (BoardPatternHeight <= 0)
	{
		return MakeError(TEXT("Chessboard pattern height is 0"));
	}

	if (BoardPatternWidth <= 0)
	{
		return MakeError(TEXT("Chessboard pattern width is 0"));
	}

	if (FMath::IsNearlyZero(BoardSquareSize) || BoardSquareSize < 0.0f)
	{
		return MakeError(TEXT("Chessboard square size is 0.0"));
	}

	return MakeValue();
}