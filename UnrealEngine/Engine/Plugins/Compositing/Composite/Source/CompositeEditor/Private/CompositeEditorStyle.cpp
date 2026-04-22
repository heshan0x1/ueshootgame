// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

FCompositeEditorStyle& FCompositeEditorStyle::Get()
{
	static FCompositeEditorStyle Instance;
	return Instance;
}

void FCompositeEditorStyle::Register()
{
	FSlateStyleRegistry::RegisterSlateStyle(Get());
}

void FCompositeEditorStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(Get());
}

FCompositeEditorStyle::FCompositeEditorStyle()
	: FSlateStyleSet("CompositeEditorStyle")
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);

	const FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("Composite"))->GetContentDir();
	FSlateStyleSet::SetContentRoot(ContentDir);
	FSlateStyleSet::SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate/"));
	
	Set("CompositeEditor.Composure", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ComposureCompositing", Icon16x16));

	Set("ClassIcon.CompositeActor", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ComposureCompositing", Icon16x16));
	Set("ClassIcon.CompositeLayerMainRender", new IMAGE_BRUSH_SVG("Editor/Icons/MainRender", Icon16x16));
	Set("ClassIcon.CompositeLayerShadowReflection", new IMAGE_BRUSH_SVG("Editor/Icons/Shadow", Icon16x16));
	Set("ClassIcon.CompositeLayerSingleLightShadow", new IMAGE_BRUSH_SVG("Editor/Icons/Shadow", Icon16x16));
	Set("ClassIcon.CompositeLayerSceneCapture", new CORE_IMAGE_BRUSH_SVG("Starship/AssetIcons/SceneCapture2D_16", Icon16x16));
	Set("ClassIcon.CompositeLayerPlate", new CORE_IMAGE_BRUSH_SVG("Starship/AssetIcons/MediaPlayer_16", Icon16x16));
	Set("ClassIcon.CompositePassColorGrade", new CORE_IMAGE_BRUSH_SVG("Starship/TimelineEditor/TrackTypeColor", Icon16x16));
	Set("ClassIcon.CompositePassColorKeyer", new IMAGE_BRUSH_SVG("Editor/Icons/Chromakey", Icon16x16));
	Set("ClassIcon.CompositePassFXAA", new CORE_IMAGE_BRUSH_SVG("Starship/Common/AntiAliasing", Icon16x16));
	Set("ClassIcon.CompositePassSMAA", new CORE_IMAGE_BRUSH_SVG("Starship/Common/AntiAliasing", Icon16x16));
	Set("ClassIcon.CompositePassOpenColorIO", new IMAGE_BRUSH_SVG("Editor/Icons/OCIO", Icon16x16));
	Set("ClassIcon.CompositePassMaterial", new CORE_IMAGE_BRUSH_SVG("Starship/AssetIcons/Material_16", Icon16x16));
	
    Set("CompositeEditor.Passes.Media", new CORE_IMAGE_BRUSH_SVG("Starship/AssetIcons/MediaPlayer_16", Icon16x16));
	Set("CompositeEditor.Passes.Scene", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Levels", Icon16x16));
	Set("CompositeEditor.Passes.Layer", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Layers", Icon16x16));

	Set("CompositeEditor.MediaProfile", new IMAGE_BRUSH_SVG("Editor/Icons/MediaProfile", Icon16x16));
}
