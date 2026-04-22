// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Pipeline.h"
#include "Pipeline/DataTree.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::MetaHuman::Pipeline
{

class FPipelineData : public FDataTree
{
public:

	UE_API FPipelineData();

	UE_API void SetFrameNumber(int32 InFrameNumber);
	UE_API int32 GetFrameNumber() const;

	UE_API void SetExitStatus(EPipelineExitStatus InExitStatus);
	UE_API EPipelineExitStatus GetExitStatus() const;

	UE_API void SetErrorMessage(const FString& InErrorMessage);
	UE_API const FString& GetErrorMessage() const;

	UE_API void SetErrorNodeName(const FString& InErrorNodeName);
	UE_API const FString& GetErrorNodeName() const;

	UE_API void SetErrorNodeCode(int32 InErrorNodeCode);
	UE_API int32 GetErrorNodeCode() const;

	UE_API void SetErrorNodeMessage(const FString& InErrorNodeMessage);
	UE_API const FString& GetErrorNodeMessage() const;

	UE_API void SetEndFrameMarker(bool bInEndFrameMarker);
	UE_API bool GetEndFrameMarker() const;

	UE_API void SetDropFrame(bool bInDropFrame);
	UE_API bool GetDropFrame() const;

	UE_API void SetMarkerStartTime(const FString& InMarkerName);
	UE_API double GetMarkerStartTime(const FString& InMarkerName) const;

	UE_API void SetMarkerEndTime(const FString& InMarkerName);
	UE_API double GetMarkerEndTime(const FString& InMarkerName) const;

	UE_API void SetUseGPU(const FString& InUseGPU);
	UE_API FString GetUseGPU() const;

private:

	UE_API void SetMarkerTime(const FString& InMarkerName);
	UE_API double GetMarkerTime(const FString& InMarkerName) const;
};

}

#undef UE_API
