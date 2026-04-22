// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "ComputeGraphFromTextFactory.generated.h"

UCLASS(hidecategories = Object)
class UComputeGraphFromTextFactory : public UFactory
{
	GENERATED_BODY()

	//~ Begin UFactory Interface.
	virtual UObject* FactoryCreateNew(
		UClass* InClass, 
		UObject* InParent, 
		FName InName, 
		EObjectFlags Flags, 
		UObject* Context, 
		FFeedbackContext* Warn
		) override;
	//~ End UFactory Interface.

public:
	UComputeGraphFromTextFactory();
};
