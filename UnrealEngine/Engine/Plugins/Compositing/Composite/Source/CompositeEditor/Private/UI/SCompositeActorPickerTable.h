// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

struct FSceneOutlinerFilters;
class SCompositeActorPickerToolbar;
class FMenuBuilder;
class FUICommandList;
template<typename T> class SListView;

/** Stores a pointer reference to a list of actors on a UObject to be managed by the actor picker */
struct FCompositeActorPickerListRef
{
public:
	FCompositeActorPickerListRef(const TWeakObjectPtr<UObject>& InActorListOwner, const FName& InActorListPropertyName, TArray<TSoftObjectPtr<AActor>>* InActorList);
	
	FCompositeActorPickerListRef() { }
	
	/** Gets whether the pointer to the actor list is valid */
	bool IsValid() const;

	/** Raises the owner's PreEditChange event for the actor list property */
	void NotifyPreEditChange();

	/** Raises the owner's PostEditchange event for the actor list property */
	void NotifyPostEditChangeList(EPropertyChangeType::Type InChangeType);
	
public:
	/** Reference to the UObject that owns the actor list being edited by this widget */
	TWeakObjectPtr<UObject> ActorListOwner = nullptr;

	/** The name of the actor list property within ActorListOwner */
	FName ActorListPropertyName = NAME_None;

	/** The property in ActorListOwner of the actor list */
	FProperty* ActorListProperty = nullptr;
	
	/** Pointer to the actor list being edited by this widget */
	TArray<TSoftObjectPtr<AActor>>* ActorList = nullptr;
};

/**
 * A table that displays a list of selected actors as well as a picker to easily add or remove actors from the current level
 */
class SCompositeActorPickerTable : public SCompoundWidget, public FEditorUndoClient
{
private:
	struct FActorListItem
	{
		/** Soft reference to the actor this item represents */
		TSoftObjectPtr<AActor> Actor;

		/** The index of the actor in the list */
		int32 Index;

		FActorListItem(const TSoftObjectPtr<AActor>& InActor, int32 InIndex)
			: Actor(InActor)
			, Index(InIndex)
		{ }
	};
	using FActorListItemRef = TSharedPtr<FActorListItem>;

public:
	DECLARE_DELEGATE_OneParam(FOnExtendAddMenu, FMenuBuilder&);
	
public:
	SLATE_BEGIN_ARGS(SCompositeActorPickerTable) : _ShowApplyMaterialSection(false) { }
		SLATE_ATTRIBUTE(TSharedPtr<FSceneOutlinerFilters>, SceneOutlinerFilters)
		SLATE_EVENT(FOnExtendAddMenu, OnExtendAddMenu)
		SLATE_EVENT(FSimpleDelegate, OnLayoutSizeChanged)
		SLATE_ATTRIBUTE(bool, ShowApplyMaterialSection)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FCompositeActorPickerListRef& InActorListRef);

	virtual ~SCompositeActorPickerTable() override;

	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// End of SWidget interface
	
	// FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient
	
private:
	/** Binds any commands to callbacks for the actor list view */
	void BindCommands();
	
	/** Fills the list view's source list with the valid actors from the actor list and applies any active filters */
	void FillActorList();

	/** Filters the actor list using any active filters */
	void FilterActorList();

	/** Raised when the toolbar filter has changed */
	void OnFilterChanged();

	/** Create the right click context menu for the actor list view */
	TSharedPtr<SWidget> CreateListContextMenu();

	/** Raised when a drag drop operation is trying to be dropped on the actor list */
	bool OnAllowListDrop(TSharedPtr<FDragDropOperation> InDragDropOperation);

	/** Raised when a drag drop operation is being dropped on the actor list */
	FReply OnListDropped(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent);
	
	/** Gets the filter status text */
	FText GetFilterStatusText() const;

	/** Gets the color for the filter status text */
	FSlateColor GetFilterStatusTextColor() const;
	
	/** Removes the selected actors from the list */
	void RemoveActors();

	/** Apply the specified material to the the selected actors from the list */
	void ApplyMaterialToActors(const TCHAR* InMaterialPath);

	/** Gets whether the contextual menu commands can be used */
	bool CanEditActors() const;
	
	/** Raised when a property on the object has changed */
	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);
	
private:
	/** List view that displays the list of selected actors */
	TSharedPtr<SListView<FActorListItemRef>> ListView;

	/** The toolbar that contains the Add button and the filter text box */
	TSharedPtr<SCompositeActorPickerToolbar> Toolbar;
	
	/** The reference to the actor list being managed by this actor picker */
	FCompositeActorPickerListRef ActorListRef;

	/** The list of actors to display in the list view */
	TArray<FActorListItemRef> ActorListItems;

	/** Filtered list of actors to display in the list view */
	TArray<FActorListItemRef> FilteredActorListItems;

	/** The command list for commands related to the actor list view */
	TSharedPtr<FUICommandList> CommandList;

	/** Stores the last observed number of widgets the list view is actually displaying, used to determine if the list view's layout has changed */
	int32 CachedListViewNumChildren = 0;
	
	/** Raised when building the Add menu, allows the menu contents to be extended with additional entries */
	FOnExtendAddMenu OnExtendAddMenu;

	/** Raised when the number of items in the list view changes */
	FSimpleDelegate OnLayoutSizeChanged;

	/** Flag to enable the material application section for selected actors. */
	TAttribute<bool> bShowApplyMaterialSection;

	friend class SCompositeActorPickerToolbar;
	friend class SCompositeActorListItemRow;
};
