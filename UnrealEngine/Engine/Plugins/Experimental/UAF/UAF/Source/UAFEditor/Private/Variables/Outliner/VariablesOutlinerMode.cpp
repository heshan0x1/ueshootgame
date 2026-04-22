// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerMode.h"

#include "ISourceControlModule.h"
#include "WorkspaceItemMenuContext.h"
#include "VariablesOutlinerHierarchy.h"
#include "VariablesOutlinerEntryItem.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "IWorkspaceEditor.h"
#include "AnimNextEditorModule.h"
#include "UAFStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Framework/Commands/GenericCommands.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Variables/AnimNextVariableItemMenuContext.h"
#include "AnimNextRigVMAsset.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IWorkspaceEditorModule.h"
#include "StructViewerFilter.h"
#include "StructViewerModule.h"
#include "VariablesOutlinerAssetItem.h"
#include "VariablesOutlinerCommands.h"
#include "VariablesOutlinerStructSharedVariablesItem.h"
#include "VariablesOutlinerCategoryItem.h"
#include "VariablesOutlinerDragDrop.h"
#include "Common/AnimNextAssetFindReplaceVariables.h"
#include "Common/GraphEditorSchemaActions.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Variables/SVariablesView.h"
#include "Entries/AnimNextSharedVariablesEntry.h"
#include "Styling/SlateIconFinder.h"
#include "Variables/AnimNextSharedVariables.h"
#include "Variables/AnimNextSharedVariablesFactory.h"
#include "Variables/AnimNextVariableSettings.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerMode"

namespace UE::UAF::Editor
{

FVariablesOutlinerMode::FVariablesOutlinerMode(SVariablesOutliner* InVariablesOutliner, const TSharedRef<UE::Workspace::IWorkspaceEditor>& InWorkspaceEditor)
	: ISceneOutlinerMode(InVariablesOutliner)
	, WeakWorkspaceEditor(InWorkspaceEditor)
{
	CommandList = MakeShared<FUICommandList>();

	static const FName MenuName("VariablesOutliner.AddVariablesMenu");
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		FToolMenuOwnerScoped ToolMenuOwnerScope(this);

		if (UToolMenu* Menu = ToolMenus->RegisterMenu(MenuName))
		{
			Menu->AddDynamicSection(TEXT("Variables"), FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				PopulateNewVariableToolMenuEntries(InMenu, false);				
			}));
		}
	}
}

void FVariablesOutlinerMode::Rebuild()
{
	Hierarchy = CreateHierarchy();
}

TSharedPtr<SWidget> FVariablesOutlinerMode::CreateContextMenu()
{
	static const FName MenuName("VariablesOutliner.ItemContextMenu");
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		FToolMenuOwnerScoped ToolMenuOwnerScope(this);
		if (UToolMenu* Menu = ToolMenus->RegisterMenu(MenuName))
		{
			Menu->AddDynamicSection(TEXT("Assets"), FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				const UAssetEditorToolkitMenuContext* EditorContext = InMenu->FindContext<UAssetEditorToolkitMenuContext>();
				const UAnimNextVariableItemMenuContext* MenuContext = InMenu->FindContext<UAnimNextVariableItemMenuContext>();
				if(EditorContext == nullptr || MenuContext == nullptr)
				{
					return;
				}

				const int32 NumSelectedVariables = MenuContext->WeakEntries.Num();
				
				/** Asset specific actions */
				FToolMenuSection& AssetSection = InMenu->AddSection("Asset", LOCTEXT("AssetSectionLabel", "Asset"));
				{
					AssetSection.AddMenuEntryWithCommandList(FVariablesOutlinerCommands::Get().SaveAsset, MenuContext->WeakCommandList.Pin(), TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset"));
					AssetSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Paste, MenuContext->WeakCommandList.Pin());
				}

				/** Variable specific actions */
				FToolMenuSection& VariablesSection = InMenu->AddSection("Variables", LOCTEXT("VariablesSectionLabel", "Variables"));
				{
					// Populates entries for adding of different variable types/sources					
					PopulateNewVariableToolMenuEntries(InMenu, true);
					
					VariablesSection.AddSubMenu("FindReferencesMenu", LOCTEXT("FindReferencesMenuLabel", "Find References..."), LOCTEXT("FindReferencesMenuToolTip", "Find references to selected variables by different means."),
					FNewToolMenuDelegate::CreateLambda(
						[MenuContext](UToolMenu* InSubmenu)
							{
								InSubmenu->AddMenuEntry(NAME_None, FToolMenuEntry::InitMenuEntryWithCommandList(FVariablesOutlinerCommands::Get().FindReferences, MenuContext->WeakCommandList.Pin(), TAttribute<FText>(), TAttribute<FText>(), UAnimNextAssetFindReplaceVariables::GetIconFromSearchScope(ESearchScope::Global)));

								InSubmenu->AddMenuEntry(NAME_None, FToolMenuEntry::InitMenuEntryWithCommandList(FVariablesOutlinerCommands::Get().FindReferencesInWorkspace, MenuContext->WeakCommandList.Pin(), TAttribute<FText>(), TAttribute<FText>(),
									UAnimNextAssetFindReplaceVariables::GetIconFromSearchScope(ESearchScope::Workspace)));
							
								InSubmenu->AddMenuEntry(NAME_None, FToolMenuEntry::InitMenuEntryWithCommandList(FVariablesOutlinerCommands::Get().FindReferencesInAsset, MenuContext->WeakCommandList.Pin(), TAttribute<FText>(), TAttribute<FText>(),
									UAnimNextAssetFindReplaceVariables::GetIconFromSearchScope(ESearchScope::Asset)));
							}),
							FUIAction(FExecuteAction(), FCanExecuteAction(), FGetActionCheckState(), FIsActionButtonVisible::CreateLambda([MenuContext]() -> bool
							{
								if (TSharedPtr<UE::UAF::Editor::SVariablesOutliner> SharedOutliner = MenuContext->WeakOutliner.Pin())
								{
									if (const FVariablesOutlinerMode* Mode = static_cast<const FVariablesOutlinerMode*>(SharedOutliner->GetMode()))
									{
										return Mode->CanFindReferences();
									}
								}

								return false;
							})),
							EUserInterfaceActionType::Button,
							false,
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search"))
						);

					VariablesSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Rename, MenuContext->WeakCommandList.Pin());
					VariablesSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Delete, MenuContext->WeakCommandList.Pin());
					VariablesSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Duplicate, MenuContext->WeakCommandList.Pin());
					
					VariablesSection.AddMenuEntryWithCommandList(FGenericCommands::Get().Copy, MenuContext->WeakCommandList.Pin());

					if (NumSelectedVariables == 1)
					{
						if (const UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(MenuContext->WeakEntries[0].Get()))
						{
							VariablesSection.AddMenuEntryWithCommandList(FVariablesOutlinerCommands::Get().ToggleVariableExport, MenuContext->WeakCommandList.Pin(), VariableEntry->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Private ? LOCTEXT("MakePublicMenuLabel", "Expose as Public") : LOCTEXT("MakePrivateMenuLabel", "Make Private"), TAttribute<FText>(), FSlateIcon(FAppStyle::Get().GetStyleSetName(), VariableEntry->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Private ? "Level.VisibleIcon16x" : "Level.NotVisibleHighlightIcon16x"));
						}
					}

					AssetSection.AddMenuEntryWithCommandList(FVariablesOutlinerCommands::Get().CreateSharedVariablesAssets, MenuContext->WeakCommandList.Pin(), TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FUAFStyle::Get().GetStyleSetName(), "ClassIcon.AnimNextSharedVariables"));
				}

				if (TSharedPtr<SVariablesOutliner> Outliner = MenuContext->WeakOutliner.Pin())
				{
					Outliner->AddSourceControlMenuOptions(InMenu);
				}
			}));
		}
	}

	{
		UAnimNextVariableItemMenuContext* MenuContext = NewObject<UAnimNextVariableItemMenuContext>();
		MenuContext->WeakWorkspaceEditor = WeakWorkspaceEditor;
		MenuContext->WeakOutliner = StaticCastSharedRef<SVariablesOutliner>(SceneOutliner->AsShared());
		MenuContext->WeakCommandList = CommandList;
		TArray<FSceneOutlinerTreeItemPtr> SelectedItems = GetOutliner()->GetSelectedItems();
		for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
		{
			if (const FVariablesOutlinerAssetItem* AssetItem = Item->CastTo<FVariablesOutlinerAssetItem>())
			{
				UAnimNextRigVMAsset* Asset = AssetItem->SoftAsset.Get();
				if(Asset == nullptr)
				{
					continue;
				}

				UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
				if(EditorData == nullptr)
				{
					continue;
				}
				
				MenuContext->WeakEditorDatas.Add(EditorData);
			}
			else if (const FVariablesOutlinerEntryItem* EntryItem = Item->CastTo<FVariablesOutlinerEntryItem>())
			{
				UAnimNextRigVMAssetEntry* Entry = EntryItem->WeakEntry.Get();
				if(Entry == nullptr)
				{
					continue;
				}

				MenuContext->WeakEntries.Add(Entry);
			}
		}

		FToolMenuContext Context;
		Context.AddObject(MenuContext);
		WeakWorkspaceEditor.Pin()->InitToolMenuContext(Context);
		return UToolMenus::Get()->GenerateWidget(MenuName, Context);
	}
}

FReply FVariablesOutlinerMode::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void FVariablesOutlinerMode::OnItemClicked(FSceneOutlinerTreeItemPtr Item)
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	HandleItemSelection(Selection);
}

void FVariablesOutlinerMode::HandleItemSelection(const FSceneOutlinerItemSelection& Selection) const
{
	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		TArray<FSceneOutlinerTreeItemPtr> SelectedItems;
		Selection.Get(SelectedItems);
		TArray<UObject*> EntriesToShow;
		EntriesToShow.Reserve(SelectedItems.Num());
		for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
		{
			if (FVariablesOutlinerEntryItem* VariablesItem = Item->CastTo<FVariablesOutlinerEntryItem>())
			{
				if(UAnimNextVariableEntry* VariableEntry = VariablesItem->WeakEntry.Get())
				{
					EntriesToShow.Add(VariableEntry);
				}
			}
			else if(const FVariablesOutlinerStructSharedVariablesItem* SharedVariablesItem = Item->CastTo<FVariablesOutlinerStructSharedVariablesItem>())
			{
				if(const UAnimNextSharedVariablesEntry* SharedVariablesEntry = SharedVariablesItem->WeakEntry.Get())
				{
					EntriesToShow.Add(const_cast<UAnimNextSharedVariablesEntry*>(SharedVariablesEntry));
				}
			}
		}

		WorkspaceEditor->SetDetailsObjects(EntriesToShow);
	}
}

void FVariablesOutlinerMode::OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection)
{	
	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		SharedWorkspaceEditor->SetGlobalSelection(SceneOutliner->AsShared(), UE::Workspace::FOnClearGlobalSelection::CreateRaw(this, &FVariablesOutlinerMode::ResetOutlinerSelection));
	}
	
	HandleItemSelection(Selection);
}

bool FVariablesOutlinerMode::CanRenameItem(const ISceneOutlinerTreeItem& Item) const
{
	if (const FVariablesOutlinerEntryItem* EntryItem = Item.CastTo<FVariablesOutlinerEntryItem>())
	{
		return !EntryItem->HasStructOwner();
	}
	else if (Item.IsA<FVariablesOutlinerAssetItem>())
	{
		return true;
	}
	else if (Item.IsA<FVariablesOutlinerCategoryItem>())
	{
		return true;
	}
	
	return false;
}

void FVariablesOutlinerMode::BindCommands(const TSharedRef<FUICommandList>& OutCommandList)
{
	CommandList->MapAction( 
		FGenericCommands::Get().Rename, 
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Rename),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanRename),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanRename)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Delete),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanDelete),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanDelete)	
		);

	// [TODO] implement copy/paste for variables
	CommandList->MapAction(
		FGenericCommands::Get().Copy, 
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Copy),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanCopy),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanCopy)
	);

	CommandList->MapAction( 
		FGenericCommands::Get().Paste, 
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Paste),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanPaste),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanPaste)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::Duplicate),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanDuplicate),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanDuplicate)
	);

	CommandList->MapAction(
		FVariablesOutlinerCommands::Get().FindReferences,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::FindReferences, ESearchScope::Global),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanFindReferences),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::IsFindReferencesVisible, ESearchScope::Global)
	);
	
	CommandList->MapAction(
		FVariablesOutlinerCommands::Get().FindReferencesInWorkspace,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::FindReferences, ESearchScope::Workspace),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanFindReferences),
		FIsActionChecked(),
	FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::IsFindReferencesVisible, ESearchScope::Workspace)
	);
	
	CommandList->MapAction(
		FVariablesOutlinerCommands::Get().FindReferencesInAsset,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::FindReferences, ESearchScope::Asset),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanFindReferences),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::IsFindReferencesVisible, ESearchScope::Asset)
	);

	CommandList->MapAction(
		FVariablesOutlinerCommands::Get().SaveAsset,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::SaveAsset),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanSaveAsset),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::IsAsset)
		);
	
	CommandList->MapAction(
		FVariablesOutlinerCommands::Get().ToggleVariableExport,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::ToggleVariableExport),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanToggleVariableExport),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanToggleVariableExport)
		);

	CommandList->MapAction(		
		FVariablesOutlinerCommands::Get().CreateSharedVariablesAssets,
		FExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CreateSharedVariablesAssets),
		FCanExecuteAction::CreateRaw(this, &FVariablesOutlinerMode::CanCreateSharedVariablesAssets),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FVariablesOutlinerMode::CanCreateSharedVariablesAssets)
	);
}

TUniquePtr<ISceneOutlinerHierarchy> FVariablesOutlinerMode::CreateHierarchy()
{
	return MakeUnique<FVariablesOutlinerHierarchy>(this);
}

void FVariablesOutlinerMode::ResetOutlinerSelection() const
{
	SceneOutliner->ClearSelection();
}

SVariablesOutliner* FVariablesOutlinerMode::GetOutliner() const
{
	return static_cast<SVariablesOutliner*>(SceneOutliner);
}

void FVariablesOutlinerMode::Rename() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr ItemToRename = Selection.SelectedItems[0].Pin();

		if (ItemToRename.IsValid() && CanRenameItem(*ItemToRename) && ItemToRename->CanInteract())
		{
			SceneOutliner->SetPendingRenameItem(ItemToRename);
			SceneOutliner->ScrollItemIntoView(ItemToRename);
		}
	}
}

bool FVariablesOutlinerMode::CanRename() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr ItemToRename = Selection.SelectedItems[0].Pin();
		return ItemToRename.IsValid() && CanRenameItem(*ItemToRename) && ItemToRename->CanInteract();
	}
	return false;
}

void FVariablesOutlinerMode::Delete() const
{
	int32 NumEntries = 0;
	TMap<UAnimNextRigVMAssetEditorData*, TArray<UAnimNextRigVMAssetEntry*>> EntriesToDeletePerAsset;
	TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SceneOutliner->GetSelectedItems();
	for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		if (const FVariablesOutlinerEntryItem* VariablesItem = Item->CastTo<FVariablesOutlinerEntryItem>())
		{
			if (!VariablesItem->HasStructOwner())
			{
				UAnimNextVariableEntry* VariableEntry = VariablesItem->WeakEntry.Get();
				const UAnimNextSharedVariablesEntry* SharedVariablesEntry = VariablesItem->WeakSharedVariablesEntry.Get();
				if(VariableEntry == nullptr || SharedVariablesEntry != nullptr)	// Cant delete variables in other data interfaces
				{
					continue;
				}

				UAnimNextRigVMAssetEditorData* EditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
				if(EditorData == nullptr)
				{
					continue;
				}

				TArray<UAnimNextRigVMAssetEntry*>& EntriesToDelete = EntriesToDeletePerAsset.FindOrAdd(EditorData);
				EntriesToDelete.Add(VariableEntry);
				NumEntries++;
			}
		}
		else if (const FVariablesOutlinerStructSharedVariablesItem* SharedVariablesItem = Item->CastTo<FVariablesOutlinerStructSharedVariablesItem>())
		{
			UAnimNextSharedVariablesEntry* SharedVariablesEntry = SharedVariablesItem->WeakEntry.Get();
			if(SharedVariablesEntry == nullptr)
			{
				continue;
			}

			UAnimNextRigVMAssetEditorData* EditorData = SharedVariablesEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
			if(EditorData == nullptr)
			{
				continue;
			}

			TArray<UAnimNextRigVMAssetEntry*>& EntriesToDelete = EntriesToDeletePerAsset.FindOrAdd(EditorData);
			EntriesToDelete.Add(SharedVariablesEntry);
			NumEntries++;
		}
	}

	if(NumEntries > 0)
	{
		// [TODO] prompt user when deletion impacts assets not part of the current workspace (local context), as these have a hidden (read unknown) impact
		FScopedTransaction Transaction(FText::Format(LOCTEXT("DeleteVariablesFormat", "Delete {0}|plural(one=variable, other=variables)"), NumEntries));
		for(const TPair<UAnimNextRigVMAssetEditorData*, TArray<UAnimNextRigVMAssetEntry*>>& EntriesPair : EntriesToDeletePerAsset)
		{
			for (UAnimNextRigVMAssetEntry* Entry : EntriesPair.Value)
			{
				UncookedOnly::FUtils::DeleteVariable(CastChecked<UAnimNextVariableEntry>(Entry), true, true);
			}
		}
	}
}

bool FVariablesOutlinerMode::CanDelete() const
{
	TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SceneOutliner->GetSelectedItems();

	bool bCanDelete = false;
	for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		if (const FVariablesOutlinerEntryItem* VariablesItem = Item->CastTo<FVariablesOutlinerEntryItem>())
		{
			if (!VariablesItem->HasStructOwner())
			{
				bCanDelete = true;
			}
		}
	}
	
	return bCanDelete;
}

void FVariablesOutlinerMode::Copy() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	Selection.ForEachItem([](const FSceneOutlinerTreeItemPtr& SelectedItem)
	{
		
	});
}

bool FVariablesOutlinerMode::CanCopy() const
{
	// [TODO] implement copy behaviour
	return false;
}

void FVariablesOutlinerMode::Paste() const
{
}

bool FVariablesOutlinerMode::CanPaste() const
{
	// [TODO] implement paste behaviour
	return false;
}

void FVariablesOutlinerMode::ToggleVariableExport() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num())
	{		
		FScopedTransaction Transaction(LOCTEXT("SetAccessSpecifierTransaction", "Set Access Specifier"));
		Selection.ForEachItem<FVariablesOutlinerEntryItem>([](const FVariablesOutlinerEntryItem& EntryItem)
		{			
			if (IAnimNextRigVMExportInterface* Export = Cast<IAnimNextRigVMExportInterface>(EntryItem.WeakEntry.Get()))
			{
				if (UAnimNextRigVMAsset* OuterAsset = EntryItem.WeakEntry.Get()->GetTypedOuter<UAnimNextRigVMAsset>())
				{
					if (OuterAsset->GetClass() && OuterAsset->GetClass() != UAnimNextSharedVariables::StaticClass())
					{
						Export->SetExportAccessSpecifier(Export->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public ? EAnimNextExportAccessSpecifier::Private : EAnimNextExportAccessSpecifier::Public);
					}
				}
			}
		});
	}
}

bool FVariablesOutlinerMode::CanToggleVariableExport() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num())
	{
		bool bCanToggle = false;
		
		FScopedTransaction Transaction(LOCTEXT("SetAccessSpecifierTransaction", "Set Access Specifier"));
		Selection.ForEachItem<FVariablesOutlinerEntryItem>([&bCanToggle](const FVariablesOutlinerEntryItem& EntryItem)
		{
			if (UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(EntryItem.WeakEntry.Get()))
			{
				if (UAnimNextRigVMAsset* OuterAsset = VariableEntry->GetTypedOuter<UAnimNextRigVMAsset>())
				{
					// Disable toggling public/private for pure UAnimNextSharedVariables objects				
					if (OuterAsset->GetClass() && OuterAsset->GetClass() != UAnimNextSharedVariables::StaticClass())
					{
						bCanToggle = true;
					}
				}
			}
		});

		return bCanToggle;
	}

	return false;
}

void FVariablesOutlinerMode::Duplicate() const
{	
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num())
	{		
		FScopedTransaction Transaction(FText::Format(LOCTEXT("DuplicateVariableFormat", "Duplicate {0}|plural(one=variable, other=variables)"), Selection.Num()));

		Selection.ForEachItem<FVariablesOutlinerEntryItem>([](const FVariablesOutlinerEntryItem& EntryItem)
		{
			if (const UAnimNextVariableEntry* VariableEntry = EntryItem.WeakEntry.Get())
			{
				if (UAnimNextRigVMAssetEditorData* EditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>())
				{	
					const FName VariableName = UncookedOnly::FUtils::GetValidVariableName(EditorData, VariableEntry->GetVariableName());		
					EditorData->AddVariable(VariableName, VariableEntry->GetType());
				}
			}
		});
	}
}	

bool FVariablesOutlinerMode::CanDuplicate() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();

	bool bCanDuplicate = true;
	Selection.ForEachItem([&bCanDuplicate](const FSceneOutlinerTreeItemPtr& Item)
	{
		const FVariablesOutlinerEntryItem* EntryItem = Item->CastTo<FVariablesOutlinerEntryItem>();
		if (EntryItem == nullptr || EntryItem->HasStructOwner())
		{
			bCanDuplicate = false;
		}
	});
	
	return bCanDuplicate;
}

void FVariablesOutlinerMode::FindReferences(ESearchScope InSearchScope) const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr ItemToFind = Selection.SelectedItems[0].Pin();
		if (const FVariablesOutlinerEntryItem* EntryItem = ItemToFind->CastTo<FVariablesOutlinerEntryItem>())
		{
			if (TSharedPtr<SDockTab> FindAndReplaceTab = WeakWorkspaceEditor.Pin()->GetTabManager()->TryInvokeTab(FTabId(UE::UAF::Editor::FindAndReplaceTabName)))
			{
				TSharedRef<IAnimAssetFindReplace> FindAndReplace = StaticCastSharedRef<IAnimAssetFindReplace>(FindAndReplaceTab->GetContent());
				FindAndReplace->SetCurrentProcessor(UAnimNextAssetFindReplaceVariables::StaticClass());
				UAnimNextAssetFindReplaceVariables* AnimNextFindAndReplaceProcessor = FindAndReplace->GetProcessor<UAnimNextAssetFindReplaceVariables>();

				if (TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
				{
					AnimNextFindAndReplaceProcessor->SetWorkspaceEditor(SharedWorkspaceEditor.ToSharedRef());
				}
				
				UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get();
				AnimNextFindAndReplaceProcessor->SetSearchScope(InSearchScope);		
				AnimNextFindAndReplaceProcessor->SetFindReferenceFromEntry(VariableEntry);				
			}
		}		
	}
}

bool FVariablesOutlinerMode::CanFindReferences() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	if (Selection.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr ItemToFind = Selection.SelectedItems[0].Pin();
		if (const FVariablesOutlinerEntryItem* EntryItem = ItemToFind->CastTo<FVariablesOutlinerEntryItem>())
		{
			return true;
		}
	}
	
	return false;
}

bool FVariablesOutlinerMode::IsFindReferencesVisible(ESearchScope InSearchScope) const
{
	return CanFindReferences();
}

void FVariablesOutlinerMode::PopulateNewVariableToolMenuEntries(UToolMenu* InMenu, bool bAddSeparator)
{
	const UAnimNextVariableItemMenuContext* MenuContext = InMenu->FindContext<UAnimNextVariableItemMenuContext>();
	if(MenuContext == nullptr)
	{
		return;
	}

	TMap<TWeakObjectPtr<UAnimNextRigVMAssetEditorData>, FString> AssetsAndCategoriesToAddTo;
	if (MenuContext->WeakEditorDatas.Num())
	{
		if (UAnimNextRigVMAssetEditorData* EditorData = MenuContext->WeakEditorDatas[0].Get())
		{
			AssetsAndCategoriesToAddTo.Add(EditorData, TEXT(""));
		}
	}
	else
	{
		TSharedPtr<SVariablesOutliner> SharedOutliner = MenuContext->WeakOutliner.Pin();
		if (SharedOutliner.IsValid())
		{
			TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SharedOutliner->GetSelectedItems();
		
			for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
			{
				const FVariablesOutlinerAssetItem* AssetItem = Item->CastTo<FVariablesOutlinerAssetItem>();
				const FVariablesOutlinerCategoryItem* CategoryItem = Item->CastTo<FVariablesOutlinerCategoryItem>();
				if (AssetItem == nullptr && CategoryItem == nullptr)
				{
					continue;
				}

				UAnimNextRigVMAsset* Asset = nullptr;
				FString Category;
		
				if (CategoryItem)
				{
					Asset = CategoryItem->WeakOwner.Get();
					Category = CategoryItem->GetDisplayString();
				}
				else
				{
					Asset = AssetItem->SoftAsset.Get();
				}

				if(Asset == nullptr)
				{
					continue;
				}

				UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
				if(EditorData == nullptr)
				{
					continue;
				}

				AssetsAndCategoriesToAddTo.Add(EditorData, Category);
			}
		}
	}
	
	if (AssetsAndCategoriesToAddTo.Num())
	{
		constexpr bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, TSharedPtr<FUICommandList>());

		FToolMenuSection& Section = InMenu->AddSection("Variables", LOCTEXT("VariablesSectionLabel", "Variables"));
		Section.AddMenuEntry("AddNewVariable", LOCTEXT("VariableLabel", "Add Variable"), LOCTEXT("VariableTooltip", "Adds a new Variable"),FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
			FUIAction(FExecuteAction::CreateLambda([WeakOutliner = MenuContext->WeakOutliner, AssetsAndCategoriesToAddTo]()
			{
				for (auto& Pair : AssetsAndCategoriesToAddTo)
				{
					if (UAnimNextRigVMAssetEditorData* EditorData = Pair.Key.Get())
                    {
                        UAnimNextVariableSettings* Settings = GetMutableDefault<UAnimNextVariableSettings>();
                        FScopedTransaction Transaction(LOCTEXT("AddVariable", "Add variable"));
                        
                        const FName VariableName = UncookedOnly::FUtils::GetValidVariableName(EditorData, Settings->GetLastVariableName());		

						if (UAnimNextVariableEntry* VariableEntry = EditorData->AddVariable(VariableName, Settings->GetLastVariableType()))
						{
							VariableEntry->SetVariableCategory(Pair.Value);
							Settings->SetLastVariableName(VariableName);

							// Prompt user to rename added variable in outliner
							TSharedPtr<SVariablesOutliner> SharedOutliner = WeakOutliner.Pin();
							if (SharedOutliner.IsValid())
							{
								// FVariablesOutlinerEntryItem::SharedVariablesEntry is only populated for Struct based SharedVariables
								const UAnimNextSharedVariablesEntry* SharedVariablesEntry = nullptr;
								FSceneOutlinerTreeItemID EntryID = HashCombine(GetTypeHash(VariableEntry), GetTypeHash(SharedVariablesEntry));
								SharedOutliner->OnItemAdded(EntryID, SceneOutliner::ENewItemAction::Select | SceneOutliner::ENewItemAction::Rename);
							}
						}
                    }
				}
			}))
		);

		if (AssetsAndCategoriesToAddTo.Num() == 1)
		{
			Section.AddSubMenu("AddSharedVariables", LOCTEXT("SharedVariableLabel", "Add Shared Variables"), LOCTEXT("SharedVariableTooltip", "Includes added SharedVariables"),
			FNewMenuDelegate::CreateLambda([AssetsAndCategoriesToAddTo](FMenuBuilder& SubMenuBuilder)
				{
					TArray<FAssetData> ExistingSharedVariableReferences;
					for (auto& Pair : AssetsAndCategoriesToAddTo)
					{
						if (UAnimNextRigVMAssetEditorData* EditorData = Pair.Key.Get())
						{
							EditorData->ForEachEntryOfType<UAnimNextSharedVariablesEntry>([&ExistingSharedVariableReferences](const UAnimNextSharedVariablesEntry* SharedVariablesEntry) -> bool
							{
								if (SharedVariablesEntry->GetType() == EAnimNextSharedVariablesType::Asset)
								{
									ExistingSharedVariableReferences.Add(SharedVariablesEntry->GetAsset());
								}
								return true;
							});
						}
					}

					FAssetPickerConfig AssetPickerConfig;
					AssetPickerConfig.Filter.bRecursiveClasses = true;
					AssetPickerConfig.Filter.ClassPaths.Add(UAnimNextSharedVariables::StaticClass()->GetClassPathName());
					AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
					AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([AssetsAndCategoriesToAddTo](const FAssetData& InAssetData)
					{
						FSlateApplication::Get().DismissAllMenus();
						if(UAnimNextSharedVariables* SharedVariables = Cast<UAnimNextSharedVariables>(InAssetData.GetAsset()))
						{							
							FScopedTransaction Transaction(LOCTEXT("AddSharedVariable", "Add Shared Variables"));
							for (auto& Pair : AssetsAndCategoriesToAddTo)
							{
								if (UAnimNextRigVMAssetEditorData* EditorData = Pair.Key.Get())
								{
									EditorData->AddSharedVariables(SharedVariables);
								}
							}
						}
					});
					AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([ExistingSharedVariableReferences](const FAssetData& InAssetData)
					{
						FAnimNextAssetRegistryExports Exports;
						UncookedOnly::FUtils::GetExportedVariablesForAsset(InAssetData, Exports);
						if(Exports.Exports.Num() == 0 && InAssetData.GetClass(EResolveClass::Yes) != UAnimNextSharedVariables::StaticClass())
						{
							return true;
						}

						// Filter out already referenced assets
						return ExistingSharedVariableReferences.ContainsByPredicate([InAssetData](const FAssetData& ExistingAssetData)
						{
							return ExistingAssetData == InAssetData;
						});
					});

					FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
					TSharedPtr<SWidget> Widget = SNew(SBox)
					.WidthOverride(300.0f)
					.HeightOverride(400.0f)
					[
						ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
					];

					SubMenuBuilder.AddWidget(Widget.ToSharedRef(), FText::GetEmpty(), true);
				}),
				false,
				FSlateIcon(FUAFStyle::Get().GetStyleSetName(), "ClassIcon.AnimNextSharedVariables")
			);


			Section.AddSubMenu("AddNativeSharedVariables",LOCTEXT("NativeSharedVariableLabel", "Add Native Shared Variables"), LOCTEXT("NativeSharedVariableTooltip", "Includes added Native SharedVariables"),
		FNewMenuDelegate::CreateLambda([AssetsAndCategoriesToAddTo](FMenuBuilder& SubMenuBuilder)
				{

					TArray<FSoftObjectPath> ExistingSharedVariableStructPaths;
					TArray<FAssetData> ExistingSharedVariableReferences;
					for (auto& Pair : AssetsAndCategoriesToAddTo)
					{
						if (UAnimNextRigVMAssetEditorData* EditorData = Pair.Key.Get())
						{
							EditorData->ForEachEntryOfType<UAnimNextSharedVariablesEntry>([&ExistingSharedVariableStructPaths](const UAnimNextSharedVariablesEntry* SharedVariablesEntry) -> bool
							{
								if (SharedVariablesEntry->GetType() == EAnimNextSharedVariablesType::Struct)
								{
									ExistingSharedVariableStructPaths.Add(SharedVariablesEntry->GetObjectPath());
								}
								return true;
							});
						}
					}
			
					class FStructFilter : public IStructViewerFilter
					{
					public:

						FStructFilter(const TArray<FSoftObjectPath>& InFilteredStructPaths) : FilteredStructPaths(InFilteredStructPaths) {}
						
						virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
						{
							if (InStruct->IsA<UUserDefinedStruct>())
							{
								return false;
							}

							if (!InStruct->HasMetaData(TEXT("BlueprintType")) || InStruct->HasMetaData(TEXT("Hidden")))
							{
								return false;
							}

							if (FilteredStructPaths.Contains(InStruct))
							{
								return false;
							}

							return true;
						}

						virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FSoftObjectPath& InStructPath, TSharedRef<class FStructViewerFilterFuncs> InFilterFuncs) override
						{
							return false;
						};

						TArray<FSoftObjectPath> FilteredStructPaths;
					};

					FStructViewerModule& StructViewerModule = FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer");

					FStructViewerInitializationOptions Options;
					Options.Mode = EStructViewerMode::StructPicker;
					Options.StructFilter = MakeShared<FStructFilter>(ExistingSharedVariableStructPaths);

					TSharedPtr<SWidget> Widget = SNew(SBox)
						.WidthOverride(300.0f)
						.HeightOverride(400.0f)
						[
							StructViewerModule.CreateStructViewer(Options, FOnStructPicked::CreateLambda([AssetsAndCategoriesToAddTo](const UScriptStruct* InStruct)
							{
								FSlateApplication::Get().DismissAllMenus();

								if(InStruct)
								{
									FScopedTransaction Transaction(LOCTEXT("AddNativeSharedVariable", "Add Native Shared Variables"));
									for (auto& Pair : AssetsAndCategoriesToAddTo)
									{
										if (UAnimNextRigVMAssetEditorData* EditorData = Pair.Key.Get())
										{									
											EditorData->AddSharedVariablesStruct(InStruct);
										}
									}
								}
							}))
						];

					SubMenuBuilder.AddWidget(Widget.ToSharedRef(), FText::GetEmpty(), true);
				}),
				false,
				FSlateIconFinder::FindIconForClass(UUserDefinedStruct::StaticClass())
			);
		}

		if (bAddSeparator)
		{
			Section.AddSeparator("PostAddVariableSeparator");
		}
	}
}

void FVariablesOutlinerMode::SetHighlightedItem(FSceneOutlinerTreeItemID InID) const
{
	if (SVariablesOutliner* Outliner = static_cast<SVariablesOutliner*>(SceneOutliner))
	{
		if (FSceneOutlinerTreeItemPtr ItemPtr = Outliner->GetTreeItem(InID))
		{
			Outliner->FrameItem(InID);
			Outliner->SetHighlightedItem(ItemPtr);
		}
	}
}

void FVariablesOutlinerMode::ClearHighlightedItem(FSceneOutlinerTreeItemID InID) const
{
	if (SVariablesOutliner* Outliner = static_cast<SVariablesOutliner*>(SceneOutliner))
	{
		if (FSceneOutlinerTreeItemPtr ItemPtr = Outliner->GetTreeItem(InID))
		{
			Outliner->ClearHighlightedItem(ItemPtr);
		}
	}
}

void FVariablesOutlinerMode::SaveAsset() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	Selection.ForEachItem<FVariablesOutlinerAssetItem>([](const FVariablesOutlinerAssetItem& AssetItem)
	{
		if (const UAnimNextRigVMAsset* Asset = AssetItem.SoftAsset.Get())
		{
			FEditorFileUtils::PromptForCheckoutAndSave({Asset->GetPackage()}, false, /*bPromptToSave=*/ false);
		}
	});	
}

bool FVariablesOutlinerMode::CanSaveAsset() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();	
	return Selection.SelectedItems.ContainsByPredicate([](const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem)
	{
		if (WeakItem.IsValid())
		{
			if (const FVariablesOutlinerAssetItem* AssetItem = WeakItem.Pin()->CastTo<FVariablesOutlinerAssetItem>())
			{
				if (const UAnimNextRigVMAsset* Asset = AssetItem->SoftAsset.Get())
				{
					return Asset->GetPackage()->IsDirty();
				}
			}
		}

		return false;
	});
}

bool FVariablesOutlinerMode::IsAsset() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();	
	return Selection.SelectedItems.ContainsByPredicate([](const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem) { return WeakItem.IsValid() && WeakItem.Pin()->IsA<FVariablesOutlinerAssetItem>(); });	
}

void FVariablesOutlinerMode::CreateSharedVariablesAssets() const
{
	ensure(CanCreateSharedVariablesAssets());
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();

	TArray<UAnimNextVariableEntry*> EntriesToMove;
	Selection.ForEachItem<FVariablesOutlinerEntryItem>([&EntriesToMove](const FVariablesOutlinerEntryItem& VariableItem)
	{
		if (UAnimNextVariableEntry* VariableEntry = VariableItem.WeakEntry.Get())
		{
			EntriesToMove.Add(VariableEntry);
		}
	});

	if (EntriesToMove.Num())
	{
		// Prompt user for asset path
		FString SaveObjectPath;
		{			
			FSaveAssetDialogConfig SaveAssetDialogConfig;
			{
				// Populate default path to match currently focussed assets
				TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin();
				if (SharedWorkspaceEditor.IsValid())
				{
					const Workspace::FWorkspaceDocument& Document =SharedWorkspaceEditor->GetFocusedWorkspaceDocument();
					if (UObject* FocussedObject = Document.GetObject())
					{
						SaveAssetDialogConfig.DefaultPath = FPaths::GetPath(FocussedObject->GetPackage()->GetPathName());
					}
				}
				
				SaveAssetDialogConfig.DefaultAssetName = TEXT("NewSharedVariables");
				SaveAssetDialogConfig.AssetClassNames.Add(UAnimNextSharedVariables::StaticClass()->GetClassPathName());
				SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
				SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveLiveLinkPresetDialogTitle", "Add new Shared Variables Asset");
			}

			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
		}

		if (!SaveObjectPath.IsEmpty())
		{
			const FString PackagePath = FPackageName::ObjectPathToPackageName(SaveObjectPath);
			const FString AssetName = FPaths::GetBaseFilename(PackagePath, true);
			
			const FText FormattedMessage = FText::Format(LOCTEXT("CreateSharedVariablesAsset", "Creating new Shared Variables Asset {0}"), FText::FromString(AssetName));
			FScopedTransaction Transaction(FormattedMessage);			
			
			UPackage* Package = CreatePackage(*PackagePath);
			Package->Modify();
				
			UAnimNextSharedVariablesFactory* Factory = NewObject<UAnimNextSharedVariablesFactory>(GetTransientPackage());
			UAnimNextSharedVariables* NewSharedVariables = CastChecked<UAnimNextSharedVariables>(Factory->FactoryCreateNew(UAnimNextSharedVariables::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, nullptr, NAME_None));
			check(NewSharedVariables);
			
			TArray<UAnimNextRigVMAssetEditorData*> UniqueOuterEditorDatas;
			for (UAnimNextVariableEntry* Entry : EntriesToMove)
			{
				// Force variable to be public as its being moved to SharedVariables asset
				Entry->SetExportAccessSpecifier(EAnimNextExportAccessSpecifier::Public);

				UniqueOuterEditorDatas.AddUnique(Entry->GetTypedOuter<UAnimNextRigVMAssetEditorData>());
				UncookedOnly::FUtils::MoveVariableToAsset(Entry, NewSharedVariables);
			}

			for (UAnimNextRigVMAssetEditorData* OuterEditorData : UniqueOuterEditorDatas)
			{
				OuterEditorData->AddSharedVariables(NewSharedVariables);
			}
			
			// mark asset dirty 
			FAssetRegistryModule::AssetCreated(NewSharedVariables);
			NewSharedVariables->MarkPackageDirty();
		}
	}
}

bool FVariablesOutlinerMode::CanCreateSharedVariablesAssets() const
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();
	const bool bSelectionOnlyContainsVariables = Selection.Num() == Selection.Num<FVariablesOutlinerEntryItem>();

	// Prevent moving from an assets which would leave it empty (removing all variables)
	TMap<const UAnimNextRigVMAssetEditorData*, int32> EditorDataToNumRemainingVariables;
	Selection.ForEachItem<FVariablesOutlinerEntryItem>([&EditorDataToNumRemainingVariables](const FVariablesOutlinerEntryItem& Item)
	{
		if (const UAnimNextVariableEntry* VariableEntry = Item.WeakEntry.Get())
		{
			if (const UAnimNextRigVMAssetEditorData* EditorData = CastChecked<UAnimNextRigVMAssetEditorData>(VariableEntry->GetOuter()))
			{
				if (int32* ExistingEntryValue = EditorDataToNumRemainingVariables.Find(EditorData))
				{
					--(*ExistingEntryValue);
				}
				else
				{
					int32 NumVariables = 0;
					EditorData->ForEachEntryOfType<UAnimNextVariableEntry>([&NumVariables](const UAnimNextVariableEntry* Entry)
					{
						++NumVariables;
						return true;
					});

					EditorDataToNumRemainingVariables.Add(EditorData, NumVariables - 1);
				}
			}
		}

		return true;
	});

	TArray<int32> NumRemainVariablesArray;
	
	EditorDataToNumRemainingVariables.GenerateValueArray(NumRemainVariablesArray);
	const bool bMoveWillLeaveEmptyAsset = NumRemainVariablesArray.Contains(0);
	
	return bSelectionOnlyContainsVariables && !bMoveWillLeaveEmptyAsset;
}

TSharedPtr<FDragDropOperation> FVariablesOutlinerMode::CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const
{
	if(InTreeItems.Num() != 1)
	{
		return nullptr;
	}

	const FSceneOutlinerTreeItemPtr& SelectedItem = InTreeItems[0];
	if (FVariablesOutlinerEntryItem* VariableItem = SelectedItem->CastTo<FVariablesOutlinerEntryItem>())
	{
		if(UAnimNextVariableEntry* Entry = Cast<UAnimNextVariableEntry>(VariableItem->WeakEntry.Get()))
		{
			const UAnimNextSharedVariables* Asset = nullptr;
			if (VariableItem->WeakSharedVariablesEntry.Get())
			{
				Asset = VariableItem->WeakSharedVariablesEntry->GetAsset();
			}
			else
			{
				Asset = Entry->GetTypedOuter<UAnimNextSharedVariables>();
			}
		
			if (Asset)
			{
				TSharedPtr<FAnimNextSchemaAction_Variable> Action = MakeShared<FAnimNextSchemaAction_Variable>(Entry->GetVariableName(), Asset, Entry->GetType(), FAnimNextSchemaAction_Variable::EVariableAccessorChoice::Deferred);

				return FVariableDragDropOp::New(Action, StaticCastSharedRef<FVariablesOutlinerEntryItem>(VariableItem->AsShared()));
			}
		}
		else if (const FProperty* Property = VariableItem->PropertyPath.Get())
		{
			if (const UScriptStruct* Struct = Property->GetOwner<UScriptStruct>())
			{
				TSharedPtr<FAnimNextSchemaAction_Variable> Action = MakeShared<FAnimNextSchemaAction_Variable>(Property->GetFName(), Struct, FAnimNextParamType::FromProperty(Property), FAnimNextSchemaAction_Variable::EVariableAccessorChoice::Deferred);
				
				TSharedPtr<FVariableDragDropOp> Op = FVariableDragDropOp::New(Action, StaticCastSharedRef<FVariablesOutlinerEntryItem>(VariableItem->AsShared()));
				return Op;
			}
		}
	}
	else if (FVariablesOutlinerCategoryItem* CategoryItem = SelectedItem->CastTo<FVariablesOutlinerCategoryItem>())
	{
		TSharedPtr<FCategoryDragDropOp> Op = FCategoryDragDropOp::New(StaticCastSharedRef<FVariablesOutlinerCategoryItem>(CategoryItem->AsShared()));
		return Op;
	}

	return nullptr;

}

bool FVariablesOutlinerMode::ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const
{
	return ISceneOutlinerMode::ParseDragDrop(OutPayload, Operation);
}

FSceneOutlinerDragValidationInfo FVariablesOutlinerMode::ValidateDrop(const ISceneOutlinerTreeItem& DropTarget,
	const FSceneOutlinerDragDropPayload& Payload) const
{
	return ISceneOutlinerMode::ValidateDrop(DropTarget, Payload);
}

void FVariablesOutlinerMode::OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload,
	const FSceneOutlinerDragValidationInfo& ValidationInfo) const
{
	ISceneOutlinerMode::OnDrop(DropTarget, Payload, ValidationInfo);
}

FReply FVariablesOutlinerMode::OnDragOverItem(const FDragDropEvent& Event, const ISceneOutlinerTreeItem& Item) const
{
	return ISceneOutlinerMode::OnDragOverItem(Event, Item);
}

int32 FVariablesOutlinerMode::GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const
{
	if(const FVariablesOutlinerAssetItem* AssetItem = Item.CastTo<FVariablesOutlinerAssetItem>())
	{
		return AssetItem->SortValue;
	}

	if(const FVariablesOutlinerEntryItem* EntryItem = Item.CastTo<FVariablesOutlinerEntryItem>())
	{
		return EntryItem->SortValue;
	}

	if(const FVariablesOutlinerCategoryItem* CategoryItem = Item.CastTo<FVariablesOutlinerCategoryItem>())
	{
		return CategoryItem->SortValue;
	}
	
	return ISceneOutlinerMode::GetTypeSortPriority(Item);
}

void FVariablesOutlinerMode::OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item)
{
	if (Item.IsValid())
	{
		if(FVariablesOutlinerAssetItem* AssetItem = Item->CastTo<FVariablesOutlinerAssetItem>())
		{
			// [TODO] Open asset from Variables Outliner
		}
	}	
	
	ISceneOutlinerMode::OnItemDoubleClick(Item);
}
}

#undef LOCTEXT_NAMESPACE // "FVariablesOutlinerMode"