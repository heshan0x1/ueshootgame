// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "MetaHumanRealtimeSmoothing.h"

#define UE_API METAHUMANPIPELINECORE_API



namespace UE::MetaHuman::Pipeline
{

class FHyprsenseRealtimeSmoothingNode : public FNode
{
public:

	UE_API FHyprsenseRealtimeSmoothingNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	TMap<FName, FMetaHumanRealtimeSmoothingParam> Parameters;
	double DeltaTime = 0;

	enum ErrorCode
	{
		SmoothingFailed = 0,
	};

private:

	TSharedPtr<FMetaHumanRealtimeSmoothing> Smoothing;
	TArray<FName> Keys;
};

}

#undef UE_API
