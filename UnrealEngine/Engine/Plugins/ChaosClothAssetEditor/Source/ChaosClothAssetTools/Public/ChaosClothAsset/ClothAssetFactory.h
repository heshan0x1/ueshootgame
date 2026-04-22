// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"

#include "ClothAssetFactory.generated.h"

#define UE_API CHAOSCLOTHASSETTOOLS_API

/**
 * Having a cloth factory allows the cloth asset to be created from the Editor's menus.
 */
UCLASS(MinimalAPI, Experimental)
class UChaosClothAssetFactory : public UFactory
{
	GENERATED_BODY()
public:
	UE_API UChaosClothAssetFactory(const FObjectInitializer& ObjectInitializer);

	/** UFactory Interface */
	virtual bool CanCreateNew() const override { return true; }
	virtual bool FactoryCanImport(const FString& Filename) override { return false; }
	virtual bool ShouldShowInNewMenu() const override { return true; }
	UE_API virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	UE_API virtual FString GetDefaultNewAssetName() const override;
	/** End UFactory Interface */
};

#undef UE_API
