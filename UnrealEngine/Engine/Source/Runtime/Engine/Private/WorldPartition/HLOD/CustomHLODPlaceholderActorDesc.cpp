// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/CustomHLODPlaceholderActorDesc.h"

#include "WorldPartition/HLOD/CustomHLODPlaceholderActor.h"

#if WITH_EDITOR
void FCustomHLODPlaceholderActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	if (!bIsDefaultActorDesc)
	{
		if (const AWorldPartitionCustomHLODPlaceholder* PlaceholderActor = CastChecked<AWorldPartitionCustomHLODPlaceholder>(InActor))
		{
			CustomHLODActorGuid = PlaceholderActor->GetCustomHLODActorGuid();
		}
	}
}

bool FCustomHLODPlaceholderActorDesc::Equals(const FWorldPartitionActorDesc* Other) const
{
	if (FWorldPartitionActorDesc::Equals(Other))
	{
		const FCustomHLODPlaceholderActorDesc* CustomHLODPlaceholderActorDesc = (FCustomHLODPlaceholderActorDesc*)Other;
		return CustomHLODActorGuid == CustomHLODPlaceholderActorDesc->CustomHLODActorGuid;
	}
	return false;
}

const FGuid& FCustomHLODPlaceholderActorDesc::GetCustomHLODActorGuid() const
{
	return CustomHLODActorGuid;
}

uint32 FCustomHLODPlaceholderActorDesc::GetSizeOf() const
{
	return sizeof(FCustomHLODPlaceholderActorDesc);
}

void FCustomHLODPlaceholderActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);

	Ar << CustomHLODActorGuid;
}
#endif