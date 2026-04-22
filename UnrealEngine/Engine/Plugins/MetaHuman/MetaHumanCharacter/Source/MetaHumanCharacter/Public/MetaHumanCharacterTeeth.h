// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"

#include "MetaHumanCharacterTeeth.generated.h"

UENUM()
enum class EMetaHumanCharacterTeethType : uint8
{
	// TODO names may change; this is how it is in titan currently
	None, 
	Variant_01,
	Variant_02,
	Variant_03,
	Variant_04,
	Variant_05,
	Variant_06,
	Variant_07,
	Variant_08,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterTeethType, EMetaHumanCharacterTeethType::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTeethProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Tooth Length", Category = "Teeth", meta = (UIMin = "-1", UIMax = "1", ClampMin = "-1", ClampMax = "1"))
	float ToothLength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Tooth Spacing", Category = "Teeth", meta = (UIMin = "-1", UIMax = "1", ClampMin = "-1", ClampMax = "1"))
	float ToothSpacing = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Upper Shift", Category = "Teeth", meta = (UIMin = "-1", UIMax = "1", ClampMin = "-1", ClampMax = "1"))
	float UpperShift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Lower Shift", Category = "Teeth", meta = (UIMin = "-1", UIMax = "1", ClampMin = "-1", ClampMax = "1"))
	float LowerShift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Overbite", Category = "Teeth", meta = (UIMin = "-1", UIMax = "1", ClampMin = "-1", ClampMax = "1"))
	float Overbite = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Overjet", Category = "Teeth", meta = (UIMin = "-1", UIMax = "1", ClampMin = "-1", ClampMax = "1"))
	float Overjet = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Worn Down", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float WornDown = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Polycanine", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Polycanine = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Receding Gums", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float RecedingGums = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Narrowness", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Narrowness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Variation", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Variation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Jaw Open", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float JawOpen = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Teeth Color", Category = "Teeth", meta = (HideAlphaChannel))
	FLinearColor TeethColor = FLinearColor::White;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Gum Color", Category = "Teeth", meta = (HideAlphaChannel))
	FLinearColor GumColor = FLinearColor::White;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Plaque Color", Category = "Teeth", meta = (HideAlphaChannel))
	FLinearColor PlaqueColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Plaque Amount", Category = "Teeth", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float PlaqueAmount = 0.0f;

	// TODO: Can probably be moved to Character Editor data
	UPROPERTY(Transient)
	bool EnableShowTeethExpression = false;

	bool operator==(const FMetaHumanCharacterTeethProperties& InOther) const
	{
		return ToothLength == InOther.ToothLength &&
			ToothSpacing == InOther.ToothSpacing &&
			UpperShift == InOther.UpperShift &&
			LowerShift == InOther.LowerShift &&
			Overbite == InOther.Overbite &&
			Overjet == InOther.Overjet &&
			WornDown == InOther.WornDown &&
			Polycanine == InOther.Polycanine &&
			RecedingGums == InOther.RecedingGums &&
			Narrowness == InOther.Narrowness &&
			Variation == InOther.Variation &&
			JawOpen == InOther.JawOpen &&
			TeethColor == InOther.TeethColor &&
			GumColor == InOther.GumColor &&
			PlaqueColor == InOther.PlaqueColor &&
			PlaqueAmount == InOther.PlaqueAmount &&
			EnableShowTeethExpression == InOther.EnableShowTeethExpression;
	}

	bool operator!=(const FMetaHumanCharacterTeethProperties& InOther) const
	{
		return !(*this == InOther);
	}

	bool AreMaterialsUpdated(const FMetaHumanCharacterTeethProperties& InOther) const
	{
		return !(TeethColor == InOther.TeethColor &&
			GumColor == InOther.GumColor &&
			PlaqueColor == InOther.PlaqueColor &&
			PlaqueAmount == InOther.PlaqueAmount);
	}

	bool IsVariantUpdated(const FMetaHumanCharacterTeethProperties& InOther) const
	{
		return !(ToothLength == InOther.ToothLength &&
			ToothSpacing == InOther.ToothSpacing &&
			UpperShift == InOther.UpperShift &&
			LowerShift == InOther.LowerShift &&
			Overbite == InOther.Overbite &&
			Overjet == InOther.Overjet &&
			WornDown == InOther.WornDown &&
			Polycanine == InOther.Polycanine &&
			RecedingGums == InOther.RecedingGums &&
			Narrowness == InOther.Narrowness &&
			Variation == InOther.Variation &&
			JawOpen == InOther.JawOpen);
	}
};