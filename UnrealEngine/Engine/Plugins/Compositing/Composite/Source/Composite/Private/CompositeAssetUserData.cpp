// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeAssetUserData.h"

#include "Components/PrimitiveComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CompositeAssetUserData)

void UCompositeAssetUserData::PostEditChangeOwner(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (UPrimitiveComponent* Outer = GetTypedOuter<UPrimitiveComponent>())
	{
		OnPostEditChangeOwner.ExecuteIfBound(*Outer);
	}
}
