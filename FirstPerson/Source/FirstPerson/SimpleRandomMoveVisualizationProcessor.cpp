// Fill out your copyright notice in the Description page of Project Settings.

#include "SimpleRandomMoveVisualizationProcessor.h"
#include "MassGameplayDebugTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimpleRandomMoveVisualizationProcessor)

//----------------------------------------------------------------------//
// USimpleRandomMoveDebugVisProcessor
// Client-side processor that updates ISM transforms each frame
//----------------------------------------------------------------------//
USimpleRandomMoveDebugVisProcessor::USimpleRandomMoveDebugVisProcessor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bAutoRegisterWithProcessingPhases = true;
}

//----------------------------------------------------------------------//
// USimpleRandomMoveAssignDebugVisProcessor
// Client-side observer that assigns ISM instance indices on entity spawn
//----------------------------------------------------------------------//
USimpleRandomMoveAssignDebugVisProcessor::USimpleRandomMoveAssignDebugVisProcessor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	// Observer processors register themselves via the observer registry, not processing phases
	// ObservedType and ObservedOperations are inherited from parent (FSimDebugVisFragment, Add)
}
