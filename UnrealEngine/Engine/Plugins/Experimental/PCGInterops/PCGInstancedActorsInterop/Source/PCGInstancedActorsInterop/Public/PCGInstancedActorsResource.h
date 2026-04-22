// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGManagedResource.h"

#include "InstancedActorsIndex.h"

#include "PCGInstancedActorsResource.generated.h"

#define UE_API PCGINSTANCEDACTORSINTEROP_API

UCLASS(MinimalAPI, BlueprintType)
class UPCGInstancedActorsManagedResource : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
	UE_API virtual void PostEditImport() override;
	//~End UObject interface

	//~Begin UPCGManagedResource interface
	UE_API virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	UE_API virtual bool ReleaseIfUnused(TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	UE_API virtual bool MoveResourceToNewActor(AActor* NewActor) override;
	UE_API virtual void MarkAsUsed() override;

#if WITH_EDITOR
	UE_API virtual void ChangeTransientState(EPCGEditorDirtyMode NewEditingMode) override;
	UE_API virtual void MarkTransientOnLoad() override;
#endif
	//~End UPCGManagedResource interface

public:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = GeneratedData)
	TArray<FInstancedActorsInstanceHandle> Handles;
};

#undef UE_API
