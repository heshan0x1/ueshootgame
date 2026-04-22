// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PVLineBatchComponent.h"
#include "PVSkeletonVisualizerComponent.generated.h"

struct FManagedArrayCollection;

UCLASS(Blueprintable, BlueprintType, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UPVSkeletonVisualizerComponent : public UPVLineBatchComponent
{
	GENERATED_BODY()

public:
	UPVSkeletonVisualizerComponent();

	const FManagedArrayCollection* GetCollection() const;
	void SetCollection(const FManagedArrayCollection* const InSkeletonCollection);

private:
	const FManagedArrayCollection* SkeletonCollection;
};
