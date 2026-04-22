// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "EaseCurveLibraryFactory.generated.h"

UCLASS()
class UEaseCurveLibraryFactory : public UFactory
{
	GENERATED_BODY()

public:
	UEaseCurveLibraryFactory();

	//~ Begin UFactory
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
	//~ End UFactory
};
