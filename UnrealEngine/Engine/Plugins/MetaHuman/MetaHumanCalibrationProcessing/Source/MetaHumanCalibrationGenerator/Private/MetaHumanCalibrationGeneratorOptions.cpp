// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationGeneratorOptions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCalibrationGeneratorOptions)

TValueOrError<void, FString> UMetaHumanCalibrationGeneratorOptions::CheckOptionsValidity() const
{
	if (PackagePath.Path.IsEmpty())
	{
		return MakeError(TEXT("Package path is empty"));
	}

	return MakeValue();
}

void UMetaHumanCalibrationGeneratorOptions::SetSelectedFrames(TArray<int32> InSelectedFrames)
{
	SelectedFrames = MoveTemp(InSelectedFrames);
}

bool UMetaHumanCalibrationGeneratorOptions::IsSelectedFramesEmpty() const
{
	return SelectedFrames.IsEmpty();
}