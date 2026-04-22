// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "UObject/Class.h"

#include "RCStatelessEventController.generated.h"

class URCVirtualPropertyBase;

USTRUCT()
struct FRCStatelessEventController
{
	GENERATED_BODY()

	REMOTECONTROLLOGIC_API static bool IsStatelessEventController(const URCVirtualPropertyBase* InController);
};
