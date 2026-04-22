// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controller/RCStatelessEventController.h"

#include "UObject/UnrealType.h"
#include "RCVirtualProperty.h"

bool FRCStatelessEventController::IsStatelessEventController(const URCVirtualPropertyBase* InController)
{
	if (InController->GetValueType() != EPropertyBagPropertyType::Struct)
	{
		return false;
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(InController->GetProperty());

	if (!StructProperty)
	{
		return false;
	}

	return StructProperty->Struct == FRCStatelessEventController::StaticStruct();
}
