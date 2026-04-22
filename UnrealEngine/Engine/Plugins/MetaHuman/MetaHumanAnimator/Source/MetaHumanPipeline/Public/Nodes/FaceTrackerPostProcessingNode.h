// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "CameraCalibration.h"
#include "UObject/ObjectPtr.h"
#include "DNAAsset.h"
#include "FrameTrackingContourData.h"
#include "FrameAnimationData.h"

#define UE_API METAHUMANPIPELINE_API


class IFaceTrackerPostProcessingInterface;

namespace UE::MetaHuman::Pipeline
{

class FFaceTrackerPostProcessingNode : public FNode
{
public:

	UE_API FFaceTrackerPostProcessingNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString TemplateData;
	FString ConfigData;
	FString DefinitionsData;
	FString HierarchicalDefinitionsData;
	FString DNAFile;
	TWeakObjectPtr<UDNAAsset> DNAAsset;
	TArray<FCameraCalibration> Calibrations;
	FString Camera;
	TArray<uint8> PredictiveWithoutTeethSolver; // use for the global teeth solve
	TArray<FFrameTrackingContourData> TrackingData;
	TArray<FFrameAnimationData> FrameData;
	FString DebuggingFolder;
	bool bSolveForTweakers = false;
	bool bDisableGlobalSolves = false;

	enum ErrorCode
	{
		FailedToInitialize = 0,
		FailedToTrack,
		BadFrame
	};

protected:

	TSharedPtr<IFaceTrackerPostProcessingInterface> Tracker = nullptr;
	TMap<int, FFrameAnimationData> AnimationWindow;
	int32 FrameNumber = 0;
};

// The managed node is a version of the above that take care of loading the correct config
// rather than these being specified by an externally.

class FFaceTrackerPostProcessingManagedNode : public FFaceTrackerPostProcessingNode
{
public:

	UE_API FFaceTrackerPostProcessingManagedNode(const FString& InName);
};

}

#undef UE_API
