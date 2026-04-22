// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MouseWheelBehavior.h"
#include "MouseWheelModifierInputBehavior.generated.h"

#define UE_API INTERACTIVETOOLSFRAMEWORK_API

/**
 * 
 */
UCLASS(MinimalAPI)
class UMouseWheelModifierInputBehavior : public UMouseWheelInputBehavior
{
	GENERATED_BODY()

public:
	UE_API virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& InputState) override;
};

#undef UE_API
