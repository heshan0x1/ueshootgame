// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Stats/Stats.h"

/** Module for experimental plugin that provides access to FastGeo components. To be removed when FastGeo exits experimental. */
class FPCGFastGeoInteropModule final : public IModuleInterface
{
public:
	//~ IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override { return true; }
	//~ End IModuleInterface implementation
};
