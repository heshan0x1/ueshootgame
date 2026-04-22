// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"

#define UE_API METAHUMANPIPELINE_API

class FJsonObject;

namespace UE::MetaHuman::Pipeline
{

class FJsonTitanTrackerNode : public FNode
{
public:

	UE_API FJsonTitanTrackerNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString JsonFile;

	enum ErrorCode
	{
		FailedToLoadJsonFile = 0,
		InvalidData
	};

private:

	TArray<FFrameTrackingContourData> Contours;
};

class FJsonTrackerNode : public FNode
{
public:

	UE_API FJsonTrackerNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString JsonFile;

	enum ErrorCode
	{
		FailedToLoadJsonFile = 0,
		InvalidData
	};

private:

	TArray<FFrameTrackingContourData> Contours;
};

class FOffsetContoursNode : public FNode
{
public:

	UE_API FOffsetContoursNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FVector2D ConstantOffset = FVector2D(0, 0);
	FVector2D RandomOffset = FVector2D(0, 0);

private:

	float RandomOffsetMinX = -1.f;
	float RandomOffsetMaxX = -1.f;
	float RandomOffsetMinY = -1.f;
	float RandomOffsetMaxY = -1.f;
};

class FSaveContoursToJsonNode : public FNode
{
public:
	UE_API FSaveContoursToJsonNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	enum ErrorCode
	{
		FailedToCreateJson = 0,
		InvalidData
	};

	FString FullSavePath;
	TSharedPtr<FJsonObject> ContourDataJson;
};

}

#undef UE_API
