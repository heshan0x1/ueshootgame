// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "CineAssembly.h"
#include "IDetailsView.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/STreeView.h"

/** An entry in the hierarchy tree view */
struct FHierarchyTreeItem
{
	/** The types of items that can be represented in this tree view */
	enum class EItemType : uint8
	{
		Asset,
		Folder
	};

	/** The type of this tree item */
	EItemType Type;

	/** The relative path of this tree item (possibly containing tokens) */
	FTemplateString Path;

	/** The children of this item in the tree that are Asset types */
	TArray<TSharedPtr<FHierarchyTreeItem>> ChildAssets;

	/** The children of this item in the tree that are Folder types */
	TArray<TSharedPtr<FHierarchyTreeItem>> ChildFolders;
};

/**
 * A widget that wraps details and config steps for setting up a cine assembly asset
 */
class SCineAssemblyConfigPanel : public SCompoundWidget
{
public:
	SCineAssemblyConfigPanel() = default;

	SLATE_BEGIN_ARGS(SCineAssemblyConfigPanel) 
		: _HideSubAssemblies(false)
		{}

		/** If set to true, the SubAssemblies section in the details view will be hidden */
		SLATE_ARGUMENT(bool, HideSubAssemblies)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCineAssembly* InAssembly);

	/** Refreshes the details view and hierarchy tree view */
	void Refresh();

private:
	/** Creates the widget to display for the Overview tab */
	TSharedRef<SWidget> MakeDetailsWidget();

	/** Creates the widget to display for the Hierarchy tab */
	TSharedRef<SWidget> MakeHierarchyWidget();

	/** Creates the widget to display for the Notes tab */
	TSharedRef<SWidget> MakeNotesWidget();

	/** Populate the tree view items from the list of folders and assets specified by the selected schema */
	void PopulateHierarchyTree();

	/** Generates the row widget for an entry in the tree view */
	TSharedRef<ITableRow> OnGenerateTreeRow(TSharedPtr<FHierarchyTreeItem> TreeItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Gets the children of the input tree view item to build additional tree rows */
	void OnGetChildren(TSharedPtr<FHierarchyTreeItem> TreeItem, TArray<TSharedPtr<FHierarchyTreeItem>>& OutNodes);

	/** Returns the tree item whose path matches the input path */
	TSharedPtr<FHierarchyTreeItem> FindItemAtPathRecursive(TSharedPtr<FHierarchyTreeItem> TreeItem, const FString& Path) const;

	/** Recursively expands every item in the tree view */
	void ExpandTreeRecursive(TSharedPtr<FHierarchyTreeItem> TreeItem) const;

	/** Recursively evaluates the tokens in the path of each item in the tree view */
	void EvaluateHierarchyTokensRecursive(TSharedPtr<FHierarchyTreeItem> TreeItem);

	/** Validates the user input text for the assembly name */
	bool ValidateAssemblyName(const FText& InText, FText& OutErrorMessage) const;

	/**
	 * Evaluates the input template string with the naming tokens subsystem, and stores the result in the Resolved text.
	 * This function is throttled to only run at a set frequency, to avoid the potential to constantly query the naming tokens subsystem.
	 */
	void EvaluateTokenString(FTemplateString& StringToEvaluate);

	/** Filter used by the Details View to determine which custom rows to show */
	bool IsCustomRowVisible(FName RowName, FName ParentName);

	/** Get the thumbnail brush for the selected schema */
	const FSlateBrush* GetSchemaThumbnail() const;

private:
	/** Transient object used only by this UI to configure the properties of the new asset that will get created by the Factory */
	UCineAssembly* CineAssemblyToConfigure = nullptr;

	/** Switcher that controls which tab widget is currently visible */
	TSharedPtr<SWidgetSwitcher> TabSwitcher;

	/** Details View displaying the reflected properties of the Cine Assembly being configured */
	TSharedPtr<IDetailsView> DetailsView;

	/** Items source for the tree view */
	TArray<TSharedPtr<FHierarchyTreeItem>> HierarchyTreeItems;

	/** The root item in the tree view */
	TSharedPtr<FHierarchyTreeItem> RootItem;

	/** A read-only tree view of folders and assets that will be created based on the selected schema */
	TSharedPtr<STreeView<TSharedPtr<FHierarchyTreeItem>>> HierarchyTreeView;

	/** The last time the naming tokens were updated */
	FDateTime LastTokenUpdateTime;
};
