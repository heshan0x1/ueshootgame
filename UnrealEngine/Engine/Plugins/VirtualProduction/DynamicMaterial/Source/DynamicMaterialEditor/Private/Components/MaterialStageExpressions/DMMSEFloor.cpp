// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageExpressions/DMMSEFloor.h"
#include "Materials/MaterialExpressionFloor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMSEFloor)

#define LOCTEXT_NAMESPACE "DMMaterialStageExpressionFloor"

UDMMaterialStageExpressionFloor::UDMMaterialStageExpressionFloor()
	: UDMMaterialStageExpressionMathBase(
		LOCTEXT("Floor", "Floor"),
		UMaterialExpressionFloor::StaticClass()
	)
{
	SetupInputs(1);
}

#undef LOCTEXT_NAMESPACE
