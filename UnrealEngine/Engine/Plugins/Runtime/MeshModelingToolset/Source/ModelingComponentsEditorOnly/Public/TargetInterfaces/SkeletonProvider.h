// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "SkeletonProvider.generated.h"


struct FReferenceSkeleton;

UINTERFACE(MinimalAPI)
class USkeletonProvider : public UInterface
{
	GENERATED_BODY()
};

class ISkeletonProvider
{
	GENERATED_BODY()

public:
	virtual FReferenceSkeleton GetSkeleton() const = 0;
};