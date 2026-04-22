// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Extension/LaunchExtension.h"
#include "Templates/UnrealTemplate.h"

enum class EAutomatedPerfTestType : uint8
{
	Sequence,
	Replay,
	ProfileGo,
	StaticCamera,
	Material,
	MAX
};

class FAutomatedPerfTestLaunchExtensionInstance : public ProjectLauncher::FAutomatedTestLaunchExtensionInstance
{
	using Super = ProjectLauncher::FAutomatedTestLaunchExtensionInstance;
public:
	FAutomatedPerfTestLaunchExtensionInstance(FArgs& InArgs) : FAutomatedTestLaunchExtensionInstance(InArgs) {};
	virtual ~FAutomatedPerfTestLaunchExtensionInstance() = default;

	virtual bool HasCustomExtensionMenu() const { return true; }
	virtual void MakeCustomExtensionSubmenu(FMenuBuilder& MenuBuilder) override;

	virtual void OnPropertyChanged() override;
	virtual void OnTestAdded(ILauncherProfileAutomatedTestRef AutomatedTest) override;
	virtual const FString GetTestInternalName() const override;

	virtual void CustomizeTree(ProjectLauncher::FLaunchProfileTreeData& ProfileTreeData) override;
	virtual void CustomizeAutomatedTestCommandLine(FString& InOutCommandLine) override;

private:
	void ExportProfileGoScenarios(const FString& Filename);

	void AddTestNodeOptions(ProjectLauncher::FLaunchProfileTreeNode& TreeNode);
	void AddSequenceTestNodeOptions(ProjectLauncher::FLaunchProfileTreeNode& TreeNode);
	void AddReplayTestNodeOptions(ProjectLauncher::FLaunchProfileTreeNode& TreeNode);
	void AddProfileGoTestNodeOptions(ProjectLauncher::FLaunchProfileTreeNode& TreeNode);
	void AddStaticCameraTestNodeOptions(ProjectLauncher::FLaunchProfileTreeNode& TreeNode);
	void AddMaterialTestNodeOptions(ProjectLauncher::FLaunchProfileTreeNode& TreeNode);

	void OnTestTypeSelectionChanged(EAutomatedPerfTestType Type);
	void SetTestType(EAutomatedPerfTestType Type);
	EAutomatedPerfTestType GetTestType() const;

	template<EAutomatedPerfTestType TestType>
	ProjectLauncher::FLaunchProfileTreeNode::FCallbacks GetTestTypeCallbacks()
	{
		const auto IsTestType = [this]()
		{
			const bool bEnabled = IsTestActive() || GetConfigBool(EConfig::PerProfile, TEXT("Enabled"));
			return GetTestType() == TestType && bEnabled;
		};

		ProjectLauncher::FLaunchProfileTreeNode::FCallbacks Callbacks
		{
			.IsVisible = IsTestType,
			.IsEnabled = IsTestType
		};

		return MoveTemp(Callbacks);
	}

	EAutomatedPerfTestType CurrentTestType = EAutomatedPerfTestType::Sequence;
	bool bEnableCSVProfiler = false;
};


class FAutomatedPerfTestLaunchExtension : public ProjectLauncher::FLaunchExtension
{
public:
	virtual TSharedPtr<ProjectLauncher::FLaunchExtensionInstance> CreateInstanceForProfile(ProjectLauncher::FLaunchExtensionInstance::FArgs& InArgs) override;
	virtual const TCHAR* GetInternalName() const override;
	virtual FText GetDisplayName() const override;
};

#endif // #if WITH_EDITOR