// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteAssemblyEditorUtilsModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "NaniteAssemblyEditorUtils"

DEFINE_LOG_CATEGORY(LogNaniteAssemblyBuilder);

void FNaniteAssemblyEditorUtilsModule::StartupModule()
{
}
	
void FNaniteAssemblyEditorUtilsModule::ShutdownModule()
{
}

FNaniteAssemblyEditorUtilsModule& FNaniteAssemblyEditorUtilsModule::GetModule()
{
	static const FName ModuleName = "NaniteAssemblyEditorUtils";
	return FModuleManager::LoadModuleChecked<FNaniteAssemblyEditorUtilsModule>(ModuleName);
}

IMPLEMENT_MODULE(FNaniteAssemblyEditorUtilsModule, NaniteAssemblyEditorUtils);

#undef LOCTEXT_NAMESPACE
