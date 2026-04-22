// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FIrisModule : public IModuleInterface
{
private:
	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModule("IrisCore", ELoadModuleFlags::None);
	}
};

IMPLEMENT_MODULE(FIrisModule, Iris);
