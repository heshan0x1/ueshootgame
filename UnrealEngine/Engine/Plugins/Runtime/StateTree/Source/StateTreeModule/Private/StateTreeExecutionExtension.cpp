// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeExecutionExtension.h"
#include "StateTreeExecutionTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeExecutionExtension)


FStateTreeExecutionExtension::FNextTickArguments::FNextTickArguments()
	: Reason(UE::StateTree::ETickReason::None)
{
}

FStateTreeExecutionExtension::FNextTickArguments::FNextTickArguments(UE::StateTree::ETickReason InReason)
	: Reason(InReason)
{ }

