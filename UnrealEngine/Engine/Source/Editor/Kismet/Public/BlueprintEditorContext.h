// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BlueprintEditorContext.generated.h"

#define UE_API KISMET_API

class UBlueprint;
class FBlueprintEditor;

UCLASS(MinimalAPI)
class UBlueprintEditorToolMenuContext : public UObject
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category="Tool Menus")
	UE_API UBlueprint* GetBlueprintObj() const;

	TWeakPtr<FBlueprintEditor> BlueprintEditor;
};

#undef UE_API
