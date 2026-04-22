// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorReimportHandler.h"
#include "Factories/Factory.h"

#include "MetaHumanCameraCalibrationImporterFactory.generated.h"

#define UE_API METAHUMANCOREEDITOR_API

UCLASS(MinimalAPI)
class UMetaHumanCameraCalibrationImporterFactory
	: public UFactory
	, public FReimportHandler
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanCameraCalibrationImporterFactory(const FObjectInitializer& InObjectInitializer);

protected:

	//~ UFactory Interface
	UE_API virtual FText GetToolTip() const override;
	UE_API virtual bool FactoryCanImport(const FString& Filename) override;
	UE_API virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFileName, const TCHAR* InParams, FFeedbackContext* InWarn, bool& bOutOperationCanceled);

	//~ FReimportHandler Interface
	UE_API virtual bool CanReimport(UObject* InObj, TArray<FString>& OutFilenames) override;
	UE_API virtual void SetReimportPaths(UObject* InObj, const TArray<FString>& InNewReimportPaths) override;
	UE_API virtual EReimportResult::Type Reimport(UObject* InObj) override;

	UE_API virtual TObjectPtr<UObject>* GetFactoryObject() const override;
	mutable TObjectPtr<UObject> GCMark{ this };
};

#undef UE_API
