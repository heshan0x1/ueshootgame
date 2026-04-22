// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "CoreMinimal.h"

#define UE_API METAHUMANCALIBRATIONLIB_API

class FMetaHumanCalibrationLibModule : public IModuleInterface
{
public:

	static UE_API FString GetVersion();
};

#undef UE_API
