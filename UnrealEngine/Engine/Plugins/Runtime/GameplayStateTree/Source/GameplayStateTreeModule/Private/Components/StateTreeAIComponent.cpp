// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/StateTreeAIComponent.h"

#include "Components/StateTreeAIComponentSchema.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeAIComponent)

TSubclassOf<UStateTreeSchema> UStateTreeAIComponent::GetSchema() const
{
	return UStateTreeAIComponentSchema::StaticClass();
}
