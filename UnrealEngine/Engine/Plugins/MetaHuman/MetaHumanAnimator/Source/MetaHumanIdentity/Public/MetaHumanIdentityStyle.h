// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

#define UE_API METAHUMANIDENTITY_API

class FMetaHumanIdentityStyle
	: public FSlateStyleSet
{
public:
	static UE_API FMetaHumanIdentityStyle& Get();

	static UE_API void Register();
	static UE_API void Unregister();

private:
	UE_API FMetaHumanIdentityStyle();
};

#undef UE_API
