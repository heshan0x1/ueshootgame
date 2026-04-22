// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeometryRemovalTypes.h"

#include "Containers/ArrayView.h"
#include "Misc/NotNull.h"

struct FImage;
class FText;
class USkeletalMesh;
class UTexture2D;

namespace UE::MetaHuman::GeometryRemoval
{
	/**
	 * Combines the source hidden face maps onto a provided destination map, so that multiple sets 
	 * of hidden faces can be removed in one call.
	 * 
	 * OutDestinationMap will be initialized by this function.
	 * 
	 * Returns false if the inputs are invalid in some way (e.g. zero size source texture) and sets
	 * OutFailureReason to an explanation for the failure.
	 */
	bool METAHUMANCHARACTEREDITOR_API TryCombineHiddenFaceMaps(TConstArrayView<FHiddenFaceMapImage> SourceMaps, FHiddenFaceMapImage& OutDestinationMap, FText& OutFailureReason);

	/**
	 * Converts the provided textures to images and copies the accompanying settings over.
	 * 
	 * Any null textures will be skipped, in which case the resulting image array will be smaller
	 * than the provided texture array.
	 * 
	 * Any failures in retrieving the image data from a non-null texture will cause this function
	 * to fail and return false. OutFailureReason will be set to an explanation for the failure.
	 */
	bool METAHUMANCHARACTEREDITOR_API TryConvertHiddenFaceMapTexturesToImages(TConstArrayView<FHiddenFaceMapTexture> SourceMaps, TArray<FHiddenFaceMapImage>& OutImages, FText& OutFailureReason);

	/**
	 * Copies the image contents into the given texture.
	 * 
	 * Texture only needs to be a valid UTexture2D object. It doesn't need to be initialized 
	 * in any particular way.
	 */
	void METAHUMANCHARACTEREDITOR_API UpdateHiddenFaceMapTextureFromImage(const FImage& Image, TNotNull<UTexture2D*> Texture);

	/**
	 * Removes and shrinks geometry in a skeletal mesh LOD according to the given HiddenFaceMap.
	 * 
	 * This is used to remove geometry that will be hidden, e.g. geometry of a body that is hidden
	 * by the clothing being worn on the body. This is done to avoid wasting performance and memory
	 * on geometry that will never be seen, but also to stop unseen geometry from intersecting with
	 * the geometry in front of it, e.g. the body showing through the clothes due to Z-fighting or 
	 * coarser geometry being used at lower LODs.
	 * 
	 * At the edges of the hidden area, e.g. around the edges of the clothes, there will be 
	 * geometry that is partially visible and therefore can't be removed, but could still intersect
	 * with geometry in front of it. For this reason, this function provides the ability to 
	 * "shrink" geometry by moving it a small distance in the opposite direction of its normal.
	 * 
	 * HiddenFaceMap is an image and some settings that control which geometry will be removed or 
	 * shrunk. For each vertex that's processed, the vertex's UV will be used to look up the image 
	 * to determine what modification to make to the vertex, if any.
	 * 
	 * Note that for backwards compatibility with the previous geometry removal system, all three
	 * color channels are sampled and the highest of the three values is used.
	 * 
	 * MaterialSlotsToProcess is an optional list of names of material slots (which correspond to 
	 * mesh sections) to operate on. If the list is empty, all material slots will be processed.
	 * 
	 * To understand how the hidden face map pixel values are used, see the comments on
	 * FHiddenFaceMapSettings.
	 */
	bool METAHUMANCHARACTEREDITOR_API RemoveAndShrinkGeometry(
		TNotNull<USkeletalMesh*> SkeletalMesh, 
		int32 LODIndex,
		const FHiddenFaceMapImage& HiddenFaceMap,
		TConstArrayView<FName> MaterialSlotsToProcess = TConstArrayView<FName>());

} // namespace UE::MetaHuman::GeometryRemoval
