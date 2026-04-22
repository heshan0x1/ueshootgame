// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"

#include "MetaHumanCharacterEyes.generated.h"

UENUM()
enum class EMetaHumanCharacterEyesBlendMethod : uint8
{
	Radial = 0,
	Structural = 1
};

UENUM()
enum class EMetaHumanCharacterEyesIrisPattern : uint8
{
	Iris001 = 0,
	Iris002,
	Iris003,
	Iris004,
	Iris005,
	Iris006,
	Iris007,
	Iris008,
	Iris009,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterEyesIrisPattern, EMetaHumanCharacterEyesIrisPattern::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterEyeIrisProperties
{
	GENERATED_BODY()
	
	// Iris Pattern Masks / Iris Normal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Pattern", Category = "Iris", meta = (ScriptName = "Pattern"))
	EMetaHumanCharacterEyesIrisPattern IrisPattern = EMetaHumanCharacterEyesIrisPattern::Iris001;

	// Iris Rotation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Rotation", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", ScriptName = "Rotation"))
	float IrisRotation = 0.0f;	

	// Iris Primary Color Hue
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Primary Color U", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float PrimaryColorU = 0.5f;
	
	// Iris Primary Color Value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Primary Color V", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float PrimaryColorV = 0.5f;

	// Iris Secondary Color Hue
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Secondary Color U", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float SecondaryColorU = 0.5f;

	// Iris Secondary Color Value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Iris", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float SecondaryColorV = 0.5f;

	// Iris Color Blend Coverage
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Color Blend", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ColorBlend = 0.5f;

	// Iris Color Blend Coverage Softness
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Color Blend Softness", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ColorBlendSoftness = 0.5f;

	// Iris Color Blend Switch
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Blend Method", Category = "Iris")
	EMetaHumanCharacterEyesBlendMethod BlendMethod = EMetaHumanCharacterEyesBlendMethod::Structural;

	// Iris Shadow Details Amount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Shadow Details", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ShadowDetails = 0.5f;

	// Limbal Ring Size. Ranges from 0.6 to 0.85
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Limbal Ring Size", Category = "Iris",  meta = (UIMin = "0", UIMax = "1", ClampMin = "0.6", ClampMax = "0.85"))
	float LimbalRingSize = 0.725f;

	// Limbal Ring Softness. Ranges from 0.02 to 0.15
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Limbal Ring Softness", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.02", ClampMax = "0.15"))
	float LimbalRingSoftness = 0.085f;

	// Limbal Ring Color (Mult)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Limbal Ring Color", Category = "Iris", meta = (HideAlphaChannel))
	FLinearColor LimbalRingColor = FLinearColor::Black;

	// Iris Global Saturation. Ranges from 0.0 to 4.0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Global Saturation", Category = "Iris", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.0", ClampMax = "4.0"))
	float GlobalSaturation = 2.0f;

	// Iris Color Multiplyblend
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Global Tint", Category = "Iris", meta = (HideAlphaChannel))
	FLinearColor GlobalTint = FLinearColor::White;

	bool operator==(const FMetaHumanCharacterEyeIrisProperties& InOther) const
	{
		return IrisPattern == InOther.IrisPattern &&
			IrisRotation == InOther.IrisRotation &&
			PrimaryColorU == InOther.PrimaryColorU &&
			PrimaryColorV == InOther.PrimaryColorV &&
			SecondaryColorU == InOther.SecondaryColorU &&
			SecondaryColorV == InOther.SecondaryColorV &&
			ColorBlend == InOther.ColorBlend &&
			ColorBlendSoftness == InOther.ColorBlendSoftness &&
			BlendMethod == InOther.BlendMethod &&
			ShadowDetails == InOther.ShadowDetails &&
			LimbalRingColor == InOther.LimbalRingColor &&
			LimbalRingSize == InOther.LimbalRingSize &&
			LimbalRingSoftness == InOther.LimbalRingSoftness &&
			GlobalSaturation == InOther.GlobalSaturation &&
			GlobalTint == InOther.GlobalTint;
	}

	bool operator!=(const FMetaHumanCharacterEyeIrisProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterEyePupilProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Dilation", Category = "Pupil", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.85", ClampMax = "1.2"))
	float Dilation = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Feather", Category = "Pupil")
	float Feather = 0.45f;

	bool operator==(const FMetaHumanCharacterEyePupilProperties& InOther) const
	{
		return Dilation == InOther.Dilation &&
			Feather == InOther.Feather;
	}

	bool operator!=(const FMetaHumanCharacterEyePupilProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterEyeScleraProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Dilation", Category = "Sclera", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Rotation = 0.0f;
	
	// If enabled allows the use of a custom sclera tint value.
	// If disabled, the Sclera tint will be calculated based on the Skin Tone
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Use Custom Tint", Category = "Sclera")
	bool bUseCustomTint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Tint", Category = "Sclera", meta = (HideAlphaChannel, EditCondition = "bUseCustomTint"))
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Transmission Spread", Category = "Sclera", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.03", ClampMax = "0.2"))
	float TransmissionSpread = 0.115f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Transmission Color", Category = "Sclera")
	FLinearColor TransmissionColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Vascularity Intensity", Category = "Sclera", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.0", ClampMax = "2.0"))
	float VascularityIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Vascularity Coverage", Category = "Sclera", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.1", ClampMax = "0.4"))
	float VascularityCoverage = 0.2f;

	bool operator==(const FMetaHumanCharacterEyeScleraProperties& InOther) const
	{
		return Rotation == InOther.Rotation &&
			bUseCustomTint == InOther.bUseCustomTint &&
			(bUseCustomTint? Tint == InOther.Tint : true) &&
			TransmissionSpread == InOther.TransmissionSpread &&
			TransmissionColor == InOther.TransmissionColor &&
			VascularityIntensity == InOther.VascularityIntensity &&
			VascularityCoverage == InOther.VascularityCoverage;
	}

	bool operator!=(const FMetaHumanCharacterEyeScleraProperties& InOther) const
	{
		return !(*this == InOther);
	}

};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterEyeCorneaProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Size", Category = "Cornea", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.145", ClampMax = "0.185"))
	float Size = 0.165f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Limbus Softness", Category = "Cornea", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.03", ClampMax = "0.15"))
	float LimbusSoftness = 0.09f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Limbus Color", Category = "Cornea", meta = (HideAlphaChannel))
	FLinearColor LimbusColor = FLinearColor::White;

	bool operator==(const FMetaHumanCharacterEyeCorneaProperties& InOther) const
	{
		return Size == InOther.Size &&
			LimbusSoftness == InOther.LimbusSoftness &&
			LimbusColor == InOther.LimbusColor;
	}

	bool operator!=(const FMetaHumanCharacterEyeCorneaProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

USTRUCT(BlueprintType)
struct METAHUMANCHARACTER_API FMetaHumanCharacterEyeProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Iris", Category = "Iris", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyeIrisProperties Iris;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Pupil", Category = "Pupil", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyePupilProperties Pupil;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Cornea", Category = "Cornea", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyeCorneaProperties Cornea;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Sclera", Category = "Sclera", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyeScleraProperties Sclera;

	bool operator==(const FMetaHumanCharacterEyeProperties& InOther) const
	{
		return Iris == InOther.Iris &&
			Pupil == InOther.Pupil &&
			Cornea == InOther.Cornea &&
			Sclera == InOther.Sclera;
	}

	bool operator!=(const FMetaHumanCharacterEyeProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

USTRUCT(BlueprintType)
struct METAHUMANCHARACTER_API FMetaHumanCharacterEyesSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Eye Left", Category = "Eye", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyeProperties EyeLeft;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Eye Right", Category = "Eye", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyeProperties EyeRight;

	bool operator==(const FMetaHumanCharacterEyesSettings& InOther) const
	{
		return EyeLeft == InOther.EyeLeft &&
			EyeRight == InOther.EyeRight;
	}

	bool operator!=(const FMetaHumanCharacterEyesSettings& InOther) const
	{
		return !(*this == InOther);
	}
};

UENUM()
enum class EMetaHumanCharacterEyelashesType : uint8
{
	None,
	Sparse,
	ShortFine,
	Thin,
	SlightCurl,
	LongCurl,
	ThickCurl,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterEyelashesType, EMetaHumanCharacterEyelashesType::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterEyelashesProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Type", Category = "Eyelashes")
	EMetaHumanCharacterEyelashesType Type = EMetaHumanCharacterEyelashesType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Dye Color", Category = "Eyelashes", meta = (HideAlphaChannel))
	FLinearColor DyeColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Melanin", Category = "Eyelashes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Melanin = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Redness", Category = "Eyelashes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Redness = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Roughness", Category = "Eyelashes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Roughness = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Salt And Pepper", Category = "Eyelashes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float SaltAndPepper = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Lightness", Category = "Eyelashes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Lightness = 0.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Enable Grooms", Category = "Eyelashes")
	bool bEnableGrooms = true;

	bool operator==(const FMetaHumanCharacterEyelashesProperties& InOther) const
	{
		return Type == InOther.Type &&
			DyeColor == InOther.DyeColor &&
			Melanin == InOther.Melanin &&
			Redness == InOther.Redness &&
			Roughness == InOther.Roughness &&
			SaltAndPepper == InOther.SaltAndPepper &&
			Lightness == InOther.Lightness &&
			bEnableGrooms == InOther.bEnableGrooms;
	}

	bool operator!=(const FMetaHumanCharacterEyelashesProperties& InOther) const
	{
		return !(*this == InOther);
	}

	bool AreMaterialsUpdated(const FMetaHumanCharacterEyelashesProperties& InOther) const
	{
		return !(DyeColor == InOther.DyeColor &&
			Melanin == InOther.Melanin &&
			Redness == InOther.Redness &&
			Roughness == InOther.Roughness);
	}
};
