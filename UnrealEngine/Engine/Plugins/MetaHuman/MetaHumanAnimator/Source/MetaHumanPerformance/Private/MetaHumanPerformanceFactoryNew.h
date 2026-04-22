// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "MetaHumanPerformanceFactoryNew.generated.h"

//////////////////////////////////////////////////////////////////////////
// UMetaHumanPerformanceFactoryNew

UCLASS(hideCategories = Object)
class UMetaHumanPerformanceFactoryNew
	: public UFactory
{
	GENERATED_BODY()

public:
	UMetaHumanPerformanceFactoryNew();

	//~Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetToolTip() const override;
	//~End UFactory interface
};
