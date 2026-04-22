// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UserAssetTagProvider.h"
#include "UserAssetTagProvider_LocalAssetTypeFavorites.generated.h"

class UUserAssetTagEditorContext;

/**
 * The local favorites provider lets you add your own selection of favorite tags per asset type
 * for quick reuse without affecting project settings.
 */
UCLASS(DisplayName="Favorites by type")
class USERASSETTAGSEDITOR_API UUserAssetTagProvider_LocalAssetTypeFavorites : public UUserAssetTagProvider
{
	GENERATED_BODY()

public:
	UUserAssetTagProvider_LocalAssetTypeFavorites();

	virtual FText GetDisplayNameText(const UUserAssetTagEditorContext* Context) const override;
	virtual TSet<FName> GetSuggestedUserAssetTags(const UUserAssetTagEditorContext* Context) const override;

	/** Favorites are per asset type, so we disable this provider if we encounter multiple asset types. */
	virtual FResultWithUserFeedback IsValid(const UUserAssetTagEditorContext* Context) const override;

	virtual TSharedPtr<SWidget> AddAdditionalSuggestedWidgets(FName UserAssetTag, const UUserAssetTagEditorContext* Context) const override;
	virtual TSharedPtr<SWidget> AddAdditionalOwnedTagWidgets(FName UserAssetTag, const UUserAssetTagEditorContext* Context) const override;
};
