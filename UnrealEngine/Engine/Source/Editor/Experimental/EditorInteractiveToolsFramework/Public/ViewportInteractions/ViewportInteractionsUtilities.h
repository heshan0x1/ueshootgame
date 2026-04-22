// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class UViewportInteractionsBehaviorSource;

namespace UE::Editor::ViewportInteractions
{
/**
 * Convenience function to add "standard" movement interactions to the specified Viewport Interactions Behavior Source
 * e.g. Movement, Rotation, View Angle, Orbit, Panning, FOV, etc
 */
EDITORINTERACTIVETOOLSFRAMEWORK_API void AddDefaultCameraMovementInteractions(UViewportInteractionsBehaviorSource* InInteractionsBehaviorSource);
}
