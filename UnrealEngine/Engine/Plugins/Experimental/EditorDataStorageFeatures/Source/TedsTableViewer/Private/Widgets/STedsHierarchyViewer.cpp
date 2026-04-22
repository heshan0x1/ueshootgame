// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/STedsHierarchyViewer.h"

#include "TedsTableViewerColumn.h"
#include "Columns/SlateDelegateColumns.h"
#include "DataStorage/Features.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Columns/TypedElementUIColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Elements/Framework/TypedElementDataStorageWidget.h"
#include "QueryStack/TopLevelRowsNode.h"
#include "Widgets/STedsTableViewerRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/STedsTreeView.h"

#define LOCTEXT_NAMESPACE "SHierarchyViewer"

namespace UE::Editor::DataStorage
{
	void SHierarchyViewer::Construct(
		const FArguments& InArgs,
		TSharedPtr<IHierarchyViewerDataInterface> InHierarchyInterface)
	{
		HierarchyInterface = MoveTemp(InHierarchyInterface);
		OnSelectionChanged = InArgs._OnSelectionChanged;
		EmptyRowsMessage = InArgs._EmptyRowsMessage;
		DefaultExpansionState = InArgs._DefaultExpansionState;
		
		IUiProvider::FPurposeID CellWidgetPurpose = InArgs._CellWidgetPurpose;

		IUiProvider* StorageUi = GetMutableDataStorageFeature<IUiProvider>(UiFeatureName);

		if (!ensureMsgf(StorageUi, TEXT("Cannot use SHierarchyVIewer without TEDS UI being initialized")))
		{
			return;
		}

		if (!CellWidgetPurpose.IsSet())
		{
			CellWidgetPurpose = StorageUi->GetGeneralWidgetPurposeID();
		}

		IUiProvider::FPurposeID HeaderWidgetPurpose = InArgs._HeaderWidgetPurpose;

		if (!HeaderWidgetPurpose.IsSet())
		{
			HeaderWidgetPurpose = IUiProvider::FPurposeInfo("General", "Header", NAME_None).GeneratePurposeID();
		}

		Model = MakeShared<FTedsTableViewerModel>(InArgs._AllNodeProvider, InArgs._Columns, CellWidgetPurpose, HeaderWidgetPurpose,
			FTedsTableViewerModel::FIsItemVisible::CreateSP(this, &SHierarchyViewer::IsItemVisible));
		
		TopLevelRowsNode = MakeShared<QueryStack::FTopLevelRowsNode>(Model->GetDataStorageInterface(), HierarchyInterface, Model->GetRowNode());
		
		HeaderRowWidget = SNew( SHeaderRow )
							.CanSelectGeneratedColumn(true);
		
		TedsWidget = StorageUi->CreateContainerTedsWidget(InvalidRowHandle);
			
		ChildSlot
		[
			TedsWidget->AsWidget()
		];
		
		AddWidgetColumns();

		TreeView = SNew(STedsTreeView, STedsTreeView::FOnGetParent::CreateSP(this, &SHierarchyViewer::GetParentRow), GetWidgetRowHandle())
			.HeaderRow(HeaderRowWidget)
			.TopLevelRowsSource(&TopLevelRows)
			.RowsSource(&Model->GetItems())
			.OnGenerateRow(this, &SHierarchyViewer::MakeTableRowWidget)
			.OnSelectionChanged(this, &SHierarchyViewer::OnListSelectionChanged)
			.SelectionMode(InArgs._ListSelectionMode);

		CreateInternalWidget();
		
		// Add each Teds column from the model to our header row widget
		Model->ForEachColumn([this](const TSharedRef<FTedsTableViewerColumn>& Column)
		{
			HeaderRowWidget->AddColumn(Column->ConstructHeaderRowColumn());
		});

		// Whenever the model changes, refresh the list to update the UI
		Model->GetOnModelChanged().AddRaw(this, &SHierarchyViewer::OnModelChanged);
	}

	void SHierarchyViewer::OnModelChanged()
	{
		TopLevelRows.Empty();

		// The current logic relies on the query stack to invalidate and cause a model refresh when the hierarchy changes since we don't have a
		// generic way of detecting hierarchy changes. If the model isn't refreshed on hierarchy change the top level rows will still use the old
		// hierarchy
		TopLevelRowsNode->Update();
		
		FRowHandleArrayView Rows(TopLevelRowsNode->GetRows());

		for(const RowHandle RowHandle : Rows)
		{
			if(Model->IsRowDisplayable(RowHandle))
			{
				FTedsRowHandle TedsRowHandle{ .RowHandle = RowHandle };
				TopLevelRows.Add(TedsRowHandle);
			}
		}
		
		TreeView->RequestListRefresh();
		CreateInternalWidget();
	}

	TSharedPtr<FTedsTableViewerModel> SHierarchyViewer::GetModel() const
	{
		return Model;
	}

	void SHierarchyViewer::AddWidgetColumns()
	{
		if(ICoreProvider* DataStorage = Model->GetDataStorageInterface())
		{
			const RowHandle WidgetRowHandle = TedsWidget->GetRowHandle();
		
			if(DataStorage->IsRowAvailable(WidgetRowHandle))
			{
				// FHideRowFromUITag - The table viewer should not show up as a row in a table viewer because that will cause all sorts of recursion issues
				// The others are Columns we are going to bind to attributes on STreeView
				DataStorage->AddColumns<FHideRowFromUITag, FWidgetContextMenuColumn, FWidgetRowScrolledIntoView, FWidgetDoubleClickedColumn>(WidgetRowHandle);
			}
		}
	}

	TableViewerItemPtr SHierarchyViewer::GetParentRow(TableViewerItemPtr InItem) const
	{
		// To work around unreliable query stack update timings, in case the widget queries parents before the top level rows node has updated
		// but after the hierarchies themselves have updated since HierarchyInterface directly queries TEDS. 
		if (TopLevelRows.Contains(InItem) || !HierarchyInterface)
		{
			return FTedsRowHandle(InvalidRowHandle);
		}
		
		return FTedsRowHandle(HierarchyInterface->GetParent(*Model->GetDataStorageInterface(), InItem));
	}

	void SHierarchyViewer::CreateInternalWidget()
	{
		TSharedPtr<SWidget> ContentWidget;

		// If there are no rows and the table viewer wants to show a message
		if(Model->GetRowCount() == 0 && EmptyRowsMessage.IsSet())
		{
			ContentWidget = 
				SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text(EmptyRowsMessage)
					];
		}
		else if(Model->GetColumnCount() == 0)
		{
			ContentWidget =
				SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("EmptyTableViewerColumnsText", "No columns found to display."))
					];
		}
		else
		{
			ContentWidget = TreeView.ToSharedRef();
		}

		TedsWidget->SetContent(ContentWidget.ToSharedRef());
	}

	void SHierarchyViewer::RefreshColumnWidgets()
	{
		HeaderRowWidget->ClearColumns();
		Model->ForEachColumn([this](const TSharedRef<FTedsTableViewerColumn>& Column)
		{
			HeaderRowWidget->AddColumn(Column->ConstructHeaderRowColumn());
		});

		CreateInternalWidget();
	}

	void SHierarchyViewer::OnListSelectionChanged(TableViewerItemPtr Item, ESelectInfo::Type SelectInfo)
	{
		if(OnSelectionChanged.IsBound())
		{
			OnSelectionChanged.Execute(Item);
		}
	}

	void SHierarchyViewer::SetColumns(const TArray<TWeakObjectPtr<const UScriptStruct>>& Columns)
	{
		Model->SetColumns(Columns);
		RefreshColumnWidgets();
	}

	void SHierarchyViewer::AddCustomRowWidget(const TSharedRef<FTedsTableViewerColumn>& InColumn)
	{
		Model->AddCustomRowWidget(InColumn);
		RefreshColumnWidgets();
	}

	void SHierarchyViewer::ForEachSelectedRow(TFunctionRef<void(RowHandle)> InCallback) const
	{
		TArray<TableViewerItemPtr> SelectedRows;
		TreeView->GetSelectedItems(SelectedRows);

		for(TableViewerItemPtr& Row : SelectedRows)
		{
			InCallback(Row);
		}
	}

	RowHandle SHierarchyViewer::GetWidgetRowHandle() const
	{
		return TedsWidget->GetRowHandle();
	}

	void SHierarchyViewer::SetSelection(RowHandle Row, bool bSelected, const ESelectInfo::Type SelectInfo) const
	{
		FTedsRowHandle TedsRowHandle{ .RowHandle = Row };
		TreeView->SetItemSelection(TedsRowHandle, bSelected, SelectInfo);
	}

	void SHierarchyViewer::ScrollIntoView(RowHandle Row) const
	{
		FTedsRowHandle TedsRowHandle{ .RowHandle = Row };
		TreeView->RequestScrollIntoView(TedsRowHandle);
	}

	void SHierarchyViewer::ClearSelection() const
	{
		TreeView->ClearSelection();
	}

	TSharedRef<SWidget> SHierarchyViewer::AsWidget()
	{
		return AsShared();
	}

	bool SHierarchyViewer::IsSelected(RowHandle InRow) const
	{
		FTedsRowHandle TedsRowHandle{ .RowHandle = InRow };
		return TreeView->IsItemSelected(TedsRowHandle);
	}

	bool SHierarchyViewer::IsSelectedExclusively(RowHandle InRow) const
	{
		return IsSelected(InRow) && TreeView->GetNumItemsSelected() == 1;
	}

	bool SHierarchyViewer::IsItemVisible(TableViewerItemPtr InItem) const
	{
		return TreeView->IsItemVisible(InItem);
	}

	TSharedRef<ITableRow> SHierarchyViewer::MakeTableRowWidget(TableViewerItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable) const
	{
		TreeView->SetItemExpansion(InItem, DefaultExpansionState.Get() == EExpansionState::Expanded);

		return SNew(SHierarchyViewerRow, OwnerTable, Model.ToSharedRef())
				.Padding(FMargin(0))
				.Item(InItem);
	}
} // namespace UE::Editor::DataStorage

#undef LOCTEXT_NAMESPACE //"SHierarchyViewer"

