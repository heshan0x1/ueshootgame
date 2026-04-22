// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneStateInstance.h"

namespace UE::SceneState::Private
{
	uint16 GetNextInstanceId()
	{
		static uint16 Id = 0;

		// Starts with 1
		uint16 NewId = ++Id;
		if (NewId == 0)
		{
			NewId = ++Id;
		}
		return NewId; 
	}

} // UE::SceneState::Private

FSceneStateInstance::FSceneStateInstance()
	: InstanceId(UE::SceneState::Private::GetNextInstanceId())
{
}

uint16 FSceneStateInstance::GetInstanceId() const
{
	return InstanceId;
}
