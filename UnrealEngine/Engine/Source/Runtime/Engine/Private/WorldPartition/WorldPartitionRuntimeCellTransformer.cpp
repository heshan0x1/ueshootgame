// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCellTransformer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldPartitionRuntimeCellTransformer)

UWorldPartitionRuntimeCellTransformer::UWorldPartitionRuntimeCellTransformer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeCellTransformer::ForEachIgnoredComponentClass(TFunctionRef<bool(const TSubclassOf<UActorComponent>&)> Func) const
{
	for (const TSubclassOf<UActorComponent>& IgnoredComponentClass : GetDefault<UWorldPartitionRuntimeCellTransformerSettings>()->IgnoredComponentClasses)
	{
		if (!Func(IgnoredComponentClass))
		{
			return;
		}
	}
}

void UWorldPartitionRuntimeCellTransformer::ForEachIgnoredExactComponentClass(TFunctionRef<bool(const TSubclassOf<UActorComponent>&)> Func) const
{
	for (const TSubclassOf<UActorComponent>& IgnoredExactComponentClass : GetDefault<UWorldPartitionRuntimeCellTransformerSettings>()->IgnoredExactComponentClasses)
	{
		if (!Func(IgnoredExactComponentClass))
		{
			return;
		}
	}
}

bool UWorldPartitionRuntimeCellTransformer::CanIgnoreComponent(const UActorComponent* InComponent) const
{
	bool bCanIgnore = false;
	const UClass* ComponentClass = InComponent->GetClass();

	ForEachIgnoredComponentClass([&bCanIgnore, &ComponentClass](const TSubclassOf<UActorComponent>& IgnoredComponentClass)
	{
		if (ComponentClass->IsChildOf(IgnoredComponentClass))
		{
			bCanIgnore = true;
			return false;
		}
		return true;
	});

	if (bCanIgnore)
	{
		return true;
	}

	ForEachIgnoredExactComponentClass([&bCanIgnore, &ComponentClass](const TSubclassOf<UActorComponent>& IgnoredExactComponentClass)
	{
		if (ComponentClass == IgnoredExactComponentClass)
		{
			bCanIgnore = true;
			return false;
		}
		return true;
	});

	return bCanIgnore;
}
#endif

const FName UWorldPartitionRuntimeCellTransformer::NAME_CellTransformerIgnoreActor(TEXT("CellTransformer_IgnoreActor"));
