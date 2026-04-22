// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "TraitCore/EntryPointHandle.h"
#include "TraitCore/TraitWriter.h"

class UAnimNextAnimationGraph;
class FAnimationAnimNextRuntimeTest_SimpleGraphBuilder;

namespace UE::UAF
{
	struct FAnimGraphFactory;
}

namespace UE::UAF
{

struct FAnimGraphBuilderContext
{
private:
	friend FAnimGraphFactory;
	friend ::FAnimationAnimNextRuntimeTest_SimpleGraphBuilder;

	// Builds a graph using the populated TraitWriter and VariableStructs
	UAFANIMGRAPH_API const UAnimNextAnimationGraph* Build();

public:
	// Root trait handle
	FAnimNextEntryPointHandle RootTraitHandle;

	// Trait writer used to build the graph
	FTraitWriter TraitWriter;

	// Variable structs used to communicate with the graph
	TArray<const UScriptStruct*> VariableStructs;
};

}