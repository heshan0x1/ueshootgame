// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "CameraCalibration.h"
#include "TrackerOpticalFlowConfiguration.h"
#include "UObject/ObjectPtr.h"

#define UE_API METAHUMANPIPELINE_API

class UDNAAsset;
class IMetaHumanFaceTrackerInterface;
class IDepthGeneratorInterface;
class IOpticalFlowInterface;

namespace UE::MetaHuman::Pipeline
{

class FUEImageDataType;

class FFaceTrackerNodeBase
{
public:

	FString SolverTemplateData;
	FString SolverConfigData;
	FString SolverPCAFromDNAData;
	TArray<uint8> PredictiveSolverGlobalTeethTrainingData;
	TArray<uint8> PredictiveSolverTrainingData;
	FString DNAFile;
	TArray<uint8> BrowJSONData;
	TArray<uint8> PCARigMemoryBuffer;
	TWeakObjectPtr<UDNAAsset> DNAAsset;
	TArray<FCameraCalibration> Calibrations;
	FString Camera;
	TArray<uint8> PredictiveWithoutTeethSolver; // use for the global teeth solve
	TArray<uint8> PredictiveSolvers;
	bool bIsFirstPass = true;
	bool bTrackingFailureIsError = true;
	bool bSkipPredictiveSolver = false;
	bool bSkipPerVertexSolve = true;
	FString DebuggingFolder;
	FTrackerOpticalFlowConfiguration OptFlowConfig = FTrackerOpticalFlowConfiguration{ true, false, true };
	
	enum ErrorCode
	{
		FailedToInitialize = 0,
		FailedToTrack,
		UntrainedSolvers,
		FailedToCalculatePCA,
		NoContourData,
		FailedToFindCalibration
	};

protected:
	
	TSharedPtr<IMetaHumanFaceTrackerInterface> Tracker = nullptr;
};

class FFaceTrackerStereoNode : public FNode, public FFaceTrackerNodeBase
{
public:

	UE_API FFaceTrackerStereoNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;
};

class FFaceTrackerIPhoneNode : public FNode, public FFaceTrackerNodeBase
{
public:

	UE_API FFaceTrackerIPhoneNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 NumberOfFrames = 0;
	bool bSkipDiagnostics = false;

protected:

	int32 FrameNumber = 0;
};

// The managed node is a version of the above that take care of loading the correct config
// rather than these being specified by an externally.

class FFaceTrackerIPhoneManagedNode : public FFaceTrackerIPhoneNode
{
public:

	UE_API FFaceTrackerIPhoneManagedNode(const FString& InName);
};

// A node for calculating depth from stereo images

class FDepthGenerateNode : public FNode
{
public:

	UE_API FDepthGenerateNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	TArray<FCameraCalibration> Calibrations;
	TRange<float> DistanceRange;

	enum ErrorCode
	{
		FailedToInitialize = 0,
		FailedToGenerateDepth
	};

protected:

	TSharedPtr<IDepthGeneratorInterface> Reconstructer = nullptr;
};

// A node for calculating optical flow

class FFlowNode : public FNode
{
public:

	UE_API FFlowNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString SolverConfigData;

	bool bUseConfidence = false;
	TArray<FCameraCalibration> Calibrations;
	FString Camera;
	bool bEnableFlow = true;

	enum ErrorCode
	{
		FailedToInitialize = 0,
		FailedToGenerateFlow
	};

private:
	
	TSharedPtr<IOpticalFlowInterface> Flow = nullptr;
	TArray<float> PreviousImage;
};

}

#undef UE_API
