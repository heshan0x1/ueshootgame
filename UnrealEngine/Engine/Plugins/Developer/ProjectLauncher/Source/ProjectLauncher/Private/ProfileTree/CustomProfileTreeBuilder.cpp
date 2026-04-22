// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfileTree/CustomProfileTreeBuilder.h"


#define LOCTEXT_NAMESPACE "CustomProfileTreeBuilder"

namespace ProjectLauncher
{
	FCustomProfileTreeBuilder::FCustomProfileTreeBuilder( const ILauncherProfileRef& InProfile, const TSharedRef<FModel>& InModel )
		: FGenericProfileTreeBuilder( InProfile, InModel->GetDefaultCustomLaunchProfile(), InModel )
	{
	}

	void FCustomProfileTreeBuilder::Construct()
	{
		FGenericProfileTreeBuilder::Construct();

		FLaunchProfileTreeNode& GeneralSettingsHeader = TreeData->AddHeading( TEXT("GeneralSettings"), LOCTEXT("GeneralSettingsHeading", "General Settings") );
		AddProjectProperty(GeneralSettingsHeader);
		AddTargetProperty(GeneralSettingsHeader);
		AddPlatformProperty(GeneralSettingsHeader);
		AddConfigurationProperty(GeneralSettingsHeader);
		AddContentSchemeProperty(GeneralSettingsHeader);

		FLaunchProfileTreeNode& ContentSchemeHeader = TreeData->AddHeading( TEXT("ContentScheme"), LOCTEXT("ContentSchemeHeading", "Content Scheme") );
		AddCompressPakFilesProperty(ContentSchemeHeader);
		AddUseIoStoreProperty(ContentSchemeHeader);
		AddGenerateChunksProperty(ContentSchemeHeader);
		AddZenSnapshotProperty(ContentSchemeHeader);
		AddImportZenSnapshotProperty(ContentSchemeHeader);
		AddZenPakStreamingPathProperty(ContentSchemeHeader);

		FLaunchProfileTreeNode& MapsAndCookingHeader = TreeData->AddHeading( TEXT("Cooking"), LOCTEXT("CookingHeading", "Maps And Cooking") );
		AddCookProperty(MapsAndCookingHeader);
		AddIncrementalCookProperty(MapsAndCookingHeader);
		AddMapsToCookProperty(MapsAndCookingHeader);
		AddAdditionalCookerOptionsProperty(MapsAndCookingHeader);

		FLaunchProfileTreeNode& BuildHeader = TreeData->AddHeading( TEXT("Build"), LOCTEXT("BuildHeading", "Build") );
		AddBuildProperty(BuildHeader);
		AddForceBuildProperty(BuildHeader);
		AddArchitectureProperty(BuildHeader);

		FLaunchProfileTreeNode& DirectoryHeader = TreeData->AddHeading( TEXT("Directory"), LOCTEXT("DirectoryHeading", "Directory") );
		AddArchiveBuildProperty(DirectoryHeader);
		AddArchiveBuildDirectoryProperty(DirectoryHeader);

		FLaunchProfileTreeNode& DeployAndRunHeader = TreeData->AddHeading( TEXT("DeployAndRun"), LOCTEXT("DeployAndRunHeading", "Deploy And Run") );
		AddDeployProperty(DeployAndRunHeader);
		AddTargetDeviceProperty(DeployAndRunHeader);
		AddRunProperty(DeployAndRunHeader);
		AddInitialMapProperty(DeployAndRunHeader);
		AddCommandLineProperty(DeployAndRunHeader);
	}



	TSharedPtr<ILaunchProfileTreeBuilder> FCustomProfileTreeBuilderFactory::TryCreateTreeBuilder( const ILauncherProfileRef& InProfile, const TSharedRef<FModel>& InModel )
	{
		return MakeShared<FCustomProfileTreeBuilder>(InProfile, InModel);
	}


}

#undef LOCTEXT_NAMESPACE
