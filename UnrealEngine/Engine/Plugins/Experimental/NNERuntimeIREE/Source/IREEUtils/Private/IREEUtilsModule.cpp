// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "IREEUtilsLog.h"

DEFINE_LOG_CATEGORY(LogIREEUtils);

class FIREEUtilsModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }
};

IMPLEMENT_MODULE(FIREEUtilsModule, IREEUtils)