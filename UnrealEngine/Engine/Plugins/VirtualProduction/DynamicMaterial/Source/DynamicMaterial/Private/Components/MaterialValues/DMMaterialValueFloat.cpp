// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialValues/DMMaterialValueFloat.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DMMaterialValueFloat)

UDMMaterialValueFloat::UDMMaterialValueFloat()
	: UDMMaterialValueFloat(EDMValueType::VT_None)
{
}

UDMMaterialValueFloat::UDMMaterialValueFloat(EDMValueType InValueType)
	: UDMMaterialValue(InValueType)
	, ValueRange(FFloatInterval(0, 0))
{
}
