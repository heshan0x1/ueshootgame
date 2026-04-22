// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigPhysicsSimulation.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsAdapters.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"
#include "Chaos/ParticleHandle.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

//======================================================================================================================
// Note that the simulation mutex will already be taken when we are called
void FRigPhysicsSimulation::UpdateWorldObjectsPrePhysics(const FRigPhysicsSolverSettings& SolverSettings)
{
	if (SolverSettings.WorldCollisionType == ERigPhysicsWorldCollisionType::None)
	{
		for (TPair<uint32, FWorldObject>& WorldObjectPair : *WorldObjects.Get())
		{
			FWorldObject& WorldObject = WorldObjectPair.Value;
			if (WorldObject.ActorHandle)
			{
				UE_LOG(LogRigPhysics, Warning,
					TEXT("Control rig %s - expiring object %s"),
					*OwnerName.ToString(), *WorldObject.ActorHandle->GetName().ToString());

				Simulation->DestroyActor(WorldObject.ActorHandle);
			}
		}
		WorldObjects->Reset();
	}
	else
	{
		TArray<uint32> EntriesToRemove;
		for (TPair<uint32, FWorldObject>& WorldObjectPair : *WorldObjects.Get())
		{
			FWorldObject& WorldObject = WorldObjectPair.Value;
			if (WorldObject.GetExpired(UpdateCounter, SolverSettings.WorldCollisionExpiryFrames))
			{
				if (WorldObject.ActorHandle)
				{
					UE_LOG(LogRigPhysics, Log,
						TEXT("Control rig %s - Expiring object %s"), 
						*OwnerName.ToString(), *WorldObject.ActorHandle->GetName().ToString());

					Simulation->DestroyActor(WorldObject.ActorHandle);
					WorldObject.ActorHandle = nullptr;
				}
				EntriesToRemove.Add(WorldObjectPair.Key);
			}
		}
		for (uint32 EntryToRemove : EntriesToRemove)
		{
			WorldObjects->Remove(EntryToRemove);
		}
	}

	for (TPair<uint32, FWorldObject>& WorldObjectPair : *WorldObjects.Get())
	{
		FWorldObject& WorldObject = WorldObjectPair.Value;
		FTransform SimSpaceTM = ConvertWorldTransformToSimSpace(SolverSettings, WorldObject.ComponentWorldTransform);
		if (WorldObject.LastSeenUpdateCounter == -1)
		{
			UE_LOG(LogRigPhysics, Log,
				TEXT("Control rig %s - Adding object %s"), *OwnerName.ToString(),
				*WorldObject.ActorHandle->GetName().ToString());

			WorldObject.ActorHandle->SetWorldTransform(SimSpaceTM);
			// If the object is found in the next overlap, then its update counter will be updated.
			// However, in case it's not, we set it here too to make sure that it expires. 
			WorldObject.LastSeenUpdateCounter = UpdateCounter;
		}
		else
		{
			WorldObject.ActorHandle->SetKinematicTarget(SimSpaceTM);
		}
	}

}

//======================================================================================================================
// Note that the simulation mutex will already be taken when we are called
void FRigPhysicsSimulation::UpdateWorldObjectsPostPhysics(
	const UWorld* World, const FRigPhysicsSolverSettings& SolverSettings, const AActor* OwningActorPtr)
{
	WorldOverlapBox = FBox();
	if (SolverSettings.WorldCollisionType == ERigPhysicsWorldCollisionType::None)
	{
		return;
	}

	// Get the volume over which to do overlaps
	const FTransform SpaceTransform = 
		ConvertSimSpaceTransformToComponentSpace(SolverSettings, FTransform()) * SimulationSpaceState.ComponentTM;
	for (TPair<FRigComponentKey, FRigBodyRecord>& BodyRecordPair : BodyRecords)
	{
		const FRigComponentKey& ComponentKey = BodyRecordPair.Key;
		const FRigBodyRecord& Record = BodyRecordPair.Value;

		if (const ImmediatePhysics::FActorHandle* ActorHandle = Record.ActorHandle)
		{
			if (!ActorHandle->GetIsKinematic())
			{
				const Chaos::FGeometryParticleHandle* GeometryParticleHandle = ActorHandle->GetParticle();
				const FTransform ParticleTransform = GeometryParticleHandle->GetTransformXR() * SpaceTransform;

				const auto& AABB = GeometryParticleHandle->LocalBounds();
				for (int32 Index = 0 ; Index != 8 ; ++Index)
				{
					WorldOverlapBox += ParticleTransform.TransformPosition(AABB.GetVertex(Index));
				}
			}
		}
	}

	if (!WorldOverlapBox.IsValid)
	{
		return;
	}

	TWeakPtr<FCriticalSection> WeakSimulationMutex = SimulationMutex;

	TWeakPtr<TMap<uint32, FWorldObject>> WeakWorldObjects = WorldObjects;
	int64 UpdateCounterCopy = UpdateCounter;
	const AActor* OwningActorPtrCopy = OwningActorPtr;
	TSharedPtr<ImmediatePhysics::FSimulation> SimulationCopy = Simulation;

	const TWeakObjectPtr<const UWorld> WeakWorld = World;

	FVector NewHalfExtent = WorldOverlapBox.GetExtent() * SolverSettings.WorldCollisionBoundsExpansion;
	FVector OverlapAABoxCenter = WorldOverlapBox.GetCenter();
	WorldOverlapBox = FBox(OverlapAABoxCenter - NewHalfExtent, OverlapAABoxCenter + NewHalfExtent);
	FCollisionShape OverlapAABox = FCollisionShape::MakeBox(NewHalfExtent);

	FCollisionQueryParams QueryParams = FCollisionQueryParams(
		SCENE_QUERY_STAT(ControlRigPhysicsFindGeometry), /*bTraceComplex=*/false);
	QueryParams.MobilityType = EQueryMobilityType::Any;

	FCollisionObjectQueryParams ObjectQueryParams;
	switch (SolverSettings.WorldCollisionType)
	{
	case ERigPhysicsWorldCollisionType::Static:
		ObjectQueryParams = FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllStaticObjects); break;
	case ERigPhysicsWorldCollisionType::Dynamic:
		ObjectQueryParams = FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllDynamicObjects); break;
	case ERigPhysicsWorldCollisionType::All:
		ObjectQueryParams = FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects); break;
	default:
		break;
	}

	// The actual overlap needs to be on the game thread, so we will run it in an async task.
	// However, note that between now and then, we/our simulation etc might get destroyed so we must
	// protect against that. Even the mutex guarding against access to the simulation may be released.
	if (ObjectQueryParams.ObjectTypesToQuery)
	{
		AsyncTask(ENamedThreads::GameThread,
			[WeakSimulationMutex, WeakWorld, WeakWorldObjects, OverlapAABox, OverlapAABoxCenter, QueryParams, 
			ObjectQueryParams, UpdateCounterCopy, OwningActorPtrCopy, SimulationCopy]()
			{
				if (TSharedPtr<FCriticalSection> SimulationMutex = WeakSimulationMutex.Pin())
				{
					FScopeLock Lock(SimulationMutex.Get());

					if (TStrongObjectPtr<const UWorld> World = WeakWorld.Pin())
					{
						if (TSharedPtr<TMap<uint32, FWorldObject>> WorldObjects = WeakWorldObjects.Pin())
						{
							TArray<FOverlapResult> Overlaps;
							World->OverlapMultiByObjectType(
								Overlaps, OverlapAABoxCenter, FQuat::Identity, ObjectQueryParams,
								OverlapAABox, QueryParams);

							for (const FOverlapResult& Overlap : Overlaps)
							{
								if (UPrimitiveComponent* OverlapComp = Overlap.GetComponent())
								{
									uint32 Key = OverlapComp->GetUniqueID();
									FWorldObject* WorldObject = WorldObjects->Find(Key);
									if (!WorldObject)
									{
										// New object - add it to the sim
										if (OverlapComp->GetOwner() && OverlapComp->GetOwner() != OwningActorPtrCopy)
										{
											WorldObject = &WorldObjects->Add(Key, FWorldObject());

											WorldObject->WorldPrimitiveComponent = OverlapComp;

											// Note that we need to create the simulation actor here, even
											// though we're in the game thread, so we can copy data out of the
											// object. This should be safe because we only run this task after
											// we've finished using the simulation in the worker thread.

											WorldObject->ActorHandle = SimulationCopy->CreateActor(
												ImmediatePhysics::MakeKinematicActorSetup(
													&OverlapComp->BodyInstance, OverlapComp->GetComponentTransform()));

#if WITH_EDITOR
											WorldObject->ActorHandle->SetName(*OverlapComp->GetOwner()->GetActorLabel());
#endif
											SimulationCopy->AddToCollidingPairs(WorldObject->ActorHandle);

											WorldObject->ComponentWorldTransform = 
												OverlapComp->BodyInstance.GetUnrealWorldTransform();

											// Flag that the simulation TM needs to be set, rather
											// than using a kinematic target.
											WorldObject->LastSeenUpdateCounter = -1;
										}
									}
									else
									{
										if (TStrongObjectPtr<UPrimitiveComponent> WPC = 
											WorldObject->WorldPrimitiveComponent.Pin())
										{
											WorldObject->ComponentWorldTransform = 
												WPC->BodyInstance.GetUnrealWorldTransform();
											WorldObject->LastSeenUpdateCounter = UpdateCounterCopy;
										}
									}
								}
							}
						}
					}
				}
			});
	}
}

