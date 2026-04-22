// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API


class FEditorInteractiveToolsFrameworkGlobals
{
public:
	// This is the key returned by AddComponentTargetFactory() for the FStaticMeshComponentTargetFactory created/registered
	// in StartupModule() below. Use this key to find/remove that module registration if you need to.
	static UE_API int32 RegisteredStaticMeshTargetFactoryKey;
};

class FEditorInteractiveToolsFrameworkModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

#undef UE_API
