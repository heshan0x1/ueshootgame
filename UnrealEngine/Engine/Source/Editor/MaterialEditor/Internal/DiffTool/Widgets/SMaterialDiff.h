// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinition.h"
#include "CoreMinimal.h"
#include "DiffControl.h"
#include "DiffTool/MaterialDiffPanel.h"
#include "SBlueprintDiff.h"
#include "Widgets/SCompoundWidget.h"

class FBlueprintDifferenceTreeEntry;
class UEdGraph;
class UMaterialGraph;

struct FMaterialToDiff;

enum class EAssetEditorCloseReason : uint8;

struct FMaterialDiffResultItem : public FDiffResultItem
{
	/** Not empty if the DiffResultItem is a property changed */
	FPropertyPath Property;
};

/* Visual Diff between two Material Graphs */
class MATERIALEDITOR_API SMaterialDiff : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialDiff) {}
		SLATE_ARGUMENT(TObjectPtr<UMaterialGraph>, OldMaterialGraph)
		SLATE_ARGUMENT(TObjectPtr<UMaterialGraph>, NewMaterialGraph)
		SLATE_ARGUMENT(FRevisionInfo, OldRevision)
		SLATE_ARGUMENT(FRevisionInfo, NewRevision)
		SLATE_ARGUMENT(bool, ShowAssetNames)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMaterialDiff() override;

	/** Called when a new Graph is clicked on by user */
	void OnGraphChanged(FMaterialToDiff* Diff);

	/** Called when user clicks on a new graph list item */
	void OnGraphSelectionChanged(TSharedPtr<FMaterialToDiff> Item, ESelectInfo::Type SelectionType);

	/** Called when user clicks on an entry in the listview of differences */
	void OnDiffListSelectionChanged(TSharedPtr<FMaterialDiffResultItem> TheDiff);

	/** Helper function for generating an empty widget */
	static TSharedRef<SWidget> DefaultEmptyPanel();

	/** Helper function to create a window that holds a diff widget */
	static TSharedPtr<SWindow> CreateDiffWindow(FText WindowTitle, TObjectPtr<UMaterialGraph> OldMaterialGraph, TObjectPtr<UMaterialGraph> NewMaterialGraph, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision);

	/** Helper function to create a window that holds a diff widget (default window title) */
	static TSharedPtr<SWindow> CreateDiffWindow(TObjectPtr<UMaterialGraph> OldMaterialGraph, TObjectPtr<UMaterialGraph> NewMaterialGraph, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision, const UClass* ObjectClass);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnWindowClosedEvent, TSharedRef<SMaterialDiff>)
	FOnWindowClosedEvent OnWindowClosedEvent;

protected:
	/** Called when user clicks button to go to next difference */
	void NextDiff();

	/** Called when user clicks button to go to prev difference */
	void PrevDiff();

	/** Called to determine whether we have a list of differences to cycle through */
	bool HasNextDiff() const;
	bool HasPrevDiff() const;

	/** User toggles the option to show/hide preview viewports */
	void ToggleViewport();

	/** Get the image to show for the toggle viewport option */
	FSlateIcon GetViewportImage() const;

	/** Find the FMaterialToDiff that displays the graph with GraphPath relative path */
	FMaterialToDiff* FindGraphToDiffEntry(const FString& GraphPath);

	/** Bring these revisions of graph into focus on main display */
	void FocusOnMaterialGraphRevisions(FMaterialToDiff* Diff);

	/** User toggles the option to lock the views between the two material graphs */
	void OnToggleLockView();

	/** User toggles the option to change the split view mode betwwen vertical and horizontal */
	void OnToggleSplitViewMode();

	/** Get the image to show for the toggle lock option */
	FSlateIcon GetLockViewImage() const;

	/** Get the image to show for the toggle split view mode option */
	FSlateIcon GetSplitViewModeImage() const;

	/** Reset the graph editor, called when user switches graphs to display */
	void ResetGraphEditors();

	/** Material Graph to diff, is added to panel */
	TSharedPtr<FMaterialToDiff> MaterialGraphToDiff;

	/** Get Graph editor associated with this Graph */
	FMaterialDiffPanel& GetDiffPanelForNode(UEdGraphNode& Node);

	/** Event handler that updates the graph view when user selects a new graph */
	void HandleGraphChanged(const FString& GraphPath);

	/** Function used to generate the list of differences and the widgets needed to calculate that list */
	void GenerateDifferencesList();

	/** Called when editor may need to be closed */
	void OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason);

	struct FMaterialGraphDiffControl
	{
		FMaterialGraphDiffControl() : Widget(), DiffControl(nullptr)
		{
		}

		TSharedPtr<SWidget> Widget;
		TSharedPtr<IDiffControl> DiffControl;
	};

	FMaterialGraphDiffControl GenerateMaterialGraphPanel();

	TSharedRef<SOverlay> GenerateMaterialGraphWidgetForPanel(FMaterialDiffPanel& OutDiffPanel) const;
	TSharedRef<SBox> GenerateRevisionInfoWidgetForPanel(TSharedPtr<SWidget>& OutGeneratedWidget, const FText& InRevisionText) const;

	/** Accessor and event handler for toggling between diff view modes - only GraphMode for now: */
	void SetCurrentMode(FName NewMode);

	FName GetCurrentMode() const { return CurrentMode; }

	void OnModeChanged(const FName& InNewViewMode) const;

	void UpdateTopSectionVisibility(const FName& InNewViewMode) const;

	void SetCurrentWidgetIndex(int32 Index);
	int32 GetCurrentWidgetIndex() const;

	FName CurrentMode;

	FMaterialDiffPanel PanelOld, PanelNew;

	/** If the two views should be locked */
	bool bLockViews = true;

	/** If the view on Graph Mode should be divided vertically */
	bool bVerticalSplitGraphMode = true;

	/** If the preview Viewports should be shown */
	bool bShowViewport = true;

	/** Contents widget that we swap when mode changes (defaults, components, etc) */
	TSharedPtr<SBox> ModeContents;

	TSharedPtr<SSplitter> TopRevisionInfoWidget;

	TSharedPtr<SSplitter> DiffGraphSplitter;

	TSharedPtr<SSplitter> GraphToolBarWidget;

	TSharedPtr<SWidgetSwitcher> WidgetSwitcher;

	int32 CurrentWidgetIndex = 0;

	/** Tree of differences collected across all panels: */
	TArray<TSharedPtr<FBlueprintDifferenceTreeEntry>> PrimaryDifferencesList;

	/** List of all differences, cached so that we can iterate only the differences and not labels, etc: */
	TArray<TSharedPtr<FBlueprintDifferenceTreeEntry>> RealDifferences;

	/** Tree view that displays the differences, cached for the buttons that iterate the differences: */
	TSharedPtr<STreeView<TSharedPtr<FBlueprintDifferenceTreeEntry>>> DifferencesTreeView;

	/** Stored references to widgets used to display various parts of a material, from the mode name */
	TMap<FName, FMaterialGraphDiffControl> ModePanels;

	/** A pointer to the window holding this */
	TWeakPtr<SWindow> WeakParentWindow;

	FDelegateHandle AssetEditorCloseDelegate;
};
