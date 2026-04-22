// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Class.h"
#include "BuildStorageToolParameters.generated.h"

USTRUCT()
struct FDocumentationLink
{
	GENERATED_BODY()

	UPROPERTY()
	FString Text;
	
	UPROPERTY()
	FString Tooltip;

	UPROPERTY()
	FString Link;
};

USTRUCT()
struct FGeneralParameters 
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDocumentationLink> HelpLinks;
};

struct FBuildStorageToolParameters
{
	FGeneralParameters GeneralParameters;
};
