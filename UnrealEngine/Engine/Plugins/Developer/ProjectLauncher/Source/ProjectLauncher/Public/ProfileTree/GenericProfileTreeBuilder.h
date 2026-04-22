// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfileTree/ILaunchProfileTreeBuilder.h"
#include "Model/ProjectLauncherModel.h"

#define UE_API PROJECTLAUNCHER_API

class SSearchableComboBox;
class SCustomLaunchMapListView;
class SCustomLaunchDeviceListView;
namespace ESelectInfo { enum Type : int; }

namespace ProjectLauncher
{
	/**
	 * Base class for a profile tree builder that creates FLaunchProfileTreeData from a given ILauncherProfile.
	 * 
	 * Expected to be created by an instance of ILaunchProfileTreeBuilderFactory, for example:
	 * 
	 *    TSharedPtr<ILaunchProfileTreeBuilder> FMyProfileTreeBuilderFactory::TryCreateTreeBuilder( const ILauncherProfileRef& InProfile, const TSharedRef<FModel>& InModel )
	 *    {
	 *        return MakeShared<FMyProfileTreeBuilder>(InProfile, InModel);
	 *    }
	 */
	class FGenericProfileTreeBuilder : public ILaunchProfileTreeBuilder, public TSharedFromThis<FGenericProfileTreeBuilder>
	{
	public:
		UE_API FGenericProfileTreeBuilder( const ILauncherProfileRef& Profile, const ILauncherProfileRef& InDefaultProfile, const TSharedRef<FModel>& InModel );
		virtual ~FGenericProfileTreeBuilder();

		UE_API virtual void Construct() override;

		virtual FLaunchProfileTreeDataRef GetProfileTree() override
		{
			return TreeData;
		}

		virtual bool AllowExtensionsUI() const override
		{
			return true;
		}

	protected:
		// default property creation functions
		UE_API void AddProjectProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddTargetProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddPlatformProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddConfigurationProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddContentSchemeProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddCompressPakFilesProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddUseIoStoreProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddGenerateChunksProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddZenSnapshotProperty(FLaunchProfileTreeNode& HeadingNode);
		UE_API void AddImportZenSnapshotProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddZenPakStreamingPathProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddIncrementalCookProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddCookProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddMapsToCookProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddAdditionalCookerOptionsProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddBuildProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddForceBuildProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddBuildUATProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddArchitectureProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddStagingDirectoryProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddArchiveBuildProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddArchiveBuildDirectoryProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddDeployProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddIncrementalDeployProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddTargetDeviceProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddRunProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddInitialMapProperty( FLaunchProfileTreeNode& HeadingNode );
		UE_API void AddCommandLineProperty( FLaunchProfileTreeNode& HeadingNode );

		// helper callbacks to simplify control enable/visibility
		FLaunchProfileTreeNode::FGetBool ForPak;
		FLaunchProfileTreeNode::FGetBool ForZen;
		FLaunchProfileTreeNode::FGetBool ForZenWS;
		FLaunchProfileTreeNode::FGetBool ForCooked;
		FLaunchProfileTreeNode::FGetBool ForEnabledCooked;
		FLaunchProfileTreeNode::FGetBool ForContent;
		FLaunchProfileTreeNode::FGetBool ForCode;
		FLaunchProfileTreeNode::FGetBool ForDeployment;
		FLaunchProfileTreeNode::FGetBool ForRun;
		FLaunchProfileTreeNode::FGetString EmptyString;



		TArray<FString> CachedMapsToCook;
		enum class EMapOption : uint8
		{
			Startup,
			Selected,
		};
		EMapOption MapOption = EMapOption::Startup;

		TArray<FString> CachedDeployDeviceIDs;
		enum class EDeployDeviceOption : uint8
		{
			Default,
			Selected,
		};
		EDeployDeviceOption DeployDeviceOption = EDeployDeviceOption::Default;


		UE_API FString GetProjectPath() const;
		UE_API void SetProjectName(FString ProjectPath);
		UE_API bool HasProject() const;

		UE_API FString GetBuildTarget() const;
		UE_API void SetBuildTarget(FString BuildTarget);
		UE_API TArray<EBuildTargetType> GetSupportedBuildTargetTypes() const;

		UE_API void SetBuildConfiguration(EBuildConfiguration BuildConfiguration);
		UE_API EBuildConfiguration GetBuildConfiguration() const;

		UE_API void SetContentScheme(EContentScheme ContentScheme);
		UE_API bool IsContentSchemeAvailable(EContentScheme, FText& OutReason) const;

		UE_API FString GetCommandLine() const;
		UE_API void SetCommandLine( const FString& NewCommandLine );

		UE_API void SetSelectedPlatforms( TArray<FString> SelectedPlatforms );
		UE_API TArray<FString> GetSelectedPlatforms() const;

		UE_API void SetCook( bool bCook );
		UE_API bool GetCook( ILauncherProfilePtr InProfile = nullptr ) const;

		UE_API void SetIncrementalCookMode( ELauncherProfileIncrementalCookMode::Type Mode );
		UE_API ELauncherProfileIncrementalCookMode::Type GetIncrementalCookMode() const;

		UE_API void SetMapsToCook(TArray<FString> MapsToCook);
		UE_API TArray<FString> GetMapsToCook() const;
		UE_API EMapOption GetMapOption() const;
		UE_API void SetMapOption( EMapOption MapOption );
		UE_API float GetMapListHeight() const;
		UE_API void SetMapListHeight( float NewHeight );
		UE_API TSharedRef<SWidget> CreateMapListWidget();
		float MapListHeight = 300.0f;


		UE_API void SetDeployDeviceIDs(TArray<FString> DeployDeviceIDs);
		UE_API TArray<FString> GetDeployDeviceIDs() const;
		UE_API EDeployDeviceOption GetDeployDeviceOption() const;
		UE_API void SetDeployDeviceOption( EDeployDeviceOption DeployDeviceOption );
		UE_API float GetDeployDeviceListHeight() const;
		UE_API void SetDeployDeviceListHeight( float NewHeight );
		UE_API void OnDeviceRemoved(FString DeviceID);
		UE_API TSharedRef<SWidget> CreateDeployDeviceWidget();
		float DeployDeviceListHeight = 150.0f;

		UE_API void SetBuild( bool bBuild );
		UE_API bool GetBuild( ILauncherProfilePtr InProfile = nullptr ) const;

		UE_API void SetForceBuild( bool bForceBuild );
		UE_API bool GetForceBuild( ILauncherProfilePtr InProfile = nullptr ) const;

		UE_API void SetArchitecture( FString Architecture );
		UE_API FString GetArchitecture() const;
		UE_API FText GetArchitectureDisplayName( FString Architecture );

		UE_API void SetDeployToDevice( bool bDeployToDevice );
		UE_API bool GetDeployToDevice( ILauncherProfilePtr InProfile = nullptr ) const;

		UE_API void SetIsRunning( bool bRun );
		UE_API bool GetIsRunning( ILauncherProfilePtr InProfile = nullptr ) const;

		UE_API void OnInitialMapChanged( TSharedPtr<FString> InitialMap, ESelectInfo::Type );
		UE_API TSharedPtr<FString> GetInitialMap() const;



	protected:

		UE_API virtual void OnPropertyChanged() override;
		UE_API virtual void OnValidateProfile();
		UE_API void RefreshContentScheme();
		UE_API void CacheStartupMapList() const;
		UE_API void CacheArchitectures();
		UE_API TSharedRef<SWidget> OnGenerateComboWidget( TSharedPtr<FString> InComboString );
		FText GetZenSnapshotText() const;

		FLaunchProfileTreeDataRef TreeData;
		const ILauncherProfileRef Profile;
		const ILauncherProfileRef DefaultProfile;
		EProfileType ProfileType;

		const TSharedRef<FModel> Model;

		EContentScheme ContentScheme;
		bool bShouldCook;
		mutable bool bStartupMapCacheDirty = true;
		mutable TArray<TSharedPtr<FString>> CachedStartupMaps;
		TSharedPtr<SSearchableComboBox> InitalMapCombo;
		TSharedPtr<SCustomLaunchMapListView> MapListView;
		TSharedPtr<SCustomLaunchDeviceListView> DeployDeviceListView;
		TArray<FString> CachedArchitectures;
		EBuildTargetType CachedBuildTargetType = EBuildTargetType::Game;
	};
}

#undef UE_API
