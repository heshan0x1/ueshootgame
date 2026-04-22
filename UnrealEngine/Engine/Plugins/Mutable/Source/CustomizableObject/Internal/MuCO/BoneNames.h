// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Skeleton.h"

class UModelResources;


class FBoneNames
{
public:
	CUSTOMIZABLEOBJECT_API FBoneNames(const UModelResources& ModelResources);

	UE::Mutable::Private::FBoneName* Find(const FName& BoneName);
	
	UE::Mutable::Private::FBoneName FindOrAdd(const FName& BoneName);
	
private:
	TMap<FString, UE::Mutable::Private::FBoneName> BoneNamesMap;

	FCriticalSection CriticalSection;
};

