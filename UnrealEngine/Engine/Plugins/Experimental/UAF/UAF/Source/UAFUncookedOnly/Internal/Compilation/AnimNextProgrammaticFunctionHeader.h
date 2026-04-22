// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamType.h"
#include "StructUtils/PropertyBag.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"

/**
 * Struct wrapping a RigVM Function header. Also contains some metadata used to drive compilation.
 */
struct FAnimNextProgrammaticFunctionHeader
{
	/** RigVM Function Header we are wrapping */
	FRigVMGraphFunctionHeader FunctionHeader;

	bool operator==(const FAnimNextProgrammaticFunctionHeader& Other) const = default;
};

