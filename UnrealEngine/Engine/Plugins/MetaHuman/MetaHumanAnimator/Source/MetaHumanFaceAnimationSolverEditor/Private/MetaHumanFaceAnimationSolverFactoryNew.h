// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "MetaHumanFaceAnimationSolverFactoryNew.generated.h"


//////////////////////////////////////////////////////////////////////////
// UMetaHumanFaceAnimationSolverFactoryNew

UCLASS(hidecategories=Object)
class UMetaHumanFaceAnimationSolverFactoryNew
	: public UFactory
{
	GENERATED_BODY()

public:

	UMetaHumanFaceAnimationSolverFactoryNew();

	//~Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetToolTip() const override;
	//~End UFactory interface
};
