// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Extension/LaunchExtension.h"



class FBootTestLaunchExtensionInstance : public ProjectLauncher::FAutomatedTestLaunchExtensionInstance
{
public:
	FBootTestLaunchExtensionInstance( FArgs& InArgs ) : ProjectLauncher::FAutomatedTestLaunchExtensionInstance(InArgs) {};
	virtual ~FBootTestLaunchExtensionInstance() = default;

	virtual void OnTestAdded( ILauncherProfileAutomatedTestRef AutomatedTest ) override;
	virtual const FString GetTestInternalName() const override;

	virtual void CustomizeTree( ProjectLauncher::FLaunchProfileTreeData& ProfileTreeData ) override;
	virtual void CustomizeAutomatedTestCommandLine( FString& InOutCommandLine ) override;

private:
	static const TCHAR* BootTestInternalName;

};


class FBootTestLaunchExtension : public ProjectLauncher::FLaunchExtension
{
public:
	virtual TSharedPtr<ProjectLauncher::FLaunchExtensionInstance> CreateInstanceForProfile( ProjectLauncher::FLaunchExtensionInstance::FArgs& InArgs ) override;
	virtual const TCHAR* GetInternalName() const override;
	virtual FText GetDisplayName() const override;
};