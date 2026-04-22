// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraNotifyOnChanged.generated.h"

UCLASS()
class UNiagaraNotifyOnChanged : public UObject
{
	GENERATED_BODY()

#if WITH_EDITOR

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);

public:
	NIAGARACORE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
	NIAGARACORE_API FOnChanged& OnChanged();

private:
	FOnChanged OnChangedDelegate;

#endif // WITH_EDITOR

};
