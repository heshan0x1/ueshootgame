// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImageCore.h"

#include "MetaHumanGeometryRemovalTypes.generated.h"

class UTexture2D;

namespace UE::MetaHuman::GeometryRemoval
{
	/** Settings that specify how the pixel values in a hidden face map should be interpreted. */
	USTRUCT(BlueprintType)
	struct FHiddenFaceMapSettings
	{
		GENERATED_BODY()
		
		/**
		 * Any triangles *fully* covered by pixel values below this threshold will be removed.
		 * 
		 * In other words, geometry will be removed in regions of the hidden face map that are closer to 
		 * zero (i.e. black) than this.
		 */
		UPROPERTY(EditAnywhere, Category = "HiddenFaceMap")
		float MaxCullValue = 0.1f;

		/**
		 * Geometry in regions of the hidden face map that are closer to one (i.e. white) than this 
		 * will be left untouched.
		 */
		UPROPERTY(EditAnywhere, Category = "HiddenFaceMap")
		float MinKeepValue = 0.9f;
		
		/**
		 * Vertices in regions of the hidden face map that are between MaxCullValue and MinKeepValue
		 * will be "shrunk", i.e. moved in the opposite direction of their vertex normal. 
		 * 
		 * This helps to keep them out of the way of any overlapping geometry without removing them 
		 * completely.
		 * 
		 * The distance a vertex will be moved depends on its pixel value in the hidden face map.
		 * The distance is calculated by using the pixel value as a weight to interpolate between 0 
		 * and the specified maximum distance.
		 * 
		 * In other words, pixels close to MaxCullValue will be shrunk the maximum distance, whereas
		 * pixels close to MinKeepValue will be moved very little.
		 */
		UPROPERTY(EditAnywhere, Category = "HiddenFaceMap")
		float MaxShrinkDistance = 0.0f;
	};

	/** A hidden face map texture and the settings to use when applying it */
	USTRUCT(BlueprintType)
	struct FHiddenFaceMapTexture
	{
		GENERATED_BODY()
		
		UPROPERTY(EditAnywhere, Category = "HiddenFaceMap")
		TObjectPtr<UTexture2D> Texture;

		UPROPERTY(EditAnywhere, Category = "HiddenFaceMap")
		FHiddenFaceMapSettings Settings;
	};

	/** A hidden face map image and the settings to use when applying it */
	struct FHiddenFaceMapImage
	{
		FImage Image;

		/** An optional debug string that will be used in error messages to identify this hidden face map */
		FString DebugName;

		FHiddenFaceMapSettings Settings;
	};

} // namespace UE::MetaHuman::GeometryRemoval
