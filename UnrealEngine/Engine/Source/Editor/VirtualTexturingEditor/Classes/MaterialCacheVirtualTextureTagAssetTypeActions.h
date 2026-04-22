// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class FAssetTypeActions_MaterialCacheVirtualTextureTag : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_MaterialCacheVirtualTextureTag() {}

protected:
	//~ Begin FAssetTypeActions_Base Interface.
	virtual UClass* GetSupportedClass() const override;
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual uint32 GetCategories() override;
	virtual void GetActions(TArray<UObject*> const& InObjects, FMenuBuilder& MenuBuilder) override;
	//~ End FAssetTypeActions_Base Interface.
};
