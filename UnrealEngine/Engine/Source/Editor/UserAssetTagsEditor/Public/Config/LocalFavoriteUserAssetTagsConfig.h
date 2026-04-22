// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorConfigBase.h"
#include "LocalFavoriteUserAssetTagsConfig.generated.h"

USTRUCT()
struct FPerTypeFavoriteUserAssetTags
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="User Asset Tags")
	TSet<FName> FavoriteUserAssetTags;
};
/**
 * 
 */
UCLASS(EditorConfig="FavoriteUserAssetTags")
class USERASSETTAGSEDITOR_API ULocalFavoriteUserAssetTagsConfig : public UEditorConfigBase
{
	GENERATED_BODY()

public:
	static ULocalFavoriteUserAssetTagsConfig* Get();
	static void Shutdown();
	
	TMap<FSoftClassPath, FPerTypeFavoriteUserAssetTags>& GetFavoriteUserAssetTagsMutable() { return FavoriteUserAssetTagsPerClass; }
	
	const TMap<FSoftClassPath, FPerTypeFavoriteUserAssetTags>& GetFavoriteUserAssetTags() const { return FavoriteUserAssetTagsPerClass; }

	TSet<FName> GetFavoriteUserAssetTagsForClass(const UClass* Class);
	void ToggleFavoriteUserAssetTag(const UClass* Class, FName InUserAssetTag);
private:
	UPROPERTY(meta=(EditorConfig))
	TMap<FSoftClassPath, FPerTypeFavoriteUserAssetTags> FavoriteUserAssetTagsPerClass;

	UPROPERTY(meta=(EditorConfig))
	int32 MaxRecentUserAssetTags = 10;
		
	static TStrongObjectPtr<ULocalFavoriteUserAssetTagsConfig> Instance;
};
