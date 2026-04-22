// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"

namespace UE::Slate::Private
{
	// Workaround for the web browser issues on some platform
	TAutoConsoleVariable<bool> CVarNoAnimationOnTabForegroundedEvent(
		TEXT("Slate.NoAnimationOnTabForegroundedEvent"),
		false,
		TEXT("Tell the docking system to broadcast the event in a way that doesn't account for the animations")
	);
}