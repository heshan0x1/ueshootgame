// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "HAL/ThreadSafeBool.h"

#define UE_API METAHUMANPIPELINECORE_API



namespace UE::MetaHuman::Pipeline
{

class FNeutralFrameNode : public FNode
{
public:

	UE_API FNeutralFrameNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FThreadSafeBool bIsNeutralFrame = false;
};

}

#undef UE_API
