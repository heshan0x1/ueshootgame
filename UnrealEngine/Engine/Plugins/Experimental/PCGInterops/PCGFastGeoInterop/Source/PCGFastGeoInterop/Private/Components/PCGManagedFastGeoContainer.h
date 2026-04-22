// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGManagedResource.h"

#include "FastGeoContainer.h"

#include "PCGManagedFastGeoContainer.generated.h"

struct FPCGProceduralISMComponentDescriptor;

UCLASS()
class UPCGManagedFastGeoContainer : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedResource interface
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	virtual bool ReleaseOnTeardown() const override { return true; }
	//~End UPCGManagedResource interface

	void SetFastGeoContainer(UFastGeoContainer* InFastGeo) { FastGeo = InFastGeo; }

	/** Set references to objects to be kept alive. */
	void SetObjectReferences(TArray<TObjectPtr<UObject>>&& InObjectReferences) { ObjectReferences = MoveTemp(InObjectReferences); }

protected:
	UPROPERTY(Transient)
	TObjectPtr<UFastGeoContainer> FastGeo;

	/** Referenced objects to be kept alive. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> ObjectReferences;
};
