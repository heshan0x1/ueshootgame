// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphInterfaces/AnimNextNativeDataInterface_BlendSpacePlayer.h"
#include "Animation/BlendSpace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextNativeDataInterface_BlendSpacePlayer)

void FAnimNextNativeDataInterface_BlendSpacePlayer::BindToFactoryObject(const FBindToFactoryObjectContext& InContext)
{
	BlendSpace = CastChecked<UBlendSpace>(InContext.FactoryObject);
}
