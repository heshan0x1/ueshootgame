// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "FrameRange.h"

#define UE_API METAHUMANPIPELINE_API


namespace UE::MetaHuman::Pipeline
{

class FDropFrameNode : public FNode
{
public:

	UE_API FDropFrameNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 DropEvery = -1;
	TArray<FFrameRange> ExcludedFrames;
};

}

#undef UE_API
