// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Guid.h"
#include "UObject/ObjectMacros.h"

#include "RigVMDebugInfo.generated.h"

#define UE_API RIGVM_API

class UObject;
class URigVMNode;

USTRUCT()
struct FRigVMDebugInfo
{
	GENERATED_BODY()

	FRigVMDebugInfo()
	{
	}

	void Reset()
	{
		EarlyExitInstruction.Reset();
	}

	bool IsEmpty() const
	{
		return !EarlyExitInstruction.IsSet();
	}

	int32 GetInstructionForEarlyExit() const
	{
		return EarlyExitInstruction.Get(INDEX_NONE);
	}

	void SetInstructionForEarlyExit(const int32 InInstruction)
	{
		EarlyExitInstruction = InInstruction;
	}

	bool ShouldEarlyExitBeforeInstruction(const int32 InInstruction) const
	{
		if (EarlyExitInstruction.IsSet())
		{
			return InInstruction > EarlyExitInstruction.GetValue();
		}
		return false;
	}

	void CopyFrom(const FRigVMDebugInfo& Other)
	{
		EarlyExitInstruction = Other.EarlyExitInstruction;
	}

	DECLARE_EVENT_ThreeParams(URigVM, FExecutionHaltedEvent, int32, UObject*, const FName&);
	inline FExecutionHaltedEvent& ExecutionHalted()
	{
		return OnExecutionHalted;
	}

private:
	FExecutionHaltedEvent OnExecutionHalted;
	TOptional<int32> EarlyExitInstruction;
};

#undef UE_API
