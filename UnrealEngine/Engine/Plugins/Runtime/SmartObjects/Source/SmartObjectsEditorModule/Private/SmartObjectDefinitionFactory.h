// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "SmartObjectDefinitionFactory.generated.h"

#define UE_API SMARTOBJECTSEDITORMODULE_API

/**
 * Factory responsible to create SmartObjectDefinitions
 */
UCLASS(MinimalAPI)
class USmartObjectDefinitionFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

protected:
	UE_API virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

#undef UE_API
