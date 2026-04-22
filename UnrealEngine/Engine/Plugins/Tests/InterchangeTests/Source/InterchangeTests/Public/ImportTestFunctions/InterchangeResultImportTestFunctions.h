// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImportTestFunctionsBase.h"
#include "UObject/Package.h"
#include "InterchangeResultImportTestFunctions.generated.h"

#define UE_API INTERCHANGETESTS_API

class UInterchangeResult;
class UInterchangeResultsContainer;

struct FInterchangeTestFunctionResult;


UCLASS(MinimalAPI)
class UInterchangeResultImportTestFunctions : public UImportTestFunctionsBase
{
	GENERATED_BODY()

public:

	// UImportTestFunctionsBase interface
	UE_API virtual UClass* GetAssociatedAssetType() const override;

	/** Check whether the specified InterchangeResult was emitted during import */
	UFUNCTION(Exec)
	static UE_API FInterchangeTestFunctionResult CheckIfErrorOrWarningWasGenerated(UInterchangeResultsContainer* ResultsContainer, TSubclassOf<UInterchangeResult> ErrorOrWarningClass);
};

#undef UE_API
