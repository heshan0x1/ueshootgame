// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanTypes.h"

#include "MetaHumanCharacterSkin.generated.h"

USTRUCT(BlueprintType)
struct FMetaHumanCharacterSkinProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float U = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float V = 0.5f;

	UPROPERTY(VisibleAnywhere, DisplayName = "Body Bias", Category = "Skin")
	FVector3f BodyBias = {74.f, 28.f, 15.f};

	UPROPERTY(VisibleAnywhere, DisplayName = "Body Gain", Category = "Skin")
	FVector3f BodyGain = {30.f, 10.f, 5.f};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Show Top Underwear", Category = "Skin")
	bool bShowTopUnderwear = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Body Texture Index", Category = "Skin", meta = (UIMin = "0", UIMax = "8", ClampMin = "0", ClampMax = "8"))
	int32 BodyTextureIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Face Texture Index", Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	int32 FaceTextureIndex = 0;

	// Roughness UI Multiply. Range from 0.85 to 1.15
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Roughness", Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.85", ClampMax = "1.15"))
	float Roughness = 1.06f;

	bool operator==(const FMetaHumanCharacterSkinProperties& InOther) const
	{
		return U == InOther.U &&
			V == InOther.V &&
			BodyTextureIndex == InOther.BodyTextureIndex &&
			FaceTextureIndex == InOther.FaceTextureIndex &&
			Roughness == InOther.Roughness;
	}

	bool operator!=(const FMetaHumanCharacterSkinProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

UENUM()
enum class EMetaHumanCharacterFrecklesMask : uint8
{
	None,
	Type1,
	Type2,
	Type3,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterFrecklesMask, EMetaHumanCharacterFrecklesMask::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterFrecklesProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Density", Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Density = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Strength", Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Strength = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Saturation", Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Saturation = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Tone Shift", Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ToneShift = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Mask", Category = "Freckles")
	EMetaHumanCharacterFrecklesMask Mask = EMetaHumanCharacterFrecklesMask::None;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAccentRegionProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Redness", Category = "Accents", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Redness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Saturation", Category = "Accents", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Saturation = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Lightness", Category = "Accents", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Lightness = 0.5f;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAccentRegions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Scalp", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Scalp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Forehead", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Forehead;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Nose", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Nose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "UnderEye", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties UnderEye;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Cheeks", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Cheeks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Lips", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Lips;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Chin", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Chin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Ears", Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Ears;
};

UENUM()
enum class EMetaHumanCharacterSkinPreviewMaterial : uint8
{
	Default		UMETA(DisplayName = "Topology"),
	Editable	UMETA(DisplayName = "Skin"),
	Clay		UMETA(DisplayName = "Clay"),
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterSkinPreviewMaterial, EMetaHumanCharacterSkinPreviewMaterial::Count);

/**
 * Struct that hard references to all possible textures used in the skin material.
 * This is also used as a utility to pass around skin textures sets
 */
USTRUCT()
struct METAHUMANCHARACTER_API FMetaHumanCharacterSkinTextureSet
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<EFaceTextureType, TObjectPtr<class UTexture2D>> Face;

	UPROPERTY()
	TMap<EBodyTextureType, TObjectPtr<class UTexture2D>> Body;

	/**
	 * Appends another texture set to this one.
	 * Replaces or adds any new textures from InOther
	 */
	void Append(const FMetaHumanCharacterSkinTextureSet& InOther);
};

/**
 * Struct used to hold soft references to a skin texture set. This is
 * used to store override textures in the MetaHuman Character object
 * which are not loaded by default.
 */
USTRUCT(BlueprintType)
struct METAHUMANCHARACTER_API FMetaHumanCharacterSkinTextureSoftSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	TMap<EFaceTextureType, TSoftObjectPtr<class UTexture2D>> Face;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	TMap<EBodyTextureType, TSoftObjectPtr<class UTexture2D>> Body;

	/**
	 * Load the textures and returns a texture set
	 */
	FMetaHumanCharacterSkinTextureSet LoadTextureSet() const;
};

/**
 * Enum with the valid texture resolutions to request from the service
 */
UENUM()
enum class ERequestTextureResolution : int32
{
	Res2k = 2048 UMETA(DisplayName = "2k"),
	Res4k = 4096 UMETA(DisplayName = "4k"),
	Res8k = 8192 UMETA(DisplayName = "8k"),
};
ENUM_RANGE_BY_VALUES(ERequestTextureResolution, ERequestTextureResolution::Res2k, ERequestTextureResolution::Res4k, ERequestTextureResolution::Res8k);

USTRUCT(BlueprintType)
struct METAHUMANCHARACTER_API FMetaHumanCharacterTextureSourceResolutions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceAlbedo = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceNormal = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceCavity = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceAnimatedMaps = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyAlbedo = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyNormal = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyCavity = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyMasks = ERequestTextureResolution::Res2k;

	/**
	 * @brief Sets all resolutions to be the same
	 */
	void SetAllResolutionsTo(ERequestTextureResolution InResolution);

	/**
	 * @brief Returns true if all resolutions are the same as InResolution
	 */
	bool AreAllResolutionsEqualTo(ERequestTextureResolution InResolution) const;
};

USTRUCT(BlueprintType)
struct METAHUMANCHARACTER_API FMetaHumanCharacterSkinSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Skin", Category = "Skin", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterSkinProperties Skin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Freckles", Category = "Freckles", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterFrecklesProperties Freckles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Accents", Category = "Accents", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterAccentRegions Accents;

	// Desired resolutions to request when Downloading Source Textures
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Sources")
	FMetaHumanCharacterTextureSourceResolutions DesiredTextureSourcesResolutions;

	// Enables the use texture overrides in the skin material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Overrides")
	bool bEnableTextureOverrides = false;

	// If bEnableTextureOverrides is enabled, use textures in this texture set as textures of the skin material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Overrides", meta = (EditCondition = "bEnableTextureOverrides"))
	FMetaHumanCharacterSkinTextureSoftSet TextureOverrides;

	/**
	 * Returns a texture set considering the bEnableTextureOverrides flag. If the flag is enabled any texture
	 * in TextureOverrides are going to be present in the returned texture set
	 */
	FMetaHumanCharacterSkinTextureSet GetFinalSkinTextureSet(const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const;
};