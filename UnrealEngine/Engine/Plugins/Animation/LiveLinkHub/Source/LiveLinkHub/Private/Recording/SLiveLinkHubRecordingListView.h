// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Delegates/DelegateCombinations.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "Implementations/LiveLinkUAssetRecording.h"
#include "LiveLinkHubModule.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Recording/LiveLinkHubPlaybackController.h"
#include "Recording/LiveLinkRecording.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "LiveLinkHub.RecordingListView"

class SLiveLinkHubRecordingListView : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnImportRecording, const struct FAssetData&);
	
	SLATE_BEGIN_ARGS(SLiveLinkHubRecordingListView)
		{}
		SLATE_EVENT(FOnImportRecording, OnImportRecording)
	SLATE_END_ARGS()

	SLiveLinkHubRecordingListView()
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		OnAssetAddedHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &SLiveLinkHubRecordingListView::OnAssetAdded);
		OnAssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &SLiveLinkHubRecordingListView::OnAssetRemoved);
	}

	virtual ~SLiveLinkHubRecordingListView() override
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			if (OnAssetAddedHandle.IsValid())
			{
				AssetRegistry.OnAssetAdded().Remove(OnAssetAddedHandle);
			}
			if (OnAssetRemovedHandle.IsValid())
			{
				AssetRegistry.OnAssetRemoved().Remove(OnAssetRemovedHandle);
			}
		}
	}
	
	//~ Begin SWidget interface
	void Construct(const FArguments& InArgs)
	{
		OnImportRecordingDelegate = InArgs._OnImportRecording;
		
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			[
				SAssignNew(BoxWidget, SBox)
				.Visibility(this, &SLiveLinkHubRecordingListView::GetRecordingPickerVisibility)
				[
					CreateRecordingPicker()
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]()
				{
					EVisibility RecordingPickerVisibility = GetRecordingPickerVisibility();
					return RecordingPickerVisibility == EVisibility::Visible ? EVisibility::Collapsed : EVisibility::Visible;
				})
				.Text(GetNoAssetsWarningText())
			]
		];
	}
	//~ End SWidget interface

private:
	/** Callback to notice the hub that we've selected a recording to play. */
	void OnImportRecording(const FAssetData& AssetData) const
	{
		OnImportRecordingDelegate.Execute(AssetData);
	}

	/** When an asset is added to the asset registry. */
	void OnAssetAdded(const FAssetData& InAssetData)
	{
		if (InAssetData.IsValid()
			&& (InAssetData.AssetClassPath == ULiveLinkUAssetRecording::StaticClass()->GetClassPathName()
				|| InAssetData.AssetClassPath == ULiveLinkRecording::StaticClass()->GetClassPathName()))
		{
			bAssetsAvailableCached = true;
		}
	}

	/** When an asset is removed from the asset registry. */
	void OnAssetRemoved(const FAssetData& InAssetData)
	{
		if (InAssetData.IsValid()
			&& (InAssetData.AssetClassPath == ULiveLinkUAssetRecording::StaticClass()->GetClassPathName()
				|| InAssetData.AssetClassPath == ULiveLinkRecording::StaticClass()->GetClassPathName()))
		{
			// Let the cache recalculate.
			bAssetsAvailableCached.Reset();
		}
	}

	/** The visibility status of the recording picker. */
	EVisibility GetRecordingPickerVisibility() const
	{
		// Cache the value initially, otherwise it is set on the asset added event.
		if (!bAssetsAvailableCached.IsSet())
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AssetDataArray;
			FARFilter Filter = MakeAssetFilter();
			AssetRegistryModule.Get().GetAssets(MoveTemp(Filter), AssetDataArray);

			bAssetsAvailableCached = AssetDataArray.Num() > 0;
		}
		
		return bAssetsAvailableCached.GetValue() ? EVisibility::Visible : EVisibility::Hidden;
	}

	TArray<FAssetViewCustomColumn> GetCustomColumns() const
	{
		TArray<FAssetViewCustomColumn> ReturnValue;
		auto ColumnStringReturningLambda = [](const FAssetData& AssetData, const FName& ColumnName) -> FString
		{
			int32 AssetVersion;
			const bool bHasMetaTag = AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(ULiveLinkRecording, SavedRecordingVersion), AssetVersion);
			if (!bHasMetaTag || AssetVersion < UE::LiveLinkHub::Private::RecordingVersions::Latest)
			{
				return TEXT("Outdated");
			}
			
			return TEXT("Latest");
		};
	
		FAssetViewCustomColumn Column;
		Column.ColumnName = GET_MEMBER_NAME_CHECKED(ULiveLinkRecording, SavedRecordingVersion);
		Column.DataType = UObject::FAssetRegistryTag::TT_Alphabetical;
		Column.DisplayName = LOCTEXT("VersionName", "Version");
		Column.OnGetColumnData = FOnGetCustomAssetColumnData::CreateLambda(ColumnStringReturningLambda);
		Column.OnGetColumnDisplayText =
			FOnGetCustomAssetColumnDisplayText::CreateLambda(
				[ColumnStringReturningLambda](const FAssetData& AssetData, const FName& ColumnName)
			{
				return FText::FromString(ColumnStringReturningLambda(AssetData, ColumnName));
			});
		
		ReturnValue.Add(Column);
		return ReturnValue;
	}
	
	/** Creates the asset picker widget for selecting a recording. */
	TSharedRef<SWidget> CreateRecordingPicker(TOptional<FAssetData> AssetData = {})
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
		
		FAssetPickerConfig AssetPickerConfig;
		{
			AssetPickerConfig.CustomColumns = GetCustomColumns();
			AssetPickerConfig.SelectionMode = ESelectionMode::Single;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
			AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
			AssetPickerConfig.bAllowNullSelection = false;
			AssetPickerConfig.bShowBottomToolbar = true;
			AssetPickerConfig.bAutohideSearchBar = false;
			AssetPickerConfig.bAllowDragging = false;
			AssetPickerConfig.bCanShowClasses = false;
			AssetPickerConfig.bShowPathInColumnView = true;
			AssetPickerConfig.bSortByPathInColumnView = false;
			AssetPickerConfig.InitialThumbnailSize = EThumbnailSize::Tiny;
			AssetPickerConfig.AssetShowWarningText = GetNoAssetsWarningText();

			AssetPickerConfig.bForceShowEngineContent = true;
			AssetPickerConfig.bForceShowPluginContent = true;

			AssetPickerConfig.Filter = MakeAssetFilter();
			AssetPickerConfig.OnAssetDoubleClicked = FOnAssetSelected::CreateRaw(this, &SLiveLinkHubRecordingListView::OnImportRecording);
			AssetPickerConfig.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateRaw(this, &SLiveLinkHubRecordingListView::GetAssetContextMenu);

			if (AssetData.IsSet())
			{
				AssetPickerConfig.InitialAssetSelection = *AssetData;
			}
		}

		{
			AssetPicker = ContentBrowser.CreateAssetPicker(AssetPickerConfig);
			TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				AssetPicker.ToSharedRef()
			];

			MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
		}

		return MenuBuilder.MakeWidget();
	}

	/** Create a filter for available recording assets. */
	FARFilter MakeAssetFilter() const
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(ULiveLinkRecording::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;
		// There shouldn't be recordings that exist in memory but not on disk. Necessary to properly register deleted assets.
		Filter.bIncludeOnlyOnDiskAssets = true;
		return Filter;
	}

	TSharedPtr<SWidget> GetAssetContextMenu(const TArray<FAssetData>& SelectedAssets)
	{
		if (SelectedAssets.Num() <= 0)
		{
			return nullptr;
		}

		const FAssetData& SelectedAsset = SelectedAssets[0];
		
		TWeakObjectPtr<UObject> SelectedAssetObject = SelectedAssets[0].GetAsset();
		if (!SelectedAssetObject.IsValid())
		{
			return nullptr;
		}

		FMenuBuilder MenuBuilder(true, MakeShared<FUICommandList>());

		MenuBuilder.BeginSection(TEXT("Recording"), LOCTEXT("RecordingSectionLabel", "Recording"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("RenameRecordingLabel", "Rename"),
				LOCTEXT("RenameRecordingTooltip", "Rename the recording"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Rename"),
				FUIAction(
					FExecuteAction::CreateLambda([SelectedAssetObject, this] ()
					{
						if (SelectedAssetObject.IsValid())
						{
							const FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
							ContentBrowserModule.Get().ExecuteRename(AssetPicker);
						}
					}),
					FCanExecuteAction::CreateLambda([] () { return true; })
				)
			);
			
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DuplicateRecordingLabel", "Duplicate"),
				LOCTEXT("DuplicateRecordingTooltip", "Duplicate the recording"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Duplicate"),
				FUIAction(
					FExecuteAction::CreateLambda([this, SelectedAssetObject] ()
					{
						if (SelectedAssetObject.IsValid())
						{
							IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
							
							FString TargetName;
							FString TargetPackageName;
							IAssetTools::Get().CreateUniqueAssetName(SelectedAssetObject->GetOutermost()->GetName(), TEXT("_Copy"), TargetPackageName, TargetName);

							// Duplicate the asset.
							UObject* NewAsset = AssetTools.DuplicateAsset(TargetName, FPackageName::GetLongPackagePath(TargetPackageName), SelectedAssetObject.Get());
							FSavePackageArgs SavePackageArgs;
							SavePackageArgs.TopLevelFlags = RF_Public | RF_Standalone;
							SavePackageArgs.Error = GLog;

							// Save the package.
							const FString PackageFileName = FPackageName::LongPackageNameToFilename(TargetPackageName, FPackageName::GetAssetPackageExtension());
							UPackage::SavePackage(NewAsset->GetPackage(), NewAsset, *PackageFileName, MoveTemp(SavePackageArgs));

							// Unload the source recording data, as the bulk data would have been fully loaded to duplicate.
							const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
							const TStrongObjectPtr<ULiveLinkRecording> PlaybackRecording = LiveLinkHubModule.GetPlaybackController()->GetRecording();
							if (PlaybackRecording.Get() != SelectedAssetObject)
							{
								CastChecked<ULiveLinkUAssetRecording>(SelectedAssetObject)->UnloadRecordingData();
							}

							// There is no inherent way to update the selection of the asset picker, so instead we'll recreate one that is already selecting the new asset.
							BoxWidget->SetContent(CreateRecordingPicker(FAssetData{ NewAsset }));

							// It may take a few frames for the selection to fully update in the new picker, so give it ample time to do so before triggering the rename.
							GEditor->GetTimerManager()->SetTimer(TimerHandle, [this]() 
							{
								if (TimerHandle.IsValid())
								{
									if (const FContentBrowserModule* ContentBrowserModule = FModuleManager::Get().GetModulePtr<FContentBrowserModule>("ContentBrowser"))
									{
										ContentBrowserModule->Get().ExecuteRename(AssetPicker);
									}
								}
							}, 0.3, false);

							
						}
					}),
					FCanExecuteAction::CreateLambda([] () { return true; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenFileLocationLabel", "Open File Location..."),
				LOCTEXT("OpenFileLocationTooltip", "Open the folder containing this file"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
				FUIAction(
					FExecuteAction::CreateLambda([SelectedAssetObject] ()
					{
						if (SelectedAssetObject.IsValid())
						{
							const FString PackageName = SelectedAssetObject->GetPathName();
							const FString AssetFilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
							const FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(AssetFilePath);
							const FString AssetDirectory = FPaths::GetPath(AbsoluteFilePath);
						
							FPlatformProcess::ExploreFolder(*AssetDirectory);
						}
					}),
					FCanExecuteAction::CreateLambda([SelectedAssetObject] () { return SelectedAssetObject.IsValid(); })
				)
			);
		}

		int32 SavedRecordingVersion = 0;
		const bool bHasRecordingValue = SelectedAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(ULiveLinkRecording, SavedRecordingVersion), SavedRecordingVersion);
		if (!bHasRecordingValue || SavedRecordingVersion < UE::LiveLinkHub::Private::RecordingVersions::Latest)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UpgradeRecording", "Upgrade Recording"),
				LOCTEXT("UpgradeRecordingTooltip", "Loads the entire recording into memory, converts it to the latest version, and saves to a new file."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"),
				FUIAction(
					FExecuteAction::CreateLambda([SelectedAssetObject] ()
					{
						if (SelectedAssetObject.IsValid())
						{
							const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
							LiveLinkHubModule.GetLiveLinkHub()->UpgradeAndSaveRecording(CastChecked<ULiveLinkUAssetRecording>(SelectedAssetObject.Get()));
						}
					}),
					FCanExecuteAction::CreateLambda([SelectedAssetObject] () { return SelectedAssetObject.IsValid(); })
				)
			);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection(TEXT("RecordingList"), LOCTEXT("RecordingListSectionLabel", "Recording List"));
		{
			MenuBuilder.AddMenuEntry(
			LOCTEXT("RefreshRecordings", "Refresh Recordings"),
			LOCTEXT("RefreshRecordingsTooltip", "Rescan the directory list."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
			FUIAction(
				FExecuteAction::CreateLambda([] ()
				{
					const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					AssetRegistryModule.Get().ScanPathsSynchronous({ TEXT("/Game") }, true);
				}))
			);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}
	
	/** The text to display when no assets are found. */
	static FText GetNoAssetsWarningText()
	{
		return LOCTEXT("NoRecordings_Warning", "No Recordings Found");
	}
	
private:
	/** Delegate used for noticing the hub that a recording was selected for playback. */
	FOnImportRecording OnImportRecordingDelegate;
	/** The asset picker used for selecting recordings. */
	TSharedPtr<SWidget> AssetPicker;
	/** Handle for when an asset is added to the asset registry. */
	FDelegateHandle OnAssetAddedHandle;
	/** Handle for when an asset is removed from the asset registry. */
	FDelegateHandle OnAssetRemovedHandle;
	/** Box widget used to hold the asset picker. */
	TSharedPtr<SBox> BoxWidget;
	/** Timer handle used for triggering a rename after duplicating a recording. */
	FTimerHandle TimerHandle;
	/** True if there are recording assets that exist. */
	mutable TOptional<bool> bAssetsAvailableCached;
};

#undef LOCTEXT_NAMESPACE /* LiveLinkHub.RecordingListView */
