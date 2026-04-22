// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/SimCallbackObject.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "Chaos/Particle/ObjectState.h"
#include "Chaos/Framework/HashMappedArray.h"
#include "SpatialReadinessVolume.h"
#include "PhysicsSolver.h"

class FPhysScene_Chaos;
namespace Chaos
{
	class FSingleParticlePhysicsProxy;
	enum class EObjectStateType: int8;
}

struct FUnreadyVolumeData_GT
{
	FUnreadyVolumeData_GT(Chaos::FSingleParticlePhysicsProxy* InProxy, const FBox& InBounds, const FString& InDescription)
		: Proxy(InProxy)
		, Bounds(InBounds)
#if WITH_SPATIAL_READINESS_DESCRIPTIONS
		, Description(InDescription)
#endif
	{ }
	Chaos::FSingleParticlePhysicsProxy* Proxy;
	FBox Bounds;
#if WITH_SPATIAL_READINESS_DESCRIPTIONS
	FString Description;
#endif
};

struct FSpatialReadinessSimCallbackInput : public Chaos::FSimCallbackInput
{
	TSet<Chaos::FSingleParticlePhysicsProxy*> UnreadyVolumesToAdd;
	TSet<Chaos::FSingleParticlePhysicsProxy*> UnreadyVolumesToRemove;

	void Reset()
	{
		UnreadyVolumesToAdd.Reset();
		UnreadyVolumesToRemove.Reset();
	}
};

struct FSpatialReadinessSimCallback
	: public Chaos::TSimCallbackObject<
		FSpatialReadinessSimCallbackInput,
		Chaos::FSimCallbackNoOutput,
		Chaos::ESimCallbackOptions::Presimulate |
		Chaos::ESimCallbackOptions::ParticleRegister |
		Chaos::ESimCallbackOptions::MidPhaseModification |
		Chaos::ESimCallbackOptions::PreIntegrate |
		Chaos::ESimCallbackOptions::PostIntegrate |
		Chaos::ESimCallbackOptions::PreSolve |
		Chaos::ESimCallbackOptions::PostSolve>
{
	using This = FSpatialReadinessSimCallback;

public:
	FSpatialReadinessSimCallback(FPhysScene_Chaos& InPhysicsScene);

	// Game thread functions for adding and removing unready volumes
	int32 AddUnreadyVolume_GT(const FBox& Bounds, const FString& Description);
	void RemoveUnreadyVolume_GT(int32 UnreadyVolumeIndex);

	// Game thread function for querying for unready volumes
	//
	// If bAllUnreadyVolumes is true, then a multi-query will be used and
	// OutVolumeIndices will be populated with every index that encroaches.
	//
	// For performance, we only return the index of the first unread volume
	// that we find.
	bool QueryReadiness_GT(const FBox& Bounds, TArray<int32>& OutVolumeIndices, bool bAllUnreadyVolumes=false) const;

	// Given a volume index, get it's description
	const FUnreadyVolumeData_GT* GetVolumeData_GT(int32 VolumeIndex) const;

	// Iterate over each unready volume and get 
	void ForEachVolumeData_GT(const TFunction<void(const FUnreadyVolumeData_GT&)>& Func);

	// Get the number of unready volumes
	int32 GetNumUnreadyVolumes_GT() const;

	// Perform an operation on each of the currently frozen particles - only valid on the physics thread
	void ForEachUnreadyRigidParticle_PT(const TFunction<bool(Chaos::FPBDRigidParticleHandle*)>& Lambda) const;

	// Get the total number of currently frozen particles - only valid on the physics thread
	int32 GetNumUnreadyRigidParticles_PT() const;

	/* begin: TSimCallbackObject */
protected:
	virtual void OnPreSimulate_Internal() override;
	virtual void OnParticlesRegistered_Internal(TArray<Chaos::FSingleParticlePhysicsProxy*>& RegisteredProxies) override;
	virtual void OnMidPhaseModification_Internal(Chaos::FMidPhaseModifierAccessor& Accessor) override;
	virtual void OnPreIntegrate_Internal() override;
	virtual void OnPostIntegrate_Internal() override;
	virtual void OnPreSolve_Internal() override;
	virtual void OnPostSolve_Internal() override;
	/* end: TSimCallbackObject */

	// Helpers
	Chaos::FPBDRigidsEvolution* GetEvolution();

	// Functions for freezing and unfreezing all particles in the
	// UnreadyRigidParticles_PT set.
	void FreezeParticles_PT();
	void UnFreezeParticles_PT();

	// Physics thread function for querying for unready volumes
	// TODO: Make const
	bool QueryReadiness_PT(const Chaos::FAABB3& Bounds, TArray<const Chaos::FSingleParticlePhysicsProxy*>& OutVolumeProxies);

	// Keep a ref to the phys scene so we can add and remove particles
	FPhysScene_Chaos& PhysicsScene;

	// List of unready volume physics proxies. We directly use single particle
	// physics proxy rather than something more generic because we know that
	// we are only going to create static single particles for these volumes. 
	struct FHashMapTraits
	{
		static uint32 GetIDHash(const int32 Idx) { return MurmurFinalize32(Idx); }
		static uint32 GetElementID(const FUnreadyVolumeData_GT& Element);
	};
	Chaos::Private::THashMappedArray<int32, FUnreadyVolumeData_GT, FHashMapTraits> UnreadyVolumeData_GT;

	// List of particle handles which represent unready volumes
	TSet<Chaos::FSingleParticlePhysicsProxy*> UnreadyVolumeParticles_PT;

	// Lists of particle handles which represent particles that interacted with unready volumes
	//
	// This got a little bit confusing due to some quirks of the particle
	// creation pipeline.
	//
	// A particle is added to the NewRegistered list if it's initially
	// overlapping an unready volume. Ideally it would then get picked
	// up in the unready list maintained by the mid-phase modifier.
	// However, it isn't inserted into the acceleration structure
	// immediately, so it is missed.
	//
	// Instead, at the beginning of the next frame, NewRegistered contents
	// are moved into OldRegistered, and NewRegistered is emptied.
	//
	// This ensures that particles which are spawned on top of unready
	// volumes will not be able to sneak in a integration step before
	// they have been inserted into the acceleration structure, where the
	// mid-phase modifier will be able to manage them.
	TSet<Chaos::FPBDRigidParticleHandle*> NewRegistered_UnreadyRigidParticles_PT;
	TSet<Chaos::FPBDRigidParticleHandle*> OldRegistered_UnreadyRigidParticles_PT;
	TSet<Chaos::FPBDRigidParticleHandle*> MidPhase_UnreadyRigidParticles_PT;

	// "Unready" particles are forced to be stationary in PreSimulate, and restored
	// to their previous state in PostIntegrate. The values needed for restoration
	// are stored and mapped back to particle id's in this hash-mapped array.
	TArray<TPair<Chaos::FGeometryParticleHandle*, Chaos::EObjectStateType>> ParticleDataCache_PT;
};
