// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaStreamActor.h"

#include "MediaStreamComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MediaStreamActor)

AMediaStreamActor::AMediaStreamActor()
{
	MediaStreamComponent = CreateDefaultSubobject<UMediaStreamComponent>(TEXT("MediaStreamComponent"));
	SetRootComponent(MediaStreamComponent);
}
