// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HierarchyViewerIntefaces.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Framework/Views/ITypedTableView.h"
#include "Widgets/SCompoundWidget.h"


class ISceneOutliner;
class SSceneOutliner;
class SHorizontalBox;
struct FTypedElementWidgetConstructor;
class SBorder;

namespace UE::Editor::DataStorage
{
	class ITableViewer;
	class STedsTableViewer;
	class FTedsTableViewerColumn;
	class SRowDetails;
	class IUiProvider;

	namespace QueryStack
	{
		class FTopLevelRowsNode;
		class FQueryNode;
		class IRowNode;
	}

	namespace Debug::QueryEditor
	{
		class FTedsQueryEditorModel;

		class SResultsView : public SCompoundWidget
		{
		public:
			SLATE_BEGIN_ARGS( SResultsView ){}
			SLATE_END_ARGS()

			~SResultsView() override;
			void Construct(const FArguments& InArgs, FTedsQueryEditorModel& InModel);
			void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

			ETableViewMode::Type GetViewMode() const;
			void SetViewMode(ETableViewMode::Type InViewMode);

		private:

			void OnModelChanged();
			void OnHierarchyChanged();
			void CreateRowHandleColumn();
			TSharedRef<SWidget> CreateTableViewer();
		
			FTedsQueryEditorModel* Model = nullptr;
			FDelegateHandle ModelChangedDelegateHandle;
			FDelegateHandle HierarchyChangedDelegateHandle;
			bool bModelDirty = true;


			QueryHandle CountQueryHandle = InvalidQueryHandle;
			
			TArray<RowHandle> TableViewerRows;
			TSharedPtr<ITableViewer> TableViewer;
			TSharedPtr<QueryStack::FQueryNode> QueryNode;
			TSharedPtr<QueryStack::IRowNode> RowQueryStack;

			// Hierarchy Data used by SHierarchyView to resolve parenting
			TSharedPtr<FHierarchyViewerData> HierarchyData;
			// The current hierarchy the widget is viewing
			FHierarchyHandle HierarchyHandle;
			
			// Custom column for the table viewer to display row handles
			TSharedPtr<FTedsTableViewerColumn> RowHandleColumn;

			// Widget that displays details of a row
			TSharedPtr<SRowDetails> RowDetailsWidget;
			
			IUiProvider* UiProvider = nullptr;

			// Border inside which the table viewer is stored so we can re-create it when needed
			TSharedPtr<SBorder> TableViewerSlot;

			ETableViewMode::Type ResultsViewMode = ETableViewMode::List;
		};

	} // namespace Debug::QueryEditor
} // namespace UE::Editor::DataStorage
