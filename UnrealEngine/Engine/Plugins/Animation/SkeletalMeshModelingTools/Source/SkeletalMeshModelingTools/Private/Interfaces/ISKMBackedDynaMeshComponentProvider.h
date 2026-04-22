// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "ISKMBackedDynaMeshComponentProvider.generated.h"

class USkeletalMeshBackedDynamicMeshComponent;

UINTERFACE(MinimalAPI)
class USkeletalMeshBackedDynamicMeshComponentProvider :
	public UInterface
{
	GENERATED_BODY()
};


class ISkeletalMeshBackedDynamicMeshComponentProvider
{
	GENERATED_BODY()

public:
	virtual USkeletalMeshBackedDynamicMeshComponent* GetComponent(UObject* SourceObject) = 0;
	virtual bool ShouldCommitToSkeletalMeshOnToolCommit() = 0;
};
