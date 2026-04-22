// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMTrait_ModuleEventDependency_ActorComponentPrimaryTickFunction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMTrait_ModuleEventDependency_ActorComponentPrimaryTickFunction)

#if WITH_EDITOR
FString FRigVMTrait_ModuleEventDependency_ActorComponentPrimaryTickFunction::GetDisplayName() const
{
	return StaticStruct()->GetDisplayNameText().ToString();
}
#endif

void FRigVMTrait_ModuleEventDependency_ActorComponentPrimaryTickFunction::OnAddDependency(const UE::UAF::FModuleDependencyContext& InContext) const
{
	UActorComponent* AnimNextComponent = Cast<UActorComponent>(InContext.Object);
	if(AnimNextComponent == nullptr)
	{
		return;
	}

	if(Component == nullptr)
	{
		return;
	}

	if(Ordering == EAnimNextModuleEventDependencyOrdering::Before)
	{
		InContext.TickFunction.AddPrerequisite(Component, Component->PrimaryComponentTick);
	}
	else
	{
		Component->PrimaryComponentTick.AddPrerequisite(AnimNextComponent, InContext.TickFunction);
	}
}

void FRigVMTrait_ModuleEventDependency_ActorComponentPrimaryTickFunction::OnRemoveDependency(const UE::UAF::FModuleDependencyContext& InContext) const
{
	UActorComponent* AnimNextComponent = Cast<UActorComponent>(InContext.Object);
	if(AnimNextComponent == nullptr)
	{
		return;
	}
	
	if(Component == nullptr)
	{
		return;
	}

	if(Ordering == EAnimNextModuleEventDependencyOrdering::Before)
	{
		InContext.TickFunction.RemovePrerequisite(Component, Component->PrimaryComponentTick);
	}
	else
	{
		Component->PrimaryComponentTick.RemovePrerequisite(AnimNextComponent, InContext.TickFunction);
	}
}
