// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextActorComponentReferenceComponent.h"

#include "Component/AnimNextComponent.h"
#include "Module/AnimNextModuleInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextActorComponentReferenceComponent)

void FAnimNextActorComponentReferenceComponent::OnInitializeHelper(UScriptStruct* InScriptStruct)
{
	check(InScriptStruct->IsChildOf(FAnimNextActorComponentReferenceComponent::StaticStruct()));

	FAnimNextModuleInstance* ModuleInstance = GetModuleInstancePtr();
	if (ModuleInstance == nullptr)
	{
		return;
	}

	UAnimNextComponent* AnimNextComponent = Cast<UAnimNextComponent>(ModuleInstance->GetObject());

	FAnimNextModuleInstance::RunTaskOnGameThread(
		[WeakAnimNextComponent = TWeakObjectPtr<UAnimNextComponent>(AnimNextComponent), ComponentType = ComponentType, InScriptStruct]()
		{
			UAnimNextComponent* PinnedAnimNextComponent = WeakAnimNextComponent.Get();
			if(PinnedAnimNextComponent == nullptr)
			{
				return;
			}

			AActor* Owner = PinnedAnimNextComponent->GetOwner();
			if(Owner == nullptr)
			{
				return;
			}

			UActorComponent* FoundComponent = Owner->FindComponentByClass(ComponentType);
			PinnedAnimNextComponent->QueueTask(NAME_None, [FoundComponent, InScriptStruct](const UE::UAF::FModuleTaskContext& InContext)
			{
				InContext.TryAccessComponent(InScriptStruct, [FoundComponent](FUAFModuleInstanceComponent& InComponent)
				{
					static_cast<FAnimNextActorComponentReferenceComponent&>(InComponent).Component = FoundComponent;
				});
			});
		});
}
