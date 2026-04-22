// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCineAssemblyConfigPanel.h"

#include "AssetToolsModule.h"
#include "CineAssemblyNamingTokens.h"
#include "CineAssemblyToolsStyle.h"
#include "IStructureDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "STemplateStringEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "SCineAssemblyConfigPanel"

void SCineAssemblyConfigPanel::Construct(const FArguments& InArgs, UCineAssembly* InAssembly)
{
	CineAssemblyToConfigure = InAssembly;

	TabSwitcher = SNew(SWidgetSwitcher)
		+ SWidgetSwitcher::Slot()
		[MakeDetailsWidget()]

		+ SWidgetSwitcher::Slot()
		[MakeHierarchyWidget()]

		+ SWidgetSwitcher::Slot()
		[MakeNotesWidget()];

	if (InArgs._HideSubAssemblies)
	{
		DetailsView->SetIsCustomRowVisibleDelegate(FIsCustomRowVisible::CreateSP(this, &SCineAssemblyConfigPanel::IsCustomRowVisible));
		DetailsView->SetObject(CineAssemblyToConfigure, true);
	}

		ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SSegmentedControl<int32>)
						.Value(0)
						.OnValueChanged_Lambda([Switcher = TabSwitcher](int32 NewValue)
							{
								Switcher->SetActiveWidgetIndex(NewValue);
							})

					+ SSegmentedControl<int32>::Slot(0)
						.Text(LOCTEXT("DetailsTab", "Details"))
						.Icon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Details").GetIcon())

					+ SSegmentedControl<int32>::Slot(1)
						.Text(LOCTEXT("HierarchyTab", "Hierarchy"))
						.Icon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed").GetIcon())

					+ SSegmentedControl<int32>::Slot(2)
						.Text(LOCTEXT("NotesTab", "Notes"))
						.Icon(FSlateIcon(FCineAssemblyToolsStyle::StyleName, "Icons.Notes").GetIcon())
				]

			+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.HAlign(HAlign_Fill)
				[
					TabSwitcher.ToSharedRef()
				]

			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
						.BorderImage(FCineAssemblyToolsStyle::Get().GetBrush("ProductionWizard.PanelBackground"))
						.Padding(16.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0.0f, 0.0f, 8.0f, 0.0f)
								[
									SNew(STextBlock).Text(LOCTEXT("AssemblyNameField", "Assembly Name"))
								]

							+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STemplateStringEditableTextBox)
										.Text_Lambda([this]() -> FText
											{
												return FText::FromString(CineAssemblyToConfigure->AssemblyName.Template);
											})
										.ResolvedText_Lambda([this]()
											{
												EvaluateTokenString(CineAssemblyToConfigure->AssemblyName);
												return CineAssemblyToConfigure->AssemblyName.Resolved;
											})
										.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type InCommitType)
											{
												CineAssemblyToConfigure->Modify();
												CineAssemblyToConfigure->AssemblyName.Template = InText.ToString();
											})
										.OnValidateTokenizedText(this, &SCineAssemblyConfigPanel::ValidateAssemblyName)
								]
						]
				]
		];
}

bool SCineAssemblyConfigPanel::ValidateAssemblyName(const FText& InText, FText& OutErrorMessage) const
{
	// Ensure that the name does not contain any characters that would be invalid for an asset name
	// This matches the validation that would happen if the user was renaming an asset in the content browser
	FString InvalidCharacters = INVALID_OBJECTNAME_CHARACTERS INVALID_LONGPACKAGE_CHARACTERS;

	// These characters are actually valid, because we want to support naming tokens
	InvalidCharacters = InvalidCharacters.Replace(TEXT("{}"), TEXT(""));
	InvalidCharacters = InvalidCharacters.Replace(TEXT(":"), TEXT(""));

	const FString PotentialName = InText.ToString();

	if (!FName::IsValidXName(PotentialName, InvalidCharacters, &OutErrorMessage))
	{
		return false;
	}

	if (PotentialName.Contains(TEXT("{assembly}")))
	{
		OutErrorMessage = LOCTEXT("RecursiveAssemblyTokenError", "You cannot use the {assembly} token");
		return false;
	}

	return true;
}

void SCineAssemblyConfigPanel::Refresh()
{
	// The details view needs to be redrawn to show the new metadata fields from the selected schema
	DetailsView->ForceRefresh();

	// Recreate the hierarchy tree items based on the selected schema
	PopulateHierarchyTree();
}

void SCineAssemblyConfigPanel::EvaluateTokenString(FTemplateString& StringToEvaluate)
{
	FDateTime CurrentTime = FDateTime::Now();
	if ((CurrentTime - LastTokenUpdateTime).GetSeconds() >= 1.0f)
	{
		StringToEvaluate.Resolved = UCineAssemblyNamingTokens::GetResolvedText(StringToEvaluate.Template, CineAssemblyToConfigure);
		LastTokenUpdateTime = CurrentTime;
	}
}

TSharedRef<SWidget> SCineAssemblyConfigPanel::MakeDetailsWidget()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;

	DetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(CineAssemblyToConfigure, true);

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
				.BorderImage(FCineAssemblyToolsStyle::Get().GetBrush("Borders.RecessedNoBorder"))
				.Padding(16.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
						.Padding(0.0f, 0.0f, 0.0f, 16.0f)
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0.0f, 0.0f, 8.0f, 0.0f)
								[
									SNew(SImage)
										.Image(this, &SCineAssemblyConfigPanel::GetSchemaThumbnail)
								]

							+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								[
									SNew(SVerticalBox)

									+ SVerticalBox::Slot()
										[
											SNew(STextBlock)
												.Text_Lambda([this]()
													{
														const UCineAssemblySchema* Schema = CineAssemblyToConfigure->GetSchema();
														return Schema ? FText::FromString(Schema->SchemaName) : LOCTEXT("NoSchemaName", "No Schema");
													})
										]

									+ SVerticalBox::Slot()
										[
											SNew(STextBlock)
												.Text(LOCTEXT("SchemClassName", "Cine Assembly Schema"))
												.ColorAndOpacity(FSlateColor::UseSubduedForeground())
										]
								]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text_Lambda([this]()
									{
										if (const UCineAssemblySchema* Schema = CineAssemblyToConfigure->GetSchema())
										{
											return !Schema->Description.IsEmpty() ? FText::FromString(Schema->Description) : LOCTEXT("EmptyDescription", "No description");
										}
										return LOCTEXT("SchemaInstructions", "Choose a schema to use as the base for configuring your Cine Assembly, or proceed with no schema.");
									})
						]
				]
		]

		+ SVerticalBox::Slot()
		.FillContentHeight(1.0f)
		[
			DetailsView.ToSharedRef()
		];
}

bool SCineAssemblyConfigPanel::IsCustomRowVisible(FName RowName, FName ParentName)
{
	if (RowName == GET_MEMBER_NAME_CHECKED(UCineAssembly, SubAssemblyNames))
	{
		return false;
	}
	return true;
}

const FSlateBrush* SCineAssemblyConfigPanel::GetSchemaThumbnail() const
{
	if (const UCineAssemblySchema* Schema = CineAssemblyToConfigure->GetSchema())
	{
		// Find the thumbnail brush associated with the selected schema
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UCineAssemblySchema::StaticClass()).Pin();

		if (AssetTypeActions)
		{
			const FAssetData AssetData = FAssetData(Schema);
			const FName AssetClassName = AssetData.AssetClassPath.GetAssetName();

			return AssetTypeActions->GetThumbnailBrush(AssetData, AssetClassName);
		}
	}

	return FCineAssemblyToolsStyle::Get().GetBrush("ClassThumbnail.CineAssemblySchema");
}

TSharedRef<SWidget> SCineAssemblyConfigPanel::MakeHierarchyWidget()
{
	HierarchyTreeView = SNew(STreeView<TSharedPtr<FHierarchyTreeItem>>)
		.TreeItemsSource(&HierarchyTreeItems)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SCineAssemblyConfigPanel::OnGenerateTreeRow)
		.OnGetChildren(this, &SCineAssemblyConfigPanel::OnGetChildren);

	// Create the hierarchy tree root
	RootItem = MakeShared<FHierarchyTreeItem>();
	RootItem->Type = FHierarchyTreeItem::EItemType::Folder;

	FTemplateString RootTemplate;
	RootTemplate.Template = TEXT("");
	RootItem->Path = RootTemplate;

	PopulateHierarchyTree();

	// Register a Slate timer that runs at a set frequency to evaluate all of the tokens in the tree view.
	// This will automatically be unregistered when this window is destroyed.
	constexpr float TimerFrequency = 1.0f;
	RegisterActiveTimer(TimerFrequency, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime) -> EActiveTimerReturnType
		{
			EvaluateHierarchyTokensRecursive(RootItem);
			HierarchyTreeView->RequestTreeRefresh();

			return EActiveTimerReturnType::Continue;
		}));

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(16.0f)
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("HierarchyInstructions", "The following content will be created as defined by the selected Schema."))
				.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		[
			SNew(SBorder)
				.BorderImage(FCineAssemblyToolsStyle::Get().GetBrush("ProductionWizard.RecessedBackground"))
				.Padding(8.0f)
				[
					HierarchyTreeView.ToSharedRef()
				]
		];
}

void SCineAssemblyConfigPanel::PopulateHierarchyTree()
{
	HierarchyTreeItems.Empty();
	HierarchyTreeItems.Add(RootItem);
	RootItem->ChildAssets.Empty();
	RootItem->ChildFolders.Empty();

	auto AddItemsToTree = [this](TArray<FTemplateString>& ItemList, FHierarchyTreeItem::EItemType ItemType)
		{
			// Sort the list so that paths are added to the tree in the proper order
			ItemList.Sort([](const FTemplateString& A, const FTemplateString& B) { return A.Template < B.Template; });

			for (const FTemplateString& ItemName : ItemList)
			{
				const FString ParentPath = FPaths::GetPath(ItemName.Template);

				// Walk the tree until we find an item whose path matches the parent path. The new tree item will be created as one of its children
				TSharedPtr<FHierarchyTreeItem> ParentItem = FindItemAtPathRecursive(RootItem, ParentPath);
				if (ParentItem)
				{
					TSharedPtr<FHierarchyTreeItem> NewItem = MakeShared<FHierarchyTreeItem>();
					NewItem->Type = ItemType;
					NewItem->Path = ItemName;

					if (ItemType == FHierarchyTreeItem::EItemType::Folder)
					{
						ParentItem->ChildFolders.Add(NewItem);
					}
					else
					{
						ParentItem->ChildAssets.Add(NewItem);
					}
				}
			}
		};

	if (const UCineAssemblySchema* Schema = CineAssemblyToConfigure->GetSchema())
	{
		TArray<FTemplateString> FolderTemplates;
		FolderTemplates.Reserve(Schema->FoldersToCreate.Num());
		Algo::Transform(Schema->FoldersToCreate, FolderTemplates, [](const FString& TemplateString)
			{
				FTemplateString FolderTemplate;
				FolderTemplate.Template = TemplateString;
				return FolderTemplate;
			});

		AddItemsToTree(FolderTemplates, FHierarchyTreeItem::EItemType::Folder);
		AddItemsToTree(CineAssemblyToConfigure->SubAssemblyNames, FHierarchyTreeItem::EItemType::Asset);

		// Add the top-level assembly tree item
		TSharedPtr<FHierarchyTreeItem> ParentItem = FindItemAtPathRecursive(RootItem, Schema->DefaultAssemblyPath);
		if (ParentItem)
		{
			TSharedPtr<FHierarchyTreeItem> NewItem = MakeShared<FHierarchyTreeItem>();
			NewItem->Type = FHierarchyTreeItem::EItemType::Asset;
			NewItem->Path.Template = TEXT("{assembly}");

			ParentItem->ChildAssets.Add(NewItem);
		}

		EvaluateHierarchyTokensRecursive(RootItem);
	}

	HierarchyTreeView->RequestTreeRefresh();
	ExpandTreeRecursive(RootItem);
}

void SCineAssemblyConfigPanel::EvaluateHierarchyTokensRecursive(TSharedPtr<FHierarchyTreeItem> TreeItem)
{
	// Evaluate the token template string for this tree item
	TreeItem->Path.Resolved = UCineAssemblyNamingTokens::GetResolvedText(TreeItem->Path.Template, CineAssemblyToConfigure);
	if (TreeItem->Path.Resolved.IsEmpty())
	{
		TreeItem->Path.Resolved = FText::FromString(TreeItem->Path.Template);
	}

	// Evaluate the tokens for all of the child assets, then resort them alphabetically based on the resolved paths
	for (const TSharedPtr<FHierarchyTreeItem>& Asset : TreeItem->ChildAssets)
	{
		EvaluateHierarchyTokensRecursive(Asset);
	}

	TreeItem->ChildAssets.Sort([](const TSharedPtr<FHierarchyTreeItem>& A, const TSharedPtr<FHierarchyTreeItem>& B) { return A->Path.Resolved.ToString() < B->Path.Resolved.ToString(); });

	// Evaluate the tokens for all of the child folders, then resort them alphabetically based on the resolved paths
	for (const TSharedPtr<FHierarchyTreeItem>& Child : TreeItem->ChildFolders)
	{
		EvaluateHierarchyTokensRecursive(Child);
	}

	TreeItem->ChildFolders.Sort([](const TSharedPtr<FHierarchyTreeItem>& A, const TSharedPtr<FHierarchyTreeItem>& B) { return A->Path.Resolved.ToString() < B->Path.Resolved.ToString(); });
}

void SCineAssemblyConfigPanel::ExpandTreeRecursive(TSharedPtr<FHierarchyTreeItem> TreeItem) const
{
	if (HierarchyTreeView)
	{
		HierarchyTreeView->SetItemExpansion(TreeItem, true);
	}

	for (const TSharedPtr<FHierarchyTreeItem>& ChildItem : TreeItem->ChildFolders)
	{
		ExpandTreeRecursive(ChildItem);
	}
}

TSharedPtr<FHierarchyTreeItem> SCineAssemblyConfigPanel::FindItemAtPathRecursive(TSharedPtr<FHierarchyTreeItem> TreeItem, const FString& Path) const
{
	if (TreeItem->Path.Template.Equals(Path))
	{
		return TreeItem;
	}

	for (const TSharedPtr<FHierarchyTreeItem>& Child : TreeItem->ChildFolders)
	{
		if (const TSharedPtr<FHierarchyTreeItem>& ItemAtPath = FindItemAtPathRecursive(Child, Path))
		{
			return ItemAtPath;
		}
	}

	return nullptr;
}

TSharedRef<ITableRow> SCineAssemblyConfigPanel::OnGenerateTreeRow(TSharedPtr<FHierarchyTreeItem> TreeItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedPtr<SImage> Icon = SNew(SImage);

	if (TreeItem->Type == FHierarchyTreeItem::EItemType::Folder)
	{
		Icon->SetImage(FCineAssemblyToolsStyle::Get().GetBrush("Icons.Folder"));
		Icon->SetColorAndOpacity(FAppStyle::Get().GetSlateColor("ContentBrowser.DefaultFolderColor"));
	}
	else
	{
		Icon->SetImage(FCineAssemblyToolsStyle::Get().GetBrush("Icons.Sequencer"));
		Icon->SetColorAndOpacity(FLinearColor::White);
	}

	return SNew(STableRow<TSharedPtr<FHierarchyTreeItem>>, OwnerTable)
		.Padding(FMargin(8.0f, 2.0f, 8.0f, 0.0f))
		.Content()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					Icon.ToSharedRef()
				]

			+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text_Lambda([this, TreeItem]() -> FText
							{
								return (TreeItem == RootItem) ? LOCTEXT("RootPathName", "Root Folder") : FText::FromString(FPaths::GetPathLeaf(TreeItem->Path.Resolved.ToString()));
							})
				]
		];
}

void SCineAssemblyConfigPanel::OnGetChildren(TSharedPtr<FHierarchyTreeItem> TreeItem, TArray<TSharedPtr<FHierarchyTreeItem>>& OutNodes)
{
	// Display all of the child assets first, followed by all of the child folders
	OutNodes.Append(TreeItem->ChildAssets);
	OutNodes.Append(TreeItem->ChildFolders);
}

TSharedRef<SWidget> SCineAssemblyConfigPanel::MakeNotesWidget()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(16.0f)
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("NoteInstructions", "The following notes will be saved with the assembly. This can also be edited later."))
				.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		[
			SNew(SBorder)
				.BorderImage(FCineAssemblyToolsStyle::Get().GetBrush("Borders.Background"))
				.Padding(16.0f)
				[
					SNew(SMultiLineEditableText)
						.HintText(LOCTEXT("NoteHintText", "Assembly Notes"))
						.Text_Lambda([this]()
							{
								return FText::FromString(CineAssemblyToConfigure->AssemblyNote);
							})
						.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type InCommitType)
							{
								CineAssemblyToConfigure->Modify();
								CineAssemblyToConfigure->AssemblyNote = InText.ToString();
							})
				]
		];
}

#undef LOCTEXT_NAMESPACE