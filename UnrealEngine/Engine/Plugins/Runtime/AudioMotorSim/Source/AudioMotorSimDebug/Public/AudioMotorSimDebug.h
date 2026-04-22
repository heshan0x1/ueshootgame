// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FAudioMotorModelSlateIMDebugger;

class FAudioMotorSimDebugModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
	TUniquePtr<FAudioMotorModelSlateIMDebugger> SlateIMDebugger;
};
