// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDBaseSceneObject.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosVDBaseSceneObject)

void FChaosVDBaseSceneObject::SetParentParentActor(AActor* NewParent)
{
	ParentActor = NewParent;
}
