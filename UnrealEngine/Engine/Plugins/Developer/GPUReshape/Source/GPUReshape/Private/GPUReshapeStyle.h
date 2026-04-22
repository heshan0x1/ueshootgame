// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Styling/ISlateStyle.h"

class FGPUReshapeStyle
{
public:
	/** Setup */
	static void Initialize();
	static void Shutdown();

	/** Get the shared style */
	static TSharedPtr<ISlateStyle> Get();
};

#endif // WITH_EDITOR
