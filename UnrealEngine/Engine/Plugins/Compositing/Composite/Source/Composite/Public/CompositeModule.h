// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Logging/LogMacros.h"
#include "Stats/Stats.h"

COMPOSITE_API DECLARE_LOG_CATEGORY_EXTERN(LogComposite, Log, All);

DECLARE_STATS_GROUP(TEXT("Composite"), STATGROUP_Composite, STATCAT_Advanced);

class FCompositeModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End of IModuleInterface interface

private:
	// Engine all module loaded callback
	void OnAllModuleLoadingPhasesComplete();
	// Engine pre-exit callback
	void OnEnginePreExit();
};

