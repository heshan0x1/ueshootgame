// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataChannelAccessContext.h"

#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataChannelAccessContext)

FNDCSpawnedSystemRef::FNDCSpawnedSystemRef(UNiagaraComponent* Component)
	: SpawnedSystem(Component)
{
}

FVector FNDCAccessContext::GetLocation()const
{
	return OwningComponent && !bOverrideLocation ? OwningComponent->GetComponentLocation() : Location;
}

FVector FNiagaraDataChannelSearchParameters::GetLocation()const
{
	return OwningComponent && !bOverrideLocation ? OwningComponent->GetComponentLocation() : Location;
}


