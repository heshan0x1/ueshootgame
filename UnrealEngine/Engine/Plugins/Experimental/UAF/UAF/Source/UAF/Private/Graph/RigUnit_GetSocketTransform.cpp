// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetSocketTransform.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_GetSocketTransform)

FRigUnit_GetSocketTransform_Execute()
{
	if (SceneComponent)
	{
		Result = SceneComponent->GetSocketTransform(SocketName, TransformSpace);
	}
}
