// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphInterfaces/AnimNextNativeDataInterface_AnimSequencePlayer.h"
#include "Animation/AnimSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextNativeDataInterface_AnimSequencePlayer)

void FAnimNextNativeDataInterface_AnimSequencePlayer::BindToFactoryObject(const FBindToFactoryObjectContext& InContext)
{
	AnimSequence = CastChecked<UAnimSequence>(InContext.FactoryObject);
}
