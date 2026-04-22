// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Modules/ModuleInterface.h"

#define UE_API MASSAIBEHAVIOREDITOR_API

class FMassAIBehaviorEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

#undef UE_API
