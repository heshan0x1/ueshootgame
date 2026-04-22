// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HyprsenseUtils.h"

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "HAL/ThreadSafeBool.h"
#include "HyprsenseRealtimeNode.generated.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::NNE
{
	class IModelInstanceGPU;
}

UENUM()
enum class EHyprsenseRealtimeNodeDebugImage : uint8
{
	None = 0,
	Input UMETA(DisplayName = "Input Video"),
	FaceDetect,
	Headpose,
	Trackers,
	Solver
};

UENUM()
enum class EHyprsenseRealtimeNodeState : uint8
{
	Unknown = 0,
	OK,
	NoFace,
	SubjectTooFar,
};

namespace UE::MetaHuman::Pipeline
{

class FHyprsenseRealtimeNode : public FNode, public FHyprsenseUtils
{
public:

	UE_API FHyprsenseRealtimeNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	UE_API bool LoadModels();

	enum ErrorCode
	{
		FailedToInitialize = 0,
		FailedToDetect,
		FailedToTrack,
		FailedToSolve
	};

	UE_API void SetDebugImage(EHyprsenseRealtimeNodeDebugImage InDebugImage);
	UE_API EHyprsenseRealtimeNodeDebugImage GetDebugImage();

	UE_API void SetFocalLength(float InFocalLength);
	UE_API float GetFocalLength();

	UE_API void SetHeadStabilization(bool bInHeadStabilization);
	UE_API bool GetHeadStabilization() const;

private:

	EHyprsenseRealtimeNodeDebugImage DebugImage = EHyprsenseRealtimeNodeDebugImage::None;
	FCriticalSection DebugImageMutex;

	float FocalLength = -1;
	FCriticalSection FocalLengthMutex;

	FThreadSafeBool bHeadStabilization = true;

	TSharedPtr<UE::NNE::IModelInstanceGPU> FaceDetector = nullptr;
	TSharedPtr<UE::NNE::IModelInstanceGPU> Headpose = nullptr;
	TSharedPtr<UE::NNE::IModelInstanceGPU> Solver = nullptr;

	const uint32 HeadposeInputSizeX = 256;
	const uint32 HeadposeInputSizeY = 256;

	const uint32 SolverInputSizeX = 256;
	const uint32 SolverInputSizeY = 512;

	bool bFaceDetected = false;
	TArray<FVector2D> TrackingPoints;
	FVector HeadTranslation = FVector::ZeroVector;

	const float FaceScoreThreshold = 0.5;

	const float LandmarkAwareSmoothingThresholdInCm = 1.5f;
	TArray<FVector2D> PreviousTrackingPoints;
	FTransform PreviousTransform;
	UE_API FTransform LandmarkAwareSmooth(const TArray<FVector2D>& InTrackingPoints, const FTransform& InTransform, const float InFocalLength);

	UE_API Matrix23f GetTransformFromPoints(const TArray<FVector2D>& InPoints, const FVector2D& InSize, bool bInIsStableBox, Matrix23f& OutTransformInv) const;
};

}

#undef UE_API
