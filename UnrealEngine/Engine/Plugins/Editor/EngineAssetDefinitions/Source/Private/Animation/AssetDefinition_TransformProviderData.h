// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/TransformProviderData.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_TransformProviderData.generated.h"

UCLASS()
class UAssetDefinition_TransformProviderData : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_TransformProviderData", "Transform Provider Data"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(78, 40, 165)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UTransformProviderData::StaticClass(); }
};
