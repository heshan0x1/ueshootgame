// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "SpeechToAnimNode.h"

namespace UE::MetaHuman::Pipeline
{

class FTongueTrackerNode : public FSpeechToAnimNode
{
public:

	METAHUMANPIPELINE_API FTongueTrackerNode(const FString& InName);
	METAHUMANPIPELINE_API virtual ~FTongueTrackerNode() override;

	METAHUMANPIPELINE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

private:
	METAHUMANPIPELINE_API bool PostConversionModifyRawControls(TMap<FString, float>& InOutAnimationFrame, FString& OutErrorMsg) override;

	static METAHUMANPIPELINE_API const TArray<FString> AffectedRawTongueControls;
};

}
#endif // WITH_EDITOR
