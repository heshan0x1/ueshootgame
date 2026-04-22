// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "FrameNumberTransformer.h"
#include "IFramePathResolver.h"

#define UE_API METAHUMANCAPTUREDATA_API

namespace UE::MetaHuman
{

class FFramePathResolver : public IFramePathResolver
{
public:
	UE_API FFramePathResolver(FString InFilePathTemplate);
	UE_API FFramePathResolver(FString InFilePathTemplate, FFrameNumberTransformer InFrameNumberTransformer);
	UE_API virtual ~FFramePathResolver() override;

	UE_API virtual FString ResolvePath(int32 InFrameNumber) const override;

private:
	FString FilePathTemplate;
	FFrameNumberTransformer FrameNumberTransformer;
};

}

#undef UE_API
