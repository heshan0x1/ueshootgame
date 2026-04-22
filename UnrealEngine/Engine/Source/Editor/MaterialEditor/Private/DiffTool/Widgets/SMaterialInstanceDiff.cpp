// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiffTool/Widgets/SMaterialInstanceDiff.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "GraphDiffControl.h"
#include "IDetailsView.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SBlueprintDiff.h"
#include "SDetailsDiff.h"
#include "SDetailsSplitter.h"
#include "SlateOptMacros.h"
#include "SMyBlueprint.h"
#include "SourceControlHelpers.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "DiffTool/MaterialToDiff.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Materials/MaterialInstance.h"

#define LOCTEXT_NAMESPACE "SMaterialGraphDiff"

static const FName DetailsMode = FName(TEXT("DetailsMode"));

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SMaterialInstanceDiff::Construct(const FArguments& InArgs)
{
	check(InArgs._OldMaterialInstance || InArgs._NewMaterialInstance);

	PanelOld.MaterialInstance = InArgs._OldMaterialInstance;
	PanelNew.MaterialInstance = InArgs._NewMaterialInstance;
	PanelOld.RevisionInfo = InArgs._OldRevision;
	PanelNew.RevisionInfo = InArgs._NewRevision;

	if (InArgs._ParentWindow.IsValid())
	{
		WeakParentWindow = InArgs._ParentWindow;

		AssetEditorCloseDelegate = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().AddSP(this, &SMaterialInstanceDiff::OnCloseAssetEditor);
	}

	FToolBarBuilder NavToolBarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None);
	NavToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SMaterialInstanceDiff::PrevDiff),
			FCanExecuteAction::CreateSP(this, &SMaterialInstanceDiff::HasPrevDiff)),
		NAME_None,
		LOCTEXT("PrevDiffLabel", "Prev"),
		LOCTEXT("PrevDiffTooltip", "Go to previous difference"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintDif.PrevDiff")
	);
	NavToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SMaterialInstanceDiff::NextDiff),
			FCanExecuteAction::CreateSP(this, &SMaterialInstanceDiff::HasNextDiff)),
		NAME_None,
		LOCTEXT("NextDiffLabel", "Next"),
		LOCTEXT("NextDiffTooltip", "Go to next difference"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintDif.NextDiff")
	);
	NavToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SMaterialInstanceDiff::ToggleViewport)),
		NAME_None,
		LOCTEXT("ToggleViewportLabel", "Toggle Viewport"),
		LOCTEXT("ToggleViewportTooltip", "Show/Hide a preview Viewport for the Materials diffed"),
		TAttribute<FSlateIcon>(this, &SMaterialInstanceDiff::GetViewportImage)
	);

	DifferencesTreeView = DiffTreeView::CreateTreeView(&PrimaryDifferencesList);

	GenerateDifferencesList();

	const auto TextBlock = [](FText Text) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
		.Padding(FMargin(4.0f, 10.0f))
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Visibility(EVisibility::HitTestInvisible)
			.TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle")
			.Text(Text)
		];
	};

	TopRevisionInfoWidget =
		SNew(SSplitter)
		.Visibility(EVisibility::HitTestInvisible)
		+ SSplitter::Slot()
		.Value(.2f)
		[
			SNew(SBox)
		]
		+ SSplitter::Slot()
		.Value(.8f)
		[
			SNew(SSplitter)
			.PhysicalSplitterHandleSize(10.0f)
			+ SSplitter::Slot()
			.Value(.5f)
			[
				TextBlock(DiffViewUtils::GetPanelLabel(PanelOld.MaterialInstance, PanelOld.RevisionInfo, FText()))
			]
			+ SSplitter::Slot()
			.Value(.5f)
			[
				TextBlock(DiffViewUtils::GetPanelLabel(PanelNew.MaterialInstance, PanelNew.RevisionInfo, FText()))
			]
		];

	// Clang-format off
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Docking.Tab", ".ContentAreaBrush"))
		[
			SNew(SOverlay)
			+SOverlay::Slot()
			.VAlign(VAlign_Top)
			[
				TopRevisionInfoWidget.ToSharedRef()		
			]
			+ SOverlay::Slot()
			.Padding(0.f, 6.f, 0.f, 4.f)
			[
				SNew(SOverlay)
				+SOverlay::Slot()
				.VAlign(VAlign_Top)
				.Padding(0.0f, 6.0f, 0.0f, 4.0f)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 2.0f)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.Padding(4.f)
						.AutoWidth()
						[
							NavToolBarBuilder.MakeWidget()
						]
						+SHorizontalBox::Slot()
						[
							SNew(SSpacer)
						]
					]
					+SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SNew(SSplitter)
						+SSplitter::Slot()
						.Value(.2f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							[
								DifferencesTreeView.ToSharedRef()
							]
						]
						+SSplitter::Slot()
						.Value(.8f)
						[
							SAssignNew(ModeContents, SBox)
						]
					]
				]
				
			]
		]
	];
	// clang-format on

	SetCurrentMode(DetailsMode);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SMaterialInstanceDiff::~SMaterialInstanceDiff()
{
	if (AssetEditorCloseDelegate.IsValid())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().Remove(AssetEditorCloseDelegate);
	}
}

void SMaterialInstanceDiff::OnDiffListSelectionChanged(TSharedPtr<FMaterialDiffResultItem> TheDiff)
{
}

TSharedRef<SWidget> SMaterialInstanceDiff::DefaultEmptyPanel()
{
	return SNew(SHorizontalBox)
		   +SHorizontalBox::Slot()
		   .HAlign(HAlign_Center)
		   .VAlign(VAlign_Center)
		   [
			   SNew(STextBlock)
			   .Text(LOCTEXT("MaterialInstanceDiffToolTip", "Select Material Instance to Diff"))
		   ];
}

TSharedPtr<SWindow> SMaterialInstanceDiff::CreateDiffWindow(FText WindowTitle, TObjectPtr<UMaterialInstance> OldMaterialInstance, TObjectPtr<UMaterialInstance> NewMaterialInstance, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision)
{
	TSharedPtr<SWindow> Window = SNew(SWindow)
									 .Title(WindowTitle)
									 .ClientSize(FVector2D(1000, 800));

	Window->SetContent(SNew(SMaterialInstanceDiff)
						   .OldMaterialInstance(OldMaterialInstance)
						   .NewMaterialInstance(NewMaterialInstance)
						   .OldRevision(OldRevision)
						   .NewRevision(NewRevision)
						   .ParentWindow(Window));

	// Make this window a child of the modal window if we've been spawned while one is active.
	const TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if (ActiveModal.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window.ToSharedRef(), ActiveModal.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	}

	return Window;
}

TSharedPtr<SWindow> SMaterialInstanceDiff::CreateDiffWindow(TObjectPtr<UMaterialInstance> OldMaterialInstance, TObjectPtr<UMaterialInstance> NewMaterialInstance, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision, const UClass* ObjectClass)
{
	check(OldMaterialInstance || NewMaterialInstance);

	// sometimes we're comparing different revisions of one single asset (other
	// times we're comparing two completely separate assets altogether)
	const bool bIsSingleAsset = !OldMaterialInstance || 
		                        !NewMaterialInstance || 
								(NewMaterialInstance->GetName() == OldMaterialInstance->GetName());

	FText WindowTitle = FText::Format(LOCTEXT("NamelessMaterialGraphDiff", "{0} Diff"), ObjectClass->GetDisplayNameText());
	// if we're diffing one asset against itself
	if (bIsSingleAsset)
	{
		// identify the assumed single asset in the window's title
		FString MaterialGraphName = OldMaterialInstance ? OldMaterialInstance->GetName() : NewMaterialInstance->GetName();
		WindowTitle = FText::Format(LOCTEXT("NamedMaterialGraphDiff", "{0} - {1} Diff"), FText::FromString(MaterialGraphName), ObjectClass->GetDisplayNameText());
	}

	return CreateDiffWindow(WindowTitle, OldMaterialInstance, NewMaterialInstance, OldRevision, NewRevision);
}

void SMaterialInstanceDiff::NextDiff()
{
	DiffTreeView::HighlightNextDifference(DifferencesTreeView.ToSharedRef(), RealDifferences, PrimaryDifferencesList);
}

void SMaterialInstanceDiff::PrevDiff()
{
	DiffTreeView::HighlightPrevDifference(DifferencesTreeView.ToSharedRef(), RealDifferences, PrimaryDifferencesList);
}

bool SMaterialInstanceDiff::HasNextDiff() const
{
	return DiffTreeView::HasNextDifference(DifferencesTreeView.ToSharedRef(), RealDifferences);
}

bool SMaterialInstanceDiff::HasPrevDiff() const
{
	return DiffTreeView::HasPrevDifference(DifferencesTreeView.ToSharedRef(), RealDifferences);
}

void SMaterialInstanceDiff::ToggleViewport()
{
	bShowViewport = !bShowViewport;
}

FSlateIcon SMaterialInstanceDiff::GetViewportImage() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports");
}
 
void SMaterialInstanceDiff::GenerateDifferencesList()
{
	PrimaryDifferencesList.Empty();
	RealDifferences.Empty();

	const auto CreateDetailsView = []()
	{
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
		DetailsViewArgs.bShowSectionSelector = true;
		DetailsViewArgs.bAllowFavoriteSystem = true;
		DetailsViewArgs.bShowScrollBar = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		return PropertyModule.CreateDetailView(DetailsViewArgs);
	};

	ModePanels.Add(DetailsMode, GenerateMaterialInstancePanel());

	DifferencesTreeView->RebuildList();
}

void SMaterialInstanceDiff::OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason)
{
	if (CloseReason == EAssetEditorCloseReason::CloseAllAssetEditors)
	{
		// Tell our window to close and set our selves to collapsed to try and stop it from ticking
		SetVisibility(EVisibility::Collapsed);
	
		if (AssetEditorCloseDelegate.IsValid())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().Remove(AssetEditorCloseDelegate);
		}
	
		if (WeakParentWindow.IsValid())
		{
			WeakParentWindow.Pin()->RequestDestroyWindow();
		}
	}
}

SMaterialInstanceDiff::FMaterialInstanceDiffControl SMaterialInstanceDiff::GenerateMaterialInstancePanel()
{
	const TSharedPtr<FDetailsDiffControl> NewDiffControl = MakeShared<FDetailsDiffControl>(PanelOld.MaterialInstance,
																						   PanelNew.MaterialInstance,
																						   FOnDiffEntryFocused::CreateRaw(this, &SMaterialInstanceDiff::SetCurrentMode, DetailsMode),
																						   true);
	NewDiffControl->EnableComments(DifferencesTreeView.ToWeakPtr());
	NewDiffControl->GenerateTreeEntries(PrimaryDifferencesList, RealDifferences);

	FMaterialInstanceDiffControl MaterialInstancePanel{};

	PanelOld.SetViewportToDisplay();
	PanelNew.SetViewportToDisplay();

	DiffDetailSplitter = SNew(SDetailsSplitter);

	if (PanelOld.MaterialInstance)
	{
		const UObject* OldMaterialInstance = PanelOld.MaterialInstance;
		DiffDetailSplitter->AddSlot(
			SDetailsSplitter::Slot()
			.Value(0.5f)
			.DetailsView(NewDiffControl->GetDetailsWidget(PanelOld.MaterialInstance))
			.DifferencesWithRightPanel(NewDiffControl.ToSharedRef(), &FDetailsDiffControl::GetDifferencesWithRight, OldMaterialInstance)
		);
	}
	if (PanelNew.MaterialInstance)
	{
		const UObject* NewMaterialInstance = PanelNew.MaterialInstance;
		DiffDetailSplitter->AddSlot(
			SDetailsSplitter::Slot()
			.Value(0.5f)
			.DetailsView(NewDiffControl->GetDetailsWidget(PanelNew.MaterialInstance))
			.DifferencesWithRightPanel(NewDiffControl.ToSharedRef(), &FDetailsDiffControl::GetDifferencesWithRight, NewMaterialInstance)
		);
	}

	MaterialInstancePanel.DiffControl = NewDiffControl;
	MaterialInstancePanel.Widget = SNew(SVerticalBox)
	+SVerticalBox::Slot()
	.FillHeight(1.0f)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+SSplitter::Slot()
			.Value(0.6f)
			[
				DiffDetailSplitter.ToSharedRef()
			]
			+SSplitter::Slot()
			.Value(0.4f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal).Visibility_Lambda([this]()
				{
					return bShowViewport ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SSplitter::Slot()
				[
					PanelOld.GetViewportToDisplay()
				]
				+SSplitter::Slot()
				[
					PanelNew.GetViewportToDisplay()
				]
			]
		]
	];

	return MaterialInstancePanel;
}

void SMaterialInstanceDiff::SetCurrentMode(FName NewMode)
{
	CurrentMode = NewMode;

	if (FMaterialInstanceDiffControl* FoundControl = ModePanels.Find(NewMode))
	{
		ModeContents->SetContent(FoundControl->Widget.ToSharedRef());
	}
	else
	{
		ensureMsgf(false, TEXT("Diff panel does not support mode %s"), *NewMode.ToString());
	}

	OnModeChanged(NewMode);
}

void SMaterialInstanceDiff::OnModeChanged(const FName& InNewViewMode) const
{
	
}

#undef LOCTEXT_NAMESPACE
