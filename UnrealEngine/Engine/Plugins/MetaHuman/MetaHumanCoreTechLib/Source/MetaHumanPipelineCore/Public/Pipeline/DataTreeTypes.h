// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "FrameTrackingContourData.h"
#include "FrameTrackingConfidenceData.h"
#include "FrameAnimationData.h"
#include "DepthMapDiagnosticsResult.h"
#include "Misc/QualifiedFrameTime.h"


namespace UE::MetaHuman::Pipeline
{

class FInvalidDataType
{
};

enum class EPipelineExitStatus
{
	Unknown = 0,
	OutOfScope,
	AlreadyRunning,
	NotInGameThread,
	InvalidNodeTypeName,
	InvalidNodeName,
	DuplicateNodeName,
	InvalidPinName,
	DuplicatePinName,
	InvalidConnection,
	AmbiguousConnection,
	Unconnected,
	LoopConnection,
	Ok,
	Aborted,
	StartError,
	ProcessError,
	EndError,
	TooFast,
	InsufficientThreadsForNodes
};

class FUEImageDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<uint8> Data; // bgra order, a=255 for fully opaque
};

class FUEGrayImageDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<uint8> Data;
};

class FHSImageDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<float> Data;
};

class FScalingDataType
{
public:

	float Factor = -1.0f;
};

class FDepthDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<float> Data;
};

class FFlowOutputDataType
{
public:

	TArray<float> Flow;
	TArray<float> Confidence;
	TArray<float> SourceCamera;
	TArray<float> TargetCamera;
};

class FAudioDataType
{
public:

	int32 NumChannels = -1;
	int32 SampleRate = -1;
	int32 NumSamples = -1;
	TArray<float> Data;
	bool bContiguous = true; // whether consecutive audio sample are contiguous
};

using FDataTreeType = TVariant<FInvalidDataType, EPipelineExitStatus, int32, float, double, bool, FString, FUEImageDataType, FUEGrayImageDataType, FHSImageDataType, FScalingDataType, FFrameTrackingContourData, FFrameAnimationData, FDepthDataType, FFrameTrackingConfidenceData, TArray<int32>, TArray<FFrameAnimationData>, FFlowOutputDataType, TMap<FString, FDepthMapDiagnosticsResult>, FAudioDataType, FQualifiedFrameTime>;

}
