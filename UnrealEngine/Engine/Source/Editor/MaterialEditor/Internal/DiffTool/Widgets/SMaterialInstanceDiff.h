// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinition.h"
#include "CoreMinimal.h"
#include "DiffTool/Widgets/SMaterialDiff.h"
#include "DiffControl.h"
#include "Materials/MaterialInstance.h"
#include "SBlueprintDiff.h"
#include "DiffTool/MaterialInstanceDiffPanel.h"
#include "Widgets/SCompoundWidget.h"

class SDetailsSplitter;
class FBlueprintDifferenceTreeEntry;

enum class EAssetEditorCloseReason : uint8;

/* Visual Diff between two Material Instances */
class MATERIALEDITOR_API SMaterialInstanceDiff : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialInstanceDiff) {}
		SLATE_ARGUMENT(TObjectPtr<UMaterialInstance>, OldMaterialInstance)
		SLATE_ARGUMENT(TObjectPtr<UMaterialInstance>, NewMaterialInstance)
		SLATE_ARGUMENT(FRevisionInfo, OldRevision)
		SLATE_ARGUMENT(FRevisionInfo, NewRevision)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMaterialInstanceDiff() override;

	/** Called when user clicks on an entry in the listview of differences */
	void OnDiffListSelectionChanged(TSharedPtr<FMaterialDiffResultItem> TheDiff);

	/** Helper function for generating an empty widget */
	static TSharedRef<SWidget> DefaultEmptyPanel();

	/** Helper function to create a window that holds a diff widget */
	static TSharedPtr<SWindow> CreateDiffWindow(FText WindowTitle, TObjectPtr<UMaterialInstance>  OldMaterialInstance, TObjectPtr<UMaterialInstance>  NewMaterialInstance, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision);

	/** Helper function to create a window that holds a diff widget (default window title) */
	static TSharedPtr<SWindow> CreateDiffWindow(TObjectPtr<UMaterialInstance>  OldMaterialInstance, TObjectPtr<UMaterialInstance>  NewMaterialInstance, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision, const UClass* ObjectClass);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnWindowClosedEvent, TSharedRef<SMaterialInstanceDiff>)
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

	/** Function used to generate the list of differences and the widgets needed to calculate that list */
	void GenerateDifferencesList();

	/** Called when editor may need to be closed */
	void OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason);

	struct FMaterialInstanceDiffControl
	{
		FMaterialInstanceDiffControl()
			: Widget()
			, DiffControl(nullptr)
		{
		}

		TSharedPtr<SWidget> Widget;
		TSharedPtr<IDiffControl> DiffControl;
	};

	FMaterialInstanceDiffControl GenerateMaterialInstancePanel();

	/** Accessor and event handler for toggling between diff view modes - only details mode for now: */
	void SetCurrentMode(FName NewMode);

	FName GetCurrentMode() const { return CurrentMode; }

	void OnModeChanged(const FName& InNewViewMode) const;

	FName CurrentMode;

	FMaterialInstanceDiffPanel PanelOld, PanelNew;

	/** If the preview Viewports should be shown */
	bool bShowViewport = true;

	/** Contents widget that we swap when mode changes (defaults, components, etc) */
	TSharedPtr<SBox> ModeContents;

	TSharedPtr<SSplitter> TopRevisionInfoWidget;

	TSharedPtr<SDetailsSplitter> DiffDetailSplitter;

	/** Tree of differences collected across all panels: */
	TArray<TSharedPtr<FBlueprintDifferenceTreeEntry>> PrimaryDifferencesList;

	/** List of all differences, cached so that we can iterate only the differences and not labels, etc: */
	TArray<TSharedPtr<FBlueprintDifferenceTreeEntry>> RealDifferences;

	/** Tree view that displays the differences, cached for the buttons that iterate the differences: */
	TSharedPtr<STreeView<TSharedPtr<FBlueprintDifferenceTreeEntry>>> DifferencesTreeView;

	/** Stored references to widgets used to display various parts of a blueprint, from the mode name */
	TMap<FName, FMaterialInstanceDiffControl> ModePanels;

	/** A pointer to the window holding this */
	TWeakPtr<SWindow> WeakParentWindow;

	FDelegateHandle AssetEditorCloseDelegate;
};
