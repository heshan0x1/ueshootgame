// Copyright Epic Games, Inc. All Rights Reserved.

#include "Style/MetaHumanCalibrationStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Math/Vector2D.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"

#include "Interfaces/IPluginManager.h"

FMetaHumanCalibrationStyle::FMetaHumanCalibrationStyle()
	: FSlateStyleSet("MetaHumanCalibrationStyle")
{
	SetContentRoot(IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetContentDir());

	// Scrubber
	Set("MetaHumanCalibration.Scrubber", FSliderStyle()
		.SetNormalBarImage(FSlateColorBrush(FColor::White))
		.SetDisabledBarImage(FSlateColorBrush(FLinearColor::Gray))
		.SetNormalThumbImage(IMAGE_BRUSH("Icons/Scrubber", FVector2D(3.0f, 12.0f)))
		.SetHoveredThumbImage(IMAGE_BRUSH("Icons/Scrubber", FVector2D(3.0f, 12.0f)))
		.SetDisabledThumbImage(IMAGE_BRUSH("Icons/Scrubber", FVector2D(3.0f, 12.0f)))
		.SetBarThickness(4.0f)
	);

	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon16x16(16.0f, 16.0f);

	Set("MetaHumanCalibration.Generator.Icon", new IMAGE_BRUSH_SVG("Icons/CalibrationGenerator_16", Icon16x16));
	Set("MetaHumanCalibration.Generator.Run", new IMAGE_BRUSH_SVG("Icons/RunCalibration_20", Icon20x20));
	Set("MetaHumanCalibration.Generator.AutoFrameSelection", new IMAGE_BRUSH_SVG("Icons/RunAutoFrame_20", Icon20x20));
	Set("MetaHumanCalibration.Generator.Config", new IMAGE_BRUSH_SVG("Icons/Config_20", Icon20x20));
	Set("MetaHumanCalibration.Generator.Add", new IMAGE_BRUSH_SVG("Icons/Plus_20", Icon20x20));
	Set("MetaHumanCalibration.Generator.Delete", new IMAGE_BRUSH_SVG("Icons/Delete_20", Icon20x20));
	Set("MetaHumanCalibration.Generator.Reset", new IMAGE_BRUSH_SVG("Icons/Reset_20", Icon20x20));

	Set("MetaHumanCalibration.Diagnostics.Icon", new IMAGE_BRUSH_SVG("Icons/DetectFeatures_16", Icon16x16));

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FMetaHumanCalibrationStyle::~FMetaHumanCalibrationStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FMetaHumanCalibrationStyle& FMetaHumanCalibrationStyle::Get()
{
	static FMetaHumanCalibrationStyle Style;
	return Style;
}