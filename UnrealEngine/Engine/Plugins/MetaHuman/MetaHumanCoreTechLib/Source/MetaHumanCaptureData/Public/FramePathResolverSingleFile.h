// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IFramePathResolver.h"

#define UE_API METAHUMANCAPTUREDATA_API

namespace UE::MetaHuman
{

/** Always resolves to the same file path */
class FFramePathResolverSingleFile : public IFramePathResolver
{
public:
	UE_API explicit FFramePathResolverSingleFile(FString InFilePath);
	UE_API virtual ~FFramePathResolverSingleFile() override;

	UE_API virtual FString ResolvePath(int32 InFrameNumber) const override;

private:
	FString FilePath;
};

}

#undef UE_API
