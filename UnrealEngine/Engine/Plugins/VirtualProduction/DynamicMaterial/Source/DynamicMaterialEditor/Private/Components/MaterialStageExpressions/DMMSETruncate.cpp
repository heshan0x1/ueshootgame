// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageExpressions/DMMSETruncate.h"
#include "Materials/MaterialExpressionTruncate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMSETruncate)

#define LOCTEXT_NAMESPACE "DMMaterialStageExpressionTruncate"

UDMMaterialStageExpressionTruncate::UDMMaterialStageExpressionTruncate()
	: UDMMaterialStageExpressionMathBase(
		LOCTEXT("Truncate", "Truncate"),
		UDMMaterialStageExpression::FindClass(TEXT("MaterialExpressionTruncate"))
	)
{
	SetupInputs(1);
}

#undef LOCTEXT_NAMESPACE
