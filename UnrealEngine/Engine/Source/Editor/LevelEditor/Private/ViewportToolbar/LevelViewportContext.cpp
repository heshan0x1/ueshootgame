// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelViewportContext.h"

#include "Editor/LevelEditor/Private/LevelEditorInternalTools.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LevelViewportContext)

FLevelEditorViewportClient* ULegacyLevelViewportToolbarContext::GetLevelViewportClient() const
{
	if (TSharedPtr<SLevelViewport> Viewport = LevelViewport.Pin())
	{
		return &Viewport->GetLevelViewportClient();
	}
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Super::GetLevelViewportClient();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
