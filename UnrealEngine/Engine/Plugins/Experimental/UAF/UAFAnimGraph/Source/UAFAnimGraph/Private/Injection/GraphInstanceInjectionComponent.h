// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InjectionInfo.h"
#include "Graph/UAFGraphInstanceComponent.h"
#include "GraphInstanceInjectionComponent.generated.h"

/**
 * FGraphInstanceInjectionComponent
 * This component maintains injection info for a graph
 */
USTRUCT()
struct FGraphInstanceInjectionComponent : public FUAFGraphInstanceComponent
{
	GENERATED_BODY()

	FGraphInstanceInjectionComponent();

	const UE::UAF::FInjectionInfo& GetInjectionInfo() const
	{
		return InjectionInfo;
	}

private:
	UE::UAF::FInjectionInfo InjectionInfo;
};
