// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Event.h"
#include "Misc/ScopeRWLock.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

FCaptureEvent::FCaptureEvent(FString InName) : Name(MoveTemp(InName))
{
}

const FString& FCaptureEvent::GetName() const
{
	return Name;
}

FCaptureEvent::~FCaptureEvent() = default;

PRAGMA_ENABLE_DEPRECATION_WARNINGS
