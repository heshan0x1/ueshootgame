// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CubicCameraSystemIngest.h"



class FStereoReconstructionSystemIngest
	: public FCubicCameraSystemIngest
{
public:

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FStereoReconstructionSystemIngest(const FString& InInputDirectory, 
					  bool bInShouldCompressDepthFiles, 
					  bool bInCopyImagesToProject,
					  TRange<float> InDepthDistance,
					  EMetaHumanCaptureDepthPrecisionType InDepthPrecision,
					  EMetaHumanCaptureDepthResolutionType InDepthResolution);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	virtual ~FStereoReconstructionSystemIngest();


protected:
	TRange<float> DepthDistance;

};
