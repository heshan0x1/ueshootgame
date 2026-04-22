// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScriptableToolBehavior.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptableToolBehavior)

void UScriptableToolBehavior::SetDefaultPriority(const FInputCapturePriority& Priority)
{
	GetWrappedBehavior()->SetDefaultPriority(Priority);
}


