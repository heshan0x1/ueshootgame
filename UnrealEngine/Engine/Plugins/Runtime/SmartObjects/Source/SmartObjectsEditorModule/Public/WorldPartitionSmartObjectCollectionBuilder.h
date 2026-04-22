// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartitionSmartObjectCollectionBuilder.generated.h"

#define UE_API SMARTOBJECTSEDITORMODULE_API

class ASmartObjectPersistentCollection;
enum class EEditorBuildResult : uint8;

/**
 * WorldPartitionBuilder dedicated to collect all smart object components from a world and store them in the collection.
 */
UCLASS(MinimalAPI)
class UWorldPartitionSmartObjectCollectionBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()

	static UE_API bool CanBuildCollections(const UWorld* InWorld, FName BuildOption);
	static UE_API EEditorBuildResult BuildCollections(UWorld* InWorld, FName BuildOption);

protected:
	virtual bool RequiresCommandletRendering() const override { return false; }
	virtual ELoadingMode GetLoadingMode() const override { return IterativeCells; }
	UE_API virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) override;
	UE_API virtual bool PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;
	UE_API virtual bool PostRun(UWorld* World, FPackageSourceControlHelper& PackageHelper, const bool bInRunSuccess) override;

	TArray<uint32> NumSmartObjectsBefore;
	TArray<uint32> OriginalContentsHash;
	uint32 NumSmartObjectsTotal = 0;

	bool bRemoveEmptyCollections = false;

	TArray<FWorldPartitionReference> SmartObjectReferences;
};

#undef UE_API
