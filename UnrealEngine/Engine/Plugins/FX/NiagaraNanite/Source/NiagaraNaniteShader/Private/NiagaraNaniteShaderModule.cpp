// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNaniteShaderModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

IMPLEMENT_MODULE(INiagaraNaniteShaderModule, NiagaraNaniteShader);

void INiagaraNaniteShaderModule::StartupModule()
{
	// Maps virtual shader source directory /Plugin/FX/Niagara to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NiagaraNanite"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/FX/NiagaraNanite"), PluginShaderDir);
}

void INiagaraNaniteShaderModule::ShutdownModule()
{
}
