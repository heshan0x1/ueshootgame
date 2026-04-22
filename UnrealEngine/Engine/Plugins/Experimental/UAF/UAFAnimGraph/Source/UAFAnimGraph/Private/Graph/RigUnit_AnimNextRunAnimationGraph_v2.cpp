// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RigUnit_AnimNextRunAnimationGraph_v2.h"

#include "AnimNextAnimGraphSettings.h"
#include "AnimNextAnimGraphStats.h"
#include "TraitInterfaces/IEvaluate.h"
#include "TraitInterfaces/IUpdate.h"
#include "Module/AnimNextModuleInstance.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextGraphInstance.h"
#include "Graph/AnimNextModuleAnimGraphComponent.h"
#include "Graph/RigVMTrait_AnimNextPublicVariables.h"
#include "Injection/GraphInstanceInjectionComponent.h"
#include "TraitCore/TraitEventList.h"
#include "Injection/AnimNextModuleInjectionComponent.h"
#include "TraceAnimNextGraphInstances.h"
#include "UAFRigVMComponent.h"
#include "Factory/AnimGraphFactory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextRunAnimationGraph_v2)

FRigUnit_AnimNextRunAnimationGraph_v2_Execute()
{
	SCOPED_NAMED_EVENT(UAF_Run_Graph_V2, FColor::Orange);

	using namespace UE::UAF;

	const FAnimNextModuleContextData& ModuleContextData = ExecuteContext.GetContextData<FAnimNextModuleContextData>();
	FAnimNextModuleInstance& ModuleInstance = ModuleContextData.GetModuleInstance();

	if (!ReferencePose.ReferencePose.IsValid())
	{
		return;
	}

	const UE::UAF::FReferencePose& RefPose = ReferencePose.ReferencePose.GetRef<UE::UAF::FReferencePose>();

	int32 DesiredLOD = LOD;
	if (DesiredLOD == -1)
	{
		DesiredLOD = RefPose.GetSourceLODLevel();
	}

	// TODO: Currently forcing additive flag to false here
	if (Result.LODPose.ShouldPrepareForLOD(RefPose, DesiredLOD, false))
	{
		Result.LODPose.PrepareForLOD(RefPose, DesiredLOD, true, false);
	}

	ensure(Result.LODPose.LODLevel == DesiredLOD);

	// Get a host to run this graph
	const UAnimNextAnimationGraph* HostGraph = Graph.HostGraph ? Graph.HostGraph.Get() : FAnimGraphFactory::GetDefaultGraphHost();
	FAnimNextModuleAnimGraphComponent& AnimationGraphComponent = ModuleInstance.GetComponent<FAnimNextModuleAnimGraphComponent>();
	if(HostGraph == nullptr)
	{
		AnimationGraphComponent.ReleaseInstance(WorkData.WeakHost);
		return;
	}


	// Release the instance if the host graph has changed
	if(WorkData.WeakHost.IsValid() && !WorkData.WeakHost.Pin()->UsesAnimationGraph(HostGraph))
	{
		AnimationGraphComponent.ReleaseInstance(WorkData.WeakHost);
	}

	// Lazily (re-)allocate graph instance if required
	if(!WorkData.WeakHost.IsValid())
	{
		// Reset our graph reference, we may be allocating a new graph
		WorkData.InjectedGraphReference.Reset();

		TSharedPtr<FAnimNextGraphInstance> NewGraphInstance = AnimationGraphComponent.AllocateInstance(HostGraph, Overrides.GetOverrides().Pin()).Pin();
		if (NewGraphInstance.IsValid())
		{
			WorkData.WeakHost = NewGraphInstance;

			FGraphInstanceInjectionComponent& HostInjectionComponent = NewGraphInstance->GetComponent<FGraphInstanceInjectionComponent>();
			WorkData.InjectedGraphReference = HostInjectionComponent.GetInjectionInfo().GetDefaultInjectionSite();
		}
	}

	if(!WorkData.WeakHost.IsValid())
	{
		return;
	}

	// Take a strong reference to the host instance, we are going to run it
	TSharedRef<FAnimNextGraphInstance> HostInstanceRef = WorkData.WeakHost.Pin().ToSharedRef();
	FAnimNextGraphInstance& HostInstance = HostInstanceRef.Get();

	// Re-set the injected graph each time, as it may change per-update
	if (!WorkData.InjectedGraphReference.IsNone())
	{
		HostInstance.SetVariable(WorkData.InjectedGraphReference, Graph);
	}

	if (HostInstance.GetAnimationGraph()->GetRigVM())
	{
		// Propagate delta time
		FUAFRigVMComponent& RigVMComponent = HostInstance.GetComponent<FUAFRigVMComponent>();
		FAnimNextExecuteContext& AnimNextExecuteContext = RigVMComponent.GetExtendedExecuteContext().GetPublicDataSafe<FAnimNextExecuteContext>();
		AnimNextExecuteContext.SetDeltaTime(ExecuteContext.GetDeltaTime());
	}

	// Every graph in a schedule will see the same input events (if they were queued before the schedule started)
	FUpdateGraphContext UpdateGraphContext(HostInstance, ExecuteContext.GetDeltaTime());
	UpdateGraphContext.SetBindingObject(RefPose.SkeletalMeshComponent);

	FTraitEventList& InputEventList = UpdateGraphContext.GetInputEventList();

	// A module can contain multiple graphs, we copy the input event list since it might be appended to during our update
	{
		FReadScopeLock ReadLock(ModuleInstance.EventListLock);
		InputEventList = ModuleInstance.InputEventList;
	}

	// Track how many input events we started with, we'll append the new ones
	const int32 NumOriginalInputEvents = InputEventList.Num();

	// Internally we use memstack allocation, so we need a mark here
	FMemStack& MemStack = FMemStack::Get();
	FMemMark MemMark(MemStack);

	// We allocate a dummy buffer to trigger the allocation of a large chunk if this is the first mark
	// This reduces churn internally by avoiding a chunk to be repeatedly allocated and freed as we push/pop marks
	MemStack.Alloc(size_t(FPageAllocator::SmallPageSize) + 1, 16);

	UpdateGraph(UpdateGraphContext);

	FEvaluateGraphContext EvaluateGraphContext(HostInstance, RefPose, DesiredLOD);
	EvaluateGraphContext.SetBindingObject(RefPose.SkeletalMeshComponent);
	EvaluateGraph(EvaluateGraphContext, Result);

	TRACE_ANIMNEXT_MODULE(ModuleInstance);
	TRACE_ANIMNEXT_GRAPHINSTANCES(HostInstance);

	// We might have appended new input/output events, append them
	{
		const int32 NumInputEvents = InputEventList.Num();

		FWriteScopeLock WriteLock(ModuleInstance.EventListLock);

		// Append the new input events
		for (int32 EventIndex = NumOriginalInputEvents; EventIndex < NumInputEvents; ++EventIndex)
		{
			FAnimNextTraitEventPtr& Event = InputEventList[EventIndex];
			if (Event->IsValid())
			{
				ModuleInstance.InputEventList.Push(MoveTemp(Event));
			}
		}

		// Append our output events
		ModuleInstance.OutputEventList.Append(UpdateGraphContext.GetOutputEventList());
	}
}
