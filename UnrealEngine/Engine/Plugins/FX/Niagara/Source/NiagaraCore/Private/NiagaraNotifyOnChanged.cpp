// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNotifyOnChanged.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraNotifyOnChanged)

#if WITH_EDITOR

void UNiagaraNotifyOnChanged::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnChangedDelegate.Broadcast();
}

UNiagaraNotifyOnChanged::FOnChanged& UNiagaraNotifyOnChanged::OnChanged()
{
	return OnChangedDelegate;
}

#endif // WITH_EDITOR
