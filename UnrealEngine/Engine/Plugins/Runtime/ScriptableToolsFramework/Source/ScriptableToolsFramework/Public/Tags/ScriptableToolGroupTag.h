// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"

#include "ScriptableToolGroupTag.generated.h"

UCLASS(MinimalAPI, Abstract, Blueprintable, Const)
class UScriptableToolGroupTag
	: public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "General")
	FString Name;
};
