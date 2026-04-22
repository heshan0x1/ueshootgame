// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Engine/Blueprint.h"
#include "Factories/Factory.h"
#include "ControlRigBlueprintLegacy.h"
#include "ControlRig.h"
#include "ControlRigBlueprintFactory.generated.h"

#define UE_API CONTROLRIGEDITOR_API

UCLASS(MinimalAPI, HideCategories=Object)
class UControlRigBlueprintFactory : public UFactory
{
	GENERATED_BODY()

public:
	UE_API UControlRigBlueprintFactory();

	// The parent class of the created blueprint
	UPROPERTY(EditAnywhere, Category="Control Rig Factory", meta=(AllowAbstract = ""))
	TSubclassOf<UControlRig> ParentClass;

	// UFactory Interface
	UE_API virtual bool ConfigureProperties() override;
	UE_API virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	UE_API virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	/**
	 * Create a new control rig asset within the contents space of the project.
	 * @param InDesiredPackagePath The package path to use for the control rig asset
	 * @param bModularRig If true the rig will be created as a modular rig
	 */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	static UE_API UControlRigBlueprint* CreateNewControlRigAsset(const FString& InDesiredPackagePath, const bool bModularRig = false);

	/**
	 * Create a new control rig asset within the contents space of the project
	 * based on a skeletal mesh or skeleton object.
	 * @param InSelectedObject The SkeletalMesh / Skeleton object to base the control rig asset on
	 * @param bModularRig If true the rig will be created as a modular rig
	 */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	static UE_API UControlRigBlueprint* CreateControlRigFromSkeletalMeshOrSkeleton(UObject* InSelectedObject, const bool bModularRig = false);
};

#undef UE_API
