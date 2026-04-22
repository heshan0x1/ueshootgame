// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterGeneratedAssets.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCharacterGeneratedAssets)

bool FMetaHumanCharacterGeneratedAssets::RemoveAssetMetadata(TNotNull<const UObject*> InAsset)
{
	const int NumRemoved = Metadata.RemoveAll(
		[InAsset](const FMetaHumanGeneratedAssetMetadata& Candidate)
		{
			return Candidate.Object == InAsset;
		}
	);
	return NumRemoved > 0;
}