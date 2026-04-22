// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphInstanceInjectionComponent.h"
#include "Graph/AnimNextGraphInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GraphInstanceInjectionComponent)

FGraphInstanceInjectionComponent::FGraphInstanceInjectionComponent()
	: FUAFGraphInstanceComponent()
	, InjectionInfo(GetGraphInstance())
{
}

