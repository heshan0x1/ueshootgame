// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCompositeActorPickerTable.h"

#include "Components/PrimitiveComponent.h"
#include "CompositeEditorCommands.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "SceneOutlinerDragDrop.h"
#include "SCompositeActorPickerSceneOutliner.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Components/VerticalBox.h"
#include "DragAndDrop/ActorDragDropGraphEdOp.h"
#include "DragAndDrop/FolderDragDropOp.h"
#include "Editor/EditorWidgets/Public/SDropTarget.h"
#include "Filters/SFilterSearchBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Materials/Material.h"
#include "Misc/TextFilter.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SCompositeActorPickerTable"

namespace CompositeActorPickerTable
{
	static FName ActorListColumn_ActorName = "ActorName";
	static FName ActorListColumn_Type = "Type";
}

/** Toolbar widget that contains filtering and an add button */
class SCompositeActorPickerToolbar : public SCompoundWidget
{
private:
	using FFilterType = SCompositeActorPickerTable::FActorListItemRef;
	using FTreeFilter =  TTextFilter<FFilterType>;
	using FOnExtendAddMenu = SCompositeActorPickerTable::FOnExtendAddMenu;
	
public:	
	SLATE_BEGIN_ARGS(SCompositeActorPickerToolbar) {}
		SLATE_ATTRIBUTE(TSharedPtr<FSceneOutlinerFilters>, SceneOutlinerFilters)
		SLATE_EVENT(FSimpleDelegate, OnFilterChanged)
		SLATE_EVENT(FSimpleDelegate, OnActorListChanged)
		SLATE_EVENT(FOnExtendAddMenu, OnExtendAddMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FCompositeActorPickerListRef& InActorListRef)
	{
		ActorListRef = InActorListRef;
		SceneOutlinerFilters = InArgs._SceneOutlinerFilters;
		OnFilterChanged = InArgs._OnFilterChanged;
		OnActorListChanged = InArgs._OnActorListChanged;
		OnExtendAddMenu = InArgs._OnExtendAddMenu;

		InitializeFilters();

		// Generate a (multi-layered) icon for the "Add" menu
		const TSharedRef<SLayeredImage> AddIcon =
			SNew(SLayeredImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FAppStyle::GetBrush("LevelEditor.OpenAddContent.Background"));
		AddIcon->AddLayer(FAppStyle::GetBrush("LevelEditor.OpenAddContent.Overlay"));
		
		ChildSlot
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SComboButton)
					.HasDownArrow(true)
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.ToolTipText(LOCTEXT("AddActorButtonToolTip", "Add a level actor to the list of selected actors"))
					.VAlign(VAlign_Center)
					.OnGetMenuContent(this, &SCompositeActorPickerToolbar::GetAddActorMenuContent)
					.ButtonContent()
					[
						AddIcon
					]
				]
				
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(TextFilterSearchBox, SFilterSearchBox)
					.HintText(LOCTEXT("FilterSearch", "Search..."))
					.ToolTipText(LOCTEXT("FilterSearchHint", "Type here to search for specific actors"))
					.OnTextChanged_Lambda([this](const FText& InText)
					{
						TreeTextFilter->SetRawFilterText(InText);
					})
				]
			]
		];
	}

	bool ItemPassesFilters(const FFilterType& InItem) const
	{
		return TreeTextFilter->PassesFilter(InItem);
	}

	void ClearFilters()
	{
		TextFilterSearchBox->SetText(FText::GetEmpty());
	}
	
private:
	/** Creates all the filter bar filters and initializes the text filter */
	void InitializeFilters()
	{
		TreeTextFilter = MakeShared<FTreeFilter>(FTreeFilter::FItemToStringArray::CreateLambda([](const FFilterType& InItem, TArray<FString>& OutStrings)
		{
			if (InItem.IsValid() && InItem->Actor.IsValid())
			{
				OutStrings.Add(InItem->Actor->GetActorNameOrLabel());
				OutStrings.Add(InItem->Actor->GetClass()->GetDisplayNameText().ToString());
			}
		}));
		TreeTextFilter->OnChanged().AddSP(this, &SCompositeActorPickerToolbar::FilterChanged);
	}

	/** Creates the menu widget to display when the Add button is pressed */
	TSharedRef<SWidget> GetAddActorMenuContent()
	{
		constexpr bool bCloseMenuAfterSelection = true;
		FMenuBuilder MenuBuilder(bCloseMenuAfterSelection, nullptr);

		MenuBuilder.BeginSection("Add", LOCTEXT("AddSectionLabel", "Add Actor"));
		{
			MenuBuilder.AddMenuEntry(LOCTEXT("AddSelectedEntryLabel", "Add Selected in Outliner"),
				LOCTEXT("AddSelectionEntryToolTip", "Adds all actors currently selected in the level editor"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(),"FoliageEditMode.SetSelect"),
				FUIAction(
					FExecuteAction::CreateSP(this, &SCompositeActorPickerToolbar::AddSelectedActors),
					FCanExecuteAction::CreateSP(this, &SCompositeActorPickerToolbar::CanAddSelectedActors))
			);

			OnExtendAddMenu.ExecuteIfBound(MenuBuilder);
		}
		MenuBuilder.EndSection();
		
		MenuBuilder.BeginSection("SceneOutliner", LOCTEXT("SceneOutlinerSectionLabel", "Browse"));
		{
			TSharedRef<SWidget> SceneOutliner =
				SNew(SCompositeActorPickerSceneOutliner, ActorListRef)
				.SceneOutlinerFilters(SceneOutlinerFilters.Get(nullptr))
				.OnActorListChanged(OnActorListChanged);
			
			MenuBuilder.AddWidget(SceneOutliner, FText::GetEmpty(), true);
		}
		MenuBuilder.EndSection();
		
		return MenuBuilder.MakeWidget();
	}

	/** Adds all actors selected in the level/outliner to the actor list */
	void AddSelectedActors()
	{
		if (!ActorListRef.IsValid())
		{
			return;
		}

		TStrongObjectPtr<UObject> PinnedListOwner = ActorListRef.ActorListOwner.Pin();
		
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

		if (SelectedActors.IsEmpty())
		{
			return;
		}

		TSharedPtr<FScopedTransaction> AddActorsTransaction = nullptr;

		for (AActor* SelectedActor : SelectedActors)
		{
			if (ActorListRef.ActorList->Contains(SelectedActor))
			{
				continue;
			}

			if (!AddActorsTransaction.IsValid())
			{
				AddActorsTransaction = MakeShared<FScopedTransaction>(LOCTEXT("AddActorsTransaction", "Add Actors"));
				PinnedListOwner->Modify();

				ActorListRef.NotifyPreEditChange();
			}

			ActorListRef.ActorList->Add(SelectedActor);
		}

		if (AddActorsTransaction.IsValid())
		{
			ActorListRef.NotifyPostEditChangeList(EPropertyChangeType::ArrayAdd);
			OnActorListChanged.ExecuteIfBound();
		}
	}

	/** Gets whether the selected level/outliner actors can be added to the actor list */
	bool CanAddSelectedActors()
	{
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);
		
		return ActorListRef.IsValid() && SelectedActors.Num() > 0;
	}

	/** Raised when the actor list filter has been changed */
	void FilterChanged()
	{
		OnFilterChanged.ExecuteIfBound();
	}
	
private:
	/** A reference to the actor list being managed by the actor picker */
	FCompositeActorPickerListRef ActorListRef;
	
	TSharedPtr<FTreeFilter> TreeTextFilter;
	
	TSharedPtr<SFilterSearchBox> TextFilterSearchBox;

	/** Attribute to retrieve the filters to apply to the scene outliner actor picker when it is opened */
	TAttribute<TSharedPtr<FSceneOutlinerFilters>> SceneOutlinerFilters;

	FSimpleDelegate OnFilterChanged;
	FSimpleDelegate OnActorListChanged;
	FOnExtendAddMenu OnExtendAddMenu;
};

/** Table row to display an actor in the actor list view */
class SCompositeActorListItemRow : public SMultiColumnTableRow<SCompositeActorPickerTable::FActorListItemRef>
{
	using FActorListItemRef = SCompositeActorPickerTable::FActorListItemRef;
	
public:
	SLATE_BEGIN_ARGS(SCompositeActorListItemRow) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const FActorListItemRef& InActorItem)
	{
		ActorItem = InActorItem;

		STableRow<FActorListItemRef>::FArguments Args = FSuperRowType::FArguments()
		   .Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"));
		
		SMultiColumnTableRow::Construct(Args, InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (InColumnName == CompositeActorPickerTable::ActorListColumn_ActorName)
		{
			return SNew(SHorizontalBox)
			
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 1.0f, 6.0f, 1.0f)
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					[
						SNew(SImage).Image(this, &SCompositeActorListItemRow::GetActorIcon)
					]
				]

				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SCompositeActorListItemRow::GetActorName)
				];
		}
		else if (InColumnName == CompositeActorPickerTable::ActorListColumn_Type)
		{
			return SNew(STextBlock).Text(this, &SCompositeActorListItemRow::GetActorType);
		}

		return SNullWidget::NullWidget;
	}

private:
	/** Gets the icon to display for the actor */
	const FSlateBrush* GetActorIcon() const
	{
		if (ActorItem.IsValid() && ActorItem->Actor.IsValid())
		{
			return FSlateIconFinder::FindIconForClass(ActorItem->Actor->GetClass()).GetIcon();
		}

		return nullptr;
	}

	/** Gets the name to display for the actor */
	FText GetActorName() const
	{
		if (ActorItem.IsValid() && ActorItem->Actor.IsValid())
		{
			return FText::FromString(ActorItem->Actor->GetActorNameOrLabel());
		}

		return LOCTEXT("NoneActorLabel", "None");
	}

	/** Gets the name of the actor's type to display */
	FText GetActorType() const
	{
		if (ActorItem.IsValid() && ActorItem->Actor.IsValid())
		{
			return ActorItem->Actor->GetClass()->GetDisplayNameText();
		}

		return LOCTEXT("InvalidActorTypeLabel", "-");
	}
	
private:
	FActorListItemRef ActorItem;
};

FCompositeActorPickerListRef::FCompositeActorPickerListRef(const TWeakObjectPtr<UObject>& InActorListOwner, const FName& InActorListPropertyName, TArray<TSoftObjectPtr<AActor>>* InActorList)
	: ActorListOwner(InActorListOwner)
	, ActorListPropertyName(InActorListPropertyName)
	, ActorList(InActorList)
{
	if (TStrongObjectPtr<UObject> PinnedOwner = ActorListOwner.Pin())
	{
		ActorListProperty = FindFProperty<FProperty>(PinnedOwner->GetClass(), ActorListPropertyName);
	}
}

bool FCompositeActorPickerListRef::IsValid() const
{
	return ActorListOwner.IsValid() && ActorListProperty != nullptr && ActorList != nullptr;
}

void FCompositeActorPickerListRef::NotifyPreEditChange()
{
	if (!IsValid())
	{
		return;
	}

	if (TStrongObjectPtr<UObject> PinnedOwner = ActorListOwner.Pin())
	{
		PinnedOwner->PreEditChange(ActorListProperty);
	}
}

void FCompositeActorPickerListRef::NotifyPostEditChangeList(EPropertyChangeType::Type InChangeType)
{
	if (!IsValid())
	{
		return;
	}

	if (TStrongObjectPtr<UObject> PinnedOwner = ActorListOwner.Pin())
	{
		FPropertyChangedEvent ChangedEvent(ActorListProperty, InChangeType);
		PinnedOwner->PostEditChangeProperty(ChangedEvent);
	}
}

void SCompositeActorPickerTable::Construct(const FArguments& InArgs, const FCompositeActorPickerListRef& InActorListRef)
{
	ActorListRef = InActorListRef;
	OnLayoutSizeChanged = InArgs._OnLayoutSizeChanged;
	bShowApplyMaterialSection = InArgs._ShowApplyMaterialSection;
	
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SCompositeActorPickerTable::OnObjectPropertyChanged);
	
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	CommandList = MakeShared<FUICommandList>();
	BindCommands();
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SAssignNew(Toolbar, SCompositeActorPickerToolbar, ActorListRef)
			.SceneOutlinerFilters(InArgs._SceneOutlinerFilters)
			.OnFilterChanged(this, &SCompositeActorPickerTable::OnFilterChanged)
			.OnActorListChanged_Lambda([this]
			{
				FillActorList();
			})
			.OnExtendAddMenu(InArgs._OnExtendAddMenu)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SDropTarget)
			.OnAllowDrop(this, &SCompositeActorPickerTable::OnAllowListDrop)
			.OnDropped(this, &SCompositeActorPickerTable::OnListDropped)
			[
				SAssignNew(ListView, SListView<FActorListItemRef>)
				.HeaderRow(
					SNew(SHeaderRow)

					+SHeaderRow::Column(CompositeActorPickerTable::ActorListColumn_ActorName)
					.FillWidth(0.6)
					.DefaultLabel(LOCTEXT("ActorNameColumnLabel", "Item Label"))

					+SHeaderRow::Column(CompositeActorPickerTable::ActorListColumn_Type)
					.FillWidth(0.4)
					.DefaultLabel(LOCTEXT("ActorTypeColumnLabel", "Type"))
				)
				.ListItemsSource(&FilteredActorListItems)
				.OnGenerateRow_Lambda([](FActorListItemRef InListItem, const TSharedRef<STableViewBase>& OwnerTable)
				{
					return SNew(SCompositeActorListItemRow, OwnerTable, InListItem);
				})
				.OnContextMenuOpening(this, &SCompositeActorPickerTable::CreateListContextMenu)
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.MinHeight(24.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SCompositeActorPickerTable::GetFilterStatusText)
			.ColorAndOpacity(this, &SCompositeActorPickerTable::GetFilterStatusTextColor)
		]
	];

	FillActorList();
}

SCompositeActorPickerTable::~SCompositeActorPickerTable()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

void SCompositeActorPickerTable::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Keep track of when the list view has changed the number of widgets it is actively displaying.
	// This allows us to know if list items have been added or removed (as a list view's internal list is
	// not generated immediately when calling RefreshList), so that we can call OnLayoutSizeChanged at the right time
	const int32 ListViewNumChildren = ListView.IsValid() ? ListView->GetNumGeneratedChildren() : 0;
	if (ListViewNumChildren != CachedListViewNumChildren)
	{
		OnLayoutSizeChanged.ExecuteIfBound();
		CachedListViewNumChildren = ListViewNumChildren;
	}
}

FReply SCompositeActorPickerTable::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

void SCompositeActorPickerTable::PostUndo(bool bSuccess)
{
	FillActorList();
}

void SCompositeActorPickerTable::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void SCompositeActorPickerTable::BindCommands()
{
	CommandList->MapAction(FCompositeEditorCommands::Get().RemoveActor,
		FExecuteAction::CreateSP(this, &SCompositeActorPickerTable::RemoveActors),
		FCanExecuteAction::CreateSP(this, &SCompositeActorPickerTable::CanEditActors));
}

void SCompositeActorPickerTable::FillActorList()
{
	if (!ActorListRef.IsValid())
	{
		return;
	}

	// Check to see if a refresh is needed by comparing the list of actors being displayed to
	// the list of actors in the actor list reference
	bool bNeedsRefresh = false;
	if (ActorListItems.Num() != ActorListRef.ActorList->Num())
	{
		bNeedsRefresh = true;
	}
	else
	{
		for (const FActorListItemRef& ActorListItem : ActorListItems)
		{
			if (!ActorListItem.IsValid())
			{
				bNeedsRefresh = true;
				break;
			}
			
			if (!ActorListRef.ActorList->Contains(ActorListItem->Actor))
			{
				bNeedsRefresh = true;
				break;
			}
		}
	}

	if (!bNeedsRefresh)
	{
		return;
	}

	ActorListItems.Empty();
	
	for (int32 Index = 0; Index < ActorListRef.ActorList->Num(); ++Index)
	{
		ActorListItems.Add(MakeShared<FActorListItem>((*ActorListRef.ActorList)[Index], Index));
	}

	FilterActorList();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SCompositeActorPickerTable::FilterActorList()
{
	FilteredActorListItems.Empty();

	for (const FActorListItemRef& Actor : ActorListItems)
	{
		const bool bPassesFilter = Toolbar.IsValid() ? Toolbar->ItemPassesFilters(Actor) : true;
		if (bPassesFilter)
		{
			FilteredActorListItems.Add(Actor);
		}
	}
}

void SCompositeActorPickerTable::OnFilterChanged()
{
	FilterActorList();
	
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedPtr<SWidget> SCompositeActorPickerTable::CreateListContextMenu()
{
	if (!ListView.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	
	const int32 NumItems = ListView->GetNumItemsSelected();
	if (NumItems >= 1)
	{
		constexpr bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);
		
		if (bShowApplyMaterialSection.Get())
		{
			MenuBuilder.BeginSection("ApplyMaterial", LOCTEXT("ApplyMaterial", "Apply Material"));

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ApplyLitMaskedMaterial", "Apply Lit Masked Material To Selected Actors"),
				LOCTEXT("ApplyLitMaskedMaterialToolTip", "Apply the plugin default lit masked material to selected actors: best for catching shadows and reflections."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SCompositeActorPickerTable::ApplyMaterialToActors, TEXT("/Composite/Materials/M_CompositeMesh_Lit_Masked.M_CompositeMesh_Lit_Masked")),
					FCanExecuteAction::CreateSP(this, &SCompositeActorPickerTable::CanEditActors)
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ApplyUnlitAlphaMaterial", "Apply Unlit Alpha Material To Selected Actors"),
				LOCTEXT("ApplyUnlitAlphaMaterialToolTip", "Apply the plugin default unlit alpha material to selected actors: best for keying media."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SCompositeActorPickerTable::ApplyMaterialToActors, TEXT("/Composite/Materials/M_CompositeMesh_Unlit_AlphaComposite.M_CompositeMesh_Unlit_AlphaComposite")),
					FCanExecuteAction::CreateSP(this, &SCompositeActorPickerTable::CanEditActors)
				)
			);
			MenuBuilder.EndSection();
		}
		
		MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
		MenuBuilder.AddMenuEntry(FCompositeEditorCommands::Get().RemoveActor);
		MenuBuilder.EndSection();
		
		return MenuBuilder.MakeWidget();
	}

	return SNullWidget::NullWidget;
}

bool SCompositeActorPickerTable::OnAllowListDrop(TSharedPtr<FDragDropOperation> InDragDropOperation)
{
	// Support dragging both actors and folders from the Outliner (dragging a folder will add all actor types in the folder)
	return InDragDropOperation->IsOfType<FActorDragDropGraphEdOp>() || InDragDropOperation->IsOfType<FSceneOutlinerDragDropOp>();
}

FReply SCompositeActorPickerTable::OnListDropped(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent)
{
	if (!ActorListRef.IsValid())
	{
		return FReply::Unhandled();
	}

	TStrongObjectPtr<UObject> PinnedListOwner = ActorListRef.ActorListOwner.Pin();
	
	TArray<AActor*> DroppedActors;
	if (const TSharedPtr<FActorDragDropGraphEdOp> ActorOperation = DragDropEvent.GetOperationAs<FActorDragDropGraphEdOp>())
	{
		Algo::TransformIf(ActorOperation->Actors, DroppedActors,
			[](const TWeakObjectPtr<AActor>& Actor) { return Actor.IsValid() && !Actor->HasAnyFlags(RF_Transient); },
			[](const TWeakObjectPtr<AActor>& Actor) { return Actor.Get(); });
	}
	else if (const TSharedPtr<FSceneOutlinerDragDropOp> SceneOperation = DragDropEvent.GetOperationAs<FSceneOutlinerDragDropOp>())
	{
		if (const TSharedPtr<FFolderDragDropOp> FolderOp = SceneOperation->GetSubOp<FFolderDragDropOp>())
		{
			FActorFolders::GetActorsFromFolders(*FolderOp->World.Get(), FolderOp->Folders, DroppedActors);
			DroppedActors.RemoveAll([](const AActor* Actor) { return Actor->HasAnyFlags(RF_Transient); });
		}
	}
			
	TSharedPtr<FScopedTransaction> AddActorsTransaction = nullptr;
	for (AActor* DroppedActor : DroppedActors)
	{
		if (ActorListRef.ActorList->Contains(DroppedActor))
		{
			continue;
		}

		if (!AddActorsTransaction.IsValid())
		{
			AddActorsTransaction = MakeShared<FScopedTransaction>(LOCTEXT("AddActorsTransaction", "Add Actors"));
			PinnedListOwner->Modify();

			ActorListRef.NotifyPreEditChange();
		}

		ActorListRef.ActorList->Add(DroppedActor);
	}

	if (AddActorsTransaction.IsValid())
	{
		ActorListRef.NotifyPostEditChangeList(EPropertyChangeType::ArrayAdd);
		FillActorList();
	}

	return FReply::Handled();
}

FText SCompositeActorPickerTable::GetFilterStatusText() const
{
	const int32 NumActors = ActorListItems.Num();
	const int32 NumFiltered = FilteredActorListItems.Num();
	const int32 NumSelected = ListView.IsValid() ? ListView->GetNumItemsSelected() : 0;
	
	const FText ActorLabel = NumActors > 1 ? LOCTEXT("ActorPlural", "Actors") : LOCTEXT("ActorSingular", "Actor");
	if (NumActors > 0 && NumActors == NumFiltered)
	{
		if (NumSelected > 0)
		{
			return FText::Format(LOCTEXT("NumActorsAndSelectedTextFormat", "{0} {1}, {2} Selected"), FText::AsNumber(NumActors), ActorLabel, FText::AsNumber(NumSelected));
		}
		else
		{
			return FText::Format(LOCTEXT("NumActorsTextFormat", "{0} {1}"), FText::AsNumber(NumActors), ActorLabel);
		}
	}
	else if (NumFiltered > 0)
	{
		if (NumSelected > 0)
		{
			return FText::Format(LOCTEXT("NumActorsWithFilteredAndSelectedTextFormat", "Showing {0} of {1} {2}, {3} Selected"),
				FText::AsNumber(NumFiltered),
				FText::AsNumber(NumActors),
				ActorLabel,
				FText::AsNumber(NumSelected));
		}
		else
		{
			return FText::Format(LOCTEXT("NumActorsWithFilteredTextFormat", "Showing {0} of {1} {2}"),
				FText::AsNumber(NumFiltered),
				FText::AsNumber(NumActors),
				ActorLabel);
		}
	}
	else
	{
		if (NumActors > 0)
		{
			return FText::Format(LOCTEXT("NoMatchingActorsTextFormat", "No matching actors ({0} {1})"), FText::AsNumber(NumActors), ActorLabel);
		}
		else
		{
			return LOCTEXT("NoActorsFoundLabel", "0 Actors");
		}
	}
}

FSlateColor SCompositeActorPickerTable::GetFilterStatusTextColor() const
{
	const int32 NumActors = ActorListItems.Num();
	const int32 NumFiltered = FilteredActorListItems.Num();
	
	if (NumActors > 0 && NumFiltered == 0)
	{
		return FAppStyle::Get().GetSlateColor("Colors.AccentRed");
	}
	
	return FSlateColor::UseForeground();
}

void SCompositeActorPickerTable::RemoveActors()
{
	TArray<FActorListItemRef> SelectedActors = ListView.IsValid() ? ListView->GetSelectedItems() : TArray<FActorListItemRef>();
	if (SelectedActors.IsEmpty())
	{
		return;
	}

	if (!ActorListRef.IsValid())
	{
		return;
	}

	TStrongObjectPtr<UObject> PinnedListOwner = ActorListRef.ActorListOwner.Pin();

	TArray<int32> ActorIndicesToRemove;
	for (const FActorListItemRef& ActorItem : SelectedActors)
	{
		if (!ActorListRef.ActorList->IsValidIndex(ActorItem->Index))
		{
			return;
		}

		ActorIndicesToRemove.Add(ActorItem->Index);
	}

	if (ActorIndicesToRemove.IsEmpty())
	{
		return;
	}

	// Remove in reverse order so that indices don't get messed up as actors are removed
	ActorIndicesToRemove.Sort();

	FScopedTransaction RemoveActorsTransaction(LOCTEXT("RemoveActorsTransaction", "Remove Actors"));
	PinnedListOwner->Modify();
	ActorListRef.NotifyPreEditChange();
	
	for (int32 Index = ActorIndicesToRemove.Num() - 1; Index >= 0; --Index)
	{
		ActorListRef.ActorList->RemoveAt(ActorIndicesToRemove[Index]);
	}

	ActorListRef.NotifyPostEditChangeList(EPropertyChangeType::ArrayRemove);
	FillActorList();
}

void SCompositeActorPickerTable::ApplyMaterialToActors(const TCHAR* InMaterialPath)
{
	UMaterial* LoadedMaterial = LoadObject<UMaterial>(nullptr, InMaterialPath);
	if (!ensureMsgf(LoadedMaterial, TEXT("Expected valid material to apply to actors.")))
	{
		return;
	}

	TArray<FActorListItemRef> SelectedActors = ListView.IsValid() ? ListView->GetSelectedItems() : TArray<FActorListItemRef>();
	if (SelectedActors.IsEmpty())
	{
		return;
	}

	if (!ActorListRef.IsValid())
	{
		return;
	}

	TArray<int32> ActorIndicesToEdit;
	for (const FActorListItemRef& ActorItem : SelectedActors)
	{
		if (!ActorListRef.ActorList->IsValidIndex(ActorItem->Index))
		{
			return;
		}

		ActorIndicesToEdit.Add(ActorItem->Index);
	}

	if (ActorIndicesToEdit.IsEmpty())
	{
		return;
	}

	TStrongObjectPtr<UObject> PinnedListOwner = ActorListRef.ActorListOwner.Pin();

	FScopedTransaction UpdateActorsMaterialTransaction(LOCTEXT("UpdateActorsMaterial", "Update Actors Material"));
	PinnedListOwner->Modify();
	ActorListRef.NotifyPreEditChange();

	for (int32 Index : ActorIndicesToEdit)
	{
		TSoftObjectPtr<AActor>& Actor = (*ActorListRef.ActorList)[Index];
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
			{
				PrimitiveComponent->Modify();
				const int32 MaterialCount = PrimitiveComponent->GetNumMaterials();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
				{
					PrimitiveComponent->SetMaterial(MaterialIndex, LoadedMaterial);
				}
			}
		}
	}
	// Rely on the global UpdateCompositeMeshes() post-edit call to update materials into MIDs
	ActorListRef.NotifyPostEditChangeList(EPropertyChangeType::Unspecified);
}

bool SCompositeActorPickerTable::CanEditActors() const
{
	const int32 NumSelected = ListView.IsValid() ? ListView->GetNumItemsSelected() : 0;
	if (NumSelected == 0)
	{
		return false;
	}

	if (!ActorListRef.IsValid())
	{
		return false;
	}

	return true;
}

void SCompositeActorPickerTable::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (!ActorListRef.IsValid())
	{
		return;
	}

	if (InObject != ActorListRef.ActorListOwner.Get())
	{
		return;
	}

	if (InPropertyChangedEvent.GetPropertyName() == ActorListRef.ActorListPropertyName)
	{
		FillActorList();
	}
}

#undef LOCTEXT_NAMESPACE
