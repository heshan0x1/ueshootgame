// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "TaggedAssetBrowserConfigurationFactory.generated.h"

UCLASS()
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserConfigurationFactory : public UFactory
{
	GENERATED_BODY()

	UTaggedAssetBrowserConfigurationFactory();
	
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
