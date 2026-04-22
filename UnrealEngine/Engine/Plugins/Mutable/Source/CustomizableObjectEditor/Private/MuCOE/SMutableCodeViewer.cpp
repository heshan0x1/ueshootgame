// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/SMutableCodeViewer.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Views/TableViewMetadata.h"
#include "Misc/Paths.h"
#include "MuCO/UnrealConversionUtils.h"
#include "MuCO/UnrealToMutableTextureConversionUtils.h"
#include "MuCOE/SMutableBoolViewer.h"
#include "MuCOE/SMutableColorViewer.h"
#include "MuCOE/SMutableConstantsWidget.h"
#include "MuCOE/SMutableCurveViewer.h"
#include "MuCOE/SMutableImageViewer.h"
#include "MuCOE/SMutableIntViewer.h"
#include "MuCOE/SMutableLayoutViewer.h"
#include "MuCOE/SMutableMeshViewer.h"
#include "MuCOE/SMutableInstanceViewer.h"
#include "MuCOE/SMutableProjectorViewer.h"
#include "MuCOE/SMutableScalarViewer.h"
#include "MuCOE/SMutableSkeletonViewer.h"
#include "MuCOE/SMutableStringViewer.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "MuCOE/Widgets/MutableExpanderArrow.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/UnrealMutableImageProvider.h"
#include "MuT/ErrorLog.h"
#include "MuT/TypeInfo.h"
#include "MuR/SystemPrivate.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Input/SSearchBox.h"
#include "Engine/SkeletalMesh.h"
#include "Internationalization/Regex.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"

class FExtender;
class FReferenceCollector;
class FUICommandList;
class SWidget;
namespace UE::Mutable::Private { struct FProjector; }
namespace UE::Mutable::Private { struct FShape; }
struct FGeometry;
struct FSlateBrush;


#define LOCTEXT_NAMESPACE "SMutableDebugger"


namespace MutableCodeTreeViewColumns
{
	static const FName OperationsColumnID("Operations");
	static const FName AdditionalDataColumnID("Flags");
};

/**
 * Mutable tree row used to display the operations held on the Mutable model object. 
 */
class SMutableCodeTreeRow final : public SMultiColumnTableRow<TSharedPtr<FMutableCodeTreeElement>>
{
public:

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FMutableCodeTreeElement>& InRowItem)
	{
		RowItem = InRowItem;
		
		SMultiColumnTableRow< TSharedPtr<FMutableCodeTreeElement> >::Construct(
			STableRow::FArguments()
			.ShowSelection(true)
			, InOwnerTableView
		);
	}

	FLinearColor OnGetExtraDataBoxColor() const
	{
		if (RowItem->bIsDynamicResource)
		{
			return DynamicResourceBoxColor;
		}
		else if (RowItem->bIsStateConstant)
		{
			return StateConstantBoxColor;
		}
		else
		{
			return ExtraDataBackgroundBoxDefaultColor;
		}
	}

	FText OnGetExtraDataText() const
	{
		// DEBUG :Uncomment the next line in order to debug the current state being used by the element
		// return FText::FromString(FString::FromInt(RowItem->GetStateIndex()));
		
		if (RowItem->bIsDynamicResource)
		{
			return  DynamicResourceText;
		}
		else if (RowItem->bIsStateConstant)
		{
			return StateConstantText;
		}
		else
		{
			return FText::FromString(FString(""));
		}
	}

	/** Depending on the state of the row returns one color or another to be used by the highlighting system */
	FLinearColor GetHighlightColor() const
	{
		if (bShouldBeHiglighted)
		{
			if (RowItem->DuplicatedOf)
			{
				return HighlightedDuplicatedBoxColor;
			}
			else
			{
				return HighlightedUniqueRowBoxColor;
			}
		}

		return HighlightBoxDefaultColor;
	}
	
	/** Method intended with the generation of the wanted objects for each column*/
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		// Primary column showing the name of the operation and tye type
		if (ColumnName == MutableCodeTreeViewColumns::OperationsColumnID)
		{
			// Prepare a ui container for all the UI objects required by this row element
			TSharedRef<SHorizontalBox> RowContainer = SNew(SHorizontalBox)
				
			// First coll showing operation name and type
			+ SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Fill)
			.AutoWidth()
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SAssignNew(this->HighlightingColorBox, SColorBlock)
					.Color(this, &SMutableCodeTreeRow::GetHighlightColor)
				]

				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SMutableExpanderArrow, SharedThis(this))
					]
					
					+ SHorizontalBox::Slot()
					[
						SNew(STextBlock)
						.Text(FText::FromString(RowItem->MainLabel))
						.ColorAndOpacity(RowItem->LabelColor)
					]
				]
			];

			return RowContainer;
		}

		// Second column showing some extra data related with the operation being displayed
		if (ColumnName == MutableCodeTreeViewColumns::AdditionalDataColumnID)
		{
			TSharedRef<SHorizontalBox> RowContainer =  SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.AutoWidth()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.MaxWidth(4.0f)
				[
					SNew(SColorBlock)
					.Color(this,&SMutableCodeTreeRow::OnGetExtraDataBoxColor)
				]

				+ SHorizontalBox::Slot()
				.Padding(4,1)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(this,&SMutableCodeTreeRow::OnGetExtraDataText)
				]
			];
			
			return RowContainer;
		}

		// Invalid column name so no widget will be produced 
		return SNullWidget::NullWidget;
	}
	
	/** Marks the row to be highlighted */
	void Highlight()
	{
		bShouldBeHiglighted = true;
	}

	/** Resets the highlighting status */
	void ResetHighlight()
	{
		bShouldBeHiglighted = false;
	}

	/** Returns a reference to the Element this row is representing */
	TSharedPtr<FMutableCodeTreeElement>& GetItem()
	{
		return RowItem;
	}

private:

	/** Pointer to the element that did spawn this row */
	TSharedPtr<FMutableCodeTreeElement> RowItem = nullptr;

	/** Transparent color */
	const FLinearColor TransparentColor = FLinearColor(0,0,0,0);

	/*
	 * Operation Highlighting 
	 */
	
	/** Custom Widget used to display a color. Used as the background of the text on the row to serve as highlighting Visual Element*/
	TSharedPtr<SColorBlock> HighlightingColorBox = nullptr;
	
	/** The color used to highlight the row if duplicated from another row */
	const FLinearColor HighlightedDuplicatedBoxColor = FLinearColor(1, 1, 1, 0.15);

	/** The color used to highlight elements that are originals (not duplicates)  */
	const FLinearColor HighlightedUniqueRowBoxColor = FLinearColor(1, 1, 1, 0.28);

	/** Default color used when the row is not highlighted */
	const FLinearColor HighlightBoxDefaultColor = TransparentColor;

	/*
	 * Extra data objects
	 */

	// Text used to set the width of the color area in front of the extra data
	const FText EmptyText = FText(INVTEXT(" "));
	
	/** String printed on the UI when the operation is shown to be dynamic resource */
	const FText DynamicResourceText = FText::FromString(FString("dyn"));

	/** String printed on the UI when the operation is shown to be state constant */
	const FText StateConstantText = FText::FromString(FString("const"));
	
	/** Color used on the extra data column when no extra data is shown */
	const FLinearColor ExtraDataBackgroundBoxDefaultColor =  TransparentColor;

	/** Color shown on the extra data column when the resource is found to be Dynamic */
	const FLinearColor DynamicResourceBoxColor = FLinearColor(0,0,1,0.8);

	/** Color shown on the extra data column when the resource is found to be State Constant */
	const FLinearColor StateConstantBoxColor = FLinearColor(1,0,0,0.8);

	bool bShouldBeHiglighted = false;
};


void SMutableCodeViewer::ClearSelectedTreeRow() const
{
	check(TreeView);
	TreeView->ClearSelection();
}


void SMutableCodeViewer::Construct(const FArguments& InArgs, UCustomizableObject& InObject, UCustomizableObjectInstance& InInstance, const TSharedPtr<UE::Mutable::Private::FModel>& InMutableModel)
{
	// Min width allowed for the column. Needed to avoid having issues with the constants space being to small
	// and then getting too tall on the y axis crashing the UI drawer.
	constexpr float MinParametersCollWidth = 200;

	Object = TStrongObjectPtr(&InObject);
	Instance = TStrongObjectPtr(&InInstance);

	MutableModel = InMutableModel;
	ExternalResourceProvider = InArgs._ExternalResourceProvider;
	PreviewParameters = Instance->GetPrivate()->GetDescriptor().GetParameters();

	RootNodes.Empty();
	RootNodeAddresses.Empty();
	ItemCache.Empty();
	MainItemPerOp.Empty();
	TreeElements.Empty();
	ExpandedElements.Empty();
	FoundModelOperationTypeElements.Empty();
	ModelOperationTypes.Empty();
	ModelOperationTypeNames.Empty();

	// Reset navigation by type / constant resource
	NavigationElements.Empty();
	NavigationIndex = -1;

	// Reset navigation by string
	NameBasedNavigationElements.Empty();
	StringNavigationIndex = -1;

	// Generate all elements before starting the tree UI so we have a deterministic set of unique and duplicated elements
	GenerateAllTreeElements();

	// Setup Navigation system
	{
		// Store the addresses of the root nodes so they can be used by operation search methods
		CacheRootNodeAddresses();

		// Cache the operation types that are present on the model
		CacheOperationTypesPresentOnModel();

		// Get an array of mutable types as an array of FStrings for the UI
		GenerateNavigationOpTypeStrings();

		// Generate list elements for the found operation types so we are able to search over them on our type dropdown
		GenerateNavigationDropdownElements();

		// Check we did find types (witch should always happen in a normal run) and select the NONE option as the default value
		check(FoundModelOperationTypeElements.Num())
		CurrentlySelectedOperationTypeElement = NoneOperationEntry;
	}
	
	FToolBarBuilder ToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Visible);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "SlimToolBar");

	TSharedRef<SScrollBar> TreeVertScrollBar =
		SNew(SScrollBar).
		Orientation(EOrientation::Orient_Vertical).
		AlwaysShowScrollbar(false);
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		[
			ToolbarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Fill)
		.Padding(5,2)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			
			+ SSplitter::Slot()
			.Value(0.35f)
			.MinSize(520)
			.Resizable(true)
			[
				SNew(SVerticalBox)

				// Search box for tree operations
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				[
					SNew(SHorizontalBox)
				
					// Search by name
					+ SHorizontalBox::Slot()
					.Padding(2,2)
					.AutoWidth()
					[
						SNew(SHorizontalBox)
				
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SelectedOperationByStringLabel","Search Operation by String "))
						]
					
						+ SHorizontalBox::Slot()
						.MaxWidth(250)
						.VAlign(VAlign_Center)
						[
							SNew(SSearchBox)
							.HintText(LOCTEXT("OperationToSearchHintText","Search OP"))
							.SearchResultData(this,&SMutableCodeViewer::SearchResultsData)
							.OnSearch(this, &SMutableCodeViewer::OnTreeStringSearch)
							.OnTextChanged(this, &SMutableCodeViewer::OnTreeSearchTextChanged)
							.OnTextCommitted(this, &SMutableCodeViewer::OnTreeSearchTextCommitted)
						]
					]
					
					
					// Regex control for search by name
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4,2)
					[
						SNew(SHorizontalBox)
				
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Regex", "Regex"))
						]
				
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged(this,&SMutableCodeViewer::OnRegexToggleChanged)
						]
					]	
				]
				
				// Operation type filtering slot
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				[
					// Box containing navigation elements
					SNew(SVerticalBox)
				
					+ SVerticalBox::Slot()
					.Padding(2,4)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
				
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SHorizontalBox)
				
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SelectedOperationTypeLabel","Search Operation Type "))
							]
				
							// ComboBox used to select one or another Op_Type for tree navigation purposes
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(TargetedTypeSelector,SComboBox<TSharedPtr<const FMutableOperationElement>>)
								.OptionsSource(&FoundModelOperationTypeElements)
								.InitiallySelectedItem(CurrentlySelectedOperationTypeElement)
								[
									SNew(STextBlock)
									.Text(this,&SMutableCodeViewer::GetCurrentNavigationOpTypeText)
									.ColorAndOpacity(this,&SMutableCodeViewer::GetCurrentNavigationOpTypeColor)
								]
								.OnGenerateWidget(this,&SMutableCodeViewer::OnGenerateOpNavigationDropDownWidget)
								.OnSelectionChanged(this,&SMutableCodeViewer::OnNavigationSelectedOperationChanged)
							]
						]
				
						+ SHorizontalBox::Slot()
						.Padding(4,0)
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.Text(LOCTEXT("GoToPreviousOperationButton"," < "))
							.OnClicked(this,&SMutableCodeViewer::OnGoToPreviousOperationButtonPressed)
							.IsEnabled(this,&SMutableCodeViewer::CanInteractWithPreviousOperationButton)
						]
				
						+ SHorizontalBox::Slot()
						.Padding(4,0)
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this,&SMutableCodeViewer::OnPrintNavigableObjectAddressesCount)
							.Justification(ETextJustify::Center)
						]
				
						+ SHorizontalBox::Slot()
						.Padding(4,0)
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.Text(LOCTEXT("GoToNextOperationButton"," > "))
							.OnClicked(this,&SMutableCodeViewer::OnGoToNextOperationButtonPressed)
							.IsEnabled(this,&SMutableCodeViewer::CanInteractWithNextOperationButton)
						]
					]
				]
				
				// Tree operations slot
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
					.Padding(FMargin(4.0f, 4.0f))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillContentWidth(1)
						[
							SNew(SScrollBox)		
							.Orientation(EOrientation::Orient_Horizontal)
							.ConsumeMouseWheel(EConsumeMouseWheel::Never)
							
							+ SScrollBox::Slot()
							.HAlign(HAlign_Fill)
							.FillContentSize(1)
							[
								SAssignNew(TreeView, STreeView<TSharedPtr<FMutableCodeTreeElement>>)
								.TreeItemsSource(&RootNodes)
								.OnGenerateRow(this, &SMutableCodeViewer::GenerateRowForNodeTree)
								.OnRowReleased(this, &SMutableCodeViewer::OnRowReleased)
								.OnGetChildren(this, &SMutableCodeViewer::GetChildrenForInfo)
								.OnSelectionChanged(this, &SMutableCodeViewer::OnSelectionChanged)
								.OnSetExpansionRecursive(this, &SMutableCodeViewer::TreeExpandRecursive)
								.OnContextMenuOpening(this, &SMutableCodeViewer::OnTreeContextMenuOpening)
								.OnExpansionChanged(this, &SMutableCodeViewer::OnExpansionChanged)
								.SelectionMode(ESelectionMode::Single)
								.ExternalScrollbar(TreeVertScrollBar)
								.HeaderRow
								(
									SNew(SHeaderRow)
									.ResizeMode(ESplitterResizeMode::Fill)

									+ SHeaderRow::Column(MutableCodeTreeViewColumns::OperationsColumnID)
										.DefaultLabel(LOCTEXT("Operation", "Operation"))

									+ SHeaderRow::Column(MutableCodeTreeViewColumns::AdditionalDataColumnID)
										.DefaultLabel(LOCTEXT("OperationFlags", "Flags"))
										.FixedWidth(50.0f)
								)
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							TreeVertScrollBar
						]
					]
				]
				
			]
			+ SSplitter::Slot()
			.Value(0.75f)
			[
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Horizontal)
				
				+ SSplitter::Slot()
				.Value(0.28f)
				.MinSize(MinParametersCollWidth)
				[
					// Splitter managing both parameter and constant panels
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding(4, 4, 4,0)
						.AutoHeight()
						[
							SNew(SButton)
							.Text(LOCTEXT("RefreshParameters", "Refresh Parameters"))
							.OnClicked(this, &SMutableCodeViewer::UpdateParameters)
						]
						
						+ SVerticalBox::Slot()
						.Padding(4, 8, 4, 0)
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.Padding(4, 0, 0, 0)
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SkipMipsLabel", "Skip mips on generate "))
								.Visibility(this, &SMutableCodeViewer::IsMipSkipVisible)
							]
							+ SHorizontalBox::Slot()
							.Padding(4, 0, 0, 0)
							[
								SNew(SNumericEntryBox<int32>)
								.Visibility(this, &SMutableCodeViewer::IsMipSkipVisible)
								.AllowSpin(true)
								.MinValue(0)
								.MaxValue(16)
								.MinSliderValue(0)
								.MaxSliderValue(16)
								.Value(this, &SMutableCodeViewer::GetCurrentMipSkip)
								.OnValueChanged(this, &SMutableCodeViewer::OnCurrentMipSkipChanged)
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8, 8, 0, 0)
					.VAlign(VAlign_Top)
					[
						// Generate a new Constants panel to show the data stored on the current mutable program
						SAssignNew(ConstantsWidget, SMutableConstantsWidget,
							&(MutableModel->GetPrivate()->Program),
							SharedThis(this))
					]
				]
				+ SSplitter::Slot()
				.Value(0.72f)
				[
					SAssignNew(PreviewBorder, SBorder)
					.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
					.Padding(FMargin(4.0f, 4.0f))					
				]
			]
		]
	];
	
	// Set the tree expanded by default
	// It does not recalculate states since the expansion of the instance will NOT expand duplicates witch means the widget position
	// of the children of duplicated (or the original of an operation with duplicates) will not change.
	TreeExpandInstance();

	// Enable the recalculation of states once the tree has already been initially expanded since now we do not control
	// how the user is point to interact with the view.
	bShouldRecalculateStates = true;
	// Now, on expansion or contraction the states will get recalculated
}


FReply SMutableCodeViewer::UpdateParameters()
{
	PreviewParameters = Instance->GetPrivate()->GetDescriptor().GetParameters();
	bIsPreviewPendingUpdate = true;

	return FReply::Handled();
}


EVisibility SMutableCodeViewer::IsMipSkipVisible() const 
{ 
	return bSelectedOperationIsImage ? EVisibility::Visible : EVisibility::Hidden;
}


TOptional<int32> SMutableCodeViewer::GetCurrentMipSkip() const 
{ 
	return MipsToSkip; 
}


void SMutableCodeViewer::OnCurrentMipSkipChanged(int32 NewValue)
{
	MipsToSkip = NewValue;
	bIsPreviewPendingUpdate = true;
}

#pragma region CodeTree operation name search


void SMutableCodeViewer::OnRegexToggleChanged(ECheckBoxState CheckBoxState)
{
	const bool bPreChangeValue = bIsSearchStringRegularExpression;
	bIsSearchStringRegularExpression = CheckBoxState == ECheckBoxState::Checked ? true : false;
	
	if (bPreChangeValue != bIsSearchStringRegularExpression)
	{
		CacheOperationsMatchingStringPattern();
		GoToNextOperation();
	}
}

void SMutableCodeViewer::OnTreeStringSearch(SSearchBox::SearchDirection SearchDirection)
{
	if (SearchDirection==SSearchBox::SearchDirection::Next)
	{
		GoToNextOperation();
	}
	else
	{
		GoToPreviousOperation();
	}
}

void SMutableCodeViewer::GoToNextOperation()
{
	// Contingency : Prevent a second scroll operation from being performed if still we do not have the first target in view
	if (bWasScrollToTargetRequested)
	{
		return;
	}
	
	if (NameBasedNavigationElements.Num())
	{
		const int32 PreviousIndex = StringNavigationIndex;

		// Focus on next target
		StringNavigationIndex = StringNavigationIndex >= NameBasedNavigationElements.Num() - 1
										   ? 0
										   : StringNavigationIndex + 1;

		if (StringNavigationIndex != PreviousIndex)
		{
			FocusViewOnNavigationTarget(NameBasedNavigationElements[StringNavigationIndex]);
		}
	}
}


void SMutableCodeViewer::GoToPreviousOperation()
{
	// Contingency : Prevent a second scroll operation from being performed if still we do not have the first target in view
	if (bWasScrollToTargetRequested)
	{
		return;
	}
	
	if (NameBasedNavigationElements.Num())
	{
		const int32 PreviousIndex = StringNavigationIndex;
		
		// Focus on previous target
		StringNavigationIndex = StringNavigationIndex <= 0 ?  NameBasedNavigationElements.Num() -1 : StringNavigationIndex -1;

		if (PreviousIndex != StringNavigationIndex)
		{
			FocusViewOnNavigationTarget(NameBasedNavigationElements[StringNavigationIndex]);
		}
	}
}

void SMutableCodeViewer::GoToTargetOperation(const int32& InTargetIndex)
{
	if (InTargetIndex == StringNavigationIndex)
	{
		return;
	}
	
	if (NameBasedNavigationElements.Num() && InTargetIndex > 0 && InTargetIndex <= NameBasedNavigationElements.Num()-1)
	{
		// Focus on the target index
		StringNavigationIndex = InTargetIndex;
		FocusViewOnNavigationTarget(NameBasedNavigationElements[StringNavigationIndex]);
	}
}


void SMutableCodeViewer::OnTreeSearchTextChanged(const FText& InUpdatedText)
{
	SearchString = InUpdatedText.ToString();
}


TOptional<SSearchBox::FSearchResultData> SMutableCodeViewer::SearchResultsData() const
{
	if (NameBasedNavigationElements.Num() == 0)
	{
		return TOptional<SSearchBox::FSearchResultData>();
	}
	return TOptional<SSearchBox::FSearchResultData>({ NameBasedNavigationElements.Num(), StringNavigationIndex + 1});
}


void SMutableCodeViewer::OnTreeSearchTextCommitted(const FText& InUpdatedText, ETextCommit::Type TextCommitType)
{
	if (TextCommitType == ETextCommit::OnEnter)
	{
		check (InUpdatedText.ToString() == SearchString);
		CacheOperationsMatchingStringPattern();	
		GoToNextOperation();
	}
}


void SMutableCodeViewer::CacheOperationsMatchingStringPattern()
{
	check(MutableModel);
	check(RootNodeAddresses.Num());
	
	if ( LastSearchedString == SearchString &&
		bWasLastSearchRegEx == bIsSearchStringRegularExpression && 
		LastSearchedModel == MutableModel )
	{
		// Do not perform a search again since the context has not changed
		return;
	} 
	
	if (!SearchString.IsEmpty())
	{
		UE_LOG(LogMutable,Display,TEXT("Starting string search with target string ""\"%s""\" "),*SearchString);
	
		// Object containing all data required by the search operation to be able to be called recursively
		FElementsSearchCache SearchPayload;
		// Initialize the Search Payload with the root node addresses. This way the search will use them as the root nodes where
		// to start searching
		SearchPayload.SetupRootBatch(RootNodeAddresses);

		const UE::Mutable::Private::FProgram& Program = MutableModel->GetPrivate()->Program;
		GetOperationsMatchingStringPattern(SearchString,bIsSearchStringRegularExpression,SearchPayload, Program);
	
		// Dump the located resources array onto the navigation array
		NameBasedNavigationElements = MoveTemp(SearchPayload.FoundElements);
		SortElementsByTreeIndex(NameBasedNavigationElements);
		
		UE_LOG(LogMutable, Display, TEXT("Operations found with matching pattern ""\"%s""\" is  %i"), *SearchString, NameBasedNavigationElements.Num());
	}
	else
	{
		NameBasedNavigationElements.Reset();
	}

	// Reset the search index
	StringNavigationIndex = -1;

	// Keep track of what context was used to perform the search to avoid doing it again if the context has not changed
	LastSearchedString = SearchString;
	bWasLastSearchRegEx = bIsSearchStringRegularExpression;
	LastSearchedModel = MutableModel;
}


void SMutableCodeViewer::GetOperationsMatchingStringPattern(const FString& InStringPattern,const bool bIsRegularExpression ,FElementsSearchCache& SearchPayload,const UE::Mutable::Private::FProgram& InProgram)
{
	// next batch of addresses to be explored 
	TArray<FItemCacheKey> NextBatchAddressesData;
	
	for (int32 ParentIndex = 0; ParentIndex < SearchPayload.BatchData.Num(); ParentIndex++)
	{	
		const FItemCacheKey CacheKey = SearchPayload.BatchData[ParentIndex];
		const FString OperationDescriptiveText = GetOperationDescriptiveText(CacheKey);
		
		bool bMatchesPattern = false;
		if (!bIsRegularExpression)
		{
			// Check if the provided text is contained over the element identification text
			bMatchesPattern = OperationDescriptiveText.Contains(InStringPattern);
		}
		else
		{
			FRegexPattern Pattern{InStringPattern};
			FRegexMatcher RegexMatcher{Pattern,OperationDescriptiveText};
			bMatchesPattern = RegexMatcher.FindNext();
		}
		
		// Get one of the previous run "children" and treat as a parent to get it's children and process them
		const UE::Mutable::Private::OP::ADDRESS& ParentAddress = SearchPayload.BatchData[ParentIndex].Child;
		
		if (bMatchesPattern)
		{
			SearchPayload.AddToFoundElements(ParentAddress,ParentIndex,ItemCache);
		}
		
		// Get all NON PROCESSED the children of this operation to later be able to process them (on next recursive call)
		SearchPayload.CacheChildrenOfAddressIfNotProcessed(ParentAddress, InProgram, NextBatchAddressesData);
	}

	// At this point all the addresses to be computed on the next batch have already been set and will be computed on
	// the next recursive call
	
	// Explore children if found 
	if (NextBatchAddressesData.Num())
	{
		// Cache next batch data so the next invocations is able to locate the provided addresses on the itemsCache
		SearchPayload.BatchData = MoveTemp(NextBatchAddressesData);
		
		GetOperationsMatchingStringPattern(InStringPattern,bIsRegularExpression, SearchPayload, InProgram);
	}
}


FString SMutableCodeViewer::GetOperationDescriptiveText(const FItemCacheKey& InItemCacheKey)
{
	FString OperationDescriptiveText;
		
	if (const TSharedPtr<FMutableCodeTreeElement>* Element = ItemCache.Find(InItemCacheKey))
	{
		OperationDescriptiveText = Element->Get()->MainLabel;
		check (!OperationDescriptiveText.IsEmpty());
	}
	
	return OperationDescriptiveText;
}

#pragma endregion 


#pragma region CodeTree operation search

FText SMutableCodeViewer::GetCurrentNavigationOpTypeText() const
{
	check (CurrentlySelectedOperationTypeElement);
	
	return CurrentlySelectedOperationTypeElement->OperationTypeText;
}

FSlateColor SMutableCodeViewer::GetCurrentNavigationOpTypeColor() const
{
	check (CurrentlySelectedOperationTypeElement);

	return CurrentlySelectedOperationTypeElement->OperationTextColor;
}

void SMutableCodeViewer::GenerateNavigationDropdownElements()
{
	const int32 OperationTypesCount = ModelOperationTypes.Num();
	
	// It must have at least one type, if not may be because we are running this before filling ModelOperationTypes
	FoundModelOperationTypeElements.Empty(OperationTypesCount);
	
	for	(int32 OperationTypeIndex = 0; OperationTypeIndex < OperationTypesCount;  OperationTypeIndex++)
	{
		// Get the type as a string to be able to print it on the UI
		const FText OperationTypeName = FText::FromString(ModelOperationTypeNames[OperationTypeIndex]);

		const UE::Mutable::Private::EOpType RepresentedType = ModelOperationTypes[OperationTypeIndex].Key;
		const uint32 OperationTypeInstancesCount = ModelOperationTypes[OperationTypeIndex].Value;
		
		// Get the Color to be used on the text that will represent the operation on the dropdown
		const FSlateColor OperationColor = ColorPerComputationalCost[StaticCast<uint8>(GetOperationTypeComputationalCost(RepresentedType))];
		
		// Generate an element to be used by the ComboBox handling the selection of the type to be used during navigation
		TSharedPtr<FMutableOperationElement> OperationElement = MakeShared<FMutableOperationElement>(RepresentedType, OperationTypeName,OperationTypeInstancesCount,OperationColor);
		FoundModelOperationTypeElements.Add(OperationElement);
	}

	// Add an entry for the NONE type of operation
	{
		const FText EntryName = FText::FromString("NONE");
		const FSlateColor EntryColor = ColorPerComputationalCost[StaticCast<uint8>(EOperationComputationalCost::Standard)];
		NoneOperationEntry = MakeShared<FMutableOperationElement>(UE::Mutable::Private::EOpType::NONE,EntryName,0,EntryColor);

		// @warn While not visible this element must be part of the collection for the ComboBox to be able to work
		// properly
		FoundModelOperationTypeElements.Add(NoneOperationEntry);
	}
	
	// Add an extra operation type that will represent the constant resource based navigation type
	{
		const FText EntryName = FText::FromString("Selected Constant");	
		const FSlateColor EntryColor = FSlateColor(FLinearColor(0.35f ,0.35f,1.0f,1));
		ConstantBasedNavigationEntry = MakeShared<FMutableOperationElement>(UE::Mutable::Private::EOpType::NONE,EntryName,0,EntryColor);
		
		// @warn While not visible this element must be part of the collection for the ComboBox to be able to work
		// properly
		FoundModelOperationTypeElements.Add(ConstantBasedNavigationEntry);
	}
}

TSharedRef<SWidget> SMutableCodeViewer::OnGenerateOpNavigationDropDownWidget(
	TSharedPtr<const FMutableOperationElement> MutableOperationElement) const
{
	TSharedRef<STextBlock> NewSlateObject = SNew(STextBlock)
		.Text(MutableOperationElement->OperationTypeText)
		.ColorAndOpacity(MutableOperationElement->OperationTextColor);

	// Set the visibility type for the UI object (currently will be hidden for the NONE type)
	NewSlateObject->SetVisibility(MutableOperationElement->GetEntryVisibility());
	
	return NewSlateObject;
}

void SMutableCodeViewer::OnNavigationSelectedOperationChanged(
	TSharedPtr<const FMutableOperationElement, ESPMode::ThreadSafe> MutableOperationElement, ESelectInfo::Type Arg)
{
	// Handle the case where we do not want an option selected, for example, when clearing the selected option.
	TSharedPtr<const FMutableOperationElement, ESPMode::ThreadSafe> NewSelectedElement = MutableOperationElement;
	if (!NewSelectedElement.IsValid())
	{
		NewSelectedElement = NoneOperationEntry;
	}
	
	check (NewSelectedElement.IsValid());

	// Cache the currently selected operation set on the UI by the user
	const UE::Mutable::Private::EOpType NewOperationType = NewSelectedElement->OperationType;
	OperationTypeToSearch = NewOperationType;
	CurrentlySelectedOperationTypeElement = NewSelectedElement;
	
	// Only do the internal work if the type is one that makes sense searching
	if (OperationTypeToSearch != UE::Mutable::Private::EOpType::NONE)
	{
		// Locate all operations on the mutable operations tree (not the visual one) that do share the same operation type
		// as the one selected. This will fill the array with the elements we should be looking for during the navigation operation
		CacheAddressesOfOperationsOfType();
	}
	// None can be set by the user or be an indication that we are navigating over constant related operations
	// todo: Separate both operations in some way on the UI to avoid complications in the code and in the UI's UX
	else
	{
		// Clear all the elements on the navigation addresses 
		NavigationElements.Empty();
	}
	
}


void SMutableCodeViewer::GenerateNavigationOpTypeStrings()
{
	// Grab only the names from the operation types located during the caching of operation types of the model
	for (const TTuple<UE::Mutable::Private::EOpType, uint32>& LocatedOperationType : ModelOperationTypes)
	{
		// Find the name of the Operation type
		const uint16 OperationIndex = static_cast<uint16>(LocatedOperationType.Key);
		const TCHAR* OpName = UE::Mutable::Private::s_opNames[OperationIndex];

		// Remove trailing whitespaces adding noise and messing up concatenations with other strings
		FString OperationNameString{OpName};
		OperationNameString.RemoveSpacesInline();

		// Save the name
		ModelOperationTypeNames.Add( OperationNameString);
	}
}

void SMutableCodeViewer::OnSelectedOperationTypeFromTree()
{
	// We require to have only 1 element selected to avoid having inconsistencies during operation
	check(TreeView->GetNumItemsSelected() == 1);
	
	const TSharedPtr<FMutableCodeTreeElement> ReferenceOperationElement = TreeView->GetSelectedItems()[0];
	
	const UE::Mutable::Private::EOpType OperationType =
		MutableModel->GetPrivate()->Program.GetOpType(ReferenceOperationElement->MutableOperation);

	// Find the operation type directly in our array of operation elements (from the drop down)
	const TSharedPtr<const FMutableOperationElement>* RepresentativeElement = FoundModelOperationTypeElements.
		FindByPredicate([OperationType](const TSharedPtr<const FMutableOperationElement> Other)
		{
			return Other->OperationType == OperationType;
		});

	// Ensure an element was found. Failing the next check would mean that we are not caching all the types present on
	// the current operation's tree
	check(RepresentativeElement != nullptr);
	
	// Set the type operation type to be looking for -> Will invoke OnOptionTypeSelectionChanged
	TargetedTypeSelector->SetSelectedItem(*RepresentativeElement);
	
	// Reset the navigation index so it starts from scratch
	NavigationIndex = -1;
}


void SMutableCodeViewer::SortElementsByTreeIndex(TArray<TSharedPtr<FMutableCodeTreeElement>>& InElementsArrayToSort)
{
	// Sort the array from lower index to bigger index (0 , 1 , 2 ...)
	InElementsArrayToSort.Sort([](const TSharedPtr<FMutableCodeTreeElement> A , const TSharedPtr<FMutableCodeTreeElement> B)
	{
		return A->IndexOnTree < B->IndexOnTree;
	});
}


void SMutableCodeViewer::CacheAddressesOfOperationsOfType()
{
	// Clear previous data
	NavigationElements.Empty();
	check(RootNodeAddresses.Num());

	// Object containing all data required by the search operation to be able to be called recursively
	FElementsSearchCache SearchPayload;
	// Initialize the Search Payload with the root node addresses. This way the search will use them as the root nodes where
	// to start searching
	SearchPayload.SetupRootBatch(RootNodeAddresses);
	
	// Main update procedure run for the targeted state and the targeted parameter values
	const UE::Mutable::Private::FProgram& Program = MutableModel->GetPrivate()->Program;
	GetOperationsOfType(OperationTypeToSearch,SearchPayload, Program);
	
	if (!SearchPayload.FoundElements.IsEmpty())
	{
		// Cache the navigation addresses so we are able to navigate over them
		NavigationElements = MoveTemp(SearchPayload.FoundElements);
		SortElementsByTreeIndex(NavigationElements);
		
		// Reset the navigation index
		NavigationIndex = -1;
	}
}

void SMutableCodeViewer::GetOperationsOfType(const UE::Mutable::Private::EOpType& TargetOperationType,
                                             FElementsSearchCache& InSearchPayload,
                                             const UE::Mutable::Private::FProgram& InProgram)
{
	// next batch of addresses to be explored 
	TArray<FItemCacheKey> NextBatchAddressesData;
	
	for	(int32 ParentIndex = 0 ; ParentIndex < InSearchPayload.BatchData.Num(); ParentIndex++)
	{
		// Get one of the previous run "children" and treat as a parent to get it's children and process them
		const UE::Mutable::Private::OP::ADDRESS& CurrentAddress = InSearchPayload.BatchData[ParentIndex].Child;
		
		// Cache if same data type and we share the same address (means this op is pointing at the provided resource)
		// It will cache duplicated entries
		if ( InProgram.GetOpType(CurrentAddress) == TargetOperationType)
		{
			// Since this element is of the type we are looking for then cache it on InSearchPayload.FoundElements
			InSearchPayload.AddToFoundElements(CurrentAddress,ParentIndex,ItemCache);
		}
		
		// Get all NON PROCESSED the children of this operation to later be able to process them (on next recursive call)
		InSearchPayload.CacheChildrenOfAddressIfNotProcessed(CurrentAddress, InProgram, NextBatchAddressesData);
	}

	// Explore children if found 
	if (NextBatchAddressesData.Num())
	{
		// Cache next batch data so the next invocations are able to locate the provided addresses on the itemsCache
		InSearchPayload.BatchData = MoveTemp(NextBatchAddressesData);
		
		// Process the children of this object
		GetOperationsOfType(TargetOperationType, InSearchPayload, InProgram);
	}
 }


void SMutableCodeViewer::CacheOperationTypesPresentOnModel()
{
	check(MutableModel)

	// Initialize NodeOperationTypes with empty TPair for each possible mutable operation type
	{
		constexpr uint32 OperationTypesCount = static_cast<uint16>(UE::Mutable::Private::EOpType::COUNT);
		ModelOperationTypes.Empty(OperationTypesCount);
		for (uint32 Index = 0; Index < OperationTypesCount ; Index++)
		{
			UE::Mutable::Private::EOpType TargetType = StaticCast<UE::Mutable::Private::EOpType>(Index);
			ModelOperationTypes.Add(TPair<UE::Mutable::Private::EOpType,uint32>{TargetType,0});
		}
	}
	
	// Locate all operation types found on the provided model program data structure and count how many instances of each
	// there are
	{
		// Get the types and the amount of instances of each unique operation on the mutable model
		const UE::Mutable::Private::FProgram& Program = MutableModel->GetPrivate()->Program;
		const uint32 ProgramAddressesCount = Program.OpAddress.Num();
		
		// Ensure first operation type is NONE since we are skipping it due to it having to have that type
		check (Program.GetOpType(Program.OpAddress[0]) == UE::Mutable::Private::EOpType::NONE);

		// Iterate over the addresses of the program and count how many instances each type has.
		for (uint32 ProgramAddressesIndex = 1 ; ProgramAddressesIndex < ProgramAddressesCount; ProgramAddressesIndex++)
		{
			// Locate what is the position (index) of the operation type of the address on our collection of types found until now
			const UE::Mutable::Private::EOpType OperationType = Program.GetOpType(ProgramAddressesIndex);
			
			// Increase the counter for this operation type
			const uint16 TypeAsInteger = StaticCast<uint16>(OperationType);
			ModelOperationTypes[TypeAsInteger].Value++;
		}
	}
	
	// Remove all operation types that do have no operations present on the model
	{
		ModelOperationTypes.RemoveAll(
		[](const TPair<UE::Mutable::Private::EOpType, uint32>& Current)
			{
				return Current.Value == 0;
			});
	}
	
	// Sort the contents of the array of mutable operation types alphabetically
	ModelOperationTypes.StableSort([&](const TPair<UE::Mutable::Private::EOpType,uint32>& A, const TPair<UE::Mutable::Private::EOpType,uint32>& B)
	{
		// Find the name
		FString AString;
		{
			const uint16 OperationIndex = static_cast<uint16>(A.Key);
			const TCHAR* OpName = UE::Mutable::Private::s_opNames[OperationIndex];
			AString = FString(OpName);
		}
		
		// Find out the name of the first element
		FString BString;
		{
			const uint16 OperationIndex = static_cast<uint16>(B.Key);
			const TCHAR* OpName = UE::Mutable::Private::s_opNames[OperationIndex];
			BString = FString(OpName);
		}
		
		// Then the name of the second element
		return AString < BString;
	});	
	
	// ModelOperationTypes is now an array with all the types found on the operations tree in alphabetical order
}



FText SMutableCodeViewer::OnPrintNavigableObjectAddressesCount() const
{
	FString OutputString = "";
	if (const int32 NavigationElementsCount = NavigationElements.Num())
	{
		// Show the index if the index showing adds information
		if (NavigationIndex >= 0)
		{
			OutputString.Append( FString::FromInt( NavigationIndex+1));
			OutputString.Append(" / ");
		}
		
		OutputString.Append( FString::FromInt(NavigationElementsCount));

		// Format : 1 / 12 or 12
	}
	
	// Depending on the amount of navigable objects (addresses, not actual elements) display the amount there are
	return FText::FromString(OutputString);
}


bool SMutableCodeViewer::CanInteractWithPreviousOperationButton() const
{
	// Only navigable if there are more than 0 elements to traverse and we are not scrolling
	return NavigationElements.Num() > 0 && NavigationIndex > 0 && (!bWasScrollToTargetRequested && !bWasUniqueExpansionInvokedForNavigation);
}

bool SMutableCodeViewer::CanInteractWithNextOperationButton() const
{
	// Only navigable if there are more than 0 elements to traverse and we are not scrolling
	return NavigationElements.Num() > 0 && NavigationIndex < NavigationElements.Num() -1 && (!bWasScrollToTargetRequested && !bWasUniqueExpansionInvokedForNavigation);
}


FReply SMutableCodeViewer::OnGoToPreviousOperationButtonPressed()
{
	// Focus on previous target
	NavigationIndex = NavigationIndex<=0 ? 0 : NavigationIndex - 1;
	FocusViewOnNavigationTarget(NavigationElements[NavigationIndex]);
	
	return FReply::Handled();
}

FReply SMutableCodeViewer::OnGoToNextOperationButtonPressed()
{
	// Focus on next target
	NavigationIndex = NavigationIndex>=NavigationElements.Num() -1 ? NavigationElements.Num() -1 : NavigationIndex + 1;
	FocusViewOnNavigationTarget(NavigationElements[NavigationIndex]);
	
	return FReply::Handled();
}

void SMutableCodeViewer::FocusViewOnNavigationTarget(TSharedPtr<FMutableCodeTreeElement> InTargetElement)
{
	// Stage 1 : Expand all tree so all navigable elements get to be reachable
	if (!bWasUniqueExpansionInvokedForNavigation && !bWasScrollToTargetRequested)
	{
		TreeExpandUnique();
		bWasUniqueExpansionInvokedForNavigation = true;
		
		// Cache the current navigation target so after the update we can focus it 
		ToFocusElement = InTargetElement;
		
		// Early exit, this method will get called again later after tree update
		return;		
	}
	
	// Stage 2 : Try to get to the targeted element. if not visible scroll into view
	check (InTargetElement.IsValid());
	
	// If required scroll to the area where we know the element is going to be in view
	// a way to ensure this happens is by calling 
	if (TreeView->IsItemVisible(InTargetElement))
	{
		// Stage 3-b : Select the element we have provided since now is sure to be in view
		
		// This line selects the element with at the same time updates the UI to show the row representing this element selected
		TreeView->SetSelection(InTargetElement);
		ToFocusElement.Reset();							// We have focused the target so we no longer need to keep a reference to it
		
		// Done!
		// We have the element in view and we have selected it!
	}
	else
	{
		// Stage 3-a (optional) : Ask for the provided element to be scrolled into view.
		
		// Failing this check would mean we have performed a scroll but we are still not able to view the element
		check (!bWasScrollToTargetRequested);
		
		// Request the tree to show us the target element we want to get focused
		TreeView->RequestScrollIntoView(InTargetElement);
		
		// Read this variable after the update and then select the object (easy at this point)
		// You may want to just call again this method after refresh since the element will be on view
		bWasScrollToTargetRequested = true;

		// Early exit, this method will get called again later after tree update once the scroll has been completed
		return;
	}

	// Reset the control flag so we do not expand all tree again if not required
	bWasUniqueExpansionInvokedForNavigation = false;
	bWasScrollToTargetRequested = false;
}

#pragma endregion 

#pragma region Operation Cost Color Hints

void SMutableCodeViewer::GenerateAllTreeElements()
{
	// By generating all tree elements prior to usage we are able to :
	//	- Compute the index of each one to aid on navigation
	//	- Remove non-deterministic assignation of the "Duplicated" state of elements. It was due to user interaction with the tree
	//	Only unique elements, their children and duplicated elements will be generated. Children of duplicates will
	// 	be ignored due to how we handle them when expanding and contracting elements (OnExpansionChanged)

	// Generate all root nodes
	const UE::Mutable::Private::FModel::Private* ModelPrivate = MutableModel->GetPrivate();
	check(ModelPrivate);
	const UE::Mutable::Private::FProgram& Program = ModelPrivate->Program;
	const uint32 StateCount = Program.States.Num();
	for ( uint32 StateIndex = 0; StateIndex < StateCount; ++StateIndex )
	{
		const UE::Mutable::Private::FProgram::FState& State = Program.States[StateIndex];
		const FString Caption = FString::Printf( TEXT("state [%s]"), *State.Name );

		const FSlateColor LabelColor = ColorPerComputationalCost[StaticCast<uint8>(GetOperationTypeComputationalCost(
			Program.GetOpType(State.Root)))];

		// Locate the "original" tree element :
		// This may happen if for some reason the state is duplicated (should never happen)
		const TSharedPtr<FMutableCodeTreeElement>* MainItemPtr = MainItemPerOp.Find(State.Root);

		// Create a new root element and add it to the collection of root nodes
		TSharedPtr<FMutableCodeTreeElement> RootNodeElement = MakeShareable(new FMutableCodeTreeElement(ItemCache.Num(),StateIndex, MutableModel, State.Root, Caption,LabelColor, MainItemPtr));
		RootNodes.Add(RootNodeElement);
		
		// Add the element to the cache so we keep the indices straight.
		constexpr UE::Mutable::Private::OP::ADDRESS CommonParent = 0;
		const FItemCacheKey Key = { CommonParent, State.Root, StateIndex };
		ItemCache.Add(Key, RootNodeElement);
		
		if (!MainItemPtr)
		{
			// Cache this node as it may be duplicated of another state. Check the "MainItemPtr" initialization for more info
			MainItemPerOp.Add(State.Root, RootNodeElement);

			// Iterate over each root node and generate all the elements in a human-readable pattern (Z Pattern)
			GenerateElementRecursive(StateIndex,State.Root,Program);
		}
	}
	
}

void SMutableCodeViewer::GenerateElementRecursive(const int32& InStateIndex, UE::Mutable::Private::OP::ADDRESS InParentAddress,  const UE::Mutable::Private::FProgram& InProgram)
{
	// This will be used to add operations
	uint32 ChildIndex = 0;
	auto AddOpFunc = [this, InParentAddress, &InProgram, &ChildIndex, &InStateIndex](UE::Mutable::Private::OP::ADDRESS ChildAddress, const FString& Caption)
		{
			{
				const FItemCacheKey Key = { InParentAddress, ChildAddress, ChildIndex };
				const TSharedPtr<FMutableCodeTreeElement>* CachedItem = ItemCache.Find(Key);

				// If not already cached then process it
				if (ensure(!CachedItem))
				{
					// Locate the "original" tree element
					const TSharedPtr<FMutableCodeTreeElement>* MainItemPtr = MainItemPerOp.Find(ChildAddress);

					// Provide the color this element should be using for the displayed text
					const FSlateColor LabelColor = ColorPerComputationalCost[StaticCast<uint8>(GetOperationTypeComputationalCost(InProgram.GetOpType(ChildAddress)))];

					const TSharedPtr<FMutableCodeTreeElement> Item = MakeShareable(new FMutableCodeTreeElement(ItemCache.Num(),InStateIndex, MutableModel, ChildAddress, Caption, LabelColor, MainItemPtr));

					// Cache this element for later access
					ItemCache.Add(Key, Item);

					// It is not a duplicated of another one, then we can continue searching
					if (!MainItemPtr)
					{
						MainItemPerOp.Add(ChildAddress, Item);

						GenerateElementRecursive(InStateIndex,ChildAddress, InProgram);
					}
				}
				else
				{
					UE_LOG(LogMutable, Error, TEXT("An already processed operation is being re-processed in order to generate a tree row. "
									"ParentAddress : %u , ChildAddress : %u , ChildIndex : %u "), InParentAddress, ChildAddress, ChildIndex);
				}
				
			}
			++ChildIndex;
		};

	
	// For some specific parent operation types we create more detailed subtrees.
	bool bUseGeneric = false;
	const UE::Mutable::Private::EOpType ParentOperationType = InProgram.GetOpType(InParentAddress);
	switch (ParentOperationType)
	{
	case UE::Mutable::Private::EOpType::IM_CONDITIONAL:
	case UE::Mutable::Private::EOpType::LA_CONDITIONAL:
	case UE::Mutable::Private::EOpType::ME_CONDITIONAL:
	case UE::Mutable::Private::EOpType::CO_CONDITIONAL:
	case UE::Mutable::Private::EOpType::SC_CONDITIONAL:
	case UE::Mutable::Private::EOpType::NU_CONDITIONAL:
	case UE::Mutable::Private::EOpType::IN_CONDITIONAL:
	case UE::Mutable::Private::EOpType::ED_CONDITIONAL:
	{
		UE::Mutable::Private::OP::ConditionalArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ConditionalArgs>(InParentAddress);
		AddOpFunc(Args.condition, TEXT("cond "));
		AddOpFunc(Args.yes, TEXT("true "));
		AddOpFunc(Args.no, TEXT("false "));
		break;
	}

	case UE::Mutable::Private::EOpType::IM_SWITCH:
	case UE::Mutable::Private::EOpType::LA_SWITCH:
	case UE::Mutable::Private::EOpType::ME_SWITCH:
	case UE::Mutable::Private::EOpType::CO_SWITCH:
	case UE::Mutable::Private::EOpType::SC_SWITCH:
	case UE::Mutable::Private::EOpType::NU_SWITCH:
	case UE::Mutable::Private::EOpType::IN_SWITCH:
	case UE::Mutable::Private::EOpType::ED_SWITCH:
	{
		const uint8* OpData = InProgram.GetOpArgsPointer(InParentAddress);

		UE::Mutable::Private::OP::ADDRESS VarAddress;
		FMemory::Memcpy(&VarAddress, OpData, sizeof(UE::Mutable::Private::OP::ADDRESS));
		OpData += sizeof(UE::Mutable::Private::OP::ADDRESS);
		AddOpFunc(VarAddress, TEXT("var "));

		UE::Mutable::Private::OP::ADDRESS DefAddress;
		FMemory::Memcpy(&DefAddress, OpData, sizeof(UE::Mutable::Private::OP::ADDRESS));
		OpData += sizeof(UE::Mutable::Private::OP::ADDRESS);
		AddOpFunc(DefAddress, TEXT("def "));

		UE::Mutable::Private::OP::FSwitchCaseDescriptor CaseDesc;
		FMemory::Memcpy(&CaseDesc, OpData, sizeof(UE::Mutable::Private::OP::FSwitchCaseDescriptor));
		OpData += sizeof(UE::Mutable::Private::OP::FSwitchCaseDescriptor);

		if (!CaseDesc.bUseRanges)
		{
			for (uint32 C = 0; C < CaseDesc.Count; ++C)
			{
				int32 Condition;
				FMemory::Memcpy(&Condition, OpData, sizeof(int32));
				OpData += sizeof(int32);

				UE::Mutable::Private::OP::ADDRESS At;
				FMemory::Memcpy(&At, OpData, sizeof(UE::Mutable::Private::OP::ADDRESS));
				OpData += sizeof(UE::Mutable::Private::OP::ADDRESS);

				FString Caption = FString::Printf(TEXT("case %d "), Condition);
				AddOpFunc(At, Caption);
			}
		}
		else
		{
			for (uint32 C = 0; C < CaseDesc.Count; ++C)
			{
				int32 ConditionStart;
				FMemory::Memcpy(&ConditionStart, OpData, sizeof(int32));
				OpData += sizeof(int32);

				uint32 RangeSize;
				FMemory::Memcpy(&RangeSize, OpData, sizeof(uint32));
				OpData += sizeof(uint32);

				UE::Mutable::Private::OP::ADDRESS At;
				FMemory::Memcpy(&At, OpData, sizeof(UE::Mutable::Private::OP::ADDRESS));
				OpData += sizeof(UE::Mutable::Private::OP::ADDRESS);

				FString Caption = FString::Printf(TEXT("case %d to %d "), ConditionStart, ConditionStart+RangeSize-1);
				AddOpFunc(At, Caption);
			}
		}

		break;
	}

	case UE::Mutable::Private::EOpType::IM_SWIZZLE:
	{
		UE::Mutable::Private::OP::ImageSwizzleArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageSwizzleArgs>(InParentAddress);
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			FString Caption = FString::Printf(TEXT("%d is %d from "), Channel, Args.sourceChannels[Channel]);
			AddOpFunc(Args.sources[Channel], Caption);
		}
		break;
	}

	case UE::Mutable::Private::EOpType::CO_SWIZZLE:
	{
		UE::Mutable::Private::OP::ColourSwizzleArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ColourSwizzleArgs>(InParentAddress);
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			FString Caption = FString::Printf(TEXT("%d is %d from "), Channel, Args.sourceChannels[Channel]);
			AddOpFunc(Args.sources[Channel], Caption);
		}
		break;
	}

	case UE::Mutable::Private::EOpType::IM_LAYER:
	{
		UE::Mutable::Private::OP::ImageLayerArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageLayerArgs>(InParentAddress);
		AddOpFunc(Args.base, TEXT("base "));
		AddOpFunc(Args.mask, TEXT("mask "));
		AddOpFunc(Args.blended, TEXT("blended "));
		break;
	}

	case UE::Mutable::Private::EOpType::IM_LAYERCOLOUR:
	{
		UE::Mutable::Private::OP::ImageLayerColourArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageLayerColourArgs>(InParentAddress);
		AddOpFunc(Args.base, TEXT("base "));
		AddOpFunc(Args.mask, TEXT("mask "));
		AddOpFunc(Args.colour, TEXT("colour "));
		break;
	}

	case UE::Mutable::Private::EOpType::IM_MULTILAYER:
	{
		UE::Mutable::Private::OP::ImageMultiLayerArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageMultiLayerArgs>(InParentAddress);
		AddOpFunc(Args.rangeSize, TEXT("range "));
		AddOpFunc(Args.base, TEXT("base "));
		AddOpFunc(Args.mask, TEXT("mask "));
		AddOpFunc(Args.blended, TEXT("blended "));
		break;
	}

	case UE::Mutable::Private::EOpType::ME_ADDMETADATA:
	{
		UE::Mutable::Private::OP::MeshAddMetadataArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshAddMetadataArgs>(InParentAddress);

		using OpEnumFlags = UE::Mutable::Private::OP::MeshAddMetadataArgs::EnumFlags;

		// Set count to 1 if is not a list.
		int32 TagCount      = int32(!EnumHasAnyFlags(Args.Flags, OpEnumFlags::IsTagList));
		int32 ResourceCount = int32(!EnumHasAnyFlags(Args.Flags, OpEnumFlags::IsResourceList));
		int32 SkeletonCount = int32(!EnumHasAnyFlags(Args.Flags, OpEnumFlags::IsSkeletonList));

		if (!TagCount)
		{
			TagCount = InProgram.ConstantUInt32Lists.IsValidIndex(Args.Tags.ListAddress) 
					? InProgram.ConstantUInt32Lists[Args.Tags.ListAddress].Num() 
					: 0;
		}

		if (!ResourceCount)
		{
			ResourceCount = InProgram.ConstantUInt64Lists.IsValidIndex(Args.ResourceIds.ListAddress) 
					? InProgram.ConstantUInt64Lists[Args.ResourceIds.ListAddress].Num() 
					: 0;
		}

		if (!SkeletonCount)
		{
			SkeletonCount = InProgram.ConstantUInt32Lists.IsValidIndex(Args.SkeletonIds.ListAddress) 
					? InProgram.ConstantUInt32Lists[Args.SkeletonIds.ListAddress].Num() 
					: 0;
		}

		FString Caption = FString::Printf(TEXT("add %d tags %d resources and %d skeletons to "), TagCount, ResourceCount, SkeletonCount);
		AddOpFunc(Args.Source, Caption);

		break;
	}

	case UE::Mutable::Private::EOpType::ME_APPLYLAYOUT:
	{
		UE::Mutable::Private::OP::MeshApplyLayoutArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshApplyLayoutArgs>(InParentAddress);
		AddOpFunc(Args.Layout, TEXT("layout "));
		AddOpFunc(Args.Mesh, TEXT("mesh "));
		break;
	}

	case UE::Mutable::Private::EOpType::ME_PREPARELAYOUT:
	{
		UE::Mutable::Private::OP::MeshPrepareLayoutArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshPrepareLayoutArgs>(InParentAddress);
		AddOpFunc(Args.Layout, TEXT("layout "));
		AddOpFunc(Args.Mesh, TEXT("mesh "));
		break;
	}

	case UE::Mutable::Private::EOpType::ME_DIFFERENCE:
	{
		const uint8* data = InProgram.GetOpArgsPointer(InParentAddress);

		UE::Mutable::Private::OP::ADDRESS BaseAt = 0;
		FMemory::Memcpy(&BaseAt, data, sizeof(UE::Mutable::Private::OP::ADDRESS));
		AddOpFunc(BaseAt, TEXT("base "));
		data += sizeof(UE::Mutable::Private::OP::ADDRESS);

		UE::Mutable::Private::OP::ADDRESS TargetAt = 0;
		FMemory::Memcpy(&TargetAt, data, sizeof(UE::Mutable::Private::OP::ADDRESS));
		AddOpFunc(TargetAt, TEXT("target "));
		data += sizeof(UE::Mutable::Private::OP::ADDRESS);
		
		break;
	}
		
	case UE::Mutable::Private::EOpType::ME_MORPH:
	{
		const uint8* Data = InProgram.GetOpArgsPointer(InParentAddress);

		UE::Mutable::Private::OP::ADDRESS FactorAt = 0;
		FMemory::Memcpy(&FactorAt, Data, sizeof(UE::Mutable::Private::OP::ADDRESS));
		AddOpFunc(FactorAt, TEXT("factor "));
		Data += sizeof(UE::Mutable::Private::OP::ADDRESS);
		
		UE::Mutable::Private::OP::ADDRESS BaseAt = 0;
		FMemory::Memcpy(&BaseAt, Data, sizeof(UE::Mutable::Private::OP::ADDRESS));
		AddOpFunc(BaseAt, TEXT("base "));
		Data += sizeof(UE::Mutable::Private::OP::ADDRESS);

		UE::Mutable::Private::OP::ADDRESS TargetAt = 0;
		FMemory::Memcpy(&TargetAt, Data, sizeof(UE::Mutable::Private::OP::ADDRESS));
		AddOpFunc(TargetAt, TEXT("target "));
		Data += sizeof(UE::Mutable::Private::OP::ADDRESS);
		
		break;
	}
		
	case UE::Mutable::Private::EOpType::SC_CURVE:
	{
		UE::Mutable::Private::OP::ScalarCurveArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ScalarCurveArgs>(InParentAddress);
		AddOpFunc(Args.time, TEXT("time "));
		AddOpFunc(Args.curve, TEXT("curve "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::CO_SAMPLEIMAGE:
	{
		UE::Mutable::Private::OP::ColourSampleImageArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ColourSampleImageArgs>(InParentAddress);
		AddOpFunc(Args.Image, TEXT("image "));
		AddOpFunc(Args.X, TEXT("x "));
		AddOpFunc(Args.Y, TEXT("y "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_RESIZELIKE:
	{
		UE::Mutable::Private::OP::ImageResizeLikeArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageResizeLikeArgs>(InParentAddress);
		AddOpFunc(Args.Source, TEXT("src "));
		AddOpFunc(Args.SizeSource, TEXT("sizeSrc "));
		break;
	}

	case UE::Mutable::Private::EOpType::IN_ADDMESH:
	case UE::Mutable::Private::EOpType::IN_ADDIMAGE:
	case UE::Mutable::Private::EOpType::IN_ADDVECTOR:
	case UE::Mutable::Private::EOpType::IN_ADDSCALAR:
	case UE::Mutable::Private::EOpType::IN_ADDSTRING:
	case UE::Mutable::Private::EOpType::IN_ADDCOMPONENT:
	case UE::Mutable::Private::EOpType::IN_ADDSURFACE:
	{
		UE::Mutable::Private::OP::InstanceAddArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(InParentAddress);
		AddOpFunc(Args.instance, TEXT("instance "));
		AddOpFunc(Args.value, TEXT("value "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_COMPOSE:
	{
		UE::Mutable::Private::OP::ImageComposeArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageComposeArgs>(InParentAddress);
		AddOpFunc(Args.layout, TEXT("layout "));
		AddOpFunc(Args.base, TEXT("base "));
		AddOpFunc(Args.blockImage, TEXT("blockImage "));
		AddOpFunc(Args.mask, TEXT("mask "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_INTERPOLATE:
	{
		UE::Mutable::Private::OP::ImageInterpolateArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageInterpolateArgs>(InParentAddress);
		AddOpFunc(Args.Factor, TEXT("factor "));
			
		int32 TargetIndex = 0;
		for (const UE::Mutable::Private::OP::ADDRESS& Operation : Args.Targets)
		{
			AddOpFunc(Operation, FString::Printf(TEXT("target %i "), TargetIndex++));
		}
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_SATURATE:
	{
		UE::Mutable::Private::OP::ImageSaturateArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageSaturateArgs>(InParentAddress);
		AddOpFunc(Args.Base, TEXT("base "));
		AddOpFunc(Args.Factor, TEXT("factor "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_COLOURMAP:
	{
		UE::Mutable::Private::OP::ImageColourMapArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageColourMapArgs>(InParentAddress);
		AddOpFunc(Args.Base, TEXT("base "));
		AddOpFunc(Args.Mask, TEXT("mask "));
		AddOpFunc(Args.Map, TEXT("map "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_BINARISE:
	{
		UE::Mutable::Private::OP::ImageBinariseArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageBinariseArgs>(InParentAddress);
		AddOpFunc(Args.Base, TEXT("base "));
		AddOpFunc(Args.Threshold, TEXT("threshold "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_PATCH:
	{
		UE::Mutable::Private::OP::ImagePatchArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImagePatchArgs>(InParentAddress);
		AddOpFunc(Args.base, TEXT("base "));
		AddOpFunc(Args.patch, TEXT("patch "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_RASTERMESH:
	{
		UE::Mutable::Private::OP::ImageRasterMeshArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageRasterMeshArgs>(InParentAddress);
		AddOpFunc(Args.mesh, TEXT("mesh "));
		AddOpFunc(Args.image, TEXT("image "));
		AddOpFunc(Args.mask, TEXT("mask "));
		AddOpFunc(Args.angleFadeProperties, TEXT("angleFadeProperties "));
		AddOpFunc(Args.projector, TEXT("projector "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_DISPLACE:
	{
		UE::Mutable::Private::OP::ImageDisplaceArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageDisplaceArgs>(InParentAddress);
		AddOpFunc(Args.Source, TEXT("src "));
		AddOpFunc(Args.DisplacementMap, TEXT("displacementMap "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_NORMALCOMPOSITE:
	{
		UE::Mutable::Private::OP::ImageNormalCompositeArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageNormalCompositeArgs>(InParentAddress);
		AddOpFunc(Args.base, TEXT("base "));
		AddOpFunc(Args.normal, TEXT("normal "));
		break;
	}
		
	case UE::Mutable::Private::EOpType::IM_TRANSFORM:
	{
		UE::Mutable::Private::OP::ImageTransformArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::ImageTransformArgs>(InParentAddress);
		AddOpFunc(Args.Base, TEXT("base "));
		AddOpFunc(Args.OffsetX, TEXT("offsetX "));
		AddOpFunc(Args.OffsetY, TEXT("offsetY "));
		AddOpFunc(Args.ScaleX, TEXT("scaleX "));
		AddOpFunc(Args.ScaleY, TEXT("scaleY "));
		AddOpFunc(Args.Rotation, TEXT("rotation "));
		break;
	}

	case UE::Mutable::Private::EOpType::SC_MATERIAL_BREAK:
	case UE::Mutable::Private::EOpType::CO_MATERIAL_BREAK:
	case UE::Mutable::Private::EOpType::IM_MATERIAL_BREAK:
	{
		UE::Mutable::Private::OP::MaterialBreakArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::MaterialBreakArgs>(InParentAddress);
		AddOpFunc(Args.Material, TEXT("Material "));
		break;
	}

	//TODO(Max) Add MI_Parameter and MI_CONSTANT

	// Add here more operation types to define how they are exposed in the tree (set a Caption)
		
	default:
		{
			// Generic list of child operations
			bUseGeneric = true;
			break;
		}
	}

	if (bUseGeneric)
	{
		// Find children of the provided element without adding any extra string to better identify what each of them represents
		UE::Mutable::Private::ForEachReference(InProgram, InParentAddress, [this, &InProgram, AddOpFunc](UE::Mutable::Private::OP::ADDRESS ChildAddress)
			{
				AddOpFunc(ChildAddress,TEXT(""));
			});
	}
	else
	{
		// Validate in case there is a mismatch in the custom processing of children and the generic one, which would cause problems.
		ChildIndex = 0;

		auto ValidateOpFunc = [this, InParentAddress, ParentOperationType, &ChildIndex](UE::Mutable::Private::OP::ADDRESS ChildAddress)
		{
			const FItemCacheKey Key = { InParentAddress, ChildAddress, ChildIndex };
			const TSharedPtr<FMutableCodeTreeElement>* CachedItem = ItemCache.Find(Key);

			// If this check fails could mean that in the switch above one type of OP processes its children in one
			// order but in "ForEachReference" that same operation processes the same children in another order.
			check(CachedItem);
				
			++ChildIndex;
		};

		UE::Mutable::Private::ForEachReference(InProgram, InParentAddress, [this, ValidateOpFunc](UE::Mutable::Private::OP::ADDRESS ChildAddress)
		{
			ValidateOpFunc(ChildAddress);
		});
	}
}

SMutableCodeViewer::EOperationComputationalCost SMutableCodeViewer::GetOperationTypeComputationalCost(UE::Mutable::Private::EOpType OperationType) const
{
	if (VeryExpensiveOperationTypes.Contains(OperationType))
	{
		return EOperationComputationalCost::VeryExpensive;
	}
	else if (ExpensiveOperationTypes.Contains(OperationType))
	{
		return EOperationComputationalCost::Expensive;
	}
	else
	{
		return EOperationComputationalCost::Standard;
	}
}

#pragma endregion 



#pragma region CodeTree Callbacks


TSharedRef<ITableRow> SMutableCodeViewer::GenerateRowForNodeTree(TSharedPtr<FMutableCodeTreeElement> InTreeNode, const TSharedRef<STableViewBase>& InOwnerTable)
{
	// Save the node for later access
	TreeElements.Add(InTreeNode);
	
	// Generate a row element
	TSharedRef<SMutableCodeTreeRow> Row = SNew(SMutableCodeTreeRow, InOwnerTable, InTreeNode);

	// Determine if a row should be painted as highlighted based on the selected item
	if (TreeView->GetNumItemsSelected())
	{
		const TSharedPtr<FMutableCodeTreeElement> SelectedElement = TreeView->GetSelectedItems()[0];
		if (SelectedElement != InTreeNode &&
			SelectedElement->MutableOperation > 0 &&
			InTreeNode->MutableOperation == SelectedElement->MutableOperation)
		{
			Row->Highlight();
		}
	}
	
	return Row;
}


void SMutableCodeViewer::GetChildrenForInfo(TSharedPtr<FMutableCodeTreeElement> InInfo, TArray<TSharedPtr<FMutableCodeTreeElement>>& OutChildren)
{
	if (!InInfo->MutableModel)
	{
		return;
	}

	check(MutableModel);
	const UE::Mutable::Private::FProgram& Program = MutableModel->GetPrivate()->Program;

	UE::Mutable::Private::OP::ADDRESS ParentAddress = InInfo->MutableOperation;

	// Generic case for unnamed children traversal.
	uint32 ChildIndex = 0;
	UE::Mutable::Private::ForEachReference(Program, InInfo->MutableOperation, [this, ParentAddress, &ChildIndex, &OutChildren](UE::Mutable::Private::OP::ADDRESS ChildAddress)
	{
		{
			const FItemCacheKey Key = { ParentAddress, ChildAddress, ChildIndex };
			const TSharedPtr<FMutableCodeTreeElement>* CachedItem = ItemCache.Find(Key);

			if (CachedItem)
			{
				OutChildren.Add(*CachedItem);
			}
			else
			{
				// if all elements have been already cached this should never happen
				checkNoEntry();
			}
		}
		++ChildIndex;
	});
}

void SMutableCodeViewer::OnExpansionChanged(TSharedPtr<FMutableCodeTreeElement> InItem, bool bInExpanded)
{
	// Update expanded state of the provided element
	InItem->bIsExpanded = bInExpanded;
	
	// If an element gets expanded then contract (if found) the other element that uses the same address
	if (bInExpanded)
	{
		const UE::Mutable::Private::OP::ADDRESS MutableOperation = InItem->MutableOperation;
		const TSharedPtr<FMutableCodeTreeElement>* PreviouslyExpandedElement = ExpandedElements.Find(MutableOperation);
		if (PreviouslyExpandedElement)
		{
			TreeView->SetItemExpansion(*PreviouslyExpandedElement, false);
		}

		// Only do this if in a situation where it may be required (do not do it if the tree has not been interacted with yet)
		if (bShouldRecalculateStates)
		{
			// Find all the children (recursive) of this item.
			TSet<TSharedPtr<FMutableCodeTreeElement>> FoundChildren;
			GetVisibleChildren(InItem, FoundChildren);
			for (const TSharedPtr<FMutableCodeTreeElement>& Child : FoundChildren)
			{
				// For each of the children found set it's state to be the one found on the expanded element
				Child->SetElementCurrentState(InItem->GetStateIndex());
			}
		}
		
		// Cache this element as one currently expanded
		ExpandedElements.Add(MutableOperation, InItem);
	}
	else
	{
		// Remove this element from the cache of expanded elements
		ExpandedElements.Remove(InItem->MutableOperation);
	}
}

void SMutableCodeViewer::GetVisibleChildren(TSharedPtr<FMutableCodeTreeElement> InInfo, TSet<TSharedPtr<FMutableCodeTreeElement>>& OutChildren)
{
	check(MutableModel);
	const UE::Mutable::Private::FProgram& MutableProgram = MutableModel->GetPrivate()->Program;
	
	TArray<TSharedPtr<FMutableCodeTreeElement>> ToSearchForChildren;
	ToSearchForChildren.Add(InInfo);
	while (!ToSearchForChildren.IsEmpty())
	{
		// Grab the first element in order to check for it's children
		const TSharedPtr<FMutableCodeTreeElement> ToCheck = ToSearchForChildren[0];
		ToSearchForChildren.RemoveAt(0);
		
		const UE::Mutable::Private::OP::ADDRESS ParentAddress = ToCheck->MutableOperation;
	
		// Generic case for unnamed children traversal.
		uint32 ChildIndex = 0;
		UE::Mutable::Private::ForEachReference(MutableProgram, ParentAddress, [this, ParentAddress, &ChildIndex, &OutChildren, &ToSearchForChildren ](UE::Mutable::Private::OP::ADDRESS ChildAddress)
		{
			{
				const FItemCacheKey Key = { ParentAddress, ChildAddress, ChildIndex };
				const TSharedPtr<FMutableCodeTreeElement> CachedItem = *ItemCache.Find(Key);

				// Since we have already generated all elements CachedItem should be therefore always a valid pointer
				check (CachedItem);
				
				// If the address has not been yet found then save it as one of the children affected
				if (!OutChildren.Contains(CachedItem))
				{
					OutChildren.Add(CachedItem);

					// And if the children is found to be expanded then also process it later to later return only the
					// elements that are expanded in the tree view (using data manually set on each tree element)
					if (CachedItem->bIsExpanded )
					{
						// Add for processing
						ToSearchForChildren.Add(CachedItem);
					}
				}
			}
			++ChildIndex;
		});
	}

	// Debug
	// UE_LOG(LogTemp,Warning,TEXT("Found a total of %i children elements "),OutChildren.Num());
}


void SMutableCodeViewer::OnSelectionChanged(TSharedPtr<FMutableCodeTreeElement> InNode, ESelectInfo::Type InSelectInfo)
{
	if (bIsElementHighlighted)
	{
		ClearHighlightedItems();
	}

	TArray<TSharedPtr<FMutableCodeTreeElement>> SelectedNodes;
	TreeView->GetSelectedItems(SelectedNodes);

	PreviewBorder->ClearContent();

	SelectedOperationAddress = 0;
	bSelectedOperationIsImage = false;

	if (SelectedNodes.IsEmpty())
	{
		return;
	}

	// Clear all selected items in the constant resources widget
	ConstantsWidget->ClearSelectedConstantItems();
	
	// Find the duplicates for the selected tree element and highlight them
	if (InNode)
	{
		HighlightDuplicatesOfEntry(InNode);
	}

	bIsPreviewPendingUpdate = true;

	SelectedOperationAddress = SelectedNodes[0]->MutableOperation;
	const UE::Mutable::Private::EOpType OperationType = MutableModel->GetPrivate()->Program.GetOpType(SelectedOperationAddress);
	const UE::Mutable::Private::EDataType OperationDataType = UE::Mutable::Private::GetOpDataType(OperationType);
	
	switch (OperationDataType)
	{
	case UE::Mutable::Private::EDataType::Layout:
	{
		// Create or reuse the UI
		PrepareLayoutViewer();
		break;
	}

	case UE::Mutable::Private::EDataType::Image:
	{
		// Create or reuse the UI
		bSelectedOperationIsImage = true;
		PrepareImageViewer();
		break;
	}

	case UE::Mutable::Private::EDataType::Mesh:
	{
		// Create or reuse the UI
		PrepareMeshViewer();
		break;
	}

	case UE::Mutable::Private::EDataType::Instance:
	{
		// Create or reuse the UI
		PrepareInstanceViewer();
		break;
	}

	case UE::Mutable::Private::EDataType::Scalar:
	{
		// Create or reuse the UI
		if (!PreviewScalarViewer)
		{
			PreviewScalarViewer = SNew(SMutableScalarViewer);
		}

		PreviewBorder->SetContent(PreviewScalarViewer.ToSharedRef());
		break;
	}

	case UE::Mutable::Private::EDataType::String:
	{
		// Create or reuse the UI
		PrepareStringViewer();
		break;
	}

	case UE::Mutable::Private::EDataType::Color:
	{
		// Create or reuse the UI
		if (!PreviewColorViewer)
		{
			PreviewColorViewer = SNew(SMutableColorViewer);
		}

		PreviewBorder->SetContent(PreviewColorViewer.ToSharedRef());
		break;
	}

	case UE::Mutable::Private::EDataType::Int:
	{
		// Create or reuse the UI
		if (!PreviewIntViewer)
		{
			PreviewIntViewer = SNew(SMutableIntViewer);
		}

		PreviewBorder->SetContent(PreviewIntViewer.ToSharedRef());
		break;
	}

	case UE::Mutable::Private::EDataType::Bool:
	{
		// Create or reuse the UI
		if (!PreviewBoolViewer)
		{
			PreviewBoolViewer = SNew(SMutableBoolViewer);
		}

		PreviewBorder->SetContent(PreviewBoolViewer.ToSharedRef());
		break;
	}

	case UE::Mutable::Private::EDataType::Projector:
	{
		// Create or reuse the UI
		PrepareProjectorViewer();
		break;
	}

	
	default:
		// There is no viewer for this type yet.
		break;

	}
}

TSharedPtr<SWidget> SMutableCodeViewer::OnTreeContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// Only show the Ui for operations different from "None" or "0"
	if (TreeView->GetSelectedItems().Num() && TreeView->GetSelectedItems()[0]->MutableOperation > 0)
	{
		if (TreeView->GetSelectedItems().Num() == 1)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("Set_as_search_operation_type","Set as search Operation"),
				LOCTEXT("Set_as_search_operation_type_Tooltip", "Sets the type of this operation as the type to be looking for when searching for operations on the tree view"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SMutableCodeViewer::OnSelectedOperationTypeFromTree)
				)	
			);
		}

		MenuBuilder.AddMenuSeparator();
		
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Code_Expand_Selected", "Expand Selected Operation"),
			LOCTEXT("Code_Expand_Selected_Tooltip", "Expands only the selected Operation and leaves the other as they are."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SMutableCodeViewer::TreeExpandSelected)
			)
		);
	}


	MenuBuilder.AddMenuEntry(
		LOCTEXT("Code_Expand_Instance", "Expand Instance-Level Operations"),
		LOCTEXT("Code_Expand_Instance_Tooltip", "Expands all the operations in the tree that are instance operations (not images, meshes, booleans, etc.)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMutableCodeViewer::TreeExpandInstance)
			//, FCanExecuteAction::CreateSP(this, &SMutableCodeViewer::HasAnyItemInPalette)
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Code_Expand_Unique", "Expand All Unique Operations"),
		LOCTEXT("Code_Expand_Unique_Tooltip", "Expands all the operations in the tree that have not been expanded yet."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMutableCodeViewer::TreeExpandUnique)
		)
	);

	return MenuBuilder.MakeWidget();
}

void SMutableCodeViewer::TreeExpandRecursive(TSharedPtr<FMutableCodeTreeElement> InInfo, bool bExpand)
{
	if (bExpand)
	{
		TreeExpandUnique();
	}
}

void SMutableCodeViewer::OnRowReleased(const TSharedRef<ITableRow>& InTreeRow)
{
	SMutableCodeTreeRow* CastedTableRow = static_cast<SMutableCodeTreeRow*>(&InTreeRow.Get());
	const TSharedPtr<FMutableCodeTreeElement>& RowElement = CastedTableRow->GetItem();
	TreeElements.Remove(RowElement);
}
#pragma endregion


#pragma region Highlight Methods
void SMutableCodeViewer::HighlightDuplicatesOfEntry(const TSharedPtr<FMutableCodeTreeElement>& InTargetEntry)
{
	if (bIsElementHighlighted)
	{
		ClearHighlightedItems();
	}

	// Do not highlight empty entries
	if (InTargetEntry->MutableOperation == 0)
	{
		return;
	}
	
	// Highlight the elements related to the currently selected item of the tree
	HighlightedOperation = InTargetEntry->MutableOperation;

	for (const TSharedPtr<FMutableCodeTreeElement>& TreeItem : TreeElements)
	{
		if (TreeItem.Get() != InTargetEntry.Get() && TreeItem->MutableOperation == HighlightedOperation)
		{
			TSharedPtr<ITableRow> TableRow = TreeView->WidgetFromItem(TreeItem);
			SMutableCodeTreeRow* MutableRow = static_cast<SMutableCodeTreeRow*>(TableRow.Get());
			MutableRow->Highlight();
		}
	}

	bIsElementHighlighted = true;
}


void SMutableCodeViewer::ClearHighlightedItems()
{
	// Clear the previously highlighted elements
	for (const TSharedPtr<FMutableCodeTreeElement>& HighlightedElement : TreeElements)
	{
		if (HighlightedElement->MutableOperation == HighlightedOperation)
		{
			TSharedPtr<ITableRow> TableRow = TreeView->WidgetFromItem(HighlightedElement);

			if (TableRow.IsValid())
			{
				SMutableCodeTreeRow* MutableRow = static_cast<SMutableCodeTreeRow*>(TableRow.Get());
				MutableRow->ResetHighlight();
			}
		}
	}

	bIsElementHighlighted = false;
}
#pragma endregion

#pragma region Element Expansion Llogic


void SMutableCodeViewer::TreeExpandElements(TArray<TSharedPtr<FMutableCodeTreeElement>>& InElementsToExpand, 
	bool bForceExpandDuplicates /*= false*/,
	UE::Mutable::Private::EDataType FilteringDataType /*= UE::Mutable::Private::EDataType::None*/,
	TSharedPtr<FProcessedOperationsBuffer> InExpandedOperationsBuffer /* = nullptr */)
{
	if (InElementsToExpand.IsEmpty())
	{
		return;
	}
	
	// Initialization of recursive elements if this is the first invocation of method
	{
		if (!InExpandedOperationsBuffer)
		{
			InExpandedOperationsBuffer = MakeShared<FProcessedOperationsBuffer>();
		}
	}

	// Load references to the arrays containing all the operations already worked on during another recursive call to this
	// method
	TArray<UE::Mutable::Private::OP::ADDRESS>& AlreadyExpandedOriginalOperations = InExpandedOperationsBuffer->ExpandedOriginalOperations;
	TArray<UE::Mutable::Private::OP::ADDRESS>& AlreadyExpandedDuplicatedOperations = InExpandedOperationsBuffer->ExpandedDuplicatedOperations;
	
	// Array containing the children object found on Item.
	TArray<TSharedPtr<FMutableCodeTreeElement>> Children;
	
	// Index of the current element being processed
	int32 CurrentElementIndex = 0;
	while (CurrentElementIndex < InElementsToExpand.Num() )
	{
		// Grab the current element to process and move the index forward once
		TSharedPtr<FMutableCodeTreeElement> Item = InElementsToExpand[CurrentElementIndex++];
		check(Item);

		// Identifier of the element. May be repeated if there are elements duplicating another element
		const UE::Mutable::Private::OP::ADDRESS OperationAddress = Item->MutableOperation;
		
		// Filter the elements being expanded if the user has defined a desired EDataType
		if (FilteringDataType != UE::Mutable::Private::EDataType::None)
		{
			const UE::Mutable::Private::EOpType OperationType = Item->MutableModel->GetPrivate()->Program.GetOpType(OperationAddress);
			const UE::Mutable::Private::EDataType OperationDataType = UE::Mutable::Private::GetOpDataType(OperationType);

			// If it is not of the desired type then ignore it and continue to the next pending element
			if (OperationDataType != FilteringDataType)
			{
				continue;
			}
		}

		// Reset the children array
		Children.SetNum(0);
		
		// If not duplicated expand it and grab the children to be also expanded on the next loop
		if (Item->DuplicatedOf == nullptr )
		{
			// Was this unique element expanded before (only valid if also expanding duplicates)
			bool bHasBeenExpandedPreviously = false;
			
			// Mind duplicated original elements if dealing with duplicated operation expansions.
			if (bForceExpandDuplicates)
			{
				// Make sure we have not already expanded this item to avoid recursive expansions of the same item and
				// children
				bHasBeenExpandedPreviously = AlreadyExpandedOriginalOperations.Contains(OperationAddress);
			}
			
			// Only check for duplicated original elements when working with duplicates
			if (!bHasBeenExpandedPreviously )
			{
				// Get the children of this unique element and prepare them for processing
				GetChildrenForInfo(Item, Children);

				// Call for the expansion of the children first
				TreeExpandElements(Children, bForceExpandDuplicates, FilteringDataType,InExpandedOperationsBuffer);

				// At this point all the children objects that needed expansion are already expanded so we can proceed with
				// the expansion of this element
				
				{
					// If we do expect to expand duplicates make sure we record this object as being expanded to be later able
					// to block the expansion of duplicates of this object
					if (bForceExpandDuplicates)
					{
						// Register this node as expanded so other nodes are able to check if it has already been worked with
						AlreadyExpandedOriginalOperations.Add(OperationAddress);
					}

					// Only ask for the expansion of the element if we know it can be expanded due to it having
					// children
					if (!Children.IsEmpty())
					{
						// Expand this unique element
						TreeView->SetItemExpansion(Item, true);
					}
				}
			}
			
		}
		// If it is a duplicated node
		else
		{
			// Special behavior where we expand duplicates if parent is not found to be expanded
			if (bForceExpandDuplicates)
			{
				// Was this element expanded as an original operation? We only want to expand the duplicate if the original
				// was not duplicated before
				bool bOriginalElementHasBeenExpanded = false;

				// Was this element expanded on a duplicated element? we only want to expand the first duplicate!
				const bool bOtherDuplicateOfSameOpWasExpandedBefore = AlreadyExpandedDuplicatedOperations.Contains(OperationAddress);

				// Only check if there is another original element with the same operation if we know that there is not another
				// duplicated element using this operation
				if (!bOtherDuplicateOfSameOpWasExpandedBefore)
				{
					bOriginalElementHasBeenExpanded = AlreadyExpandedOriginalOperations.Contains(OperationAddress);
				}
				
				// Was this operation expanded before?
				const bool bWasOperationExpandedPreviously = bOriginalElementHasBeenExpanded || bOtherDuplicateOfSameOpWasExpandedBefore;

				// If this operation have not yet been expanded then expand it!
				// Duplicates do not have priority over original elements.
				if ( !bWasOperationExpandedPreviously )
				{
					// Mark the children to be expanded later if conditions are met
					GetChildrenForInfo(Item, Children);

					// Expand the children objects
					TreeExpandElements(Children, bForceExpandDuplicates, FilteringDataType,InExpandedOperationsBuffer);

					// At this point all the children objects that needed expansion are already expanded so we can proceed with
					// the expansion of this element
					
					{
						// Record this node being expanded
						AlreadyExpandedDuplicatedOperations.Add(OperationAddress);

						// Only ask for the expansion of the element if we know it can be expanded due to it having
						// children
						if (!Children.IsEmpty())
						{
							// Expand the current element since we know it is from a operation not yet expanded
							TreeView->SetItemExpansion(Item, true);
						}
					}
				}
			}
		}
	}
}


void SMutableCodeViewer::TreeExpandSelected()
{
	// Get the selected items and expand them excluding the duplicates
	TArray<TSharedPtr<FMutableCodeTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	TreeExpandElements(SelectedItems,true);
}

void SMutableCodeViewer::TreeExpandUnique()
{
	// Expand the tree from the root and do not expand the duplicated elements
	TreeExpandElements(RootNodes);
}

void SMutableCodeViewer::TreeExpandInstance()
{
	// Expand only the items that match the datatype provided
	TreeExpandElements(RootNodes,false, UE::Mutable::Private::EDataType::Instance);
}


void SMutableCodeViewer::CacheRootNodeAddresses()
{
	check (MutableModel);
	check (RootNodeAddresses.IsEmpty())

	TArray<UE::Mutable::Private::OP::ADDRESS> FoundRootNodeAddresses;
	
	const UE::Mutable::Private::FModel::Private* ModelPrivate = MutableModel->GetPrivate();
	const int32 StateCount = ModelPrivate->Program.States.Num();
	for ( int32 StateIndex=0; StateIndex < StateCount; ++StateIndex )
	{
		const UE::Mutable::Private::FProgram::FState& State = ModelPrivate->Program.States[StateIndex];
		FoundRootNodeAddresses.Add(State.Root);
	}

	RootNodeAddresses = MoveTemp(FoundRootNodeAddresses);
}

 void SMutableCodeViewer::CacheAddressesRelatedWithConstantResource(const UE::Mutable::Private::EDataType ConstantDataType,
	const int32 IndexOnConstantsArray)
{
	check(MutableModel);
	check(RootNodeAddresses.Num());

	if (IndexOnConstantsArray < 0)
	{
		// Not valid index.
		UE_LOG(LogTemp,Error,TEXT("The provided index [%d] is not valid."),IndexOnConstantsArray );
		return;
	}

	// Object containing all data required by the search operation to be able to be called recursively
	FElementsSearchCache SearchPayload;
	// Initialize the Search Payload with the root node addresses. This way the search will use them as the root nodes where
	// to start searching
	SearchPayload.SetupRootBatch(RootNodeAddresses);
	
	// Main update procedure run for the targeted state and the targeted parameter values
	const UE::Mutable::Private::FProgram& Program = MutableModel->GetPrivate()->Program; 
	GetOperationsReferencingConstantResource(ConstantDataType,IndexOnConstantsArray,SearchPayload, Program);
	
	// At this point we did get all the addresses of operations that do involve the usage of our resource
	if (SearchPayload.FoundElements.Num() > 0)
	{
		// Set the type operation type to CONST_BASED_NAVIGATION (used to tell the user what is happening)
		TargetedTypeSelector->SetSelectedItem(ConstantBasedNavigationEntry);

		// Dump the located resources array onto the navigation array since we have content to navigate over
		NavigationElements = MoveTemp(SearchPayload.FoundElements);
		SortElementsByTreeIndex(NavigationElements);
	}
	else
	{
		TargetedTypeSelector->SetSelectedItem(NoneOperationEntry);
		NavigationElements.Reset();
		UE_LOG(LogTemp,Error,TEXT("The provided constant index does not seem to be used anywhere : Make sure the index is valid and that IsConstantResourceUsedByOperation() switch is up to date"));
	}

	// Reset the navigation index
	NavigationIndex = -1;
}


void SMutableCodeViewer::GetOperationsReferencingConstantResource(
	const UE::Mutable::Private::EDataType ConstantDataType,
	const int32 IndexOnConstantsArray,
	FElementsSearchCache& InSearchPayload,
	const UE::Mutable::Private::FProgram& InProgram)
{
	// next batch of addresses to be explored 
	TArray<FItemCacheKey> NextBatchAddressesData;
	
	for	(int32 ParentIndex = 0 ; ParentIndex < InSearchPayload.BatchData.Num(); ParentIndex++)
	{
		// Get one of the previous run "children" and treat as a parent to get it's children and process them
		const UE::Mutable::Private::OP::ADDRESS& ParentAddress = InSearchPayload.BatchData[ParentIndex].Child;
		
		// Cache if same data type and we share the same address (means this op is pointing at the provided resource)
		// It will cache duplicated entries
		if (IsConstantResourceUsedByOperation(IndexOnConstantsArray, ConstantDataType, ParentAddress,InProgram))
		{
			// Since this element is related with the provided constant resource cache it on InSearchPayload.FoundElements
			InSearchPayload.AddToFoundElements(ParentAddress,ParentIndex,ItemCache);
		}
		
		// Get all NON PROCESSED the children of this operation to later be able to process them (on next recursive call)
		InSearchPayload.CacheChildrenOfAddressIfNotProcessed(ParentAddress, InProgram, NextBatchAddressesData);
	}

	// At this point all the addresses to be computed on the next batch have already been set and will be computed on
	// the next recursive call
	
	// Explore children if found 
	if (NextBatchAddressesData.Num())
	{
		// Cache next batch data so the next invocations is able to locate the provided addresses on the itemsCache
		InSearchPayload.BatchData = MoveTemp(NextBatchAddressesData);
		
		GetOperationsReferencingConstantResource(ConstantDataType, IndexOnConstantsArray, InSearchPayload, InProgram);
	}
}

bool SMutableCodeViewer::IsConstantResourceUsedByOperation(const int32 IndexOnConstantsArray,
	const UE::Mutable::Private::EDataType ConstantDataType, const UE::Mutable::Private::OP::ADDRESS OperationAddress, const UE::Mutable::Private::FProgram& InProgram) const
{
	// Cache the current operation type to know where to look and what to check
	const UE::Mutable::Private::EOpType OperationType = InProgram.GetOpType(OperationAddress);

	// Making usage of the operation data type is not valid since some operations while return one type do, in fact,
	// contain data from other types (like the mesh constant for example that contains mesh, skeleton and physics asset)
	// const UE::Mutable::Private::EDataType DataType = UE::Mutable::Private::GetOpDataType(OperationType);
	// if (DataType != ConstantDataType)
	// {
	// 	return false;
	// }

	// Is this operation referencing (by an index) the index we are providing from a constants array
	bool bResourceLocated = false;
	
	// Check if the operation data type is compatible with the type of resources we are providing
	switch (ConstantDataType)
	{
		case UE::Mutable::Private::EDataType::String:
			{
				// TIP: To know if they represent a constant value check the code on code runner to see if they read from the constants array
				if (OperationType == UE::Mutable::Private::EOpType::ST_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::ResourceConstantArgs>(OperationAddress).value;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDSTRING)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDMESH)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if(OperationType == UE::Mutable::Private::EOpType::IN_ADDIMAGE)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDVECTOR)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDSCALAR)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDCOMPONENT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDSURFACE)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(OperationAddress).name;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_CONDITIONAL)
				{
					UE::Mutable::Private::OP::ConditionalArgs Arguments = InProgram.GetOpArgs<UE::Mutable::Private::OP::ConditionalArgs>(OperationAddress);
					
					if (IndexOnConstantsArray == Arguments.condition)
					{
						bResourceLocated = true;
						break;
					}

					if (IndexOnConstantsArray == Arguments.yes)
					{
						bResourceLocated = true;
						break;
					}

					if (IndexOnConstantsArray == Arguments.no)
					{
						bResourceLocated = true;
						break;
					}
				}
				else if (OperationType == UE::Mutable::Private::EOpType::IN_ADDEXTENSIONDATA)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::InstanceAddExtensionDataArgs>(OperationAddress).ExtensionDataName;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::ME_BINDSHAPE)
				{
					UE::Mutable::Private::OP::MeshBindShapeArgs Arguments = InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshBindShapeArgs>(OperationAddress);
					const uint8_t* Data = InProgram.GetOpArgsPointer(OperationAddress);

					// Bones are stored after the args
					Data += sizeof(Arguments);

					// Iterate over the bones and check if they point to the same index on the string constants array
					int32 NumBones;
					FMemory::Memcpy(&NumBones, Data, sizeof(int32)); 
					Data += sizeof(int32);
				
					for (int32 Bone = 0; Bone < NumBones; ++Bone)
					{
						// Exit once we know that the data is pointing to the index provided
						if (*Data == IndexOnConstantsArray)
						{
							bResourceLocated = true;
							continue;
						}
						
						Data += sizeof(int32);
					}
				}
				else if (OperationType == UE::Mutable::Private::EOpType::ME_ADDMETADATA)
				{
					 const uint8* OpData = InProgram.GetOpArgsPointer(OperationAddress);
					
					 UE::Mutable::Private::OP::ADDRESS SourceAddress;
					 FMemory::Memcpy(&SourceAddress, OpData, sizeof(UE::Mutable::Private::OP::ADDRESS));
					 OpData += sizeof(UE::Mutable::Private::OP::ADDRESS);
					
					 uint16 TagCount;
					 FMemory::Memcpy(&TagCount, OpData, sizeof(uint16));
					 OpData += sizeof(uint16);
					
					for (int32 TagIndex = 0; TagIndex < TagCount; ++TagIndex)
					{
						// Exit once we know that the data is pointing to the index provided
						if (*OpData == IndexOnConstantsArray)
						{
							bResourceLocated = true;
							continue;
						}
						
						OpData += sizeof(uint16);
					}
				}

				break;
			}

		case UE::Mutable::Private::EDataType::Image:
			{
				if (OperationType == UE::Mutable::Private::EOpType::IM_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::ResourceConstantArgs>(OperationAddress).value;
				} 
				
				break;
			}

		case UE::Mutable::Private::EDataType::Mesh:
			{
				if (OperationType == UE::Mutable::Private::EOpType::ME_CONSTANT)
				{
					UE::Mutable::Private::OP::MeshConstantArgs Args = InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshConstantArgs>(OperationAddress);
					UE::Mutable::Private::FConstantResourceIndex ResourceIndex = *reinterpret_cast<const UE::Mutable::Private::FConstantResourceIndex*>(&Args.Value);
					
					if (!ResourceIndex.Streamable)
					{
						bResourceLocated = IndexOnConstantsArray == ResourceIndex.Index;
					}
					else
					{
						int32 DebuggerIndexOffset = InProgram.ConstantMeshesPermanent.Num();
						for (const TPair<uint32, TSharedPtr<const UE::Mutable::Private::FMesh>>& Entry : InProgram.ConstantMeshesStreamed)
						{
							if (Entry.Key == ResourceIndex.Index)
							{
								bResourceLocated = IndexOnConstantsArray == DebuggerIndexOffset;
								break;
							}
							++DebuggerIndexOffset;
						}
					}
				}
				break;
			}

		case UE::Mutable::Private::EDataType::Layout:
			{
				if (OperationType == UE::Mutable::Private::EOpType::LA_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::ResourceConstantArgs>(OperationAddress).value;
				}
				break;
			}

		case UE::Mutable::Private::EDataType::Projector:
			{
				if (OperationType == UE::Mutable::Private::EOpType::PR_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::ResourceConstantArgs>(OperationAddress).value;
				}
				break;
			}

		case UE::Mutable::Private::EDataType::Matrix:
			{
				if (OperationType == UE::Mutable::Private::EOpType::ME_TRANSFORM)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshTransformArgs>(OperationAddress).matrix;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::ME_TRANSFORMWITHMESH)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshTransformWithinMeshArgs>(OperationAddress).matrix;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::ME_TRANSFORMWITHBONE)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshTransformWithBoneArgs>(OperationAddress).Matrix;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::MA_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MatrixConstantArgs>(OperationAddress).value;
				}
				break;
			}

		case UE::Mutable::Private::EDataType::Shape:
			{
				if (OperationType == UE::Mutable::Private::EOpType::ME_CLIPMORPHPLANE)
				{
					const UE::Mutable::Private::OP::MeshClipMorphPlaneArgs Arguments =  InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshClipMorphPlaneArgs>(OperationAddress);
					
					// Morph shape
					bResourceLocated = IndexOnConstantsArray == Arguments.MorphShape;
					if (bResourceLocated)
					{
						break;
					}

					if (Arguments.VertexSelectionType == EClipVertexSelectionType::Shape)
					{
						// Selection Shape
						bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshClipMorphPlaneArgs>(OperationAddress).VertexSelectionShapeOrBone;
					}
				}
				break;
			}
		
		case UE::Mutable::Private::EDataType::Curve:
			{
				if (OperationType == UE::Mutable::Private::EOpType::SC_CURVE)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::ScalarCurveArgs>(OperationAddress).curve;
				}
				break;
			}

		case UE::Mutable::Private::EDataType::Skeleton:
			{
				if (OperationType == UE::Mutable::Private::EOpType::ME_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshConstantArgs>(OperationAddress).Skeleton;
				}
				else if (OperationType == UE::Mutable::Private::EOpType::ME_SETSKELETON)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshSetSkeletonArgs>(OperationAddress).Skeleton;
				}
				break;
			}
		case UE::Mutable::Private::EDataType::PhysicsAsset:
			{
				if (OperationType == UE::Mutable::Private::EOpType::ME_CONSTANT)
				{
					bResourceLocated = IndexOnConstantsArray == InProgram.GetOpArgs<UE::Mutable::Private::OP::MeshConstantArgs>(OperationAddress).Value;
				}
				break;
			}

		// Invalid types
		case UE::Mutable::Private::EDataType::None:
		default:
			{
				checkNoEntry();
			}
	}


	return bResourceLocated;
}


void SMutableCodeViewer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	// After the tick we do know the tree has been refreshed, so all expansion and contraction operations have been
	// completed and the new data has been loaded onto our listening arrays. Then its safe to expect the widgets to be
	// there to be selected or inspected.
	if (!TreeView->IsPendingRefresh())
	{
		/** If we have expanded the tree elements in order to reach one of them then continue the operation */
		if (bWasUniqueExpansionInvokedForNavigation || bWasScrollToTargetRequested)
		{
			FocusViewOnNavigationTarget(ToFocusElement);
		}
	}
	
	
	if (!bIsPreviewPendingUpdate)
	{
		return;
	}

	bIsPreviewPendingUpdate = false;

	const UE::Mutable::Private::EOpType OperationType = MutableModel->GetPrivate()->Program.GetOpType(SelectedOperationAddress);
	const UE::Mutable::Private::EDataType OperationDataType = UE::Mutable::Private::GetOpDataType(OperationType);

	UE::Mutable::Private::FSettings Settings;
	const TSharedPtr<UE::Mutable::Private::FSystem> System = MakeShared<UE::Mutable::Private::FSystem>(Settings);
	
	System->GetPrivate()->BeginBuild(MutableModel, Object->GetPrivate()->MeshIdRegistry, Object->GetPrivate()->ImageIdRegistry, Object->GetPrivate()->MaterialIdRegistry);

	switch (OperationDataType)
	{
	case UE::Mutable::Private::EDataType::Layout:
	{
		check(PreviewLayoutViewer);
		TSharedPtr<const UE::Mutable::Private::FLayout> MutableLayout = System->GetPrivate()->BuildLayout(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewLayoutViewer->SetLayout(MutableLayout);
		break;
	}

	case UE::Mutable::Private::EDataType::Image:
	{
		check(PreviewImageViewer);
		TSharedPtr<const UE::Mutable::Private::FImage> MutableImage = System->GetPrivate()->BuildImage(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress, MipsToSkip, 0);
		PreviewImageViewer->SetImage(MutableImage, 0);
		break;
	}

	case UE::Mutable::Private::EDataType::Mesh:
	{
		check(PreviewMeshViewer);
		TSharedPtr<const UE::Mutable::Private::FMesh> MutableMesh = System->GetPrivate()->BuildMesh(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress, UE::Mutable::Private::EMeshContentFlags::AllFlags);
		PreviewMeshViewer->SetMesh(MutableMesh);
		break;
	}

	case UE::Mutable::Private::EDataType::Instance:
	{
		check(PreviewInstanceViewer);
		TSharedPtr<const UE::Mutable::Private::FInstance> MutableInstance = System->GetPrivate()->BuildInstance(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewInstanceViewer->SetInstance(MutableInstance, MutableModel, PreviewParameters, *System);
		break;
	}

	case UE::Mutable::Private::EDataType::Bool:
	{
		check(PreviewBoolViewer);
		const bool MutableBool = System->GetPrivate()->BuildBool(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewBoolViewer->SetBool(MutableBool);
		break;
	}

	case UE::Mutable::Private::EDataType::Int:
	{
		check(PreviewIntViewer);
		const int32 MutableInt = System->GetPrivate()->BuildInt(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewIntViewer->SetInt(MutableInt);
		break;
	}

	case UE::Mutable::Private::EDataType::Scalar:
	{
		check(PreviewScalarViewer);
		const float MutableScalar = System->GetPrivate()->BuildScalar(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewScalarViewer->SetScalar(MutableScalar);
		break;
	}

	case UE::Mutable::Private::EDataType::String:
	{
		check(PreviewStringViewer);
		TSharedPtr<const UE::Mutable::Private::String> MutableString = System->GetPrivate()->BuildString(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		const FText MutableText = FText::FromString(FString(MutableString->GetValue()));
		PreviewStringViewer->SetString(MutableText);
		break;
	}

	case UE::Mutable::Private::EDataType::Color:
	{
		check(PreviewColorViewer);
		FVector4f Color = System->GetPrivate()->BuildColour(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewColorViewer->SetColor(Color);
		break;
	}

	case UE::Mutable::Private::EDataType::Projector:
	{
		check (PreviewProjectorViewer);
		UE::Mutable::Private::FProjector Projector = System->GetPrivate()->BuildProjector(ExternalResourceProvider, MutableModel, PreviewParameters.Get(), SelectedOperationAddress);
		PreviewProjectorViewer->SetProjector(Projector);
	}
	
	default:
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
		UE_LOG(LogMutable, Log, TEXT("There is no previewer for the selected type of Mutable object"))
#endif
		// There is no viewer for this type.
		break;
	}

	System->GetPrivate()->EndBuild();

}


void SMutableCodeViewer::OnPreviewParameterValueChanged(int32 ParamIndex)
{
	// This is deferred to the tick to avoid multiple updates per frame.
	bIsPreviewPendingUpdate = true;
}


void SMutableCodeViewer::PrepareStringViewer()
{
	if (!PreviewStringViewer)
	{
		PreviewStringViewer = SNew(SMutableStringViewer);
	}

	PreviewBorder->SetContent(PreviewStringViewer.ToSharedRef());
}

void SMutableCodeViewer::PrepareImageViewer()
{
	if (!PreviewImageViewer)
	{
		PreviewImageViewer = SNew(SMutableImageViewer)
			.GridSize(FIntPoint(8, 8));
	}

	PreviewBorder->SetContent(PreviewImageViewer.ToSharedRef());
}

void SMutableCodeViewer::PrepareMeshViewer()
{
	if (!PreviewMeshViewer)
	{
		PreviewMeshViewer = SNew(SMutableMeshViewer);
	}

	PreviewBorder->SetContent(PreviewMeshViewer.ToSharedRef());
}

void SMutableCodeViewer::PrepareInstanceViewer()
{
	if (!PreviewInstanceViewer)
	{
		PreviewInstanceViewer = SNew(SMutableInstanceViewer, ExternalResourceProvider);
	}

	PreviewBorder->SetContent(PreviewInstanceViewer.ToSharedRef());
}

void SMutableCodeViewer::PrepareLayoutViewer()
{
	if (!PreviewLayoutViewer)
	{
		PreviewLayoutViewer = SNew(SMutableLayoutViewer);
	}

	PreviewBorder->SetContent(PreviewLayoutViewer.ToSharedRef());
}


void SMutableCodeViewer::PrepareProjectorViewer()
{
	if (!PreviewProjectorViewer)
	{
		PreviewProjectorViewer = SNew(SMutableProjectorViewer);
	}

	PreviewBorder->SetContent(PreviewProjectorViewer.ToSharedRef());
}


void SMutableCodeViewer::PreviewMutableString(const FString& InString)
{
	// Prepare the previewer object to receive data 
	PrepareStringViewer();
	
	//  Provide the desired data to the previewer object
	const FText TextToShow = FText::FromString(InString);
	PreviewStringViewer->SetString(TextToShow);
}


void SMutableCodeViewer::PreviewMutableImage(TSharedPtr<const UE::Mutable::Private::FImage> InImagePtr)
{
	PrepareImageViewer();
	PreviewImageViewer->SetImage(InImagePtr,0);
}


void SMutableCodeViewer::PreviewMutableMesh(TSharedPtr<const UE::Mutable::Private::FMesh> InMeshPtr)
{
	PrepareMeshViewer();
	PreviewMeshViewer->SetMesh(InMeshPtr);
}


void SMutableCodeViewer::PreviewMutableLayout(TSharedPtr<const UE::Mutable::Private::FLayout> Layout)
{
	PrepareLayoutViewer();
	PreviewLayoutViewer->SetLayout(Layout);
}


void SMutableCodeViewer::PreviewMutableProjector(const UE::Mutable::Private::FProjector* Projector)
{
	if (!Projector)
	{
		UE_LOG(LogTemp,Error,TEXT("Unable to preview data on null Projector pointer."))
		return;
	}
	
	PrepareProjectorViewer();
	PreviewProjectorViewer->SetProjector(*Projector);
}


void SMutableCodeViewer::PreviewMutableSkeleton(TSharedPtr<const UE::Mutable::Private::FSkeleton> Skeleton)
{
	if (!PreviewSkeletonViewer)
	{
		PreviewSkeletonViewer = SNew(SMutableSkeletonViewer);
	}

	PreviewBorder->SetContent(PreviewSkeletonViewer.ToSharedRef());
	
	PreviewSkeletonViewer->SetSkeleton(Skeleton);
}


void SMutableCodeViewer::PreviewMutableCurve(const FRichCurve& Curve)
{
	if (!PreviewCurveViewer)
	{
		PreviewCurveViewer = SNew(SMutableCurveViewer);
	}

	PreviewBorder->SetContent(PreviewCurveViewer.ToSharedRef());
	
	PreviewCurveViewer->SetCurve(Curve);
}

// TODO: Implement physics viewer
void SMutableCodeViewer::PreviewMutablePhysics(TSharedPtr<const UE::Mutable::Private::FPhysicsBody> Physics)
{
	UE_LOG(LogMutable, Warning, TEXT("Previewer for Mutable Physics not yet implemented"))
}

// TODO: Implement matrix viewer
void SMutableCodeViewer::PreviewMutableMatrix(const FMatrix44f& Mat)
{
	UE_LOG(LogMutable, Warning, TEXT("Previewer for Mutable Matrices not yet implemented"))
}

// TODO: Implement shape viewer
void SMutableCodeViewer::PreviewMutableShape(const UE::Mutable::Private::FShape* Shape)
{
	UE_LOG(LogMutable, Warning, TEXT("Previewer for Mutable Shapes not yet implemented"))
}


FMutableCodeTreeElement::FMutableCodeTreeElement(int32 InIndexOnTree, const int32& InMutableStateIndex, const TSharedPtr<UE::Mutable::Private::FModel, ESPMode::ThreadSafe>& InModel, UE::Mutable::Private::OP::ADDRESS InOperation, const FString& InCaption, const FSlateColor InLabelColor, const TSharedPtr<FMutableCodeTreeElement>* InDuplicatedOf)
{
	MutableModel = InModel;
	MutableOperation = InOperation;
	Caption = InCaption;
	LabelColor = InLabelColor;
	
	// Use a special color to denote "none" entries instead of the provided one
	if (MutableOperation == 0)
	{
		LabelColor = FColor(FColorList::DimGrey);
	}
	
	IndexOnTree = InIndexOnTree;
	if (InDuplicatedOf)
	{
		DuplicatedOf = *InDuplicatedOf;
	}

	// Generate the label to be used to display this operation in the operation tree
	GenerateLabelText();

	// Process the data that can be extracted from the current state
	SetElementCurrentState(InMutableStateIndex);
}


void FMutableCodeTreeElement::SetElementCurrentState(const int32& InStateIndex)
{
	// Skip operation if state is the same
	if (InStateIndex == CurrentMutableStateIndex)
	{
		return;
	}

	// Check for an out of bounds value
	check(MutableModel);
	UE::Mutable::Private::FProgram& MutableProgram = MutableModel->GetPrivate()->Program;
	check(InStateIndex >= 0 && InStateIndex < MutableProgram.States.Num());

	CurrentMutableStateIndex = InStateIndex;
	const UE::Mutable::Private::FProgram::FState& CurrentState = MutableProgram.States[CurrentMutableStateIndex];

	// Check if it is a dynamic resource
	for (auto& DynamicResource : CurrentState.m_dynamicResources)
	{
		// If the operation gets located then mark it as dynamic resource
		if (DynamicResource.Key == MutableOperation)
		{
			bIsDynamicResource = true;
			break;
		}
	}
	// Early exit: A dynamic resource can not be at the same time a state constant
	if (bIsDynamicResource)
	{
		return;
	}

	// Check if it is a state constant
	bIsStateConstant = CurrentState.m_updateCache.Contains(MutableOperation);
}


void FMutableCodeTreeElement::GenerateLabelText()
{
	const UE::Mutable::Private::FProgram& Program = MutableModel->GetPrivate()->Program;
	const UE::Mutable::Private::EOpType OperationType = Program.GetOpType(MutableOperation);
	FString OpName = UE::Mutable::Private::s_opNames[static_cast<int32>(OperationType)];
	OpName.TrimEndInline();

	// See if the operation type accepts additional information in the label
	switch (OperationType)
	{
	case UE::Mutable::Private::EOpType::BO_PARAMETER:
	case UE::Mutable::Private::EOpType::NU_PARAMETER:
	case UE::Mutable::Private::EOpType::SC_PARAMETER:
	case UE::Mutable::Private::EOpType::CO_PARAMETER:
	case UE::Mutable::Private::EOpType::PR_PARAMETER:
	case UE::Mutable::Private::EOpType::IM_PARAMETER:
	case UE::Mutable::Private::EOpType::ST_PARAMETER:
	{
		UE::Mutable::Private::OP::ParameterArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ParameterArgs>(MutableOperation);
		OpName += TEXT(" ");
		OpName += Program.Parameters[int32(Args.variable)].Name;
		break;
	}

	case UE::Mutable::Private::EOpType::ME_PARAMETER:
	{
		UE::Mutable::Private::OP::MeshParameterArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::MeshParameterArgs>(MutableOperation);
		OpName += FString::Printf( TEXT(" LOD %d Section %d of "), Args.LOD, Args.Section );
		OpName += Program.Parameters[int32(Args.variable)].Name;
		break;
	}

	case UE::Mutable::Private::EOpType::IM_SWIZZLE:
	{
		UE::Mutable::Private::OP::ImageSwizzleArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageSwizzleArgs>(MutableOperation);
		OpName += TEXT(" ");
		OpName += StringCast<TCHAR>(UE::Mutable::Private::TypeInfo::s_imageFormatName[int32(Args.format)]).Get();
		break;
	}

	case UE::Mutable::Private::EOpType::IM_PIXELFORMAT:
	{
		UE::Mutable::Private::OP::ImagePixelFormatArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImagePixelFormatArgs>(MutableOperation);
		OpName += TEXT(" ");
		OpName += StringCast<TCHAR>(UE::Mutable::Private::TypeInfo::s_imageFormatName[int32(Args.format)]).Get();
		OpName += TEXT(" or ");
		OpName += StringCast<TCHAR>(UE::Mutable::Private::TypeInfo::s_imageFormatName[int32(Args.formatIfAlpha)]).Get();
		break;
	}

	case UE::Mutable::Private::EOpType::IM_MIPMAP:
	{
		UE::Mutable::Private::OP::ImageMipmapArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageMipmapArgs>(MutableOperation);
		OpName += FString::Printf(TEXT(" levels: %d-%d tail: %d"), Args.levels, Args.blockLevels, int32(Args.onlyTail));
		break;
	}

	case UE::Mutable::Private::EOpType::IM_RESIZE:
	{
		UE::Mutable::Private::OP::ImageResizeArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageResizeArgs>(MutableOperation);
		OpName += FString::Printf(TEXT(" %d x %d"), int32(Args.Size[0]), int32(Args.Size[1]));
		break;
	}

	case UE::Mutable::Private::EOpType::IM_RESIZEREL:
	{
		UE::Mutable::Private::OP::ImageResizeRelArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageResizeRelArgs>(MutableOperation);
		OpName += FString::Printf(TEXT(" %.3f x %.3f"), Args.Factor[0], Args.Factor[1]);
		break;
	}

	case UE::Mutable::Private::EOpType::IM_MULTILAYER:
	{
		UE::Mutable::Private::OP::ImageMultiLayerArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageMultiLayerArgs>(MutableOperation);
		OpName += TEXT(" rgb: ");
		OpName += UE::Mutable::Private::TypeInfo::s_blendModeName[int32(Args.blendType)];
		OpName += TEXT(", a: ");
		OpName += UE::Mutable::Private::TypeInfo::s_blendModeName[int32(Args.blendTypeAlpha)];
		OpName += FString::Printf(TEXT(" a from %d "), Args.BlendAlphaSourceChannel);
		OpName += FString::Printf(TEXT(" range-id: %d"), Args.rangeId);
		OpName += FString::Printf(TEXT(" mask-from-alpha: %d"), int32(Args.bUseMaskFromBlended));
		break;
	}

	case UE::Mutable::Private::EOpType::IM_LAYER:
	{
		UE::Mutable::Private::OP::ImageLayerArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageLayerArgs>(MutableOperation);
		OpName += TEXT(" rgb: ");
		OpName += UE::Mutable::Private::TypeInfo::s_blendModeName[int32(Args.blendType)];
		OpName += TEXT(", a: ");
		OpName += UE::Mutable::Private::TypeInfo::s_blendModeName[int32(Args.blendTypeAlpha)];
		OpName += FString::Printf(TEXT(" a from %d "), Args.BlendAlphaSourceChannel);
		OpName += FString::Printf(TEXT(" flags %d"), Args.flags);
		break;
	}

	case UE::Mutable::Private::EOpType::IM_LAYERCOLOUR:
	{
		UE::Mutable::Private::OP::ImageLayerColourArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImageLayerColourArgs>(MutableOperation);
		OpName += TEXT(" rgb: ");
		OpName += UE::Mutable::Private::TypeInfo::s_blendModeName[int32(Args.blendType)];
		OpName += TEXT(" a: ");
		OpName += UE::Mutable::Private::TypeInfo::s_blendModeName[int32(Args.blendTypeAlpha)];
		OpName += TEXT(" a from ");
		OpName += FString::Printf(TEXT(" a from %d "), Args.BlendAlphaSourceChannel);
		OpName += FString::Printf(TEXT(" flags %d"), Args.flags);
		break;
	}

	case UE::Mutable::Private::EOpType::IM_PLAINCOLOUR:
	{
		UE::Mutable::Private::OP::ImagePlainColorArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::ImagePlainColorArgs>(MutableOperation);
		OpName += TEXT(" format: ");
		OpName += StringCast<TCHAR>(UE::Mutable::Private::TypeInfo::s_imageFormatName[int32(Args.Format)]).Get();
		OpName += FString::Printf(TEXT(" size %d x %d"), Args.Size[0], Args.Size[1]);
		OpName += FString::Printf(TEXT(" mips %d"), Args.LODs);
		break;
	}

	case UE::Mutable::Private::EOpType::IN_ADDIMAGE:
	{
		UE::Mutable::Private::OP::InstanceAddArgs Args = Program.GetOpArgs<UE::Mutable::Private::OP::InstanceAddArgs>(MutableOperation);
		if (Program.ConstantStrings.IsValidIndex(Args.name))
		{
			OpName += TEXT(" name: ");
			OpName += Program.ConstantStrings[Args.name];
		}
		break;
	}

	default:
		break;
	}

	OpName = OpName.TrimStartAndEnd();
	
	// Prepare the text shown on the UI side of the operation tree
	if (!Caption.IsEmpty())
	{
		Caption = Caption.TrimStartAndEnd();
		MainLabel = FString::Printf(TEXT("%d : [ %s ] %s"), int32(MutableOperation), *Caption, *OpName);
	}
	else
	{
		MainLabel = FString::Printf(TEXT("%d : %s"), int32(MutableOperation), *OpName);
	}
	

	// DEBUG : 
	// FString IndexOnTree = FString::FromInt(IndexOnTree);
	// IndexOnTree.Append(TEXT("- "));
	// MainLabel.InsertAt(0,IndexOnTree);

	// DEBUG : 
	// FString RowStateIndex = FString::FromInt(GetStateIndex());
	// RowStateIndex.Append(TEXT(" st "));
	// MainLabel.InsertAt(0,RowStateIndex);

	// Ignore the special case of operations of type "None"
	if (MutableOperation > 0 && DuplicatedOf)
	{
		MainLabel.Append(TEXT(" (duplicated)"));
	}
}


int32 FMutableCodeTreeElement::GetStateIndex() const
{
	return CurrentMutableStateIndex;
}


#undef LOCTEXT_NAMESPACE
