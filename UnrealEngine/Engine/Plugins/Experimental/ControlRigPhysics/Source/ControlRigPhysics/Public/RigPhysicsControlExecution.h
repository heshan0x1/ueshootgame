// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigPhysicsExecution.h"
#include "RigPhysicsControlComponent.h"
#include "PhysicsControlData.h"

#include "Units/Execution/RigUnit_DynamicHierarchy.h"

#include "RigPhysicsControlExecution.generated.h"

#define UE_API CONTROLRIGPHYSICS_API

/**
 * Adds a new physics control as a component on the owner element.
 * Note: This node only runs as part of the construction event.
 */
USTRUCT(meta = (Category = "RigPhysics", NodeColor = "1.0 0.6 0.3", DisplayName = "Spawn Physics Control", Keywords = "Add,Construction,Create,New,Control", Varying))
struct FRigUnit_AddPhysicsControl : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_AddPhysicsControl()
	{
		Owner.Type = ERigElementType::Bone;
		ParentBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		ParentBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
		ChildBodyComponentKey.ElementKey.Type = ERigElementType::Bone;
		ChildBodyComponentKey.Name = FRigPhysicsBodyComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	/**
	 * The owner of the newly created component (must be set/valid)
	 */
	UPROPERTY(meta = (Input, BoneName))
	FRigElementKey Owner;

	UPROPERTY(meta = (Output))
	FRigComponentKey ControlComponentKey;

	// The optional body that "does" the controlling - though if it is dynamic then it can move too
	UPROPERTY(meta = (Input))
	FRigComponentKey ParentBodyComponentKey;

	UPROPERTY(meta = (Input), DisplayName = "Use Parent Body")
	bool bUseParentBodyAsDefault = false;

	// The body that is controlled
	UPROPERTY(meta = (Input))
	FRigComponentKey ChildBodyComponentKey;

	/** Describes the initial strength etc of the new control */
	UPROPERTY(meta = (Input))
	FPhysicsControlData ControlData;

	/** Fine control over the control strengths etc */
	UPROPERTY(meta = (Input))
	FPhysicsControlMultiplier ControlMultiplier;

	/** Describes the initial target for the new control */
	UPROPERTY(meta = (Input))
	FPhysicsControlTarget ControlTarget;

	/** Which set to include the control in (optional). Note that it automatically gets added to the set "All" */
	//UPROPERTY(meta = (Input))
	//FName Set;
};

// Sets whether a control is enabled
USTRUCT(meta = (DisplayName = "Set Physics Control Enabled", Varying))
struct FRigUnit_HierarchySetControlEnabled : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlEnabled()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	bool bEnabled = true;
};

// Sets the custom control point on a control
USTRUCT(meta = (DisplayName = "Set Physics Control Custom Control Point", Varying))
struct FRigUnit_HierarchySetControlCustomControlPoint : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlCustomControlPoint()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	// The position of the control point relative to the child mesh, when using a custom control point.
	UPROPERTY(meta = (Input))
	FVector CustomControlPoint = FVector::ZeroVector;

	// Whether or not to use the custom control point position
	UPROPERTY(meta = (Input))
	bool bUseCustomControlPoint = true;
};


// Sets the control data for a physics control
USTRUCT(meta = (DisplayName = "Set Physics Control Data", Varying))
struct FRigUnit_HierarchySetControlData : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlData()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	FPhysicsControlData ControlData;
};

USTRUCT(meta = (DisplayName = "Set Physics Control Linear Strength", Varying))
struct FRigUnit_HierarchySetControlLinearStrength : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlLinearStrength()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	float Strength = 0.0f;
};

USTRUCT(meta = (DisplayName = "Set Physics Control Linear Damping Ratio", Varying))
struct FRigUnit_HierarchySetControlLinearDampingRatio : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlLinearDampingRatio()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	float DampingRatio = 1.0f;
};

USTRUCT(meta = (DisplayName = "Set Physics Control Angular Strength", Varying))
struct FRigUnit_HierarchySetControlAngularStrength : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlAngularStrength()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	float Strength = 0.0f;
};

USTRUCT(meta = (DisplayName = "Set Physics Control Angular Damping Ratio", Varying))
struct FRigUnit_HierarchySetControlAngularDampingRatio : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlAngularDampingRatio()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	float DampingRatio = 1.0f;
};

// Gets the control data for a physics control
USTRUCT(meta = (DisplayName = "Get Physics Control Data", Varying))
struct FRigUnit_HierarchyGetControlData : public FRigUnit_PhysicsBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetControlData()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Output))
	FPhysicsControlData ControlData;
};

// Sets the multipliers for a physics control
USTRUCT(meta = (DisplayName = "Set Physics Control Multiplier", Varying))
struct FRigUnit_HierarchySetControlMultiplier : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlMultiplier()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	FPhysicsControlMultiplier ControlMultiplier;
};

// Sets the control data and multiplier for a physics control
USTRUCT(meta = (DisplayName = "Set Physics Control Data And Multiplier", Varying))
struct FRigUnit_HierarchySetControlDataAndMultiplier : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlDataAndMultiplier()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	FPhysicsControlData ControlData;

	UPROPERTY(meta = (Input))
	FPhysicsControlMultiplier ControlMultiplier;
};

// Sets the target for a physics control
USTRUCT(meta = (DisplayName = "Set Physics Control Target", Varying))
struct FRigUnit_HierarchySetControlTarget : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetControlTarget()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	UPROPERTY(meta = (Input))
	FPhysicsControlTarget ControlTarget;
};

// Sets the target for a physics control and updates the target velocities based on the previews
// targets (which will be overwritten)
USTRUCT(meta = (DisplayName = "Update Physics Control Target", Varying))
struct FRigUnit_HierarchyUpdateControlTarget : public FRigUnit_PhysicsBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchyUpdateControlTarget()
	{
		PhysicsControlComponentKey.ElementKey.Type = ERigElementType::Bone;
		PhysicsControlComponentKey.Name = FRigPhysicsControlComponent::GetDefaultName();
	}

	RIGVM_METHOD()
	UE_API virtual void Execute() override;

	UPROPERTY(meta = (Input))
	FRigComponentKey PhysicsControlComponentKey;

	/** The target position of the child body, relative to the parent body */
	UPROPERTY(meta = (Input))
	FVector TargetPosition = FVector::ZeroVector;

	/** The target orientation of the child body, relative to the parent body */
	UPROPERTY(meta = (Input))
	FRotator TargetOrientation = FRotator::ZeroRotator;

	// The delta time used to calculate the target velocity
	UPROPERTY(meta = (Input))
	float DeltaTime = 0.0f;
};


#undef UE_API
