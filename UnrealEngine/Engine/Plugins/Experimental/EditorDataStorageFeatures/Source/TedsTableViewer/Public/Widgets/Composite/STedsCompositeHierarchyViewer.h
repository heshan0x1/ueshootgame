// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DataStorage/Handles.h"
#include "TedsFilter.h"
#include "Widgets/STedsHierarchyViewer.h"

namespace UE::Editor::DataStorage
{
	class FTedsTableViewerModel;
	class IRowNode;
	class STedsFilterBar;
	class STedsSearchBox;
}

namespace UE::Editor::DataStorage
{
	class FTedsTableViewerColumn;

	/*
	 * A hierarchy viewer widget can be used to show a visual representation of data in TEDS. This composite widget adds features in a 'default' layout
	 * such as searching and filtering. The rows to display can be specified using a RowQueryStack, and the columns to display are directly input 
	 * into the widget
	 * Example usage:
	 *
	 *	SNew(STedsCompositeHierarchyViewer, HierarchyData) // Filtering and Searching enabled by default
     *		.HierarchyViewerArgs(SHierarchyViewer::FArguments()
     *			.AllNodeProvider(FilterNode)
     *			.Columns({ FTypedElementLabelColumn::StaticStruct(), FTypedElementClassTypeInfoColumn::StaticStruct() })
     *			.CellWidgetPurpose(PurposeId)
	 *		.Filters(MyCustomFilterArray);
	 */
	class STedsCompositeHierarchyViewer : public SHierarchyViewer
	{
	public:

		SLATE_BEGIN_ARGS(STedsCompositeHierarchyViewer)
			: _HierarchyViewerArgs()
			, _EnableSearching(true)
			, _EnableFiltering(true)
			, _UseSectionsForCategories(false)
		{
		}

		// Arguments used to create the Hierarchy Viewer
		SLATE_ARGUMENT(SHierarchyViewer::FArguments, HierarchyViewerArgs)
		
		// If we want to enable the search bar on the table
        SLATE_ARGUMENT(bool, EnableSearching)

		// If we want to enable the filter bar on the table
		SLATE_ARGUMENT(bool, EnableFiltering)

		// Array of filters to add to the filter bar *AND Filters
		SLATE_ARGUMENT(TArray<FTedsFilterData>, Filters)

		// Array of class filters to add to the filter bar *OR Filters
		SLATE_ARGUMENT(TArray<UClass*>, ClassFilters)
		
		// Whether to use submenus or sections for categories in the filter menu
		SLATE_ARGUMENT(bool, UseSectionsForCategories)

		SLATE_END_ARGS()

	public:
		
		TEDSTABLEVIEWER_API void Construct(const FArguments& InArgs, TSharedPtr<IHierarchyViewerDataInterface> InHierarchyInterface);

		// Clear the current list of columns being displayed and set it to the given list
		TEDSTABLEVIEWER_API virtual void SetColumns(const TArray<TWeakObjectPtr<const UScriptStruct>>& InColumns) override;

		// Add a custom per-row widget to the table viewer (that doesn't necessarily map to a TEDS column)
		// This means a new column for the table viewer
		TEDSTABLEVIEWER_API virtual void AddCustomRowWidget(const TSharedRef<FTedsTableViewerColumn>& InColumn) override;

		// Execute the given callback for each row that is selected in the table viewer
		TEDSTABLEVIEWER_API virtual void ForEachSelectedRow(TFunctionRef<void(RowHandle)> InCallback) const override;

		// Get the row handle for the widget row the table viewer's contents are stored in
		TEDSTABLEVIEWER_API virtual RowHandle GetWidgetRowHandle() const override;
		
		// Select the given row in the table viewer
		TEDSTABLEVIEWER_API virtual void SetSelection(RowHandle Row, bool bSelected, const ESelectInfo::Type SelectInfo) const override;

		// Scroll the given row into view in the table viewer
		TEDSTABLEVIEWER_API virtual void ScrollIntoView(RowHandle Row) const override;

		TEDSTABLEVIEWER_API virtual void ClearSelection() const override;
		
		TEDSTABLEVIEWER_API virtual TSharedRef<SWidget> AsWidget() override;

		TEDSTABLEVIEWER_API virtual bool IsSelected(RowHandle InRow) const override;

		TEDSTABLEVIEWER_API virtual bool IsSelectedExclusively(RowHandle InRow) const override;

	private:
		// The actual height of each item
		TAttribute<float> ItemHeight;

		// The actual padding between each item
		TAttribute<FMargin> ItemPadding;

		// Our model class
		TSharedPtr<FTedsTableViewerModel> Model;

		// Row Node passed to the SearchBox that receives the searched result
		TSharedPtr<QueryStack::IRowNode> SearchNode;

		// Row Node passed to the FilterBar that receives the filtered result
		TSharedPtr<QueryStack::IRowNode> FilterNode;
		
		// The search box widget
		TSharedPtr<STedsSearchBox> SearchBox;

		// The filter bar widget (uses MakeAddFilterButton to create the menu dropdown)
		TSharedPtr<STedsFilterBar> FilterBar;
		
		// The actual HierarchyViewer widget that displays the rows
		TSharedPtr<SHierarchyViewer> HierarchyViewer = MakeShared<SHierarchyViewer>();

		// Delegate fired when the selection changes
		FOnSelectionChanged OnSelectionChanged;

		// The message to show in place of the table viewer when there are no rows provided by the current query stack
		TAttribute<FText> EmptyRowsMessage;
	};
}