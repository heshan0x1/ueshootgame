// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/OverlapResult.h"

#include "Components/PrimitiveComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OverlapResult)

int32 FOverlapResult::GetItemIndexInternal() const
{
	return ItemIndex;
}

int32 FOverlapResult::GetItemIndex() const
{
	if (const UPrimitiveComponent* OwnerComponent = GetComponent())
	{
		return OwnerComponent->bMultiBodyOverlap ? ItemIndex : INDEX_NONE;
	}
	return INDEX_NONE;
}

void FOverlapResult::SetItemIndex(const int32 InItemIndex)
{
	ItemIndex = InItemIndex;
}
