// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UClass;
class UEngineCustomTimeStep;

namespace UE::TakeRecorder
{
/** Shows a notification that the user has not set up any timecode provider. */
void ShowNoTimecodeProviderNotification();

/** Shows a notification that the configured time step class could not be resolved. */
void ShowNoTimeStepClassNotification();
}
