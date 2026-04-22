// Copyright Epic Games, Inc. All Rights Reserved.


#include "SCompositePlatePassPanel.h"

#include "DetailsViewArgs.h"
#include "PropertyEditorModule.h"
#include "PropertyPath.h"
#include "SCompositePassTree.h"
#include "Layers/CompositeLayerPlate.h"
#include "Modules/ModuleManager.h"

void SCompositePlatePassPanel::Construct(const FArguments& InArgs, UCompositeLayerPlate* InPlate)
{
	Plate = InPlate;
	OnLayoutSizeChanged = InArgs._OnLayoutSizeChanged;
	
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.bShowScrollBar = false;
	DetailsViewArgs.bAllowSearch = false;
	
	DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);

	DetailsView->CountRows();
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(TreeView, SCompositePassTree, Plate)
			.OnSelectionChanged(this, &SCompositePlatePassPanel::OnTreeViewSelectionChanged)
			.OnLayoutChanged_Lambda([this]
			{
				OnLayoutSizeChanged.ExecuteIfBound();
			})
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			DetailsView.ToSharedRef()
		]
	];
}

void SCompositePlatePassPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// This panel often changes its size dynamically due to the details panel (showing new objects, expanding properties, etc.)
	// When this panel is embedded into a parent details panel (or any list/tree view), the dynamic size is an issue when it comes to
	// determining scroll bar visibility and size. As such, this panel invokes a OnLayoutSizeChange event that allows parent containers
	// to request a layout refresh, where the correct scroll bar size can be computed. To detect if the details view may have changed size,
	// we can use GetPropertyRowNumbers, which returns a list of properties that are actively being displayed as widgets (thus will exclude collapsed
	// properties). Caching this count on tick allows this panel to detect if the object properties being displayed have changed or been expanded.
	// The actual layout size changed event must be invoked one tick after a row count difference has been detected to give the details view
	// time to actually change its rendered geometry
	
	if (bLayoutSizeChanged)
	{
		OnLayoutSizeChanged.ExecuteIfBound();
		bLayoutSizeChanged = false;
	}
	
	const int32 RowCount = DetailsView->GetPropertyRowNumbers().Num();	
 	if (CachedDetailsViewRowCount != RowCount)
	{
		CachedDetailsViewRowCount = RowCount;
 		bLayoutSizeChanged = true;
	}
}

void SCompositePlatePassPanel::OnTreeViewSelectionChanged(const TArray<UObject*>& Objects)
{
	if (!DetailsView.IsValid())
	{
		return;
	}

	DetailsView->SetObjects(Objects);
}

