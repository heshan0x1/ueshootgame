// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningTraining.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void FLearningTrainingModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	RegisterPythonPackages();
}

void FLearningTrainingModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

void FLearningTrainingModule::RegisterPythonPackages()
{
	const FString ExperimentalPluginsPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Experimental"));
	const FString SitePackagesPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("PipInstall") / TEXT("Lib") / TEXT("site-packages"));
	const FString PyPluginsSitePackageFile = FPaths::ConvertRelativePathToFull(SitePackagesPath / TEXT("learning_agents.pth"));
	
	TArray<FString> PluginSitePackagePaths;
	PluginSitePackagePaths.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(ExperimentalPluginsPath, TEXT("LearningAgents/Content/Python"))));
	PluginSitePackagePaths.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(ExperimentalPluginsPath, TEXT("NNERuntimeBasicCpu/Content/Python"))));
	
	FFileHelper::SaveStringArrayToFile(PluginSitePackagePaths, *PyPluginsSitePackageFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

IMPLEMENT_MODULE(FLearningTrainingModule, LearningTraining)
