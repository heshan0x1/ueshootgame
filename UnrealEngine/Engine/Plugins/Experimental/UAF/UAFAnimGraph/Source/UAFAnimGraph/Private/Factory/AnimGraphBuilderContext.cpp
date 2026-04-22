// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/AnimGraphBuilderContext.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "TraitCore/TraitWriter.h"

namespace UE::UAF
{

const UAnimNextAnimationGraph* FAnimGraphBuilderContext::Build()
{
	if(!ensure(TraitWriter.GetGraphSharedData().Num() > 0))
	{
		return nullptr;
	}

	if (!ensure(RootTraitHandle.IsValid()))
	{
		return nullptr;
	}

	// Create our anim graph
	UAnimNextAnimationGraph* AnimationGraph = NewObject<UAnimNextAnimationGraph>(GetTransientPackage(), NAME_None, RF_Transient);

	FAnimNextGraphEntryPoint& EntryPoint = AnimationGraph->EntryPoints.AddDefaulted_GetRef();
	EntryPoint.EntryPointName = AnimationGraph->DefaultEntryPoint;
	EntryPoint.RootTraitHandle = RootTraitHandle;
	AnimationGraph->ReferencedVariableStructs = VariableStructs;
	AnimationGraph->GraphReferencedObjects = TraitWriter.GetGraphReferencedObjects();
	AnimationGraph->GraphReferencedSoftObjects = TraitWriter.GetGraphReferencedSoftObjects();
	ensure(AnimationGraph->LoadFromArchiveBuffer(TraitWriter.GetGraphSharedData()));

	// Clear async flag to make the object visible to GC
	(void)AnimationGraph->AtomicallyClearInternalFlags(EInternalObjectFlags::Async);
	return AnimationGraph;
}

}