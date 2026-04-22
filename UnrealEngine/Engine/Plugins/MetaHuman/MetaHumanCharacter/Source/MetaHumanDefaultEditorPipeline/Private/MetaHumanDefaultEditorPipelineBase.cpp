// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanDefaultEditorPipelineBase.h"

#include "Item/MetaHumanDefaultGroomPipeline.h"
#include "Item/MetaHumanGroomEditorPipeline.h"
#include "Item/MetaHumanGroomPipeline.h"
#include "Item/MetaHumanOutfitEditorPipeline.h"
#include "Item/MetaHumanOutfitPipeline.h"
#include "Item/MetaHumanSkeletalMeshEditorPipeline.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterInstance.h"
#include "MetaHumanCharacterPaletteUnpackHelpers.h"
#include "MetaHumanCharacterPipelineData.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCollection.h"
#include "MetaHumanDefaultEditorPipelineLog.h"
#include "MetaHumanDefaultPipelineBase.h"
#include "MetaHumanGeometryRemoval.h"
#include "MetaHumanWardrobeItem.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "ProjectUtilities/MetaHumanProjectUtilities.h"
#include "MetaHumanTypesEditor.h"
#include "MetaHumanCharacterPalette.h"
#include "IMetaHumanValidationContext.h"

#include "ChaosClothAsset/ClothAssetBase.h"
#include "ChaosOutfitAsset/BodyUserData.h"
#include "ChaosOutfitAsset/CollectionOutfitFacade.h"
#include "Dataflow/DataflowObject.h"
#include "Engine/SkeletalMesh.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomCreateFollicleMaskOptions.h"
#include "GroomTextureBuilder.h"

#include "Algo/Accumulate.h"
#include "Algo/Find.h"
#include "Algo/Contains.h"
#include "Algo/AnyOf.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/TG_AsyncExportTask.h"
#include "Editor/EditorEngine.h"
#include "EditorAssetLibrary.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "ImageCoreUtils.h"
#include "LODUtilities.h"
#include "Logging/StructuredLog.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterPaletteEditorModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/UObjectToken.h"
#include "Engine/Texture2D.h"
#include "PackageTools.h"
#include "Internationalization/Regex.h"
#include "TG_Material.h"
#include "TextureGraph.h"
#include "UObject/FastReferenceCollector.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/GCObjectScopeGuard.h"
#include "BlueprintCompilationManager.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "Misc/MessageDialog.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshModel.h"

#include "MetaHumanCharacterActorInterface.h"
#include "MetaHumanRigLogicUnpackLibrary.h"
#include "MetaHumanCommonDataUtils.h"

extern UNREALED_API UEditorEngine* GEditor;

#define LOCTEXT_NAMESPACE "MetaHumanDefaultEditorPipelineBase"

namespace UE::MetaHuman::Private
{
	static TAutoConsoleVariable<bool> CVarMHCEnableGCOnTextureBaking
	{
		TEXT("mh.Assembly.EnableGCOnTextureBaking"),
		true,
		TEXT("Set to true to run GC during the texture baking part of the assembly."),
		ECVF_Default
	};

	static constexpr TStaticArray<EFaceTextureType, 6> GetAnimatedMapTypes()
	{
		constexpr TStaticArray<EFaceTextureType, 6> AnimatedMapTypes =
		{
			EFaceTextureType::Basecolor_Animated_CM1,
			EFaceTextureType::Basecolor_Animated_CM2,
			EFaceTextureType::Basecolor_Animated_CM3,

			EFaceTextureType::Normal_Animated_WM1,
			EFaceTextureType::Normal_Animated_WM2,
			EFaceTextureType::Normal_Animated_WM3,
		};

		return AnimatedMapTypes;
	}

	/** Reparent the Skin LOD Materials so they form a hierarchy */
	static void ReparentSkinLODMaterials(const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets)
	{
		const FMetaHumanCharacterFaceMaterialSet NewFaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(InGeneratedAssets.FaceMesh);

		TArray<TObjectPtr<UMaterialInstance>> SkinMaterialChain;
		NewFaceMaterialSet.Skin.GenerateValueArray(SkinMaterialChain);

		for (int32 Index = 0; Index < SkinMaterialChain.Num() - 1; ++Index)
		{
			UMaterialInstance* NewParent = SkinMaterialChain[Index];
			UMaterialInstanceConstant* Material = Cast<UMaterialInstanceConstant>(SkinMaterialChain[Index + 1]);

			if (Material && NewParent)
			{
				FMetaHumanCharacterSkinMaterials::SetMaterialInstanceParent(Material, NewParent);
			}
		}
	}
	
	template<class T>
	static void WriteSkinMaterialValueToLODArray(TArray<T>& OutLODArray, EMetaHumanCharacterSkinMaterialSlot Slot, const T& Value)
	{
		switch (Slot)
		{
			case EMetaHumanCharacterSkinMaterialSlot::LOD0:
				OutLODArray[0] = Value;
				break;

			case EMetaHumanCharacterSkinMaterialSlot::LOD1:
				OutLODArray[1] = Value;
				break;

			case EMetaHumanCharacterSkinMaterialSlot::LOD2:
				OutLODArray[2] = Value;
				break;

			case EMetaHumanCharacterSkinMaterialSlot::LOD3:
				OutLODArray[3] = Value;
				break;

			case EMetaHumanCharacterSkinMaterialSlot::LOD4:
				OutLODArray[4] = Value;
				break;

			case EMetaHumanCharacterSkinMaterialSlot::LOD5to7:
				OutLODArray[5] = Value;
				OutLODArray[6] = Value;
				OutLODArray[7] = Value;
				break;

			default:
				checkNoEntry();
		}
	}

	static const FName OutfitResize_TargetBodyPropertyName("TargetBody");
	static const FName OutfitResize_ResizableOutfitPropertyName("ResizableOutfit");
}

void FMetaHumanMaterialBakingOptions::RefreshTextureResolutionsOverrides()
{
	if (UMetaHumanMaterialBakingSettings* BakingSettingsPtr = BakingSettings.LoadSynchronous())
	{
		TMap<FName, EMetaHumanBuildTextureResolution> OutputTextures;
		for (const FMetaHumanTextureGraphOutputProperties& Graph : BakingSettingsPtr->TextureGraphs)
		{
			for (const FMetaHumanOutputTextureProperties& OutputTexture : Graph.OutputTextures)
			{
				EMetaHumanBuildTextureResolution OverrideResolution = EMetaHumanBuildTextureResolution::Res1024;

				// Searches the texture graph to obtain the resolution set in the in the output
				for (const TPair<FTG_Id, FTG_OutputSettings>& OutputNode : Graph.TextureGraphInstance->OutputSettingsMap)
				{
					const int32 PinIndex = 3;
					const FTG_Id PinId(OutputNode.Key.NodeIdx(), PinIndex);

					const FName OutputParamName = Graph.TextureGraphInstance->Graph()->GetParamName(PinId);

					if (OutputParamName == OutputTexture.OutputTextureNameInGraph)
					{
						const FTG_OutputSettings& OutputSettings = OutputNode.Value;
						if (OutputSettings.Width == OutputSettings.Height)
						{
							const int32 OutputResolution = (int32) OutputSettings.Width;
							if (((int32) EMetaHumanBuildTextureResolution::Res256) <= OutputResolution && OutputResolution <= ((int32) EMetaHumanBuildTextureResolution::Res8192))
							{
								OverrideResolution = (EMetaHumanBuildTextureResolution) OutputResolution;
							}
						}

						break;
					}
				}

				OutputTextures.FindOrAdd(OutputTexture.OutputTextureName) = OverrideResolution;
			}
		}

		// Remove the entries that are not in the baking settings object anymore but keep the ones that are already in the map
		for (TMap<FName, EMetaHumanBuildTextureResolution>::TIterator It = TextureResolutionsOverrides.CreateIterator(); It; ++It)
		{
			if (!OutputTextures.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}

		// Add the new ones
		TextureResolutionsOverrides.Append(OutputTextures);
	}
}

UMetaHumanDefaultEditorPipelineBase::UMetaHumanDefaultEditorPipelineBase()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
	Specification->BuildInputStruct = FMetaHumanBuildInputBase::StaticStruct();

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd("Hair");
		Slot.BuildInputStruct = FMetaHumanGroomPipelineBuildInput::StaticStruct();
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd("Eyebrows");
		Slot.BuildInputStruct = FMetaHumanGroomPipelineBuildInput::StaticStruct();
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd("Beard");
		Slot.BuildInputStruct = FMetaHumanGroomPipelineBuildInput::StaticStruct();
	}
		
	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd("Mustache");
		Slot.BuildInputStruct = FMetaHumanGroomPipelineBuildInput::StaticStruct();
	}
		
	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd("Eyelashes");
		Slot.BuildInputStruct = FMetaHumanGroomPipelineBuildInput::StaticStruct();
	}
				
	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd("Peachfuzz");
		Slot.BuildInputStruct = FMetaHumanGroomPipelineBuildInput::StaticStruct();
	}

	FaceSkeleton = FSoftObjectPath(FMetaHumanCommonDataUtils::GetCharacterPluginFaceSkeletonPath());
	BodySkeleton = FSoftObjectPath(FMetaHumanCommonDataUtils::GetCharacterPluginBodySkeletonPath());
}

void UMetaHumanDefaultEditorPipelineBase::BuildCollection(
	TNotNull<const UMetaHumanCollection*> CharacterPalette,
	TNotNull<UObject*> OuterForGeneratedAssets,
	const TArray<FMetaHumanPinnedSlotSelection>& SortedPinnedSlotSelections,
	const TArray<FMetaHumanPaletteItemPath>& SortedItemsToExclude,
	const FInstancedStruct& BuildInput,
	EMetaHumanCharacterPaletteBuildQuality Quality,
	ITargetPlatform* TargetPlatform,
	const FOnBuildComplete& OnComplete) const
{
	const FText SlowTaskMessage = Quality == EMetaHumanCharacterPaletteBuildQuality::Preview
		? LOCTEXT("BuildSlowTaskMessage_Preview", "Assembling Character for preview...")
		: LOCTEXT("BuildSlowTaskMessage_Production", "Assembling Character...");
	FScopedSlowTask SlowTask(1, SlowTaskMessage);
	SlowTask.MakeDialog();

	check(CharacterPalette->GetEditorPipeline() == this);

	// Some UObjects generated during the build are only referenced from the built data, and during
	// texture baking we force garbage collection to save memory, so the built data is temporarily 
	// stored in a UObject that's visible to GC to prevent the generated objects from being deleted.
	TStrongObjectPtr<UMetaHumanStructHost> BuiltDataHost = TStrongObjectPtr(NewObject<UMetaHumanStructHost>());
	FMetaHumanCollectionBuiltData* BuiltData = &BuiltDataHost->Struct.InitializeAs<FMetaHumanCollectionBuiltData>();
	BuiltData->Quality = Quality;

	const UMetaHumanDefaultPipelineBase* RuntimePipeline = Cast<UMetaHumanDefaultPipelineBase>(GetRuntimePipeline());
	if (!RuntimePipeline)
	{
		// Runtime pipeline must inherit from UMetaHumanDefaultPipelineBase
		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
		return;
	}

	FMetaHumanBuildInputBase Input;
	{
		const FMetaHumanBuildInputBase* InputPtr = BuildInput.GetPtr<FMetaHumanBuildInputBase>();
		if (InputPtr)
		{
			Input = *InputPtr;
		}
	}

	if (!CharacterPalette->GetPipeline()->GetSpecification()->IsValid())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error()
			->AddToken(FTextToken::Create(LOCTEXT("PipelineSpecInvalid", "The MetaHuman Character Pipeline's specification is invalid. This usually means there's an issue with the configuration of the pipeline slots.")));

		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
		return;
	}

	// TODO: More validations on the skeletons?

	// Actually try to load the skeletons to make sure they exist
	if (!FaceSkeleton.LoadSynchronous())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error()
			->AddToken(FTextToken::Create(LOCTEXT("InvalidFaceSkeleton", "A valid Face Skeleton is required to run the pipeline")));

		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
		return;
	}

	if (!BodySkeleton.LoadSynchronous())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error()
			->AddToken(FTextToken::Create(LOCTEXT("InvalidBodySkeleton", "A valid Body Skeleton is required to run the pipeline")));

		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
		return;
	}

	// Create a temporary object to reference this data, so that it's visible to GC and temporary
	// assets referenced from it won't be deleted.
	TStrongObjectPtr<UCharacterPipelineDataMap> CharacterPipelineDataMap = TStrongObjectPtr(NewObject<UCharacterPipelineDataMap>(GetTransientPackageAsObject(), NAME_None, RF_Transient));
	TMap<FMetaHumanPaletteItemKey, FCharacterPipelineData>& CharacterPipelineData = CharacterPipelineDataMap->Map;
	const bool bCanResizeOutfits = CanResizeOutfits();

	for (const FMetaHumanCharacterPaletteItem& Item : CharacterPalette->GetItems())
	{
		UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous();
		const FMetaHumanPaletteItemKey ItemKey(Item.GetItemKey());
		const FMetaHumanPaletteItemPath ItemPath(ItemKey);

		const UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(PrincipalAsset);

		if (Item.SlotName != UE::MetaHuman::CharacterPipelineSlots::Character
			|| !Character
			|| SortedItemsToExclude.Contains(ItemPath))
		{
			continue;
		}

		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterEditorSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

		if (Quality == EMetaHumanCharacterPaletteBuildQuality::Preview
			&& Input.EditorPreviewCharacter == ItemKey)
		{
			const FMetaHumanCharacterPreviewAssetOptions Options
			{
				.bGenerateMergedHeadAndBodyMesh = bCanResizeOutfits,
				.bGenerateBodyMeasurements = bCanResizeOutfits
			};

			FMetaHumanCharacterPreviewAssets PreviewAssets;
			if (MetaHumanCharacterEditorSubsystem->TryGetCharacterPreviewAssets(Character, Options, PreviewAssets))
			{
				FCharacterPipelineData& PipelineData = CharacterPipelineData.Add(ItemKey);
				PipelineData.FaceMesh = PreviewAssets.FaceMesh;
				PipelineData.BodyMesh = PreviewAssets.BodyMesh;
				PipelineData.MergedHeadAndBody = PreviewAssets.MergedHeadAndBodyMesh;

				// no skin transfer is required when no rig is available as animation is disabled anyhow
				PipelineData.bTransferSkinWeights = Character->HasFaceDNA();
				PipelineData.bStripSimMesh = !Character->HasFaceDNA();

				PipelineData.FaceMaterialChangesPerLOD.AddZeroed(PipelineData.FaceMesh->GetLODNum());
			}
			else
			{
				FMessageLog(UE::MetaHuman::MessageLogName).Error()
					->AddToken(FTextToken::Create(LOCTEXT("FailedGeneratePreviewCharacterAssets", "Failed to generate preview assets for Character")));

				OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
				return;
			}
		}
		else
		{
			const FMetaHumanCharacterGeneratedAssetOptions Options
			{
				.bGenerateMergedHeadAndBodyMesh = bCanResizeOutfits,
				.bGenerateBodyMeasurements = bCanResizeOutfits
			};

			FMetaHumanCharacterGeneratedAssets GeneratedAssets;
			if (MetaHumanCharacterEditorSubsystem->TryGenerateCharacterAssets(Character, OuterForGeneratedAssets, Options, GeneratedAssets))
			{
				FCharacterPipelineData& PipelineData = CharacterPipelineData.Add(ItemKey);
				PipelineData.FaceMesh = GeneratedAssets.FaceMesh;
				PipelineData.BodyMesh = GeneratedAssets.BodyMesh;
				PipelineData.MergedHeadAndBody = GeneratedAssets.MergedHeadAndBodyMesh;

				{
					PipelineData.GeneratedAssets = GeneratedAssets;
					PipelineData.bIsGeneratedAssetsValid = true;
				}

				// Clear the merged mesh from the generated assets, as we don't want to pass it into the build output
				if (GeneratedAssets.MergedHeadAndBodyMesh)
				{
					PipelineData.GeneratedAssets.MergedHeadAndBodyMesh = nullptr;
					PipelineData.GeneratedAssets.RemoveAssetMetadata(GeneratedAssets.MergedHeadAndBodyMesh);
				}

				PipelineData.bTransferSkinWeights = true;
				PipelineData.bStripSimMesh = false;

				check(GeneratedAssets.FaceMesh);
				check(GeneratedAssets.BodyMesh);

				PipelineData.FaceMaterialChangesPerLOD.AddZeroed(PipelineData.FaceMesh->GetLODNum());

				TNotNull<USkeleton*> GeneratedFaceSkeleton = GenerateSkeleton(GeneratedAssets, FaceSkeleton.LoadSynchronous(), TEXT("Face"), OuterForGeneratedAssets);
				TNotNull<USkeleton*> GeneratedBodySkeleton = GenerateSkeleton(GeneratedAssets, BodySkeleton.LoadSynchronous(), TEXT("Body"), OuterForGeneratedAssets);

				GeneratedAssets.FaceMesh->SetSkeleton(GeneratedFaceSkeleton);
				GeneratedAssets.BodyMesh->SetSkeleton(GeneratedBodySkeleton);

				// Set the MH asset version to the assets that will be exported as is
				FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(GeneratedAssets.FaceMesh);
				FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(GeneratedAssets.BodyMesh);
				FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(GeneratedAssets.PhysicsAsset);

				const UMetaHumanMaterialBakingSettings* BakingSettings = FaceMaterialBakingOptions.BakingSettings.LoadSynchronous();

				if (bBakeMaterials
					&& BakingSettings != nullptr
					&& BakingSettings->LODBakingUtilityClass != nullptr)
				{
					ULODBakingUtility* LODBaking = NewObject<ULODBakingUtility>(GetTransientPackage(), BakingSettings->LODBakingUtilityClass);
					UGeometryScriptDebug* DebugObject = NewObject<UGeometryScriptDebug>(LODBaking);
					PipelineData.FaceBakedNormalsTextures = LODBaking->BakeTangentNormals(GeneratedAssets.FaceMesh, DebugObject);

					if (PipelineData.FaceBakedNormalsTextures.Num() < 3)
					{
						// TODO: Log the messages from the Debug Object, if any

						OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
						return;
					}
					else
					{
						PipelineData.GeneratedAssets.Metadata.Emplace(PipelineData.FaceBakedNormalsTextures[0], TEXT("Face/Baked"), TEXT("T_BakedNormal_LOD3"));
						PipelineData.GeneratedAssets.Metadata.Emplace(PipelineData.FaceBakedNormalsTextures[1], TEXT("Face/Baked"), TEXT("T_BakedNormal_LOD4"));
						PipelineData.GeneratedAssets.Metadata.Emplace(PipelineData.FaceBakedNormalsTextures[2], TEXT("Face/Baked"), TEXT("T_BakedNormal_LOD5"));
					}
				}

				RemoveLODsIfNeeded(PipelineData.GeneratedAssets, PipelineData.FaceRemovedMaterialSlots);

				for (int32& ChangeCount : PipelineData.FaceMaterialChangesPerLOD)
				{
					ChangeCount = INDEX_NONE;
				}
				
				const FMetaHumanCharacterFaceMaterialSet FaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(GeneratedAssets.FaceMesh);
				FaceMaterialSet.ForEachSkinMaterial<UMaterialInstanceConstant>(
					[&PipelineData](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstanceConstant* Material)
					{
						UE::MetaHuman::Private::WriteSkinMaterialValueToLODArray(PipelineData.FaceMaterialChangesPerLOD, Slot, 0);
					});
			}
			else
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("Character"), FText::FromString(Character->GetName()));

				FMessageLog(UE::MetaHuman::MessageLogName).Error()
					->AddToken(FTextToken::Create(FText::Format(LOCTEXT("FailedGenerateCharacterAssets", "Failed to generate assets for Character {Character}"), Arguments)));

				OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
				return;
			}
		}
	}

	const FString TextureOutputFolder = FPackageName::GetLongPackagePath(CharacterPalette->GetUnpackFolder() / TEXT("Textures"));

	ProcessGroomAndClothSlots(
		CharacterPalette,
		*BuiltData,
		SortedPinnedSlotSelections,
		SortedItemsToExclude,
		Quality,
		TargetPlatform,
		OuterForGeneratedAssets,
		TextureOutputFolder,
		CharacterPipelineData);

	// Process Character slots
	for (const FMetaHumanCharacterPaletteItem& Item : CharacterPalette->GetItems())
	{
		UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous();
		const FMetaHumanPaletteItemKey ItemKey(Item.GetItemKey());
		const FMetaHumanPaletteItemPath ItemPath(ItemKey);

		if (Item.SlotName == NAME_None
			|| !PrincipalAsset
			|| SortedItemsToExclude.Contains(ItemPath))
		{
			continue;
		}

		const TOptional<FName> RealSlotName = GetRuntimePipeline()->GetSpecification()->ResolveRealSlotName(Item.SlotName);
		if (!ensure(RealSlotName.IsSet()))
		{
			// Since the spec was validated above, this shouldn't happen.
			//
			// Handle gracefully anyway by skipping this item.
			continue;
		}

		FMetaHumanPipelineBuiltData ItemBuiltData;
		FInstancedStruct& BuildOutput = ItemBuiltData.BuildOutput;
		ItemBuiltData.SlotName = RealSlotName.GetValue();

		if (Item.SlotName == UE::MetaHuman::CharacterPipelineSlots::Character)
		{
			FCharacterPipelineData& PipelineData = CharacterPipelineData[ItemKey];
			if (!PipelineData.bIsGeneratedAssetsValid)
			{
				FMetaHumanCharacterPartOutput& OutputStruct = BuildOutput.InitializeAs<FMetaHumanCharacterPartOutput>();
				OutputStruct.GeneratedAssets.FaceMesh = PipelineData.FaceMesh;
				OutputStruct.GeneratedAssets.BodyMesh = PipelineData.BodyMesh;
			}
			else
			{
				FMetaHumanCharacterGeneratedAssets& GeneratedAssets = PipelineData.GeneratedAssets;

				if (GeneratedAssets.FaceMesh
					&& (PipelineData.FollicleMap || HairProperties.UseFollicleMapMaterialParameterName != NAME_None))
				{
					// Either there's a follicle map, or a parameter we need to set to enable/disable the follicle map

					const bool bShouldSetFollicleMap = PipelineData.FollicleMap != nullptr && HairProperties.FollicleMapMaterialParameterName != NAME_None;

					TArray<FSkeletalMaterial>& MeshMaterials = GeneratedAssets.FaceMesh->GetMaterials();

					for (FName MaterialSlotName : HairProperties.FollicleMapMaterialSlotNames)
					{
						if (PipelineData.FaceRemovedMaterialSlots.Contains(MaterialSlotName))
						{
							// This slot has been intentionally removed, so don't search for it
							continue;
						}

						FSkeletalMaterial* FoundMaterial = Algo::FindBy(MeshMaterials, MaterialSlotName, &FSkeletalMaterial::MaterialSlotName);
						if (FoundMaterial)
						{
							if (Algo::FindBy(GeneratedAssets.Metadata, FoundMaterial->MaterialInterface, &FMetaHumanGeneratedAssetMetadata::Object))
							{
								if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(FoundMaterial->MaterialInterface))
								{
									if (HairProperties.UseFollicleMapMaterialParameterName != NAME_None)
									{
										MIC->SetStaticSwitchParameterValueEditorOnly(HairProperties.UseFollicleMapMaterialParameterName, bShouldSetFollicleMap);
									}

									if (bShouldSetFollicleMap)
									{
										MIC->SetTextureParameterValueEditorOnly(HairProperties.FollicleMapMaterialParameterName, PipelineData.FollicleMap);
									}
								}
								else
								{
									UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Can't set follicle map on material {Material}: Must be MaterialInstanceConstant", GetFullNameSafe(FoundMaterial->MaterialInterface));
								}
							}
							else
							{
								UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Can't set follicle map on material {Material}: Must be part of generated character assets", GetFullNameSafe(FoundMaterial->MaterialInterface));
							}
						}
						else
						{
							UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Can't set follicle map on material slot {MaterialSlot}: Slot not found on face mesh", MaterialSlotName);
						}
					}
				}

				auto TryRemoveHiddenGeometry = [&OnComplete](
					USkeletalMesh* Mesh,
					const TArray<UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture>& HiddenFaceMaps,
					TConstArrayView<FName> MaterialSlotsToProcess)
				{
					using namespace UE::MetaHuman::GeometryRemoval;

					if (!Mesh || HiddenFaceMaps.Num() == 0)
					{
						return true;
					}

					TArray<FHiddenFaceMapImage> Images;
					FText FailureReason;
					if (!TryConvertHiddenFaceMapTexturesToImages(HiddenFaceMaps, Images, FailureReason))
					{
						TArray<FString> HiddenFaceMapNames;
						Algo::Transform(HiddenFaceMaps, HiddenFaceMapNames, 
							[](const UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture& Map)
							{
								return Map.Texture->GetPathName();
							});

						UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Failed to convert hidden face map textures to images: {HiddenFaceMapNames}: {Reason}",
							FString::Join(HiddenFaceMapNames, TEXT(", ")), FailureReason.ToString());

						return false;
					}

					FHiddenFaceMapImage* ImageToUse = nullptr;
					FHiddenFaceMapImage CombinedImage;
					if (Images.Num() == 1)
					{
						ImageToUse = &Images[0];
					}
					else
					{
						if (!TryCombineHiddenFaceMaps(Images, CombinedImage, FailureReason))
						{
							TArray<FString> HiddenFaceMapNames;
							Algo::Transform(HiddenFaceMaps, HiddenFaceMapNames, 
								[](const UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture& Map)
								{
									return Map.Texture->GetPathName();
								});

							UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Failed to combine hidden face maps: {HiddenFaceMapNames}: {Reason}",
								FString::Join(HiddenFaceMapNames, TEXT(", ")), FailureReason.ToString());
							
							return false;
						}

						ImageToUse = &CombinedImage;
					}

					// The image should be valid if this code is reached
					check(ImageToUse != nullptr && ImageToUse->Image.GetNumPixels() > 0);

					FScopedSkeletalMeshPostEditChange ScopedPostEditChange(Mesh);
					const int32 NumLODs = Mesh->GetLODNum();

					for (int32 LODIndex = 0; LODIndex < NumLODs; LODIndex++)
					{
						if (!RemoveAndShrinkGeometry(
							Mesh,
							LODIndex,
							*ImageToUse,
							MaterialSlotsToProcess))
						{
							UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Failed to remove hidden geometry from {Mesh}", Mesh->GetPathName());
							
							return false;
						}
					}

					return true;
				};

				TArray<FName> SkinMaterialSlots;
				SkinMaterialSlots.Reserve(static_cast<int32>(EMetaHumanCharacterSkinMaterialSlot::Count));
				for (EMetaHumanCharacterSkinMaterialSlot SkinMaterialSlot : TEnumRange<EMetaHumanCharacterSkinMaterialSlot>())
				{
					SkinMaterialSlots.Add(FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(SkinMaterialSlot));
				}

				if (!TryRemoveHiddenGeometry(GeneratedAssets.FaceMesh, PipelineData.HeadHiddenFaceMaps, SkinMaterialSlots)
					|| !TryRemoveHiddenGeometry(GeneratedAssets.BodyMesh, PipelineData.BodyHiddenFaceMaps, TConstArrayView<FName>()))
				{
					OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
					return;
				}

				if (bBakeMaterials)
				{
					if (!ProcessBakedMaterials(TextureOutputFolder, *BuiltData, GeneratedAssets, OuterForGeneratedAssets, PipelineData))
					{
						OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
						return;
					}
				}

				UE::MetaHuman::Private::ReparentSkinLODMaterials(GeneratedAssets);

				// Downsize any textures if specified in the pipeline
				if (!MaxTextureResolutions.Face.IsEmpty())
				{
					for (const TPair<EFaceTextureType, EMetaHumanBuildTextureResolution>& TargetResolutionPair : MaxTextureResolutions.Face)
					{
						EFaceTextureType TextureType = TargetResolutionPair.Key;
						EMetaHumanBuildTextureResolution TargetResolution = TargetResolutionPair.Value;

						if (const TObjectPtr<UTexture2D>* FoundTexture = GeneratedAssets.SynthesizedFaceTextures.Find(TextureType))
						{
							FMetaHumanCharacterEditorBuild::DownsizeTexture(*FoundTexture, static_cast<int32>(TargetResolution), TargetPlatform);
						}
					}
				}

				// Set the post process anim blueprint
				if (IsValid(BodyProperties.PostProcessAnimBp.Get()))
				{
					GeneratedAssets.BodyMesh->SetPostProcessAnimBlueprint(BodyProperties.PostProcessAnimBp.Get());
				}

				if (BodyProperties.bUnpackRigLogic)
				{
					UAnimBlueprint* BodyPostProcessAnimBP = nullptr;
					if (IsValid(BodyProperties.PostProcessAnimBp.Get()))
					{
						BodyPostProcessAnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(BodyProperties.PostProcessAnimBp->GetPackage()->GetName()));
					}
						
					TArray<uint16> HalfRotationSolvers;
					if (BodyProperties.BodyRigLogicUnpackProperties.bUnpackRbfToPoseAssets)
					{
						TArray<FMetaHumanBodyRigLogicGeneratedAsset> OutGeneratedRigLogicAssets;
						UMetaHumanRigLogicUnpackLibrary::UnpackRBFEvaluation(
							BodyPostProcessAnimBP,
							GeneratedAssets.BodyMesh,
							OuterForGeneratedAssets,
							BodyProperties.BodyRigLogicUnpackProperties.bUnpackFingerHalfRotationsToControlRig,
							HalfRotationSolvers,
							OutGeneratedRigLogicAssets
							);

						for (const FMetaHumanBodyRigLogicGeneratedAsset& GeneratedRiglogicAsset: OutGeneratedRigLogicAssets)
						{
							GeneratedAssets.Metadata.Emplace(GeneratedRiglogicAsset.AnimSequence, "Body/RBF", "AS_"+GeneratedRiglogicAsset.SolverName);
							GeneratedAssets.Metadata.Emplace(GeneratedRiglogicAsset.PoseAsset, "Body/RBF", "PA_"+GeneratedRiglogicAsset.SolverName);
								
						}
						GeneratedAssets.BodyRigLogicAssets = OutGeneratedRigLogicAssets;
					}
						
					if (BodyProperties.BodyRigLogicUnpackProperties.bUnpackSwingTwistToControlRig || (BodyProperties.BodyRigLogicUnpackProperties.bUnpackRbfToPoseAssets && BodyProperties.BodyRigLogicUnpackProperties.bUnpackFingerHalfRotationsToControlRig))
					{
						TObjectPtr<UControlRigBlueprint> BodyControlRig = UMetaHumanRigLogicUnpackLibrary::UnpackControlRigEvaluation(
							BodyPostProcessAnimBP,
							GeneratedAssets.BodyMesh,
							BodyProperties.BodyRigLogicUnpackProperties.ControlRig,
							OuterForGeneratedAssets,
							BodyProperties.BodyRigLogicUnpackProperties.bUnpackFingerHalfRotationsToControlRig,
							HalfRotationSolvers
						);
						if (IsValid(BodyControlRig))
						{
							GeneratedAssets.Metadata.Emplace(BodyControlRig, "Body/Controls", "CR_Body_Procedural");
						}
					}
					// Update the body DNA user asset data to enable/disable rbf and swing twist evaluation
					if (UAssetUserData* UserData = GeneratedAssets.BodyMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass()))
					{
						UDNAAsset* DNAAsset = CastChecked<UDNAAsset>(UserData);
						if (IsValid(BodyProperties.PostProcessAnimBp.Get()))
						{
							DNAAsset->RigLogicConfiguration.LoadRBFBehavior = !BodyProperties.BodyRigLogicUnpackProperties.bUnpackRbfToPoseAssets;
						}
						DNAAsset->RigLogicConfiguration.LoadTwistSwingBehavior = !BodyProperties.BodyRigLogicUnpackProperties.bUnpackSwingTwistToControlRig;
					}
				}

				FMetaHumanCharacterPartOutput& OutputStruct = BuildOutput.InitializeAs<FMetaHumanCharacterPartOutput>();
				OutputStruct.GeneratedAssets = GeneratedAssets;
			}

			if (BuildOutput.IsValid())
			{
				BuiltData->PaletteBuiltData.ItemBuiltData.Add(FMetaHumanPaletteItemPath(Item.GetItemKey()), MoveTemp(ItemBuiltData));
			}
		}
	}

	TSharedRef<FMetaHumanCollectionBuiltData> BuildDataShared = MakeShared<FMetaHumanCollectionBuiltData>();
	*BuildDataShared = MoveTemp(*BuiltData);
	OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Succeeded, BuildDataShared);
}

bool UMetaHumanDefaultEditorPipelineBase::CanBuild() const
{
	return true;
}

bool UMetaHumanDefaultEditorPipelineBase::ProcessBakedMaterials(
	const FString& TextureOutputFolder, 
	FMetaHumanCollectionBuiltData& BuiltData,
	FMetaHumanCharacterGeneratedAssets& GeneratedAssets,
	TNotNull<UObject*> OuterForGeneratedAssets,
	FCharacterPipelineData& PipelineData)  const
{
	// Run all the TG baking in a separate scope in order to be able to guard from GC all the build generated assets up to this point
	// Leaving the scope, some of the textures in the generated assets may be removed, depending on the baking output
	{
		TArray<UObject*> GCGuardObjects;
		if (UE::MetaHuman::Private::CVarMHCEnableGCOnTextureBaking.GetValueOnAnyThread())
		{
			// Add the assets generated by the pipeline
			Algo::Transform(GeneratedAssets.Metadata, GCGuardObjects,
				[](const FMetaHumanGeneratedAssetMetadata& Metadata)
				{
					return Metadata.Object;
				});

			// Add any built data that have been created up to this point
			for (const TPair<FMetaHumanPaletteItemPath, FMetaHumanPipelineBuiltData>& ItemBuildDataPair : BuiltData.PaletteBuiltData.ItemBuiltData)
			{
				if (const FMetaHumanOutfitPipelineBuildOutput* OutfitOutputStruct = ItemBuildDataPair.Value.BuildOutput.GetPtr<FMetaHumanOutfitPipelineBuildOutput>())
				{
					// Separate handling of the outfit pipeline since it does not add the built objects to the metadata
					for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanOutfitGeneratedAssets>& Pair : OutfitOutputStruct->CharacterAssets)
					{
						if (Pair.Value.Outfit)
						{
							GCGuardObjects.Add(Pair.Value.Outfit);
						}
						if (Pair.Value.OutfitMesh)
						{
							GCGuardObjects.Add(Pair.Value.OutfitMesh);
						}
						if (Pair.Value.CombinedBodyMesh)
						{
							GCGuardObjects.Add(Pair.Value.CombinedBodyMesh);
						}
					}
				}
				else
				{
					Algo::Transform(ItemBuildDataPair.Value.Metadata, GCGuardObjects,
						[](const FMetaHumanGeneratedAssetMetadata& Metadata)
						{
							return Metadata.Object;
						});
				}
			}
		}

		TGCObjectsScopeGuard<UObject> GCGuard_Textures(GCGuardObjects);

		// Bake face textures if needed
		if (!FaceMaterialBakingOptions.BakingSettings.IsNull())
		{
			// Output to the unpack folder of defined by palette
			TArray<FSkeletalMaterial> FaceMeshMaterials = GeneratedAssets.FaceMesh->GetMaterials();
			{
				TArray<TObjectPtr<UTexture>> GeneratedTextures;

				if (!TryBakeMaterials(
					TextureOutputFolder,
					FaceMaterialBakingOptions,
					FaceMeshMaterials,
					PipelineData.FaceRemovedMaterialSlots,
					PipelineData.FaceMaterialChangesPerLOD,
					OuterForGeneratedAssets,
					GeneratedAssets,
					GeneratedTextures))
				{
					return false;
				}
			}

			GeneratedAssets.FaceMesh->SetMaterials(FaceMeshMaterials);
		}

		// Bake body textures if needed
		if (!BodyMaterialBakingOptions.BakingSettings.IsNull())
		{
			TArray<TObjectPtr<UTexture>> GeneratedTextures;

			TArray<FSkeletalMaterial> BodyMeshMaterials = GeneratedAssets.BodyMesh->GetMaterials();
			if (!TryBakeMaterials(
				TextureOutputFolder, 
				BodyMaterialBakingOptions, 
				BodyMeshMaterials, 
				TMap<FName, TObjectPtr<UMaterialInterface>>(), 
				TArray<int32>(), 
				OuterForGeneratedAssets, 
				GeneratedAssets,
				GeneratedTextures))
			{
				return false;
			}
			GeneratedAssets.BodyMesh->SetMaterials(BodyMeshMaterials);
		}
	}

	// Remove any face textures if needed
	if (!FaceMaterialBakingOptions.BakingSettings.IsNull())
	{
		// Ensure remove materials are not in the generated data
		// This is safe to do only after baking since potentially all materials will be used during baking
		for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : PipelineData.FaceRemovedMaterialSlots)
		{
			GeneratedAssets.RemoveAssetMetadata(Pair.Value.Get());
		}

		// If there is a follicle map, it's now baked into the face, so we can discard it
		if (PipelineData.FollicleMap)
		{
			GeneratedAssets.RemoveAssetMetadata(PipelineData.FollicleMap);
			PipelineData.FollicleMap = nullptr;
		}

		for (TObjectPtr<UTexture> Texture : PipelineData.PreBakedGroomTextures)
		{
			// Pre-baked groom textures are only needed by the unbaked skin material, and can now be deleted.
			FAssetRegistryModule::AssetDeleted(Texture);
			Texture->ClearFlags(RF_Public | RF_Standalone);
		}

		// When baking materials is enabled, Basecolor and Normal synthesized textures are not used, so remove them from the list of exported assets
		constexpr TStaticArray<EFaceTextureType, 3> TexturesToRemove =
		{
			EFaceTextureType::Basecolor,
			EFaceTextureType::Normal,
			EFaceTextureType::Cavity,
		};

		for (EFaceTextureType TextureType : TexturesToRemove)
		{
			// Maps may have been removed already
			if (GeneratedAssets.SynthesizedFaceTextures.Contains(TextureType))
			{
				UTexture2D* TextureToRemove = GeneratedAssets.SynthesizedFaceTextures[TextureType];
				GeneratedAssets.RemoveAssetMetadata(TextureToRemove);
				GeneratedAssets.SynthesizedFaceTextures.Remove(TextureType);
				TextureToRemove->MarkAsGarbage();
			}
		}

		// Apply the animated maps to the baked materials
		const FMetaHumanCharacterFaceMaterialSet BakedFaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(GeneratedAssets.FaceMesh);

		BakedFaceMaterialSet.ForEachSkinMaterial<UMaterialInstanceConstant>(
			[&GeneratedAssets, &PipelineData](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstanceConstant* BakedSkinMaterial)
			{
				for (EFaceTextureType AnimatedMapType : UE::MetaHuman::Private::GetAnimatedMapTypes())
				{
					if (const TObjectPtr<UTexture2D>* FoundAnimatedMap = GeneratedAssets.SynthesizedFaceTextures.Find(AnimatedMapType))
					{
						const TObjectPtr<UTexture2D> AnimatedMap = *FoundAnimatedMap;

						const bool bUseVTParameterName = AnimatedMap->IsCurrentlyVirtualTextured();
						const FName FaceTextureParameterName = FMetaHumanCharacterSkinMaterials::GetFaceTextureParameterName(AnimatedMapType, bUseVTParameterName);
						BakedSkinMaterial->SetTextureParameterValueEditorOnly(FaceTextureParameterName, AnimatedMap);
					}
				}

				UTexture2D* BakedLODNormal = nullptr;

				if (Slot == EMetaHumanCharacterSkinMaterialSlot::LOD3 && PipelineData.FaceBakedNormalsTextures.IsValidIndex(0))
				{
					BakedLODNormal = PipelineData.FaceBakedNormalsTextures[0];
				}
				else if (Slot == EMetaHumanCharacterSkinMaterialSlot::LOD4 && PipelineData.FaceBakedNormalsTextures.IsValidIndex(1))
				{
					BakedLODNormal = PipelineData.FaceBakedNormalsTextures[1];
				}
				else if (Slot == EMetaHumanCharacterSkinMaterialSlot::LOD5to7 && PipelineData.FaceBakedNormalsTextures.IsValidIndex(2))
				{
					BakedLODNormal = PipelineData.FaceBakedNormalsTextures[2];
				}

				if (IsValid(BakedLODNormal))
				{
					// TODO: Get this parameter name from FMetaHumanCharacterSkinMaterials
					BakedSkinMaterial->SetTextureParameterValueEditorOnly(TEXT("Normal LOD Baked"), BakedLODNormal);
				}
			}
		);
	}

	// Remove any body textures if needed
	if (!BodyMaterialBakingOptions.BakingSettings.IsNull())
	{
		// Remove source textures after baking
		for (EBodyTextureType TextureType : TEnumRange<EBodyTextureType>())
		{
			UTexture2D* TextureToRemove = GeneratedAssets.BodyTextures[TextureType];
			if (GeneratedAssets.RemoveAssetMetadata(TextureToRemove))
			{
				// Clean up textures that have been generated with metadata
				GeneratedAssets.BodyTextures.Remove(TextureType);
				TextureToRemove->MarkAsGarbage();
			}
		}
	}

	return true;
}

void UMetaHumanDefaultEditorPipelineBase::ProcessGroomAndClothSlots(
	TNotNull<const UMetaHumanCollection*> CharacterCollection, 
	FMetaHumanCollectionBuiltData& BuiltData,
	const TArray<FMetaHumanPinnedSlotSelection>& SortedPinnedSlotSelections,
	const TArray<FMetaHumanPaletteItemPath>& SortedItemsToExclude,
	EMetaHumanCharacterPaletteBuildQuality Quality,
	ITargetPlatform* TargetPlatform,
	TNotNull<UObject*> OuterForGeneratedAssets,
	const FString& TextureOutputFolder,
	TMap<FMetaHumanPaletteItemKey, FCharacterPipelineData>& CharacterPipelineData) const
{
	const int32 NumFaceLODs = 8;

	const bool bCanResizeOutfits = CanResizeOutfits();
	// Do some basic checks to see if a follicle map would be used
	const bool bGenerateFollicleMaps = HairProperties.FollicleMapMaterialParameterName != NAME_None && HairProperties.FollicleMapMaterialSlotNames.Num() > 0;

	// Data used to build a follicle map texture for the pinned groom selections
	TArray<FFollicleInfo> PinnedFollicleMapInfo;
	const TMap<FName, FFollicleInfo::EChannel> FollicleChannelMapping
	{
		{"Hair", FFollicleInfo::EChannel::R},
		{"Eyebrows", FFollicleInfo::EChannel::G},
		{"Beard", FFollicleInfo::EChannel::B},
		{"Mustache", FFollicleInfo::EChannel::B}
	};

	// Material instances used to pre-bake grooms to a set of textures, which is then read by the 
	// face materials. This reduces the number of texture samplers needed in the skin material.
	TMap<EMetaHumanCharacterSkinMaterialSlot, TStrongObjectPtr<UMaterialInstanceConstant>> PinnedBakedGroomMaterials;
	TArray<int32> PinnedBakedGroomLODToMaterial;
	TArray<UMaterialInstanceConstant*> PinnedBakedGroomMaterialArray;
	int32 FirstLODPinnedBakedGrooms = MAX_int32;
	// Only in production for now
	if (Quality == EMetaHumanCharacterPaletteBuildQuality::Production
		&& HairProperties.bUsePreBakedGrooms
		&& HairProperties.PreBakedGroomTextureGraphInstance
		&& HairProperties.PreBakedGroomGeneratorMaterial)
	{
		bool bIsSetupValid = HairProperties.PreBakedGroomTextureGraphOutputTextures.Num() > 0;
		for (const FMetaHumanOutputTextureProperties& OutputTexture : HairProperties.PreBakedGroomTextureGraphOutputTextures)
		{
			if (OutputTexture.bOutputsVirtualTexture)
			{
				UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "bOutputsVirtualTexture is true in pre-baked groom texture graph output settings. This is not supported.");

				bIsSetupValid = false;
				break;
			}

			if (OutputTexture.OutputTextureName == NAME_None)
			{
				UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "OutputTextureName is empty in pre-baked groom texture graph output settings. You must provide a texture name.");

				bIsSetupValid = false;
				break;
			}
		}

		if (bIsSetupValid)
		{
			PinnedBakedGroomMaterials.Reserve(static_cast<int32>(EMetaHumanCharacterSkinMaterialSlot::Count));

			// Set up some data structures used by SetFaceMaterialParameters
			PinnedBakedGroomLODToMaterial.Reserve(NumFaceLODs);
			for (int32 Index = 0; Index < NumFaceLODs; Index++)
			{
				PinnedBakedGroomLODToMaterial.Add(INDEX_NONE);
			}
		
			PinnedBakedGroomMaterialArray.Reserve(NumFaceLODs);

			for (const EMetaHumanCharacterSkinMaterialSlot Slot : TEnumRange<EMetaHumanCharacterSkinMaterialSlot>())
			{
				UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>();
				MIC->SetParentEditorOnly(HairProperties.PreBakedGroomGeneratorMaterial);

				PinnedBakedGroomMaterials.Add(Slot, TStrongObjectPtr(MIC));

				const int32 MaterialIndex = PinnedBakedGroomMaterialArray.Add(MIC);
				UE::MetaHuman::Private::WriteSkinMaterialValueToLODArray(PinnedBakedGroomLODToMaterial, Slot, MaterialIndex);
			}
		}
	}

	for (const FMetaHumanCharacterPaletteItem& Item : CharacterCollection->GetItems())
	{
		const FMetaHumanPaletteItemPath ItemPath(Item.GetItemKey());

		if (Item.SlotName == NAME_None
			|| Item.SlotName == UE::MetaHuman::CharacterPipelineSlots::Character
			|| !Item.WardrobeItem
			|| !IsWardrobeItemCompatibleWithSlot(Item.SlotName, Item.WardrobeItem)
			|| SortedItemsToExclude.Contains(ItemPath))
		{
			continue;
		}

		const TOptional<FName> RealSlotName = GetRuntimePipeline()->GetSpecification()->ResolveRealSlotName(Item.SlotName);
		if (!ensure(RealSlotName.IsSet())
			|| !ensure(GetRuntimePipeline()->GetSpecification()->Slots.Contains(RealSlotName.GetValue())))
		{
			// Since the spec was validated above, this shouldn't happen.
			//
			// Handle gracefully anyway by skipping this item.
			continue;
		}

		const UMetaHumanItemPipeline* ItemPipeline = nullptr;
		static_cast<void>(CharacterCollection->TryResolveItemPipeline(ItemPath, ItemPipeline));

		UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous();

		if (UGroomBindingAsset* GroomBinding = Cast<UGroomBindingAsset>(PrincipalAsset))
		{
			if (!ItemPipeline)
			{
				ItemPipeline = GetDefault<UMetaHumanGroomPipeline>();
			}

			const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
			if (!ItemEditorPipeline)
			{
				// Can't build this item without an editor pipeline

				// TODO: Log
				continue;
			}

			// TODO: Do this validation at the start of the build

			// Ensure the item pipeline produces build output that's compatible with the slot it's
			// assigned to.
			//
			// The item's output must be a superset of the slot's output, so that all the fields 
			// that the slot is expecting will be initialized.
			const UScriptStruct* ItemOutputStruct = ItemPipeline->GetSpecification()->BuildOutputStruct;
			const UScriptStruct* SlotOutputStruct = GetRuntimePipeline()->GetSpecification()->Slots[RealSlotName.GetValue()].BuildOutputStruct;
			if (!ItemOutputStruct)
			{
				// Items must produce build output in order to be valid

				// TODO: Log
				continue;
			}

			if (SlotOutputStruct
				&& ItemOutputStruct
				&& !ItemOutputStruct->IsChildOf(SlotOutputStruct))
			{
				// Item's output doesn't inherit from the slot's expected output, so this 
				// item isn't compatible with this slot.

				// TODO: Log
				continue;
			}

			const UScriptStruct* ItemInputStruct = ItemEditorPipeline->GetSpecification()->BuildInputStruct;
			const UScriptStruct* SlotInputStruct = Specification->SlotEditorData[RealSlotName.GetValue()].BuildInputStruct;
			check(SlotInputStruct == FMetaHumanGroomPipelineBuildInput::StaticStruct());
			if (ItemInputStruct != SlotInputStruct)
			{
				// The item and slot both need to implement the same struct, otherwise there's no
				// guarantee they will be mutually compatible.
				//
				// For example, if a groom pipeline doesn't take an input to say which meshes to 
				// bind to, how is it going to produce useful groom bindings? 
				//
				// Keeping this requirement strict should make it clearer which pipelines are
				// compatible. In future, we could allow items to declare compatibility with 
				// multiple build input structs if there's a need for more flexibility.

				// TODO: Log
				continue;
			}

			FInstancedStruct BuildInput;
			FMetaHumanGroomPipelineBuildInput& GroomBuildInput = BuildInput.InitializeAs<FMetaHumanGroomPipelineBuildInput>();
			
			for (const TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& Pair : CharacterPipelineData)
			{
				if (Pair.Value.FaceMesh)
				{
					GroomBuildInput.BindingMeshes.Add(Pair.Value.FaceMesh);
				}
			}

			GroomBuildInput.FaceLODs = LODProperties.FaceLODs;

			FMetaHumanPaletteBuildCacheEntry BuildCache;// TODO = CharacterCollection->ItemBuildCache.FindOrAdd(ItemPath);
			FMetaHumanPaletteBuiltData ItemBuiltData;

			const TArrayView<const FMetaHumanPinnedSlotSelection> PinnedSlotSelectionsForItem = 
				UMetaHumanCharacterPipeline::FilterPinnedSlotSelectionsToItem(SortedPinnedSlotSelections, ItemPath);

			const TArrayView<const FMetaHumanPaletteItemPath> ItemsToExcludeForItem = UMetaHumanCharacterPipeline::FilterItemPaths(SortedItemsToExclude, ItemPath);

			ItemEditorPipeline->BuildItemSynchronous(
				ItemPath,
				Item.WardrobeItem,
				BuildInput,
				PinnedSlotSelectionsForItem,
				ItemsToExcludeForItem,
				BuildCache,
				Quality,
				TargetPlatform,
				OuterForGeneratedAssets,
				ItemBuiltData);

			if (ItemBuiltData.ContainsOnlyValidBuildOutputForItem(ItemPath))
			{
				if (!ItemBuiltData.ItemBuiltData[ItemPath].BuildOutput.GetScriptStruct()->IsChildOf(ItemOutputStruct))
				{
					// The item produced a struct that isn't compatible with the struct its 
					// specification said it would produce.
					//
					// This behavior is not permitted, as it can cause downstream errors that are
					// hard to diagnose, therefore we consider this a failed build.

					// TODO: Log
					continue;
				}

				BuiltData.PaletteBuiltData.IntegrateItemBuiltData(ItemPath, Item.SlotName, MoveTemp(ItemBuiltData));

				const FMetaHumanPinnedSlotSelection* PinnedItem = nullptr;
				if (FMetaHumanPinnedSlotSelection::TryGetPinnedItem(SortedPinnedSlotSelections, ItemPath, PinnedItem))
				{
					// Follicle map generation is done here for now. It should move into the groom pipeline.
					if (bGenerateFollicleMaps
						&& FollicleChannelMapping.Contains(Item.SlotName))
					{
						const FMetaHumanGroomPipelineBuildOutput& GroomBuildOutput = BuiltData.PaletteBuiltData.ItemBuiltData[ItemPath].BuildOutput.Get<FMetaHumanGroomPipelineBuildOutput>();

						UGroomAsset* Groom = nullptr;
						if (GroomBuildOutput.Bindings.Num() > 0
							&& GroomBuildOutput.Bindings[0])
						{
							Groom = GroomBuildOutput.Bindings[0]->GetGroom();
						}

						if (Groom)
						{
							FFollicleInfo& FollicleInfo = PinnedFollicleMapInfo.AddDefaulted_GetRef();
							FollicleInfo.GroomAsset = Groom;
							FollicleInfo.Channel = FollicleChannelMapping[Item.SlotName];
							FollicleInfo.KernelSizeInPixels = FMath::Max(2, HairProperties.FollicleMapRootRadius);
						}
					}

					// This is a temporary solution. This should be moved into the groom pipeline.
					if (const UMetaHumanDefaultGroomPipeline* GroomPipeline = Cast<UMetaHumanDefaultGroomPipeline>(ItemPipeline))
					{
						if (PinnedBakedGroomMaterials.Num() > 0)
						{
							int32 FirstLODBaked = INDEX_NONE;
							const bool bHideHair = false;
							GroomPipeline->SetFaceMaterialParameters(
								PinnedBakedGroomMaterialArray,
								PinnedBakedGroomLODToMaterial,
								Item.SlotName,
								PinnedItem->InstanceParameters,
								bHideHair,
								FirstLODBaked);

							if (FirstLODBaked > INDEX_NONE)
							{
								FirstLODPinnedBakedGrooms = FMath::Min(FirstLODPinnedBakedGrooms, FirstLODBaked);
							}
						}
						else
						{
							for (TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& Pair : CharacterPipelineData)
							{
								if (!Pair.Value.bIsGeneratedAssetsValid
									|| !Pair.Value.FaceMesh)
								{
									// Only enabled on non-preview meshes for now
									continue;
								}

								const FMetaHumanCharacterFaceMaterialSet FaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(Pair.Value.FaceMesh);
								TArray<UMaterialInstanceConstant*> FaceMaterials;
								TArray<int32> LODToMaterial;
								LODToMaterial.Reserve(NumFaceLODs);
								for (int32 Index = 0; Index < NumFaceLODs; Index++)
								{
									LODToMaterial.Add(INDEX_NONE);
								}

								FaceMaterialSet.ForEachSkinMaterial<UMaterialInstanceConstant>(
									[&FaceMaterials, &LODToMaterial](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstanceConstant* Material)
									{
										const int32 MaterialIndex = FaceMaterials.Add(Material);

										UE::MetaHuman::Private::WriteSkinMaterialValueToLODArray(LODToMaterial, Slot, MaterialIndex);
									});

								// Note that some LODs may have been removed, so LODToMaterial may 
								// still contain some entries set to INDEX_NONE.

								int32 FirstLODBaked = INDEX_NONE;

								const bool bHideHair = false;
								GroomPipeline->SetFaceMaterialParameters(
									FaceMaterials,
									LODToMaterial,
									Item.SlotName,
									PinnedItem->InstanceParameters,
									bHideHair,
									FirstLODBaked);

								// LODs from FirstLODBaked onwards have had their materials changed
								if (Pair.Value.FaceMaterialChangesPerLOD.IsValidIndex(FirstLODBaked))
								{
									for (int32 LODIndex = FirstLODBaked; LODIndex < Pair.Value.FaceMaterialChangesPerLOD.Num(); LODIndex++)
									{
										if (Pair.Value.FaceMaterialChangesPerLOD[LODIndex] != INDEX_NONE)
										{
											Pair.Value.FaceMaterialChangesPerLOD[LODIndex]++;
										}
									}
								}
							}
						}
					}
				}
			}
		}
		else if (UChaosOutfitAsset* ClothAsset = Cast<UChaosOutfitAsset>(PrincipalAsset))
		{
			FInstancedStruct PartOutput;
			FMetaHumanOutfitPipelineBuildOutput& OutfitOutputStruct = PartOutput.InitializeAs<FMetaHumanOutfitPipelineBuildOutput>();

			if (!ItemPipeline)
			{
				ItemPipeline = GetDefault<UMetaHumanOutfitPipeline>();
			}

			const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
			if (!ItemEditorPipeline)
			{
				// Can't build this item without an editor pipeline
				UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Failed to build item containing {PrincipalAsset}: No item editor pipeline found for {ItemPipeline}", 
					GetFullNameSafe(PrincipalAsset), GetFullNameSafe(ItemPipeline));
				continue;
			}

			// Generate a fitted version of this cloth for each Character
			for (TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& Pair : CharacterPipelineData)
			{
				FName AutoSelectedSourceSize;
				TArray<FName> AvailableSourceSizes;

				// Try to fit this cloth to the Character's body
				UChaosOutfitAsset* ClothForCharacter = nullptr;
				if (bCanResizeOutfits
					&& Pair.Value.MergedHeadAndBody)
				{
					UChaosOutfitAsset* FittedOutfit = NewObject<UChaosOutfitAsset>(OuterForGeneratedAssets);
					FittedOutfit->SetDataflow(CostumeProperties.OutfitResizeDataflowAsset);

					FDataflowVariableOverrides& FittedOutfitVariableOverrides = FittedOutfit->GetDataflowInstance().GetVariableOverrides();

					FittedOutfitVariableOverrides.OverrideVariableObject(UE::MetaHuman::Private::OutfitResize_TargetBodyPropertyName, Pair.Value.MergedHeadAndBody);
					FittedOutfitVariableOverrides.OverrideVariableObject(UE::MetaHuman::Private::OutfitResize_ResizableOutfitPropertyName, ClothAsset);

					FittedOutfitVariableOverrides.OverrideVariableBool("TransferSkinWeights", Pair.Value.bTransferSkinWeights);
					FittedOutfitVariableOverrides.OverrideVariableBool("GenerateLOD0Only", !Pair.Value.bTransferSkinWeights);
					FittedOutfitVariableOverrides.OverrideVariableBool("StripSimMesh", Pair.Value.bStripSimMesh);
			
					
					const FMetaHumanPinnedSlotSelection* PinnedSelection = nullptr;
					if (FMetaHumanPinnedSlotSelection::TryGetPinnedItem(SortedPinnedSlotSelections, ItemPath, PinnedSelection))
					{
						auto OverrideBoolVariable = [PinnedSelection, &FittedOutfitVariableOverrides](const FName& VariableName)
						{
							TValueOrError<bool, EPropertyBagResult> Value = PinnedSelection->InstanceParameters.GetValueBool(VariableName);
							if (Value.HasValue())
							{
								FittedOutfitVariableOverrides.OverrideVariableBool(VariableName, Value.GetValue());
							}
						};

						OverrideBoolVariable(TEXT("PruneSkinWeights"));
						OverrideBoolVariable(TEXT("RelaxSkinWeights"));
						OverrideBoolVariable(TEXT("HammerSkinWeights"));
						OverrideBoolVariable(TEXT("ClampSkinWeights"));
						OverrideBoolVariable(TEXT("NormalizeSkinWeights"));
						OverrideBoolVariable(TEXT("ResizeUVs"));
						OverrideBoolVariable(TEXT("CustomRegionResizing"));
												
						{
							// Important: Result must be stored in a local variable here to keep 
							// the FString alive.
							//
							// Chaining TryGetValue directly onto GetValueString will result in the
							// FString being deleted and a dangling pointer being returned.
							TValueOrError<FString, EPropertyBagResult> Result = PinnedSelection->InstanceParameters.GetValueString("SourceSizeOverride");
							const FString* VarPtr = Result.TryGetValue();
							if (VarPtr != nullptr)
							{
								FittedOutfitVariableOverrides.OverrideVariableName("OverrideBodySizeName", **VarPtr);
							}
						}
					}

					FittedOutfit->GetDataflowInstance().UpdateOwnerAsset(true);

					{
						const UE::Chaos::OutfitAsset::FCollectionOutfitConstFacade OutfitFacade(ClothAsset->GetOutfitCollection());
						if (OutfitFacade.IsValid())
						{
							const int32 ClosestBodySize = OutfitFacade.FindClosestBodySize(*Pair.Value.MergedHeadAndBody);
							AutoSelectedSourceSize = *OutfitFacade.GetBodySizeName(ClosestBodySize);
						}

						for (int32 BodySizeIndex = 0; BodySizeIndex < OutfitFacade.GetNumBodySizes(); BodySizeIndex++)
						{
							AvailableSourceSizes.Add(*OutfitFacade.GetBodySizeName(BodySizeIndex));
						}
					}

					ClothForCharacter = FittedOutfit;
				}

				if (!ClothForCharacter)
				{
					// Failed to fit the cloth, so pass through the original cloth -- it may not need fitting
					ClothForCharacter = ClothAsset;
				}

				FMetaHumanOutfitGeneratedAssets* OutfitGeneratedAssets = nullptr;

				if (Quality == EMetaHumanCharacterPaletteBuildQuality::Production)
				{
					USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(OuterForGeneratedAssets);

					// For Production quality, we bake to meshes, because Outfits can't yet be cooked
					if (ClothForCharacter->ExportToSkeletalMesh(*SkeletalMesh))
					{
						SkeletalMesh->SetSkeleton(Pair.Value.BodyMesh->GetSkeleton());

						OutfitGeneratedAssets = &OutfitOutputStruct.CharacterAssets.Add(Pair.Key);
						OutfitGeneratedAssets->OutfitMesh = SkeletalMesh;
					}
				}
				else
				{
					OutfitGeneratedAssets = &OutfitOutputStruct.CharacterAssets.Add(Pair.Key);
					OutfitGeneratedAssets->Outfit = ClothForCharacter;
					OutfitGeneratedAssets->CombinedBodyMesh = Pair.Value.MergedHeadAndBody;
				}

				if (OutfitGeneratedAssets)
				{
					OutfitGeneratedAssets->AvailableSourceSizes = AvailableSourceSizes;
					OutfitGeneratedAssets->AutoSelectedSourceSize = AutoSelectedSourceSize;

					if (const UMetaHumanOutfitEditorPipeline* OutfitEditorPipeline = Cast<UMetaHumanOutfitEditorPipeline>(ItemEditorPipeline))
					{
						// When we move this code to the outfit editor pipeline, Head/BodyHiddenFaceMap should come from the build output,
						// and could be different per character, as it will eventually depend on the body measurements.

						if (FMetaHumanPinnedSlotSelection::IsItemPinned(SortedPinnedSlotSelections, ItemPath))
						{
							if (OutfitEditorPipeline->HeadHiddenFaceMapTexture.Texture)
							{
								if (OutfitEditorPipeline->HeadHiddenFaceMapTexture.Texture->Source.IsValid())
								{
									Pair.Value.HeadHiddenFaceMaps.Add(OutfitEditorPipeline->HeadHiddenFaceMapTexture);
									if (Quality == EMetaHumanCharacterPaletteBuildQuality::Preview)
									{
										// Set the hidden face mask for the preview only
										OutfitGeneratedAssets->HeadHiddenFaceMap = OutfitEditorPipeline->HeadHiddenFaceMapTexture;
									}
								}
								else
								{
									UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Hidden face map {Texture} from Wardrobe Item {WardrobeItem} has no source data and can't be used",
										OutfitEditorPipeline->HeadHiddenFaceMapTexture.Texture->GetPathName(), Item.WardrobeItem->GetPathName());
								}
							}
						
							if (OutfitEditorPipeline->BodyHiddenFaceMapTexture.Texture)
							{
								if (OutfitEditorPipeline->BodyHiddenFaceMapTexture.Texture->Source.IsValid())
								{
									Pair.Value.BodyHiddenFaceMaps.Add(OutfitEditorPipeline->BodyHiddenFaceMapTexture);
									if (Quality == EMetaHumanCharacterPaletteBuildQuality::Preview)
									{
										// Set the hidden face mask for the preview only
										OutfitGeneratedAssets->BodyHiddenFaceMap = OutfitEditorPipeline->BodyHiddenFaceMapTexture;
									}
								}
								else
								{
									UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Hidden face map {Texture} from Wardrobe Item {WardrobeItem} has no source data and can't be used",
										OutfitEditorPipeline->BodyHiddenFaceMapTexture.Texture->GetPathName(), Item.WardrobeItem->GetPathName());
								}
							}
						}
					}
				}
			}

			FMetaHumanPipelineBuiltData& ItemBuildOutput = BuiltData.PaletteBuiltData.ItemBuiltData.Add(FMetaHumanPaletteItemPath(Item.GetItemKey()));
			ItemBuildOutput.SlotName = Item.SlotName;
			ItemBuildOutput.BuildOutput = MoveTemp(PartOutput);
		}
		else if (USkeletalMesh* MeshAsset = Cast<USkeletalMesh>(PrincipalAsset))
		{
			// Handle hidden face maps, etc, for skeletal mesh clothing. In future this will be done by the Skeletal Mesh Pipeline.

			FInstancedStruct PartOutput;
			FMetaHumanSkeletalMeshPipelineBuildOutput& SkelMeshOutputStruct = PartOutput.InitializeAs<FMetaHumanSkeletalMeshPipelineBuildOutput>();
			SkelMeshOutputStruct.Mesh = MeshAsset;

			if (!ItemPipeline)
			{
				ItemPipeline = GetDefault<UMetaHumanSkeletalMeshPipeline>();
			}

			const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
			if (!ItemEditorPipeline)
			{
				// Can't build this item without an editor pipeline
				UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Failed to build item containing {PrincipalAsset}: No item editor pipeline found for {ItemPipeline}", 
					GetFullNameSafe(PrincipalAsset), GetFullNameSafe(ItemPipeline));

				continue;
			}

			if (const UMetaHumanSkeletalMeshEditorPipeline* SkelMeshEditorPipeline = Cast<UMetaHumanSkeletalMeshEditorPipeline>(ItemEditorPipeline))
			{
				if (FMetaHumanPinnedSlotSelection::IsItemPinned(SortedPinnedSlotSelections, ItemPath))
				{
					if (SkelMeshEditorPipeline->HeadHiddenFaceMapTexture.Texture)
					{
						if (SkelMeshEditorPipeline->HeadHiddenFaceMapTexture.Texture->Source.IsValid())
						{
							for (TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& Pair : CharacterPipelineData)
							{
								Pair.Value.HeadHiddenFaceMaps.Add(SkelMeshEditorPipeline->HeadHiddenFaceMapTexture);
							}

							if (Quality == EMetaHumanCharacterPaletteBuildQuality::Preview)
							{
								// Set the hidden face mask for the preview only
								SkelMeshOutputStruct.HeadHiddenFaceMap = SkelMeshEditorPipeline->HeadHiddenFaceMapTexture;
							}
						}
						else
						{
							UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Hidden face map {Texture} from Wardrobe Item {WardrobeItem} has no source data and can't be used",
								SkelMeshEditorPipeline->HeadHiddenFaceMapTexture.Texture->GetPathName(), Item.WardrobeItem->GetPathName());
						}
					}

					if (SkelMeshEditorPipeline->BodyHiddenFaceMapTexture.Texture)
					{
						if (SkelMeshEditorPipeline->BodyHiddenFaceMapTexture.Texture->Source.IsValid())
						{
							for (TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& Pair : CharacterPipelineData)
							{
								Pair.Value.BodyHiddenFaceMaps.Add(SkelMeshEditorPipeline->BodyHiddenFaceMapTexture);
							}

							if (Quality == EMetaHumanCharacterPaletteBuildQuality::Preview)
							{
								// Set the hidden face mask for the preview only
								SkelMeshOutputStruct.BodyHiddenFaceMap = SkelMeshEditorPipeline->BodyHiddenFaceMapTexture;
							}
						}
						else
						{
							UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Hidden face map {Texture} from Wardrobe Item {WardrobeItem} has no source data and can't be used",
								SkelMeshEditorPipeline->BodyHiddenFaceMapTexture.Texture->GetPathName(), Item.WardrobeItem->GetPathName());
						}
					}
				}
			}

			FMetaHumanPipelineBuiltData& ItemBuildOutput = BuiltData.PaletteBuiltData.ItemBuiltData.Add(FMetaHumanPaletteItemPath(Item.GetItemKey()));
			ItemBuildOutput.SlotName = Item.SlotName;
			ItemBuildOutput.BuildOutput = MoveTemp(PartOutput);
		}
	}

	if (PinnedFollicleMapInfo.Num() > 0)
	{
		const int32 Resolution = static_cast<int32>(HairProperties.FollicleMapResolution);
		const int32 MipCount = FMath::FloorLog2(Resolution) + 1;

		UTexture2D* FollicleMap = NewObject<UTexture2D>(OuterForGeneratedAssets);
		FGroomTextureBuilder::AllocateFollicleTextureResources(FollicleMap, FIntPoint(Resolution), MipCount);

		// Blur the lower mips, as this looks better
		FollicleMap->MipGenSettings = TMGS_Blur5;
		
		// Need Pre/PostEditChange around updating the texture's image data
		{
			FollicleMap->PreEditChange(nullptr);

			FGroomTextureBuilder::BuildFollicleTexture(PinnedFollicleMapInfo, FollicleMap, false);

			FollicleMap->PostEditChange();
		}

		// The follicle map is character-independent, so set the same on all characters
		for (TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& Pair : CharacterPipelineData)
		{
			Pair.Value.FollicleMap = FollicleMap;

			// Add the follicle map to the generated assets metadata so that it gets unpacked with the character's assets
			if (Pair.Value.bIsGeneratedAssetsValid)
			{
				Pair.Value.GeneratedAssets.Metadata.Emplace(FollicleMap, TEXT("Grooms"), TEXT("T_FollicleMap"));
			}
		}
	}

	if (PinnedBakedGroomMaterials.Num() > 0)
	{
		for (TPair<FMetaHumanPaletteItemKey, FCharacterPipelineData>& CharacterPair : CharacterPipelineData)
		{
			if (!CharacterPair.Value.bIsGeneratedAssetsValid
				|| !CharacterPair.Value.FaceMesh)
			{
				// Only enabled on non-preview meshes for now
				continue;
			}

			const FMetaHumanCharacterFaceMaterialSet FaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(CharacterPair.Value.FaceMesh);

			FMetaHumanMaterialBakingOptions MaterialBakingOptions;
			TStrongObjectPtr<UMetaHumanMaterialBakingSettings> BakingSettings = TStrongObjectPtr(NewObject<UMetaHumanMaterialBakingSettings>());
			MaterialBakingOptions.BakingSettings = TSoftObjectPtr<UMetaHumanMaterialBakingSettings>(FSoftObjectPath(BakingSettings.Get()));

			// These materials generate the pre-baked groom textures.
			//
			// They're set up here as FSkeletalMaterials because that's the format used by 
			// TryBakeMaterials.
			TArray<FSkeletalMaterial> SkelMeshMaterials;
			{
				SkelMeshMaterials.Reserve(PinnedBakedGroomMaterials.Num());

				for (const TPair<EMetaHumanCharacterSkinMaterialSlot, TStrongObjectPtr<UMaterialInstanceConstant>>& Pair : PinnedBakedGroomMaterials)
				{
					// LOD names from the EMetaHumanCharacterSkinMaterialSlot enum are used as 
					// material slot names in the generated baking settings.
					const FName LODName = *StaticEnum<EMetaHumanCharacterSkinMaterialSlot>()->GetAuthoredNameStringByValue(static_cast<int64>(Pair.Key));
					SkelMeshMaterials.Emplace(Pair.Value.Get(), LODName);
				}
			}

			FName PreviousBakedGroomMaterialSlot;

			for (const TPair<EMetaHumanCharacterSkinMaterialSlot, TObjectPtr<UMaterialInstance>>& SkinMaterialPair : FaceMaterialSet.Skin)
			{
				if (!SkinMaterialPair.Value)
				{
					// This shouldn't be null, but it's handled gracefully here
					continue;
				}

				UMaterialInstanceConstant* SkinMaterial = Cast<UMaterialInstanceConstant>(SkinMaterialPair.Value);
				if (!SkinMaterial)
				{
					UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Unexpected material type in skin materials from {Mesh}: {Material}. Should be Material Instance Constant.", 
						GetFullNameSafe(CharacterPair.Value.FaceMesh), GetFullNameSafe(SkinMaterialPair.Value));

					continue;
				}

				if (static_cast<int32>(SkinMaterialPair.Key) < FirstLODPinnedBakedGrooms)
				{
					// No grooms need baking at this LOD
					continue;
				}

				const FString SkinMaterialSlotNameString = StaticEnum<EMetaHumanCharacterSkinMaterialSlot>()->GetAuthoredNameStringByValue(static_cast<int64>(SkinMaterialPair.Key));
				const FName SkinMaterialSlotName = *SkinMaterialSlotNameString;

				SkinMaterial->SetStaticSwitchParameterValueEditorOnly(FName("Use Pre-Baked Groom Texture Set"), true);

				{
					FMetaHumanBakedMaterialProperties& BakedMaterial = BakingSettings->BakedMaterials.AddDefaulted_GetRef();
					BakedMaterial.PrimaryMaterialSlotName = SkinMaterialSlotName;
					BakedMaterial.Material = SkinMaterial;
				}

				const UMaterialInstanceConstant* BakedGroomMaterial = PinnedBakedGroomMaterials[SkinMaterialPair.Key].Get();

				// First see if there's an existing texture graph setup with the same parameters
				FMetaHumanTextureGraphOutputProperties* TextureGraph = Algo::FindByPredicate(BakingSettings->TextureGraphs, 
					[BakedGroomMaterial, &PinnedBakedGroomMaterials](const FMetaHumanTextureGraphOutputProperties& TextureGraph)
					{
						const int64 SlotValue = StaticEnum<EMetaHumanCharacterSkinMaterialSlot>()->GetValueByName(TextureGraph.InputMaterials[0].SourceMaterialSlotName);
						check(SlotValue != INDEX_NONE);

						const UMaterialInstanceConstant* TextureGraphSourceMaterial = PinnedBakedGroomMaterials[static_cast<EMetaHumanCharacterSkinMaterialSlot>(SlotValue)].Get();

						return TextureGraphSourceMaterial->Equivalent(BakedGroomMaterial);
					});

				if (!TextureGraph)
				{
					TextureGraph = &BakingSettings->TextureGraphs.AddDefaulted_GetRef();
					TextureGraph->TextureGraphInstance = HairProperties.PreBakedGroomTextureGraphInstance;

					FMetaHumanInputMaterialProperties& InputMaterial = TextureGraph->InputMaterials.AddDefaulted_GetRef();
					InputMaterial.InputParameterName = HairProperties.PreBakedGroomTextureGraphInputName;

					// This references the name of the entry in SkelMeshMaterials that will be used 
					// as the source material for the bake.
					InputMaterial.SourceMaterialSlotName = SkinMaterialSlotName;

					EMetaHumanBuildTextureResolution PreBakedTextureResolution = EMetaHumanBuildTextureResolution::Res256;
					{
						TArray<UTexture*> UsedTextures;
						BakedGroomMaterial->GetUsedTextures(UsedTextures);

						auto GetMaxTextureDimension = [](const UTexture* Texture) -> int32
						{
							int32 SizeX = 0;
							int32 SizeY = 0;
							Texture->GetBuiltTextureSize(GetTargetPlatformManagerRef().GetRunningTargetPlatform(), SizeX, SizeY);

							return FMath::Max(SizeX, SizeY);
						};

						auto GetMaxInt = [](int32 A, int32 B)
						{
							return FMath::Max(A, B);
						};

						// Compares all texture dimensions and returns the highest
						const int32 MaxDimension = Algo::TransformAccumulate(UsedTextures, GetMaxTextureDimension, 0, GetMaxInt);

						// Clamp to the supported range
						const int32 MaxSupportedDimension = FMath::Clamp(
							FMath::RoundUpToPowerOfTwo(MaxDimension), 
							static_cast<int32>(EMetaHumanBuildTextureResolution::Res256), 
							static_cast<int32>(StaticEnum<EMetaHumanBuildTextureResolution>()->GetMaxEnumValue()));

						// Every power of two between the min and max is represented in 
						// EMetaHumanBuildTextureResolution, so this should never fail, but handle
						// gracefully just in case.
						if (ensure(StaticEnum<EMetaHumanBuildTextureResolution>()->IsValidEnumValue(MaxSupportedDimension)))
						{
							PreBakedTextureResolution = static_cast<EMetaHumanBuildTextureResolution>(MaxSupportedDimension);
						}
					}

					TextureGraph->OutputTextures = HairProperties.PreBakedGroomTextureGraphOutputTextures;
					for (FMetaHumanOutputTextureProperties& OutputTexture : TextureGraph->OutputTextures)
					{
						// Append the slot name to the texture name to avoid conflicting texture names
						OutputTexture.OutputTextureName = FName(*(OutputTexture.OutputTextureName.ToString() + TEXT("_") + SkinMaterialSlotNameString));

						MaterialBakingOptions.TextureResolutionsOverrides.Add(OutputTexture.OutputTextureName, PreBakedTextureResolution);

						// The output slots are set procedurally
						OutputTexture.OutputMaterialSlotNames.Reset();
						OutputTexture.OutputMaterialSlotNames.Add(SkinMaterialSlotName);
					}
				}
				else
				{
					// Add to the existing texture graph setup
					for (FMetaHumanOutputTextureProperties& OutputTexture : TextureGraph->OutputTextures)
					{
						OutputTexture.OutputMaterialSlotNames.Add(SkinMaterialSlotName);
					}
				}

				const FName BakedGroomMaterialSlot = TextureGraph->InputMaterials[0].SourceMaterialSlotName;

				if (PreviousBakedGroomMaterialSlot != BakedGroomMaterialSlot)
				{
					// This skin material uses a different pre-baked groom material from the 
					// previous LOD's skin material, so update FaceMaterialChangesPerLOD to let the 
					// skin material baker know that it needs to be baked separately.

					const int32 FirstLODBaked = static_cast<int32>(SkinMaterialPair.Key);
					if (CharacterPair.Value.FaceMaterialChangesPerLOD.IsValidIndex(FirstLODBaked))
					{
						for (int32 LODIndex = FirstLODBaked; LODIndex < CharacterPair.Value.FaceMaterialChangesPerLOD.Num(); LODIndex++)
						{
							if (CharacterPair.Value.FaceMaterialChangesPerLOD[LODIndex] != INDEX_NONE)
							{
								CharacterPair.Value.FaceMaterialChangesPerLOD[LODIndex]++;
							}
						}
					}
				}

				PreviousBakedGroomMaterialSlot = BakedGroomMaterialSlot;
			}

			if (BakingSettings->TextureGraphs.Num() == 0)
			{
				// No baking needed
				continue;
			}

			// The materials referenced in the baking options should be used as they are and not instanced.
			const bool bCreateInstancesOfBakedMaterials = false;
			TArray<TObjectPtr<UTexture>> GeneratedTextures;

			if (!TryBakeMaterials(
				TextureOutputFolder,
				MaterialBakingOptions,
				SkelMeshMaterials,
				TMap<FName, TObjectPtr<UMaterialInterface>>(),
				TArray<int32>(),
				OuterForGeneratedAssets,
				CharacterPair.Value.GeneratedAssets,
				GeneratedTextures,
				bCreateInstancesOfBakedMaterials))
			{
				// TryBakeMaterials does its own error logging, so there will be more info in the log
				UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Failed to generated pre-baked groom textures");
			}

			CharacterPair.Value.PreBakedGroomTextures = GeneratedTextures;
		}
	}
}

bool UMetaHumanDefaultEditorPipelineBase::CanResizeOutfits() const
{
	if (!CostumeProperties.OutfitResizeDataflowAsset)
	{
		return false;
	}

	const FInstancedPropertyBag& SourceVariables = CostumeProperties.OutfitResizeDataflowAsset->Variables;
	const FPropertyBagPropertyDesc* TargetBodyProperty = SourceVariables.FindPropertyDescByName(UE::MetaHuman::Private::OutfitResize_TargetBodyPropertyName);
	const FPropertyBagPropertyDesc* ResizableOutfitProperty = SourceVariables.FindPropertyDescByName(UE::MetaHuman::Private::OutfitResize_ResizableOutfitPropertyName);

	return TargetBodyProperty
		&& TargetBodyProperty->IsObjectType()
		&& USkeletalMesh::StaticClass()->IsChildOf(Cast<UClass>(TargetBodyProperty->ValueTypeObject))
		&& ResizableOutfitProperty
		&& ResizableOutfitProperty->IsObjectType()
		&& UChaosOutfitAsset::StaticClass()->IsChildOf(Cast<UClass>(ResizableOutfitProperty->ValueTypeObject));
}

void UMetaHumanDefaultEditorPipelineBase::UnpackCollectionAssets(
	TNotNull<UMetaHumanCollection*> Collection,
	FMetaHumanCollectionBuiltData& CollectionBuiltData,
	const FOnUnpackComplete& OnComplete) const
{
	// TODO: UnpackCollectionAssets should use the existing built data instead of doing its own build

	Collection->Build(
		FInstancedStruct(),
		EMetaHumanCharacterPaletteBuildQuality::Production,
		GetTargetPlatformManagerRef().GetRunningTargetPlatform(),
		UMetaHumanCollection::FOnBuildComplete::CreateUObject(
			this,
			&UMetaHumanDefaultEditorPipelineBase::OnCharacterPaletteAssetsUnpacked,
			TWeakObjectPtr<UMetaHumanCollection>(Collection),
			TNotNull<FMetaHumanCollectionBuiltData*>(&CollectionBuiltData),
			OnComplete),
		Collection->GetDefaultInstance()->ToPinnedSlotSelections(EMetaHumanUnusedSlotBehavior::PinnedToEmpty));
}

void UMetaHumanDefaultEditorPipelineBase::OnCharacterPaletteAssetsUnpacked(
	EMetaHumanBuildStatus Result,
	TWeakObjectPtr<UMetaHumanCollection> WeakCollection,
	TNotNull<FMetaHumanCollectionBuiltData*> CollectionBuiltData,
	FOnUnpackComplete OnComplete) const
{
	TStrongObjectPtr<UMetaHumanCollection> StrongCollection = WeakCollection.Pin();
	UMetaHumanCollection* Collection = StrongCollection.Get();

	if (!Collection
		|| Result == EMetaHumanBuildStatus::Failed)
	{
		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
		return;
	}

	const FString UnpackFolder = Collection->GetUnpackFolder();

	// The paths of all unpacked assets, so that we can ensure we don't unpack two different 
	// assets to the same path.
	TSet<FString> UnpackedAssetPaths;

	for (TPair<FMetaHumanPaletteItemPath, FMetaHumanPipelineBuiltData>& Item : CollectionBuiltData->PaletteBuiltData.ItemBuiltData)
	{
		// Only process items directly owned by the collection, i.e. not sub-items
		if (!Item.Key.IsDirectChildPathOf(FMetaHumanPaletteItemPath::Collection))
		{
			continue;
		}

		if (const FMetaHumanCharacterPartOutput* CharacterPart = Item.Value.BuildOutput.GetPtr<FMetaHumanCharacterPartOutput>())
		{
			// TODO: It seems that reporting progress is causing a crash when exporting to UEFN since it causes a redraw
			// FScopedSlowTask UnpackSlowTask(CharacterPart->GeneratedAssets.Metadata.Num(), LOCTEXT("UnpackingCharacterItemsTask", "Unpacking Character Assets"));
			// UnpackSlowTask.MakeDialog();

			for (const FMetaHumanGeneratedAssetMetadata& AssetMetadata : CharacterPart->GeneratedAssets.Metadata)
			{
				// UnpackSlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("UnpackingItem", "Unpacking '{0}'"), FText::FromName(GetFNameSafe(AssetMetadata.Object))));

				if (!AssetMetadata.Object)
				{
					continue;
				}

				FString AssetPackagePath = UnpackFolder;

				if (!AssetMetadata.PreferredSubfolderPath.IsEmpty())
				{
					if (AssetMetadata.bSubfolderIsAbsolute)
					{
						AssetPackagePath = AssetMetadata.PreferredSubfolderPath;
					}
					else
					{
						AssetPackagePath = AssetPackagePath / AssetMetadata.PreferredSubfolderPath;
					}
				}

				if (!AssetMetadata.PreferredName.IsEmpty())
				{
					AssetPackagePath = AssetPackagePath / AssetMetadata.PreferredName;
				}
				else
				{
					AssetPackagePath = AssetPackagePath / AssetMetadata.Object->GetName();
				}

				if (!TryUnpackObject(AssetMetadata.Object, Collection, AssetPackagePath, UnpackedAssetPaths))
				{
					OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
					return;
				}
			}
		}
		else if (const FMetaHumanOutfitPipelineBuildOutput* OutfitPart = Item.Value.BuildOutput.GetPtr<FMetaHumanOutfitPipelineBuildOutput>())
		{
			FScopedSlowTask UnpackSlowTask(OutfitPart->CharacterAssets.Num(), LOCTEXT("UnpackingClothAssets", "Unpacking Clothing Assets"));
			UnpackSlowTask.MakeDialog();

			for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanOutfitGeneratedAssets>& Pair : OutfitPart->CharacterAssets)
			{
				{
					const FString AssetName = FString::Format(TEXT("{0}_{1}"), { Pair.Key.ToAssetNameString(), Item.Value.SlotName.ToString() }).Replace(TEXT(" "), TEXT(""));
					FString AssetPackagePath = UnpackFolder / TEXT("Clothing") / AssetName;

					UnpackSlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("UnpackingCloshAsset", "Unpacking Clothing Asset '{0}'"), FText::FromString(AssetName)));

					// There will either be an Outfit or a mesh baked from an Outfit, but not both
					UObject* AssetToUnpack = Pair.Value.Outfit ? static_cast<UObject*>(Pair.Value.Outfit) : static_cast<UObject*>(Pair.Value.OutfitMesh);
					if (AssetToUnpack)
					{
						if (!TryUnpackObject(AssetToUnpack, Collection, AssetPackagePath, UnpackedAssetPaths))
						{
							OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
							return;
						}
					}
				}

				if (Pair.Value.CombinedBodyMesh)
				{
					// This mesh has no render data, which can cause crashes if the engine tries to
					// render it or save it to disk.
					//
					// Now that it's being unpacked, it's going to be visible to other systems and 
					// therefore its render data needs to be built.
					Pair.Value.CombinedBodyMesh->PostEditChange();

					// There should only be one combined head/body mesh per Character, so the name 
					// only needs to reference the Character name in order to be unique.
					const FString AssetName = FString::Format(TEXT("{0}_CombinedBody"), { Pair.Key.ToAssetNameString() });
					FString AssetPackagePath = UnpackFolder / TEXT("Cloth") / AssetName;

					// If the mesh has already been unpacked for another outfit, this will silently
					// succeed, so there's no need to check this before calling.
					if (!TryUnpackObject(Pair.Value.CombinedBodyMesh, Collection, AssetPackagePath, UnpackedAssetPaths))
					{
						OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
						return;
					}
				}
			}
		}
		else
		{
			const UMetaHumanItemPipeline* ItemPipeline = nullptr;
			if (Collection->TryResolveItemPipeline(Item.Key, ItemPipeline))
			{
				const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
				FMetaHumanCharacterPaletteItem ResolvedItem;
				verify(Collection->TryResolveItem(Item.Key, ContainingPalette, ResolvedItem));

				if (!ResolvedItem.WardrobeItem)
				{
					OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
					return;
				}

				if (!ItemPipeline->GetEditorPipeline()->TryUnpackItemAssets(
					ResolvedItem.WardrobeItem,
					Item.Key,
					// TODO: Filter this to just the built data belonging to this item and its sub-items
					CollectionBuiltData->PaletteBuiltData.ItemBuiltData,
					UnpackFolder,
					FTryUnpackObjectDelegate::CreateWeakLambda(this, 
						[this, Collection, &UnpackedAssetPaths](TNotNull<UObject*> Object, FString& InOutAssetPath)
						{
							return TryUnpackObject(Object, Collection, InOutAssetPath, UnpackedAssetPaths);
						})))
				{
					OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
					return;
				}
			}
		}
	}

	OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Succeeded);
}

bool UMetaHumanDefaultEditorPipelineBase::TryUnpackObject(
	UObject* Object,
	UObject* UnpackingAsset,
	FString& InOutAssetPath,
	TSet<FString>& OutUnpackedAssetPaths) const
{
	if (Object->GetOuter()->IsA<UPackage>()
		&& Object->GetFName() == FPackageName::GetShortName(CastChecked<UPackage>(Object->GetOuter())))
	{
		// This object is already the principal asset of its package and doesn't need unpacking
		FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(Object);
		
		return true;
	}

	if (!Object->IsInPackage(UnpackingAsset->GetPackage()))
	{
		// Can't unpack this object, as the asset being unpacked doesn't own it
		return false;
	}

	if (InOutAssetPath.Len() == 0)
	{
		InOutAssetPath = Object->GetName();
	}

	bool bIsUnpackedPathAlreadyUsed = false;
	OutUnpackedAssetPaths.Add(InOutAssetPath, &bIsUnpackedPathAlreadyUsed);

	if (bIsUnpackedPathAlreadyUsed)
	{
		const FRegexPattern Pattern(TEXT("^(.*)_(\\d+)$"));

		while (bIsUnpackedPathAlreadyUsed)
		{
			FRegexMatcher Matcher(Pattern, InOutAssetPath);

			if (Matcher.FindNext())
			{
				// The asset name is already in the format Name_Index, and so we can simply increment
				// the index
				const int32 ExistingNameIndex = FCString::Atoi(*Matcher.GetCaptureGroup(2));

				InOutAssetPath = FString::Format(TEXT("{0}_{1}"), { Matcher.GetCaptureGroup(1), ExistingNameIndex + 1 });
			}
			else
			{
				// Append a new index to the name, starting at 2
				InOutAssetPath = InOutAssetPath + TEXT("_2");
			}

			// Try to add the new name to see if it's unique
			OutUnpackedAssetPaths.Add(InOutAssetPath, &bIsUnpackedPathAlreadyUsed);
		}
	}

	return TryMoveObjectToAssetPackage(Object, InOutAssetPath);
}

bool UMetaHumanDefaultEditorPipelineBase::IsWardrobeItemCompatibleWithSlot(FName InSlotName, TNotNull<const UMetaHumanWardrobeItem*> InWardrobeItem) const
{
	const bool bIsCompatibleWithSlot = Super::IsWardrobeItemCompatibleWithSlot(InSlotName, InWardrobeItem);

	bool bIsItemValid = true;
	
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	if (Settings->bEnableWardrobeItemValidation && IsValid(ValidationContext.GetObject()))
	{
		bIsItemValid = ValidationContext->ValidateWardrobeItem(InWardrobeItem);
	}	

	return bIsCompatibleWithSlot && bIsItemValid;
}

void UMetaHumanDefaultEditorPipelineBase::SetValidationContext(TScriptInterface<IMetaHumanValidationContext> InValidationContext)
{
	ValidationContext = InValidationContext;
}

bool UMetaHumanDefaultEditorPipelineBase::TryMoveObjectToAssetPackage(
	UObject* Object,
	FStringView NewAssetPath) const
{
	UPackage* AssetPackage = UPackageTools::FindOrCreatePackageForAssetType(FName(NewAssetPath), Object->GetClass());
	const FString AssetName = FPackageName::GetShortName(AssetPackage);

	// Attempt to load an object from this package to see if one already exists
	const FString AssetPath = AssetPackage->GetName() + TEXT(".") + AssetName;
	UObject* ExistingAsset = LoadObject<UObject>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);

	// Rename any existing object out of the way
	if (ExistingAsset)
	{
		if (UBlueprint* ExistingBlueprintAsset = Cast<UBlueprint>(ExistingAsset))
		{
			if (!ExistingAsset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_SkipGeneratedClasses))
			{
				return false;
			}
			const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), ExistingBlueprintAsset->StaticClass()); 
			ExistingBlueprintAsset->RenameGeneratedClasses(*UniqueName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors);
		}
		else if (!ExistingAsset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
		{
			return false;
		}
	}

	if (!Object->Rename(*AssetName, AssetPackage, REN_DontCreateRedirectors))
	{
		return false;
	}

	Object->ClearFlags(RF_Transient);
	Object->SetFlags(RF_Public | RF_Transactional | RF_Standalone);
	Object->MarkPackageDirty();

	// Notify the asset registry so that the asset appears in the Content Browser
	if (!ExistingAsset)
	{
		FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(Object);

		FAssetRegistryModule::AssetCreated(Object);
	}
	else
	{
		// For files of older MH version, we are forcing them to be deleted as cached data may not match with their latest version.
		// Note this does not cover all cases since older MH versioned assets that are not replaced by new assembled ones
		// are not tracked in any part of the assembly.
		// See https://jira.it.epicgames.com/browse/MH-16372 for reference
		if (!FMetaHumanCharacterEditorBuild::MetaHumanAssetMetadataVersionIsCompatible(ExistingAsset))
		{
			ExistingAsset->MarkAsGarbage();

			// If the existing asset is a Material Instance, make sure that any child MIs are also released
			// Otherwise, child MIs could end up with invalid render resources
			if (UMaterialInstance* ParentMI = Cast<UMaterialInstance>(ExistingAsset))
			{
				FMetaHumanCharacterEditorBuild::DeleteMaterialInstanceChildren(ParentMI);
			}

			// Notify any editor tools about the object that was replaced
			if (GEditor)
			{
				TMap<UObject*, UObject*> ReplacementMap;
				ReplacementMap.Add(ExistingAsset, Object);
				GEditor->NotifyToolsOfObjectReplacement(ReplacementMap);
			}
		}
	}

	return true;
}

bool UMetaHumanDefaultEditorPipelineBase::TryUnpackInstanceAssets(
	TNotNull<UMetaHumanCharacterInstance*> Instance,
	FInstancedStruct& AssemblyOutput,
	TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
	const FString& TargetFolder) const
{
	// Since this is not shared with the build unpack, technically the assets could clash.
	//
	// The process of unpacking an instance will be reworked in future to deal with this properly.
	TSet<FString> UnpackedAssetPaths;

	for (FMetaHumanGeneratedAssetMetadata& AssetMetadata : AssemblyAssetMetadata)
	{
		if (!AssetMetadata.Object)
		{
			continue;
		}

		FString AssetPackagePath = TargetFolder;

		if (!AssetMetadata.PreferredSubfolderPath.IsEmpty())
		{
			if (AssetMetadata.bSubfolderIsAbsolute)
			{
				AssetPackagePath = AssetMetadata.PreferredSubfolderPath;
			}
			else
			{
				AssetPackagePath = AssetPackagePath / AssetMetadata.PreferredSubfolderPath;
			}
		}

		if (!AssetMetadata.PreferredName.IsEmpty())
		{
			AssetPackagePath = AssetPackagePath / AssetMetadata.PreferredName;
		}
		else
		{
			AssetPackagePath = AssetPackagePath / AssetMetadata.Object->GetName();
		}

		if (const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(AssetMetadata.Object))
		{
			AssetMetadata.Object = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(MID, MID->GetOuter());

			ReplaceReferencesInAssemblyOutput(AssemblyOutput, MID, AssetMetadata.Object);
		}

		if (!TryUnpackObject(AssetMetadata.Object, Instance, AssetPackagePath, UnpackedAssetPaths))
		{
			return false;
		}
	}

	return true;
}

void UMetaHumanDefaultEditorPipelineBase::ReplaceReferencesInAssemblyOutput(
	FInstancedStruct& AssemblyOutput, 
	TNotNull<const UObject*> OriginalObject, 
	TNotNull<UObject*> ReplacementObject)
{
	FMetaHumanDefaultAssemblyOutput* DefaultOutput = AssemblyOutput.GetMutablePtr<FMetaHumanDefaultAssemblyOutput>();
	if (!ensure(DefaultOutput))
	{
		return;
	}

	// For now this is hardcoded to search properties that are known to need replacing.
	//
	// In future, it will do a generic search over all object properties in the AssemblyOutput.
	auto FindReplaceOverrideMaterials = 
		[OriginalObject, ReplacementObject](FMetaHumanGroomPipelineAssemblyOutput& GroomOutput)
		{
			for (TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : GroomOutput.OverrideMaterials)
			{
				if (Pair.Value == OriginalObject)
				{
					Pair.Value = CastChecked<UMaterialInterface>(ReplacementObject);
				}
			}
		};

	FindReplaceOverrideMaterials(DefaultOutput->Hair);
	FindReplaceOverrideMaterials(DefaultOutput->Eyebrows);
	FindReplaceOverrideMaterials(DefaultOutput->Beard);
	FindReplaceOverrideMaterials(DefaultOutput->Mustache);
	FindReplaceOverrideMaterials(DefaultOutput->Eyelashes);
	FindReplaceOverrideMaterials(DefaultOutput->Peachfuzz);

	for (FMetaHumanSkeletalMeshPipelineAssemblyOutput& Data : DefaultOutput->SkeletalMeshData)
	{
		for (TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : Data.OverrideMaterials)
		{
			if (Pair.Value == OriginalObject)
			{
				Pair.Value = CastChecked<UMaterialInterface>(ReplacementObject);
			}
		}
	}
	
	for (FMetaHumanOutfitPipelineAssemblyOutput& Data : DefaultOutput->ClothData)
	{
		for (TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : Data.OverrideMaterials)
		{
			if (Pair.Value == OriginalObject)
			{
				Pair.Value = CastChecked<UMaterialInterface>(ReplacementObject);
			}
		}
	}
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanDefaultEditorPipelineBase::GetSpecification() const
{
	return Specification;
}

TSubclassOf<AActor> UMetaHumanDefaultEditorPipelineBase::GetEditorActorClass() const
{
	return EditorActorClass;
}

bool UMetaHumanDefaultEditorPipelineBase::TryBakeMaterials(
	const FString& BaseOutputFolder,
	const FMetaHumanMaterialBakingOptions& InMaterialBakingOptions,
	TArray<FSkeletalMaterial>& InOutSkelMeshMaterials,
	const TMap<FName, TObjectPtr<UMaterialInterface>>& RemovedMaterialSlots,
	const TArray<int32>& MaterialChangesPerLOD,
	TNotNull<UObject*> GeneratedAssetOuter,
	FMetaHumanCharacterGeneratedAssets& InOutGeneratedAssets,
	TArray<TObjectPtr<UTexture>>& OutGeneratedTextures,
	bool bCreateInstancesOfBakedMaterials) const
{
	TNotNull<UMetaHumanMaterialBakingSettings*> BakingSettings = InMaterialBakingOptions.BakingSettings.LoadSynchronous();

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	const bool bUseVirtualTextures = Settings->ShouldUseVirtualTextures();

	FScopedSlowTask BakeTask{ 4, LOCTEXT("BakingFaceMaterialsTaskLabel", "Baking Face Materials") };
	BakeTask.MakeDialog();

	struct FGeneratedMaterialInstance
	{
		// Ensure that new instances are not GC'ed since we may run GC in between TG export tasks
		TStrongObjectPtr<UMaterialInstanceConstant> MaterialInstance;
		TArray<FName> AdditionalMaterialSlotNames;
	};

	TMap<FName, FGeneratedMaterialInstance> NewMaterialInstances;
	NewMaterialInstances.Reserve(BakingSettings->BakedMaterials.Num());

	BakeTask.EnterProgressFrame(1.0f, LOCTEXT("CreatingMaterialInstances", "Creating Material Instances"));

	// Create a new Material Instance for each of the specified materials
	for (const FMetaHumanBakedMaterialProperties& BakedMaterial : BakingSettings->BakedMaterials)
	{
		// Do not create materials for removed slots
		if (RemovedMaterialSlots.Contains(BakedMaterial.PrimaryMaterialSlotName))
		{
			continue;
		}

		UMaterialInstance* BakedMaterialInstance = nullptr;

		if (BakedMaterial.bOutputsMaterialWithVirtualTextures && bUseVirtualTextures)
		{
			BakedMaterialInstance = Cast<UMaterialInstance>(BakedMaterial.MaterialVT);
		}
		else
		{
			BakedMaterialInstance = Cast<UMaterialInstance>(BakedMaterial.Material);
		}
		
		if (BakedMaterialInstance == nullptr)
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error()
				->AddToken(FTextToken::Create(LOCTEXT("CreateBakedMaterial_Failed1", "Material baking: Material must be a Material Instance")))
				->AddToken(FUObjectToken::Create(BakedMaterial.Material));
			return false;
		}

		FGeneratedMaterialInstance NewEntry;
		if (bCreateInstancesOfBakedMaterials)
		{
			NewEntry.MaterialInstance = TStrongObjectPtr<UMaterialInstanceConstant>(UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(BakedMaterialInstance, GeneratedAssetOuter));
		}
		else
		{
			// Baked materials must be MICs when bCreateInstancesOfBakedMaterials is false
			NewEntry.MaterialInstance = TStrongObjectPtr(CastChecked<UMaterialInstanceConstant>(BakedMaterialInstance));
		}
		NewEntry.AdditionalMaterialSlotNames = BakedMaterial.AdditionalMaterialSlotNames;

		if (const FSkeletalMaterial* FoundMaterial = Algo::FindBy(InOutSkelMeshMaterials, BakedMaterial.PrimaryMaterialSlotName, &FSkeletalMaterial::MaterialSlotName))
		{
			for (FName ParameterToCopy : BakedMaterial.ParametersToCopy)
			{
				float ScalarParam;
				FLinearColor VectorParam;
				UTexture* TextureParam;
				if (FoundMaterial->MaterialInterface->GetScalarParameterValue(ParameterToCopy, ScalarParam))
				{
					NewEntry.MaterialInstance->SetScalarParameterValueEditorOnly(ParameterToCopy, ScalarParam);
				}
				else if (FoundMaterial->MaterialInterface->GetVectorParameterValue(ParameterToCopy, VectorParam))
				{
					NewEntry.MaterialInstance->SetVectorParameterValueEditorOnly(ParameterToCopy, VectorParam);
				}
				else if (FoundMaterial->MaterialInterface->GetTextureParameterValue(ParameterToCopy, TextureParam))
				{
					NewEntry.MaterialInstance->SetTextureParameterValueEditorOnly(ParameterToCopy, TextureParam);
				}
			}

			// Remove metadata for the material we are replacing so it doesn't get unpacked
			InOutGeneratedAssets.RemoveAssetMetadata(FoundMaterial->MaterialInterface);
		}

		// Add the replacement material to the list
		FName OutputMaterialName = BakedMaterial.OutputMaterialName;
		
		// Select the material name based on the use of Virtual Texture or not
		if (bUseVirtualTextures && BakedMaterial.bOutputsMaterialWithVirtualTextures)
		{
			OutputMaterialName = BakedMaterial.OutputMaterialNameVT;
		}

		InOutGeneratedAssets.Metadata.Emplace(NewEntry.MaterialInstance.Get(), BakedMaterial.OutputMaterialFolder, OutputMaterialName.ToString());

		NewMaterialInstances.Add(BakedMaterial.PrimaryMaterialSlotName, NewEntry);
	}

	struct FGeneratedTexture
	{
		UMaterialInstanceConstant* MaterialInstance = nullptr;
		FName ParameterName;
		TSoftObjectPtr<UTexture> Texture;
		bool bSetToVirtual = false;
	};

	TArray<FGeneratedTexture> GeneratedTextures;

	BakeTask.EnterProgressFrame(1.0f, LOCTEXT("RunningTextureGraphs", "Running Texture Graphs"));

	for (const FMetaHumanTextureGraphOutputProperties& Graph : BakingSettings->TextureGraphs)
	{
		if (!Graph.TextureGraphInstance)
		{
			return false;
		}

		// Do not bake textures if all output slots are removed
		{
			bool bHasActiveSlots = false;
			for (const FMetaHumanOutputTextureProperties& OutputTexture : Graph.OutputTextures)
			{
				for (const FName& OutputMaterialSlotName : OutputTexture.OutputMaterialSlotNames)
				{
					if (!RemovedMaterialSlots.Contains(OutputMaterialSlotName))
					{
						bHasActiveSlots = true;
						break;
					}
				}

				if (bHasActiveSlots)
				{
					break;
				}
			}

			if (!bHasActiveSlots)
			{
				continue;
			}
		}

		UObject* TextureGraphOuter = BakingSettings->bGenerateTextureGraphInstanceAssets ? NotNullGet(GeneratedAssetOuter) : GetTransientPackage();
		UTextureGraphInstance* TextureGraphInstance = DuplicateObject<UTextureGraphInstance>(Graph.TextureGraphInstance, TextureGraphOuter);

		// If the user wants to keep the TGIs, generate metadata so that they get unpacked
		if (BakingSettings->bGenerateTextureGraphInstanceAssets)
		{
			FMetaHumanGeneratedAssetMetadata& Metadata = InOutGeneratedAssets.Metadata.AddDefaulted_GetRef();
			Metadata.Object = TextureGraphInstance;
			Metadata.PreferredSubfolderPath = TEXT("TextureGraphs");
			Metadata.PreferredName = TextureGraphInstance->GetName();
		}

		if (!TextureGraphInstance->Graph())
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error()
				->AddToken(FTextToken::Create(LOCTEXT("BakeFaceMatsFailure_TextureGraphInvalid1", "Material baking: Material is in an invalid state after being duplicated")))
				->AddToken(FUObjectToken::Create(Graph.TextureGraphInstance));

			return false;
		}

		for (const TPair<FName, float>& ValuePair : Graph.InputValues)
		{
			const FName ValueName = ValuePair.Key;
			const float Value = ValuePair.Value;
			if (FVarArgument* Argument = TextureGraphInstance->InputParams.VarArguments.Find(ValueName))
			{
				Argument->Var.SetAs(Value);
			}
			else
			{
				const FText Message = FText::Format(LOCTEXT("BakeFaceMatsFailure_InputValueNotFound", "Material baking: Failed to find parameter for input value '{0}'"),
													FText::FromName(ValueName));
				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message)
					->AddToken(FUObjectToken::Create(Graph.TextureGraphInstance));

				return false;
			}
		}

		bool bSkipDueToPreviousLODMatch = true;
		for (const FMetaHumanInputMaterialProperties& InputMaterial : Graph.InputMaterials)
		{
			FVarArgument* Argument = TextureGraphInstance->InputParams.VarArguments.Find(InputMaterial.InputParameterName);
			if (!Argument)
			{
				const FText Message = FText::Format(
					LOCTEXT("BakeFaceMatsFailure_InputParamNotFound", "Material baking: Failed to find input parameter {0}"),
					FText::FromName(InputMaterial.InputParameterName));

				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message)
					->AddToken(FUObjectToken::Create(Graph.TextureGraphInstance));

				return false;
			}

			if (InputMaterial.MainSectionTopLODIndex != INDEX_NONE
				&& MaterialChangesPerLOD.IsValidIndex(InputMaterial.MainSectionTopLODIndex)
				&& MaterialChangesPerLOD[InputMaterial.MainSectionTopLODIndex] != INDEX_NONE)
			{
				int32 PreviousValidChangeNumber = INDEX_NONE;
				for (int32 MaterialChangeIndex = InputMaterial.MainSectionTopLODIndex - 1; MaterialChangeIndex >= 0; MaterialChangeIndex--)
				{
					if (MaterialChangesPerLOD[MaterialChangeIndex] != INDEX_NONE)
					{
						PreviousValidChangeNumber = MaterialChangesPerLOD[MaterialChangeIndex];
						break;
					}
				}

				if (PreviousValidChangeNumber == MaterialChangesPerLOD[InputMaterial.MainSectionTopLODIndex])
				{
					// This is the same as the previous LOD's material, so skip this bake
					continue;
				}
			}

			// At least one input material is different, so the bake should go ahead
			bSkipDueToPreviousLODMatch = false;

			UMaterialInterface* SourceMaterial = nullptr;

			const FSkeletalMaterial* MaterialSlot = Algo::FindBy(InOutSkelMeshMaterials, InputMaterial.SourceMaterialSlotName, &FSkeletalMaterial::MaterialSlotName);
			if (MaterialSlot
				&& MaterialSlot->MaterialInterface)
			{
				SourceMaterial = MaterialSlot->MaterialInterface;
			}
			else 
			{
				const TObjectPtr<UMaterialInterface>* RemovedMaterialPtr = RemovedMaterialSlots.Find(InputMaterial.SourceMaterialSlotName);
				if (RemovedMaterialPtr
					&& *RemovedMaterialPtr)
				{
					SourceMaterial = *RemovedMaterialPtr;
				}
				else
				{
					const FText Message = FText::Format(
						LOCTEXT("BakeFaceMatsFailure_MaterialSlotNotFound", "Material baking: Failed to find material slot {0} on face mesh"),
						FText::FromName(InputMaterial.SourceMaterialSlotName));

					FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);

					return false;
				}
			}

			check(SourceMaterial);

			if (SourceMaterial->GetOuter() == GeneratedAssetOuter)
			{
				if (!BakingSettings->bGenerateTextureGraphInstanceAssets)
				{
					// The user doesn't want to keep the source materials, so remove them from the list of assets to unpack
					InOutGeneratedAssets.RemoveAssetMetadata(SourceMaterial);
				}
			}

			FTG_Material MaterialValue;
			MaterialValue.AssetPath = SourceMaterial->GetPathName();
			Argument->Var.SetAs(MaterialValue);
		}

		if (bSkipDueToPreviousLODMatch)
		{
			// Skip this bake
			continue;
		}

		for (const FMetaHumanOutputTextureProperties& OutputTexture : Graph.OutputTextures)
		{
			FTG_OutputSettings* OutputSettings = nullptr;
			for (TPair<FTG_Id, FTG_OutputSettings>& Pair : TextureGraphInstance->OutputSettingsMap)
			{
				// The Texture Graph team has provided us with this temporary workaround to get the 
				// output parameter name.
				//
				// The hardcoded constant will be removed when a proper solution is available.
				const int32 PinIndex = 3;
				const FTG_Id PinId(Pair.Key.NodeIdx(),  PinIndex);

				if (TextureGraphInstance->Graph()->GetParamName(PinId) == OutputTexture.OutputTextureNameInGraph)
				{
					OutputSettings = &Pair.Value;
					break;
				}
			}

			if (!OutputSettings)
			{
				const FText Message = FText::Format(
					LOCTEXT("BakeFaceMatsFailure_OutputTextureNotFound1", "Material baking: Failed to find output texture {0}"),
					FText::FromName(OutputTexture.OutputTextureNameInGraph));

				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message)
					->AddToken(FUObjectToken::Create(Graph.TextureGraphInstance));

				return false;
			}

			OutputSettings->FolderPath = *(BaseOutputFolder / OutputTexture.OutputTextureFolder);

			if (bUseVirtualTextures && OutputTexture.bOutputsVirtualTexture)
			{
				if (OutputTexture.OutputVirtualTextureName != NAME_None)
				{
					OutputSettings->BaseName = OutputTexture.OutputVirtualTextureName;
				}
			}
			else
			{
				if (OutputTexture.OutputTextureName != NAME_None)
				{
					OutputSettings->BaseName = OutputTexture.OutputTextureName;
				}
			}

			// Override the texture resolution if specified by the pipeline
			if (const EMetaHumanBuildTextureResolution* OverrideResolution = InMaterialBakingOptions.TextureResolutionsOverrides.Find(OutputTexture.OutputTextureName))
			{
				const int32 Resolution = static_cast<int32>(*OverrideResolution);

				if (Resolution <= 0 || Resolution > static_cast<int32>(EMetaHumanBuildTextureResolution::Res8192))
				{
					const FText Message = FText::Format(LOCTEXT("InvalidResolution", "Invalid texture resolution override for texture '{0}': '{1}'"),
														FText::FromName(OutputTexture.OutputTextureName),
														Resolution);
					FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				}
				else
				{
					OutputSettings->Width = static_cast<EResolution>(Resolution);
					OutputSettings->Height = static_cast<EResolution>(Resolution);
				}
			}

			for (const FName& OutputMaterialSlotName : OutputTexture.OutputMaterialSlotNames)
			{
				// Ignore removed slots
				if (RemovedMaterialSlots.Contains(OutputMaterialSlotName))
				{
					continue;
				}

				if (NewMaterialInstances.Contains(OutputMaterialSlotName))
				{
					FGeneratedTexture& GeneratedTexture = GeneratedTextures.AddDefaulted_GetRef();
					GeneratedTexture.MaterialInstance = NewMaterialInstances[OutputMaterialSlotName].MaterialInstance.Get();

					if (OutputTexture.bOutputsVirtualTexture && bUseVirtualTextures)
					{
						GeneratedTexture.ParameterName = OutputTexture.OutputMaterialParameterNameVT;
					}
					else
					{
						GeneratedTexture.ParameterName = OutputTexture.OutputMaterialParameterName;
					}
					

					const FString PackageName = OutputSettings->FolderPath.ToString() / OutputSettings->BaseName.ToString();
					const FString AssetPath = FString::Format(TEXT("{0}.{1}"), { PackageName, OutputSettings->BaseName.ToString() });
					GeneratedTexture.Texture = TSoftObjectPtr<UTexture>(FSoftObjectPath(AssetPath));
					GeneratedTexture.bSetToVirtual = OutputTexture.bOutputsVirtualTexture && bUseVirtualTextures;
				}
				else
				{
					const FText Message = FText::Format(
						LOCTEXT("BakeFaceMatsFailure_BakedMaterialNotFound", "Failed to find a Baked Material entry with PrimaryMaterialSlotName set to {0}. This is being referenced by an output texture."),
						FText::FromName(OutputMaterialSlotName));

					FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);

					return false;

				}
			}
		}

		const bool bOverwriteTextures = true;
		const bool bSave = false;
		const bool bExportAll = false;
		const bool bDisableCache = true; // Disable the TG cache since we only need to run the TG instances once
										 // Works around issues with memory allocated for the cache not getting released in the editor
		UTG_AsyncExportTask* Task = UTG_AsyncExportTask::TG_AsyncExportTask(TextureGraphInstance, bOverwriteTextures, bSave, bExportAll, bDisableCache);
		Task->ActivateBlocking(nullptr);
		Task->MarkAsGarbage();

		// Running GC at this point will free the memory allocated by the TG export task and not needed since the created TG instance will not be re-used
		// This helps to reduce the total memory usage spike of running all the MH TG instances one after another
		if (UE::MetaHuman::Private::CVarMHCEnableGCOnTextureBaking.GetValueOnAnyThread())
		{
			TryCollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	BakeTask.EnterProgressFrame(1.0f, LOCTEXT("AssigningGeneratedTextures", "Assigning baked textures"));

	for (const FGeneratedTexture& GeneratedTexture : GeneratedTextures)
	{
		UTexture* ActualTexture = GeneratedTexture.Texture.Get();
		if (ActualTexture)
		{
			// Texture Graph generated textures are created with RF_MarkAsRootSet, which means they won't be garbage collected, even when not being referenced.
			// This is important when exporting to UEFN as the project is mounted as a plugin and if there are objects that are not garbage collected the plugin
			// will fail to unload. The textures exported here are meant to be referenced by some material so its safe to remove them from root here
			ActualTexture->RemoveFromRoot();

			const bool bAlreadyIncluded = InOutGeneratedAssets.Metadata.ContainsByPredicate([ActualTexture](const FMetaHumanGeneratedAssetMetadata& CandidateMetadata)
																							{
																								return CandidateMetadata.Object == ActualTexture;
																							});

			if (!bAlreadyIncluded)
			{
				// Add the texture metadata to the list of generated assets
				FMetaHumanGeneratedAssetMetadata TextureMetadata;
				TextureMetadata.Object = ActualTexture;
				InOutGeneratedAssets.Metadata.Emplace(TextureMetadata);

				OutGeneratedTextures.Add(ActualTexture);
			}

			if (GeneratedTexture.bSetToVirtual && !ActualTexture->IsCurrentlyVirtualTextured())
			{
				ActualTexture->PreEditChange(nullptr);
				ActualTexture->VirtualTextureStreaming = GeneratedTexture.bSetToVirtual;
				ActualTexture->UpdateResource();
				ActualTexture->PostEditChange();
			}

			if (GeneratedTexture.ParameterName == NAME_None)
			{
				FMessageLog(UE::MetaHuman::MessageLogName)
					.Warning(LOCTEXT("BakeMaterials_InvalidTextureParamName1", "Invalid texture parameter name for generated texture"))
					->AddToken(FUObjectToken::Create(ActualTexture))
					->AddText(LOCTEXT("BakeMaterials_InvalidTextureParamName2", "when trying to set texture in material"))
					->AddToken(FUObjectToken::Create(GeneratedTexture.MaterialInstance));
			}

			GeneratedTexture.MaterialInstance->SetTextureParameterValueEditorOnly(GeneratedTexture.ParameterName, ActualTexture);
		}
		else
		{
			const FText Message = FText::Format(
				LOCTEXT("BakeFaceMatsFailure_BakedTextureNotFound", "Couldn't find baked texture {0}. This should have been produced by the texture graph."),
				FText::FromString(GeneratedTexture.Texture.ToString()));

			FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);

			return false;
		}
	}

	BakeTask.EnterProgressFrame(1.0f, LOCTEXT("AssigningMaterials", "Assigning materials to Face mesh"));

	for (const TPair<FName, FGeneratedMaterialInstance>& NewMaterialInstance : NewMaterialInstances)
	{
		// Primary slot
		{
			FSkeletalMaterial* MaterialSlot = Algo::FindBy(InOutSkelMeshMaterials, NewMaterialInstance.Key, &FSkeletalMaterial::MaterialSlotName);
			if (MaterialSlot)
			{
				MaterialSlot->MaterialInterface = NewMaterialInstance.Value.MaterialInstance.Get();
			}
			else if (!RemovedMaterialSlots.Contains(NewMaterialInstance.Key))
			{
				const FText Message = FText::Format(
					LOCTEXT("BakeFaceMatsFailure_MaterialSlotNotFoundForBakedMaterial", "Failed to find material slot {0} on face mesh. This is referenced from the Baked Materials array."),
					FText::FromName(NewMaterialInstance.Key));

				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);

				return false;
			}
		}

		// Additional slots
		for (const FName AdditionalMaterialSlotName : NewMaterialInstance.Value.AdditionalMaterialSlotNames)
		{
			FSkeletalMaterial* MaterialSlot = Algo::FindBy(InOutSkelMeshMaterials, AdditionalMaterialSlotName, &FSkeletalMaterial::MaterialSlotName);
			if (MaterialSlot)
			{
				MaterialSlot->MaterialInterface = NewMaterialInstance.Value.MaterialInstance.Get();
			}
			else if (!RemovedMaterialSlots.Contains(AdditionalMaterialSlotName))
			{
				const FText Message = FText::Format(
					LOCTEXT("BakeFaceMatsFailure_MaterialSlotNotFoundForBakedMaterial", "Failed to find material slot {0} on face mesh. This is referenced from the Baked Materials array."),
					FText::FromName(AdditionalMaterialSlotName));

				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);

				return false;
			}
		}
	}

	return true;
}

bool UMetaHumanDefaultEditorPipelineBase::ConfirmUpdateCommonAssetsForBP(TNotNull<const UObject*> InGeneratedBP, const FString& InAssetPath) const
{
	const UE::MetaHuman::FMetaHumanAssetVersion MetaHumanBPVersion = FMetaHumanCharacterEditorBuild::GetMetaHumanAssetVersion(InGeneratedBP);
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetPath"), FText::FromString(InAssetPath));
	Args.Add(TEXT("AssetMetaHumanVersion"), FText::FromString(MetaHumanBPVersion.AsString()));
	FText Message;

	if (MetaHumanBPVersion == UE::MetaHuman::FMetaHumanAssetVersion(0, 0))
	{
		// Manually created and populated BPs won't have a version tag and would default to 0.0
		Message = FText::Format(
			LOCTEXT("AssemblyManualBPOverwriteWarning", "The assembly is about to write over a manually created MetaHuman Actor Blueprint. "
				"Continuing may break functionality on this MetaHuman. Do you wish to continue?\n\n"
				"{AssetPath}"), Args
		);

	}
	else if (MetaHumanBPVersion < FMetaHumanCharacterEditorBuild::GetFirstMetaHumanCompatibleVersion())
	{
		// Quixel imported MetaHumans have a non-zero version that is less than first compatible MH version
		Message = FText::Format(
			LOCTEXT("AssemblyQuixelBPOverwriteWarning", "The assembly is about to write over a MetaHuman Actor Blueprint imported with Quixel Bridge. "
				"Continuing may break functionality on this MetaHuman. Do you wish to continue?\n\n"
				"{AssetPath} - Version: {AssetMetaHumanVersion}"), Args
		);

	}
	else if (MetaHumanBPVersion < FMetaHumanCharacterEditorBuild::GetCurrentMetaHumanAssetVersion())
	{
		Message = FText::Format(
			LOCTEXT("AssemblyOldBPOverwriteWarning", "The assembly is about to write over a MetaHuman Actor Blueprint created with previous engine version. "
				"Continuing may break functionality on this MetaHuman. Do you wish to continue?\n\n"
				"{AssetPath} - Version: {AssetMetaHumanVersion}"), Args
		);
	}

	if (!Message.IsEmpty())
	{
		EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel, Message);
		if (Result == EAppReturnType::Cancel)
		{
			return false;
		}
	}

	return true;
}

UBlueprint* UMetaHumanDefaultEditorPipelineBase::WriteActorBlueprintHelper(
	TSubclassOf<AActor> InBaseActorClass,
	const FString& InBlueprintPath,
	const TFunction<bool(UBlueprint*)> CanReuseBlueprintFunc,
	const TFunction<UBlueprint*(UPackage*)> GenerateBlueprintFunc) const
{
	if (!InBaseActorClass)
	{
		return nullptr;
	}

	UPackage* BPPackage = UPackageTools::FindOrCreatePackageForAssetType(FName(InBlueprintPath), UBlueprint::StaticClass());
	const FString BlueprintShortName = FPackageName::GetShortName(InBlueprintPath);

	const FString AssetPath = BPPackage->GetPathName() + TEXT(".") + FPackageName::GetShortName(BPPackage);
	UBlueprint* GeneratedBP = LoadObject<UBlueprint>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);

	const bool bAssetAlreadyExisted = GeneratedBP != nullptr;

	if (GeneratedBP)
	{
		// Check BP MH version
		if (!ConfirmUpdateCommonAssetsForBP(GeneratedBP, AssetPath))
		{
			return nullptr;
		}

		if (!CanReuseBlueprintFunc(GeneratedBP))
		{
			FFormatNamedArguments FormatArguments;
			FormatArguments.Add(TEXT("TargetAssetName"), FText::FromString(BPPackage->GetPathName()));
			FormatArguments.Add(TEXT("BaseActorClass"), FText::FromString(InBaseActorClass->GetPathName()));

			const FText Message = FText::Format(
				LOCTEXT("ExistingBlueprintDifferentParentClass",
					"The generated actor blueprint can't be written to {TargetAssetName}, because the existing blueprint "
					"is not based on the actor class specified by the MetaHuman Character Pipeline, {BaseActorClass}.\n\n"
					"If you wish to overwrite the existing blueprint, delete it from the Content Browser and try again."),
				FormatArguments);

			FMessageLog(UE::MetaHuman::MessageLogName).Error(Message)
				->AddToken(FUObjectToken::Create(InBaseActorClass));

			return nullptr;
		}
	}

	// Generate a new Blueprint, replacing the existing one, if any
	GeneratedBP = GenerateBlueprintFunc(BPPackage);

	if (!GeneratedBP)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("FailedToGenerateBlueprint", "Failed to generate the MetaHuman actor blueprint."));
		return nullptr;
	}

	GeneratedBP->SetFlags(RF_Public | RF_Transactional | RF_Standalone);

	FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(GeneratedBP);
	GeneratedBP->MarkPackageDirty();

	const FBPCompileRequest Request(GeneratedBP, EBlueprintCompileOptions::None, nullptr);
	FBlueprintCompilationManager::CompileSynchronously(Request);

	// Check if compile was successful
	if (!GeneratedBP->IsUpToDate() || !GeneratedBP->GeneratedClass)
	{
		// Warn user but continue anyway
		FMessageLog(UE::MetaHuman::MessageLogName).Warning(LOCTEXT("NewBlueprintCompileError", "Generated blueprint failed to compile"))
			->AddToken(FUObjectToken::Create(GeneratedBP));
	}

	// Notify the asset registry so that the asset appears in the Content Browser
	if (!bAssetAlreadyExisted)
	{
		FAssetRegistryModule::AssetCreated(GeneratedBP);
	}

	return GeneratedBP;
}

bool UMetaHumanDefaultEditorPipelineBase::IsPluginAsset(TNotNull<UObject*> InObject)
{
	return FPackageName::GetPackageMountPoint(InObject->GetPackage()->GetName()) == UE_PLUGIN_NAME;
}

TNotNull<USkeleton*> UMetaHumanDefaultEditorPipelineBase::GenerateSkeleton(FMetaHumanCharacterGeneratedAssets& InGeneratedAssets,
																		   TNotNull<USkeleton*> InBaseSkeleton,
																		   const FString& InTargetFolderName,
																		   TNotNull<UObject*> InOuterForGeneratedAssets) const
{
	// By default, always return the generated skeleton
	return InBaseSkeleton;
}

void UMetaHumanDefaultEditorPipelineBase::RemoveLODsIfNeeded(FMetaHumanCharacterGeneratedAssets& InGeneratedAssets, TMap<FName, TObjectPtr<UMaterialInterface>>& OutRemovedMaterialSlots) const
{
	for (const FSkeletalMaterial& Material : InGeneratedAssets.FaceMesh->GetMaterials())
	{
		OutRemovedMaterialSlots.Add(Material.MaterialSlotName, Material.MaterialInterface);
	}

	// Get the face material set before removing LODs so unused materials can be removed later
	const FMetaHumanCharacterFaceMaterialSet OldFaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(InGeneratedAssets.FaceMesh);

	bool bFaceLODsModified = false;
	bool bBodyLODsModified = false;

	const int32 NumFaceLODs = InGeneratedAssets.FaceMesh->GetLODNum();

	// Configure the LODs of the exported character
	if (!LODProperties.FaceLODs.IsEmpty() && (LODProperties.FaceLODs.Num() < NumFaceLODs))
	{
		FMetaHumanCharacterEditorBuild::StripLODsFromMesh(InGeneratedAssets.FaceMesh, LODProperties.FaceLODs);
		bFaceLODsModified = true;
	}

	if (!LODProperties.BodyLODs.IsEmpty() && (LODProperties.BodyLODs.Num() < InGeneratedAssets.BodyMesh->GetLODNum()))
	{
		FMetaHumanCharacterEditorBuild::StripLODsFromMesh(InGeneratedAssets.BodyMesh, LODProperties.BodyLODs);
		bBodyLODsModified = true;
	}

	if (LODProperties.bOverrideFaceLODSettings)
	{
		InGeneratedAssets.FaceMesh->SetLODSettings(LODProperties.FaceLODSettings.LoadSynchronous());
		bFaceLODsModified = true;
	}

	if (LODProperties.bOverrideBodyLODSettings)
	{
		InGeneratedAssets.BodyMesh->SetLODSettings(LODProperties.BodyLODSettings.LoadSynchronous());
		bBodyLODsModified = true;
	}

	// Call PostEditChange to build the skeletal mesh in case LODs were modified
	if (bFaceLODsModified)
	{
		InGeneratedAssets.FaceMesh->PostEditChange();
	}

	if (bBodyLODsModified)
	{
		InGeneratedAssets.BodyMesh->PostEditChange();
	}

	// Remove any slots that are still on the mesh from the "removed" list.
	//
	// Any slots remaining on the list must have been removed.
	for (const FSkeletalMaterial& Material : InGeneratedAssets.FaceMesh->GetMaterials())
	{
		OutRemovedMaterialSlots.Remove(Material.MaterialSlotName);
	}

	// Get the new face material set from the face mesh after removing LODs and unused materials
	const FMetaHumanCharacterFaceMaterialSet NewFaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(InGeneratedAssets.FaceMesh);

	if (bFaceLODsModified)
	{
		// Remove all unused materials from the list of generated assets if they are no longer used by the face mesh
		OldFaceMaterialSet.ForEachSkinMaterial<UMaterialInstance>(
			[&NewFaceMaterialSet, &InGeneratedAssets](EMetaHumanCharacterSkinMaterialSlot SkinMaterialSlot, UMaterialInstance* OldMaterialInstance)
			{
				if (!NewFaceMaterialSet.Skin.Contains(SkinMaterialSlot))
				{
					InGeneratedAssets.RemoveAssetMetadata(OldMaterialInstance);
				}
			}
		);

		if (NewFaceMaterialSet.EyeLeft == nullptr && OldFaceMaterialSet.EyeLeft != nullptr)
		{
			InGeneratedAssets.RemoveAssetMetadata(OldFaceMaterialSet.EyeLeft);
		}

		if (NewFaceMaterialSet.EyeRight == nullptr && OldFaceMaterialSet.EyeRight != nullptr)
		{
			InGeneratedAssets.RemoveAssetMetadata(OldFaceMaterialSet.EyeRight);
		}

		if (NewFaceMaterialSet.Eyelashes == nullptr && OldFaceMaterialSet.Eyelashes != nullptr)
		{
			InGeneratedAssets.RemoveAssetMetadata(OldFaceMaterialSet.Eyelashes);
		}

		if (NewFaceMaterialSet.EyelashesHiLods == nullptr && OldFaceMaterialSet.EyelashesHiLods != nullptr)
		{
			InGeneratedAssets.RemoveAssetMetadata(OldFaceMaterialSet.EyelashesHiLods);
		}
	}

	auto IsStaticSwitchEnabled = [](const TArray<FSkeletalMaterial>& Materials, FName ParamName)
	{
		return Algo::AnyOf(Materials,
						   [ParamName](const FSkeletalMaterial& Material)
						   {
							   bool bIsEnabled = false;

							   if (UMaterialInterface* MaterialInterface = Material.MaterialInterface)
							   {
								   FGuid Guid;
								   const bool bOverridenOnly = false;
								   Material.MaterialInterface->GetStaticSwitchParameterValue(ParamName, bIsEnabled, Guid, bOverridenOnly);
							   }

							   return bIsEnabled;
						   });
	};

	// Remove Textures that are not used based on features enabled in the remaining materials
	if (!IsStaticSwitchEnabled(InGeneratedAssets.FaceMesh->GetMaterials(), FMetaHumanCharacterSkinMaterials::UseAnimatedMapsParamName))
	{
		// Remove the animated maps if they are not being used by any of the face materials

		bool bHasLoggedOnce = false;
		for (EFaceTextureType AnimatedMap : UE::MetaHuman::Private::GetAnimatedMapTypes())
		{
			if (const TObjectPtr<UTexture2D>* FoundAnimatedMap = InGeneratedAssets.SynthesizedFaceTextures.Find(AnimatedMap))
			{
				UTexture2D* AnimatedMapToRemove = *FoundAnimatedMap;
				InGeneratedAssets.RemoveAssetMetadata(AnimatedMapToRemove);
				InGeneratedAssets.SynthesizedFaceTextures.Remove(AnimatedMap);
				AnimatedMapToRemove->MarkAsGarbage();
			}
			else if (!bHasLoggedOnce)
			{
				bHasLoggedOnce = true;

				const FString TexturesList = FString::JoinBy(
					InGeneratedAssets.SynthesizedFaceTextures, 
					TEXT(", "), 
					[](const TPair<EFaceTextureType, TObjectPtr<UTexture2D>>& Pair)
					{
						return StaticEnum<EFaceTextureType>()->GetNameStringByValue(static_cast<int64>(Pair.Key)) 
							+ TEXT(" = ")
							+ GetPathNameSafe(Pair.Value);
					});

				UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Failed to remove animated map {AnimatedMap} from texture array. Remaining textures: {Textures}",
					StaticEnum<EFaceTextureType>()->GetNameStringByValue(static_cast<int64>(AnimatedMap)), TexturesList);
			}
		}
	}

	// Remove the cavity map if not being used by any of the face materials
	if (!IsStaticSwitchEnabled(InGeneratedAssets.FaceMesh->GetMaterials(), FMetaHumanCharacterSkinMaterials::UseCavityParamName))
	{
		if (const TObjectPtr<UTexture2D>* FoundCavityMap = InGeneratedAssets.SynthesizedFaceTextures.Find(EFaceTextureType::Cavity))
		{
			UTexture2D* CavityMap = *FoundCavityMap;
			InGeneratedAssets.RemoveAssetMetadata(CavityMap);
			InGeneratedAssets.SynthesizedFaceTextures.Remove(EFaceTextureType::Cavity);
			CavityMap->MarkAsGarbage();
		}
	}
}

#undef LOCTEXT_NAMESPACE
