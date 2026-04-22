// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchActorFactory.h"
#include "SwitchActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SwitchActorFactory)

USwitchActorFactory::USwitchActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = NSLOCTEXT("SwitchActorFactory", "SwitchActorDisplayName", "Switch Actor");
	NewActorClass = ASwitchActor::StaticClass();
}
