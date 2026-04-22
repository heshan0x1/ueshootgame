// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "ProjectLauncherModule.h"
#include "Globals/GlobalsLaunchExtension.h"
#include "Insights/InsightsLaunchExtension.h"
#include "BootTest/BootTestLaunchExtension.h"

class FCommonLaunchExtensionsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		Globals = MakeShared<FGlobalsLaunchExtension>();
		Insights = MakeShared<FInsightsLaunchExtension>();
		BootTest = MakeShared<FBootTestLaunchExtension>();

		IProjectLauncherModule::Get().RegisterExtension(Globals.ToSharedRef());
		IProjectLauncherModule::Get().RegisterExtension(Insights.ToSharedRef());
		IProjectLauncherModule::Get().RegisterExtension(BootTest.ToSharedRef());
	}

	virtual void ShutdownModule() override
	{
		if (IProjectLauncherModule* ProjectLauncher = IProjectLauncherModule::TryGet())
		{
			ProjectLauncher->UnregisterExtension(Globals.ToSharedRef());
			ProjectLauncher->UnregisterExtension(Insights.ToSharedRef());
			ProjectLauncher->UnregisterExtension(BootTest.ToSharedRef());
		}

		Globals.Reset();
		Insights.Reset();
		BootTest.Reset();
	}

private:
	TSharedPtr<FGlobalsLaunchExtension> Globals;
	TSharedPtr<FInsightsLaunchExtension> Insights;
	TSharedPtr<FBootTestLaunchExtension> BootTest;

};


IMPLEMENT_MODULE(FCommonLaunchExtensionsModule, CommonLaunchExtensions);

