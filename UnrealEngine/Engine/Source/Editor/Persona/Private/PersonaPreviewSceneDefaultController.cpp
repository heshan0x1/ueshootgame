// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaPreviewSceneDefaultController.h"
#include "AnimationEditorPreviewScene.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PersonaPreviewSceneDefaultController)

void UPersonaPreviewSceneDefaultController::InitializeView(UPersonaPreviewSceneDescription* SceneDescription, IPersonaPreviewScene* PreviewScene) const
{
	PreviewScene->ShowDefaultMode();
}

void UPersonaPreviewSceneDefaultController::UninitializeView(UPersonaPreviewSceneDescription* SceneDescription, IPersonaPreviewScene* PreviewScene) const
{

}
