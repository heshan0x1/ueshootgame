// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FBuildStorageToolStyle
{
public:
	/**
	 * Set up specific styles for the storage server UI app
	 */
	static void Initialize();

	/**
	 * Tidy up on shut-down
	 */
	static void Shutdown();

	/*
	 * Access to singleton style object
	 */ 
	static const ISlateStyle& Get();

private:
	static TSharedRef<FSlateStyleSet> Create();

	/** Singleton style object */
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
