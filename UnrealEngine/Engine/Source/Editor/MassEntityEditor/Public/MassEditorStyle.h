// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

#define UE_API MASSENTITYEDITOR_API

class FMassEntityEditorStyle
{
public:
	static UE_API void Initialize();
	static UE_API void Shutdown();
	static TSharedPtr<ISlateStyle> Get() { return StyleSet; }
	static UE_API FName GetStyleSetName();

	static const FSlateBrush* GetBrush(FName PropertyName, const ANSICHAR* Specifier = NULL)
	{
		return StyleSet->GetBrush(PropertyName, Specifier);
	}

	static UE_API FString InContent(const FString& RelativePath, const ANSICHAR* Extension);


private:

	static UE_API TSharedPtr<FSlateStyleSet> StyleSet;
};

#undef UE_API
