// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "UserAssetTagEditorMenuContexts.generated.h"

UCLASS(MinimalAPI)
class UUserAssetTagEditorContext : public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<class SUserAssetTagsEditor> UserAssetTagsEditor;
};

UCLASS(MinimalAPI)
class UTaggedAssetBrowserMenuContext : public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<class STaggedAssetBrowser> TaggedAssetBrowser;
};