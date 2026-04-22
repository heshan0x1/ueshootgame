// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UserAssetTagProvider.h"
#include "UserAssetTagProvider_Project.generated.h"

/** 
 *  A provider with the commonly used tags for this project, per asset type.
 *  You can assign tags per asset type in the project settings.
 */
UCLASS(DisplayName="Project Tags by type")
class USERASSETTAGSEDITOR_API UUserAssetTagProvider_Project : public UUserAssetTagProvider
{
	GENERATED_BODY()

	virtual FText GetDisplayNameText(const UUserAssetTagEditorContext* Context) const override;
	virtual TSet<FName> GetSuggestedUserAssetTags(const UUserAssetTagEditorContext* Context) const override;
	virtual FResultWithUserFeedback IsValid(const UUserAssetTagEditorContext* Context) const override;
	
	static void NavigateToProjectTags();
protected:
	virtual void AddToolbarMenuEntries(class UToolMenu* DynamicMenu, const UUserAssetTagEditorContext* Context) const override;
};
