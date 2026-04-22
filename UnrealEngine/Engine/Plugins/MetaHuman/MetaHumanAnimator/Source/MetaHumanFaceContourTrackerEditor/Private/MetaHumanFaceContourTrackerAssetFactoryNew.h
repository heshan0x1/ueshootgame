// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "UObject/ObjectMacros.h"

#include "MetaHumanFaceContourTrackerAssetFactoryNew.generated.h"


/**
 * Implements a factory for UMetaHumanFaceContourTrackerAsset objects from new
 */
UCLASS(hidecategories = Object)
class UMetaHumanFaceContourTrackerAssetFactoryNew
	: public UFactory
{
	GENERATED_BODY()

public:
	UMetaHumanFaceContourTrackerAssetFactoryNew();

	//~Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetToolTip() const override;
	//~End UFactory interface
};
