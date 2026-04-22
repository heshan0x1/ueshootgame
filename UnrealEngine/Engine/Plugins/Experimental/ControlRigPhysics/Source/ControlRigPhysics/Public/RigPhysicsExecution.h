// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigPhysicsBodyComponent.h"
#include "RigPhysicsSolverComponent.h"
#include "RigPhysicsSimulation.h"
#include "PhysicsControlData.h"

#include "Units/Execution/RigUnit_DynamicHierarchy.h"

#include "RigPhysicsExecution.generated.h"

#define UE_API CONTROLRIGPHYSICS_API

/**
 * Base struct for all other mutable physics nodes
 */
USTRUCT(meta = (Category = "RigPhysics", NodeColor = "1.0 0.6 0.3", Keywords = "Physics"))
struct FRigUnit_PhysicsBaseMutable: public FRigUnitMutable
{
	GENERATED_BODY()
};

/**
 * Base struct for all other non-mutable physics nodes
 */
USTRUCT(meta = (Category = "RigPhysics", NodeColor = "1.0 0.6 0.3", Keywords = "Physics"))
struct FRigUnit_PhysicsBase : public FRigUnit
{
	GENERATED_BODY()
};

/**
 * Adds a new physics solver as a component on the owner element.
 * Note: This node only runs as part of the construction event.
 */
USTRUCT(meta=(DisplayName="Spawn Physics Solver", Keywords="Add,Construction,Create,New,Simulation", Varying))
struct FRigUnit_AddPhysicsSolver : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_AddPhysicsSolver()
	{
		Owner.Type = ERigElementType::Bone;
		// Default the material here to have friction and restitution. Then the interactions are
		// easily adjusted on the dynamic bodies.
		SolverSettings.Collision.Material.Friction = 1.0f;
		SolverSettings.Collision.Material.Restitution = 1.0f;
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	/**
	 * The owner of the newly created component (must be set/valid)
	 */
	UPROPERTY(meta = (Input, BoneName))
	FRigElementKey Owner;

	UPROPERTY(meta = (Output))
	FRigComponentKey PhysicsSolverComponentKey;

	UPROPERTY(meta = (Input))
	FRigPhysicsSolverSettings SolverSettings;

	UPROPERTY(meta = (Input))
	FRigPhysicsSimulationSpaceSettings SimulationSpaceSettings;
};

/**
 * Instantiates all the objects in the physics world. Some properties can't be modified after this happens.
 * Note that it will happen automatically during the first simulation step if it hasn't been explicitly
 * requested. Explicit instantiation allows the timing to be controlled, as allocations etc may cause some
 * delays.
 */
USTRUCT(meta = (DisplayName = "Instantiate physics"))
struct FRigUnit_InstantiatePhysics : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_InstantiatePhysics()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	/*
	 * The solver to relate this new physics element to
	 */
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;
};


/**
 * Steps the specified physics solver
 */
USTRUCT(meta = (DisplayName = "Step Physics Solver", Keywords = "Simulate"))
struct FRigUnit_StepPhysicsSolver : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_StepPhysicsSolver()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// The physics solver
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;
	
	// If this is zero, then the execute context time will be used. If this is positive then it will
	// override the delta time. A negative value will prevent the solver from stepping, but there will
	// still be update costs associated with the node.
	UPROPERTY(meta = (Input))
	float DeltaTimeOverride = 0.0f;

	// If this is zero, then the simulation delta time will be used for evaluating movement of the
	// simulation space. If this is positive then it will override. This may be needed if the
	// component movement is being done in parallel, in which case you might need to pass in the
	// previous time delta here.
	UPROPERTY(meta = (Input, ClampMin = "0.0"))
	float SimulationSpaceDeltaTimeOverride = 0.0f;

	// How much of the simulation is combined with the input bone. This currently happens in
	// component space. Note that the simulation will continue to run, even if alpha = 0
	UPROPERTY(meta = (Input, ClampMin = "0.0", ClampMax = "1.0"))
	float Alpha = 1.0f;

	UPROPERTY(meta = (Input))
	FRigPhysicsVisualizationSettings VisualizationSettings;
};

// Sets all the simulation space settings on the solver
USTRUCT(meta = (DisplayName = "Set Physics Solver Simulation Space Settings"))
struct FRigUnit_SetPhysicsSolverSimulationSpaceSettings : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_SetPhysicsSolverSimulationSpaceSettings()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// The physics solver
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;

	// The new simulation space settings
	UPROPERTY(meta = (Input))
	FRigPhysicsSimulationSpaceSettings SimulationSpaceSettings;
};

// Sets the external velocity of the simulation - used for adding wind effects
USTRUCT(meta = (DisplayName = "Set Physics Solver External Velocity"))
struct FRigUnit_SetPhysicsSolverExternalVelocity : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_SetPhysicsSolverExternalVelocity()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// The physics solver
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;

	// Additional velocity that is added to the component velocity so the simulation acts as if the
	// actor is moving at speed, even when stationary. The vector is in world space. This could be
	// used for wind effects etc. Typical values are similar to the velocity of the object or
	// effect, and usually around or less than 1000 for characters/wind.
	UPROPERTY(meta = (Input))
	FVector ExternalLinearVelocity = FVector::ZeroVector;

	// Additional angular velocity that is added to the component angular velocity. This can be used
	// to make the simulation act as if the actor is rotating even when it is not. E.g., to apply
	// physics to a character on a podium as the camera rotates around it, to emulate the podium
	// itself rotating. Vector is in world space. Units are deg/s.
	UPROPERTY(meta = (Input))
	FVector ExternalAngularVelocity = FVector::ZeroVector;

	// This will treat the external velocity like a wind field and add turbulence to it. Units are
	// the same as velocity, so this is the approximate magnitude of the turbulence.
	UPROPERTY(meta = (Input))
	FVector ExternalTurbulenceVelocity = FVector::ZeroVector;
};

/**
 * Forces tracking of the input animation (on all physics bodies) for the next N frames
 */
USTRUCT(meta = (DisplayName = "Track Input Pose", Keywords = "Reset,Simulate"))
struct FRigUnit_TrackInputPose : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_TrackInputPose()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// The solver to relate this new physics element to
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;

	// The number of frames to track the input pose for
	UPROPERTY(meta = (Input, ClampMin = "0"))
	int NumberOfFrames = 1;

	// If true, then the number will be forced, potentially reducing the number. If false, then the
	// NumberOfFrames will only be used to increase the number of frames remaining.
	UPROPERTY(meta = (Input))
	bool bForceNumberOfFrames = false;
};


// Adds a set of physics components including the body, joint and controls
USTRUCT(meta = (DisplayName = "Spawn Physics Components", Keywords = "Add,Construction,Create,New,Body,Joint,Control", Varying))
struct FRigUnit_AddPhysicsComponents : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_AddPhysicsComponents()
	{
		Owner.Type = ERigElementType::Bone;
		Solver.PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		Solver.PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	/**
	 * The owner of the newly created component (must be set/valid)
	 */
	UPROPERTY(meta = (Input, BoneName))
	FRigElementKey Owner;

	UPROPERTY(meta = (Input))
	bool bAddJoint = true;

	UPROPERTY(meta = (Input))
	bool bAddSimSpaceControl = true;

	UPROPERTY(meta = (Input))
	bool bAddParentSpaceControl = true;

	UPROPERTY(meta = (Output))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Output))
	FRigComponentKey PhysicsJointComponentKey;

	UPROPERTY(meta = (Output))
	FRigComponentKey SimSpaceControlComponentKey;

	UPROPERTY(meta = (Output))
	FRigComponentKey ParentSpaceControlComponentKey;

	// The solver to relate this new physics element to
	UPROPERTY(meta = (Input))
	FRigPhysicsBodySolverSettings Solver;

	// The dynamics properties of the new physics body	
	UPROPERTY(meta = (Input))
	FRigPhysicsDynamics Dynamics;

	// The collision properties of the new physics body
	UPROPERTY(meta = (Input))
	FRigPhysicsCollision Collision;

	// The runtime modifiable data of the new physics body
	UPROPERTY(meta = (Input))
	FPhysicsControlModifierData BodyData;

	// The properties of the joint
	UPROPERTY(meta = (Input))
	FRigPhysicsJointData JointData;

	// Optional motor/drive associated with the physics joint
	UPROPERTY(meta = (Input))
	FRigPhysicsDriveData DriveData;

	// Data for the simulation space control
	UPROPERTY(meta = (Input))
	FPhysicsControlData SimSpaceControlData;

	// Data for the parent space control
	UPROPERTY(meta = (Input))
	FPhysicsControlData ParentSpaceControlData;
};

// Imports/creates bones from the physics asset and creates collision for them.
// The bones will lose their hierarchy and be placed under the specified parent - ready to be moved around.
USTRUCT(meta = (DisplayName = "Import Collision From Physics Asset", Keywords = "Construction,Create,New", Varying))
struct FRigUnit_HierarchyImportCollisionFromPhysicsAsset : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchyImportCollisionFromPhysicsAsset()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// Note that setting the solver component, if known, has the benefit of avoiding the need to
	// search for an automatic solver.
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;

	// If true (and the physics solver is not explicitly set), then this component will be added to
	// any physics solver that exists above it in the hierarchy, if that solver allows automatically
	// adding physics components.
	UPROPERTY(meta = (Input))
	bool bUseAutomaticSolver = true;

	UPROPERTY(meta = (Input))
	TObjectPtr<UPhysicsAsset> PhysicsAsset;

	// Name of the constraint profile to use. If empty (or invalid), the default profile will be used
	UPROPERTY(meta = (Input))
	FName ConstraintProfileName;

	// If this is empty, then all bones with bodies in the physics asset will be created. Otherwise
	// only bodies that relate to the specified bones will be created.
	UPROPERTY(meta = (Input))
	TArray<FName> BonesToUse;

	// Prefix to the bone names
	UPROPERTY(meta = (Input))
	FName NameSpace = "Physics_";

	// Parent/owner for all the new bones
	UPROPERTY(meta = (Input))
	FRigElementKey Owner;

	UPROPERTY(meta = (Output))
	TArray<FRigElementKey> BoneKeys;

	UPROPERTY(meta = (Output))
	TArray<FRigComponentKey> PhysicsBodyComponentKeys;
};

// Creates multiple physics components based on the supplied physics asset.
// Note that the resulting simulation bodies may not precisely match the physics asset.
USTRUCT(meta = (DisplayName = "Instantiate From Physics Asset", Keywords = "Construction,Create,New", Varying))
struct FRigUnit_HierarchyInstantiateFromPhysicsAsset : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchyInstantiateFromPhysicsAsset()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// Note that setting the solver component, if known, has the benefit of avoiding the need to
	// search for an automatic solver.
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;

	// If true (and the physics solver is not explicitly set), then this component will be added to
	// any physics solver that exists above it in the hierarchy, if that solver allows automatically
	// adding physics components.
	UPROPERTY(meta = (Input))
	bool bUseAutomaticSolver = true;

	UPROPERTY(meta = (Input))
	TObjectPtr<UPhysicsAsset> PhysicsAsset;

	// Name of the constraint profile to use. If empty (or invalid), the default profile will be used
	UPROPERTY(meta = (Input))
	FName ConstraintProfileName;

	// If this is empty, then all bodies in the physics asset that match a bone in the hierarchy
	// will be created. Otherwise only bodies that relate to the specified bones will be created.
	UPROPERTY(meta = (Input))
	TArray<FRigElementKey> BonesToUse;

	// Whether to enable the joints authored in the physics asset. Note that you can't have drives
	// without joints.
	UPROPERTY(meta = (Input))
	bool bEnableJoints = true;

	// Whether to enable the drives authored in the physics asset. Note that if you are creating
	// parent space controls, you may not want the drives
	UPROPERTY(meta = (Input))
	bool bEnableDrives = true;

	UPROPERTY(meta = (Input))
	bool bAddSimSpaceControl = false;

	UPROPERTY(meta = (Input))
	bool bAddParentSpaceControl = false;

	// Data for the simulation space control
	UPROPERTY(meta = (Input))
	FPhysicsControlData SimSpaceControlData;

	// Data for the parent space control
	UPROPERTY(meta = (Input))
	FPhysicsControlData ParentSpaceControlData;

	UPROPERTY(meta = (Output))
	TArray<FRigComponentKey> PhysicsBodyComponentKeys;

	UPROPERTY(meta = (Output))
	TArray<FRigComponentKey> PhysicsJointComponentKeys;

	UPROPERTY(meta = (Output))
	TArray<FRigComponentKey> SimSpaceControlComponentKeys;

	UPROPERTY(meta = (Output))
	TArray<FRigComponentKey> ParentSpaceControlComponentKeys;
};


// Retrieves the simulation space data. Note that this will have been generated during the
// simulation step, so the values returned will relate to the previous update if the solver has not
// yet been stepped.
USTRUCT(meta = (DisplayName = "Get Physics Solver Space Data", Keywords = "Debug"))
struct FRigUnit_GetPhysicsSolverSpaceData : public FRigUnit_PhysicsBase
{
	GENERATED_BODY()

	FRigUnit_GetPhysicsSolverSpaceData()
	{
		PhysicsSolverComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsSolverComponentKey.Name = FRigPhysicsSolverComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	// The solver to relate this new physics element to
	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsSolverComponentKey;

	UPROPERTY(meta = (Output))
	FVector LinearVelocity = FVector::ZeroVector;

	UPROPERTY(meta = (Output))
	FVector AngularVelocity = FVector::ZeroVector;

	UPROPERTY(meta = (Output))
	FVector LinearAcceleration = FVector::ZeroVector;

	UPROPERTY(meta = (Output))
	FVector AngularAcceleration = FVector::ZeroVector;

	UPROPERTY(meta = (Output))
	FVector Gravity = FVector::ZeroVector;
};

#undef UE_API
