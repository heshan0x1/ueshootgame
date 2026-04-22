// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserDataLegacyBridge.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentBrowserDataLegacyBridge)

namespace ContentBrowserDataLegacyBridge
{

FOnCreateNewAsset& OnCreateNewAsset()
{
	static FOnCreateNewAsset OnCreateNewAssetDelegate;
	return OnCreateNewAssetDelegate;
}

}
