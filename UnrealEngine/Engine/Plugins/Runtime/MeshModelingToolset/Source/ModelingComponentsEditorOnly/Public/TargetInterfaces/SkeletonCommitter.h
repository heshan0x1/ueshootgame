// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "SkeletonCommitter.generated.h"


class USkeletonModifier;

UINTERFACE(MinimalAPI)
class USkeletonCommitter: public UInterface
{
	GENERATED_BODY()
};

class ISkeletonCommitter
{
	GENERATED_BODY()

public:
	virtual void SetupSkeletonModifier(USkeletonModifier* InModifier) = 0;
	virtual void CommitSkeletonModifier(USkeletonModifier* InModifier) = 0;
};