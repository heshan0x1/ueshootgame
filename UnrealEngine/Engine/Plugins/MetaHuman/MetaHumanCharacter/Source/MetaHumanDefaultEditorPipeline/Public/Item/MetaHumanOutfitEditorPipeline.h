// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanOutfitEditorPipeline.generated.h"

class USkeletalMesh;

USTRUCT()
struct FMetaHumanOutfitPipelineBuildInput
{
	GENERATED_BODY()

public:
};

UCLASS(EditInlineNew)
class METAHUMANDEFAULTEDITORPIPELINE_API UMetaHumanOutfitEditorPipeline : public UMetaHumanItemEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanOutfitEditorPipeline();

	virtual void BuildItem(
		const FMetaHumanPaletteItemPath& ItemPath,
		TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
		const FInstancedStruct& BuildInput,
		TArrayView<const FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections,
		TArrayView<const FMetaHumanPaletteItemPath> SortedItemsToExclude,
		FMetaHumanPaletteBuildCacheEntry& BuildCache,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		ITargetPlatform* TargetPlatform,
		TNotNull<UObject*> OuterForGeneratedObjects,
		const FOnBuildComplete& OnComplete) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;

	// UObject interface
	virtual void PostLoad() override;

	/**
	 * If this clothing covers any part of the head mesh, you may want to provide a hidden face map
	 * to remove or shrink the head geometry out of the way of the clothing. 
	 * 
	 * This helps to prevent the head and clothing geometry from intersecting, especially at lower
	 * LODs where the geometry is coarser.
	 * 
	 * The skin mesh sections will sample in their UV space from this map to determine if they 
	 * should be removed, shrunk or left unmodified. Shrinking is useful for geometry near the 
	 * edges of the clothing that is partly visible.
	 * 
	 * Note that for backwards compatibility with the previous geometry removal system, all three
	 * color channels are sampled and the highest of the three values is used.
	 */
	UPROPERTY(EditAnywhere, Category = "Outfit")
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMapTexture;

	/**
	 * This hidden face map works in the same way as the HeadHiddenFaceMapTexture, except it's 
	 * applied to the body mesh.
	 */
	UPROPERTY(EditAnywhere, Category = "Outfit")
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMapTexture;

private:
	UPROPERTY()
	TObjectPtr<UTexture2D> BodyHiddenFaceMap_DEPRECATED;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
