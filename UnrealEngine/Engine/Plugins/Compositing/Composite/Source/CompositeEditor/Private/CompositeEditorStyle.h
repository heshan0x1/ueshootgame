// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Styling/SlateStyle.h"

class FCompositeEditorStyle	final : public FSlateStyleSet
{
public:
	static FCompositeEditorStyle& Get();

	static void Register();
	static void Unregister();
	
private:
	FCompositeEditorStyle();
};
