// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageExpressions/DMMSEAbs.h"
#include "Materials/MaterialExpressionAbs.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMSEAbs)

#define LOCTEXT_NAMESPACE "DMMaterialStageExpressionAbs"

UDMMaterialStageExpressionAbs::UDMMaterialStageExpressionAbs()
	: UDMMaterialStageExpressionMathBase(
		LOCTEXT("Abs", "Abs"),
		UMaterialExpressionAbs::StaticClass()
	)
{
	SetupInputs(1);
}

#undef LOCTEXT_NAMESPACE
