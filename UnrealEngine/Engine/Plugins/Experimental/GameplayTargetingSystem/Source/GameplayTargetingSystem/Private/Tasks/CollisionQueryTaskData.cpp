// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/CollisionQueryTaskData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CollisionQueryTaskData)

DEFINE_TARGETING_DATA_STORE(FCollisionQueryTaskData)

void FCollisionQueryTaskData::AddStructReferencedObjects(class FReferenceCollector& Collector)
{
	for (TObjectPtr<AActor>& ReferencedActor : IgnoredActors)
	{
		Collector.AddReferencedObject(ReferencedActor);
	}
}
