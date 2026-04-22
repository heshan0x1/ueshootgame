// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define UE_API CHAOSCLOTHASSETEDITOR_API

namespace UE::Chaos::ClothAsset
{
/**
 * Slate style set for Cloth Editor
 */
class FChaosClothAssetEditorStyle
	: public FSlateStyleSet
{
public:
	UE_API const static FName StyleName;

	/** Access the singleton instance for this style set */
	static UE_API FChaosClothAssetEditorStyle& Get();

private:
	static UE_API FString InContent(const FString& RelativePath, const ANSICHAR* Extension);

	UE_API FChaosClothAssetEditorStyle();
	UE_API ~FChaosClothAssetEditorStyle();
};
} // namespace UE::Chaos::ClothAsset

#undef UE_API
