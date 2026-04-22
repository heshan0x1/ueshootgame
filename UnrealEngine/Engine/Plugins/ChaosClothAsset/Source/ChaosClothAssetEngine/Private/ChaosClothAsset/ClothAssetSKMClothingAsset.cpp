// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothAssetSKMClothingAsset.h"
#include "ChaosClothAsset/ClothAssetBase.h"
#include "ChaosClothAsset/ClothAssetPrivate.h"
#include "ChaosClothAsset/ClothSimulationModel.h"

#if WITH_EDITOR
#include "Chaos/CollectionPropertyFacade.h"
#include "ChaosClothAsset/ClothEngineTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Utils/ClothingMeshUtils.h"
#include "ClothingAsset.h"
#endif  // #if WITH_EDITOR

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothAssetSKMClothingAsset)

#define LOCTEXT_NAMESPACE "ChaosClothAssetSKMClothingAsset"

namespace UE::Chaos::ClothAsset::Private
{
	static FString MakeClothSimulationModelIdString(const FName Name, const FGuid Guid)
	{
		return FString::Format(TEXT("{0}-{1}"), { Name.ToString(), Guid.ToString() });
	}

#if WITH_EDITOR
	// Modified version of FLODUtilities::UnbindClothingAndBackup to only unbind specified assets
	static void UnbindClothingAndBackup(USkeletalMesh& SkeletalMesh, const UChaosClothAssetSKMClothingAsset& ClothingAsset, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings)
	{
		ClothingBindings.Reset();

		if (SkeletalMesh.GetImportedModel())
		{
			auto UnbindClothingLODAndBackup = [&SkeletalMesh, &ClothingAsset](const int32 LODIndex, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingLODBindings)
				{
					ClothingLODBindings.Reset();

					TIndirectArray<FSkeletalMeshLODModel>& LODModels = SkeletalMesh.GetImportedModel()->LODModels;
					if (LODModels.IsValidIndex(LODIndex))
					{
						FSkeletalMeshLODModel& LODModel = LODModels[LODIndex];

						// Store this LOD's bindings
						ClothingAssetUtils::GetAllLodMeshClothingAssetBindings(&SkeletalMesh, ClothingLODBindings, LODIndex);  // TODO: We only need the binding for the specified ClothingAsset

						// Unbind this Cloth's LOD
						for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingLODBindings)
						{
							if (Binding.LODIndex == LODIndex && Binding.Asset == &ClothingAsset)
							{
								const int32 OriginalDataSectionIndex = LODModel.Sections[Binding.SectionIndex].OriginalDataSectionIndex;
								Binding.Asset->UnbindFromSkeletalMesh(&SkeletalMesh, Binding.LODIndex);
								Binding.SectionIndex = OriginalDataSectionIndex;

								FSkelMeshSourceSectionUserData& SectionUserData = LODModel.UserSectionsData.FindChecked(OriginalDataSectionIndex);
								SectionUserData.ClothingData.AssetGuid = FGuid();
								SectionUserData.ClothingData.AssetLodIndex = INDEX_NONE;
								SectionUserData.CorrespondClothAssetIndex = INDEX_NONE;
							}
						}
					}
				};

			for (int32 LODIndex = 0; LODIndex < SkeletalMesh.GetImportedModel()->LODModels.Num(); ++LODIndex)
			{
				TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingLODBindings;
				UnbindClothingLODAndBackup(LODIndex, ClothingLODBindings);
				ClothingBindings.Append(ClothingLODBindings);
			}
		}
	}

	// Modified version of FLODUtilities::RestoreClothingFromBackup to only rebind specified assets
	static void RestoreClothingFromBackup(USkeletalMesh& SkeletalMesh, const UChaosClothAssetSKMClothingAsset& ClothingAsset, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings)
	{
		if (SkeletalMesh.GetImportedModel() && ClothingAsset.GetClothSimulationModelIndex() != INDEX_NONE)
		{
			auto RestoreClothingLODFromBackup = [&SkeletalMesh, &ClothingAsset](const int32 LODIndex, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings)
				{
					TIndirectArray<FSkeletalMeshLODModel>& LODModels = SkeletalMesh.GetImportedModel()->LODModels;
					if (LODModels.IsValidIndex(LODIndex))
					{
						FSkeletalMeshLODModel& LODModel = SkeletalMesh.GetImportedModel()->LODModels[LODIndex];
						for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
						{
							if (Binding.LODIndex == LODIndex && Binding.Asset == &ClothingAsset)
							{
								for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
								{
									if (LODModel.Sections[SectionIndex].OriginalDataSectionIndex == Binding.SectionIndex)
									{
										if (Binding.Asset->BindToSkeletalMesh(&SkeletalMesh, Binding.LODIndex, SectionIndex, Binding.AssetInternalLodIndex))
										{
											// If successfull set back the section user data
											FSkelMeshSourceSectionUserData& SectionUserData = LODModel.UserSectionsData.FindChecked(Binding.SectionIndex);
											SectionUserData.CorrespondClothAssetIndex = LODModel.Sections[SectionIndex].CorrespondClothAssetIndex;
											SectionUserData.ClothingData = LODModel.Sections[SectionIndex].ClothingData;
										}
										break;  // Found the section, next binding
									}
								}
							}
						}
					}
				};

			for (int32 LODIndex = 0; LODIndex < SkeletalMesh.GetImportedModel()->LODModels.Num(); ++LODIndex)
			{
				RestoreClothingLODFromBackup(LODIndex, ClothingBindings);
			}
		}
	}
#endif
}

UChaosClothAssetSKMClothingAsset::UChaosClothAssetSKMClothingAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UClothingAssetBase::AssetGuid = FGuid::NewGuid();
}

void UChaosClothAssetSKMClothingAsset::PostLoad()
{
	Super::PostLoad();

	CalculateLODHasAnySimulationMeshData();
}

#if WITH_EDITOR
void UChaosClothAssetSKMClothingAsset::SetAsset(const UChaosClothAssetBase* InAsset)
{
	if (Asset != InAsset)
	{
		Asset = InAsset;
		OnAssetChanged();
	}
}

void UChaosClothAssetSKMClothingAsset::OnAssetChanged(const bool bReregisterComponents)
{
	// If the asset has changed, check whether the current specified model still exists in this new asset, otherwise update the model guid
	const FGuid ClothSimulationModelGuid = GetClothSimulationModelGuid();
	FGuid ModelGuid;
	int32 ModelIndex = INDEX_NONE;

	if (Asset)
	{
		if (const int32 NumClothSimulationModels = Asset->GetNumClothSimulationModels())
		{
			// Find a matching model GUID
			for (ModelIndex = 0; ModelIndex < NumClothSimulationModels; ++ModelIndex)
			{
				if (Asset->GetAssetGuid(ModelIndex) == ClothSimulationModelGuid)
				{
					break;
				}
			}

			if (ModelIndex == NumClothSimulationModels)
			{
				// Try locate a model with a similar name to update to a valid GUID if possible
				const FName ClothSimulationModelName = GetClothSimulationModelName();

				for (ModelIndex = 0; ModelIndex < Asset->GetNumClothSimulationModels(); ++ModelIndex)
				{
					if (Asset->GetClothSimulationModelName(ModelIndex) == ClothSimulationModelName)
					{
						break;
					}
				}

				if (ModelIndex == NumClothSimulationModels)
				{
					// Or reset to the first model if there isn't one
					ModelIndex = 0;
				}
			}

			if (ModelIndex != INDEX_NONE)
			{
				ModelGuid = Asset->GetAssetGuid(ModelIndex);
			}
		}
	}

	// Notify the model has changed
	if (ClothSimulationModelIndex != ModelIndex || ClothSimulationModelGuid != ModelGuid)
	{
		ClothSimulationModelIndex = ModelIndex;

		OnModelChanged();
	}
	else
	{
		CalculateLODHasAnySimulationMeshData();
	}

	if (bReregisterComponents)
	{
		if (const USkeletalMesh* const OwnerMesh = Cast<USkeletalMesh>(GetOuter()))
		{
			for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
			{
				if (USkeletalMeshComponent* const Component = *It)
				{
					if (Component->GetSkeletalMeshAsset() == OwnerMesh)
					{
						FComponentReregisterContext Context(Component);
						// Context goes out of scope, causing Component to be re-registered
					}
				}
			}
		}  // TODO: Move UClothingAssetCommon::ReregisterComponentsUsingClothing() to a common place (in Utils/ClothingMeshUtils?)
	}
}

void UChaosClothAssetSKMClothingAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UChaosClothAssetSKMClothingAsset, Asset))
	{
		OnAssetChanged();
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UChaosClothAssetSKMClothingAsset, ClothSimulationModelId))
	{
		OnModelChanged();
	}
}

bool UChaosClothAssetSKMClothingAsset::BindToSkeletalMesh(USkeletalMesh* SkeletalMesh, const int32 MeshLodIndex, const int32 SectionIndex, const int32 AssetLodIndex)
{
	using namespace UE::Chaos::ClothAsset;

	if (!ensure(SkeletalMesh) ||
		!ensure(SkeletalMesh == GetOuter()) ||
		!ensure(SkeletalMesh->GetImportedModel()) ||
		!ensure(SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(MeshLodIndex)) ||
		!ensure(SkeletalMesh->GetImportedModel()->LODModels[MeshLodIndex].Sections.IsValidIndex(SectionIndex)) ||
		!ensure(MeshLodIndex == AssetLodIndex))
	{
		return false;
	}

	// When FScopedSkeletalMeshPostEditChange goes out of scope it causes the postedit change and components to be re-registered and the mesh to rebuild
	FScopedSkeletalMeshPostEditChange SkeletalMeshPostEditChange(SkeletalMesh);

	// Get the original render section
	FSkeletalMeshLODModel& SkeletalMeshLODModel = SkeletalMesh->GetImportedModel()->LODModels[MeshLodIndex];
	FSkelMeshSection& SkelMeshSection = SkeletalMeshLODModel.Sections[SectionIndex];

	// Clear the proxy deformer mappings data now in case the cloth simulation model is invalid
	SkelMeshSection.ClothMappingDataLODs.Reset();

	// Set the asset guid
	SkelMeshSection.ClothingData.AssetGuid = AssetGuid;
	SkelMeshSection.ClothingData.AssetLodIndex = AssetLodIndex;

	// Set the asset index, used during rendering to pick the correct sim mesh buffer
	int32 AssetIndex = INDEX_NONE;
	verify(SkeletalMesh->GetMeshClothingAssets().Find(this, AssetIndex));
	SkelMeshSection.CorrespondClothAssetIndex = (int16)AssetIndex;

	// Retrieve the cloth simulation model
	if (!Asset)
	{
		const FText Text = FText::Format(
			LOCTEXT("NoAssetHasBeenSet", "Clothing Data [{0}]: no Asset has been set."),
			FText::FromString(GetName()));
		WarningNotification(Text);
		return true;  // Must return true, this is to avoid breaking the binding when the cloth simulation model is invalid
	}
	if (ClothSimulationModelIndex == INDEX_NONE)
	{
		const FText Text = FText::Format(
			LOCTEXT("NotAClothSimulationModelOfAsset", "Clothing Data [{0}]: [{1}] is not an existing Cloth Simulation Model of Asset [{2}]."),
			FText::FromString(GetName()),
			FText::FromName(GetClothSimulationModelName()),
			FText::FromName(Asset->GetFName()));
		WarningNotification(Text);
		return true;  // Must return true, this is to avoid breaking the binding when the cloth simulation model is invalid
	}
	const TSharedPtr<const FChaosClothSimulationModel> ClothSimulationModel = Asset->GetClothSimulationModel(ClothSimulationModelIndex);
	if (!ClothSimulationModel ||
		!ClothSimulationModel->GetNumVertices(AssetLodIndex) ||
		!ClothSimulationModel->GetNumTriangles(AssetLodIndex))
	{
		const FText Text = FText::Format(
			LOCTEXT("EmptyClothSimulationModel", "Clothing Data [{0}]: [{1}] is an empty Cloth Simulation Model of Asset [{2}]."),
			FText::FromString(GetName()),
			FText::FromName(GetClothSimulationModelName()),
			FText::FromName(Asset->GetFName()));
		WarningNotification(Text);
		return true;  // Must return true, this is to avoid breaking the binding when the cloth simulation model is invalid
	}
	if (!ClothSimulationModel->IsValidLodIndex(AssetLodIndex))
	{
		const FText Text = FText::Format(
			LOCTEXT("NoLODInClothSimulationModel", "Clothing Data [{0}]: Cloth Simulation Model [{1}] has no LOD{2} in Asset [{3}]."),
			FText::FromString(GetName()),
			FText::FromName(GetClothSimulationModelName()),
			AssetLodIndex,
			FText::FromName(Asset->GetFName()));
		WarningNotification(Text);
		return true;  // Must return true, this is to avoid breaking the binding when the cloth simulation model is invalid
	}

	// Set the new section bone map but first verify that it doesn't exceed the maximum when adding the cloth asset bones
	TArray<FBoneIndexType> BoneMap = SkelMeshSection.BoneMap;
	for (const FName BoneName : ClothSimulationModel->UsedBoneNames)
	{
		const int32 BoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			BoneMap.AddUnique(BoneIndex);
		}
	}

	if (BoneMap.Num() > FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones())
	{
		// Failed to apply as we've exceeded the number of bones we can skin
		const FText Text = FText::Format(
			LOCTEXT("TooManyBonesInClothSimulationModel", "Failed to bind Clothing Data [{0}] using asset [{1}], as this causes the section to require {2} bones. The maximum per section is currently {3}."),
			FText::FromString(GetName()),
			FText::FromName(Asset->GetFName()),
			BoneMap.Num(),
			FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones());
		WarningNotification(Text);
		return false;
	}

	SkelMeshSection.BoneMap = MoveTemp(BoneMap);

	bool bRequireBoneChange = false;
	for (FBoneIndexType& BoneIndex : SkelMeshSection.BoneMap)
	{
		if (!SkeletalMeshLODModel.RequiredBones.Contains(BoneIndex))
		{
			bRequireBoneChange = true;
			if (SkeletalMesh->GetRefSkeleton().IsValidIndex(BoneIndex))
			{
				SkeletalMeshLODModel.RequiredBones.Add(BoneIndex);
				SkeletalMeshLODModel.ActiveBoneIndices.AddUnique(BoneIndex);
			}
		}
	}
	if (bRequireBoneChange)
	{
		SkeletalMeshLODModel.RequiredBones.Sort();
		SkeletalMesh->GetRefSkeleton().EnsureParentsExistAndSort(SkeletalMeshLODModel.ActiveBoneIndices);
	}

	// Calculate the deformer mappings
	const int32 NumIndices = SkelMeshSection.NumTriangles * 3;
	const int32 NumVertices = SkelMeshSection.SoftVertices.Num();
	const uint32 BaseIndex = SkelMeshSection.BaseIndex;
	const uint32 BaseVertexIndex = SkelMeshSection.BaseVertexIndex;

	TArray<FVector3f> RenderPositions;
	TArray<FVector3f> RenderNormals;
	TArray<FVector3f> RenderTangents;
	RenderPositions.Reserve(NumVertices);
	RenderNormals.Reserve(NumVertices);
	RenderTangents.Reserve(NumVertices);
	for (const FSoftSkinVertex& SoftSkinVertex : SkelMeshSection.SoftVertices)
	{
		RenderPositions.Add(SoftSkinVertex.Position);
		RenderNormals.Add(SoftSkinVertex.TangentZ);
		RenderTangents.Add(SoftSkinVertex.TangentX);
	}

	const TConstArrayView<uint32> SectionRenderIndices(SkeletalMeshLODModel.IndexBuffer.GetData() + BaseIndex, NumIndices);
	TArray<uint32> RenderIndices;
	RenderIndices.Reserve(SectionRenderIndices.Num());
	for (const uint32 SectionRenderIndex : SectionRenderIndices)
	{
		RenderIndices.Add(SectionRenderIndex - BaseVertexIndex);
	}

	const ClothingMeshUtils::ClothMeshDesc TargetMesh(RenderPositions, RenderNormals, RenderTangents, RenderIndices);
	const ClothingMeshUtils::ClothMeshDesc SourceMesh(
		ClothSimulationModel->GetPositions(AssetLodIndex),
		ClothSimulationModel->GetIndices(AssetLodIndex));  // Let it calculate the averaged normals as to match the simulation data output

	const TSharedRef<const FManagedArrayCollection>& Collection = Asset->GetCollections(ClothSimulationModelIndex)[AssetLodIndex];
	FCollectionClothConstFacade ClothFacade(Collection);
	::Chaos::Softs::FCollectionPropertyConstFacade PropertyFacade(Collection);
	const FPointWeightMap MaxDistances = FClothEngineTools::GetMaxDistanceWeightMap(ClothFacade, PropertyFacade, ClothSimulationModel->GetNumVertices(AssetLodIndex));

	constexpr bool bSmoothTransition = true;
	constexpr bool bUseMultipleInfluences = false;
	constexpr float SkinningKernelRadius = 100.f;
	TArray<FMeshToMeshVertData> MeshToMeshData;
	ClothingMeshUtils::GenerateMeshToMeshVertData(
		MeshToMeshData, TargetMesh, SourceMesh, &MaxDistances,
		bSmoothTransition, bUseMultipleInfluences, SkinningKernelRadius);

	SkelMeshSection.ClothMappingDataLODs.SetNum(1);
	SkelMeshSection.ClothMappingDataLODs[0] = MoveTemp(MeshToMeshData);

	// Update the extra cloth deformer mapping LOD bias using this cloth entry
	// TODO: Implement a generic form of LOD Bias mapping in ClothingAssetUtils (need to rework the clothing asset interface for this)
	//UpdateLODBiasMappings(SkeletalMesh, MeshLodIndex, SectionIndex);

	return true;
}

void UChaosClothAssetSKMClothingAsset::UnbindFromSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	if (SkeletalMesh)
	{
		ClothingAssetUtils::UnbindFromSkeletalMesh(*this, *SkeletalMesh);
	}
}

void UChaosClothAssetSKMClothingAsset::UnbindFromSkeletalMesh(USkeletalMesh* SkeletalMesh, const int32 MeshLodIndex)
{
	if (SkeletalMesh)
	{
		ClothingAssetUtils::UnbindFromSkeletalMesh(*this, *SkeletalMesh, MeshLodIndex);
	}
}

void UChaosClothAssetSKMClothingAsset::UpdateAllLODBiasMappings(USkeletalMesh* SkeletalMesh)
{
	// TODO: Implement a generic form of LOD Bias mapping in ClothingAssetUtils (need to rework the clothing asset interface for this)
}

void UChaosClothAssetSKMClothingAsset::OnModelChanged()
{
	// Update the ClothSimulationModelId dropdown property
	using namespace UE::Chaos::ClothAsset::Private;
	ClothSimulationModelId = (ClothSimulationModelIndex != INDEX_NONE) ?
		MakeClothSimulationModelIdString(Asset->GetClothSimulationModelName(ClothSimulationModelIndex), Asset->GetAssetGuid(ClothSimulationModelIndex)) :
		FString();

	// Unbind/rebind clothing with the new GUID
	if (USkeletalMesh* const SkeletalMesh = Cast<USkeletalMesh>(GetOuter()))
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);  // Prevent the skeletal mesh from rebuilding until the unbind/rebind operation is complete

		TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
		UnbindClothingAndBackup(*SkeletalMesh, *this, ClothingBindings);
		RestoreClothingFromBackup(*SkeletalMesh, *this, ClothingBindings);
	}
	CalculateLODHasAnySimulationMeshData();
}

FGuid UChaosClothAssetSKMClothingAsset::GetClothSimulationModelGuid() const
{
	int32 DashIndex;
	return ClothSimulationModelId.FindLastChar(TEXT('-'), DashIndex) ?
		FGuid(ClothSimulationModelId.RightChop(DashIndex + 1)) : FGuid();
}

FName UChaosClothAssetSKMClothingAsset::GetClothSimulationModelName() const
{
	int32 DashIndex;
	return ClothSimulationModelId.FindLastChar(TEXT('-'), DashIndex) ?
		FName(ClothSimulationModelId.LeftChop(ClothSimulationModelId.Len() - DashIndex)) : FName();
}
#endif  // #if WITH_EDITOR

TArray<FString> UChaosClothAssetSKMClothingAsset::GetClothSimulationModelIds() const
{
	using namespace UE::Chaos::ClothAsset::Private;

	TArray<FString> ClothSimulationModelIds;
	if (Asset)
	{
		const int32 NumSimulationModels = Asset->GetNumClothSimulationModels();
		ClothSimulationModelIds.Reserve(NumSimulationModels);
		for (int32 ModelIndex = 0; ModelIndex < NumSimulationModels; ++ModelIndex)
		{
			if (Asset->GetClothSimulationModel(ModelIndex).IsValid() && Asset->GetClothSimulationModel(ModelIndex)->GetNumLods())
			{
				ClothSimulationModelIds.Emplace(MakeClothSimulationModelIdString(
					Asset->GetClothSimulationModelName(ModelIndex),
					Asset->GetAssetGuid(ModelIndex)));
			}
		}
	}
	return ClothSimulationModelIds;
}

void UChaosClothAssetSKMClothingAsset::CalculateLODHasAnySimulationMeshData()
{
	LODHasAnySimulationMeshData.Reset();
	if (Asset && ClothSimulationModelIndex != INDEX_NONE)
	{
		if (const TSharedPtr<const FChaosClothSimulationModel> ClothSimulationModel = Asset->GetClothSimulationModel(ClothSimulationModelIndex))
		{
			LODHasAnySimulationMeshData.SetNumUninitialized(ClothSimulationModel->GetNumLods());
			for (int32 LODIndex = 0; LODIndex < ClothSimulationModel->GetNumLods(); ++LODIndex)
			{
				LODHasAnySimulationMeshData[LODIndex] = ClothSimulationModel->GetNumVertices(LODIndex) > 0 && ClothSimulationModel->GetNumTriangles(LODIndex) > 0;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
