// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextGraphInstance.h"

#include "AnimNextAnimGraphStats.h"
#include "ObjectTrace.h"
#include "UAFRigVMComponent.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextGraphContextData.h"
#include "Graph/AnimNextGraphLatentPropertiesContextData.h"
#include "TraitCore/ExecutionContext.h"
#include "Graph/GC_GraphInstanceComponent.h"
#include "Graph/RigUnit_AnimNextShimRoot.h"
#include "Graph/RigVMTrait_AnimNextPublicVariables.h"
#include "Logging/StructuredLog.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModuleInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextGraphInstance)

DEFINE_STAT(STAT_AnimNext_Graph_RigVM);

FAnimNextGraphInstance::FAnimNextGraphInstance()
{
}

FAnimNextGraphInstance::~FAnimNextGraphInstance()
{
	TRACE_INSTANCE_LIFETIME_END(ModuleInstance ? ModuleInstance->GetObject() : nullptr, GetUniqueId());
	Release();
}

void FAnimNextGraphInstance::Release()
{
#if WITH_EDITORONLY_DATA
	if(const UAnimNextAnimationGraph* Graph = GetAnimationGraph())
	{
		FScopeLock Lock(&Graph->GraphInstancesLock);
		Graph->GraphInstances.Remove(this);
	}
#endif

	if (!GraphInstancePtr.IsValid())
	{
		return;
	}

	GraphInstancePtr.Reset();
	ModuleInstance = nullptr;
	RootGraphInstance = nullptr;
	ReleaseComponents();
	Asset = nullptr;
}

bool FAnimNextGraphInstance::IsValid() const
{
	return GraphInstancePtr.IsValid();
}

const UAnimNextAnimationGraph* FAnimNextGraphInstance::GetAnimationGraph() const
{
	return CastChecked<UAnimNextAnimationGraph>(Asset, ECastCheckedType::NullAllowed);
}

FName FAnimNextGraphInstance::GetEntryPoint() const
{
	return EntryPoint;
}

UE::UAF::FWeakTraitPtr FAnimNextGraphInstance::GetGraphRootPtr() const
{
	return GraphInstancePtr;
}

FAnimNextModuleInstance* FAnimNextGraphInstance::GetModuleInstance() const
{
	return ModuleInstance;
}

FAnimNextGraphInstance* FAnimNextGraphInstance::GetParentGraphInstance() const
{
	if(IsRoot())
	{
		return nullptr;
	}
	else
	{
		return static_cast<FAnimNextGraphInstance*>(HostInstance);
	}
}

FAnimNextGraphInstance* FAnimNextGraphInstance::GetRootGraphInstance() const
{
	return RootGraphInstance;
}

bool FAnimNextGraphInstance::UsesAnimationGraph(const UAnimNextAnimationGraph* InAnimationGraph) const
{
	return GetAnimationGraph() == InAnimationGraph;
}

bool FAnimNextGraphInstance::UsesEntryPoint(FName InEntryPoint) const
{
	if(const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph())
	{
		if(InEntryPoint == NAME_None)
		{
			return EntryPoint == AnimationGraph->DefaultEntryPoint;
		}

		return InEntryPoint == EntryPoint;
	}
	return false;
}

bool FAnimNextGraphInstance::IsRoot() const
{	
	return this == RootGraphInstance;
}

bool FAnimNextGraphInstance::HasUpdated() const
{
	return bHasUpdatedOnce;
}

void FAnimNextGraphInstance::MarkAsUpdated()
{
	bHasUpdatedOnce = true;
}

void FAnimNextGraphInstance::ExecuteLatentPins(const TConstArrayView<UE::UAF::FLatentPropertyHandle>& LatentHandles, void* DestinationBasePtr, bool bIsFrozen, bool bJustBecameRelevant)
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Graph_RigVM);

	if (!IsValid())
	{
		return;
	}

	if (URigVM* VM = GetAnimationGraph()->RigVM)
	{
		FUAFRigVMComponent& RigVMComponent = GetComponent<FUAFRigVMComponent>();
		FRigVMExtendedExecuteContext& ExtendedExecuteContext = RigVMComponent.GetExtendedExecuteContext();
		FAnimNextExecuteContext& AnimNextContext = ExtendedExecuteContext.GetPublicDataSafe<FAnimNextExecuteContext>();
		if (ModuleInstance)
		{
			AnimNextContext.SetOwningObject(ModuleInstance->GetObject());
		}

		// Insert our context data for the scope of execution
		FAnimNextGraphLatentPropertiesContextData ContextData(ModuleInstance, this, LatentHandles, DestinationBasePtr, bIsFrozen, bJustBecameRelevant);
		UE::UAF::FScopedExecuteContextData ContextDataScope(AnimNextContext, ContextData);

		VM->ExecuteVM(ExtendedExecuteContext, FRigUnit_AnimNextShimRoot::EventName);
	}
}

void FAnimNextGraphInstance::CopyVariablesToLatentPins(const TConstArrayView<UE::UAF::FLatentPropertyHandle>& LatentHandles, void* DestinationBasePtr, bool bIsFrozen, bool bJustBecameRelevant)
{
	for (UE::UAF::FLatentPropertyHandle Handle : LatentHandles)
	{
		if (!Handle.IsIndexValid())
		{
			// This handle isn't valid
			continue;
		}

		if (!Handle.IsOffsetValid())
		{
			// This handle isn't valid
			continue;
		}

		if (bIsFrozen && Handle.CanFreeze())
		{
			// This handle can freeze and we are frozen, no need to update it
			continue;
		}

		if (!bJustBecameRelevant && Handle.OnBecomeRelevant())
		{
			// This handle should only update on become relevant
			continue;
		}

		uint8* DestinationPtr = (uint8*)DestinationBasePtr + Handle.GetLatentPropertyOffset();
		AccessVariablePropertyByIndex(Handle.GetLatentPropertyIndex(), [DestinationPtr](const FProperty* InProperty, TArrayView<uint8> InData)
		{
			// Copy from our source into our destination
			// We assume the source and destination properties are identical
			check(InData.Num() == InProperty->GetElementSize());
			InProperty->CopyCompleteValue(DestinationPtr, InData.GetData());
		});
	}
}

#if WITH_EDITORONLY_DATA
void FAnimNextGraphInstance::Freeze()
{
	if (!IsValid())
	{
		return;
	}

	GraphInstancePtr.Reset();
	ReleaseComponents();
	bHasUpdatedOnce = false;
}

void FAnimNextGraphInstance::Thaw()
{
	if (const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph())
	{
		if (Variables.bHasBeenInitialized)
		{
			MigrateVariables();
		}
		else
		{
			InitializeVariables();
		}

		{
			UE::UAF::FExecutionContext Context(*this);
			if(const FAnimNextTraitHandle* FoundHandle = AnimationGraph->ResolvedRootTraitHandles.Find(EntryPoint))
			{
				GraphInstancePtr = Context.AllocateNodeInstance(*this, *FoundHandle);
			}
		}

		if (!IsValid())
		{
			// We failed to allocate our instance, clear everything
			Release();
		}
	}
}

#endif
