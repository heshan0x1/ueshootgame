// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeMeshActor.h"

#include "Components/CompositeMeshComponent.h"

ACompositeMeshActor::ACompositeMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CompositeMeshComponent = CreateDefaultSubobject<UCompositeMeshComponent>(TEXT("DefaultCompositeMeshComponent"));
	SetRootComponent(CompositeMeshComponent);
}

ACompositeMeshActor::~ACompositeMeshActor() = default;

