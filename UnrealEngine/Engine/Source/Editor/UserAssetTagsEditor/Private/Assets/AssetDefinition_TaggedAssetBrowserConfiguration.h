// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_TaggedAssetBrowserConfiguration.generated.h"

/**
 * 
 */
UCLASS()
class USERASSETTAGSEDITOR_API UAssetDefinition_TaggedAssetBrowserConfiguration : public UAssetDefinitionDefault
{
	GENERATED_BODY()

	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
