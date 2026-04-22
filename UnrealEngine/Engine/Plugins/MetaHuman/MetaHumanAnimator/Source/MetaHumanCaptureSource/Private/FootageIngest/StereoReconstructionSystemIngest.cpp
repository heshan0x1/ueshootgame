// Copyright Epic Games, Inc.All Rights Reserved.

#include "StereoReconstructionSystemIngest.h"

#define LOCTEXT_NAMESPACE "FootageIngest"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FStereoReconstructionSystemIngest::FStereoReconstructionSystemIngest(const FString& InInputDirectory, 
									 bool bInShouldCompressDepthFiles, 
									 bool bInCopyImagesToProject,
									 TRange<float> InDepthDistance,
									 EMetaHumanCaptureDepthPrecisionType InDepthPrecision,
									 EMetaHumanCaptureDepthResolutionType InDepthResolution)
	: FCubicCameraSystemIngest(InInputDirectory, bInShouldCompressDepthFiles, bInCopyImagesToProject, InDepthPrecision, InDepthResolution),
	DepthDistance(MoveTemp(InDepthDistance))
{
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

FStereoReconstructionSystemIngest::~FStereoReconstructionSystemIngest() = default;

#undef LOCTEXT_NAMESPACE