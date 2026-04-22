// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaDataLinkProcessor.h"
#include "AvaSceneSubsystem.h"
#include "Engine/Level.h"
#include "Engine/World.h"

IAvaSceneInterface* UAvaDataLinkProcessor::GetSceneInterface() const
{
	ULevel* Level = GetTypedOuter<ULevel>();
	if (!Level || !Level->OwningWorld)
	{
		return nullptr;
	}

	const UAvaSceneSubsystem* SceneSubsystem = Level->OwningWorld->GetSubsystem<UAvaSceneSubsystem>();
	if (!SceneSubsystem)
	{
		// No subsystem yet available, try finding the scene interface by iterating the actors
		return UAvaSceneSubsystem::FindSceneInterface(Level);
	}

	return SceneSubsystem->GetSceneInterface(Level);
}
