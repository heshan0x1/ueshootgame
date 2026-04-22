// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

#include "CineAssembly.h"
#include "MovieSceneSequence.h"
#include "Widgets/Views/STreeView.h"

class UMovieSceneFolder;

/** List of available duplication modes for a subsequence */
enum class ECineAssemblyDuplicationMode : uint8
{
	DuplicateOriginal, // Duplicates the original assembly's subsequence track/section AND duplicates the original subsequence, updating the reference
	MaintainReference, // Duplicates the original assembly's subsequence track/section BUT maintains the reference to the original subsequence
	Remove // Does not duplicate the original assembly's subsequence track/section
};

/** The data required to duplicate a specific subsequence */
struct FSubsequenceDuplicationData
{
	/** The duplication mode to use */
	ECineAssemblyDuplicationMode DuplicationMode = ECineAssemblyDuplicationMode::DuplicateOriginal;

	/** The name to use for the duplicated subsequence (if the mode if DuplicateOriginal) */
	FTemplateString SubsequenceName;

	/** Whether or not the subsequence was defined in the schema of the original subsequence */
	bool bIsSubAssembly = false;
};

/** An item in the subsequence tree view, associating a subsequence with its duplication data and child subsequences */
struct FSequenceTreeItem
{
	/** The subsequence represented by this item in the tree */
	UMovieSceneSequence* Subsequence;

	/** The duplication associated with this subsequence */
	FSubsequenceDuplicationData DuplicationData;

	/** Child subsequence tree items */
	TArray<TSharedPtr<FSequenceTreeItem>> Children;

	/** Parent sequence tree item */
	TSharedPtr<FSequenceTreeItem> Parent;
};

/** Widget row for an item in the subsequence tree view */
class SSubsequenceDuplicationRow : public SMultiColumnTableRow<TSharedPtr<FSequenceTreeItem>>
{
public:

	SLATE_BEGIN_ARGS(SSubsequenceDuplicationRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedPtr<FSequenceTreeItem>& InTreeItem);

	/** Creates the widget for this row for the specified column */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	/** Make the widget for each column */
	TSharedRef<SWidget> MakeOriginalNameWidget();
	TSharedRef<SWidget> MakeDuplicationModeWidget();
	TSharedRef<SWidget> MakeDuplicateNameWidget();

	/** Updates the duplication mode for the input tree item, and potentially its children */
	void SetDuplicationModeRecursive(TSharedPtr<FSequenceTreeItem> InItem, ECineAssemblyDuplicationMode Mode);

	/** Updates the subsequence name associated with this tree row */
	void OnSubsequenceNameCommitted(const FText& InText, ETextCommit::Type InCommitType);

	/** Get the display name of the input duplication mode */
	FText GetDuplicationModeDisplayName(ECineAssemblyDuplicationMode DuplicationMode) const;

private:
	/** The tree view item displayed by this row widget */
	TSharedPtr<FSequenceTreeItem> TreeItem;
};

/** A window to configure how a Cine Assembly asset should be duplicated */
class SDuplicateAssemblyWindow : public SWindow
{
public:
	SDuplicateAssemblyWindow() = default;

	SLATE_BEGIN_ARGS(SDuplicateAssemblyWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCineAssembly* InAssembly);

public:
	static const FName OriginalNameColumn;
	static const FName DuplicationModeColumn;
	static const FName DuplicateNameColumn;

private:
	/** Creates the subsequences panel */
	TSharedRef<SWidget> MakeSubsequencesPanel();

	/** Creates the buttons on the bottom of the window */
	TSharedRef<SWidget> MakeButtonsPanel();

	/** Called when the Duplicate button is pressed, which will execute the duplication steps as defined by the duplication data for each subsequence */
	FReply OnDuplicateClicked();

	/** Called when the Cancel button is pressed, which will close the window without duplicating anything */
	FReply OnCancelClicked();

	/** Generates the row widget in the subsequence tree view for the input tree item */
	TSharedRef<ITableRow> OnGenerateTreeRow(TSharedPtr<FSequenceTreeItem> InTreeItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Adds the children of the input tree item to the OutNodes array */
	void OnGetChildren(TSharedPtr<FSequenceTreeItem> InTreeItem, TArray<TSharedPtr<FSequenceTreeItem>>& OutNodes);

	/** Builds the list of tree items in the subsequence tree by recursively walking the input sequence and finding all subsequences that may need to be duplicated */
	void PopulateSubsequenceTreeRecurisve(UMovieSceneSequence* InSequence, TSharedPtr<FSequenceTreeItem> InTreeItem);

	/** Recursively duplicates any of the subsequences of the input sequence that are set to the DuplicateOriginal duplication mode */
	void DuplicateSubsequencesRecursive(UMovieSceneSequence* InSequence);

	/** Recursively removes any of the subsequences of the input sequence that are set to the Remove duplication mode */
	void RemoveSubsequencesRecursive(UMovieSceneSequence* InSequence);

	/** Recursively removes the set of tracks from the input folders (or child folders) */
	void RemoveTracksFromFoldersRecursive(TArrayView<UMovieSceneFolder* const> Folders, const TArray<UMovieSceneTrack*>& TracksToRemove);

	/** Converts the subsequence tree into a map of subsequences to their associated duplication data */
	void BuildDuplicationMapRecursive(const TSharedPtr<FSequenceTreeItem> InTreeItem);

	/** Resolves any tokens present in the subsequence name of the input tree item, then resolves its children */
	void ResolveSubsequenceNameRecursive(TSharedPtr<FSequenceTreeItem> InTreeItem);

	/** Expands the input item in the subsequence tree view */
	void ExpandTreeItem(TSharedPtr<FSequenceTreeItem> InTreeItem) const;

	/** Save the duplication mode settings for each subassembly to a config file to re-use the next time an assembly with the same schema is duplicated */
	void SaveDuplicationPreferences() const;

	/** Reads the saved config and returns the most recent duplication mode for the input subassembly */
	ECineAssemblyDuplicationMode GetDuplicationPreference(const FString& SubAssemblyName) const;

private:
	/** The original assembly being duplicated */
	UCineAssembly* OriginalAssembly;

	/** The schema of the assembly being duplicated */
	const UCineAssemblySchema* Schema;

	/** The duplicated assembly that is being configured */
	TStrongObjectPtr<UCineAssembly> DuplicateAssembly;

	/** List of subsequence tree items for the tree view */
	TArray<TSharedPtr<FSequenceTreeItem>> TreeItems;

	/** Tree view of subsequences, allowing the user to configure the duplication data */
	TSharedPtr<STreeView<TSharedPtr<FSequenceTreeItem>>> TreeView;

	/** Map of subsequences to the associated duplication data, which mirrors the tree view */
	TMap<UMovieSceneSequence*, FSubsequenceDuplicationData> SubsequenceDuplicationData;

	/** The path where the duplicate assembly asset should be created */
	FString DuplicationPath;

	/** The path of the original assembly asset */
	FString OriginalAssemblyPath;

	/** The root folder of the original assembly (i.e. the asset path with the default assembly path from the schema removed) */
	FString OriginalAssemblyRootFolder;

	/** The path of the duplicate assembly asset (after it has been renamed by the factory) */
	FString DuplicateAssemblyPath;

	/** The root folder of the duplicate assembly (i.e. the asset path with the default assembly path from the schema removed) */
	FString DuplicateAssemblyRootFolder;

	/** Config section name for duplication preferences */
	static const FString DuplicationPreferenceSection;
};
