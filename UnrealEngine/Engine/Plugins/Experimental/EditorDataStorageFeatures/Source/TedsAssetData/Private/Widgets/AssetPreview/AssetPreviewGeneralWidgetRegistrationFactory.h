// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "AssetPreviewGeneralWidgetRegistrationFactory.generated.h"

UCLASS()
class UAssetPreviewGeneralWidgetRegistrationFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UAssetPreviewGeneralWidgetRegistrationFactory() override = default;

	void RegisterWidgetPurposes(UE::Editor::DataStorage::IUiProvider& DataStorageUi) const override;
};
