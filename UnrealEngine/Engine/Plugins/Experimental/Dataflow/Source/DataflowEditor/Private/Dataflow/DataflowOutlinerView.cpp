// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowOutlinerView.h"
#include "Dataflow/DataflowEditorPreviewSceneBase.h"
#include "Dataflow/DataflowOutlinerMode.h"
#include "TedsOutlinerImpl.h"
#include "Compatibility/SceneOutlinerTedsBridge.h"
#include "Dataflow/DataflowContent.h"
#include "DataStorage/Features.h"
#include "DataStorage/Queries/Description.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Columns/TypedElementVisibilityColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

#define LOCTEXT_NAMESPACE "DataflowOutlinerView"

FDataflowOutlinerView::FDataflowOutlinerView(TWeakPtr<FDataflowConstructionScene> InConstructionScene, TWeakPtr<FDataflowSimulationScene> InSimulationScene, TObjectPtr<UDataflowBaseContent> InContent)
	: FDataflowNodeView(InContent)
	, OutlinerWidget(nullptr)
	, ConstructionScene(InConstructionScene)
	, SimulationScene(InSimulationScene)
{
	check(InContent);
}

FDataflowOutlinerView::~FDataflowOutlinerView()
{}

TSharedPtr<ISceneOutliner> FDataflowOutlinerView::CreateWidget()
{
	using namespace UE::Editor::DataStorage::Queries;
	using namespace UE::Editor::Outliner;

	ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
	
	const UE::Editor::DataStorage::FQueryDescription RowQueryDescription = 
		Select()
		.Where(TColumn<FDataflowSceneObjectTag>(GetEditorContent()->GetDataflowOwner().GetFName()) || TColumn<FDataflowSceneStructTag>(GetEditorContent()->GetDataflowOwner().GetFName()) )
		.Compile();

	const UE::Editor::DataStorage::FQueryDescription ColumnQueryDescription =
		Select()
		.ReadOnly<FTypedElementClassTypeInfoColumn, FVisibleInEditorColumn>()
		.Compile();

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.FilterBarOptions.bHasFilterBar = true;
	InitOptions.bShowTransient = false;
	InitOptions.OutlinerIdentifier = "DataflowOutliner";
	
	UE::Editor::Outliner::FTedsOutlinerParams Params(nullptr);
	Params.QueryDescription = RowQueryDescription;
	Params.bShowRowHandleColumn = false;
	Params.ColumnQueryDescription = ColumnQueryDescription;
	
	// Add outliner filter queries
	Params.Filters.Emplace("Dataflow Construction",
		LOCTEXT("FilterDataflowConstructionDisplayName", "Dataflow Construction"),
		Storage->RegisterQuery(
			Select()
			.Where(TColumn<FDataflowConstructionObjectTag>())
			.Compile()));
	Params.Filters.Emplace("Dataflow Simulation",
		LOCTEXT("FilterDataflowSimulationDisplayName", "Dataflow Simulation"),
		Storage->RegisterQuery(
			Select()
			.Where(TColumn<FDataflowSimulationObjectTag>())
			.Compile()));
	Params.Filters.Emplace("Dataflow Elements",
		LOCTEXT("FilterDataflowElementsDisplayName", "Dataflow Elements"),
		Storage->RegisterQuery(
			Select()
			.Where(TColumn<FDataflowSceneStructTag>(GetEditorContent()->GetDataflowOwner().GetFName()))
			.Compile()));
	Params.Filters.Emplace("Dataflow Components",
		LOCTEXT("FilterDataflowComponentsDisplayName", "Dataflow Components"),
		Storage->RegisterQuery(
			Select()
			.Where(TColumn<FDataflowSceneObjectTag>(GetEditorContent()->GetDataflowOwner().GetFName()))
			.Compile()));
	
	// Empty selection set name is currently the level editor
	Params.SelectionSetOverride = FName("DataflowSelection");

	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([this, &Params](SSceneOutliner* Outliner)
	{
		Params.SceneOutliner = Outliner;
		return new FDataflowOutlinerMode(Params, ConstructionScene, SimulationScene);
	});
	
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));

	OutlinerWidget = SNew(SSceneOutliner, InitOptions);

	return OutlinerWidget;
}

void FDataflowOutlinerView::SetSupportedOutputTypes()
{
	GetSupportedOutputTypes().Empty();
	GetSupportedOutputTypes().Add("FManagedArrayCollection");
}

void FDataflowOutlinerView::RefreshView()
{
	UpdateViewData();
}

void FDataflowOutlinerView::UpdateViewData()
{
	if(OutlinerWidget)
	{
		OutlinerWidget->CollapseAll();
		OutlinerWidget->FullRefresh();
	}
}

void FDataflowOutlinerView::ConstructionViewSelectionChanged(const TArray<UPrimitiveComponent*>& SelectedComponents, const TArray<FDataflowBaseElement*>& SelectedElements)
{
	OutlinerWidget->ClearSelection();
	
	using namespace UE::Editor::DataStorage;
	TArray<RowHandle> SelectedRowHandles;
	if (const ICompatibilityProvider* Compatibility = GetDataStorageFeature<ICompatibilityProvider>(CompatibilityFeatureName))
	{
		// Transfer components selection to outliner
		for(UPrimitiveComponent* SelectedComponent : SelectedComponents)
		{
			if (FSceneOutlinerTreeItemPtr SelectedTreeItem = OutlinerWidget->GetTreeItem(
				Compatibility->FindRowWithCompatibleObject(SelectedComponent), true))
			{
				OutlinerWidget->AddToSelection(SelectedTreeItem);
				OutlinerWidget->ScrollItemIntoView(SelectedTreeItem);
			}
		}
		// Transfer elements selection to outliner
		for(FDataflowBaseElement* SelectedElement : SelectedElements)
		{
			if (FSceneOutlinerTreeItemPtr SelectedTreeItem = OutlinerWidget->GetTreeItem(
				Compatibility->FindRowWithCompatibleObject(SelectedElement), true))
			{
				OutlinerWidget->AddToSelection(SelectedTreeItem);
				OutlinerWidget->ScrollItemIntoView(SelectedTreeItem);
			}
		}
	}
}

void FDataflowOutlinerView::SimulationViewSelectionChanged(const TArray<UPrimitiveComponent*>& SelectedComponents, const TArray<FDataflowBaseElement*>& SelectedElements)
{
	ConstructionViewSelectionChanged(SelectedComponents, SelectedElements);
}

void FDataflowOutlinerView::AddReferencedObjects(FReferenceCollector& Collector)
{
	FDataflowNodeView::AddReferencedObjects(Collector);
}

#undef LOCTEXT_NAMESPACE