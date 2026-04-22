// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteAssemblyStaticMeshBuilder.h"
#include "AssetToolsModule.h"
#include "PackageTools.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"

UNaniteAssemblyStaticMeshBuilder::UNaniteAssemblyStaticMeshBuilder(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
}

UNaniteAssemblyStaticMeshBuilder* UNaniteAssemblyStaticMeshBuilder::BeginNewStaticMeshAssemblyBuild(
	const FNaniteAssemblyCreateNewParameters& Parameters
)
{
	if (UStaticMesh* TargetMesh = CreateNewMeshForAssemblyBuild<UStaticMesh>(Parameters))
	{
		return BeginEditStaticMeshAssemblyBuild(TargetMesh);
	}

	return nullptr;
}

UNaniteAssemblyStaticMeshBuilder* UNaniteAssemblyStaticMeshBuilder::BeginEditStaticMeshAssemblyBuild(UStaticMesh* TargetMesh)
{
	if (TargetMesh == nullptr)
	{
		UE_LOG(LogNaniteAssemblyBuilder, Error,
			TEXT("[BeginEditStaticMeshAssemblyBuild] Target Static Mesh is not valid."));
		return nullptr;
	}

	UNaniteAssemblyStaticMeshBuilder* Builder = NewObject<UNaniteAssemblyStaticMeshBuilder>();
	if (!Builder->BeginAssemblyBuild(TargetMesh))
	{
		return nullptr;
	}

	return Builder;
}

bool UNaniteAssemblyStaticMeshBuilder::BeginAssemblyBuild(UStaticMesh* InTargetMesh)
{
	check(InTargetMesh != nullptr);

	if (!ValidateBuildStatus(TEXT("BeginStaticMeshAssemblyBuild"), false))
	{
		return false;
	}

	TargetMesh = InTargetMesh;

	// Add any materials on the target mesh to the default slot group
	for (const auto& Material : TargetMesh->GetStaticMaterials())
	{
		AddMaterialSlot(Material.MaterialInterface.Get(), 0, Material.MaterialSlotName);
	}

	return true;
}

bool UNaniteAssemblyStaticMeshBuilder::FinishAssemblyBuild(UStaticMesh*& OutStaticMesh)
{
	OutStaticMesh = TargetMesh;
	if (!ValidateBuildStatus(TEXT("FinishStaticMeshAssemblyBuild"), true))
	{
		return false;
	}

	UStaticMesh::FCommitMeshDescriptionParams Params;
	Params.bMarkPackageDirty = true;
	if (!Builder.ApplyToStaticMesh(*TargetMesh, Params))
	{
		UE_LOG(LogNaniteAssemblyBuilder, Display,
			TEXT("[FinishAssemblyBuild] Assembly build failed. No assembly parts were added."),
			*TargetMesh->GetPathName());
		return false;
	}

	TargetMesh->GetNaniteSettings().bEnabled = true;
	TargetMesh->PostEditChange();
	
	TargetMesh = nullptr;
	Reset();

	return true;
}

bool UNaniteAssemblyStaticMeshBuilder::AddAssemblyParts(
	const UStaticMesh* PartMesh,
	TArrayView<const FTransform> LocalTransforms,
	const FNaniteAssemblyMaterialMergeOptions& MaterialMergeOptions
)
{
	if (!ValidateCandidatePartMesh(PartMesh))
	{
		return false;
	}

	auto [PartIndex, FirstNodeIndex, bNewPart] = AddParts(FSoftObjectPath(PartMesh), LocalTransforms, MaterialMergeOptions);

	if (bNewPart)
	{
		// We need to merge and remap this part mesh's materials
		int32 SourceMaterialIndex = 0;
		for (const auto& Material : PartMesh->GetStaticMaterials())
		{
			TObjectPtr<UMaterialInterface> MaterialInterface = Material.MaterialInterface;
			if (MaterialMergeOptions.MaterialOverrides.IsValidIndex(SourceMaterialIndex))
			{
				MaterialInterface = MaterialMergeOptions.MaterialOverrides[SourceMaterialIndex];
			}
			const FNaniteAssemblyDataBuilder::FMaterialSlot NewSlot(
				MaterialInterface,
				Material.MaterialSlotName,
				MaterialMergeOptions.MaterialSlotGroup
			);
			const int32 MaterialIndex = MergeMaterialSlot(SourceMaterialIndex, NewSlot, MaterialMergeOptions.MergeBehavior);
			Builder.RemapPartMaterial(PartIndex, SourceMaterialIndex, MaterialIndex);

			++SourceMaterialIndex;
		}
	}

	return true;
}

bool UNaniteAssemblyStaticMeshBuilder::AddAssemblyPartsFromComponent(
	const UStaticMeshComponent* PartComponent,
	const FTransform& OriginTransform,
	const USceneComponent* OriginObject,
	const FNaniteAssemblyMaterialMergeOptions& MaterialMergeOptions
)
{
	UStaticMesh* PartMesh = PartComponent->GetStaticMesh();
	if (!ValidateCandidatePartMesh(PartMesh))
	{
		return false;
	}

	TArray<FTransform, TInlineAllocator<1>> LocalTransforms;
	if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(PartComponent))
	{
		for (int32 i = 0; i < ISM->GetInstanceCount(); ++i)
		{
			ISM->GetInstanceTransform(i, LocalTransforms.AddDefaulted_GetRef(), true);
		}
	}
	else
	{
		LocalTransforms.Add(PartComponent->GetComponentToWorld());
	}

	FTransform ParentTransform = OriginTransform;
	if (OriginObject)
	{
		ParentTransform *= OriginObject->GetComponentToWorld();
	}

	for (auto& Transform : LocalTransforms)
	{
		Transform.SetToRelativeTransform(ParentTransform);
	}

	auto [PartIndex, FirstNodeIndex, bNewPart] = AddParts(FSoftObjectPath(PartMesh), LocalTransforms, MaterialMergeOptions);

	if (bNewPart)
	{
		// We need to merge and remap this part mesh's materials, but use the component's overrides
		TArray<UMaterialInterface*> Materials = PartComponent->GetMaterials();
		TArray<FName> MaterialSlotNames = PartComponent->GetMaterialSlotNames();
		for (int32 SourceMaterialIndex = 0; SourceMaterialIndex < Materials.Num(); ++SourceMaterialIndex)
		{
			TObjectPtr<UMaterialInterface> MaterialInterface = Materials[SourceMaterialIndex];
			if (MaterialMergeOptions.MaterialOverrides.IsValidIndex(SourceMaterialIndex))
			{
				MaterialInterface = MaterialMergeOptions.MaterialOverrides[SourceMaterialIndex];
			}
			const FNaniteAssemblyDataBuilder::FMaterialSlot NewSlot(
				MaterialInterface,
				MaterialSlotNames[SourceMaterialIndex],
				MaterialMergeOptions.MaterialSlotGroup
			);
			const int32 MaterialIndex = MergeMaterialSlot(SourceMaterialIndex, NewSlot, MaterialMergeOptions.MergeBehavior);
			Builder.RemapPartMaterial(PartIndex, SourceMaterialIndex, MaterialIndex);
		}
	}

	return true;
}
