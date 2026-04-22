// Copyright Epic Games, Inc. All Rights Reserved.

#include "ElectraSamplesModule.h"

#include "CoreTypes.h"
#include "Modules/ModuleManager.h"
#include "IElectraSamplesModule.h"

#define LOCTEXT_NAMESPACE "ElectraBaseModule"

DEFINE_LOG_CATEGORY(LogElectraSamples);

// -----------------------------------------------------------------------------------------------------------------------------------

class FElectraSamplesModule: public IElectraSamplesModule
{
public:
	// IModuleInterface interface
	void StartupModule() override
	{
	}

	void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FElectraSamplesModule, ElectraSamples);

#undef LOCTEXT_NAMESPACE
