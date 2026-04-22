// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/PropertyBag.h"
#include "SceneStateConduitInstance.generated.h"

/** Instance data of a conduit */
USTRUCT()
struct FSceneStateConduitInstance
{
	GENERATED_BODY()

	/**
	 * Flag to determine if the conduit instance has already been initialized.
	 * Used as a way to avoid re-setting up all the exit transitions of a conduit.
	 */
	UPROPERTY()
	bool bInitialized = false;
};
