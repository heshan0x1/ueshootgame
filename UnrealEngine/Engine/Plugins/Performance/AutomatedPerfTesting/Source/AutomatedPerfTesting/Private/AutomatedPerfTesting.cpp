// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTesting.h"

#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "LaunchExtension/AutomatedPerfTestLaunchExtension.h"

#if WITH_EDITOR
#include "ProjectLauncherModule.h"
#endif

#define LOCTEXT_NAMESPACE "FAutomatedPerfTestingModule"

void FAutomatedPerfTestingModule::StartupModule()
{
#if WITH_EDITOR
	LaunchExtension = MakeShared<FAutomatedPerfTestLaunchExtension>();
	IProjectLauncherModule::Get().RegisterExtension(LaunchExtension.ToSharedRef());
#endif
}

void FAutomatedPerfTestingModule::ShutdownModule()
{
#if WITH_EDITOR
	if (IProjectLauncherModule* ProjectLauncher = IProjectLauncherModule::TryGet())
	{
		ProjectLauncher->UnregisterExtension(LaunchExtension.ToSharedRef());
	}

	LaunchExtension.Reset();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAutomatedPerfTestingModule, AutomatedPerfTesting)