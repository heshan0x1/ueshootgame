// Copyright Epic Games, Inc. All Rights Reserved.

#include "BootTest/BootTestLaunchExtension.h"


#define LOCTEXT_NAMESPACE "FBootTestLaunchExtensionInstance"

const TCHAR* FBootTestLaunchExtensionInstance::BootTestInternalName = TEXT("BootTestExtension.BootTest");


void FBootTestLaunchExtensionInstance::CustomizeTree( ProjectLauncher::FLaunchProfileTreeData& ProfileTreeData )
{
	// note: Windowed option is here mostly as an example of adding UI items for a particular profile - your tests may not need it

	auto IsVisible = [this]() // this always outlives the ProfileTreeData
	{
		return IsTestActive();
	};

	AddDefaultHeading(ProfileTreeData)
		.AddBoolean(LOCTEXT("WindowedLabel","Windowed"),
		{
			.GetValue = [this]()          { return GetConfigBool(EConfig::PerProfile, TEXT("Windowed"));},
			.SetValue = [this](bool bVal) { SetConfigBool(EConfig::PerProfile, TEXT("Windowed"), bVal ); },
			.IsVisible = IsVisible,
		}
	);
}

void FBootTestLaunchExtensionInstance::CustomizeAutomatedTestCommandLine( FString& InOutCommandLine )
{
	bool bWindowed = GetConfigBool(EConfig::PerProfile, TEXT("Windowed"));
	if (!bWindowed)
	{
		InOutCommandLine += TEXT(" -windowmode=Fullscreen");
	}
}

const FString FBootTestLaunchExtensionInstance::GetTestInternalName() const
{
	return BootTestInternalName;
}

void FBootTestLaunchExtensionInstance::OnTestAdded( ILauncherProfileAutomatedTestRef AutomatedTest )
{
	AutomatedTest->SetTests(TEXT("UE.BootTest"));
	AutomatedTest->SetPriority(1000); // Boot Test is likely the most lightweight test there is, so we want to run it first - give it a high priority
}




TSharedPtr<ProjectLauncher::FLaunchExtensionInstance> FBootTestLaunchExtension::CreateInstanceForProfile( ProjectLauncher::FLaunchExtensionInstance::FArgs& InArgs )
{
	return MakeShared<FBootTestLaunchExtensionInstance>(InArgs);
}

const TCHAR* FBootTestLaunchExtension::GetInternalName() const
{
	return TEXT("BootTest");
}

FText FBootTestLaunchExtension::GetDisplayName() const
{
	return LOCTEXT("ExtensionName", "Automated Boot Test");
}


#undef LOCTEXT_NAMESPACE
