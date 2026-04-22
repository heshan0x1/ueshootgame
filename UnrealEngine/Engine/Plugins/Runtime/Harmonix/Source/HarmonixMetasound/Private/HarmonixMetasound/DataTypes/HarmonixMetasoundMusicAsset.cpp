// Copyright Epic Games, Inc. All Rights Reserved.

#include "HarmonixMetasound/DataTypes/HarmonixMetasoundMusicAsset.h"
#include "HarmonixMetasound/Interfaces/HarmonixMusicInterfaces.h"
#include "MetasoundSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HarmonixMetasoundMusicAsset)

bool UHarmonixMetasoundMusicAsset::AssetIsMissingHarmoixMusicInterface(const FAssetData& AssetData)
{
#if WITH_EDITORONLY_DATA
	bool GotDocInfo = false;
	Metasound::Frontend::FMetaSoundClassInfo ClassInfo(AssetData);
	return !ClassInfo.InheritsInterface(HarmonixMetasound::MusicAssetInterface::FrontendVersion.Name);
#else
	return false;
#endif
}
