// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "HarmonixMetasoundMusicFactory.generated.h"

UCLASS()
class UHarmonixMetasoundMusicFactory : public UFactory
{
	GENERATED_BODY()
public:
	UHarmonixMetasoundMusicFactory();
	//~ BEGIN UFactory interface
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ END UFactory interface
};

