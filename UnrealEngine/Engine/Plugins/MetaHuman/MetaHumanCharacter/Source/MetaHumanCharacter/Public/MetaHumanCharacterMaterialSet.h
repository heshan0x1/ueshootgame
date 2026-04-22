// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"

#include "MetaHumanCharacterMaterialSet.generated.h"

UENUM()
enum class EMetaHumanCharacterSkinMaterialSlot : uint8
{
	LOD0 = 0,
	LOD1,
	LOD2,
	LOD3,
	LOD4,
	LOD5to7,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterSkinMaterialSlot, EMetaHumanCharacterSkinMaterialSlot::Count);

USTRUCT()
struct METAHUMANCHARACTER_API FMetaHumanCharacterFaceMaterialSet
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<EMetaHumanCharacterSkinMaterialSlot, TObjectPtr<class UMaterialInstance>> Skin;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> EyeLeft;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> EyeRight;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> EyeShell;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> LacrimalFluid;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> Teeth;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> Eyelashes;

	UPROPERTY()
	TObjectPtr<class UMaterialInstance> EyelashesHiLods;

	/**
	 * Utility to iterate over all the skin materials casting them to a particular type
	 */
	template<typename MaterialType>
	void ForEachSkinMaterial(TFunction<void(EMetaHumanCharacterSkinMaterialSlot, MaterialType*)> InCallback) const
	{
		for (const TPair<EMetaHumanCharacterSkinMaterialSlot, TObjectPtr<class UMaterialInstance>>& SkinMaterialPair : Skin)
		{
			const EMetaHumanCharacterSkinMaterialSlot Slot = SkinMaterialPair.Key;
			class UMaterialInstance* Material = SkinMaterialPair.Value;

			if (MaterialType* SkinMaterial = Cast<MaterialType>(Material))
			{
				InCallback(Slot, SkinMaterial);
			}
		}
	}

	template<typename MaterialType>
	void ForEachEyelashMaterial(TFunction<void(MaterialType*)> InCallback) const
	{
		if (MaterialType* EyelashMaterial = Cast<MaterialType>(Eyelashes))
		{
			InCallback(EyelashMaterial);
		}

		if (MaterialType* EyelashMaterial = Cast<MaterialType>(EyelashesHiLods))
		{
			InCallback(EyelashMaterial);
		}
	}
};