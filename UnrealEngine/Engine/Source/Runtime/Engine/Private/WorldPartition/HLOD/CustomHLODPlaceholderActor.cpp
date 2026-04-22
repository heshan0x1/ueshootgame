// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/CustomHLODPlaceholderActor.h"

#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/HLOD/CustomHLODPlaceholderActorDesc.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomHLODPlaceholderActor)

AWorldPartitionCustomHLODPlaceholder::AWorldPartitionCustomHLODPlaceholder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, CustomHLODActorDescInstance(nullptr)
#endif
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

#if WITH_EDITOR
void AWorldPartitionCustomHLODPlaceholder::InitFrom(const FWorldPartitionActorDescInstance* InCustomHLODActorDescInstance)
{
	CustomHLODActorDescInstance = InCustomHLODActorDescInstance;
	CustomHLODActorGuid = CustomHLODActorDescInstance->GetContainerInstance()->GetContainerID().GetActorGuid(CustomHLODActorDescInstance->GetGuid());

	SetActorTransform(CustomHLODActorDescInstance->GetActorTransform());
	SetRuntimeGrid(CustomHLODActorDescInstance->GetRuntimeGrid());
	SetIsSpatiallyLoaded(CustomHLODActorDescInstance->GetActorDesc()->GetIsSpatiallyLoadedRaw());
	bIsEditorOnlyActor = CustomHLODActorDescInstance->GetActorIsEditorOnly();
}

const FGuid& AWorldPartitionCustomHLODPlaceholder::GetCustomHLODActorGuid() const
{
	return CustomHLODActorGuid;
}

void AWorldPartitionCustomHLODPlaceholder::GetStreamingBounds(FBox& OutRuntimeBounds, FBox& OutEditorBounds) const
{
	if (CustomHLODActorDescInstance)
	{
		OutRuntimeBounds = CustomHLODActorDescInstance->GetActorDesc()->GetRuntimeBounds();
		OutEditorBounds = CustomHLODActorDescInstance->GetActorDesc()->GetEditorBounds();
	}
	else
	{
		OutRuntimeBounds = OutEditorBounds = FBox(EForceInit::ForceInit);
	}
}

TUniquePtr<FWorldPartitionActorDesc> AWorldPartitionCustomHLODPlaceholder::CreateClassActorDesc() const
{
	return TUniquePtr<FWorldPartitionActorDesc>(new FCustomHLODPlaceholderActorDesc());
}
#endif