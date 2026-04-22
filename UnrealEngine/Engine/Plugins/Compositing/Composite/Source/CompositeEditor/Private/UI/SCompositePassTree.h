// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class FUICommandList;
class SCompositePassTreeToolbar;
class UCompositePassBase;
class UCompositeLayerPlate;
template<typename T> class STreeView;

/**
 * Widget that displays a filterable tree view for the passes within a composite plate layer
 */
class SCompositePassTree : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, const TArray<UObject*>&);
	
private:
	enum class EPassType : uint8
	{
		Media,
		//Scene, // TODO: Add Scene when it is supported
		Layer,
		MAX
	};

	/** Stores data needed for a single row item in the tree view */
	struct FPassTreeItem
	{
		/** Plate whose passes are being displayed */
		TWeakObjectPtr<UCompositeLayerPlate> Plate = nullptr;

		/** The pass type this tree item belongs to */
		EPassType PassType = EPassType::Media;

		/** If this tree item is for a specific pass, this is the pass's index in the plate's corresponding list of passes */
		int32 PassIndex = INDEX_NONE;

		/** Indicates if this tree item is being filtered out by the current filter */
		bool bFilteredOut = false;

		/** Child tree items of this tree item */
		TArray<TSharedPtr<FPassTreeItem>> Children;
		
		
		/** Gets whether the pass index is valid within the plate's list of passes */
		bool HasValidPassIndex() const;

		/** Gets the pass object referenced by this tree item */
		UCompositePassBase* GetPass() const;
	};
	using FPassTreeItemPtr = TSharedPtr<FPassTreeItem>;
	
public:
	SLATE_BEGIN_ARGS(SCompositePassTree) { }
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		SLATE_EVENT(FSimpleDelegate, OnLayoutChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakObjectPtr<UCompositeLayerPlate>& InPlate);

	virtual ~SCompositePassTree() override;
	
	// SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// End of SWidget interface
	
private:
	/** Gets a reference to the pass list that corresponds to the specified pass type in the plate. The plate pointer must be valid to call properly */
	static TArray<TObjectPtr<UCompositePassBase>>& GetPassList(TWeakObjectPtr<UCompositeLayerPlate> InPlate, EPassType InPassType);
	TArray<TObjectPtr<UCompositePassBase>>& GetPassList(EPassType InPassType) { return GetPassList(Plate, InPassType); }
	const TArray<TObjectPtr<UCompositePassBase>>& GetPassList(EPassType InPassType) const { return GetPassList(Plate, InPassType); }
	
	/** Gets the FProperty for the specified pass type */
	static FProperty* GetPassListProperty(EPassType InPassType);

	/** Binds commands in the command list to callbacks */
	void BindCommands();
	
	/** Creates any tree items needed for all passes in the plate */
	void FillPassTreeItems();

	/** Applies the current filter to the pass tree items to build a filtered list of tree items */
	void FilterPassTreeItems();

	/** Refreshes the tree items for the passes in the specified pass type list */
	void RefreshPassType(EPassType InPassType, bool bRefreshTreeView = true);
	
	/** Refreshes the tree view's displayed list and automatically expands all tree items */
	void RefreshAndExpandTreeView();

	/** Creates the tree view right click context menu */
	TSharedPtr<SWidget> CreateTreeContextMenu();

	/** Gets the global enabled state for all passes on the plate */
	ECheckBoxState GetGlobalEnabledState() const;

	/** Sets the enabled state for all passes on the plate */
	void OnGlobalEnabledStateChanged(ECheckBoxState CheckBoxState);
	
	/** Raised when a pass has been moved via a drag drop operation */
	void OnPassMoved(const TSharedPtr<FPassTreeItem>& InTreeItem, EPassType InDestPassType, int InDestIndex);

	/** Raised when the selected items in the tree view have bene changed */
	void OnPassSelectionChanged(TSharedPtr<FPassTreeItem> InTreeItem, ESelectInfo::Type SelectInfo);
	
	/** Raised when the active filter has been changed */
	void OnFilterChanged();

	/** Raised when a new pass is being added */
	void OnPassAdded(const UClass* InPassClass);

	/** Raised when the toolbar is adding pass types to the Add Pass menu, allowing types to be filtered out */
	bool OnFilterNewPassType(const UClass* InPassType) const;
	
	/** Gets status text based on selection and active filter */
	FText GetFilterStatusText() const;

	/** Gets status text color based on selection and active filter */
	FSlateColor GetFilterStatusColor() const;

	/** Raised when a property on the object has changed */
	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);
	
	/** Generic command callbacks to handle copy, cut, paste, delete, and enable */
	void CopySelectedItems();
	bool CanCopySelectedItems();
	void CutSelectedItems();
	bool CanCutSelectedItems();
	void PasteSelectedItems();
	bool CanPasteSelectedItems();
	void DuplicateSelectedItems();
	bool CanDuplicateSelectedItems();
	void DeleteSelectedItems();
	bool CanDeleteSelectedItems();
	void RenameSelectedItem();
	bool CanRenameSelectedItem();
	void EnableSelectedItems();
	bool CanEnableSelectedItems();
	
private:
	/** The plate whose passes are being displayed */
	TWeakObjectPtr<UCompositeLayerPlate> Plate;

	/** List of all pass tree items for the plate */
	TArray<FPassTreeItemPtr> PassTreeItems;

	/** List of all passes that match the current filter */
	TArray<FPassTreeItemPtr> FilteredPassTreeItems;
	
	/** Tree view that displays a hierarchical list of passes on the plate */
	TSharedPtr<STreeView<FPassTreeItemPtr>> TreeView;

	/** Toolbar widget displayed above the tree view */
	TSharedPtr<SCompositePassTreeToolbar> Toolbar;

	/** The command list for commands related to the tree view */
	TSharedPtr<FUICommandList> CommandList;
	
	/** Delegate that gets raised when the tree view selection changes */
	FOnSelectionChanged OnSelectionChanged;

	/** Delegate raised when there is a potential layout change to the tree view, to give containers the opportunity to correctly resize their layouts */
	FSimpleDelegate OnLayoutChanged;
	
	friend class SCompositePassTreeItemRow;
	friend class SCompositePassTreeToolbar;
	friend class FCompositePassTreeFilter;
};
