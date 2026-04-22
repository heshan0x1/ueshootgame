// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCAPTUREDATA_API

class FTrackingPathUtils
{
public:
	static UE_API bool GetTrackingFilePathAndInfo(const class UImgMediaSource* InImgSequence, FString& OutTrackingFilePath, int32& OutFrameOffset, int32& OutNumFrames);
	static UE_API bool GetTrackingFilePathAndInfo(const FString& InFullSequencePath, FString& OutTrackingFilePath, int32& OutFrameOffset, int32& OutNumFrames);

	static UE_API FString ExpandFilePathFormat(const FString& InFilePathFormat, int32 InFrameNumber);
};

#undef UE_API
