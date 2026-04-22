// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modifiers/Utilities/ActorModifierCoreUtilities.h"

#include "Modifiers/ActorModifierCoreComponent.h"
#include "Modifiers/ActorModifierCoreStack.h"

UActorModifierCoreBase* UE::ActorModifierCore::Utilities::FindFirstActorModifierByClass(const AActor* InStartActor, const TSubclassOf<UActorModifierCoreBase>& InModifierClass)
{
	UActorModifierCoreBase* FoundModifier = nullptr;

	if (!InStartActor || !InModifierClass)
	{
		return FoundModifier;
	}

	if (const UActorModifierCoreComponent* ModifierComponent = InStartActor->FindComponentByClass<UActorModifierCoreComponent>())
	{
		if (const UActorModifierCoreStack* ModifierStack = ModifierComponent->GetModifierStack())
		{
			FoundModifier = ModifierStack->FindModifier(InModifierClass.Get());
		}
	}

	if (FoundModifier)
	{
		return FoundModifier;
	}

	return FindFirstActorModifierByClass(InStartActor->GetAttachParentActor(), InModifierClass);
}
