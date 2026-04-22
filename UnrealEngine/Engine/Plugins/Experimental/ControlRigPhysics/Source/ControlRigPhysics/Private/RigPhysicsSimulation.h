// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigPhysicsData.h"

#include "PhysicsControlData.h"
#include "PhysicsControlPoseData.h"

#include "Rigs/RigHierarchyComponents.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "HAL/CriticalSection.h"

#include "RigPhysicsSimulation.generated.h"

class UControlRig;

struct FRigVMDrawInterface;
struct FRigVMExecuteContext;
struct FRigPhysicsSolverComponent;

struct FRigPhysicsCollision;
struct FRigPhysicsDynamics;
struct FRigPhysicsBodyComponent;
struct FRigPhysicsControlComponent;
struct FRigPhysicsJointComponent;

#if UE_BUILD_SHIPPING || UE_BUILD_TEST
DECLARE_LOG_CATEGORY_EXTERN(LogRigPhysics, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogRigPhysics, Log, All);
#endif

//======================================================================================================================
// For internal use only
//======================================================================================================================
struct FRigPhysicsIgnorePair
{
	FRigPhysicsIgnorePair(const FRigComponentKey& InA, const FRigComponentKey& InB)
		: A(InA), B(InB) {}
	FRigComponentKey A;
	FRigComponentKey B;

	bool operator==(const FRigPhysicsIgnorePair& Other) const
	{
		return (A == Other.A && B == Other.B) || (A == Other.B && B == Other.A);
	}
};

FORCEINLINE uint32 GetTypeHash(const FRigPhysicsIgnorePair& Pair)
{
	return HashCombine(GetTypeHash(FMath::Min(Pair.A, Pair.B)), GetTypeHash(FMath::Max(Pair.A, Pair.B)));
}
typedef TSet<FRigPhysicsIgnorePair> FRigPhysicsIgnorePairs;

//======================================================================================================================
// For internal use only, to keep track of objects we have created
//======================================================================================================================
USTRUCT()
struct FRigBodyRecord
{
	GENERATED_BODY()

	// Things that are set during instantiation
	ImmediatePhysics::FActorHandle* ActorHandle = nullptr;

	// Cache the element key for where we will write the simulation result
	FRigElementKey TargetElementKey;

	// The final/simulated TM is stored before writing it into the output, so we can avoid
	// corrupting the output if anything is bad and we need to reset.
	FTransform FinalComponentSpaceTM;

	// These source (i.e. bone/element) transforms are updated for all records in UpdatePrePhysics.
	// The times/validity are determined by the CurrentDeltaTime, PrevDeltaTime and update counters
	// in the simulation itself.
	UE::PhysicsControl::FPosQuat SourceComponentSpaceTM;
	FVector                      SourceComponentSpaceVelocity = FVector::ZeroVector;
	FVector                      SourceComponentSpaceAngularVelocity = FVector::ZeroVector;

	UE::PhysicsControl::FPosQuat PrevSourceComponentSpaceTM;
	FVector                      PrevSourceComponentSpaceVelocity = FVector::ZeroVector;
	FVector                      PrevSourceComponentSpaceAngularVelocity = FVector::ZeroVector;
};

//======================================================================================================================
// For internal use only, to keep track of joints we have created
//======================================================================================================================
USTRUCT()
struct FRigJointRecord
{
	GENERATED_BODY()

	// Things that are set during instantiation
	ImmediatePhysics::FJointHandle* JointHandle = nullptr;

	// These keys are filled in when the record is created, even if the original key is set to pick
	// up the components automatically.
	FRigComponentKey                ParentBodyComponentKey;
	FRigComponentKey                ChildBodyComponentKey;

	// Things that are updated as the simulation progresses

	// The drive works with velocities so we store the previous target transform, and when it was stored.
	UE::PhysicsControl::FPosQuat    PreviousDriveTargetTM;
	// This is stored from the main solver update counter, marking when the previous drive TM was valid.
	int64                           PreviousDriveTargetUpdateCounter = -999;
};

//======================================================================================================================
// For internal use only, to keep track of controls we have created
//======================================================================================================================
USTRUCT()
struct FRigControlRecord
{
	GENERATED_BODY()

	// Things that are set during instantiation
	ImmediatePhysics::FJointHandle* JointHandle = nullptr;

	// These keys are filled in when the record is created, even if the original key is set to pick
	// up the components automatically.
	FRigComponentKey                ParentBodyComponentKey;
	FRigComponentKey                ChildBodyComponentKey;

	// Things that are updated as the simulation progresses

	// The control works with velocities so we store the previous target transform, and when it was stored.
	FTransform                      PreviousTargetTM;
	// This is stored from the main solver update counter, marking when the previous target TM was valid.
	int64                           PreviousTargetUpdateCounter = -999;
};


//======================================================================================================================
// This represents the low level simulation, plus all the objects and controls we make to go in it
//======================================================================================================================
USTRUCT()
struct FRigPhysicsSimulation : public FRigPhysicsSimulationBase
{
public:
	GENERATED_BODY()

	FRigPhysicsSimulation(const FName InOwnerName = FName());

	// This will initialise/create the simulation and then create everything we need in it. 
	void Instantiate(
		const FRigVMExecuteContext&       ExecuteContext, 
		const URigHierarchy&              Hierarchy, 
		const FRigPhysicsSolverComponent* SolverComponent);

	// Integrates the simulation forwards.
	// If DeltaTimeOverride is +ve, then that value is used.
	// If it is zero, then delta time is taken from the execute context
	// If it is negative, then the simulation isn't stepped.
	void StepSimulation(
		const UWorld*                World,
		const AActor*                OwningActorPtr,
		const FRigVMExecuteContext&  ExecuteContext, 
		URigHierarchy&               Hierarchy,
		FRigPhysicsSolverComponent*  SolverComponent,
		const float                  DeltaTimeOverride, 
		const float                  SimulationSpaceDeltaTimeOverride,
		const float                  Alpha);

	// Draws shapes etc AND (potentially) enables the low-level Chaos debug draw
	void Draw(
		FRigVMDrawInterface*                    DI,
		const FRigPhysicsSolverSettings&        SolverSettings,
		const FRigPhysicsVisualizationSettings& VisualizationSettings,
		const UWorld*                           DebugWorld) const;

	// Represents the properties of the simulation space - calculated near the beginning of the update.
	// Note that all these are specified in the simulation space itself
	struct FSimulationSpaceData
	{
		FVector LinearVelocity;
		// Angular velocity is in rad/s
		FVector AngularVelocity;
		FVector LinearAcceleration;
		// Angular acceleration is in rad/s/s
		FVector AngularAcceleration;
		FVector Gravity;
	};

	// Returns the simulation space data, as calculated at the start of the last step
	const FSimulationSpaceData& GetSimulationSpaceData() const { return SimulationSpaceData; }

private:
	// Used by the world-space to simulation-space motion transfer system in Component- or
	// Bone-Space sims, and preserved between updates.
	struct FSimulationSpaceState
	{
		FTransform ComponentTM;
		FTransform BoneRelComponentTM;

		// The world transform of the simulation space
		FTransform SimulationSpaceTM;
		FTransform PrevSimulationSpaceTM;
		FTransform PrevPrevSimulationSpaceTM;
		// The time between SimulationSpaceTM and PrevSimulationSpaceTM
		float      Dt = 1.0f; 
		// The time between PrevSimulationSpaceTM and PrevPrevSimulationSpaceTM
		float      PrevDt = 1.0f;
	};

	// Creates the low level simulation
	void InitialiseSimulation(const FRigPhysicsSolverComponent* SolverComponent);

	// This collects all the bodies associated with the solver and makes records for them
	void InitialiseBodyRecords(const URigHierarchy& Hierarchy, const FRigPhysicsSolverComponent* SolverComponent);

	// This collects all the joints associated with the solver and makes records for them
	void InitialiseJointRecords(const URigHierarchy& Hierarchy, const FRigPhysicsSolverComponent* SolverComponent);

	// This collects all the bodies associated with the solver and makes records for them
	void InitialiseControlRecords(const URigHierarchy& Hierarchy, const FRigPhysicsSolverComponent* SolverComponent);

	// Creates low level bodies
	void InstantiatePhysicsBodies(
		const URigHierarchy&              Hierarchy,
		const FRigPhysicsSolverComponent* SolverComponent,
		FRigPhysicsIgnorePairs&           IgnorePairs);

	// Creates low level physics joints 
	void InstantiatePhysicsJoints(
		const URigHierarchy&              Hierarchy,
		const FRigPhysicsSolverComponent* SolverComponent,
		FRigPhysicsIgnorePairs&           IgnorePairs);

	// Creates low level controls
	void InstantiateControls(
		const URigHierarchy&              Hierarchy,
		const FRigPhysicsSolverComponent* SolverComponent,
		FRigPhysicsIgnorePairs&           IgnorePairs);

	// Creates collision shapes associated with the solver. Also applies IgnorePairs
	void InstantiateSolverCollision(
		const FRigPhysicsSolverComponent* SolverComponent,
		FRigPhysicsIgnorePairs&           IgnorePairs);

	// Returns true if the component is physics, and its solver matches the solver component
	// (directly, or automatically)
	bool ShouldComponentBeInSimulation(
		const URigHierarchy& Hierarchy, FRigComponentKey SolverComponentKey, FRigComponentKey ComponentKey) const;

	// Creates an actor with collision. This will by dynamic if Dynamics is valid, or otherwise kinematic
	ImmediatePhysics::FActorHandle* CreateBody(
		const FName                        BodyName,
		const FRigPhysicsCollision&        Collision,
		const FRigPhysicsDynamics*         Dynamics,
		const FPhysicsControlModifierData* BodyData,
		const FTransform&                  BodyRelSimSpaceTM) const;

	// This releases the actors etc in the simulation, and destroys the simulation, but it doesn't
	// change our records (apart from resetting pointers to actors etc).
	void DestroyPhysicsSimulation();

	void UpdatePrePhysics(
		const FRigVMExecuteContext&        ExecuteContext, 
		const URigHierarchy&               Hierarchy,
		FRigPhysicsSolverComponent*        SolverComponent, 
		const float                        DeltaTime);

	void UpdateBodyRecordPrePhysics(
		const URigHierarchy&               Hierarchy,
		const FRigPhysicsSolverComponent*  SolverComponent,
		const float                        DeltaTime,
		FRigBodyRecord&                    Record,
		const FRigPhysicsBodyComponent*    PhysicsComponent);

	void UpdateBodyPrePhysics(
		const FRigVMExecuteContext&        ExecuteContext, 
		const FRigPhysicsSolverComponent*  SolverComponent,
		const FRigBodyRecord&              Record, 
		const FRigPhysicsBodyComponent*    PhysicsComponent);

	void UpdateJointPrePhysics(
		const URigHierarchy&               Hierarchy, 
		FRigJointRecord&                   JointRecord,
		const FRigPhysicsJointComponent*   JointComponent, 
		const float                        DeltaTime);

	void UpdateControlPrePhysics(
		FRigControlRecord&                 ControlRecord,
		const FRigPhysicsControlComponent* ControlComponent,
		const FRigPhysicsSolverComponent*  SolverComponent,
		const URigHierarchy&               Hierarchy,
		const float                        DeltaTime);

	void UpdatePostPhysics(
		URigHierarchy&              Hierarchy,
		FRigPhysicsSolverComponent* SolverComponent, 
		const float                 Alpha, 
		const float                 DeltaTime,
		const bool                  bUsingFixedStep);

	// This will walk through the WorldObjects, which should now be updated, and update the simulation
	void UpdateWorldObjectsPrePhysics(const FRigPhysicsSolverSettings& SolverSettings);

	// This will trigger a game-thread update of WorldObjects, ready for the next update
	void UpdateWorldObjectsPostPhysics(
		const UWorld*                    World,
		const FRigPhysicsSolverSettings& SolverSetting, 
		const AActor*                    OwningActorPtr);

	void InitSimulationSpace(const FTransform& ComponentToWorld, const FTransform& BoneToComponent);

	void CheckForResetsPrePhysics(
		const URigHierarchy& Hierarchy, FRigPhysicsSolverComponent* SolverComponent, const float DeltaTime);

	// AbsoluteTime is used for calculating turbulence when applying the "wind"
	FSimulationSpaceData UpdateSimulationSpaceStateAndCalculateData(
		const FRigVMExecuteContext&         ExecuteContext,
		const URigHierarchy&                Hierarchy,
		const FRigPhysicsSolverComponent*   SolverComponent,
		const float                         Dt,
		const double                        AbsoluteTime);

	// Returns the simulation space transform, in world space
	FTransform GetSimulationSpaceTransform(const FRigPhysicsSolverSettings& SolverSettings) const;

	// Converts a transform from component space (e.g. coming from the owning control rig) into the
	// simulation space
	FTransform ConvertComponentSpaceTransformToSimSpace(
		const FRigPhysicsSolverSettings& SolverSettings, const FTransform& TM) const;

	FVector ConvertComponentSpaceVectorToSimSpace(
		const FRigPhysicsSolverSettings& SolverSettings, const FVector& V) const;

	// Converts a transform from the simulation space to component space (e.g. for writing back to
	// the owning control rig)
	FTransform ConvertSimSpaceTransformToComponentSpace(
		const FRigPhysicsSolverSettings& SolverSettings, const FTransform& TM) const;

	// Converts a vector specified in world space to the simulation space (e.g. converting gravity)
	FVector ConvertWorldVectorToSimSpaceNoScale(
		const FRigPhysicsSolverSettings& SolverSettings, const FVector& WorldVector) const;

	// Converts a transform specified in world space to the simulation space
	FTransform ConvertWorldTransformToSimSpace(
		const FRigPhysicsSolverSettings& SolverSettings, const FTransform& WorldTM) const;

	FTransform ConvertCollisionSpaceTransformToSimSpace(
		const FRigPhysicsSolverSettings& SolverSettings, const FTransform& TM) const;

	ImmediatePhysics::FJointHandle* CreateConstraint(
		ImmediatePhysics::FActorHandle* ChildActorHandle, ImmediatePhysics::FActorHandle* ParentActorHandle);

	// Gets the simulation actor handle for a component key. Note that the component key could be a
	// body or a solver component. Can return nullptr
	ImmediatePhysics::FActorHandle* GetActor(const FRigComponentKey& ComponentKey) const;

private:
	TObjectPtr<UControlRig> OwningControlRig;
	FRigComponentKey        PhysicsSolverComponentKey;

	// A name that identifies what owns us - just used for debugging/logging
	FName                   OwnerName;

	// All the bodies, but in no particular order
	TMap<FRigComponentKey, FRigBodyRecord> BodyRecords;

	// Ordering so that we can traverse from root to leaf bones
	TArray<FRigComponentKey> SortedBodyComponentKeys;

	// All the joints
	TMap<FRigComponentKey, FRigJointRecord> JointRecords;

	// All the controls
	TMap<FRigComponentKey, FRigControlRecord> ControlRecords;

	TSharedPtr<ImmediatePhysics::FSimulation> Simulation;

	// Used to store things the simulation collision shape. May be offset from the origin if
	// collision is in a different space to the simulation.
	ImmediatePhysics::FActorHandle* CollisionActorHandle = nullptr;

	// Used to make controls when they're not attached to another simulated body. Will always be at
	// the origin.
	ImmediatePhysics::FActorHandle* SimulationActorHandle = nullptr;

	Chaos::FPBDJointSolverSettings ChaosJointSolverSettings;

	FSimulationSpaceState SimulationSpaceState;

	// Retain the data - we don't actually need to but (a) it makes it available for debugging and
	// (b) it avoids passing it through the functions.
	FSimulationSpaceData SimulationSpaceData;

	// This is incremented at the end of each simulation step, used to identify when previously
	// calculated values are valid
	// Note that the universe will roll over before a uint64 does
	int64 UpdateCounter = 0;

	// The update counter when PrevSourceComponentSpaceTM etc were written. Check this before using them
	int64 PreviousUpdateCounter = -999;

	// This will be set to false after instantiation, which can happen manually during construction,
	// or be postponed until there is a simulation step.
	bool bNeedToInstantiate = true;

	// Record of a world object that we want to track, and "import" into our simulation for collision.
	struct FWorldObject
	{
		FWorldObject() : ActorHandle(nullptr), LastSeenUpdateCounter(0) {}
		FWorldObject(ImmediatePhysics::FActorHandle* InActorHandle, int64 InLastSeenUpdateCounter)
			: ActorHandle(InActorHandle)
			, LastSeenUpdateCounter(InLastSeenUpdateCounter) {
		}

		// The object we are tracking
		TWeakObjectPtr<UPrimitiveComponent> WorldPrimitiveComponent;

		// The actor in our simulation
		ImmediatePhysics::FActorHandle* ActorHandle;

		// The transform of the world object. This will be updated in the game-thread task.
		FTransform ComponentWorldTransform;

		// When the world object was last seen (i.e. captured by an overlap), used to detect when we
		// should expire/forget about it.
		int64 LastSeenUpdateCounter;

		bool GetExpired(int64 InUpdateCounter, int64 InRetainCount) const
		{
			if (LastSeenUpdateCounter != -1 && InUpdateCounter > LastSeenUpdateCounter + InRetainCount)
			{
				return true;
			}
			if (!WorldPrimitiveComponent.IsValid() || !ActorHandle)
			{
				return true;
			}
			return false;
		}
	};

	// The world objects we track for collision. Index uses GetUniqueID from the PrimitiveComponent.
	// Note that this is only guaranteed to be unique whilst the object exists, so it is possible
	// (but hopefully unlikely) that an object will be replaced and have the same ID. This will be
	// rectified on the next update, as the record's pointer to the primitive component will be invalid.
	TSharedPtr<TMap<uint32, FWorldObject>> WorldObjects;

	// The box used for overlap tests to find the world objects
	FBox WorldOverlapBox;

	// Guard against access to the low level simulation. Note that this access may come from an
	// async/spawned task, so the mutex itself needs to be shared, as we (as primary owners) may get
	// deleted whilst the spawned task is in flight.
	TSharedPtr<FCriticalSection> SimulationMutex;
};

