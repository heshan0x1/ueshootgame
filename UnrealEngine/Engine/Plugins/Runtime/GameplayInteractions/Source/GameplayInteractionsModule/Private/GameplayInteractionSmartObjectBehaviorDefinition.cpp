// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "StateTree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayInteractionSmartObjectBehaviorDefinition)

#if WITH_EDITOR
void UGameplayInteractionSmartObjectBehaviorDefinition::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	if (UStateTree* StateTree = StateTreeReference.GetMutableStateTree())
	{
		OutDeps.Add(StateTree);
	}
}
#endif //WITH_EDITOR
