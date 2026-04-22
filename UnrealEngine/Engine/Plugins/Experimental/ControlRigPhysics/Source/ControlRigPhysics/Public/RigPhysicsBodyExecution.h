// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigPhysicsExecution.h"
#include "RigPhysicsBodyComponent.h"
#include "RigPhysicsSolverComponent.h"
#include "RigPhysicsSimulation.h"
#include "PhysicsControlData.h"

#include "Units/Execution/RigUnit_DynamicHierarchy.h"

#include "RigPhysicsBodyExecution.generated.h"

#define UE_API CONTROLRIGPHYSICS_API

/**
 * Adds a new physics body as a component on the owner element.
 * Note: This node only runs as part of the construction event.
 */
USTRUCT(meta = (DisplayName = "Spawn Physics Body", Keywords = "Add,Construction,Create,New,Body,Skeleton", Varying))
struct FRigUnit_AddPhysicsBody : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_AddPhysicsBody()
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

	UPROPERTY(meta = (Output))
	FRigComponentKey PhysicsBodyComponentKey;

	/*
	 * The solver to relate this new physics element to
	 */
	UPROPERTY(meta = (Input))
	FRigPhysicsBodySolverSettings Solver;

	// The dynamics properties of the new physics element	
	UPROPERTY(meta = (Input))
	FRigPhysicsDynamics Dynamics;

	// The collision properties of the new physics element
	UPROPERTY(meta = (Input))
	FRigPhysicsCollision Collision;

	// The runtime modifiable data
	UPROPERTY(meta = (Input))
	FPhysicsControlModifierData BodyData;
};

// Discards any existing collision data and replaces it with a box based on the joint positions.
// Note that this must be called before the physics solver is instantiated/stepped.
USTRUCT(meta = (DisplayName = "Calculate Physics Collision", Varying))
struct FRigUnit_HierarchyAutoCalculateCollision : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchyAutoCalculateCollision()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	// For boxes: The minimum box extent, as a proportion of the maximum box extent.
	// For capsules: The minimum radius, as a proportion of the length (not including the radius)
	UPROPERTY(meta = (Input))
	float MinAspectRatio = 0.25f;

	// For boxes: The minimum side length. 
	// For capsules: The minimum radius
	UPROPERTY(meta = (Input))
	float MinSize = 0.0f;
};

// Sets the mass etc for a physics component body
USTRUCT(meta = (DisplayName = "Set Physics Body Dynamics Properties", Varying))
struct FRigUnit_HierarchySetDynamics : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetDynamics()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FRigPhysicsDynamics Dynamics;
};

// Sets the collision for a physics component body
USTRUCT(meta = (DisplayName = "Set Physics Body Collision Properties", Varying))
struct FRigUnit_HierarchySetCollision : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetCollision()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FRigPhysicsCollision Collision;
};


// Disables collision between two bodies
USTRUCT(meta = (DisplayName = "Disable Collision Between", Varying))
struct FRigUnit_HierarchyDisableCollisionBetween : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchyDisableCollisionBetween()
	{
		PhysicsBodyComponentKey1.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey1.Name = FRigPhysicsBodyComponent::GetDefaultName();
		PhysicsBodyComponentKey2.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey2.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey1;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey2;
};

/**
 * Sets what bone is used as a source transform for the physics body. This is used as a kinematic target, and when
 * initializing the simulation.
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Source Bone", Varying))
struct FRigUnit_HierarchySetPhysicsBodySourceBone : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodySourceBone()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
		SourceBone.Type = ERigElementType::Bone;
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FRigElementKey SourceBone;
};

/**
 * Sets what bone is targeted by the simulation - i.e. where the simulation output is written to.
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Target Bone", Varying))
struct FRigUnit_HierarchySetPhysicsBodyTargetBone : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyTargetBone()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
		TargetBone.Type = ERigElementType::Bone;
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FRigElementKey TargetBone;
};


/**
 * Sets all the data on a body - but in a sparse way so you can decide which parameters get applied.
 * Danny TODO - Note that the sparse data does not get displayed correctly, so this is largely 
 * unusable - the flags that enable/disable all end up getting reset if the user attempts to change them.
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Data", Varying))
struct FRigUnit_HierarchySetPhysicsBodySparseData : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodySparseData()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FPhysicsControlModifierSparseData Data;
};

/**
 * Sets the kinematic target for a body - note that this won't actually make the body kinematic
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Kinematic Target", Varying))
struct FRigUnit_HierarchySetPhysicsBodyKinematicTarget : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyKinematicTarget()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FTransform KinematicTarget;
};

/**
 * Sets the kinematic target space for a body - note that this won't actually make the body kinematic
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Kinematic Target Space", Varying))
struct FRigUnit_HierarchySetPhysicsBodyKinematicTargetSpace : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyKinematicTargetSpace()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	EPhysicsControlKinematicTargetSpace KinematicTargetSpace = EPhysicsControlKinematicTargetSpace::OffsetInBoneSpace;
};

/**
 * Sets the movement mode for this body. 
 * Danny TODO explain the different between kinematic and static
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Movement Mode", Varying))
struct FRigUnit_HierarchySetPhysicsBodyMovementType : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyMovementType()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	EPhysicsMovementType MovementType = EPhysicsMovementType::Simulated;
};

/**
 * Sets what collision mode is used for this body
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Collision Mode", Varying))
struct FRigUnit_HierarchySetPhysicsBodyCollisionType : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyCollisionType()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	TEnumAsByte<ECollisionEnabled::Type> CollisionType = ECollisionEnabled::QueryAndPhysics;
};


// Sets whether this body should be included in checks for resetting physics on the whole rig
USTRUCT(meta = (DisplayName = "Set Physics Body Include In Checks For Reset", Varying))
struct FRigUnit_HierarchySetPhysicsBodyIncludeInChecksForReset : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyIncludeInChecksForReset()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	bool bInclude = true;
};

// Applies the material settings to all the collision shapes
USTRUCT(meta = (DisplayName = "Set Physics Body Material", Varying))
struct FRigUnit_HierarchySetPhysicsBodyMaterial : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyMaterial()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	FRigPhysicsMaterial Material;
};

/**
 * Sets the multiplier on gravity that should be applied to the body.
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Gravity Multiplier", Varying))
struct FRigUnit_HierarchySetPhysicsBodyGravityMultiplier : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyGravityMultiplier()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	float GravityMultiplier = 1.0f;
};

/**
 * Controls the amount that the simulation is blended back into the target bones. 
 * Danny TODO implement
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Physics Blend Weight", Varying))
struct FRigUnit_HierarchySetPhysicsBodyPhysicsBlendWeight : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyPhysicsBlendWeight()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	float PhysicsBlendWeight = 1.0f;
};

/**
 * If true, then kinematic objects will be written back from simulation to the bones. This only 
 * necessary when either kinematic targets are being used, or when the target bone differs from the source bone.
 * Danny TODO - check the implementation, and also can we auto-calculate this at runtime?
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Update Kinematic From Simulation", Varying))
struct FRigUnit_HierarchySetPhysicsBodyUpdateKinematicFromSimulation : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyUpdateKinematicFromSimulation()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	bool bUpdateKinematicFromSimulation = true;
};

/**
 * If true, then kinematic objects will be written back from simulation to the bones. This only 
 * necessary when either kinematic targets are being used, or when the target bone differs from the source bone.
 * Danny TODO - check the implementation, and also can we auto-calculate this at runtime?
 */
USTRUCT(meta = (DisplayName = "Set Physics Body Damping", Varying))
struct FRigUnit_HierarchySetPhysicsBodyDamping : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetPhysicsBodyDamping()
	{
		PhysicsBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsBodyComponentKey;

	UPROPERTY(meta = (Input))
	float LinearDamping = 0;

	UPROPERTY(meta = (Input))
	float AngularDamping = 0;
};

#undef UE_API
