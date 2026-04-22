// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviors/ScriptableToolDoubleClickBehavior.h"
#include "BaseBehaviors/DoubleClickBehavior.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptableToolDoubleClickBehavior)

USingleClickInputBehavior* UScriptableToolDoubleClickBehavior::CreateNewBehavior() const
{
	return NewObject<UDoubleClickInputBehavior>();
}
