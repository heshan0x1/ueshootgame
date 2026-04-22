// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerImpl.h"

#include "SSceneOutliner.h"
#include "TedsOutlinerColumn.h"
#include "TedsTableViewerUtils.h"
#include "DataStorage/Features.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Columns/SlateDelegateColumns.h"
#include "Compatibility/SceneOutlinerRowHandleColumn.h"
#include "RowMapNode.h"
#include "TedsOutlinerFilter.h"
#include "TedsOutlinerHelpers.h"
#include "TedsOutlinerItem.h"
#include "TedsQueryHandleNode.h"
#include "TedsQueryMergeNode.h"
#include "TedsQueryNode.h"
#include "TedsRowCopyNode.h"
#include "TedsRowFilterNode.h"
#include "TedsRowHandleSortNode.h"
#include "TedsRowMergeNode.h"
#include "TedsRowMonitorNode.h"
#include "TedsRowOrderInversionNode.h"
#include "TedsRowQueryResultsNode.h"
#include "TedsRowSortNode.h"
#include "Columns/TedsOutlinerColumns.h"
#include "Compatibility/SceneOutlinerTedsBridge.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementOverrideColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Filters/FilterBase.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "TedsOutliner"

namespace UE::Editor::Outliner
{
namespace Private
{
	static bool bShowTedsColumnFilters = false;
} // namespace Private

static FAutoConsoleVariableRef CVarShowTedsColumnFilters(
	TEXT("TEDS.UI.Outliner.ShowTedsColumnFilters"),
	Private::bShowTedsColumnFilters,
	TEXT("Show Teds Column/Tag Filters in the Outliner (requires TEDS Outliner to be enabled and the Outliner to be reopened)"));
	
namespace QueryUtils
{
	static bool CanDisplayRow(DataStorage::IQueryContext& Context, const FTedsOutlinerColumn& TedsOutlinerColumn, DataStorage::RowHandle Row, SSceneOutliner& SceneOutliner)
	{
		/*
		 * Don't display widgets that are created for rows in this table viewer. Widgets are only created for rows that are currently visible, so if we
		 * display the rows for them we are now adding/removing rows to the table viewer based on currently visible rows. But adding rows can cause
		 * scrolling and change the currently visible rows which in turn again adds/removes widget rows. This chain keeps continuing which can cause
		 * flickering/scrolling issues in the table viewer.
		 */
		if (Context.HasColumn<FTypedElementSlateWidgetReferenceColumn>(Row))
		{
			// Check if this widget row belongs to the same table viewer it is being displayed in
			if (const TSharedPtr<ISceneOutliner> TableViewer = TedsOutlinerColumn.Outliner.Pin())
			{
				return &SceneOutliner != TableViewer.Get();
			}
			
		}
		return true;
	}

	static bool HasItemParentChanged(DataStorage::IQueryContext& Context, DataStorage::RowHandle Row, DataStorage::RowHandle ParentRowHandle, SSceneOutliner& SceneOutliner)
	{
		const FSceneOutlinerTreeItemPtr Item = SceneOutliner.GetTreeItem(Row, true);

		// If the item doesn't exist, it doesn't make sense to say its parent changed
		if (!Item)
		{
			return false;
		}
										
		const FSceneOutlinerTreeItemPtr ParentItem = Item->GetParent();

		// If the item doesn't have a parent, but ParentRowHandle is valid: The item just got added a parent so we want to dirty it
		if (!ParentItem)
		{
			return Context.IsRowAvailable(ParentRowHandle);
		}
										
		const FTedsOutlinerTreeItem* TedsParentItem = ParentItem->CastTo<FTedsOutlinerTreeItem>();

		if (TedsParentItem)
		{
			// return true if the row handle of the parent item doesn't match what we are given, i.e the parent has changed
			return TedsParentItem->GetRowHandle() != ParentRowHandle;
		}

		return false;
	};
}

namespace LabelWidgetUtils
{
	static TUniquePtr<FTypedElementWidgetConstructor> CreateLabelWidgetConstructorInternal(
			const DataStorage::ICoreProvider& Storage,
			DataStorage::IUiProvider& StorageUi,
			const DataStorage::IUiProvider::FPurposeID& Purpose,
			const DataStorage::RowHandle TargetRow)
	{
		// Get all the columns on the given row
		TArray<TWeakObjectPtr<const UScriptStruct>> ColumnTypes;
		Storage.ListColumns(TargetRow, [&ColumnTypes](const UScriptStruct& ColumnType)
		{
			ColumnTypes.Add(&ColumnType);
		});
	
		TUniquePtr<FTypedElementWidgetConstructor> WidgetConstructor;
		StorageUi.CreateWidgetConstructors(StorageUi.FindPurpose(Purpose), DataStorage::IUiProvider::EMatchApproach::LongestMatch, ColumnTypes, {},
			[&WidgetConstructor](TUniquePtr<FTypedElementWidgetConstructor> CreatedConstructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>>)
			{
				WidgetConstructor = MoveTemp(CreatedConstructor);
				// Either this was the exact match so no need to search further or the longest possible chain didn't match so the next ones will 
				// always be shorter in both cases just return.
				return false;
			});
		return WidgetConstructor;
	}
	
	TSharedPtr<SWidget> CreateLabelWidgetInternal(
		FTypedElementWidgetConstructor& WidgetConstructor,
		DataStorage::ICoreProvider& Storage,
		DataStorage::IUiProvider& StorageUi,
		const DataStorage::RowHandle TargetRow,
		ISceneOutlinerTreeItem& TreeItem,
		const STableRow<FSceneOutlinerTreeItemPtr>& RowItem,
		const bool bInteractable)
	{
		using namespace DataStorage::Queries;
		// Query description to pass as metadata to allow the label column to be writable
		static const FQueryDescription MetaDataQueryReadWrite = Select().ReadWrite<FTypedElementLabelColumn>().Where().Compile();
		static const FQueryDescription MetaDataQueryRead = Select().ReadOnly<FTypedElementLabelColumn>().Where().Compile();
	
		RowHandle WidgetRow = Storage.AddRow(Storage.FindTable(TableViewerUtils::GetWidgetTableName()));
		checkf(WidgetRow != InvalidRowHandle, TEXT("Expected valid row handle!"));
		if (FTypedElementRowReferenceColumn* RowReference = Storage.GetColumn<FTypedElementRowReferenceColumn>(WidgetRow))
		{
			RowReference->Row = TargetRow;
		}
		Storage.AddColumn(WidgetRow, FTedsOutlinerColumn{ .Outliner = TreeItem.WeakSceneOutliner });
	
		// Create metadata for the query
		FQueryMetaDataView QueryMetaDataView = bInteractable ? FQueryMetaDataView(MetaDataQueryReadWrite) : FQueryMetaDataView(MetaDataQueryRead);
		TSharedPtr<SWidget> Widget = StorageUi.ConstructWidget(WidgetRow, WidgetConstructor, QueryMetaDataView);
		if (Widget)
		{
			if (FExternalWidgetSelectionColumn* ExternalWidgetSelectionColumn = Storage.GetColumn<FExternalWidgetSelectionColumn>(WidgetRow))
			{
				ExternalWidgetSelectionColumn->IsSelected = FIsSelected::CreateSP(&RowItem, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively);
			}

			if (bInteractable)
			{
				TreeItem.RenameRequestEvent.BindLambda([StoragePtr = &Storage, WidgetRow]()
					{
						if (const FWidgetEnterEditModeColumn* Column = StoragePtr->GetColumn<FWidgetEnterEditModeColumn>(WidgetRow))
						{
							Column->OnEnterEditMode.ExecuteIfBound();
						}
					});
			}
		}
		else
		{
			Storage.RemoveRow(WidgetRow);
		}	
		return Widget;
	}
}

FTedsOutlinerParams::FTedsOutlinerParams(SSceneOutliner* InSceneOutliner)
	: SceneOutliner(InSceneOutliner)
	, QueryDescription()
	, bShowRowHandleColumn(true)
	, bForceShowParents(true)
	, bUseDefaultObservers(true)
	, HierarchyData(FTedsOutlinerHierarchyData::GetDefaultHierarchyData())
{
	CellWidgetPurpose =
		DataStorage::IUiProvider::FPurposeID(DataStorage::IUiProvider::FPurposeInfo(
			"SceneOutliner", "Cell", NAME_None).GeneratePurposeID());
	
	HeaderWidgetPurpose =
		DataStorage::IUiProvider::FPurposeID(DataStorage::IUiProvider::FPurposeInfo(
			"SceneOutliner", "Header", NAME_None).GeneratePurposeID());

	LabelWidgetPurpose =
		DataStorage::IUiProvider::FPurposeID(DataStorage::IUiProvider::FPurposeInfo(
			"SceneOutliner", "RowLabel", NAME_None).GeneratePurposeID());
}
	
FTedsOutlinerImpl::FTedsOutlinerImpl(const FTedsOutlinerParams& InParams, ISceneOutlinerMode* InMode, bool bInHybridMode)
	: CreationParams(InParams)
	, CellWidgetPurpose(InParams.CellWidgetPurpose)
	, HeaderWidgetPurpose(InParams.HeaderWidgetPurpose)
	, LabelWidgetPurpose(InParams.LabelWidgetPurpose)
	, InitialQueryDescription(InParams.QueryDescription)
	, HierarchyData(InParams.HierarchyData)
	, SelectionSetName(InParams.SelectionSetOverride)
	, bForceShowParents(InParams.bForceShowParents)
	, SceneOutlinerMode(InMode)
	, SceneOutliner(InParams.SceneOutliner)
	, bHybridMode(bInHybridMode)
{
	// Initialize the TEDS constructs
	using namespace UE::Editor::DataStorage;
	Storage = GetMutableDataStorageFeature<DataStorage::ICoreProvider>(StorageFeatureName);
	StorageUi = GetMutableDataStorageFeature<DataStorage::IUiProvider>(UiFeatureName);
	StorageCompatibility = GetMutableDataStorageFeature<DataStorage::ICompatibilityProvider>(CompatibilityFeatureName);

	bUsingQueryConditionsSyntax = InitialQueryDescription.Conditions && !InitialQueryDescription.Conditions->IsEmpty();
}

void FTedsOutlinerImpl::CreateFilterQueries()
{
	using namespace UE::Editor::DataStorage::Queries;

	if (Private::bShowTedsColumnFilters)
	{
		// Create separate categories for columns and tags
		TSharedRef<FFilterCategory> TedsColumnFilterCategory = MakeShared<FFilterCategory>(LOCTEXT("TedsColumnFilters", "TEDS Columns"), LOCTEXT("TedsColumnFiltersTooltip", "Filter by TEDS columns"));
		TSharedRef<FFilterCategory> TedsTagFilterCategory = MakeShared<FFilterCategory>(LOCTEXT("TedsTagFilters", "TEDS Tags"), LOCTEXT("TedsTagFiltersTooltip", "Filter by TEDS Tags"));

		const UStruct* TedsColumn = DataStorage::FColumn::StaticStruct();
		const UStruct* TedsTag = DataStorage::FTag::StaticStruct();

		// Grab all UStruct types to see if they derive from FColumn or FTag
		ForEachObjectOfClass(UScriptStruct::StaticClass(), [&](UObject* Obj)
		{
			if (const UScriptStruct* Struct = Cast<const UScriptStruct>(Obj))
			{
				if (Struct->IsChildOf(TedsColumn) || Struct->IsChildOf(TedsTag))
				{
					// Create a query description to filter for this tag/column
					QueryHandle FilterQuery;

					if (bUsingQueryConditionsSyntax)
					{
						FilterQuery = Storage->RegisterQuery(
							Select()
							.Where(TColumn(Struct))
							.Compile());
					}
					else
					{
						FilterQuery = Storage->RegisterQuery(
							Select()
							.Where()
								.All(Struct)
							.Compile());
					}

					// Create the filter
					TSharedRef<FTedsOutlinerFilter> TedsFilter = MakeShared<FTedsOutlinerFilter>(Struct->GetFName(), Struct->GetDisplayNameText(), 
						Struct->GetDisplayNameText(), FName(), Struct->IsChildOf(TedsColumn) ? TedsColumnFilterCategory : TedsTagFilterCategory, 
						AsShared(), FilterQuery);
					SceneOutliner->AddFilterToFilterBar(TedsFilter);
				}
			}
		});
	}

	TSharedRef<FFilterCategory> CustomClassFiltersCategory = MakeShared<FFilterCategory>(LOCTEXT("CustomTypeFilters", "Custom Type Filters"), LOCTEXT("CustomTypeFiltersTooltip", "Filter by custom class types"));
	for(UClass* Class : CreationParams.ClassFilters)
	{
		SceneOutliner->AddFilterToFilterBar(MakeShared<FTedsOutlinerFilter>(Class, CustomClassFiltersCategory, AsShared()));
	}
	
	TSharedRef<FFilterCategory> CustomFiltersCategory = MakeShared<FFilterCategory>(LOCTEXT("TedsFilters", "TEDS Custom Filters"), LOCTEXT("TedsFiltersTooltip", "Filter by custom TEDS queries"));
		
	for(FTedsFilterData& Filter : CreationParams.Filters)
	{
		if (!Filter.FilterCategory)
		{
			Filter.FilterCategory = CustomFiltersCategory;
		}
		SceneOutliner->AddFilterToFilterBar(MakeShared<FTedsOutlinerFilter>(Filter, AsShared()));
	}
}

void FTedsOutlinerImpl::Init()
{
	RegisterTedsOutliner();
	CreateFilterQueries();
	RecompileQueries();

	// Tick post TEDS update to make sure all processors have run and the data is correct
	Storage->OnUpdateCompleted().AddRaw(this, &FTedsOutlinerImpl::PostDataStorageUpdate);

	// FTedsOutlinerImpl::Init is called before the Outliner UI has been fully init, so we generate the columns in the next tick
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		RegenerateColumns();
		return false;
	}));
}

void FTedsOutlinerImpl::OnMonitoredRowsAdded(DataStorage::FRowHandleArrayView InRows)
{
	// Refresh queries so that the added rows can pass through the filters if applied
	RefreshQueries();

	const bool bHasNoActiveFilters = FilterNodes.IsEmpty() && ClassFilterNodes.IsEmpty();
	for (DataStorage::RowHandle RowToAdd : InRows)
	{
		// Don't display the new row unless it is in the filtered row list, but there is no need to check if there are no filters active
		if (bHasNoActiveFilters || FinalCombinedRowNode->GetRows().Contains(RowToAdd))
		{
			// If the row we are adding is selected, we are probably synced to some external selection and the Outliner needs to update selection
			if (Storage->HasColumns<FTypedElementSelectionColumn>(RowToAdd))
			{
				bSelectionDirty = true;
			}
			FSceneOutlinerHierarchyChangedData EventData;
			EventData.Type = FSceneOutlinerHierarchyChangedData::Added;
			EventData.Items.Add(SceneOutlinerMode->CreateItemFor<FTedsOutlinerTreeItem>(FTedsOutlinerTreeItem(RowToAdd, AsShared())));
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}
}

void FTedsOutlinerImpl::OnMonitoredRowsRemoved(DataStorage::FRowHandleArrayView InRows)
{
	for (DataStorage::RowHandle RowToRemove : InRows)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
		EventData.ItemIDs.Add(RowToRemove);
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

FTedsOutlinerImpl::~FTedsOutlinerImpl()
{
	if (Storage)
	{
		Storage->RemoveRow(OutlinerRowHandle);
		Storage->OnUpdateCompleted().RemoveAll(this);
	}
	UnregisterQueries();
	FTSTicker::RemoveTicker(TickerHandle);
}

FTedsOutlinerImpl::FIsItemCompatible& FTedsOutlinerImpl::IsItemCompatible()
{
	return IsItemCompatibleWithTeds;
}

void FTedsOutlinerImpl::SetSelection(const TArray<DataStorage::RowHandle>& InSelectedRows)
{
	if (!SelectionSetName.IsSet())
	{
		return;
	}
	
	ClearSelection();

	for(DataStorage::RowHandle Row : InSelectedRows)
	{
		Storage->AddColumn(Row, FTypedElementSelectionColumn{ .SelectionSet = SelectionSetName.GetValue() });
	}
}
	
TSharedRef<SWidget> FTedsOutlinerImpl::CreateLabelWidget(
	DataStorage::ICoreProvider& Storage,
	DataStorage::IUiProvider& StorageUi,
	const DataStorage::IUiProvider::FPurposeID& Purpose,
	DataStorage::RowHandle TargetRow,
	ISceneOutlinerTreeItem& TreeItem,
	const STableRow<FSceneOutlinerTreeItemPtr>& RowItem,
	bool bInteractable)
{
	TUniquePtr<FTypedElementWidgetConstructor> WidgetConstructor = LabelWidgetUtils::CreateLabelWidgetConstructorInternal(Storage, StorageUi, Purpose, TargetRow);
	if (!WidgetConstructor)
	{
		return SNullWidget::NullWidget;
	}

	TSharedPtr<SWidget> Widget = LabelWidgetUtils::CreateLabelWidgetInternal(*WidgetConstructor, Storage, StorageUi, TargetRow, TreeItem, RowItem, bInteractable);
	if (!Widget)
	{
		return SNullWidget::NullWidget;
	}
	
	return SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			Widget.ToSharedRef()
		];
}

TSharedRef<SWidget>FTedsOutlinerImpl::CreateLabelWidgetForItem(DataStorage::RowHandle TargetRow, ISceneOutlinerTreeItem& TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& RowItem, bool bIsInteractable) const
{
	return CreateLabelWidget(*Storage, *StorageUi, LabelWidgetPurpose, TargetRow, TreeItem, RowItem, bIsInteractable);
}

void FTedsOutlinerImpl::AddExternalQuery(const FName& QueryName, const QueryHandle& InQueryHandle)
{
	if (ensureMsgf(InQueryHandle != InvalidQueryHandle, TEXT("An Invalid Query Handle cannot be used for a TEDS Filter")))
	{
		ExternalQueries.Emplace(QueryName, MakeShared<DataStorage::QueryStack::FQueryHandleNode>(InQueryHandle));
	}
}

void FTedsOutlinerImpl::RemoveExternalQuery(const FName& QueryName)
{
	ExternalQueries.Remove(QueryName);
}

void FTedsOutlinerImpl::AppendExternalQueries(FQueryDescription& OutQuery)
{
	for(const TPair<FName, TSharedPtr<QueryStack::IQueryNode>>& ExternalQuery : ExternalQueries)
	{
		DataStorage::Queries::MergeQueries(OutQuery, Storage->GetQueryDescription(ExternalQuery.Value->GetQuery()));
	}
}

void FTedsOutlinerImpl::AddExternalQueryFunction(const FName& QueryName, const DataStorage::Queries::TQueryFunction<bool>& InQueryFunction)
{
	// Store the external query functions as functions instead of RowNodes since the row node to filter on has not been initialized yet
	ExternalQueryFunctions.Emplace(QueryName, InQueryFunction);
}

void FTedsOutlinerImpl::RemoveExternalQueryFunction(const FName& QueryName)
{
	ExternalQueryFunctions.Remove(QueryName);
}

void FTedsOutlinerImpl::AddClassQueryFunction(const FName& ClassName, const TQueryFunction<bool>& InClassQueryFunction)
{
	ClassFilters.Emplace(ClassName, InClassQueryFunction);
}

void FTedsOutlinerImpl::RemoveClassQueryFunction(const FName& ClassName)
{
	ClassFilters.Remove(ClassName);
}

bool FTedsOutlinerImpl::CanDisplayRow(DataStorage::RowHandle ItemRowHandle) const
{
	/*
	 * Don't display widgets that are created for rows in this table viewer. Widgets are only created for rows that are currently visible, so if we
	 * display the rows for them we are now adding/removing rows to the table viewer based on currently visible rows. But adding rows can cause
	 * scrolling and change the currently visible rows which in turn again adds/removes widget rows. This chain keeps continuing which can cause
	 * flickering/scrolling issues in the table viewer.
	 */
	if (Storage->HasColumns<FTypedElementSlateWidgetReferenceColumn>(ItemRowHandle))
	{
		// Check if this widget row belongs to the same table viewer it is being displayed in
		if (const FTedsOutlinerColumn* TedsOutlinerColumn = Storage->GetColumn<FTedsOutlinerColumn>(ItemRowHandle))
		{
			if (const TSharedPtr<ISceneOutliner> TableViewer = TedsOutlinerColumn->Outliner.Pin())
		{
				return SceneOutliner != TableViewer.Get();
			}
		}
	}
	return true;
}

void FTedsOutlinerImpl::RegisterTedsOutliner()
{
	DataStorage::TableHandle OutlinerTable = Storage->FindTable(UE::Editor::Outliner::Helpers::GetTedsOutlinerTableName());

	if (OutlinerTable != DataStorage::InvalidTableHandle)
	{
		OutlinerRowHandle = Storage->AddRow(OutlinerTable);

		if (FTedsOutlinerColumn* TedsOutlinerColumn = Storage->GetColumn<FTedsOutlinerColumn>(OutlinerRowHandle))
		{
			TedsOutlinerColumn->Outliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner->AsShared());
		}
		
		if (FTedsOutlinerColumnQueryColumn* ColumnQueryColumn = Storage->GetColumn<FTedsOutlinerColumnQueryColumn>(OutlinerRowHandle))
		{
			ColumnQueryColumn->ColumnQueryDescription = CreationParams.ColumnQueryDescription;

			ColumnQueryColumn->OnRefreshColumns.BindSP(this, &FTedsOutlinerImpl::RegenerateColumns);
		}

		FName Identifier = SceneOutliner->GetOutlinerIdentifier();

		if (!Identifier.IsNone())
		{
			// Any old outliner instances could still have dangling references if external systems are holding onto them - which means it might only
			// be destructed after this new instance is init and MapRow ensures on duplicate keys. At this point it is safe to remove the old mapping
			// since all the data will be fine because the old row exists till the destructor is called and we want any systems using the mapping
			// to start looking at the new instance anyways
			Storage->RemoveRowMapping(MappingDomain, DataStorage::FMapKey(SceneOutliner->GetOutlinerIdentifier()));
			Storage->MapRow(MappingDomain, DataStorage::FMapKey(SceneOutliner->GetOutlinerIdentifier()), OutlinerRowHandle);
		}
	}
}

void FTedsOutlinerImpl::ClearColumns() const
{
	for (FName ColumnName : AddedColumns)
	{
		SceneOutliner->RemoveColumn(ColumnName);
	}
}

void FTedsOutlinerImpl::RegenerateColumns()
{
	ClearColumns();
	
	FTedsOutlinerColumnQueryColumn* ColumnQueryColumn = Storage->GetColumn<FTedsOutlinerColumnQueryColumn>(OutlinerRowHandle);
	FTedsOutlinerColumn* TedsOutlinerColumn = Storage->GetColumn<FTedsOutlinerColumn>(OutlinerRowHandle);

	if  (!ColumnQueryColumn || !TedsOutlinerColumn)
	{
		return;
	}

	FTreeItemIDDealiaser Dealiaser;

	if (FTedsOutlinerDealiaserColumn* DealiaserColumn = Storage->GetColumn<FTedsOutlinerDealiaserColumn>(OutlinerRowHandle))
	{
		Dealiaser = DealiaserColumn->Dealiaser;
	}

	using MatchApproach = DataStorage::IUiProvider::EMatchApproach;
	constexpr uint8 DefaultPriorityIndex = 100;

	DataStorage::FQueryMetaDataView MetaDataView(ColumnQueryColumn->ColumnQueryDescription);
	
	int32 SelectionCount = ColumnQueryColumn->ColumnQueryDescription.SelectionTypes.Num();
	AddedColumns.Reset(SelectionCount);

	TArray<TWeakObjectPtr<const UScriptStruct>> ColumnTypes =
		DataStorage::TableViewerUtils::CreateVerifiedColumnTypeArray(ColumnQueryColumn->ColumnQueryDescription.SelectionTypes);
	
	DataStorage::RowHandle CellPurposeRow = StorageUi->FindPurpose(CellWidgetPurpose);
	DataStorage::RowHandle HeaderPurposeRow = StorageUi->FindPurpose(HeaderWidgetPurpose);

	int32 IndexOffset = 0;
	auto ColumnConstructor = [this, ColumnQueryColumn, TedsOutlinerColumn, MetaDataView, &IndexOffset, HeaderPurposeRow, Dealiaser](
		TUniquePtr<FTypedElementWidgetConstructor> Constructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes)
		{
			TSharedPtr<FTypedElementWidgetConstructor> CellConstructor(Constructor.Release());

			/* If we have a fallback column for this query, remove it, take over it's priority and 
			 * replace it with the TEDS column. But also allow the TEDS-Outliner column to fallback to it for
			 * data not in TEDS yet.
			 */
			FName FallbackColumnName = Helpers::FindOutlinerColumnFromTedsColumns(Storage, ColumnTypes);
			const FSceneOutlinerColumnInfo* FallbackColumnInfo = SceneOutliner->GetSharedData().ColumnMap.Find(FallbackColumnName);
			uint8 ColumnPriority = FallbackColumnInfo
				? FallbackColumnInfo->PriorityIndex
				: static_cast<uint8>(
					FMath::Clamp(DefaultPriorityIndex + static_cast<uint8>(
						FMath::Clamp(IndexOffset, 0, 255)), 0, 255));

			// Grab a reference to the fallback column and remove it from the actual outliner
			const TMap<FName, TSharedPtr<ISceneOutlinerColumn>>& Columns = SceneOutliner->GetColumns();
			const TSharedPtr<ISceneOutlinerColumn> FallbackColumnPtr = Columns.FindRef(FallbackColumnName);
			SceneOutliner->RemoveColumn(FallbackColumnName);

			FName NameId = UE::Editor::DataStorage::TableViewerUtils::FindLongestMatchingName(ColumnTypes, IndexOffset);
			FText DisplayName = CellConstructor.Get()->CreateWidgetDisplayNameText(Storage);
			AddedColumns.Add(NameId);
			SceneOutliner->AddColumn(NameId,
				FSceneOutlinerColumnInfo(
					ESceneOutlinerColumnVisibility::Visible, 
					ColumnPriority,
					FCreateSceneOutlinerColumn::CreateLambda(
						[this, ColumnQueryColumn, TedsOutlinerColumn, MetaDataView, NameId, &ColumnTypes, CellConstructor, FallbackColumnPtr, HeaderPurposeRow, Dealiaser](ISceneOutliner&)
						{
							TSharedPtr<FTypedElementWidgetConstructor> HeaderConstructor = 
								UE::Editor::DataStorage::TableViewerUtils::CreateHeaderWidgetConstructor(*StorageUi, MetaDataView, ColumnTypes, HeaderPurposeRow);

							DataStorage::FTedsTableViewerColumn::FSortHandler SortDelegates
							{
								.OnSortHandler = DataStorage::FTedsTableViewerColumn::FOnSort::CreateSP(this, &FTedsOutlinerImpl::OnSort),
								.IsSortingHandler = DataStorage::FTedsTableViewerColumn::FIsSorting::CreateSP(this, &FTedsOutlinerImpl::IsSorting)
							};

							FTedsOutlinerUiColumnInitParams Params(ColumnQueryColumn->ColumnQueryDescription, *Storage, *StorageUi, *StorageCompatibility);
							Params.NameId = NameId;
							Params.ColumnTypes = TArray<TWeakObjectPtr<const UScriptStruct>>(ColumnTypes.GetData(), ColumnTypes.Num());
							Params.HeaderWidgetConstructor = MoveTemp(HeaderConstructor);
							Params.CellWidgetConstructor = CellConstructor;
							Params.FallbackColumn = FallbackColumnPtr;
							Params.OwningOutliner = TedsOutlinerColumn->Outliner;
							Params.TedsOutlinerImpl = AsWeak();
							Params.SortDelegates = MoveTemp(SortDelegates);
							Params.Dealiaser = Dealiaser;
							Params.bHybridMode = bHybridMode;
							
							return MakeShared<FTedsOutlinerUiColumn>(Params);

						}),
					true,
					TOptional<float>(),
					DisplayName
				)
			);
			++IndexOffset;
			return true;
		};

	StorageUi->CreateWidgetConstructors(CellPurposeRow, MatchApproach::LongestMatch, ColumnTypes, 
		MetaDataView, ColumnConstructor);
}

DataStorage::RowHandle FTedsOutlinerImpl::GetOutlinerRowHandle() const
{
	return OutlinerRowHandle;
}

void FTedsOutlinerImpl::CreateItemsFromQuery(TArray<FSceneOutlinerTreeItemPtr>& OutItems, ISceneOutlinerMode* InMode)
{
	using namespace UE::Editor::DataStorage::Queries;

	// We refresh the row collector node on demand, since this function is only called on a FullRefresh() and the node is setup to not do anything on
	// update. Addition/Removal in between refreshes is handled by the row monitor node
	// Technically the RowMonitorNode also handles keeping this list up to date using observers, but the monitor node is optional and it's safer
	// to do a full clean refresh at this point anyways
	
	RefreshQueries();
	for (RowHandle Row : FinalCombinedRowNode->GetRows())
	{
		if (FSceneOutlinerTreeItemPtr TreeItem = InMode->CreateItemFor<FTedsOutlinerTreeItem>(FTedsOutlinerTreeItem(Row, AsShared()), false))
		{
			OutItems.Add(TreeItem);
		}
	}

	// We need to update selection since the whole outliner was re-populated
	bSelectionDirty = true;
}

void FTedsOutlinerImpl::CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const
{
	/* TEDS-Outliner TODO: This can probably be improved or optimized in the future
	 * 
	 * TEDS currently only supports one way lookup for parents, so to get the children
	 * for a given row we currently have to go through every row (that matches our populate query) with a parent column to check if the parent
	 * is our row.
	 * This has to be done recursively to grab our children, grandchildren and so on...
	 */

	// If there's no hierarchy data, there is no need to create children
	if (!HierarchyData.IsSet())
	{
		return;
	}

	using namespace UE::Editor::DataStorage::Queries;
	
	const FTedsOutlinerTreeItem* TedsTreeItem = Item->CastTo<FTedsOutlinerTreeItem>();

	// If this item is not a TEDS item, we are not handling it
	if (!TedsTreeItem)
	{
		return;
	}
		
	RowHandle ItemRowHandle = TedsTreeItem->GetRowHandle();

	if(!Storage->IsRowAssigned(ItemRowHandle))
	{
		return;
	}

	TArray<RowHandle> ChildItems;

	if (HierarchyData->GetChildren.IsBound())
	{
		void* ParentColumnData = Storage->GetColumnData(ItemRowHandle, HierarchyData->HierarchyColumn);
		ChildItems = HierarchyData->GetChildren.Execute(ParentColumnData);
	}
	else
	{
		TArray<RowHandle> MatchedRowsWithParentColumn;

		// Collect all entities that are owned by our entity
		DirectQueryCallback ChildRowCollector = CreateDirectQueryCallbackBinding(
		[&MatchedRowsWithParentColumn] (const IDirectQueryContext& Context, const RowHandle*)
		{
			MatchedRowsWithParentColumn.Append(Context.GetRowHandles());
		});

		Storage->RunQuery(ChildRowHandleQuery, ChildRowCollector);

		// Recursively get the children for each entity
		TFunction<void(RowHandle)> GetChildrenRecursive = [&ChildItems, &MatchedRowsWithParentColumn, DataStorage = Storage, &GetChildrenRecursive, InHierarchyData = HierarchyData]
		(RowHandle EntityRowHandle) -> void
		{
			for(RowHandle ChildEntityRowHandle : MatchedRowsWithParentColumn)
			{
				const void* ParentColumnData = DataStorage->GetColumnData(ChildEntityRowHandle, InHierarchyData.GetValue().HierarchyColumn);

				if (ensureMsgf(ParentColumnData, TEXT("We should always the a parent column since we only grabbed rows with those ")))
				{
					// Get the parent row handle
					const RowHandle ParentRowHandle = InHierarchyData.GetValue().GetParent.Execute(ParentColumnData);
				
					// Check if this entity is owned by the entity we are looking children for
					if (ParentRowHandle == EntityRowHandle)
					{
						ChildItems.Add(ChildEntityRowHandle);

						// Recursively look for children of this item
						GetChildrenRecursive(ChildEntityRowHandle);
					}
				}
			}
		};

		GetChildrenRecursive(ItemRowHandle);
	}
	
	// If the row doesn't exist in our query stack results, it doesn't match the query and should not be displayed
	FRowHandleArrayView Rows(RowHandleSortNode->GetRows());

	// Actually create the items for the child entities 
	for (RowHandle ChildItemRowHandle : ChildItems)
	{
		if (!Rows.Contains(ChildItemRowHandle))
		{
			continue;
		}
		
		if (FSceneOutlinerTreeItemPtr ChildActorItem = SceneOutlinerMode->CreateItemFor<FTedsOutlinerTreeItem>(FTedsOutlinerTreeItem(ChildItemRowHandle, AsShared())))
		{
			OutChildren.Add(ChildActorItem);
		}
	}
}

DataStorage::RowHandle FTedsOutlinerImpl::GetParentRow(DataStorage::RowHandle InRowHandle)
{
	// No parent if there is no hierarchy data specified
	if (!HierarchyData.IsSet())
	{
		return DataStorage::InvalidRowHandle;
	}
	
	// If this entity does not have a parent entity, return InvalidRowHandle
	const void* ParentColumnData = Storage->GetColumnData(InRowHandle, HierarchyData.GetValue().HierarchyColumn);
	
	if (!ParentColumnData)
	{
		return DataStorage::InvalidRowHandle;
	}

	// If the parent is invalid for some reason, return InvalidRowHandle
	const DataStorage::RowHandle ParentRowHandle = HierarchyData.GetValue().GetParent.Execute(ParentColumnData);
	
	if (!Storage->IsRowAvailable(ParentRowHandle))
	{
		return DataStorage::InvalidRowHandle;
	}

	// If the row doesn't exist in our query stack results, it doesn't match the query and should not be displayed
	DataStorage::FRowHandleArrayView Rows(RowHandleSortNode->GetRows());

	if (!Rows.Contains(ParentRowHandle))
	{
		return DataStorage::InvalidRowHandle;
	}

	return ParentRowHandle;
}

bool FTedsOutlinerImpl::ShouldForceShowParentRows() const
{
	return bForceShowParents;
}

void FTedsOutlinerImpl::RefreshQueries() const
{
	InitialRowCollectorNode->Refresh();
	CombinedRowCollectorNode->Refresh();
	FinalCombinedRowNode->Update();
}

void FTedsOutlinerImpl::RecompileQueries()
{
	using namespace UE::Editor::DataStorage::Queries;
	using namespace UE::Editor::DataStorage::QueryStack;
	
	UnregisterQueries();

	// Get a list of all the query nodes (initial query + query description filters) to combine them into one composite query
	TArray<TSharedPtr<IQueryNode>> QueryNodes;

	TSharedPtr<IQueryNode> InitialQueryNode = MakeShared<FQueryNode>(*Storage, InitialQueryDescription);
	
	QueryNodes.Add(InitialQueryNode);
	for (const TPair<FName, TSharedPtr<IQueryNode>>& ExternalQuery : ExternalQueries)
	{
		QueryNodes.Add(ExternalQuery.Value);
	}

	TSharedPtr<IQueryNode> CombinedQueryNode = MakeShared<FQueryMergeNode>(*Storage, QueryNodes);

	// If we have a monitor node, it handles updating the collector node automatically. Otherwise, we need it to update on tick
	FRowQueryResultsNode::ESyncFlags SyncFlags =
		CreationParams.bUseDefaultObservers ? FRowQueryResultsNode::ESyncFlags::None : FRowQueryResultsNode::ESyncFlags::RefreshOnUpdate;

	// Node to collect all rows matching the initial query (stored so OR filters can filter on the original query)
	InitialRowCollectorNode = MakeShared<FRowQueryResultsNode>(*Storage, InitialQueryNode, SyncFlags);

	// Node to collect all rows matching the composite query
	CombinedRowCollectorNode = MakeShared<FRowQueryResultsNode>(*Storage, CombinedQueryNode, SyncFlags);

	FilterNodes.Empty();
	TSharedPtr<IRowNode> PrimaryFilterNode = CombinedRowCollectorNode;
	// Combines all active external filters using AND
	for (const TPair<FName, TQueryFunction<bool>>& ExternalQueryFunction : ExternalQueryFunctions)
	{
		TSharedPtr<FRowFilterNode> FilterNode = MakeShared<FRowFilterNode>(Storage, PrimaryFilterNode, ExternalQueryFunction.Value);
		FilterNodes.Add(FilterNode);
		PrimaryFilterNode = FilterNode;
	}

	ClassFilterNodes.Empty();
	// Combines all active class filters using OR
	if (!ClassFilters.IsEmpty())
	{
		for (const TPair<FName, TQueryFunction<bool>>& ClassFilter : ClassFilters)
		{
			ClassFilterNodes.Add(MakeShared<FRowFilterNode>(Storage, InitialRowCollectorNode, ClassFilter.Value));
		}
		
		TArray<TSharedPtr<IRowNode>> ClassBaseFilterNodes(ClassFilterNodes);
		CombinedClassFilterRowNode = MakeShared<FRowMergeNode>(ClassBaseFilterNodes, FRowMergeNode::EMergeApproach::Unique);

		FinalCombinedRowNode = MakeShared<FRowMergeNode>(MakeConstArrayView({PrimaryFilterNode, CombinedClassFilterRowNode}), FRowMergeNode::EMergeApproach::Repeating);
	}
	else
	{
		FinalCombinedRowNode = PrimaryFilterNode;
	}

	// The "actual" collector node passed into other misc nodes is the monitor node if it exists, the collector node otherwise
	TSharedPtr<IRowNode> ActualCollectorNode = FinalCombinedRowNode;
	
	// Node to monitor row addition/removal
	if (CreationParams.bUseDefaultObservers)
	{
		RowMonitorNode = MakeShared<FRowMonitorNode>(*Storage, FinalCombinedRowNode, CombinedQueryNode);
		RowMonitorNode->OnMonitoredRowsAdded().AddRaw(this, &FTedsOutlinerImpl::OnMonitoredRowsAdded);
		RowMonitorNode->OnMonitoredRowsRemoved().AddRaw(this, &FTedsOutlinerImpl::OnMonitoredRowsRemoved);
		ActualCollectorNode = RowMonitorNode;
	}
	
	// We store a copy of the collected rows into an array sorted by row handles for faster lookup
	TSharedRef<FRowCopyNode> CopyNode = MakeShared<FRowCopyNode>(ActualCollectorNode);
	RowHandleSortNode = MakeShared<FRowHandleSortNode>(CopyNode);

	// Nodes to actually sort the rows by the column the user requested
	// Persist the sort order if the sorting nodes are being reset
	RowPrimarySortingNode = MakeShared<FRowSortNode>(*Storage, RowHandleSortNode, RowPrimarySortingNode ? RowPrimarySortingNode->GetColumnSorter() : nullptr);
	RowPrimaryInversionNode = MakeShared<FRowOrderInversionNode>(RowPrimarySortingNode, RowPrimaryInversionNode ? RowPrimaryInversionNode->IsEnabled() : false);

	// A custom node to store row handle -> sort index mapping.
	RowSortMapNode = MakeShared<FRowMapNode>(RowPrimaryInversionNode);

	// Our final query to collect rows to populate the Outliner - currently the same as the initial query the user provided
	FQueryDescription FinalQueryDescription(InitialQueryDescription);

	// Add the filters the user has active to the query
	AppendExternalQueries(FinalQueryDescription);

	// Queries to track parent info, only required if we have hierarchy data
	if (HierarchyData.IsSet())
	{
		const UScriptStruct* ParentColumnType = HierarchyData.GetValue().HierarchyColumn;

		// Query to get all rows that match our conditions with a parent column (i.e all child rows)
		FQueryDescription ChildHandleQueryDescription;
		
		if (bUsingQueryConditionsSyntax)
		{
			ChildHandleQueryDescription = Select()
				.Where(TColumn(ParentColumnType))
				.Compile();
		}
		else
		{
			ChildHandleQueryDescription =
				Select()
				.Where()
					.All(ParentColumnType)
				.Compile();
		}
		
		// Add the conditions from FinalQueryDescription to ensure we are tracking removal of the rows the user requested
		MergeQueries(ChildHandleQueryDescription, FinalQueryDescription);

		FQueryDescription UpdateParentQueryDescription =
			Select(
			TEXT("Update item parent"),
			FProcessor(EQueryTickPhase::PostPhysics, Storage->GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[this, ParentColumnType](IQueryContext& Context, const RowHandle* Rows)
			{
				if (const char* ParentColumn = reinterpret_cast<const char*>(Context.GetColumn(ParentColumnType)))
				{
					int32 ColumnSize = ParentColumnType->GetCppStructOps() ? ParentColumnType->GetCppStructOps()->GetSize() : ParentColumnType->GetStructureSize();
					const FTedsOutlinerColumn* TedsOutlinerColumns = Context.GetColumn<FTedsOutlinerColumn>();
					uint32 RowCount = Context.GetRowCount();
					
					for(uint32 RowIndex = 0; RowIndex < RowCount; ++RowIndex, ParentColumn += ColumnSize)
					{
						RowHandle ParentRowHandle = HierarchyData.GetValue().GetParent.Execute(ParentColumn);
								
						if (QueryUtils::HasItemParentChanged(Context, Rows[RowIndex], ParentRowHandle, *SceneOutliner))
						{
							FSceneOutlinerHierarchyChangedData EventData;
							EventData.Type = FSceneOutlinerHierarchyChangedData::Moved;
							EventData.ItemIDs.Add(Rows[RowIndex]);
							HierarchyChangedEvent.Broadcast(EventData);
						}
					}
				}
			})
			.ReadOnly(ParentColumnType, EOptional::Yes)
			.ReadOnly<FTedsOutlinerColumn>(EOptional::Yes)
		.Compile();

		if (bUsingQueryConditionsSyntax)
		{
			UpdateParentQueryDescription.Conditions.Emplace(TColumn<FTypedElementSyncBackToWorldTag>());
		}
		else
		{
			UpdateParentQueryDescription.ConditionTypes.Add(FQueryDescription::EOperatorType::SimpleAll);
			UpdateParentQueryDescription.ConditionOperators.AddZeroed_GetRef().Type = FTypedElementSyncBackToWorldTag::StaticStruct();
		}
		
		// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
		MergeQueries(UpdateParentQueryDescription, FinalQueryDescription);
		
		ChildRowHandleQuery = Storage->RegisterQuery(MoveTemp(ChildHandleQueryDescription));
		UpdateParentQuery = Storage->RegisterQuery(MoveTemp(UpdateParentQueryDescription));
	}

	if (SelectionSetName.IsSet())
	{
		// Query to grab all selected rows
		FQueryDescription SelectedRowsQueryDescription;
		
		if (bUsingQueryConditionsSyntax)
		{
			SelectedRowsQueryDescription = Select()
				.Where(TColumn<FTypedElementSelectionColumn>())
				.Compile();
		}
		else
		{
			SelectedRowsQueryDescription =
				Select()
				.Where()
					.All(FTypedElementSelectionColumn::StaticStruct())
				.Compile();
		}
	
		// Query to track when a row gets selected
		FQueryDescription SelectionAddedQueryDescription =
							Select(
							TEXT("Row selected"),
							FObserver::OnAdd<FTypedElementSelectionColumn>().SetExecutionMode(EExecutionMode::GameThread),
							[this](IQueryContext& Context, DataStorage::RowHandle Row)
							{
								bSelectionDirty = true;
							})
							.Compile();

		// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
		MergeQueries(SelectionAddedQueryDescription, FinalQueryDescription);

		// Query to track when a row gets deselected
		FQueryDescription SelectionRemovedQueryDescription =
							Select(
							TEXT("Row deselected"),
							FObserver::OnRemove<FTypedElementSelectionColumn>().SetExecutionMode(EExecutionMode::GameThread),
							[this](IQueryContext& Context, DataStorage::RowHandle Row)
							{
								bSelectionDirty = true;
							})
							.Compile();

		// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
		MergeQueries(SelectionRemovedQueryDescription, FinalQueryDescription);

		SelectedRowsQuery = Storage->RegisterQuery(MoveTemp(SelectedRowsQueryDescription));
		SelectionAddedQuery = Storage->RegisterQuery(MoveTemp(SelectionAddedQueryDescription));
		SelectionRemovedQuery = Storage->RegisterQuery(MoveTemp(SelectionRemovedQueryDescription));
	}

	// Query to track when the label of a row we are observing changes, to re-filter/re-search for the item
	FQueryDescription LabelUpdateQueryDescription = 
		Select(
			TEXT("Re-Filter Teds Outliner Item on label change"),
			FProcessor(EQueryTickPhase::PostPhysics, Storage->GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[this](IQueryContext& Context, RowHandle Row, const FTypedElementLabelColumn& LabelColumn)
			{
				RowsPendingLabelUpdate.Add(Row);
			}
		)
		.Compile();
	
	if (bUsingQueryConditionsSyntax)
	{
		LabelUpdateQueryDescription.Conditions.Emplace(TColumn<FTypedElementSyncBackToWorldTag>() || TColumn<FTypedElementSyncFromWorldTag>());
	}
	else
	{
		LabelUpdateQueryDescription.ConditionTypes.Add(FQueryDescription::EOperatorType::SimpleAny);
		LabelUpdateQueryDescription.ConditionOperators.AddZeroed_GetRef().Type = FTypedElementSyncBackToWorldTag::StaticStruct();
		
		LabelUpdateQueryDescription.ConditionTypes.Add(FQueryDescription::EOperatorType::SimpleAny);
		LabelUpdateQueryDescription.ConditionOperators.AddZeroed_GetRef().Type = FTypedElementSyncFromWorldTag::StaticStruct();
	}
		
	// Add the conditions from FinalQueryDescription to ensure we are the rows the user requested
	MergeQueries(LabelUpdateQueryDescription, FinalQueryDescription);

	LabelUpdateQuery = Storage->RegisterQuery(MoveTemp(LabelUpdateQueryDescription));
}

void FTedsOutlinerImpl::UnregisterQueries()
{
	if (Storage)
	{
		Storage->UnregisterQuery(ChildRowHandleQuery);
		Storage->UnregisterQuery(UpdateParentQuery);
		Storage->UnregisterQuery(SelectedRowsQuery);
		Storage->UnregisterQuery(SelectionAddedQuery);
		Storage->UnregisterQuery(SelectionRemovedQuery);
		Storage->UnregisterQuery(LabelUpdateQuery);
		
		ChildRowHandleQuery = DataStorage::InvalidQueryHandle;
		UpdateParentQuery = DataStorage::InvalidQueryHandle;
		SelectedRowsQuery = DataStorage::InvalidQueryHandle;
		SelectionAddedQuery = DataStorage::InvalidQueryHandle;
		SelectionRemovedQuery = DataStorage::InvalidQueryHandle;
		LabelUpdateQuery = DataStorage::InvalidQueryHandle;
	}
}

void FTedsOutlinerImpl::ClearSelection() const
{
	if (!SelectionSetName.IsSet())
	{
		return;
	}

	using namespace UE::Editor::DataStorage::Queries;

	TArray<RowHandle> RowsToRemoveSelectionColumn;

	// Query to remove the selection column from all rows that belong to this selection set
	DirectQueryCallback RowCollector = CreateDirectQueryCallbackBinding(
	[this, &RowsToRemoveSelectionColumn](const IDirectQueryContext& Context, const RowHandle* RowHandles)
	{
		const TConstArrayView<RowHandle> Rows(RowHandles, Context.GetRowCount());

		for(const RowHandle RowHandle : Rows)
		{
			if (const FTypedElementSelectionColumn* SelectionColumn = Storage->GetColumn<FTypedElementSelectionColumn>(RowHandle))
			{
				if (SelectionColumn->SelectionSet == SelectionSetName)
				{
					RowsToRemoveSelectionColumn.Add(RowHandle);
				}
			}
		}
	});

	Storage->RunQuery(SelectedRowsQuery, RowCollector);

	for(const RowHandle RowHandle : RowsToRemoveSelectionColumn)
	{
		Storage->RemoveColumn<FTypedElementSelectionColumn>(RowHandle);
	}

}

void FTedsOutlinerImpl::PostDataStorageUpdate()
{
	// Update the label for any rows that might need it
	for (DataStorage::RowHandle Row : RowsPendingLabelUpdate)
	{
		// Search is currently handled externally and not through the query stack, so if a node doesn't match the search filters it should still
		// exist in the query stack. If it does not exist in the query stack, there's no need to check it against the search filter or add it to the
		// Outliner
		if (RowSortMapNode->GetRowIndex(Row) != INDEX_NONE)
		{
			// If the item already exists, it only needs an update if it passed a filter previously and does not now (or vice versa)
			if (FSceneOutlinerTreeItemPtr ExistingItem = SceneOutliner->GetTreeItem(Row))
			{
				bool bCachedFilteredFlag = ExistingItem->Flags.bIsFilteredOut;

				// This implicitly calls into the data storage to get the label of the row and check against the search query
				ExistingItem->Flags.bIsFilteredOut = !SceneOutliner->PassesAllFilters(ExistingItem);

				if (bCachedFilteredFlag != ExistingItem->Flags.bIsFilteredOut)
				{
					SceneOutliner->OnItemLabelChanged(ExistingItem, false);
				}
			}
			// If the item doesn't exist, create a dummy item to see if it would match the current search/filter queries and should be actually added
			else if (FSceneOutlinerTreeItemPtr PotentialItem = SceneOutlinerMode->CreateItemFor<FTedsOutlinerTreeItem>(
					FTedsOutlinerTreeItem(Row, AsShared()), true))
			{
				SceneOutliner->OnItemLabelChanged(PotentialItem, false);
			}
		}
	}
	
	RowsPendingLabelUpdate.Empty();
}

void FTedsOutlinerImpl::Tick()
{
	if (bSelectionDirty)
	{
		OnTedsOutlinerSelectionChanged.Broadcast();
		bSelectionDirty = false;
	}

	// The Sort node is the childmost node in all cases, so updating it causes all the nodes in the stack to update
	RowSortMapNode->Update();
}

bool FTedsOutlinerImpl::IsSorting() const
{
	return RowPrimarySortingNode ? RowPrimarySortingNode->IsSorting() : false;
}

void FTedsOutlinerImpl::OnSort(FName ColumnName, TSharedPtr<const DataStorage::FColumnSorterInterface> ColumnSorter, EColumnSortMode::Type Direction,
	EColumnSortPriority::Type Priority)
{
	if (RowPrimarySortingNode)
	{
		if (Direction == EColumnSortMode::Type::Ascending)
		{
			RowPrimarySortingNode->SetColumnSorter(ColumnSorter);
			RowPrimaryInversionNode->Enable(false);
		}
		else
		{
			RowPrimaryInversionNode->Enable(true);
		}
	}
}

void FTedsOutlinerImpl::SortItems(TArray<FSceneOutlinerTreeItemPtr>& Items, EColumnSortMode::Type Direction) const
{
	Items.Sort([this](FSceneOutlinerTreeItemPtr A, FSceneOutlinerTreeItemPtr B)
	{
		FTedsOutlinerTreeItem* TedsA = A->CastTo<FTedsOutlinerTreeItem>();
		FTedsOutlinerTreeItem* TedsB = B->CastTo<FTedsOutlinerTreeItem>();

		if (!TedsA)
		{
			return false;
		}

		if (!TedsB)
		{
			return true;
		}
			
		return RowSortMapNode->GetRowIndex(TedsA->GetRowHandle()) < RowSortMapNode->GetRowIndex(TedsB->GetRowHandle());
	});
}

DataStorage::ICoreProvider* FTedsOutlinerImpl::GetStorage() const
{
	return Storage;
}

DataStorage::IUiProvider* FTedsOutlinerImpl::GetStorageUI() const
{
	return StorageUi;
}

DataStorage::ICompatibilityProvider* FTedsOutlinerImpl::GetStorageCompatibility() const
{
	return StorageCompatibility;
}

TOptional<FName> FTedsOutlinerImpl::GetSelectionSetName() const
{
	return SelectionSetName;
}

FOnTedsOutlinerSelectionChanged& FTedsOutlinerImpl::OnSelectionChanged()
{
	return OnTedsOutlinerSelectionChanged;
}

ISceneOutlinerHierarchy::FHierarchyChangedEvent& FTedsOutlinerImpl::OnHierarchyChanged()
{
	return HierarchyChangedEvent;
}

const TOptional<FTedsOutlinerHierarchyData>& FTedsOutlinerImpl::GetHierarchyData()
{
	return HierarchyData;
}
} // namespace UE::Editor::Outliner

#undef LOCTEXT_NAMESPACE
