// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageExpressions/DMMSEMax.h"
#include "Materials/MaterialExpressionMax.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMSEMax)

#define LOCTEXT_NAMESPACE "DMMaterialStageExpressionMax"

UDMMaterialStageExpressionMax::UDMMaterialStageExpressionMax()
	: UDMMaterialStageExpressionMathBase(
		LOCTEXT("Max", "Max"),
		UMaterialExpressionMax::StaticClass()
	)
{
	SetupInputs(2);

	bAllowSingleFloatMatch = false;
}

#undef LOCTEXT_NAMESPACE
