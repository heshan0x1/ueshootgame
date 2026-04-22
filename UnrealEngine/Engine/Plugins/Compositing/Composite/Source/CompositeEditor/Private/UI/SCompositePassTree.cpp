// Copyright Epic Games, Inc. All Rights Reserved.


#include "SCompositePassTree.h"

#include "CompositeEditorCommands.h"
#include "CompositeEditorStyle.h"
#include "Factories.h"
#include "SActionButton.h"
#include "ScopedTransaction.h"
#include "UnrealExporter.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Exporters/Exporter.h"
#include "Filters/GenericFilter.h"
#include "Filters/SBasicFilterBar.h"
#include "Framework/Commands/GenericCommands.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Layers/CompositeLayerPlate.h"
#include "Misc/StringOutputDevice.h"
#include "Passes/CompositePassDistortion.h"
#include "Passes/CompositePassMaterial.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/Package.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "SCompositePassTree"

namespace CompositePassTree
{
	static const FName PassTreeColumn_Enabled = "Enabled";
	static const FName PassTreeColumn_Name = "Name";
	static const FName PassTreeColumn_Type = "Type";
}

/**
 * Widget for the filter bar, needs to be a subclass that overrides MakeAddFilterMenu to give the filter bar its own unique menu name,
 * otherwise the editor will get confused with any other basic filter bar used elsewhere.
 */
template<typename TFilterType>
class SCompositePassesFilterBar : public SBasicFilterBar<TFilterType>
{
	using Super = SBasicFilterBar<TFilterType>;
	
public:

	using FOnFilterChanged = typename SBasicFilterBar<TFilterType>::FOnFilterChanged;
	using FCreateTextFilter = typename SBasicFilterBar<TFilterType>::FCreateTextFilter;

	SLATE_BEGIN_ARGS(SCompositePassesFilterBar)
	{}
		SLATE_EVENT(SCompositePassesFilterBar<TFilterType>::FOnFilterChanged, OnFilterChanged)
		SLATE_ARGUMENT(TArray<TSharedRef<FFilterBase<TFilterType>>>, CustomFilters)	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		typename SBasicFilterBar<TFilterType>::FArguments Args;
		Args._OnFilterChanged = InArgs._OnFilterChanged;
		Args._CustomFilters = InArgs._CustomFilters;
		Args._UseSectionsForCategories = true;
		Args._bPinAllFrontendFilters = true;
		
		SBasicFilterBar<TFilterType>::Construct(Args.FilterPillStyle(EFilterPillStyle::Basic));
	}

private:
	virtual TSharedRef<SWidget> MakeAddFilterMenu() override
	{
		const FName FilterMenuName = "CompositePlatePassesFilterBar.FilterMenu";
		if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
		{
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
			Menu->bShouldCloseWindowAfterMenuSelection = true;
			Menu->bCloseSelfOnly = true;

			Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				if (UFilterBarContext* Context = InMenu->FindContext<UFilterBarContext>())
				{
					Context->PopulateFilterMenu.ExecuteIfBound(InMenu);
					Context->OnExtendAddFilterMenu.ExecuteIfBound(InMenu);
				}
			}));
		}

		UFilterBarContext* FilterBarContext = NewObject<UFilterBarContext>();
		FilterBarContext->PopulateFilterMenu = FOnPopulateAddFilterMenu::CreateLambda([this](UToolMenu* Menu)
		{
			Super::PopulateCommonFilterSections(Menu);
			Super::PopulateCustomFilters(Menu);
		});
		FToolMenuContext ToolMenuContext(FilterBarContext);
		
		return UToolMenus::Get()->GenerateWidget(FilterMenuName, ToolMenuContext);
	}
};

/** Custom filter for the filter bar so that events can be received for when the filter is activated or deactivated */
class FCompositePassTreeFilter : public FGenericFilter<SCompositePassTree::FPassTreeItemPtr>
{
public:
	DECLARE_DELEGATE_OneParam(FOnActiveStateChanged, bool)
	
public:
	FCompositePassTreeFilter(const TSharedPtr<FFilterCategory>& InCategory,
		const FString& InName,
		const FText& DisplayName,
		const FOnItemFiltered& InFilterDelegate)
		: FGenericFilter<TSharedPtr<SCompositePassTree::FPassTreeItem>>(InCategory, InName, DisplayName, InFilterDelegate)
	{ }

	/** Activates the filter directly, avoiding invoking OnActiveStateChanged */
	void ActivateFilter()
	{
		TGuardValue<bool> Guard(bStateChangeGuard, true);
		SetActive(true);
	}

	/** Deactivates the filter directly, avoiding invoking OnActiveStateChanged */
	void DeactivateFilter()
	{
		TGuardValue<bool> Guard(bStateChangeGuard, true);
		SetActive(false);
	}

	/** Raised when the state of the filter is chanaged */
	virtual void ActiveStateChanged(bool bActive) override
	{
		if (bStateChangeGuard)
		{
			return;
		}

		OnActiveStateChanged.ExecuteIfBound(bActive);
	}

public:
	FOnActiveStateChanged OnActiveStateChanged;
	bool bStateChangeGuard = false;
};

/** Toolbar widget that contains filtering and an add button */
class SCompositePassTreeToolbar : public SCompoundWidget
{
private:
	using FFilterType = SCompositePassTree::FPassTreeItemPtr;
	using FTreeFilter =  TTextFilter<FFilterType>;
	
public:
	DECLARE_DELEGATE(FOnFilterChanged)
	DECLARE_DELEGATE_OneParam(FOnPassAdded, const UClass* /* InPassClass */)
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnFilterNewPassType, const UClass*);
	
	SLATE_BEGIN_ARGS(SCompositePassTreeToolbar) {}
		SLATE_EVENT(FOnFilterChanged, OnFilterChanged)
		SLATE_EVENT(FOnPassAdded, OnPassAdded)
		SLATE_EVENT(FOnFilterNewPassType, OnFilterNewPassType)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnFilterChanged = InArgs._OnFilterChanged;
		OnPassAdded = InArgs._OnPassAdded;
		OnFilterNewPassType = InArgs._OnFilterNewPassType;

		TArray<TSharedRef<FFilterBase<FFilterType>>> AllFilters;
		InitializeFilters(AllFilters);
		
		FilterBar = SNew(SCompositePassesFilterBar<FFilterType>)
			.CustomFilters(AllFilters)
			.OnFilterChanged(this, &SCompositePassTreeToolbar::FilterChanged);

		AllPassesFilter->SetActive(true);
		
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
					SNew(SActionButton)
					.ActionButtonType(EActionButtonType::Simple)
					.Icon(FAppStyle::GetBrush("Icons.PlusCircle"))
					.ToolTipText(LOCTEXT("AddPassButtonToolTip", "Add a new pass to the selected list of passes"))
					.OnGetMenuContent(this, &SCompositePassTreeToolbar::GetAddPassMenuContent)
				]
				
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(TextFilterSearchBox, SFilterSearchBox)
					.HintText(LOCTEXT("FilterSearch", "Search..."))
					.ToolTipText(LOCTEXT("FilterSearchHint", "Type here to search for specific passes"))
					.OnTextChanged_Lambda([this](const FText& InText)
					{
						TreeTextFilter->SetRawFilterText(InText);
					})
				]
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				FilterBar.ToSharedRef()
			]
		];
	}

	bool ItemPassesFilters(const FFilterType& InItem) const
	{
		// Performs an OR check on any of the filters that are enabled in the filter bar, mirroring the scene outliner filter bar behavior
		auto PassesAnyFilterBarFilter = [this](const FFilterType& InItem)
		{
			TSharedPtr<TFilterCollection<FFilterType>> FilterCollection = FilterBar->GetAllActiveFilters();
			const int32 NumFilters = FilterCollection->Num();
			if (NumFilters == 0)
			{
				return true;
			}
		
			for (int32 Index = 0; Index < NumFilters; ++Index)
			{
				if (FilterCollection->GetFilterAtIndex(Index)->PassesFilter(InItem))
				{
					return true;
				}
			}

			return false;
		};
		
		return TreeTextFilter->PassesFilter(InItem) && PassesAnyFilterBarFilter(InItem);
	}

	void ClearFilters()
	{
		// Prevent filter changed callback from firing while changing filter status, will invoke once all filters are reset
		TGuardValue<bool> FilterChangedGuard(bFilterChangedGuard, true);
		for (const TSharedPtr<FCompositePassTreeFilter>& Filter : PassTypeFilters)
		{
			Filter->DeactivateFilter();
		}

		AllPassesFilter->ActivateFilter();
		TextFilterSearchBox->SetText(FText::GetEmpty());

		OnFilterChanged.ExecuteIfBound();
	}
	
private:
	/** Creates all the filter bar filters and initializes the text filter */
	void InitializeFilters(TArray<TSharedRef<FFilterBase<FFilterType>>>& OutAllFilters)
	{
		TSharedPtr<FFilterCategory> BasicFiltersCategory = MakeShared<FFilterCategory>(LOCTEXT("BasicFiltersCategory", "Basic"), FText::GetEmpty());

		static TArray<TTuple<FString, FText, FText>> PassTypeFilterConfigs =
		{
			{ TEXT("MediaPassesFilter"), LOCTEXT("MediaPassesFilterName", "Media Passes"),  LOCTEXT("MediaPassesFilterToolTip", "Only show media passes") },
			//{ TEXT("ScenePassesFilter"), LOCTEXT("ScenePassesFilterName", "Scene Passes"),  LOCTEXT("ScenePassesFilterToolTip", "Only show scene passes")  },
			{ TEXT("LayerPassesFilter"), LOCTEXT("LayerPassesFilterName", "Layer Passes"),  LOCTEXT("LayerPassesFilterToolTip", "Only show layer passes")  }
		};

		for (int32 Index = 0; Index < PassTypeFilterConfigs.Num(); ++Index)
		{
			TSharedPtr<FCompositePassTreeFilter> PassTypeFilter = MakeShared<FCompositePassTreeFilter>(
				BasicFiltersCategory,
				PassTypeFilterConfigs[Index].Get<0>(),
				PassTypeFilterConfigs[Index].Get<1>(),
				FGenericFilter<FFilterType>::FOnItemFiltered::CreateLambda([Index](FFilterType InItem)
				{
					if (!InItem.IsValid())
					{
						return false;
					}

					return (int32)InItem->PassType == Index;
				}));
	
			PassTypeFilter->SetToolTipText(PassTypeFilterConfigs[Index].Get<2>());
			PassTypeFilter->OnActiveStateChanged.BindSP(this, &SCompositePassTreeToolbar::OnPassTypeFilterStateChanged, Index);

			PassTypeFilters.Add(PassTypeFilter);
			OutAllFilters.Add(PassTypeFilter.ToSharedRef());
		}
		
		AllPassesFilter = MakeShared<FCompositePassTreeFilter>(
				BasicFiltersCategory,
				TEXT("AllPassesFilter"),
				LOCTEXT("AllPassesFilterName", "All Passes"),
				FGenericFilter<FFilterType>::FOnItemFiltered::CreateLambda([](FFilterType InItem)
				{
					return true;
				}));
	
		AllPassesFilter->SetToolTipText(LOCTEXT("AllPassesFilterToolTip", "Show all passes"));
		AllPassesFilter->OnActiveStateChanged.BindSP(this, &SCompositePassTreeToolbar::OnAllPassesFilterStateChanged);
		
		OutAllFilters.Add(AllPassesFilter.ToSharedRef());
		
		TreeTextFilter = MakeShared<FTreeFilter>(FTreeFilter::FItemToStringArray::CreateLambda([](const FFilterType& InItem, TArray<FString>& OutStrings)
		{
			if (InItem.IsValid())
			{
				if (InItem->HasValidPassIndex())
				{
					if (UCompositePassBase* Pass = InItem->GetPass())
					{
						OutStrings.Add(Pass->GetClass()->GetDisplayNameText().ToString());
					}
					else
					{
						OutStrings.Add(TEXT("None"));
					}
				}

				static FString PassTypeFilterStrings[] =
				{
					TEXT("Media Passes"),
					// TEXT("Scene Passes"),
					TEXT("Layer Passes")
				};
				
				OutStrings.Add(PassTypeFilterStrings[(int32)InItem->PassType]);
			}
		}));
		TreeTextFilter->OnChanged().AddSP(this, &SCompositePassTreeToolbar::FilterChanged);
	}
	
	TSharedRef<SWidget> GetAddPassMenuContent()
	{
		constexpr bool bCloseMenuAfterSelection = true;
		FMenuBuilder MenuBuilder(bCloseMenuAfterSelection, nullptr);

		TArray<UClass*> BaseLayerTypes;
		const bool bRecursive = false;
		GetDerivedClasses(UCompositePassBase::StaticClass(), BaseLayerTypes, bRecursive);

		auto ShouldSkipClass = [](const UClass* InClass)
			{
				constexpr EClassFlags InvalidClassFlags = CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract;
				return InClass->HasAnyClassFlags(InvalidClassFlags) ||
					InClass->GetName().StartsWith(TEXT("SKEL_")) ||
					InClass->GetName().StartsWith(TEXT("REINST_"));
			};
		
		// Add the native child classes of UCompositeLayerBase to the menu first
		for (const UClass* BaseLayerType : BaseLayerTypes)
		{
			if (ShouldSkipClass(BaseLayerType))
			{
				continue;
			}

			if (OnFilterNewPassType.IsBound())
			{
				if (OnFilterNewPassType.Execute(BaseLayerType))
				{
					continue;
				}
			}
			
			MenuBuilder.AddMenuEntry(
				BaseLayerType->GetDisplayNameText(),
				TAttribute<FText>(),
				FSlateIconFinder::FindIconForClass(BaseLayerType),
				FUIAction(FExecuteAction::CreateSP(this, &SCompositePassTreeToolbar::AddPass, BaseLayerType)));
		}
				
		// Add any blueprint derived classes in a new section of the menu
		bool bHasAddedSeparator = false;
		for (const UClass* BaseLayerType : BaseLayerTypes)
		{
			TArray<UClass*> ChildLayerTypes;
			GetDerivedClasses(BaseLayerType, ChildLayerTypes);

			for (const UClass* ChildLayerType : ChildLayerTypes)
			{
				if (ShouldSkipClass(ChildLayerType))
				{
					continue;
				}

				if (OnFilterNewPassType.IsBound())
				{
					if (OnFilterNewPassType.Execute(BaseLayerType))
					{
						continue;
					}
				}
				
				if (!bHasAddedSeparator)
				{
					MenuBuilder.AddSeparator();
					bHasAddedSeparator = true;
				}

				MenuBuilder.AddMenuEntry(
					ChildLayerType->GetDisplayNameText(),
					TAttribute<FText>(),
					FSlateIconFinder::FindIconForClass(ChildLayerType),
					FUIAction(FExecuteAction::CreateSP(this, &SCompositePassTreeToolbar::AddPass, ChildLayerType)));
			}
		}
		
		return MenuBuilder.MakeWidget();
	}

	void AddPass(const UClass* InPassClass)
	{
		OnPassAdded.ExecuteIfBound(InPassClass);	
	}

	void OnPassTypeFilterStateChanged(bool bActive, int32 PassTypeIndex)
	{
		if (bActive)
		{
			for (int32 Index = 0; Index < PassTypeFilters.Num(); ++Index)
			{
				if (Index != PassTypeIndex && PassTypeFilters[Index]->IsActive())
				{
					PassTypeFilters[Index]->SetActive(false);
				}
			}
			
			if (AllPassesFilter->IsActive())
			{
				AllPassesFilter->DeactivateFilter();
			}
		}
		else
		{
			if (!AllPassesFilter->IsActive())
			{
				AllPassesFilter->ActivateFilter();
			}
		}
	}

	void OnAllPassesFilterStateChanged(bool bActive)
	{
		if (bActive)
		{
			for (TSharedPtr<FCompositePassTreeFilter>& Filter : PassTypeFilters)
			{
				if (Filter->IsActive())
				{
					Filter->DeactivateFilter();
				}
			}
		}
		else
		{
			if (!PassTypeFilters[0]->IsActive())
			{
				PassTypeFilters[0]->ActivateFilter();
			}
		}
	}
	
	void FilterChanged()
	{
		if (bFilterChangedGuard)
		{
			return;
		}

		OnFilterChanged.ExecuteIfBound();
	}
	
private:
	TSharedPtr<FCompositePassTreeFilter> AllPassesFilter;
	TArray<TSharedPtr<FCompositePassTreeFilter>> PassTypeFilters;
	TSharedPtr<FTreeFilter> TreeTextFilter;
	
	TSharedPtr<SFilterSearchBox> TextFilterSearchBox;
	TSharedPtr<SCompositePassesFilterBar<FFilterType>> FilterBar;

	bool bFilterChangedGuard = false;
	
	FOnFilterChanged OnFilterChanged;
	FOnPassAdded OnPassAdded;
	FOnFilterNewPassType OnFilterNewPassType;
};

/** Table row that displays the pass tree item content in the tree view */
class SCompositePassTreeItemRow : public SMultiColumnTableRow<SCompositePassTree::FPassTreeItemPtr>
{
	using FTreeItem = SCompositePassTree::FPassTreeItem;
	using FTreeItemPtr = SCompositePassTree::FPassTreeItemPtr;
	using FTreeItemWeakPtr = TWeakPtr<FTreeItem>;
	
public:
	DECLARE_DELEGATE_ThreeParams(FOnPassMoved, const FTreeItemPtr& /* InItemToMove */, SCompositePassTree::EPassType /* InDestPassType */, int32 /* InDestIndex */)

private:
	/** Drag/drop operation for changing the order of passes in the tree view */
	class FPassDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FCompositeLayerDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FPassDragDropOp> New(const FTreeItemPtr& InDraggedItem)
		{
			TSharedRef<FPassDragDropOp> Operation = MakeShared<FPassDragDropOp>();
			Operation->DraggedItem = InDraggedItem;
			Operation->Construct();
			return Operation;
		}

		virtual void Construct() override
		{
			if (FTreeItemPtr PinnedItem = DraggedItem.Pin())
			{
				if (UCompositePassBase* Pass = PinnedItem->GetPass())
				{
					const FText PassName = Pass->GetClass()->GetDisplayNameText();
					const FSlateBrush* PassIcon = FSlateIconFinder::FindIconForClass(Pass->GetClass()).GetIcon();
					SetToolTip(PassName, PassIcon);
				}
			}
			
			FDecoratedDragDropOp::Construct();
		}

		FTreeItemWeakPtr DraggedItem;
	};
	
public:
	SLATE_BEGIN_ARGS(SCompositePassTreeItemRow) { }
		SLATE_EVENT(FOnPassMoved, OnPassMoved)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, FTreeItemPtr InTreeItem)
	{
		TreeItem = InTreeItem;
		OnPassMoved = InArgs._OnPassMoved;

		STableRow<FTreeItemPtr>::FArguments Args = FSuperRowType::FArguments()
           .Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"))
           .OnDragDetected(this, &SCompositePassTreeItemRow::HandleDragDetected)
           .OnCanAcceptDrop(this, &SCompositePassTreeItemRow::HandleCanAcceptDrop)
           .OnAcceptDrop(this, &SCompositePassTreeItemRow::HandleAcceptDrop);
		
		SMultiColumnTableRow::Construct(Args, InOwnerTable);
	}

	/** Sets the name text block to be in edit mode */
	void RequestRename()
	{
		if (NameTextBlock.IsValid())
		{
			NameTextBlock->EnterEditingMode();
		}
	}
	
private:
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (InColumnName == CompositePassTree::PassTreeColumn_Enabled)
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.HAlign(HAlign_Center)
				.Padding(3.5f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SCompositePassTreeItemRow::IsItemEnabled)
					.OnCheckStateChanged(this, &SCompositePassTreeItemRow::OnIsItemEnabledChanged)
				];
		}
		if (InColumnName == CompositePassTree::PassTreeColumn_Name)
		{
			return SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.f, 0.f, 0.f, 0.f)
				[
					SNew(SExpanderArrow, SharedThis(this)).IndentAmount(12)
				]
				
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 1.0f, 6.0f, 1.0f)
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					[
						SNew(SImage).Image(this, &SCompositePassTreeItemRow::GetItemIcon)
					]
				]

				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SAssignNew(NameTextBlock, SInlineEditableTextBlock)
					.Text(this, &SCompositePassTreeItemRow::GetItemName)
					.OnTextCommitted(this, &SCompositePassTreeItemRow::SetItemName)
					.IsReadOnly(this, &SCompositePassTreeItemRow::IsItemNameReadOnly)
					.IsSelected(FIsSelected::CreateSP(this, &SCompositePassTreeItemRow::IsSelectedExclusively))
				];
		}
		if (InColumnName == CompositePassTree::PassTreeColumn_Type)
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(4.5f, 0.0f)
				[
					SNew(STextBlock).Text(this, &SCompositePassTreeItemRow::GetItemType)
				];
		}
		
		return SNullWidget::NullWidget;
	}

	/** Gets the checkbox state for the row item's Enabled flag */
	ECheckBoxState IsItemEnabled() const
	{
		if (!TreeItem->Plate.IsValid())
		{
			return ECheckBoxState::Unchecked;
		}
		
		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				return Pass->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
			
			// In the case where there is a valid pass index, but the pass itself is null
			return ECheckBoxState::Unchecked;
		}
		
		// Tree item is a pass type group item, so set its checkbox state as the aggregate of all passes of that type
		TArray<TObjectPtr<UCompositePassBase>>& AllPassesOfType = SCompositePassTree::GetPassList(TreeItem->Plate, TreeItem->PassType);

		bool bAnyEnabled = false;
		bool bAnyDisabled = false;
		for (const TObjectPtr<UCompositePassBase>& Pass : AllPassesOfType)
		{
			if (!IsValid(Pass))
			{
				continue;
			}
			
			if (Pass->IsEnabled())
			{
				bAnyEnabled = true;
			}
			else
			{
				bAnyDisabled = true;
			}
		}

		if (bAnyEnabled)
		{
			return bAnyDisabled ? ECheckBoxState::Undetermined : ECheckBoxState::Checked;
		}
		else
		{
			return bAnyDisabled ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
		}
	}

	/** Raised when the row item's Enabled checkbox has changed state */
	void OnIsItemEnabledChanged(ECheckBoxState InCheckBoxState)
	{
		if (!TreeItem->Plate.IsValid())
		{
			return;
		}
		
		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				FScopedTransaction SetEnabledTransaction(LOCTEXT("SetEnabledTransaction", "Set Enabled"));
				Pass->Modify();

				Pass->SetEnabled(InCheckBoxState == ECheckBoxState::Checked);
				return;
			}

			// In the case where there is a valid pass index, but the pass itself is null
			return;
		}
		
		// Tree item is a pass type group item, so set enable state of all passes of type
		TArray<TObjectPtr<UCompositePassBase>>& AllPassesOfType = SCompositePassTree::GetPassList(TreeItem->Plate, TreeItem->PassType);
		if (AllPassesOfType.IsEmpty())
		{
			return;
		}
		
		FScopedTransaction SetAllEnabledTransaction(LOCTEXT("SetAllEnabledTransaction", "Set All Enabled"));
		for (const TObjectPtr<UCompositePassBase>& Pass : AllPassesOfType)
		{
			Pass->Modify();
			Pass->SetEnabled(InCheckBoxState == ECheckBoxState::Checked);
		}
	}
	
	/** Gets the row item's icon to display next to its name */
	const FSlateBrush* GetItemIcon() const
	{
		if (!TreeItem->Plate.IsValid())
		{
			return nullptr;
		}
		
		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				return FSlateIconFinder::FindIconForClass(Pass->GetClass()).GetIcon();
			}
			
			// In the case where there is a valid pass index, but the pass itself is null
			return nullptr;
		}

		// Tree item is a pass type group item, so return icon for pass type
		static const FSlateBrush* PassTypeIcons[] =
		{
			FCompositeEditorStyle::Get().GetBrush("CompositeEditor.Passes.Media"),
			//FCompositeEditorStyle::Get().GetBrush("CompositeEditor.Passes.Scene"),
			FCompositeEditorStyle::Get().GetBrush("CompositeEditor.Passes.Layer")
		};
		
		return PassTypeIcons[(int32)TreeItem->PassType];
	}

	/** Gets the row item's display name */
	FText GetItemName() const
	{
		if (!TreeItem->Plate.IsValid())
		{
			return FText::GetEmpty();
		}
		
		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				return FText::FromString(Pass->GetDisplayName());
			}
			
			// In the case where there is a valid pass index, but the pass itself is null
			return LOCTEXT("PassNotSetLabel", "Pass not set");
		}

		// Tree item is a pass type group item, so return label for pass type
		static FText PassTypeLabels[] =
		{
			LOCTEXT("MediaPassTypeLabel", "Media Passes"),
			//LOCTEXT("ScenePassTypeLabel", "Scene Passes"),
			LOCTEXT("LayerPassTypeLabel", "Layer Passes"),
		};
		
		return PassTypeLabels[(int32)TreeItem->PassType];
	}

	/** Sets this row item's display name */
	void SetItemName(const FText& InNewText, ETextCommit::Type InCommitType)
	{
		if (!TreeItem->Plate.IsValid())
		{
			return;
		}
		
		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				FScopedTransaction SetPassNameTransaction(LOCTEXT("SetPassNameTransaction", "Set Name"));
				Pass->Modify();

				Pass->SetDisplayName(InNewText.ToString());
			}
		}
	}

	/** Gets whether the row item's display name can be changed */
	bool IsItemNameReadOnly() const
	{
		if (!TreeItem->Plate.IsValid())
		{
			return true;
		}

		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				return false;
			}
		}

		return true;
	}
	
	/** Gets the row item's display name */
	FText GetItemType() const
	{
		if (!TreeItem->Plate.IsValid())
		{
			return FText::GetEmpty();
		}
		
		if (TreeItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = TreeItem->GetPass())
			{
				return Pass->GetClass()->GetDisplayNameText();
			}
			
			// In the case where there is a valid pass index, but the pass itself is null
            return LOCTEXT("NoneTypeLabel", "None");
		}
		
		return FText::GetEmpty();
	}

	/** Raised when a drag start has been detected on this row item */
	FReply HandleDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
	{
		if (!TreeItem->Plate.IsValid())
		{
			return FReply::Unhandled();
		}

		if (!TreeItem->HasValidPassIndex())
		{
			return FReply::Unhandled();
		}
		
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			TSharedPtr<FDragDropOperation> DragDropOp = FPassDragDropOp::New(TreeItem);
			return FReply::Handled().BeginDragDrop(DragDropOp.ToSharedRef());
		}

		return FReply::Unhandled();
	}

	/** Raised when the user is attempting to drop something onto this item */
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InItemDropZone, FTreeItemPtr InTreeItem)
	{
		TSharedPtr<FPassDragDropOp> DragDropOp = InDragDropEvent.GetOperationAs<FPassDragDropOp>();

		if (!DragDropOp.IsValid() || !DragDropOp->DraggedItem.IsValid())
		{
			return TOptional<EItemDropZone>();
		}

		FTreeItemPtr DraggedItem = DragDropOp->DraggedItem.Pin();
		if (DraggedItem->Plate.IsValid())
		{
			if (UCompositePassBase* Pass = DraggedItem->GetPass())
			{
				// PPM passes can only be added to the Layer passes list
				if (Pass->GetClass()->IsChildOf<UCompositePassMaterial>() && InTreeItem->PassType != SCompositePassTree::EPassType::Layer)
				{
					return TOptional<EItemDropZone>();
				}
			}
			
			if (!InTreeItem->HasValidPassIndex())
			{
				return EItemDropZone::BelowItem;
			}

			return InItemDropZone == EItemDropZone::OntoItem ? EItemDropZone::AboveItem : InItemDropZone;
		}
		
		return TOptional<EItemDropZone>();
	}

	/** Raised when the user has dropped something onto this item */
	FReply HandleAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InItemDropZone, FTreeItemPtr InTreeItem)
	{
		TSharedPtr<FPassDragDropOp> DragDropOp = InDragDropEvent.GetOperationAs<FPassDragDropOp>();

		if (!DragDropOp.IsValid() || !DragDropOp->DraggedItem.IsValid())
		{
			return FReply::Unhandled();
		}

		if (OnPassMoved.IsBound())
		{
			int32 DestIndex = InItemDropZone == EItemDropZone::AboveItem ? InTreeItem->PassIndex: InTreeItem->PassIndex + 1;

			// If the destination is further down than the item being moved, we must subtract one from the destination index
			// to account for the removal of the original item
			if (InTreeItem->PassIndex > DragDropOp->DraggedItem.Pin()->PassIndex && InTreeItem->PassType == DragDropOp->DraggedItem.Pin()->PassType)
			{
				--DestIndex;
			}
			
			OnPassMoved.Execute(DragDropOp->DraggedItem.Pin(), InTreeItem->PassType, DestIndex);
			return FReply::Handled();
		}
		
		return FReply::Unhandled();
	}
	
private:
	/** The tree item this widget is outputting */
	FTreeItemPtr TreeItem;

	/** The editable text block displaying the item's name */
	TSharedPtr<SInlineEditableTextBlock> NameTextBlock;
	
	/** Callback that is raised when a layer is moved via a drag and drop operation */
	FOnPassMoved OnPassMoved;
};

bool SCompositePassTree::FPassTreeItem::HasValidPassIndex() const
{
	if (!Plate.IsValid())
	{
		return false;
	}

	TArray<TObjectPtr<UCompositePassBase>>& PassList = SCompositePassTree::GetPassList(Plate, PassType);
	return PassList.IsValidIndex(PassIndex);
}

UCompositePassBase* SCompositePassTree::FPassTreeItem::GetPass() const
{
	if (!HasValidPassIndex())
	{
		return nullptr;
	}

	TArray<TObjectPtr<UCompositePassBase>>& PassList = SCompositePassTree::GetPassList(Plate, PassType);
	return PassList[PassIndex];
}

void SCompositePassTree::Construct(const FArguments& InArgs, const TWeakObjectPtr<UCompositeLayerPlate>& InPlate)
{
	Plate = InPlate;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	OnLayoutChanged = InArgs._OnLayoutChanged;

	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SCompositePassTree::OnObjectPropertyChanged);
	
	CommandList = MakeShared<FUICommandList>();
	BindCommands();
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SAssignNew(Toolbar, SCompositePassTreeToolbar)
			.OnFilterChanged(this, &SCompositePassTree::OnFilterChanged)
			.OnPassAdded(this, &SCompositePassTree::OnPassAdded)
			.OnFilterNewPassType(this, &SCompositePassTree::OnFilterNewPassType)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(TreeView, STreeView<FPassTreeItemPtr>)
			.TreeItemsSource(&FilteredPassTreeItems)
			.HeaderRow(
				SNew(SHeaderRow)

				+SHeaderRow::Column(CompositePassTree::PassTreeColumn_Enabled)
				.DefaultLabel(FText::GetEmpty())
				.FixedWidth(24.0f)
				.HAlignHeader(HAlign_Center)
				.VAlignHeader(VAlign_Center)
				.HAlignCell(HAlign_Center)
				.VAlignCell(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SCompositePassTree::GetGlobalEnabledState)
					.OnCheckStateChanged(this, &SCompositePassTree::OnGlobalEnabledStateChanged)
				]

				+SHeaderRow::Column(CompositePassTree::PassTreeColumn_Name)
				.FillWidth(0.55)
				.DefaultLabel(LOCTEXT("PassNameColumnLabel", "Pass Name"))

				+SHeaderRow::Column(CompositePassTree::PassTreeColumn_Type)
				.FillWidth(0.45)
				.DefaultLabel(LOCTEXT("PassTypeColumnLabel", "Pass Type"))
			)
			.OnGenerateRow_Lambda([this](FPassTreeItemPtr InTreeItem, const TSharedRef<STableViewBase>& InOwnerTable)
			{
				return SNew(SCompositePassTreeItemRow, InOwnerTable, InTreeItem)
					.OnPassMoved(this, &SCompositePassTree::OnPassMoved);
			})
			.OnGetChildren_Lambda([](FPassTreeItemPtr InTreeItem, TArray<FPassTreeItemPtr>& OutChildren)
			{
				if (InTreeItem.IsValid())
				{
					for (FPassTreeItemPtr& Child : InTreeItem->Children)
					{
						if (Child.IsValid() && !Child->bFilteredOut)
						{
							OutChildren.Add(Child);
						}
					}
				}
			})
			.OnSelectionChanged(this, &SCompositePassTree::OnPassSelectionChanged)
			.OnContextMenuOpening(this, &SCompositePassTree::CreateTreeContextMenu)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.MinHeight(24.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SCompositePassTree::GetFilterStatusText)
			.ColorAndOpacity(this, &SCompositePassTree::GetFilterStatusColor)
		]
	];

	FillPassTreeItems();
}

SCompositePassTree::~SCompositePassTree()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

FReply SCompositePassTree::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

TArray<TObjectPtr<UCompositePassBase>>& SCompositePassTree::GetPassList(TWeakObjectPtr<UCompositeLayerPlate> InPlate, EPassType InPassType)
{
	check(InPlate.IsValid());
	
	switch (InPassType)
	{
	case EPassType::Media:
		return InPlate->MediaPasses;

	// case EPassType::Scene:
	// return InPlate->ScenePasses;
		
	case EPassType::Layer:
	default:
		return InPlate->LayerPasses;
	}
}

FProperty* SCompositePassTree::GetPassListProperty(EPassType InPassType)
{
	switch (InPassType)
	{
	case EPassType::Media:
		return FindFProperty<FProperty>(UCompositeLayerPlate::StaticClass(), GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, MediaPasses));

	// case EPassType::Scene:
	// return FindFProperty<FProperty>(UCompositeLayerPlate::StaticClass(), GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, ScenePasses));
		
	case EPassType::Layer:
	default:
		return FindFProperty<FProperty>(UCompositeLayerPlate::StaticClass(), GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, LayerPasses));
	}
}

void SCompositePassTree::BindCommands()
{
	CommandList->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SCompositePassTree::CopySelectedItems),
		FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanCopySelectedItems));

	CommandList->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SCompositePassTree::CutSelectedItems),
		FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanCutSelectedItems));

	CommandList->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SCompositePassTree::PasteSelectedItems),
		FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanPasteSelectedItems));

	CommandList->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SCompositePassTree::DuplicateSelectedItems),
		FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanDuplicateSelectedItems));
	
	CommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SCompositePassTree::DeleteSelectedItems),
		FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanDeleteSelectedItems));

	CommandList->MapAction(FGenericCommands::Get().Rename,
			FUIAction(FExecuteAction::CreateSP(this, &SCompositePassTree::RenameSelectedItem),
			FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanRenameSelectedItem)));
	
	CommandList->MapAction(FCompositeEditorCommands::Get().Enable,
		FExecuteAction::CreateSP(this, &SCompositePassTree::EnableSelectedItems),
		FCanExecuteAction::CreateSP(this, &SCompositePassTree::CanEnableSelectedItems));
}

void SCompositePassTree::FillPassTreeItems()
{
	PassTreeItems.Empty();

	if (Plate.IsValid())
	{
		for (int32 TypeIndex = 0; TypeIndex < (int32)EPassType::MAX; ++TypeIndex)
		{
			const EPassType PassType = (EPassType)TypeIndex;
			FPassTreeItemPtr NewPassTypeTreeItem = MakeShared<FPassTreeItem>();
			NewPassTypeTreeItem->Plate = Plate;
			NewPassTypeTreeItem->PassType = PassType;
			
			PassTreeItems.Add(NewPassTypeTreeItem);
			
			TArray<TObjectPtr<UCompositePassBase>>& Passes = GetPassList(PassType);
			for (int32 Index = 0; Index < Passes.Num(); ++Index)
			{
				FPassTreeItemPtr NewPassTreeItem = MakeShared<FPassTreeItem>();
				NewPassTreeItem->Plate = Plate;
				NewPassTreeItem->PassType = PassType;
				NewPassTreeItem->PassIndex = Index;

				NewPassTypeTreeItem->Children.Add(NewPassTreeItem);
			}
		}
	}

	FilterPassTreeItems();
	RefreshAndExpandTreeView();
}

void SCompositePassTree::FilterPassTreeItems()
{
	FilteredPassTreeItems.Empty();

	if (!Toolbar.IsValid())
	{
		return;
	}
	
	for (FPassTreeItemPtr& TreeItem : PassTreeItems)
	{
		TreeItem->bFilteredOut = !Toolbar->ItemPassesFilters(TreeItem);

		bool bHasUnfilteredChildren = false;
		for (FPassTreeItemPtr& ChildTreeItem : TreeItem->Children)
		{
			ChildTreeItem->bFilteredOut = !Toolbar->ItemPassesFilters(ChildTreeItem);
			if (!ChildTreeItem->bFilteredOut)
			{
				bHasUnfilteredChildren = true;
			}
		}

		if (!TreeItem->bFilteredOut || bHasUnfilteredChildren)
		{
			FilteredPassTreeItems.Add(TreeItem);
		}
	}
}

void SCompositePassTree::RefreshPassType(EPassType InPassType, bool bRefreshTreeView)
{
	FPassTreeItemPtr PassTypeTreeItem = PassTreeItems[(int32)InPassType];
	PassTypeTreeItem->Children.Empty();

	TArray<TObjectPtr<UCompositePassBase>>& Passes = GetPassList(InPassType);
	for (int32 Index = 0; Index < Passes.Num(); ++Index)
	{
		FPassTreeItemPtr NewPassTreeItem = MakeShared<FPassTreeItem>();
		NewPassTreeItem->Plate = Plate;
		NewPassTreeItem->PassType = InPassType;
		NewPassTreeItem->PassIndex = Index;

		PassTypeTreeItem->Children.Add(NewPassTreeItem);
	}

	FilterPassTreeItems();
	
	if (bRefreshTreeView)
	{
		RefreshAndExpandTreeView();
	}
}

void SCompositePassTree::RefreshAndExpandTreeView()
{
	if (TreeView.IsValid())
	{
		TreeView->RebuildList();

		for (int32 Index = 0; Index < FilteredPassTreeItems.Num(); ++Index)
		{
			TreeView->SetItemExpansion(FilteredPassTreeItems[Index], true);
		}

		OnLayoutChanged.ExecuteIfBound();
	}
}

TSharedPtr<SWidget> SCompositePassTree::CreateTreeContextMenu()
{
	if (!TreeView.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	
	const int32 NumItems = TreeView->GetNumItemsSelected();
	if (NumItems >= 1)
	{
		constexpr bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);

		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(FCompositeEditorCommands::Get().Enable);
		
		return MenuBuilder.MakeWidget();
	}

	return SNullWidget::NullWidget;
}

ECheckBoxState SCompositePassTree::GetGlobalEnabledState() const
{
	if (!Plate.IsValid())
	{
		return ECheckBoxState::Checked;
	}

	// Determine if all passes are disabled, enabled, or if there is a mix
	bool bAnyPassesEnabled = false;
	bool bAnyPassesDisabled = false;
	
	for (int32 Index = 0; Index < (int32)EPassType::MAX; ++Index)
	{
		const TArray<TObjectPtr<UCompositePassBase>>& PassList = GetPassList((EPassType)Index);

		// Consider any pass list that is empty as enabled to align with how the check state of the pass type tree item works
		if (PassList.IsEmpty())
		{
			bAnyPassesEnabled = true;
		}
		
		for (const TObjectPtr<UCompositePassBase>& Pass : PassList)
		{
			if (!IsValid(Pass))
			{
				continue;
			}
			
			if (Pass->IsEnabled())
			{
				bAnyPassesEnabled = true;
			}
			else
			{
				bAnyPassesDisabled = true;
			}
		}
	}

	if (bAnyPassesEnabled)
	{
		return bAnyPassesDisabled ? ECheckBoxState::Undetermined : ECheckBoxState::Checked;
	}
	else
	{
		return bAnyPassesDisabled ? ECheckBoxState::Unchecked : ECheckBoxState::Undetermined;
	}
}

void SCompositePassTree::OnGlobalEnabledStateChanged(ECheckBoxState CheckBoxState)
{
	if (!Plate.IsValid())
	{
		return;
	}

	TSharedPtr<FScopedTransaction> AllEnabledTransaction;
	
	for (int32 Index = 0; Index < (int32)EPassType::MAX; ++Index)
	{
		const TArray<TObjectPtr<UCompositePassBase>>& PassList = GetPassList((EPassType)Index);

		for (const TObjectPtr<UCompositePassBase>& Pass : PassList)
		{
			if (!AllEnabledTransaction.IsValid())
			{
				AllEnabledTransaction = MakeShared<FScopedTransaction>(LOCTEXT("AllEnabledTransaction", "Set All Enabled"));
				Plate->Modify();
			}

			Pass->Modify();
			Pass->SetEnabled(CheckBoxState == ECheckBoxState::Checked);
		}
	}
}

void SCompositePassTree::OnPassMoved(const TSharedPtr<FPassTreeItem>& InTreeItem, EPassType InDestPassType, int InDestIndex)
{
	if (!InTreeItem.IsValid() ||
		!InTreeItem->Plate.IsValid() ||
		!InTreeItem->HasValidPassIndex())
	{
		return;
	}

	FScopedTransaction MovePassTransaction(LOCTEXT("MovePassTransaction", "Move Pass"));
	InTreeItem->Plate->Modify();
	
	TArray<TObjectPtr<UCompositePassBase>>& SourcePassList = GetPassList(InTreeItem->Plate, InTreeItem->PassType);
	TArray<TObjectPtr<UCompositePassBase>>& DestPassList = GetPassList(InTreeItem->Plate, InDestPassType);

	FProperty* SourcePassListProperty = GetPassListProperty(InTreeItem->PassType);
	FProperty* DestPassListProperty = GetPassListProperty(InDestPassType);
	
	UCompositePassBase* Pass = SourcePassList[InTreeItem->PassIndex];
	
	InTreeItem->Plate->PreEditChange(SourcePassListProperty);
	SourcePassList.RemoveAt(InTreeItem->PassIndex);

	// If we aren't adding the item back to the same pass list, invoke a property changed event for its removal
	if (InTreeItem->PassType != InDestPassType)
	{
		FPropertyChangedEvent SourceListChangedEvent(SourcePassListProperty, EPropertyChangeType::ArrayRemove);
		InTreeItem->Plate->PostEditChangeProperty(SourceListChangedEvent);

		InTreeItem->Plate->PreEditChange(DestPassListProperty);
	}
	
	DestPassList.Insert(Pass, InDestIndex);

	// If the item moved from one list to another, invoke a property changed event for the add; otherwise, invoke a move event
	if (InTreeItem->PassType != InDestPassType)
	{
		FPropertyChangedEvent DestListChangedEvent(DestPassListProperty, EPropertyChangeType::ArrayAdd);
		InTreeItem->Plate->PostEditChangeProperty(DestListChangedEvent);
	}
	else
	{
		FPropertyChangedEvent MoveChangedEvent(SourcePassListProperty, EPropertyChangeType::ArrayMove);
		InTreeItem->Plate->PostEditChangeProperty(MoveChangedEvent);
	}
	
	RefreshPassType(InTreeItem->PassType, /* bRefreshTreeView */ false);
	RefreshPassType(InDestPassType);
}

void SCompositePassTree::OnPassSelectionChanged(TSharedPtr<FPassTreeItem> InTreeItem, ESelectInfo::Type SelectInfo)
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	
	TArray<UObject*> SelectedLayers;
	SelectedLayers.Reserve(SelectedItems.Num());

	for (const FPassTreeItemPtr& Item : SelectedItems)
	{
		if (!Item->Plate.IsValid())
		{
			continue;
		}

		if (Item->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = Item->GetPass())
			{
				SelectedLayers.Add(Pass);
			}
		}
	}
	
	OnSelectionChanged.ExecuteIfBound(SelectedLayers);
}

void SCompositePassTree::OnFilterChanged()
{
	FilterPassTreeItems();
	RefreshAndExpandTreeView();
}

void SCompositePassTree::OnPassAdded(const UClass* InPassClass)
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	if (SelectedItems.IsEmpty())
	{
		// If there are no selected items, add the pass to the media passes list, unless it is a PPM pass, which can only get added to the layers passes list
		if (InPassClass->IsChildOf<UCompositePassMaterial>())
		{
			SelectedItems.Add(PassTreeItems[1]);
		}
		else
		{
			SelectedItems.Add(PassTreeItems[0]);
		}
	}

	// Item after which the new pass will get added
	FPassTreeItemPtr AnchorItem = SelectedItems.Last();
	if (!AnchorItem.IsValid() || !AnchorItem->Plate.IsValid())
	{
		return;
	}

	// Post-process material pass can only be added to the Layer passes list
	if (InPassClass->IsChildOf<UCompositePassMaterial>() && AnchorItem->PassType != EPassType::Layer)
	{
		return;
	}
	
	FScopedTransaction AddPassTransaction(LOCTEXT("AddPassTransaction", "Add Pass"));
	AnchorItem->Plate->Modify();
	
	UCompositePassBase* NewPass = NewObject<UCompositePassBase>(AnchorItem->Plate.Get(), InPassClass, NAME_None, RF_Transactional);
	TArray<TObjectPtr<UCompositePassBase>>& PassList = GetPassList(AnchorItem->Plate, AnchorItem->PassType);

	FProperty* PassListProperty = GetPassListProperty(AnchorItem->PassType);
	AnchorItem->Plate->PreEditChange(PassListProperty);
	
	int32 NewPassIndex = PassList.Insert(NewPass, AnchorItem->PassIndex + 1);

	FPropertyChangedEvent PropertyChangedEvent(PassListProperty, EPropertyChangeType::ArrayAdd);
	AnchorItem->Plate->PostEditChangeProperty(PropertyChangedEvent);

	if (Toolbar.IsValid())
	{
		Toolbar->ClearFilters();
	}
	
	RefreshPassType(AnchorItem->PassType);

	FPassTreeItemPtr PassToSelect = PassTreeItems[(int32)AnchorItem->PassType]->Children[NewPassIndex];
	if (TreeView.IsValid())
	{
		TreeView->ClearSelection();
		TreeView->SetItemSelection(PassToSelect, true);
	}
}

bool SCompositePassTree::OnFilterNewPassType(const UClass* InPassType) const
{
	// Filter out Distortion passes as those are implicitly added when there is a lens file on the composite actor camera
	if (InPassType->IsChildOf<UCompositePassDistortion>())
	{
		return true;
	}

	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	const bool bLayerPassesSelected = SelectedItems.IsEmpty() || SelectedItems.ContainsByPredicate([](const FPassTreeItemPtr& InSelectedItem)
	{
		return InSelectedItem->PassType == EPassType::Layer;
	});

	// Post-process material pass can only be added to the Layer passes list
	if (!bLayerPassesSelected && InPassType->IsChildOf<UCompositePassMaterial>())
	{
		return true;
	}
	
	return false;
}

FText SCompositePassTree::GetFilterStatusText() const
{
	int32 NumPasses = 0;
	for (const FPassTreeItemPtr& PassTypeItem : PassTreeItems)
	{
		NumPasses += PassTypeItem->Children.Num();
	}

	int32 NumFilteredPasses = 0;
	for (const FPassTreeItemPtr& PassTypeItem : FilteredPassTreeItems)
	{
		for (const FPassTreeItemPtr& PassItem : PassTypeItem->Children)
		{
			if (!PassItem->bFilteredOut)
			{
				++NumFilteredPasses;
			}
		}
	}
	
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	int32 NumSelectedPasses = 0;
	for (const FPassTreeItemPtr& PassItem : SelectedItems)
	{
		if (PassItem->HasValidPassIndex())
		{
			++NumSelectedPasses;
		}
	}
	
	const FText PassLabel = NumPasses > 1 ? LOCTEXT("PassPlural", "Passes") : LOCTEXT("PassSingular", "Pass");
	if (NumPasses > 0 && NumPasses == NumFilteredPasses)
	{
		if (NumSelectedPasses > 0)
		{
			return FText::Format(LOCTEXT("NumPassesAndSelectedTextFormat", "{0} {1}, {2} Selected"), FText::AsNumber(NumPasses), PassLabel, FText::AsNumber(NumSelectedPasses));
		}
		else
		{
			return FText::Format(LOCTEXT("NumPassesTextFormat", "{0} {1}"), FText::AsNumber(NumPasses), PassLabel);
		}
	}
	else if (NumFilteredPasses > 0)
	{
		if (NumSelectedPasses > 0)
		{
			return FText::Format(LOCTEXT("NumPassesWithFilteredAndSelectedTextFormat", "Showing {0} of {1} {2}, {3} Selected"),
				FText::AsNumber(NumSelectedPasses),
				FText::AsNumber(NumPasses),
				PassLabel,
				FText::AsNumber(NumSelectedPasses));
		}
		else
		{
			return FText::Format(LOCTEXT("NumPassesWithFilteredTextFormat", "Showing {0} of {1} {2}"),
				FText::AsNumber(NumFilteredPasses),
				FText::AsNumber(NumPasses),
				PassLabel);
		}
	}
	else
	{
		if (NumPasses > 0)
		{
			return FText::Format(LOCTEXT("NoMatchingPassesTextFormat", "No matching passes ({0} {1})"), FText::AsNumber(NumPasses), PassLabel);
		}
		else
		{
			return LOCTEXT("NoPassesLabel", "0 Passes");
		}
	}
}

FSlateColor SCompositePassTree::GetFilterStatusColor() const
{
	int32 NumPasses = 0;
	for (const FPassTreeItemPtr& PassTypeItem : PassTreeItems)
	{
		NumPasses += PassTypeItem->Children.Num();
	}

	int32 NumFilteredPasses = 0;
	for (const FPassTreeItemPtr& PassTypeItem : FilteredPassTreeItems)
	{
		for (const FPassTreeItemPtr& PassItem : PassTypeItem->Children)
		{
			if (!PassItem->bFilteredOut)
			{
				++NumFilteredPasses;
			}
		}
	}

	if (NumPasses > 0 && NumFilteredPasses == 0)
	{
		return FAppStyle::Get().GetSlateColor("Colors.AccentRed");
	}
	
	return FSlateColor::UseForeground();
}

void SCompositePassTree::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (!Plate.IsValid())
	{
		return;
	}

	if (InObject != Plate.Get())
	{
		return;
	}

	const FName PropName = InPropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, MediaPasses) ||
		PropName == GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, ScenePasses) ||
		PropName == GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, LayerPasses))
	{
		FillPassTreeItems();
	}
}

void SCompositePassTree::CopySelectedItems()
{
	TArray<UObject*> ObjectsToCopy;

	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	for (const FPassTreeItemPtr& SelectedItem : SelectedItems)
	{
		if (SelectedItem.IsValid() && SelectedItem->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = SelectedItem->GetPass())
			{
				ObjectsToCopy.Add(Pass);
			}
		}
	}

	if (ObjectsToCopy.IsEmpty())
	{
		return;
	}
	
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	FStringOutputDevice Archive;
	const FExportObjectInnerContext Context;

	for (UObject* Object : ObjectsToCopy)
	{
		UExporter::ExportToOutputDevice(&Context, Object, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, nullptr);
	}

	FPlatformApplicationMisc::ClipboardCopy(*Archive);
}

bool SCompositePassTree::CanCopySelectedItems()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	if (!SelectedItems.Num())
	{
		return false;
	}

	const bool bContainsPass = SelectedItems.ContainsByPredicate([](const FPassTreeItemPtr& InTreeItem)
	{
		return InTreeItem.IsValid() && InTreeItem->HasValidPassIndex() && InTreeItem->GetPass();
	});

	return bContainsPass;
}

void SCompositePassTree::CutSelectedItems()
{
	CopySelectedItems();
	DeleteSelectedItems();
}

bool SCompositePassTree::CanCutSelectedItems()
{
	return CanCopySelectedItems() && CanDeleteSelectedItems();
}

class FCompositePassObjectTextFactory : public FCustomizableTextObjectFactory
{
public:
	FCompositePassObjectTextFactory() : FCustomizableTextObjectFactory(GWarn) { }

	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		return ObjectClass->IsChildOf(UCompositePassBase::StaticClass());
	}

	virtual void ProcessConstructedObject(UObject* NewObject) override
	{
		check(NewObject);

		if (UCompositePassBase* Pass = Cast<UCompositePassBase>(NewObject))
		{
			CompositePasses.Add(Pass);
		}
	}

public:
	TArray<UCompositePassBase*> CompositePasses;
};

void SCompositePassTree::PasteSelectedItems()
{
	if (!Plate.IsValid())
	{
		return;
	}
	
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	if (SelectedItems.IsEmpty())
	{
		return;
	}

	// A list of pass types to paste the clipboard contents in, and the index to paste at
	TMap<EPassType, int32> PassTypesToPasteTo;
	for (const FPassTreeItemPtr& SelectedItem : SelectedItems)
	{
		if (SelectedItem.IsValid())
		{
			if (PassTypesToPasteTo.Contains(SelectedItem->PassType))
			{
				PassTypesToPasteTo[SelectedItem->PassType] = FMath::Max(SelectedItem->PassIndex + 1, PassTypesToPasteTo[SelectedItem->PassType]);
			}
			else
			{
				PassTypesToPasteTo.Add(SelectedItem->PassType, SelectedItem->PassIndex + 1);
			}
		}
	}

	if (PassTypesToPasteTo.IsEmpty())
	{
		return;
	}

	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	
	FCompositePassObjectTextFactory Factory;
	Factory.ProcessBuffer(GetTransientPackage(), RF_Transactional, ClipboardContent);

	if (Factory.CompositePasses.IsEmpty())
	{
		return;
	}

	TMap<EPassType, TArray<int32>> PassesToSelect;
	FScopedTransaction PastePassesTransaction(LOCTEXT("PastePassesTransaction", "Paste Passes"));
	Plate->Modify();
	for (const TPair<EPassType, int32>& Pair : PassTypesToPasteTo)
	{
		EPassType PassTypeToPasteAt = Pair.Key;
		int32 IndexToPasteAt = Pair.Value;

		TArray<TObjectPtr<UCompositePassBase>>& PassList = GetPassList(PassTypeToPasteAt);
		
		PassesToSelect.Add(PassTypeToPasteAt, TArray<int32>());
		
		FProperty* PassListProperty = GetPassListProperty(PassTypeToPasteAt);
		Plate->PreEditChange(PassListProperty);
		
		for (UCompositePassBase* PassToPaste : Factory.CompositePasses)
		{
			UCompositePassBase* NewPass = DuplicateObject(PassToPaste, Plate.Get());
			
			PassList.Insert(NewPass, IndexToPasteAt);
			PassesToSelect[PassTypeToPasteAt].Add(IndexToPasteAt);
			++IndexToPasteAt;
		}

		FPropertyChangedEvent PropertyChangedEvent(PassListProperty, EPropertyChangeType::ArrayAdd);
		Plate->PostEditChangeProperty(PropertyChangedEvent);
		
		constexpr bool bRefreshTree = false;
		RefreshPassType(PassTypeToPasteAt, bRefreshTree);
	}

	FilterPassTreeItems();
	RefreshAndExpandTreeView();

	if (TreeView.IsValid())
	{
		for (const TPair<EPassType, TArray<int32>>& Pair : PassesToSelect)
		{
			EPassType PassType = Pair.Key;
			for (int32 IndexToSelect : Pair.Value)
			{
				if (PassTreeItems[(int32)PassType]->Children.IsValidIndex(IndexToSelect))
				{
					TreeView->SetItemSelection(PassTreeItems[(int32)PassType]->Children[IndexToSelect], true);
				}
			}
		}
	}
}

bool SCompositePassTree::CanPasteSelectedItems()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	if (SelectedItems.IsEmpty())
	{
		return false;
	}
	
	// Can't paste unless the clipboard has a string in it
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	if (!ClipboardContent.IsEmpty())
	{
		FCompositePassObjectTextFactory Factory;
		Factory.ProcessBuffer(GetTransientPackage(), RF_Transactional | RF_Transient, ClipboardContent);
		return !Factory.CompositePasses.IsEmpty();
	}

	return false;
}

void SCompositePassTree::DuplicateSelectedItems()
{
	CopySelectedItems();
	PasteSelectedItems();
}

bool SCompositePassTree::CanDuplicateSelectedItems()
{
	return CanCopySelectedItems();
}

void SCompositePassTree::DeleteSelectedItems()
{
	if (!Plate.IsValid())
	{
		return;
	}
	
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();

	TMap<EPassType, TArray<int32>> PassesToDelete;
	for (const FPassTreeItemPtr& Item : SelectedItems)
	{
		if (!Item.IsValid() || !Item->HasValidPassIndex())
		{
			continue;
		}

		if (PassesToDelete.Contains(Item->PassType))
		{
			PassesToDelete[Item->PassType].Add(Item->PassIndex);
		}
		else
		{
			PassesToDelete.Add(Item->PassType, TArray<int32> { Item->PassIndex });
		}
	}

	if (PassesToDelete.Num())
	{
		FScopedTransaction DeletePassTransaction(LOCTEXT("DeletePassTransaction", "Delete Pass"));
		Plate->Modify();
		
		for (TPair<EPassType, TArray<int32>>& PassesToDeleteByType : PassesToDelete)
		{
			const EPassType PassType = PassesToDeleteByType.Key;
			TArray<TObjectPtr<UCompositePassBase>>& PassList = GetPassList(PassType);
			
			// Delete the passes in reverse order so that indices don't get messed up as we delete
			TArray<int32>& PassIndicesToDelete = PassesToDeleteByType.Value;
			PassIndicesToDelete.Sort();

			FProperty* PassListProperty = GetPassListProperty(PassType);
			Plate->PreEditChange(PassListProperty);
			
			for (int32 Index = PassIndicesToDelete.Num() - 1; Index >= 0; --Index)
			{
				const int32 PassIndex = PassIndicesToDelete[Index];

				// Here we replicate details panels instanced property array removal code.
				// The removed instanced pass objects must be moved to the transient package.
				TObjectPtr<UCompositePassBase>& PassToRemove = PassList[PassIndex];
				if (IsValid(PassToRemove))
				{
					PassToRemove->Modify();
					PassToRemove->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
				}

				PassList.RemoveAt(PassIndex, EAllowShrinking::No);
			}

			PassList.Shrink();
			
			FPropertyChangedEvent PropertyChangedEvent(PassListProperty, EPropertyChangeType::ArrayAdd);
			Plate->PostEditChangeProperty(PropertyChangedEvent);
			
			constexpr bool bRefreshTreeView = false;
			RefreshPassType(PassType, bRefreshTreeView);
		}

		RefreshAndExpandTreeView();
	}
}

bool SCompositePassTree::CanDeleteSelectedItems()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	const bool bContainsPassInSelection = SelectedItems.ContainsByPredicate([](const FPassTreeItemPtr& InTreeItem)
	{
		return InTreeItem.IsValid() && InTreeItem->HasValidPassIndex();
	});
	
	return bContainsPassInSelection;
}

void SCompositePassTree::RenameSelectedItem()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	if (SelectedItems.Num() != 1 || !SelectedItems[0]->HasValidPassIndex() || !SelectedItems[0]->GetPass())
	{
		return;
	}
	
	TSharedPtr<ITableRow> RowWidget = TreeView->WidgetFromItem(SelectedItems[0]);
	if (TSharedPtr<SCompositePassTreeItemRow> TreeItemRowWidget = StaticCastSharedPtr<SCompositePassTreeItemRow>(RowWidget))
	{
		TreeItemRowWidget->RequestRename();
	}
}

bool SCompositePassTree::CanRenameSelectedItem()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	if (SelectedItems.Num() == 1)
	{
		return SelectedItems[0].IsValid() && SelectedItems[0]->HasValidPassIndex() && SelectedItems[0]->GetPass();
	}
	
	return false;
}

void SCompositePassTree::EnableSelectedItems()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();

	bool bAnyEnabled = false;
	for (const FPassTreeItemPtr& Item : SelectedItems)
	{
		if (Item->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = Item->GetPass())
			{
				if (Pass->IsEnabled())
				{
					bAnyEnabled = true;
				}
			}
		}
		else
		{
			for (const FPassTreeItemPtr& ChildItem : Item->Children)
			{
				if (ChildItem->HasValidPassIndex())
				{
					if (UCompositePassBase* Pass = ChildItem->GetPass())
					{
						if (Pass->IsEnabled())
						{
							bAnyEnabled = true;
						}
					}
				}
			}
		}
	}

	const bool bSetEnabled = !bAnyEnabled;

	FScopedTransaction SetEnabledTransaction(LOCTEXT("SetEnabledTransaction", "Set Enabled"));
	Plate->Modify();

	for (const FPassTreeItemPtr& Item : SelectedItems)
	{
		if (Item->HasValidPassIndex())
		{
			if (UCompositePassBase* Pass = Item->GetPass())
			{
				Pass->Modify();
				Pass->SetEnabled(bSetEnabled);
			}
		}
		else
		{
			for (const FPassTreeItemPtr& ChildItem : Item->Children)
			{
				if (ChildItem->HasValidPassIndex())
				{
					if (UCompositePassBase* Pass = ChildItem->GetPass())
					{
						Pass->Modify();
						Pass->SetEnabled(bSetEnabled);
					}
				}
			}
		}
	}
}

bool SCompositePassTree::CanEnableSelectedItems()
{
	TArray<FPassTreeItemPtr> SelectedItems = TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<FPassTreeItemPtr>();
	return !SelectedItems.IsEmpty();
}

#undef LOCTEXT_NAMESPACE
