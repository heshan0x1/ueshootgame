// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "MetaHumanIdentityFactoryNew.generated.h"

#define UE_API METAHUMANIDENTITYEDITOR_API

UCLASS(MinimalAPI, hidecategories = Object)
class UMetaHumanIdentityFactoryNew
	: public UFactory
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanIdentityFactoryNew();
	
	//~Begin UFactory interface
	UE_API virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn) override;
	UE_API virtual FText GetToolTip() const override;
	//~End UFactory interface
};

#undef UE_API
