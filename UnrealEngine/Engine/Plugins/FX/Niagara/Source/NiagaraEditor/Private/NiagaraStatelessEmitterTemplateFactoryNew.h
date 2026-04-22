// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "NiagaraStatelessEmitterTemplateFactoryNew.generated.h"

UCLASS(hidecategories=Object, MinimalAPI)
class UNiagaraStatelessEmitterTemplateFactoryNew : public UFactory
{
	GENERATED_UCLASS_BODY()

	virtual bool ShouldShowInNewMenu() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
