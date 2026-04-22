// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "AssetActionUtility.h"

#include "SubtitleAssetActionUtility.generated.h"

// Asset Action Utility class for any editor right-click actions relevant to subtitles.
UCLASS()
class USubtitleAssetActionUtility : public UAssetActionUtility
{
	GENERATED_BODY()

public:
	USubtitleAssetActionUtility();

	UFUNCTION(CallInEditor)
	void ConvertBasicOverlaysToSubtitles();


};