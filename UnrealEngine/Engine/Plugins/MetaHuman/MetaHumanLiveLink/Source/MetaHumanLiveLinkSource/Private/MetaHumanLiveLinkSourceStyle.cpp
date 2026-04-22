// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLiveLinkSourceStyle.h"

#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/CoreStyle.h"
#include "Interfaces/IPluginManager.h"



FMetaHumanLiveLinkSourceStyle::FMetaHumanLiveLinkSourceStyle() : FSlateStyleSet(TEXT("MetaHumanLiveLinkSourceStyle"))
{
	SetContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	Set("Refresh", new IMAGE_BRUSH_SVG("Starship/Common/Update", CoreStyleConstants::Icon16x16));

	SetContentRoot(IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetContentDir());

	Set("IncreaseSize", new IMAGE_BRUSH_SVG("Icons/WindowScaleUp_16", CoreStyleConstants::Icon16x16));
	Set("DecreaseSize", new IMAGE_BRUSH_SVG("Icons/WindowScaleDown_16", CoreStyleConstants::Icon16x16));
	Set("RestoreSize", new IMAGE_BRUSH_SVG("Icons/WindowScaleRestore_16", CoreStyleConstants::Icon16x16));
	Set("RestoreView", new IMAGE_BRUSH_SVG("Icons/WindowViewRestore_16", CoreStyleConstants::Icon16x16));
}

void FMetaHumanLiveLinkSourceStyle::Register()
{
	FSlateStyleRegistry::RegisterSlateStyle(Get());
}

void FMetaHumanLiveLinkSourceStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(Get());
}

FMetaHumanLiveLinkSourceStyle& FMetaHumanLiveLinkSourceStyle::Get()
{
	static FMetaHumanLiveLinkSourceStyle Inst;
	return Inst;
}