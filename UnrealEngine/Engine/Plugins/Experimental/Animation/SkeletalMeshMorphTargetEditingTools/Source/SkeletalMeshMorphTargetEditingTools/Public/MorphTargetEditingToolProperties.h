// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "InteractiveTool.h"

#include "MorphTargetEditingToolProperties.generated.h"



UCLASS()
class UMorphTargetEditingToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "MorphTarget", meta = (GetOptions = GetMorphTargetNames, NoResetToDefault, DisplayName = "Name"))
	FName EditMorphTargetName;
};