// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeCategories.h"
#include "AssetTypeActions_Base.h"

class FAssetTypeActions_ComputeGraphFromText : public FAssetTypeActions_Base
{
	//~ Begin FAssetTypeActions_Base Interface.
	FText GetName() const override;
	FColor GetTypeColor() const override;
	UClass* GetSupportedClass() const override;
	uint32 GetCategories() override;
	//~ End FAssetTypeActions_Base Interface.

public:
	FAssetTypeActions_ComputeGraphFromText(EAssetTypeCategories::Type InAssetCategoryBit = EAssetTypeCategories::Misc);

private:
	EAssetTypeCategories::Type AssetCategoryBit;
};
