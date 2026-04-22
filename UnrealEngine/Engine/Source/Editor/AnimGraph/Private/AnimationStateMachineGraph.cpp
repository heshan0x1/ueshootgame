// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationStateMachineGraph.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimationStateMachineGraph)

#define LOCTEXT_NAMESPACE "AnimationGraph"

/////////////////////////////////////////////////////
// UAnimationStateMachineGraph

UAnimationStateMachineGraph::UAnimationStateMachineGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAllowDeletion = false;
	bAllowRenaming = true;
}


#undef LOCTEXT_NAMESPACE
