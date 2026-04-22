// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneStateEnums.h"
#include "UObject/UnrealType.h"

class UObject;

namespace UE::SceneState
{
#if WITH_EDITOR
/** Describes an Edit change for a task or task instance */
struct FTaskEditChange : FPropertyChangedEvent
{
	FTaskEditChange()
		: FPropertyChangedEvent(nullptr)
	{
	}
	/** Owning object of the task */
	UObject* Outer = nullptr;
	/** The object that was changed: whether it was the task or the task instance */
	ETaskObjectType ChangedObject = ETaskObjectType::None;
};
#endif

} // UE::SceneState
