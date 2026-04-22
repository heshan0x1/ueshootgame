// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigPhysicsControlExecution.h"

#include "RigPhysicsControlComponent.h"
#include "PhysicsControlHelpers.h"

#include "Rigs/RigHierarchyController.h"
#include "Units/RigUnitContext.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"

#include "ControlRig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigPhysicsControlExecution)

//======================================================================================================================
FRigUnit_AddPhysicsControl_Execute()
{
	if (!ExecuteContext.Hierarchy)
	{
		return;
	}
	if (ExecuteContext.GetEventName() != FRigUnit_PrepareForExecution::EventName)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_ERROR(TEXT("AddPhysicsControl can only be used during Setup"));
	}

	if (URigHierarchyController* Controller = ExecuteContext.Hierarchy->GetController())
	{
		FRigHierarchyControllerInstructionBracket InstructionBracket(
			Controller, ExecuteContext.GetInstructionIndex());

		ControlComponentKey = Controller->AddComponent(FRigPhysicsControlComponent::StaticStruct(), 
			FRigPhysicsControlComponent::GetDefaultName(), Owner);
		if (ControlComponentKey.IsValid())
		{
			if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
				ExecuteContext.Hierarchy->FindComponent(ControlComponentKey)))
			{
				Component->ParentBodyComponentKey = ParentBodyComponentKey;
				Component->bUseParentBodyAsDefault = bUseParentBodyAsDefault;
				Component->ChildBodyComponentKey = ChildBodyComponentKey;
				Component->ControlData = ControlData;
				Component->ControlMultiplier = ControlMultiplier;
				Component->ControlTarget = ControlTarget;
			}
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlEnabled_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData.bEnabled = bEnabled;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlCustomControlPoint_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData.CustomControlPoint = CustomControlPoint;
			Component->ControlData.bUseCustomControlPoint = bUseCustomControlPoint;
		}
	}
}


//======================================================================================================================
FRigUnit_HierarchySetControlData_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData = ControlData;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlLinearStrength_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData.LinearStrength = Strength;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlLinearDampingRatio_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData.LinearDampingRatio = DampingRatio;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlAngularStrength_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData.AngularStrength = Strength;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlAngularDampingRatio_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData.AngularDampingRatio = DampingRatio;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchyGetControlData_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			ControlData = Component->ControlData;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlMultiplier_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlMultiplier = ControlMultiplier;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlDataAndMultiplier_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlData = ControlData;
			Component->ControlMultiplier = ControlMultiplier;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchySetControlTarget_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlTarget = ControlTarget;
		}
	}
}

//======================================================================================================================
FRigUnit_HierarchyUpdateControlTarget_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();
	if (ExecuteContext.Hierarchy)
	{
		if (FRigPhysicsControlComponent* Component = Cast<FRigPhysicsControlComponent>(
			ExecuteContext.Hierarchy->FindComponent(PhysicsControlComponentKey)))
		{
			Component->ControlTarget.TargetVelocity = UE::PhysicsControl::CalculateLinearVelocity(
				TargetPosition, Component->ControlTarget.TargetPosition, DeltaTime);
			Component->ControlTarget.TargetAngularVelocity = UE::PhysicsControl::CalculateAngularVelocity(
				TargetOrientation.Quaternion(), Component->ControlTarget.TargetOrientation.Quaternion(), DeltaTime);
			Component->ControlTarget.TargetPosition = TargetPosition;
			Component->ControlTarget.TargetOrientation = TargetOrientation;
		}
	}
}

