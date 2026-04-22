// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "MetaHumanFaceFittingSolverFactoryNew.generated.h"


//////////////////////////////////////////////////////////////////////////
// UMetaHumanFaceFittingSolverFactoryNew

UCLASS(hidecategories=Object)
class UMetaHumanFaceFittingSolverFactoryNew
	: public UFactory
{
	GENERATED_BODY()

public:

	UMetaHumanFaceFittingSolverFactoryNew();

	//~Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetToolTip() const override;
	//~End UFactory interface
};
