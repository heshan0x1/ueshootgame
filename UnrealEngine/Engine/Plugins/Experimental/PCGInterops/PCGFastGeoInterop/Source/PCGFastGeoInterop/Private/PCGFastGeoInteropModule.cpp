// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGFastGeoInteropModule.h"

#include "Compute/PrimitiveFactories/PCGPrimitiveFactoryFastGeoPISMC.h"

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FPCGFastGeoInteropModule, PCGFastGeoInterop);

void FPCGFastGeoInteropModule::StartupModule()
{
	PCGPrimitiveFactoryHelpers::Private::SetupFastGeoPrimitiveFactory([]()
	{
		return MakeShared<FPCGPrimitiveFactoryFastGeoPISMC>();
	});
}

void FPCGFastGeoInteropModule::ShutdownModule()
{
	PCGPrimitiveFactoryHelpers::Private::ResetFastGeoPrimitiveFactory();
}
