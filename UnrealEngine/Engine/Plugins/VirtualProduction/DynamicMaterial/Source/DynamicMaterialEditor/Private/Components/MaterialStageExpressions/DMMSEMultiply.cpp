// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageExpressions/DMMSEMultiply.h"
#include "Materials/MaterialExpressionMultiply.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMSEMultiply)

#define LOCTEXT_NAMESPACE "DMMaterialStageExpressionMultiply"

UDMMaterialStageExpressionMultiply::UDMMaterialStageExpressionMultiply()
	: UDMMaterialStageExpressionMathBase(
		LOCTEXT("Multiply", "Multiply"),
		UMaterialExpressionMultiply::StaticClass()
	)
{
	SetupInputs(2);
}

#undef LOCTEXT_NAMESPACE
