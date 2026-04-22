// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextRigVMFunctionData.generated.h"

USTRUCT()
struct FAnimNextRigVMFunctionData
{
	GENERATED_BODY()

	// The name of the function
	UPROPERTY()
	FName Name;

	// The mangled event name of the function
	UPROPERTY()
	FName EventName;

	// Indices into RigVM external variables for each arg
	UPROPERTY()
	TArray<int32> ArgIndices;
};