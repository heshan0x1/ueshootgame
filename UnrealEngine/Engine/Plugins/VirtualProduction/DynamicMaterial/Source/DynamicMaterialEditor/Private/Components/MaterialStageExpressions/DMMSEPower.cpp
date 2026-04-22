// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageExpressions/DMMSEPower.h"
#include "Materials/MaterialExpressionPower.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMSEPower)

#define LOCTEXT_NAMESPACE "DMMaterialStageExpressionPower"

UDMMaterialStageExpressionPower::UDMMaterialStageExpressionPower()
	: UDMMaterialStageExpressionMathBase(
		LOCTEXT("Power", "Power"),
		UMaterialExpressionPower::StaticClass()
	)
{
	SetupInputs(2);

	bSingleChannelOnly = true;

	InputConnectors[0].Name = LOCTEXT("Mantissa", "Mantissa");
	InputConnectors[1].Name = LOCTEXT("Exponent", "Exponent");
}

#undef LOCTEXT_NAMESPACE
