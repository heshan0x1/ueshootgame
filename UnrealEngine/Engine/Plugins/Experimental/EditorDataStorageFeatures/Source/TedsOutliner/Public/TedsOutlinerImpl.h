// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "ISceneOutlinerHierarchy.h"
#include "ISceneOutlinerMode.h"
#include "Columns/TedsOutlinerColumns.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Framework/TypedElementRowHandleArrayView.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "TedsOutlinerFilter.h"

#define UE_API TEDSOUTLINER_API

namespace UE::Editor::DataStorage
{
	namespace QueryStack
	{
		class FRowOrderInversionNode;
		class FRowSortNode;
		class FRowHandleSortNode;
		class FRowQueryResultsNode;
		class IQueryNode;
		class FRowMonitorNode;
		class FRowMergeNode;
		class FRowFilterNode;
		class IRowNode;
	}

	class IUiProvider;
	class ICompatibilityProvider;
} // namespace UE::Editor::DataStorage

namespace UE::Editor::Outliner
{
	class FRowMapNode;
}

struct FTypedElementWidgetConstructor;
class SWidget;

namespace UE::Editor::Outliner
{
// Struct storing information on how hierarchies are handled in the TEDS Outliner
struct FTedsOutlinerHierarchyData
{
	/** A delegate used to get the parent row handle for a given row */
	DECLARE_DELEGATE_RetVal_OneParam(DataStorage::RowHandle, FGetParentRowHandle, const void* /* InColumnData */);

	/** A delegate used to get the children row handles for a given row */
	DECLARE_DELEGATE_RetVal_OneParam(TArrayView<DataStorage::RowHandle>, FGetChildrenRowsHandles, void* /* InColumnData */);
	
	/** A delegate used to set the parent row handle for a given row */
	DECLARE_DELEGATE_TwoParams(FSetParentRowHandle, void* /* InColumnData */, DataStorage::RowHandle /* InParentRowHandle */);

	FTedsOutlinerHierarchyData(const UScriptStruct* InHierarchyColumn, const FGetParentRowHandle& InGetParent, const FSetParentRowHandle& InSetParent, const FGetChildrenRowsHandles& InGetChildren)
		: HierarchyColumn(InHierarchyColumn)
		, GetParent(InGetParent)
		, GetChildren(InGetChildren)
		, SetParent(InSetParent)
	{
	
	}

	// The column that contains the parent row handle for rows
	const UScriptStruct* HierarchyColumn;

	// Function to get parent row handle
	FGetParentRowHandle GetParent;

	// Function to get the children row handle
	FGetChildrenRowsHandles GetChildren;

	// Function to set the parent row handle
	FSetParentRowHandle SetParent;
	
	// Get the default hierarchy data for the TEDS Outliner that uses FTableRowParentColumn to get the parent
	static FTedsOutlinerHierarchyData GetDefaultHierarchyData()
	{
		const FGetParentRowHandle RowHandleGetter = FGetParentRowHandle::CreateLambda([](const void* InColumnData)
			{
				if(const FTableRowParentColumn* ParentColumn = static_cast<const FTableRowParentColumn *>(InColumnData))
				{
					return ParentColumn->Parent;
				}

				return DataStorage::InvalidRowHandle;
			});

		const FSetParentRowHandle RowHandleSetter = FSetParentRowHandle::CreateLambda([](void* InColumnData,
			DataStorage::RowHandle InRowHandle)
			{
				if(FTableRowParentColumn* ParentColumn = static_cast<FTableRowParentColumn *>(InColumnData))
				{
					ParentColumn->Parent = InRowHandle;
				}
			});
		
		return FTedsOutlinerHierarchyData(FTableRowParentColumn::StaticStruct(), RowHandleGetter, RowHandleSetter, FGetChildrenRowsHandles());
	}
};

struct FTedsOutlinerParams
{
	TEDSOUTLINER_API FTedsOutlinerParams(SSceneOutliner* InSceneOutliner);

	SSceneOutliner* SceneOutliner;

	// The query description that will be used to populate rows in the TEDS Outliner
	DataStorage::FQueryDescription QueryDescription;

	// The query description that will be used to populate the initial columns in the TEDS Outliner
	DataStorage::FQueryDescription ColumnQueryDescription;
	
	// TEDS Filters that utilize QueryDescriptions or QueryFunctions in this TEDS Outliner
	TArray<FTedsFilterData> Filters;

	// Class types to create filters for in this TEDS Outliner
	TArray<UClass*> ClassFilters;

	// If true, this Outliner will include a column for row handle
	bool bShowRowHandleColumn;

	// If true, parent nodes will be remain visible if a child passes all filters even if the parent fails a filter
	bool bForceShowParents;

	// If true, the Teds Outliner will create observers to track addition/removal of rows and update the Outliner
	bool bUseDefaultObservers;

	// If specified, this is how the TEDS Outliner will handle hierarchies. If not specified - there will be no hierarchies shown as a
	// parent-child relation in the tree view
	TOptional<FTedsOutlinerHierarchyData> HierarchyData;

	// The selection set to use for this Outliner, unset = don't propagate tree selection to the TEDS column
	TOptional<FName> SelectionSetOverride;

	// The purpose to use when generating widgets for row/column pairs through TEDS UI
	DataStorage::IUiProvider::FPurposeID CellWidgetPurpose;

	// The purpose to use when generating widgets for column headers through TEDS UI
	DataStorage::IUiProvider::FPurposeID HeaderWidgetPurpose;

	// The purpose to use when generating widgets for the "Item Label" column through TEDS UI
	DataStorage::IUiProvider::FPurposeID LabelWidgetPurpose;
};


// This class is meant to be a model to hold functionality to create a "table viewer" in TEDS that can be
// attached to any view/UI.
// TEDS-Outliner TODO: This class still has a few outliner implementation details leaking in that should be removed
class FTedsOutlinerImpl : public TSharedFromThis<FTedsOutlinerImpl>
{

public:
	// Helper function to create a label widget for a given row
	static UE_API TSharedRef<SWidget> CreateLabelWidget(
		DataStorage::ICoreProvider& Storage,
		DataStorage::IUiProvider& StorageUi,
		const DataStorage::IUiProvider::FPurposeID& Purpose,
		DataStorage::RowHandle TargetRow,
		ISceneOutlinerTreeItem& TreeItem,
		const STableRow<FSceneOutlinerTreeItemPtr>& RowItem,
		bool bInteractable = true);

	UE_API FTedsOutlinerImpl(const FTedsOutlinerParams& InParams, ISceneOutlinerMode* InMode, bool bInHybridMode);
	UE_API virtual ~FTedsOutlinerImpl();

	UE_API void Init();
	UE_API void OnMonitoredRowsAdded(DataStorage::FRowHandleArrayView InRows);
	UE_API void OnMonitoredRowsRemoved(DataStorage::FRowHandleArrayView InRows);

	// TEDS construct getters
	UE_API DataStorage::ICoreProvider* GetStorage() const;
	UE_API DataStorage::IUiProvider* GetStorageUI() const;
	UE_API DataStorage::ICompatibilityProvider* GetStorageCompatibility() const;

	UE_API TOptional<FName> GetSelectionSetName() const;

	// Delegate fired when the selection in TEDS changes, only if SelectionSetName is set
	UE_API FOnTedsOutlinerSelectionChanged& OnSelectionChanged();

	// Delegate fired when the hierarchy changes due to item addition/removal/move
	UE_API ISceneOutlinerHierarchy::FHierarchyChangedEvent& OnHierarchyChanged();

	// Delegate to check if a certain outliner item is compatible with this TEDS Outliner Impl - set by the system using FTedsOutlinerImpl
	DECLARE_DELEGATE_RetVal_OneParam(bool, FIsItemCompatible, const ISceneOutlinerTreeItem&)
	UE_API FIsItemCompatible& IsItemCompatible();

	// Update the selection in TEDS to the input rows, only if SelectionSetName is set
	UE_API void SetSelection(const TArray<DataStorage::RowHandle>& InSelectedRows);

	// Helper function to create a label widget for a given row
	UE_API TSharedRef<SWidget> CreateLabelWidgetForItem(DataStorage::RowHandle TargetRow, ISceneOutlinerTreeItem& TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& RowItem, bool bIsInteractable = true) const;

	// Get the hierarchy data associated with this table viewer
	UE_API const TOptional<FTedsOutlinerHierarchyData>& GetHierarchyData();
	
	// Add an external query description to the Outliner
	UE_API void AddExternalQuery(const FName& QueryName, const QueryHandle& InQueryHandle);
	UE_API void RemoveExternalQuery(const FName& QueryName);

	// Append all external queries into the given query description
	UE_API void AppendExternalQueries(DataStorage::FQueryDescription& OutQuery);

	// Add an external query function to the Outliner
	UE_API void AddExternalQueryFunction(const FName& QueryName, const TQueryFunction<bool>& InQueryFunction);
	UE_API void RemoveExternalQueryFunction(const FName& QueryName);

	// Add an external class query function to the Outliner (uses OR) 
	UE_API void AddClassQueryFunction(const FName& ClassName, const TQueryFunction<bool>& InClassQueryFunction);
	UE_API void RemoveClassQueryFunction(const FName& ClassName);
	
	// Outliner specific functionality
	UE_API void CreateItemsFromQuery(TArray<FSceneOutlinerTreeItemPtr>& OutItems, ISceneOutlinerMode* InMode);
	UE_API void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const;

	// Get the parent row for a given row
	UE_API DataStorage::RowHandle GetParentRow(DataStorage::RowHandle InRowHandle);

	UE_API bool ShouldForceShowParentRows() const;

	// Refresh the primary query node and its filters; use instead of RecompileQueries if the queries have not changed 
	UE_API void RefreshQueries() const;

	// Recompile all queries used by this table viewer
	UE_API void RecompileQueries();

	// Unregister all queries used by this table viewer
	UE_API void UnregisterQueries();
	
	UE_API void Tick();

	// Sorting delegates
	bool IsSorting() const;
	void OnSort(FName ColumnName, TSharedPtr<const DataStorage::FColumnSorterInterface> ColumnSorter, EColumnSortMode::Type Direction, 
		EColumnSortPriority::Type Priority);
	void SortItems(TArray<FSceneOutlinerTreeItemPtr>& Items, EColumnSortMode::Type Direction) const;

	UE_API DataStorage::RowHandle GetOutlinerRowHandle() const;

protected:
	
	UE_API void ClearSelection() const;
	UE_API void PostDataStorageUpdate();

	UE_API void CreateFilterQueries();

	// Check if this row can be displayed in this table viewer
	UE_API bool CanDisplayRow(DataStorage::RowHandle ItemRowHandle) const;

	// Register this Teds Outliner with TEDS
	UE_API void RegisterTedsOutliner();

	UE_API void ClearColumns() const;
	UE_API void RegenerateColumns();

protected:
	// TEDS Storage Constructs
	DataStorage::ICoreProvider* Storage{ nullptr };
	DataStorage::IUiProvider* StorageUi{ nullptr };
	DataStorage::ICompatibilityProvider* StorageCompatibility{ nullptr };

	FTedsOutlinerParams CreationParams;

	// The purpose to use when generating widgets for row/column pairs through TEDS UI
	DataStorage::IUiProvider::FPurposeID CellWidgetPurpose;
	
	// The purpose to use when generating widgets for headers pairs through TEDS UI
	DataStorage::IUiProvider::FPurposeID HeaderWidgetPurpose;

	// The purpose to use when generating widgets for the "Item Label" column through TEDS UI
	DataStorage::IUiProvider::FPurposeID LabelWidgetPurpose;
	
	// Initial query provided by user
	DataStorage::FQueryDescription InitialQueryDescription;

	// External query descriptions that are currently active (e.g Filters)
	TMap<FName, TSharedPtr<DataStorage::QueryStack::IQueryNode>> ExternalQueries;
	
	// External query functions that are currently active (e.g Filters)
    TMap<FName, DataStorage::Queries::TQueryFunction<bool>> ExternalQueryFunctions;

	// External query functions used to filter by class, these are the only filters that use OR instead of AND
	TMap<FName, DataStorage::Queries::TQueryFunction<bool>> ClassFilters;

	// Array to store all filter nodes created from the active query functions
	TArray<TSharedPtr<DataStorage::QueryStack::FRowFilterNode>> FilterNodes;

	// Array to store all filter nodes created from the active class filters
	TArray<TSharedPtr<DataStorage::QueryStack::FRowFilterNode>> ClassFilterNodes;

	// Optional Hierarchy Data
	TOptional<FTedsOutlinerHierarchyData> HierarchyData;
	// The query stack node responsible for collecting all rows that match the initial query on FullRefresh()
	TSharedPtr<DataStorage::QueryStack::FRowQueryResultsNode> InitialRowCollectorNode;
	// The query stack node responsible for collecting all rows that match the composite query on FullRefresh()
	TSharedPtr<DataStorage::QueryStack::FRowQueryResultsNode> CombinedRowCollectorNode;

	// The query stack node responsible for combining class filters - utilizes the unique merge approach (OR)
	TSharedPtr<DataStorage::QueryStack::IRowNode> CombinedClassFilterRowNode;

	// The query stack node responsible for combining all filters and queries as the 'final' node responsible
	// for getting all desired rows - utilizes the repeating merge approach (AND)
	TSharedPtr<DataStorage::QueryStack::IRowNode> FinalCombinedRowNode;

	// The query stack node responsible for tracking and row addition/removals that happen in between refreshes
	TSharedPtr<DataStorage::QueryStack::FRowMonitorNode> RowMonitorNode;

	// A query stack node that keeps a copy of our rows sorted by row handle for faster search/lookup
	TSharedPtr<DataStorage::QueryStack::FRowHandleSortNode> RowHandleSortNode;

	// A query stack node that keeps a copy of our rows sorted by the actual column the user wants to sort by
	TSharedPtr<DataStorage::QueryStack::FRowSortNode> RowPrimarySortingNode;

	// Inverts the rows from the query stack when needed by the primary sort.
	TSharedPtr<DataStorage::QueryStack::FRowOrderInversionNode> RowPrimaryInversionNode;

	// A map of the row handle -> index in the sort node for lookup
	TSharedPtr<FRowMapNode> RowSortMapNode;

	// Query to get all child rows
	DataStorage::QueryHandle ChildRowHandleQuery = DataStorage::InvalidQueryHandle;

	// Query to track when a row's parent gets changed
	DataStorage::QueryHandle UpdateParentQuery = DataStorage::InvalidQueryHandle;

	// Query to get all selected rows, track selection added, track selection removed
	DataStorage::QueryHandle SelectedRowsQuery = DataStorage::InvalidQueryHandle;
	DataStorage::QueryHandle SelectionAddedQuery = DataStorage::InvalidQueryHandle;
	DataStorage::QueryHandle SelectionRemovedQuery = DataStorage::InvalidQueryHandle;

	// Query to track when a row's label gets changed
	DataStorage::QueryHandle LabelUpdateQuery = DataStorage::InvalidQueryHandle;
	
	TOptional<FName> SelectionSetName;
	bool bSelectionDirty = false;

	// If true, parent nodes will remain visible if a child passes all filters even if the parent fails a filter
	bool bForceShowParents = true;
	
	// Ticker for selection updates so we don't fire the delegate multiple times in one frame for multi select
	FTSTicker::FDelegateHandle TickerHandle;
	
	FOnTedsOutlinerSelectionChanged OnTedsOutlinerSelectionChanged;

	// Scene Outliner specific constructors
	ISceneOutlinerMode* SceneOutlinerMode;
	SSceneOutliner* SceneOutliner;

	// Event fired when the hierarchy changes (addition/removal/move)
	ISceneOutlinerHierarchy::FHierarchyChangedEvent HierarchyChangedEvent;

	// Delegate to check if an item is compatible with this table viewer
	FIsItemCompatible IsItemCompatibleWithTeds;

	// Whether the row query is using the FConditions syntax (TColumn<FMyColumn>()) or the old syntax (.All().Any().None())
	bool bUsingQueryConditionsSyntax = false;

	// Addition and Label Updates are deferred because they access data storage implicitly
	// Deletion is currently not deferred to work nicely with object lifecycles in some cases - but can be once everything goes through the query stack
	TSet<DataStorage::RowHandle> RowsPendingLabelUpdate;

	// The Row Handle associated with this outliner
	DataStorage::RowHandle OutlinerRowHandle = DataStorage::InvalidRowHandle;

	// The columns currently being displayed by the Teds Outliner
	TArray<FName> AddedColumns;

	// Whether this is a hybrid outliner, i.e it is displaying TEDS rows and some non-TEDS items
	bool bHybridMode;
};
} // namsepace UE::Editor::Outliner

#undef UE_API
