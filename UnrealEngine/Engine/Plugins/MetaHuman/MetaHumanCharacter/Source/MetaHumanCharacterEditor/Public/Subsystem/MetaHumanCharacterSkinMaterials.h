// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/EnumRange.h"
#include "Misc/NotNull.h"
#include "MetaHumanCharacterTeeth.h"
#include "MetaHumanCharacterMakeup.h"
#include "MetaHumanCharacterEyes.h"
#include "MetaHumanCharacterSkin.h"
#include "MetaHumanCharacterMaterialSet.h"
#include "MetaHumanTypes.h"
#include "Engine/DataTable.h"

#include "MetaHumanCharacterSkinMaterials.generated.h"

UENUM()
enum class EMetaHumanCharacterAccentRegion : uint8
{
	Scalp,
	Forehead,
	Nose,
	UnderEye,
	Cheeks,
	Lips,
	Chin,
	Ears,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterAccentRegion, EMetaHumanCharacterAccentRegion::Count);

UENUM()
enum class EMetaHumanCharacterAccentRegionParameter : uint8
{
	Redness,
	Saturation,
	Lightness,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterAccentRegionParameter, EMetaHumanCharacterAccentRegionParameter::Count);

UENUM()
enum class EMetaHumanCharacterFrecklesParameter : uint8
{
	Mask,
	Density,
	Strength,
	Saturation,
	ToneShift,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterFrecklesParameter, EMetaHumanCharacterFrecklesParameter::Count);

USTRUCT()
struct FMetaHumanCharacterSkinMaterialOverrideRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Scalar Parameters")
	TMap<FName, float> ScalarParameterValues;
};

struct FMetaHumanCharacterSkinMaterials
{
	/**
	 * Returns the material slot names for the skin materials
	 */
	METAHUMANCHARACTEREDITOR_API static FName GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot InSlot);

	/**
	 * @brief Returns the material parameter name for the a given synthesized texture type
	 * 
	 * @param InTextureType the face texture type to get the parameter name for
	 * @param bInWithVTSupport Whether or not to return the virtual texture parameter name if supported
	 */
	METAHUMANCHARACTEREDITOR_API static FName GetFaceTextureParameterName(EFaceTextureType InTextureType, bool bInWithVTSupport = false);

	/**
	 * @brief Returns the material parameter name for the a given body texture type
	 * 
	 * @param InTextureType the body texture type to get the parameter name for
	 * @param bInWithVTSupport Whether or not to return the virtual texture parameter name if supported
	 */
	METAHUMANCHARACTEREDITOR_API static FName GetBodyTextureParameterName(EBodyTextureType InTextureType, bool bInWithVTSupport = false);

	static const FName EyeLeftSlotName;
	static const FName EyeRightSlotName;
	static const FName SalivaSlotName;
	static const FName EyeShellSlotName;
	static const FName EyeEdgeSlotName;
	static const FName TeethSlotName;
	static const FName EyelashesSlotName;
	static const FName EyelashesHiLodSlotName;
	METAHUMANCHARACTEREDITOR_API static const FName UseCavityParamName;
	METAHUMANCHARACTEREDITOR_API static const FName UseAnimatedMapsParamName;
	static const FName UseTextureOverrideParamName;
	static const FName RoughnessUIMultiplyParamName;

	static void SetHeadMaterialsOnMesh(const FMetaHumanCharacterFaceMaterialSet& InMaterialSet, TNotNull<class USkeletalMesh*> InMesh);
	static void SetBodyMaterialOnMesh(TNotNull<class UMaterialInterface*> InBodyMaterial, TNotNull<class USkeletalMesh*> InMesh);

	/**
	 * Creates a Face Material set from the materials in the given face mesh
	 */
	METAHUMANCHARACTEREDITOR_API static FMetaHumanCharacterFaceMaterialSet GetHeadMaterialsFromMesh(TNotNull<class USkeletalMesh*> InFaceMesh);

	// Number of rows and columns to use when building the foundation color picker palette
	static constexpr int32 FoundationPaletteColumns = 7;
	static constexpr int32 FoundationPaletteRows = 5;
	static constexpr float FoundationSaturationShift = 0.1f;
	static constexpr float FoundationValueShift = 0.1f;

	/**
	 * Shifts the input color based on the preset index to calculate the final foundation color to apply.
	 * If InColorIndex is INDEX_NONE returns InColor so that no shift happens.
	 */
	METAHUMANCHARACTEREDITOR_API static FLinearColor ShiftFoundationColor(const FLinearColor& InColor, int32 InColorIndex, int32 InShowColumns, int32 InShowRows, float InSaturationShift, float InValueShift);

	static void ApplyTextureOverrideParameterToMaterials(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Apply skin material parameter overrides based on the face texture index for better visuals
	 */
	static void ApplySkinParametersToMaterials(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, TNotNull<class UMaterialInstanceDynamic*> InBodyMID, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Apply the Roughness UI Multiply to the skin materials
	 */
	static void ApplyRoughnessMultiplyToMaterials(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Update the preview material parameter value of the given accent region.
	 */
	static void ApplySkinAccentParameterToMaterial(
		const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet,
		EMetaHumanCharacterAccentRegion InRegion,
		EMetaHumanCharacterAccentRegionParameter InParameter,
		float InValue);

	/**
	 * Updates the accent region parameters in the given face material set
	 */
	static void ApplySkinAccentsToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterAccentRegions& InAccentProperties);

	/**
	 * Updates the freckles mask in the given face material set
	 */
	static void ApplyFrecklesMaskToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, EMetaHumanCharacterFrecklesMask InMask);

	/**
	 * Updates one of the freckles material parameters in the given face material set
	 */
	static void ApplyFrecklesParameterToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, EMetaHumanCharacterFrecklesParameter InParam, float InValue);

	/**
	 * Updates all freckle parameters in the given face material set
	 */
	static void ApplyFrecklesToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterFrecklesProperties& InFrecklesProperties);

	/**
	 * @brief Apply makeup settings to the given face material set
	 * 
	 * @param InFaceMaterialSet the set of face materials to where the foundation properties are going to be applied
	 * @param bInWithVTSupport Whether or not apply textures to virtual texture slots when supported
	 */
	static void ApplyFoundationMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterFoundationMakeupProperties& InFoundationMakeupProperties, bool bInWithVTSupport);
	static void ApplyEyeMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterEyeMakeupProperties& InEyeMakeupProperties, bool bInWithVTSupport);
	static void ApplyBlushMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterBlushMakeupProperties& InBlushMakeupProperties, bool bInWithVTSupport);
	static void ApplyLipsMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterLipsMakeupProperties& InLipsMakeupProperties, bool bInWithVTSupport);

	/**
	 * Helper to apply update the MH face material so that it references the (transient) synthesized textures
	 */
	static void ApplySynthesizedTexturesToFaceMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InSynthesizedFaceTextures);

	/**
	 * Helper to apply all eye material settings to the given face material set
	 */
	static void ApplyEyeSettingsToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterEyesSettings& InEyeSettings);

	/**
	 * Set the Sclera tint based on skin tone U value if not using a custom sclera tint.
	 * InOutEyeSettings will have its sclera tint values based on the skin tone
	 */
	static void ApplyEyeScleraTintBasedOnSkinTone(const FMetaHumanCharacterSkinSettings& InSkinSettings, FMetaHumanCharacterEyesSettings& InOutEyeSettings);

	/**
	 * Read the eye settings from the default eye material
	 */
	static void GetDefaultEyeSettings(FMetaHumanCharacterEyesSettings& OutEyeSettings);

	/**
	* Applies eyelashes material properties to given face material set
	*/
	static void ApplyEyelashesPropertiesToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties);

	/**
	* Applies teeth material properties to given face material set
	*/
	static void ApplyTeethPropertiesToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterTeethProperties& InTeethProperties);

	/**
	 * @brief Returns a new material instance for the head for a given preview material type
	 * 
	 * @param bInWithVTSupport Returns the materials that have support for virtual textures
	 */
	static FMetaHumanCharacterFaceMaterialSet GetHeadPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, bool bInWithVTSupport);

	/**
	 * Returns a new material instance for the body for a given preview material type
	 * 
	 * @param bInWithVTSupport Returns the materials that have support for virtual texures
	 */
	static class UMaterialInstanceDynamic* GetBodyPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, bool bInWithVTSupport);

	/**
	 * Set the parent of InMaterial to InNewParent preserving overrides and static switches
	 */
	METAHUMANCHARACTEREDITOR_API static void SetMaterialInstanceParent(TNotNull<class UMaterialInstanceConstant*> InMaterial, TNotNull<class UMaterialInterface*> InNewParent);

	/**
	* Returns the active mask texture used for the eyelashes mesh given the input eyelashes properties
	*/
	static class UTexture2D* GetEyelashesMask(const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties);
};
