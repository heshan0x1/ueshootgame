// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "UObject/LinkerPlaceholderBase.h"

#include "LinkerPlaceholderFunction.generated.h"

/**  */
UCLASS(MinimalAPI)
class ULinkerPlaceholderFunction : public UFunction, public TLinkerImportPlaceholder<UFunction>
{
public:
	GENERATED_BODY()

	ULinkerPlaceholderFunction(const FObjectInitializer& ObjectInitializer);

	// FLinkerPlaceholderBase interface 
	virtual UObject* GetPlaceholderAsUObject() override { return (UObject*)(this); }
	// End of FLinkerPlaceholderBase interface
};
