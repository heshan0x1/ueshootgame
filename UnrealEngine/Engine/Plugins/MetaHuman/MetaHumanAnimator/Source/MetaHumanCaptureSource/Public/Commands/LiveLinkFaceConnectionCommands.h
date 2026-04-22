// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCommand.h"
#include "Misc/Optional.h"

#define UE_API METAHUMANCAPTURESOURCE_API

PRAGMA_DISABLE_DEPRECATION_WARNINGS

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FStartCaptureCommandArgs : public FBaseCommandArgs
{
public:
	static UE_API const FString CommandName;

	UE_API FStartCaptureCommandArgs(FString InSlateName,
							 uint16 InTakeNumber,
							 TOptional<FString> InSubject = TOptional<FString>(),
							 TOptional<FString> InScenario = TOptional<FString>(),
							 TOptional<TArray<FString>> InTags = TOptional<TArray<FString>>());

	virtual ~FStartCaptureCommandArgs() override = default;

	FString SlateName;
	uint16 TakeNumber;
	TOptional<FString> Subject;
	TOptional<FString> Scenario;
	TOptional<TArray<FString>> Tags;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FStopCaptureCommandArgs: public FBaseCommandArgs
{
public:
	static UE_API const FString CommandName;

	UE_API FStopCaptureCommandArgs(bool bInShouldFetchTake = true);

	virtual ~FStopCaptureCommandArgs() override = default;

	// Introduced as a solution to the current async code and object lifecycle.
	// Should be removed when a proper design is in place.
	bool bShouldFetchTake;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef UE_API
