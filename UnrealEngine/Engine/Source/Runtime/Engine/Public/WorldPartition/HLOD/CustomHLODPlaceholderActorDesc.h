// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
class FCustomHLODPlaceholderActorDesc : public FWorldPartitionActorDesc
{
public:
	virtual void Init(const AActor* InActor) override;
	virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;
	const FGuid& GetCustomHLODActorGuid() const;

protected:
	virtual uint32 GetSizeOf() const override;
	virtual void Serialize(FArchive& Ar) override;

	FGuid CustomHLODActorGuid;
};
#endif