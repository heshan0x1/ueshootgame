// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterSkin.h"

#include "Engine/Texture2D.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCharacterSkin)

namespace UE::MetaHuman
{
	template<typename TEnum>
	static TMap<TEnum, TObjectPtr<UTexture2D>> LoadTextures(const TMap<TEnum, TSoftObjectPtr<UTexture2D>>& InSoftTextures)
	{
		TMap<TEnum, TObjectPtr<UTexture2D>> LoadedTextures;
		for (const TPair<TEnum, TSoftObjectPtr<UTexture2D>>& SoftTexturePair : InSoftTextures)
		{
			const TEnum TextureType = SoftTexturePair.Key;
			const TSoftObjectPtr<UTexture2D> Texture = SoftTexturePair.Value;

			if (!Texture.IsNull())
			{
				if (UTexture2D* LoadedTexture = Texture.LoadSynchronous())
				{
					LoadedTextures.Add(TextureType, LoadedTexture);
				}
			}
		}
		return LoadedTextures;
	}
}

void FMetaHumanCharacterSkinTextureSet::Append(const FMetaHumanCharacterSkinTextureSet& InOther)
{
	Face.Append(InOther.Face);
	Body.Append(InOther.Body);
}

FMetaHumanCharacterSkinTextureSet FMetaHumanCharacterSkinTextureSoftSet::LoadTextureSet() const
{
	return FMetaHumanCharacterSkinTextureSet
	{
		.Face = UE::MetaHuman::LoadTextures(Face),
		.Body = UE::MetaHuman::LoadTextures(Body)
	};
}

FMetaHumanCharacterSkinTextureSet FMetaHumanCharacterSkinSettings::GetFinalSkinTextureSet(const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const
{
	FMetaHumanCharacterSkinTextureSet FinalSkinTextureSet = InSkinTextureSet;

	if (bEnableTextureOverrides)
	{
		FMetaHumanCharacterSkinTextureSet LoadedOverrides = TextureOverrides.LoadTextureSet();

		FinalSkinTextureSet.Append(LoadedOverrides);
	}

	return FinalSkinTextureSet;
}

void FMetaHumanCharacterTextureSourceResolutions::SetAllResolutionsTo(ERequestTextureResolution InResolution)
{
	for (TFieldIterator<FEnumProperty> It(StaticStruct()); It; ++It)
	{
		if (FEnumProperty* ResolutionProperty = *It)
		{
			ResolutionProperty->SetValue_InContainer(this, &InResolution);
		}
	}
}

bool FMetaHumanCharacterTextureSourceResolutions::AreAllResolutionsEqualTo(ERequestTextureResolution InResolution) const
{
	for (TFieldIterator<FEnumProperty> It(StaticStruct()); It; ++It)
	{
		if (const FEnumProperty* ResolutionProperty = *It)
		{
			ERequestTextureResolution Resolution = ERequestTextureResolution::Res2k;
			ResolutionProperty->GetValue_InContainer(this, &Resolution);

			if (InResolution != Resolution)
			{
				return false;
			}
		}
	}

	return true;
}