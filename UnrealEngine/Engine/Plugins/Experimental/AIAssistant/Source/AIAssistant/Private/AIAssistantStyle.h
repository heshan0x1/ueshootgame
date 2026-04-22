// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once


#include "Styling/SlateStyle.h"


//
// FAIAssistantStyle
//


class FAIAssistantStyle
{
public:

	
	static void Initialize();
	static void Shutdown();

	static void ReloadTextures();

	static const ISlateStyle& Get();

	static FName GetStyleSetName();

	
private:

	
	static TSharedRef< class FSlateStyleSet > Create();

	static TSharedPtr< class FSlateStyleSet > StyleInstance;
};