// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Composite/STedsCompositeHierarchyViewer.h"

#include "TedsQueryStackInterfaces.h"
#include "Widgets/STedsFilterBar.h"
#include "TedsTableViewerModel.h"
#include "Widgets/STedsSearchBox.h"

namespace UE::Editor::DataStorage
{
	void STedsCompositeHierarchyViewer::Construct(const FArguments& InArgs, TSharedPtr<IHierarchyViewerDataInterface> InHierarchyInterface)
	{
		TSharedPtr<SVerticalBox> ContentWidget = SNew(SVerticalBox);

		const bool bFilteringEnabled = InArgs._EnableFiltering;
		const bool bSearchingEnabled = InArgs._EnableSearching;

		SHierarchyViewer::FArguments HierarchyViewerArgs = InArgs._HierarchyViewerArgs;
		TSharedPtr<QueryStack::IRowNode> QueryStackNode = HierarchyViewerArgs._AllNodeProvider;
		
		if(bFilteringEnabled || bSearchingEnabled)
		{
			// Box containing the default search + filter widgets
			TSharedPtr<SHorizontalBox> FilterSearchBar = SNew(SHorizontalBox);

			if (bSearchingEnabled)
			{
				SearchBox = SNew(STedsSearchBox)
					.InSearchableRowNode(QueryStackNode)
					.OutSearchNode(&SearchNode);

				if (SearchNode.IsValid())
				{
					QueryStackNode = SearchNode;
				}
			}
			if (bFilteringEnabled)
			{
				FilterBar = SNew(STedsFilterBar)
					.InFilterableRowNode(QueryStackNode)
					.OutFilteredNode(&FilterNode)
					.Filters(InArgs._Filters)
					.ClassFilters(InArgs._ClassFilters)
					.UseSectionsForCategories(InArgs._UseSectionsForCategories)
					.OnPostFiltersChanged_Lambda([this]()
					{
						if (Model && FilterNode.IsValid())
						{
							// Forward the updated FilterNode to the Model QueryStack since it holds a static copy to the old filter node
							// Only called when the filters have changed, meaning the query stack has to be recomputed
							Model->SetQueryStack(FilterNode);
						}
					});

				if (FilterNode.IsValid())
				{
					QueryStackNode = FilterNode;
				}
			}

			// Add Filter Menu Button before Search Box if it was successfully created
			if (FilterBar)
			{
				FilterSearchBar->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				[
					FilterBar->MakeAddFilterButton(FilterBar.ToSharedRef())
				];
			}
			// Add Search Box if it was successfully created
			if (SearchBox)
			{
				FilterSearchBar->AddSlot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				[
					SearchBox.ToSharedRef()
				];
			}

			// Add Filter menu dropdown button + search bar above the viewer
			// TODO: Make it so that different default styles can be set to show the Filter/Search bar in different ways
			ContentWidget->AddSlot()
			.AutoHeight()
			[
				FilterSearchBar.ToSharedRef()
			];

			// Add Filter shelf
			if (bFilteringEnabled)
			{
				ContentWidget->AddSlot()
				.AutoHeight()
				.Padding(2.0f)
				[
					FilterBar.ToSharedRef()
				];
			}
		}

		HierarchyViewerArgs.AllNodeProvider(QueryStackNode);
		HierarchyViewer->Construct(HierarchyViewerArgs, MoveTemp(InHierarchyInterface));

		Model = HierarchyViewer->GetModel();
		
		ContentWidget->AddSlot()
		[
			HierarchyViewer->AsWidget()
		];
		
		ChildSlot
		[
			ContentWidget.ToSharedRef()
		];
	}

	void STedsCompositeHierarchyViewer::SetColumns(const TArray<TWeakObjectPtr<const UScriptStruct>>& InColumns)
	{
		HierarchyViewer->SetColumns(InColumns);
	}

	void STedsCompositeHierarchyViewer::AddCustomRowWidget(const TSharedRef<FTedsTableViewerColumn>& InColumn)
	{
		HierarchyViewer->AddCustomRowWidget(InColumn);
	}

	void STedsCompositeHierarchyViewer::ForEachSelectedRow(TFunctionRef<void(RowHandle)> InCallback) const
	{
		HierarchyViewer->ForEachSelectedRow(InCallback);
	}

	RowHandle STedsCompositeHierarchyViewer::GetWidgetRowHandle() const
	{
		return HierarchyViewer->GetWidgetRowHandle();
	}

	void STedsCompositeHierarchyViewer::SetSelection(RowHandle Row, bool bSelected, const ESelectInfo::Type SelectInfo) const
	{
		HierarchyViewer->SetSelection(Row, bSelected, SelectInfo);
	}

	void STedsCompositeHierarchyViewer::ScrollIntoView(RowHandle Row) const
	{
		HierarchyViewer->ScrollIntoView(Row);
	}

	void STedsCompositeHierarchyViewer::ClearSelection() const
	{
		HierarchyViewer->ClearSelection();
	}

	TSharedRef<SWidget> STedsCompositeHierarchyViewer::AsWidget()
	{
		return AsShared();
	}

	bool STedsCompositeHierarchyViewer::IsSelected(RowHandle InRow) const
	{
		return HierarchyViewer->IsSelected(InRow);
	}

	bool STedsCompositeHierarchyViewer::IsSelectedExclusively(RowHandle InRow) const
	{
		return HierarchyViewer->IsSelectedExclusively(InRow);
	}

} // namespace UE::Editor::DataStorage
