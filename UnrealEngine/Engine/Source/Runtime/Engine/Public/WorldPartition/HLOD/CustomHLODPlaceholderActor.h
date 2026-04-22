// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Misc/Guid.h"

#include "CustomHLODPlaceholderActor.generated.h"

class FWorldPartitionActorDescInstance;

UCLASS(MinimalAPI, NotPlaceable, Transient)
class AWorldPartitionCustomHLODPlaceholder : public AActor
{
	GENERATED_BODY()

public:
	ENGINE_API AWorldPartitionCustomHLODPlaceholder(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	void InitFrom(const FWorldPartitionActorDescInstance* InCustomHLODActorDescInstance);
	virtual void GetStreamingBounds(FBox& OutRuntimeBounds, FBox& OutEditorBounds) const override;
	ENGINE_API virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;
	ENGINE_API const FGuid& GetCustomHLODActorGuid() const;

private:
	FGuid CustomHLODActorGuid;
	const FWorldPartitionActorDescInstance* CustomHLODActorDescInstance;
#endif
};