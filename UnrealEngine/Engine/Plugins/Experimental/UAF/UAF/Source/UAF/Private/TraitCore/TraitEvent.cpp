// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitCore/TraitEvent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TraitEvent)

bool FAnimNextTraitEvent::DecrementLifetime(UE::UAF::FTraitEventList& OutputEventList)
{
	const bool bExpired = Lifetime.Decrement();
	if (bExpired)
	{
		OnExpired(OutputEventList);
	}

	return bExpired;
}
