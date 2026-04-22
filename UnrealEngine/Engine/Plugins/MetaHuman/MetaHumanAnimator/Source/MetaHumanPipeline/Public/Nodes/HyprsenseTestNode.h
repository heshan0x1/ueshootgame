// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "UObject/WeakObjectPtr.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

#include "SupressWarnings.h"

#define UE_API METAHUMANPIPELINE_API

class UNeuralNetwork;

namespace UE::MetaHuman::Pipeline
{
#if WITH_DEV_AUTOMATION_TESTS
	class FHyprsenseTestNode : public FNode
	{
	public:

		UE_API FHyprsenseTestNode(const FString& InName);
		UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
		UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
		UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

		FString InJsonFilePath;
		FString OutJsonFilePath;

		TArray<FFrameTrackingContourData> ContourByFrame;
		TArray<TMap<FString, float>> ContourDiffAverageByFrame;
		TArray<float> TotalLandmarkDiffAverageByFrame;

		int32 FrameCount = 0;
		float MaxAverageDifference = 0;
		float TotalAverageInAllFrames = 0;
		bool bAllowExtraCurvesInTrackingData = false;
	};
#endif
}

#undef UE_API
