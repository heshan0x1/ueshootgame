// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTransitionDefaultLayerTags.h"

namespace UE::AvaTransition
{

FDefaultTags::FDefaultTags()
{
	TSoftObjectPtr<UAvaTagCollection> TagCollection(FSoftObjectPath(TEXT("/Avalanche/Tags/DefaultTransitionLayerTags.DefaultTransitionLayerTags")));
	DefaultLayer = FAvaTagSoftHandle(TagCollection, FAvaTagId(FGuid(0x89084F7E, 0x4D80443E, 0xAC48609D, 0x7A348C8A)));
}

const FDefaultTags& FDefaultTags::Get()
{
	static const FDefaultTags DefaultTags;
	return DefaultTags;
}

} // UE::AvaTransition
