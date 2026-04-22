// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDuplicateAssemblyWindow.h"

#include "AssetToolsModule.h"
#include "CineAssemblyFactory.h"
#include "CineAssemblyNamingTokens.h"
#include "CineAssemblyToolsStyle.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "MovieScene.h"
#include "MovieSceneFolder.h"
#include "ObjectTools.h"
#include "SCineAssemblyConfigPanel.h"
#include "Sections/MovieSceneSubSection.h"
#include "STemplateStringEditableTextBox.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDuplicateAssemblyWindow"

const FName SDuplicateAssemblyWindow::OriginalNameColumn = "OriginalName";
const FName SDuplicateAssemblyWindow::DuplicationModeColumn = "DuplicationMode";
const FName SDuplicateAssemblyWindow::DuplicateNameColumn = "DuplicateName";

const FString SDuplicateAssemblyWindow::DuplicationPreferenceSection = TEXT("CinematicAssemblyDuplication_");

void SDuplicateAssemblyWindow::Construct(const FArguments& InArgs, UCineAssembly* InAssembly)
{
	OriginalAssembly = InAssembly;
	Schema = OriginalAssembly->GetSchema();

	// Duplicate the original assembly into a new transient assembly that can be configured in the UI
	// Note: all of the subsequence tracks/sections will be duplicated, but will reference the sequences in the original assembly
	// So if the user chooses MaintainReference as the duplication mode for any subsequence, no further work is needed for that subsequence
	DuplicateAssembly.Reset(Cast<UCineAssembly>(StaticDuplicateObject(OriginalAssembly, GetTransientPackage(), NAME_None, RF_Transient)));

	// Get the path and root folder of the original assembly) and initialize the duplication path to be the same root folder as the original
	OriginalAssembly->GetAssetPathAndRootFolder(OriginalAssemblyPath, OriginalAssemblyRootFolder);
	DuplicationPath = OriginalAssemblyRootFolder;

	// This path will be used to find the relative paths for some subsequences, but the function that finds the relative path only works if there is a trailing '/'
	OriginalAssemblyPath = OriginalAssemblyPath.AppendChar('/');

	const FVector2D DefaultWindowSize = FVector2D(1200, 750);

	SWindow::Construct(SWindow::FArguments()
		.Title(LOCTEXT("WindowTitle", "Duplicate Assembly"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(DefaultWindowSize)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SBorder)
				.BorderImage(FCineAssemblyToolsStyle::Get().GetBrush("ProductionWizard.PanelBackground"))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
						[
							SNew(SSplitter)
								.Orientation(Orient_Horizontal)
								.PhysicalSplitterHandleSize(2.0f)

							+ SSplitter::Slot()
								.Value(0.65f)
								[
									MakeSubsequencesPanel()
								]

							+ SSplitter::Slot()
								.Value(0.35f)
								[
									SNew(SCineAssemblyConfigPanel, DuplicateAssembly.Get())
										.HideSubAssemblies(true)
								]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SSeparator)
								.Orientation(Orient_Horizontal)
								.Thickness(2.0f)
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						[
							MakeButtonsPanel()
						]
				]
		]);
}

TSharedRef<SWidget> SDuplicateAssemblyWindow::MakeSubsequencesPanel()
{
	// Walk the duplicate assembly to recursively find every subsequence and build up the subsequence tree
	PopulateSubsequenceTreeRecurisve(DuplicateAssembly.Get(), nullptr);

	TreeView = SNew(STreeView<TSharedPtr<FSequenceTreeItem>>)
		.TreeItemsSource(&TreeItems)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SDuplicateAssemblyWindow::OnGenerateTreeRow)
		.OnGetChildren(this, &SDuplicateAssemblyWindow::OnGetChildren)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(OriginalNameColumn)
			.DefaultLabel(LOCTEXT("OriginalNameColumn", "Original Name"))
			.FillWidth(0.3f)
			.HeaderContentPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))

			+ SHeaderRow::Column(DuplicationModeColumn)
			.DefaultLabel(LOCTEXT("DuplicationModeColumn", "Duplication Mode"))
			.FillWidth(0.35f)
			.HeaderContentPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))

			+ SHeaderRow::Column(DuplicateNameColumn)
			.DefaultLabel(LOCTEXT("DuplicateNameColumn", "Duplicate Name"))
			.FillWidth(0.35f)
			.HeaderContentPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
		);

	// Start with the entire subsequence tree expanded
	for (TSharedPtr<FSequenceTreeItem>& Item : TreeItems)
	{
		ExpandTreeItem(Item);
	}

	// Create a path picker widget to let the user choose where the duplicate assembly should be created
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.DefaultPath = OriginalAssemblyRootFolder;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateLambda([this](const FString& NewPath)
		{
			DuplicationPath = NewPath;
		});

	TSharedRef<SWidget> PathPicker = ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig);

	// Register a Slate timer that runs at a set frequency to evaluate all of the tokens in the tree view.
	// This will automatically be unregistered when this window is destroyed.
	constexpr float TimerFrequency = 1.0f;
	RegisterActiveTimer(TimerFrequency, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime) -> EActiveTimerReturnType
		{
			for (TSharedPtr<FSequenceTreeItem>& Item : TreeItems)
			{
				ResolveSubsequenceNameRecursive(Item);
			}

			return EActiveTimerReturnType::Continue;
		}));

	// Resolve the subsequence names once, immediately, so they appear correctly when the window first opens
	for (TSharedPtr<FSequenceTreeItem>& Item : TreeItems)
	{
		ResolveSubsequenceNameRecursive(Item);
	}

	return SNew(SBorder)
		.BorderImage(FCineAssemblyToolsStyle::Get().GetBrush("ProductionWizard.PanelBackground"))
		.Padding(16.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("DuplicateAssemblyTitle", "Subsequences"))
						.Font(FCoreStyle::GetDefaultFontStyle("Normal", 14))
				]

			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("DuplicateAssemblyInstructions", "Choose how subsequences will be duplicated."))
						.Font(FCoreStyle::GetDefaultFontStyle("Normal", 10))
				]

			+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					TreeView.ToSharedRef()
				]

			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("DuplicationPath", "Duplication Path"))
						]

					+ SHorizontalBox::Slot()
						.FillContentWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SEditableTextBox)
								.Text_Lambda([this]() { return FText::FromString(DuplicationPath); })
								.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type InCommitType) { DuplicationPath = InText.ToString(); })
						]

					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SComboButton)
								.MenuContent()
								[
									SNew(SVerticalBox)

									+ SVerticalBox::Slot()
										.MaxHeight(300.0f)
										[
											PathPicker
										]
								]
								.ButtonContent()
								[
	 								SNew(SImage).Image(FCineAssemblyToolsStyle::Get().GetBrush("Icons.Folder"))
								]
						]
				]
		];
}

TSharedRef<SWidget> SDuplicateAssemblyWindow::MakeButtonsPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(16.0f)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.MinWidth(118.0f)
				.MaxWidth(118.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
						.Text(LOCTEXT("DuplicateButton", "Duplicate"))
						.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
						.HAlign(HAlign_Center)
						.OnClicked(this, &SDuplicateAssemblyWindow::OnDuplicateClicked)
				]

			+ SHorizontalBox::Slot()
				.MinWidth(118.0f)
				.MaxWidth(118.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
						.Text(LOCTEXT("CancelButton", "Cancel"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SDuplicateAssemblyWindow::OnCancelClicked)
				]
		];
}

FReply SDuplicateAssemblyWindow::OnDuplicateClicked()
{
	UCineAssemblyFactory::CreateConfiguredAssembly(DuplicateAssembly.Get(), DuplicationPath);

	// Flatten the duplication data from the subsequence tree into a map
	for (TSharedPtr<FSequenceTreeItem>& Item : TreeItems)
	{
		BuildDuplicationMapRecursive(Item);
	}

	DuplicateAssembly->GetAssetPathAndRootFolder(DuplicateAssemblyPath, DuplicateAssemblyRootFolder);

	// Duplicate any subsequences that are set to DuplicateOriginal
	DuplicateSubsequencesRecursive(DuplicateAssembly.Get());

	// Remove any subsequencers that are set to Remove
	RemoveSubsequencesRecursive(DuplicateAssembly.Get());

	// Write out the duplication mode values for each subassembly to a config file so those values can be used as the default the next time an assembly with this schema type is duplicated.
	SaveDuplicationPreferences();

	RequestDestroyWindow();
	return FReply::Handled();
}

FReply SDuplicateAssemblyWindow::OnCancelClicked()
{
	RequestDestroyWindow();
	return FReply::Handled();
}

TSharedRef<ITableRow> SDuplicateAssemblyWindow::OnGenerateTreeRow(TSharedPtr<FSequenceTreeItem> TreeItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSubsequenceDuplicationRow, OwnerTable, TreeItem);
}

void SDuplicateAssemblyWindow::OnGetChildren(TSharedPtr<FSequenceTreeItem> TreeItem, TArray<TSharedPtr<FSequenceTreeItem>>& OutNodes)
{
	OutNodes.Append(TreeItem->Children);
}

void SDuplicateAssemblyWindow::PopulateSubsequenceTreeRecurisve(UMovieSceneSequence* InSequence, TSharedPtr<FSequenceTreeItem> InItem)
{
	UMovieScene* MovieScene = InSequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	// Loop through all of the tracks and all of the sections to find every subsequence in the input sequence
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track))
		{
			for (UMovieSceneSection* Section : SubTrack->GetAllSections())
			{
				if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section))
				{
					if (UMovieSceneSequence* Subsequence = SubSection->GetSequence())
					{
						TSharedPtr<FSequenceTreeItem> NewItem = MakeShared<FSequenceTreeItem>();
						NewItem->Subsequence = Subsequence;

						// Check if this subsequence is one of the subassemblies created from the schema
						const int32 SubAssemblyIndex = DuplicateAssembly->SubAssemblies.IndexOfByKey(SubSection);

						if (SubAssemblyIndex != INDEX_NONE)
						{
							// If this is a subassembly, use the template name (originally defined by the schema)
							NewItem->DuplicationData.bIsSubAssembly = true;
							NewItem->DuplicationData.SubsequenceName.Template = DuplicateAssembly->SubAssemblyNames[SubAssemblyIndex].Template;
							NewItem->DuplicationData.DuplicationMode = GetDuplicationPreference(NewItem->DuplicationData.SubsequenceName.Template);
						}
						else
						{
							// If this is not a subassembly, find the relative path to the original assembly so that a duplicate susbequence can be created with the same relative folder structure
							NewItem->DuplicationData.bIsSubAssembly = false;

							FString SubsequencePath = FPaths::GetPath(Subsequence->GetPathName());
							FPaths::MakePathRelativeTo(SubsequencePath, *OriginalAssemblyPath);

							NewItem->DuplicationData.SubsequenceName.Template = SubsequencePath / Subsequence->GetDisplayName().ToString();
							NewItem->DuplicationData.DuplicationMode = ECineAssemblyDuplicationMode::DuplicateOriginal;
						}

						if (InItem)
						{
							InItem->Children.Add(NewItem);
							NewItem->Parent = InItem;
						}
						else
						{
							TreeItems.Add(NewItem);
							NewItem->Parent = nullptr;
						}

						PopulateSubsequenceTreeRecurisve(Subsequence, NewItem);
					}
				}
			}
		}
	}
}

void SDuplicateAssemblyWindow::DuplicateSubsequencesRecursive(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	// In order to duplicate subsequences, the MovieScene needs to be mutable.
	// The original read-only state will be restored when we are done.
	const bool bIsReadOnly = MovieScene->IsReadOnly();
	MovieScene->SetReadOnly(false);

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Loop through all of the tracks and all of the sections to find every subsequence in the input sequence
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track))
		{
			for (UMovieSceneSection* Section : SubTrack->GetAllSections())
			{
				if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section))
				{
					if (UMovieSceneSequence* Subsequence = SubSection->GetSequence())
					{
						if (!SubsequenceDuplicationData.Contains(Subsequence))
						{
							continue;
						}

						const FSubsequenceDuplicationData& DuplicationData = SubsequenceDuplicationData[Subsequence];

						if (DuplicationData.DuplicationMode == ECineAssemblyDuplicationMode::DuplicateOriginal)
						{
							const FString SubsequenceNameAndPath = UCineAssemblyNamingTokens::GetResolvedText(DuplicationData.SubsequenceName.Template, DuplicateAssembly.Get()).ToString();

							FString DesiredAssetName;
							if (DuplicationData.bIsSubAssembly)
							{
								// Update the display name of the subsequence track to match the resolved subassembly name
								const FString SubAssemblyFilename = FPaths::GetBaseFilename(SubsequenceNameAndPath);
								SubTrack->SetDisplayName(FText::FromString(SubAssemblyFilename));

								DesiredAssetName = DuplicateAssemblyRootFolder / SubsequenceNameAndPath;
							}
							else
							{
								DesiredAssetName = DuplicateAssemblyPath / SubsequenceNameAndPath;
								FPaths::CollapseRelativeDirectories(DesiredAssetName);
							}

							FString UniquePackageName;
							FString UniqueAssetName;
							AssetTools.CreateUniqueAssetName(DesiredAssetName, TEXT(""), UniquePackageName, UniqueAssetName);

							const FString SubAssemblyPath = FPaths::GetPath(UniquePackageName);
							UMovieSceneSequence* DuplicateSubsequence = Cast<UMovieSceneSequence>(AssetTools.DuplicateAsset(UniqueAssetName, SubAssemblyPath, Subsequence));

							if (DuplicateSubsequence)
							{
								// Update the sequence of the duplicate subsection to be the duplicate subsequence
								// If the section is locked, unlock it, then update the subsequence, then lock it again
								const bool bIsLocked = SubSection->IsLocked();
								SubSection->SetIsLocked(false);
								SubSection->SetSequence(DuplicateSubsequence);
								SubSection->SetIsLocked(bIsLocked);

								// Update the data in the duplication data map to correspond to the new duplicate subsequence
								SubsequenceDuplicationData.Add(DuplicateSubsequence, DuplicationData);
								SubsequenceDuplicationData.Remove(Subsequence);
							}
						}

						DuplicateSubsequencesRecursive(SubSection->GetSequence());
					}
				}
			}
		}
	}

	// Restore the original read-only state
	MovieScene->SetReadOnly(bIsReadOnly);
}

void SDuplicateAssemblyWindow::RemoveSubsequencesRecursive(UMovieSceneSequence* Sequence)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TArray<UMovieSceneTrack*> TracksToRemove;

	// Loop through all of the tracks and all of the sections to find every subsequence in the input sequence
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track))
		{
			TArray<UMovieSceneSection*> SectionsToRemove;

			for (UMovieSceneSection* Section : SubTrack->GetAllSections())
			{
				if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section))
				{
					if (UMovieSceneSequence* Subsequence = SubSection->GetSequence())
					{
						if (!SubsequenceDuplicationData.Contains(Subsequence))
						{
							continue;
						}

						const FSubsequenceDuplicationData& DuplicationData = SubsequenceDuplicationData[Subsequence];
						if (DuplicationData.DuplicationMode == ECineAssemblyDuplicationMode::Remove)
						{
							SectionsToRemove.Add(SubSection);
						}
						else
						{
							RemoveSubsequencesRecursive(Subsequence);
						}
					}
				}
			}

			for (UMovieSceneSection* Section : SectionsToRemove)
			{
				SubTrack->Modify();
				SubTrack->RemoveSection(*Section);
			}

			// If we have now removed every section from the subsequence track, we can remove the entire track as well
			if (SubTrack->IsEmpty())
			{
				TracksToRemove.Add(SubTrack);
			}
		}
	}

	for (UMovieSceneTrack* Track : TracksToRemove)
	{
		MovieScene->RemoveTrack(*Track);
	}

	TArray<UMovieSceneFolder*> RootFolders;
	MovieScene->GetRootFolders(RootFolders);

	RemoveTracksFromFoldersRecursive(RootFolders, TracksToRemove);
}

void SDuplicateAssemblyWindow::RemoveTracksFromFoldersRecursive(TArrayView<UMovieSceneFolder* const> Folders, const TArray<UMovieSceneTrack*>& TracksToRemove)
{
	for (UMovieSceneFolder* Folder : Folders)
	{
		for (UMovieSceneTrack* Track : TracksToRemove)
		{
			Folder->RemoveChildTrack(Track);
		}

		RemoveTracksFromFoldersRecursive(Folder->GetChildFolders(), TracksToRemove);
	}
}

void SDuplicateAssemblyWindow::BuildDuplicationMapRecursive(const TSharedPtr<FSequenceTreeItem> InItem)
{
	SubsequenceDuplicationData.Add(InItem->Subsequence, InItem->DuplicationData);

	for (const TSharedPtr<FSequenceTreeItem>& Child : InItem->Children)
	{
		BuildDuplicationMapRecursive(Child);
	}
}

void SDuplicateAssemblyWindow::ResolveSubsequenceNameRecursive(TSharedPtr<FSequenceTreeItem> InItem)
{
	// Evaluate the token template string for this tree item
	FSubsequenceDuplicationData& DuplicationData = InItem->DuplicationData;
	DuplicationData.SubsequenceName.Resolved = UCineAssemblyNamingTokens::GetResolvedText(DuplicationData.SubsequenceName.Template, DuplicateAssembly.Get());

	for (const TSharedPtr<FSequenceTreeItem>& Child : InItem->Children)
	{
		ResolveSubsequenceNameRecursive(Child);
	}
}

void SDuplicateAssemblyWindow::ExpandTreeItem(TSharedPtr<FSequenceTreeItem> InItem) const
{
	TreeView->SetItemExpansion(InItem, true);

	for (TSharedPtr<FSequenceTreeItem>& Child : InItem->Children)
	{
		ExpandTreeItem(Child);
	}
}

void SDuplicateAssemblyWindow::SaveDuplicationPreferences() const
{
	if (Schema && GConfig)
	{
		const FString SectionName = DuplicationPreferenceSection + Schema->SchemaName;

		for (const TPair<UMovieSceneSequence*, FSubsequenceDuplicationData>& DuplicationData : SubsequenceDuplicationData)
		{
			if (DuplicationData.Value.bIsSubAssembly)
			{
				// The config strips off these token characters (for some reason), so we save and restore the values without them
				FString TemplateName = DuplicationData.Value.SubsequenceName.Template;
				TemplateName = TemplateName.Replace(TEXT("{"), TEXT(""));
				TemplateName = TemplateName.Replace(TEXT("}"), TEXT(""));

				const int32 Value = static_cast<int32>(DuplicationData.Value.DuplicationMode);
				GConfig->SetInt(*SectionName, *TemplateName, Value, GEditorPerProjectIni);
			}
		}
	}
}

ECineAssemblyDuplicationMode SDuplicateAssemblyWindow::GetDuplicationPreference(const FString& SubAssemblyName) const
{
	if (Schema && GConfig)
	{
		const FString SectionName = DuplicationPreferenceSection + Schema->SchemaName;

		// The config strips off these token characters (for some reason), so we save and restore the values without them
		FString TemplateName = SubAssemblyName;
		TemplateName = TemplateName.Replace(TEXT("{"), TEXT(""));
		TemplateName = TemplateName.Replace(TEXT("}"), TEXT(""));

		int32 Value = 0;
		GConfig->GetInt(*SectionName, *TemplateName, Value, GEditorPerProjectIni);

		return static_cast<ECineAssemblyDuplicationMode>(Value);
	}

	return ECineAssemblyDuplicationMode::DuplicateOriginal;
}

void SSubsequenceDuplicationRow::Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedPtr<FSequenceTreeItem>& InTreeItem)
{
	TreeItem = InTreeItem;

	FSuperRowType::FArguments StyleArguments = FSuperRowType::FArguments()
		.Padding(FMargin(4.0f, 8.0f))
		.Style(&FAppStyle().GetWidgetStyle<FTableRowStyle>("TableView.AlternatingRow"));

	SMultiColumnTableRow<TSharedPtr<FSequenceTreeItem>>::Construct(StyleArguments, OwnerTableView);
}

TSharedRef<SWidget> SSubsequenceDuplicationRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == SDuplicateAssemblyWindow::OriginalNameColumn)
	{
		return MakeOriginalNameWidget();
	}
	else if (ColumnName == SDuplicateAssemblyWindow::DuplicationModeColumn)
	{
		return MakeDuplicationModeWidget();
	}
	else if (ColumnName == SDuplicateAssemblyWindow::DuplicateNameColumn)
	{
		return MakeDuplicateNameWidget();
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SSubsequenceDuplicationRow::MakeOriginalNameWidget()
{
	return SNew(SHorizontalBox)

		// The first column gets the expander / indent for children items in the tree view
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SExpanderArrow, SharedThis(this)).IndentAmount(12)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SImage).Image(FCineAssemblyToolsStyle::Get().GetBrush("Icons.Sequencer"))
		]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(TreeItem->Subsequence->GetDisplayName())
		];
}

TSharedRef<SWidget> SSubsequenceDuplicationRow::MakeDuplicationModeWidget()
{
	FMenuBuilder DuplicationModeMenu(true, nullptr);
	{
		auto AddEntryForDuplicationMode = [this, &DuplicationModeMenu](ECineAssemblyDuplicationMode DuplicationMode)
			{
				DuplicationModeMenu.AddMenuEntry(
					GetDuplicationModeDisplayName(DuplicationMode),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SSubsequenceDuplicationRow::SetDuplicationModeRecursive, TreeItem, DuplicationMode)),
					NAME_None,
					EUserInterfaceActionType::Button
				);
			};

		AddEntryForDuplicationMode(ECineAssemblyDuplicationMode::DuplicateOriginal);
		AddEntryForDuplicationMode(ECineAssemblyDuplicationMode::MaintainReference);
		AddEntryForDuplicationMode(ECineAssemblyDuplicationMode::Remove);
	}

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4.0f, 0.0f, 16.0f, 0.0f)
		[
			SNew(SComboButton)
				.MenuContent()
				[
					DuplicationModeMenu.MakeWidget()
				]
				.ButtonContent()
				[
					SNew(STextBlock).Text_Lambda([this]() { return GetDuplicationModeDisplayName(TreeItem->DuplicationData.DuplicationMode); })
				]
				.IsEnabled_Lambda([this]()
					{
						// Disable the duplication mode widget if the parent is MaintainReference or Remove (since the value should be locked to match the parent)
						if (TreeItem->Parent && TreeItem->Parent->DuplicationData.DuplicationMode != ECineAssemblyDuplicationMode::DuplicateOriginal)
						{
							return false;
						}
						return true;
					})
		];
}

TSharedRef<SWidget> SSubsequenceDuplicationRow::MakeDuplicateNameWidget()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(10.0f, 0.0f, 16.0f, 0.0f)
		[
			// TODO: STemplateStringEditableTextBox does not currently support input validation because SMultiLineEditableText does not support it.
			// Once we add support to that widget, add some input validation here to ensure that the input is a valid object name
			SNew(STemplateStringEditableTextBox)
				.Text_Lambda([this]()
					{
						if (TreeItem->DuplicationData.DuplicationMode == ECineAssemblyDuplicationMode::DuplicateOriginal)
						{
							const FString SubsequenceName = FPaths::GetBaseFilename(TreeItem->DuplicationData.SubsequenceName.Template);
							return FText::FromString(SubsequenceName);
						}
						return TreeItem->Subsequence->GetDisplayName();
					})
				.ResolvedText_Lambda([this]()
					{
						if (TreeItem->DuplicationData.DuplicationMode == ECineAssemblyDuplicationMode::DuplicateOriginal)
						{
							const FString SubsequenceName = FPaths::GetBaseFilename(TreeItem->DuplicationData.SubsequenceName.Resolved.ToString());
							return FText::FromString(SubsequenceName);
						}
						return TreeItem->Subsequence->GetDisplayName();
					})
				.OnTextCommitted(this, &SSubsequenceDuplicationRow::OnSubsequenceNameCommitted)
				.IsReadOnly_Lambda([this]()
					{
						return TreeItem->DuplicationData.DuplicationMode != ECineAssemblyDuplicationMode::DuplicateOriginal;
					})
				.Visibility_Lambda([this]()
					{
						return (TreeItem->DuplicationData.DuplicationMode != ECineAssemblyDuplicationMode::Remove) ? EVisibility::Visible : EVisibility::Hidden;
					})
		];
}

FText SSubsequenceDuplicationRow::GetDuplicationModeDisplayName(ECineAssemblyDuplicationMode DuplicationMode) const
{
	switch (DuplicationMode)
	{
	case ECineAssemblyDuplicationMode::DuplicateOriginal:
		return LOCTEXT("DuplicateOriginalDisplayName", "Duplicate Original");
	case ECineAssemblyDuplicationMode::MaintainReference:
		return LOCTEXT("MaintainReferenceDisplayName", "Maintain Reference");
	case ECineAssemblyDuplicationMode::Remove:
		return LOCTEXT("RemoveDisplayName", "Remove");
	}

	return FText::GetEmpty();
}

void SSubsequenceDuplicationRow::SetDuplicationModeRecursive(TSharedPtr<FSequenceTreeItem> InItem, ECineAssemblyDuplicationMode Mode)
{
	InItem->DuplicationData.DuplicationMode = Mode;

	if (Mode != ECineAssemblyDuplicationMode::DuplicateOriginal)
	{
		for (TSharedPtr<FSequenceTreeItem>& Child : InItem->Children)
		{
			SetDuplicationModeRecursive(Child, Mode);
		}
	}
}

void SSubsequenceDuplicationRow::OnSubsequenceNameCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	const FString OldPath = TreeItem->DuplicationData.SubsequenceName.Template;
	const FString NewPath = FPaths::GetPath(OldPath) / InText.ToString();
	TreeItem->DuplicationData.SubsequenceName.Template = NewPath;
}

#undef LOCTEXT_NAMESPACE
