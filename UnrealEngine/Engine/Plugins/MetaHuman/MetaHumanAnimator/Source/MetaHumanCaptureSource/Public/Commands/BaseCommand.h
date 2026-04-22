// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

#define UE_API METAHUMANCAPTURESOURCE_API

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FBaseCommandArgs
{
public:
	UE_API FBaseCommandArgs(const FString& InName);
	virtual ~FBaseCommandArgs() = default;

	UE_API const FString& GetCommandName() const;

private:

	FString CommandName;
};

#undef UE_API
