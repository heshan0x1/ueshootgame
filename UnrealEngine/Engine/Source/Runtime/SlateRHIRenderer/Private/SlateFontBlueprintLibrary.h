// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Fonts/SlateFontInfo.h"
#include "UObject/Class.h"
#include "SlateFontBlueprintLibrary.generated.h"

SLATERHIRENDERER_API DECLARE_LOG_CATEGORY_EXTERN(LogSlateFontBlueprint, Log, All);

UCLASS()
class USlateFontBlueprintLibrary : public UObject
{
	GENERATED_BODY()

	UFUNCTION(BlueprintPure, Category = "Slate Font", meta = (NativeMakeFunc))
	static SLATERHIRENDERER_API FSlateFontInfo MakeSlateFontInfo(const UObject* FontObject, UObject* FontMaterial, FFontOutlineSettings OutlineSettings, FName TypefaceFontName, float Size = 24.0f, int32 LetterSpacing = 0, float SkewAmount = 0.0f, bool bForceMonospaced = false, bool bMaterialIsStencil = false, float MonospacedWidth = 1.0f);
};