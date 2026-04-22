// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

#define UE_API METAHUMANTOOLKIT_API

class FMetaHumanToolkitStyle
	: public FSlateStyleSet
{
public:
	static UE_API FMetaHumanToolkitStyle& Get();

	static UE_API void Register();
	static UE_API void Unregister();

private:
	UE_API FMetaHumanToolkitStyle();
};

#undef UE_API
