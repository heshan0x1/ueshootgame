// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsAssetDataflowState.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

void FPhysicsAssetDataflowState::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(TargetSkeleton);
	Collector.AddReferencedObject(TargetMesh);
	AddArrayReferences(Collector, Bodies, Constraints);
}

void FPhysicsAssetDataflowState::DebugLog() const
{
	for(const USkeletalBodySetup* Setup : Bodies)
	{
		UE_LOG(LogTemp, Warning, TEXT("Body: %s"), *Setup->GetName());
	}

	for(const UPhysicsConstraintTemplate* Constraint : Constraints)
	{
		UE_LOG(LogTemp, Warning, TEXT("Constraint: %s"), *Constraint->GetName());
	}
}

bool FPhysicsAssetDataflowState::HasData() const
{
	return TargetSkeleton && TargetMesh;
}
