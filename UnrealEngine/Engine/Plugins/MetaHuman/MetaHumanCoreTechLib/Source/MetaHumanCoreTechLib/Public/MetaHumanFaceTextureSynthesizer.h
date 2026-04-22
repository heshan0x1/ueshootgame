// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector2D.h"
#include "ImageCore.h"
#include "Templates/PimplPtr.h"

#define UE_API METAHUMANCORETECHLIB_API

class FMetaHumanFaceTextureAttributeMap;

/**
* UE API for creating MH Face textures based on the Texture Synthesis module feature set developed in titan 
*/
class FMetaHumanFaceTextureSynthesizer
{
public:

	UE_API FMetaHumanFaceTextureSynthesizer();
	~FMetaHumanFaceTextureSynthesizer()
	{
		Clear();
	}

	FMetaHumanFaceTextureSynthesizer(const FMetaHumanFaceTextureSynthesizer& InOther) = delete;

	/**
	* Initialize the Texture Synthesis object by loading all the necessary model data from the MetaHumanCharacter content
	* InTextureSynthesisFolderPath should point to a folder with the texture synthesis model data as used by the UE data loader
	* InNumThreads is the number of threads to use for each texture synthesis map, pass 0 for single thread synthesis
	*/
	UE_API bool Init(const FString& InTextureSynthesisFolderPath, int32 InNumThreads = 0);

	/**
	* Returns whether the Texture Synthesis object has been initialized and the internal model data are valid
	*/
	UE_API bool IsValid() const;

	/**
	* Releases any memory allocated by the Texture Synthesis object
	*/
	UE_API void Clear();

	/**
	* Get the maximum value for the HF index the model supports
	*/
	UE_API int32 GetMaxHighFrequencyIndex() const;

	/**
	* Size of the generated Textures along the first dimension
	*/
	UE_API int32 GetTextureSizeX() const;

	/**
	* Size of the generated Textures along the second dimension
	*/
	UE_API int32 GetTextureSizeY() const;

	/*
	* Image format of the generated textures
	*/
	UE_API ERawImageFormat::Type GetTextureFormat() const;

	/*
	* Color space of the generated textures
	*/
	UE_API EGammaSpace GetTextureColorSpace() const;

	/**
	* Enum with indices corresponding to the TS model supported maps
	*/
	enum class EMapType : int32
	{
		Base = 0,
		Animated0 = 1,
		Animated1 = 2,
		Animated2 = 3,
	};

	/*
	* Returns the albedo map types that can be synthesized by this model (without input HF maps)
	*/
	UE_API TArray<EMapType> GetSupportedAlbedoMapTypes() const;
	
	/*
	 * Returns the normal map types that can be selected by this model
	*/
	UE_API TArray<EMapType> GetSupportedNormalMapTypes() const;

	/**
	* Parameters for synthesizing a texture from the model
	*/
	struct FTextureSynthesisParams
	{
		FVector2f	SkinUVFromUI;		// UV skin coordinates, as defined in the MHC UI
		int32		HighFrequencyIndex;	// Index to the HF map
		EMapType	MapType;			// Index to one of the supported map types
	};

	/**
	 * Gets the Skin Tone for a given Skin UV value
	 * @returns Color in sRGB space
	*/
	UE_API FLinearColor GetSkinTone(const FVector2f& InUV) const;

	/**
	 * Projects a skin tone to the texture model and returns the Skin UV Value
	 * The returned UV value is clamped to [0,1] if the input skin tone is projected
	 * outside the bounds what the texture model can represent.
	 * @param InSkinTone the skin tone to project in sRGB space.
	 */
	UE_API FVector2f ProjectSkinTone(const FLinearColor& InSkinTone, const int HFIndex) const;

	/**
	 * Gets the body albedo gain for a given Skin UV value
	 * @returns Albedo gain
	*/
	UE_API FVector3f GetBodyAlbedoGain(const FVector2f& InUV) const;

	/**
	* Synthesize an albedo texture map with the input parameters and store in the passed ImageView object
	* OutAlbedoImage should be pre-allocated with the appropriate size and format
	*/
	UE_API bool SynthesizeAlbedo(const FTextureSynthesisParams& InTextureSynthesisParams, FImageView OutAlbedoImage) const;

	/**
	* Synthesize an albedo texture map using the input HF maps
	* OutAlbedoImage should be pre-allocated with the same target resolution of the input HFMap and this model output format
	*/
	UE_API bool SynthesizeAlbedoWithHF(const FTextureSynthesisParams& InTextureSynthesisParams, const TStaticArray<TArray<uint8>, 4>& InHFMaps, FImageView OutAlbedoImage) const;

	/**
	* Select a normal texture map based on the input parameters
	* OutNormalImage should be pre-allocated with the appropriate size and format
	*/
	UE_API bool SelectNormal(const FTextureSynthesisParams& InTextureSynthesisParams, FImageView OutNormalImage) const;

	/**
	* Select the cavity texture map to be used for this HF index
	* OutCavityImage should be pre-allocated with the appropriate size and format
	*/
	UE_API bool SelectCavity(int32 HighFrequencyIndex, FImageView OutCavityImage) const;


	/**
	 * Gets the texture attribute map associated with the face texture synthesizer.
	 */
	UE_API const FMetaHumanFaceTextureAttributeMap& GetFaceTextureAttributeMap() const;

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};


class FMetaHumanFaceTextureAttributeMap
{
public:
	UE_API bool Init(const FString& InTextureSynthesisFolderPath, int32 NumTextures);

	int32 NumAttributes() const { return AttributeNames.Num(); }

	UE_API const FString& GetAttributeName(int32 Idx) const;

	UE_API const TArray<FString>& GetAttributeValueNames(int32 Idx) const;

	UE_API const TArray<int32>& GetAttributeValues(int32 Idx) const;

	const TArray<int32>& GetAllIndices() const { return AllIndices; }

	UE_API TArray<int32> Filter(int32 AttributeIndex, int32 AttributeValue, const TArray<int32>& InIndices) const;

private:
	TArray<FString> AttributeNames;
	TArray<TArray<FString>> AttributeValueNames;
	TArray<TArray<int32>> AttributeValues;
	TArray<int32> AllIndices;
};


class FMetaHumanFilteredFaceTextureIndices
{
public:
	UE_API FMetaHumanFilteredFaceTextureIndices(const FMetaHumanFaceTextureAttributeMap& FaceTextureAttributeMap, const TArray<int32>& AttributeValues);

	int32 Num() const { return Indices.Num();  }

	UE_API int32 ConvertTextureIndexToFilterIndex(int32 TextureIndex) const;

	UE_API int32 ConvertFilterIndexToTextureIndex(int32 FilterIndex) const;

private:
	TArray<int32> Indices;
};

#undef UE_API
