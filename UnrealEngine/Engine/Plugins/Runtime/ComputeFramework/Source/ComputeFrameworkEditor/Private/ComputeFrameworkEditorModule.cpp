// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFrameworkEditorModule.h"

#include "AssetToolsModule.h"
#include "ComputeFramework/ComputeFrameworkCompilationTick.h"
#include "ComputeFramework/ComputeGraphFromTextAssetActions.h"
#include "Modules/ModuleManager.h"

void FComputeFrameworkEditorModule::StartupModule()
{
	TickObject = MakeUnique<FComputeFrameworkCompilationTick>();

 	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
 	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_ComputeGraphFromText));
}

void FComputeFrameworkEditorModule::ShutdownModule()
{
	TickObject = nullptr;
}

IMPLEMENT_MODULE(FComputeFrameworkEditorModule, ComputeFrameworkEditor)
