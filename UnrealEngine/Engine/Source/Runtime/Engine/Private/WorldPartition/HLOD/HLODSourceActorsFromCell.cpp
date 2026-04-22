// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODSourceActorsFromCell.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODHashBuilder.h"
#include "Serialization/ArchiveCrc32.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"

#include "Editor/UnrealEd/Public/EditorLevelUtils.h" // MoveActorsToLevel
#include "WorldPartition/ContentBundle/ContentBundlePaths.h"
#include "WorldPartition/DataLayer/ExternalDataLayerHelper.h"

#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(HLODSourceActorsFromCell)

UWorldPartitionHLODSourceActorsFromCell::UWorldPartitionHLODSourceActorsFromCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

bool UWorldPartitionHLODSourceActorsFromCell::LoadSourceActors(bool& bOutDirty, UWorld* TargetWorld) const
{
	UPackage::WaitForAsyncFileWrites();

	bOutDirty = false;
	AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(GetOuter());
	UWorld* SourceWorld = HLODActor->GetWorld();

	FLinkerInstancingContext InstancingContext;
	InstancingContext.AddTag(ULevel::DontLoadExternalObjectsTag);
	InstancingContext.AddTag(ULevel::DontLoadExternalFoldersTag);

	// Don't do SoftObjectPath remapping for PersistentLevel actors because references can end up in different cells
	InstancingContext.SetSoftObjectPathRemappingEnabled(false);

	UPackage* SourcePackage = SourceWorld->GetPackage();
	UPackage* TargetPackage = TargetWorld->GetPackage();

	FName SourcePackageName = SourcePackage->GetFName();
	FName TargetPackageName = TargetPackage->GetFName();
	InstancingContext.AddPackageMapping(SourcePackageName, TargetPackageName);

	for (const FWorldPartitionRuntimeCellObjectMapping& CellObjectMapping : Actors)
	{
		if (ContentBundlePaths::IsAContentBundlePath(CellObjectMapping.ContainerPackage.ToString()) ||
			FExternalDataLayerHelper::IsExternalDataLayerPath(CellObjectMapping.ContainerPackage.ToString()))
		{
			check(CellObjectMapping.ContainerPackage != CellObjectMapping.WorldPackage);
			bool bIsContainerPackageAlreadyRemapped = InstancingContext.RemapPackage(CellObjectMapping.ContainerPackage) != CellObjectMapping.ContainerPackage;
			if (!bIsContainerPackageAlreadyRemapped)
			{
				InstancingContext.AddPackageMapping(CellObjectMapping.ContainerPackage, TargetPackageName);
			}
		}
	}

	TArray<FWorldPartitionRuntimeCellObjectMapping> ActorsCopy = Actors;

	FWorldPartitionLevelHelper::FPackageReferencer PackageReferencer;
	FWorldPartitionLevelHelper::FLoadActorsParams Params = FWorldPartitionLevelHelper::FLoadActorsParams()
		.SetOuterWorld(TargetWorld)
		.SetDestLevel(nullptr)
		.SetActorPackages(ActorsCopy)
		.SetPackageReferencer(&PackageReferencer)
		.SetCompletionCallback([&bOutDirty](bool bSucceeded) { bOutDirty = !bSucceeded; })
		.SetLoadAsync(false)
		.SetInstancingContext(MoveTemp(InstancingContext))
		.SetSilenceLoadFailures(true);

	if (FWorldPartitionLevelHelper::LoadActors(MoveTemp(Params)))
	{
		TArray<UPackage*> ModifiedPackages;
		FWorldPartitionLevelHelper::MoveExternalActorsToLevel(ActorsCopy, TargetWorld->PersistentLevel, ModifiedPackages);
		return true;
	}

	return false;
}

uint32 UWorldPartitionHLODSourceActorsFromCell::GetSourceActorsHash(const TArray<FWorldPartitionRuntimeCellObjectMapping>& InSourceActors)
{
	TArray<FWorldPartitionRuntimeCellObjectMapping>& MutableSourceActors(const_cast<TArray<FWorldPartitionRuntimeCellObjectMapping>&>(InSourceActors));

	FArchiveCrc32 Ar;
	for (FWorldPartitionRuntimeCellObjectMapping& Mapping : MutableSourceActors)
	{
		Ar << Mapping;
	}
	
	return Ar.GetCrc();
}

void UWorldPartitionHLODSourceActorsFromCell::ComputeHLODHash(FHLODHashBuilder& InHashBuilder) const
{
	Super::ComputeHLODHash(InHashBuilder);

	// Source Actors
	uint32 SourceActorsHash = GetSourceActorsHash(Actors);
	InHashBuilder.HashField(SourceActorsHash, TEXT("SourceActorsHash"));
}

void UWorldPartitionHLODSourceActorsFromCell::SetActors(const TArray<FWorldPartitionRuntimeCellObjectMapping>&& InSourceActors)
{
	Actors = InSourceActors;
}

const TArray<FWorldPartitionRuntimeCellObjectMapping>& UWorldPartitionHLODSourceActorsFromCell::GetActors() const
{
	return Actors;
}

#endif // #if WITH_EDITOR
