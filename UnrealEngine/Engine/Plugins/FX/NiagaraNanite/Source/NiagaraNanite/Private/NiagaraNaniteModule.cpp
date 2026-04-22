// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNaniteModule.h"
#include "Renderer/NiagaraNaniteRendererProperties.h"

#define LOCTEXT_NAMESPACE "FNiagaraNaniteModule"

void FNiagaraNaniteModule::StartupModule()
{
	UNiagaraNaniteRendererProperties::InitCDOPropertiesAfterModuleStartup();
}

void FNiagaraNaniteModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNiagaraNaniteModule, NiagaraNanite)
