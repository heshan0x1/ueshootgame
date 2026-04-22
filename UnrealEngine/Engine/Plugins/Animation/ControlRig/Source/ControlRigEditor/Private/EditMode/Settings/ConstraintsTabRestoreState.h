// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "ConstraintsTabRestoreState.generated.h"

UENUM()
enum class EControlRigConstrainTab : uint8
{
	Spaces,
	Constraints,
	Snapper
};

USTRUCT()
struct FControlRigConstraintsTabRestoreState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bWasOpen = false;

	UPROPERTY() 
	EControlRigConstrainTab LastOpenInlineTab = EControlRigConstrainTab::Spaces;
};