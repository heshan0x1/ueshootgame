// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshModelingToolsEditorModeToolkit.h"

#include "SkeletalMeshModelingToolsCommands.h"

#include "Tools/EdModeInteractiveToolsContext.h"
#include "Features/IModularFeatures.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "InteractiveTool.h"
#include "InteractiveToolsContext.h"
#include "ISkeletalMeshEditor.h"
#include "ModelingToolsEditorModeStyle.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Tools/UEdMode.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingToolTargetEditorOnlyUtil.h"
#include "PersonaTabs.h"
#include "SEnumCombo.h"
#include "SkeletalMeshEditingCache.h"
#include "SkeletalMeshModelingModeToolExtensions.h"
#include "SkeletalMeshModelingToolsEditorMode.h"
#include "SMorphTargetManager.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "SPrimaryButton.h"
#include "SkeletonModifier.h"
#include "SReferenceSkeletonTree.h"
#include "Components/SKMBackedDynaMeshComponent.h"
#include "Editor/EditorWidgets/Public/SEnumCombo.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Docking/SDockTab.h"
#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "SkeletalMeshModelingToolsEditorModeToolkit"

FSkeletalMeshModelingToolsEditorModeToolkit::~FSkeletalMeshModelingToolsEditorModeToolkit()
{
	if (UEditorInteractiveToolsContext* Context = GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode))
	{
		Context->OnToolNotificationMessage.RemoveAll(this);
		Context->OnToolWarningMessage.RemoveAll(this);
	}
}

void FSkeletalMeshModelingToolsEditorModeToolkit::Init(
	const TSharedPtr<IToolkitHost>& InToolkitHost, 
	TWeakObjectPtr<UEdMode> InOwningMode)
{
	bUsesToolkitBuilder = true;

	FModeToolkit::Init(InToolkitHost, InOwningMode);

	ModeWarningArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f)));
	ModeWarningArea->SetText(FText::GetEmpty());
	ModeWarningArea->SetVisibility(EVisibility::Collapsed);

	ModeHeaderArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	ModeHeaderArea->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
	ModeHeaderArea->SetJustification(ETextJustify::Center);
	
	ToolWarningArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f)));
	ToolWarningArea->SetText(FText::GetEmpty());


	RegisterPalettes();

	ClearNotification();
	ClearWarning();
	
	// create the toolkit widget
	{
		ToolkitSections->ModeWarningArea = ModeWarningArea;
		ToolkitSections->DetailsView = ModeDetailsView;
		ToolkitSections->ToolWarningArea = ToolWarningArea;
		ToolkitSections->Footer = MakeFooterWidget();

		SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(0)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			ToolkitBuilder->GenerateWidget()->AsShared()
		];	
	}
	
	ActiveToolName = FText::GetEmpty();
	ActiveToolMessage = FText::GetEmpty();

	if (UEditorInteractiveToolsContext* Context = GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode))
	{
		Context->OnToolNotificationMessage.AddSP(this, &FSkeletalMeshModelingToolsEditorModeToolkit::PostNotification);
		Context->OnToolWarningMessage.AddSP(this, &FSkeletalMeshModelingToolsEditorModeToolkit::PostWarning);
	}

	// add viewport overlay widget to accept / cancel tool
	MakeToolAcceptCancelWidget();

	
	EditorMode = CastChecked<USkeletalMeshModelingToolsEditorMode>(InOwningMode.Get());
	EditorMode->OnInitialized().AddSP(this, &FSkeletalMeshModelingToolsEditorModeToolkit::OnEditorModeInitialized);
}

FName FSkeletalMeshModelingToolsEditorModeToolkit::GetToolkitFName() const
{
	return FName("SkeletalMeshModelingToolsEditorModeToolkit");
}

FText FSkeletalMeshModelingToolsEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "Skeletal Mesh Editing Tools");
}

void FSkeletalMeshModelingToolsEditorModeToolkit::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	UpdateActiveToolProperties(Tool);

	Tool->OnPropertySetsModified.AddSP(this, &FSkeletalMeshModelingToolsEditorModeToolkit::UpdateActiveToolProperties, Tool);

	ModeHeaderArea->SetVisibility(EVisibility::Collapsed);
	ActiveToolName = Tool->GetToolInfo().ToolDisplayName;

	FString ActiveToolIdentifier = GetScriptableEditorMode()->GetToolManager()->GetActiveToolName(EToolSide::Left);
	ActiveToolIdentifier.InsertAt(0, ".");
	FName ActiveToolIconName = ISlateStyle::Join(FModelingToolsManagerCommands::Get().GetContextName(), TCHAR_TO_ANSI(*ActiveToolIdentifier));
	ActiveToolIcon = FModelingToolsEditorModeStyle::Get()->GetOptionalBrush(ActiveToolIconName);
	
	GetToolkitHost()->AddViewportOverlayWidget(ViewportOverlayWidget.ToSharedRef());

	AssetConfigPanel->SetExpanded_Animated(false);
}

void FSkeletalMeshModelingToolsEditorModeToolkit::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	if (IsHosted())
	{
		GetToolkitHost()->RemoveViewportOverlayWidget(ViewportOverlayWidget.ToSharedRef());
	}
	
	if (Tool)
	{
		Tool->OnPropertySetsModified.RemoveAll(this);
	}

	ModeHeaderArea->SetVisibility(EVisibility::Visible);
	ModeDetailsView->SetObject(nullptr);
	ActiveToolName = FText::GetEmpty();
	ClearNotification();
	ClearWarning();
	
	AssetConfigPanel->SetExpanded_Animated(true);
}

FText FSkeletalMeshModelingToolsEditorModeToolkit::GetActiveToolDisplayName() const
{
	return ActiveToolName;
}


FText FSkeletalMeshModelingToolsEditorModeToolkit::GetActiveToolMessage() const
{
	return ActiveToolMessage;
}

void FSkeletalMeshModelingToolsEditorModeToolkit::InvokeUI()
{
	// Skipping parent class call here since persona already spawns a tool box tab
	// invoking the UI here would create a second tool box tab
	// FModeToolkit::InvokeUI();
	
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		TSharedPtr<FTabManager> TabManager = ModeUILayerPtr->GetTabManager();
		

		SAssignNew(MorphTargetManagerWidget, SBorder)
		.Padding(4.f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"));
		
		// Morph Target
		{
			const FName& MorphTargetTabID = FPersonaTabs::MorphTargetsID;
			TSharedPtr<SDockTab> MorphTargetTab = TabManager->FindExistingLiveTab(MorphTargetTabID);
			if (!MorphTargetTab.IsValid())
			{
				MorphTargetTab = TabManager->TryInvokeTab(MorphTargetTabID);
			}
		
			{
				DefaultMorphTargetWidget = MorphTargetTab->GetContent();
			
				MorphTargetTab->SetContent(MorphTargetManagerWidget.ToSharedRef());
			}
		}	
	}

	
}

void FSkeletalMeshModelingToolsEditorModeToolkit::ShutdownUI()
{
	//Skeleton
	SkeletonNotifierBindScope.Reset();
	
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		TSharedPtr<FTabManager> TabManager = ModeUILayerPtr->GetTabManager();

		// Morph Target
		{
			const FName& MorphTargetTabID = FPersonaTabs::MorphTargetsID;
			TSharedPtr<SDockTab> MorphTargetTab = TabManager->FindExistingLiveTab(MorphTargetTabID);
			if (MorphTargetTab.IsValid())
			{
				// switch back to default widget if it was replaced before
				if (DefaultMorphTargetWidget)
				{
					MorphTargetTab->SetContent(DefaultMorphTargetWidget.ToSharedRef());
				}
			}
		}	
	}

	FModeToolkit::ShutdownUI();
}

TArray<FName> FSkeletalMeshModelingToolsEditorModeToolkit::GetSelectedBonesForDynamicMeshSkeleton()
{
	TArray<FName> SelectedBones;
	RefSkeletonTree->GetSelectedBoneNames(SelectedBones);
	return SelectedBones;
}

void FSkeletalMeshModelingToolsEditorModeToolkit::ResetDynamicMeshBoneTransforms(bool bSelectedOnly)
{
	FText Title = bSelectedOnly ? LOCTEXT("ResetBoneTransforms", "Reset Bone Transforms") : LOCTEXT("ResetAllBonesTransforms", "Reset All Bone Transforms");
	
	FScopedTransaction Transaction(Title);
	
	if (EditorMode.IsValid())
	{
		EditorMode->GetCurrentEditingCache()->ResetDynamicMeshBoneTransforms(bSelectedOnly);
	}
}


void FSkeletalMeshModelingToolsEditorModeToolkit::RegisterPalettes()
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();
	const TSharedPtr<FUICommandList> CommandList = GetToolkitCommands();
	
	ToolkitSections = MakeShared<FToolkitSections>();
	FToolkitBuilderArgs ToolkitBuilderArgs(GetScriptableEditorMode()->GetModeInfo().ToolbarCustomizationName);
	ToolkitBuilderArgs.ToolkitCommandList = GetToolkitCommands();
	ToolkitBuilderArgs.ToolkitSections = ToolkitSections;
	ToolkitBuilderArgs.SelectedCategoryTitleVisibility = EVisibility::Collapsed;
	ToolkitBuilder = MakeShared<FToolkitBuilder>(ToolkitBuilderArgs);

	const TArray<TSharedPtr<FUICommandInfo>> SkeletonCommands({
		Commands.BeginSkeletonEditingTool
	});
	ToolkitBuilder->AddPalette(
		MakeShareable( new FToolPalette( Commands.LoadSkeletonTools.ToSharedRef(), SkeletonCommands ) ) );

	const TArray<TSharedPtr<FUICommandInfo>> SkinCommands({
		Commands.BeginSkinWeightsBindingTool,
		Commands.BeginSkinWeightsPaintTool,
		Commands.BeginAttributeEditorTool,
		Commands.BeginMeshAttributePaintTool,
		Commands.BeginMeshVertexPaintTool,
		Commands.BeginPolyGroupsTool,
		Commands.BeginMeshGroupPaintTool
	});
	ToolkitBuilder->AddPalette(
		MakeShareable( new FToolPalette( Commands.LoadSkinTools.ToSharedRef(), SkinCommands ) ) );

	const TArray<TSharedPtr<FUICommandInfo>> DeformCommands({
		Commands.BeginSculptMeshTool,
		Commands.BeginRemeshSculptMeshTool,
		Commands.BeginSmoothMeshTool,
		Commands.BeginOffsetMeshTool,
		Commands.BeginMeshSpaceDeformerTool,
		Commands.BeginLatticeDeformerTool,
		Commands.BeginDisplaceMeshTool
	});
	ToolkitBuilder->AddPalette(
		MakeShareable( new FToolPalette( Commands.LoadDeformTools.ToSharedRef(), DeformCommands ) ) );

	const TArray<TSharedPtr<FUICommandInfo>> ModelingMeshCommands({
		Commands.BeginPolyEditTool,
		Commands.BeginTriEditTool,
		Commands.BeginPolyDeformTool,
		Commands.BeginHoleFillTool,
		Commands.BeginPolygonCutTool,
		});
	ToolkitBuilder->AddPalette(
		MakeShareable( new FToolPalette( Commands.LoadPolyTools.ToSharedRef(), ModelingMeshCommands ) ) );

	const TArray<TSharedPtr<FUICommandInfo>> ProcessMeshCommands({
		Commands.BeginSimplifyMeshTool,
		Commands.BeginRemeshMeshTool,
		Commands.BeginWeldEdgesTool,
		Commands.BeginRemoveOccludedTrianglesTool,
		Commands.BeginProjectToTargetTool
	});
	ToolkitBuilder->AddPalette(
		MakeShareable( new FToolPalette( Commands.LoadMeshOpsTools.ToSharedRef(), ProcessMeshCommands ) ) );

	ToolkitBuilder->SetActivePaletteOnLoad(Commands.LoadSkinTools.Get());
	ToolkitBuilder->UpdateWidget();

	RegisterExtensionsPalettes();
	
	// if selected palette changes, make sure we are showing the palette command buttons, which may be hidden by active Tool
	ActivePaletteChangedHandle = ToolkitBuilder->OnActivePaletteChanged.AddLambda([this]()
	{
		ToolkitBuilder->SetActivePaletteCommandsVisibility(EVisibility::Visible);
	});
}

void FSkeletalMeshModelingToolsEditorModeToolkit::RegisterExtensionsPalettes() const
{
	const TArray<ISkeletalMeshModelingModeToolExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<ISkeletalMeshModelingModeToolExtension>(
		ISkeletalMeshModelingModeToolExtension::GetModularFeatureName());

	if (Extensions.IsEmpty())
	{
		return;
	}

	for (ISkeletalMeshModelingModeToolExtension* Extension: Extensions)
	{
		const FText ExtensionName = Extension->GetExtensionName();
		const FText SectionName = Extension->GetToolSectionName();

		FModelingModeExtensionExtendedInfo ExtensionExtendedInfo;
		const bool bHasExtendedInfo = Extension->GetExtensionExtendedInfo(ExtensionExtendedInfo);

		TSharedPtr<FUICommandInfo> PaletteCommand;
		if (bHasExtendedInfo && ExtensionExtendedInfo.ExtensionCommand.IsValid())
		{
			PaletteCommand = ExtensionExtendedInfo.ExtensionCommand;
		}
		else
		{
			const FText UseTooltipText = (bHasExtendedInfo && ExtensionExtendedInfo.ToolPaletteButtonTooltip.IsEmpty() == false) ?
				ExtensionExtendedInfo.ToolPaletteButtonTooltip : SectionName;
			PaletteCommand = FModelingToolsManagerCommands::RegisterExtensionPaletteCommand(
				FName(ExtensionName.ToString()),
				SectionName, UseTooltipText, FSlateIcon());
		}

		TArray<TSharedPtr<FUICommandInfo>> PaletteItems;
		FExtensionToolQueryInfo ExtensionQueryInfo;
		ExtensionQueryInfo.bIsInfoQueryOnly = true;

		TArray<FExtensionToolDescription> ToolSet;
		Extension->GetExtensionTools(ExtensionQueryInfo, ToolSet);
		
		for (const FExtensionToolDescription& ToolInfo : ToolSet)
		{
			PaletteItems.Add(ToolInfo.ToolCommand);
		}

		ToolkitBuilder->AddPalette(
			MakeShareable(new FToolPalette(PaletteCommand.ToSharedRef(), PaletteItems)));
	}
}

void FSkeletalMeshModelingToolsEditorModeToolkit::MakeToolAcceptCancelWidget()
{
	SAssignNew(ViewportOverlayWidget, SHorizontalBox)

	+SHorizontalBox::Slot()
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(0.0f, 0.0f, 0.f, 15.f))
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewport.OverlayBrush"))
		.Padding(8.f)
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SImage)
				.Image_Lambda([this] () { return ActiveToolIcon; })
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(STextBlock)
				.Text(this, &FSkeletalMeshModelingToolsEditorModeToolkit::GetActiveToolDisplayName)
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.0, 0.f, 2.f, 0.f))
			[
				SNew(SPrimaryButton)
				.Text_Lambda([this]()
					{
						if (EditorMode->GetApplyMode() == USkeletalMeshModelingToolsEditorMode::EApplyMode::ApplyOnToolExit)
						{
							return LOCTEXT("OverlayApplyToAsset", "Apply to Asset");
						}
						
						check(EditorMode->GetApplyMode() == USkeletalMeshModelingToolsEditorMode::EApplyMode::ApplyManually)
						
						return LOCTEXT("OverlayExitTool", "Exit Tool");
					})
				.ToolTipText_Lambda([this]()
				{
					if (EditorMode->GetApplyMode() == USkeletalMeshModelingToolsEditorMode::EApplyMode::ApplyOnToolExit)
					{
						return LOCTEXT("OverlayApplyToAssetToolTip",
							"\"Apply on Tool Exit\" mode is active: changes are applied to the asset immediately. "
							"Switching to a different tool discards current change");
					}
						
					check(EditorMode->GetApplyMode() == USkeletalMeshModelingToolsEditorMode::EApplyMode::ApplyManually)
						
					return LOCTEXT("OverlayExitToolToolTip",
						"\"Apply Manually\" mode is active: changes are saved when Exiting tool or Switching to a different tool");
				})
				.OnClicked_Lambda([this]() { GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Accept); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanAcceptActiveTool(); })
				.Visibility_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->ActiveToolHasAccept() ? EVisibility::Visible : EVisibility::Collapsed; })
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.TextStyle( FAppStyle::Get(), "DialogButtonText" )
				.Text(LOCTEXT("OverlayCancel", "Cancel"))
				.ToolTipText(LOCTEXT("OverlayCancelTooltip", "Cancel the active Tool [Esc]"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Cancel); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanCancelActiveTool(); })
				.Visibility_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->ActiveToolHasAccept() ? EVisibility::Visible : EVisibility::Collapsed; })
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0, 0.f, 0.f, 0.f))
			[
				SNew(SPrimaryButton)
				.Text(LOCTEXT("OverlayComplete", "Complete"))
				.ToolTipText(LOCTEXT("OverlayCompleteTooltip", "Exit the active Tool [Enter]"))
				.OnClicked_Lambda([this]() { GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Completed); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanCompleteActiveTool(); })
				.Visibility_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanCompleteActiveTool() ? EVisibility::Visible : EVisibility::Collapsed; })
			]
		]	
	];
}

void FSkeletalMeshModelingToolsEditorModeToolkit::PostNotification(const FText& InMessage)
{
	ClearNotification();
	
	ActiveToolMessage = InMessage;

	if (ModeUILayer.IsValid())
	{
		const FName StatusBarName = ModeUILayer.Pin()->GetStatusBarName();
		ActiveToolMessageHandle = GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PushStatusBarMessage(StatusBarName, ActiveToolMessage);
	}
}


void FSkeletalMeshModelingToolsEditorModeToolkit::ClearNotification()
{
	ActiveToolMessage = FText::GetEmpty();

	if (ModeUILayer.IsValid())
	{
		const FName StatusBarName = ModeUILayer.Pin()->GetStatusBarName();
		GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PopStatusBarMessage(StatusBarName, ActiveToolMessageHandle);
	}
	ActiveToolMessageHandle.Reset();
}


void FSkeletalMeshModelingToolsEditorModeToolkit::PostWarning(const FText& Message)
{
	ToolWarningArea->SetText(Message);
	ToolWarningArea->SetVisibility(EVisibility::Visible);
}


void FSkeletalMeshModelingToolsEditorModeToolkit::ClearWarning()
{
	ToolWarningArea->SetText(FText());
	ToolWarningArea->SetVisibility(EVisibility::Collapsed);
}

void FSkeletalMeshModelingToolsEditorModeToolkit::OnEditorModeInitialized()
{
	USkeletalMesh* SkeletalMesh = EditorMode->GetSkeletalMesh();

	constexpr bool bSkipAutoGenerated= true;
	AssetAvailableLODs = UE::ToolTarget::GetAvailableLODs(SkeletalMesh, bSkipAutoGenerated);
	
	for (EMeshLODIdentifier LOD : AssetAvailableLODs)
	{
		int32 LODIndex = static_cast<int32>(LOD);
		FString LODName = TEXT("LOD") + FString::FromInt(LODIndex);
		AssetLODModes.Add(MakeShared<FString>(LODName));
	}

	AssetLODMode->SetSelectedItem(AssetLODModes[0]);

	int32 EnumIndex = ApplyModeEnums.IndexOfByKey(EditorMode->GetApplyMode());
	ApplyModeComboBox->SetSelectedItem(ApplyModeStrings[EnumIndex]);

	SAssignNew(RefSkeletonWidget, SBorder)
	.Padding(4.f)
	.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
	[
		SAssignNew(RefSkeletonTree, SReferenceSkeletonTree)
			.Modifier(EditorMode->GetSkeletonReader())
			.bIsReadOnly(true)
	];

	SMorphTargetManager::FMorphTargetManagerDelegates Delegates;

	Delegates.OnGetMorphTargets.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::GetMorphTargets);
	
	Delegates.OnGetMorphTargetWeight.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::GetMorphTargetWeight);
	Delegates.OnSetMorphTargetWeight.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleSetMorphTargetWeight);
	
	Delegates.OnGetMorphTargetAutoFill.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::GetMorphTargetAutoFill);
	Delegates.OnSetMorphTargetAutoFill.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleSetMorphTargetAutoFill);
	
	Delegates.OnGetEditingMorphTarget.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::GetEditingMorphTarget);
	Delegates.OnSetEditingMorphTarget.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleSetEditingMorphTarget);
	
	Delegates.OnAddNewMorphTarget.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleAddMorphTarget);
	Delegates.OnRenameMorphTarget.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleRenameMorphTarget);
	Delegates.OnRemoveMorphTargets.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleRemoveMorphTargets);
	Delegates.OnDuplicateMorphTargets.BindUObject(EditorMode.Get(), &USkeletalMeshModelingToolsEditorMode::HandleDuplicateMorphTargets);
	
	SAssignNew(MorphTargetManager, SMorphTargetManager)
			.Delegates(Delegates);
	MorphTargetManagerWidget->SetContent(MorphTargetManager.ToSharedRef());

	// Make sure viewport selection is routed to the default skeleton tree by default
	SkeletonNotifierBindScope.Reset(
		new UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope(
			EditorMode->GetModeBinding()->GetNotifier(),
			EditorMode->GetEditorBinding()->GetNotifier()));


	
}

void FSkeletalMeshModelingToolsEditorModeToolkit::ShowDynamicMeshSkeletonTree()
{
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		TSharedPtr<FTabManager> TabManager = ModeUILayerPtr->GetTabManager();

		// Skeleton Tree
		{
			const FName& SkeletonTreeId = FPersonaTabs::SkeletonTreeViewID;
			TSharedPtr<SDockTab> SkeletonTab = TabManager->FindExistingLiveTab(SkeletonTreeId);
			if (!SkeletonTab.IsValid())
			{
				SkeletonTab = TabManager->TryInvokeTab(SkeletonTreeId);
			}
		
			if (SkeletonTab->GetContent() != RefSkeletonWidget)
			{
				DefaultSkeletonWidget = SkeletonTab->GetContent();
			
				// switch between SSkeletonTree and SReferenceSkeletonTree
				SkeletonTab->SetContent(RefSkeletonWidget.ToSharedRef());

				
				// Make sure viewport selection is routed to the ref skeleton tree
				SkeletonNotifierBindScope.Reset(
					new UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope(
						EditorMode->GetModeBinding()->GetNotifier(),
						RefSkeletonTree->GetNotifier()));
				
				TArray<FName> SelectedBones = EditorMode->GetSelectedBones();
				
				EditorMode->GetEditorBinding()->GetNotifier()->HandleNotification({}, ESkeletalMeshNotifyType::BonesSelected);
				RefSkeletonTree->GetNotifier()->HandleNotification({}, ESkeletalMeshNotifyType::HierarchyChanged);
				RefSkeletonTree->GetNotifier()->HandleNotification(SelectedBones, ESkeletalMeshNotifyType::BonesSelected);


				const TSharedRef<FUICommandList>& CommandList = EditorMode->GetEditor()->GetToolkitCommands();
				DefaultResetBoneTransformsAction = *(CommandList->GetActionForCommand(EditorMode->GetEditor()->GetResetBoneTransformsCommand()));
				DefaultResetAllBoneTransformsAction = *(CommandList->GetActionForCommand(EditorMode->GetEditor()->GetResetAllBonesTransformsCommand()));

				// Reset transforms
				static constexpr bool bSelectedOnly = true;
				CommandList->MapAction(EditorMode->GetEditor()->GetResetBoneTransformsCommand(),
					FExecuteAction::CreateSP(this, &FSkeletalMeshModelingToolsEditorModeToolkit::ResetDynamicMeshBoneTransforms ,bSelectedOnly));
	
				CommandList->MapAction(EditorMode->GetEditor()->GetResetAllBonesTransformsCommand(),
					FExecuteAction::CreateSP(this, &FSkeletalMeshModelingToolsEditorModeToolkit::ResetDynamicMeshBoneTransforms ,!bSelectedOnly));
			}
		}
	}
}

void FSkeletalMeshModelingToolsEditorModeToolkit::ShowSkeletalMeshSkeletonTree()
{
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		TSharedPtr<FTabManager> TabManager = ModeUILayerPtr->GetTabManager();

		// Skeleton Tree
		{
			const FName& SkeletonTreeViewID = FPersonaTabs::SkeletonTreeViewID;
			TSharedPtr<SDockTab> SkeletonTab = TabManager->FindExistingLiveTab(SkeletonTreeViewID);
			if (!SkeletonTab.IsValid())
			{
				SkeletonTab = TabManager->TryInvokeTab(SkeletonTreeViewID);
			}

			// switch back to default widget if it was replaced before
			if (DefaultSkeletonWidget)
			{
				SkeletonTab->SetContent(DefaultSkeletonWidget.ToSharedRef());
			}
		}

		// Make sure viewport selection is routed to the default skeleton tree
		SkeletonNotifierBindScope.Reset(
			new UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope(
				EditorMode->GetModeBinding()->GetNotifier(),
				EditorMode->GetEditorBinding()->GetNotifier()));

		TArray<FName> SelectedBones = EditorMode->GetSelectedBones();
		RefSkeletonTree->GetNotifier()->HandleNotification({}, ESkeletalMeshNotifyType::BonesSelected);
		EditorMode->GetEditorBinding()->GetNotifier()->HandleNotification({}, ESkeletalMeshNotifyType::HierarchyChanged);
		EditorMode->GetEditorBinding()->GetNotifier()->HandleNotification(SelectedBones, ESkeletalMeshNotifyType::BonesSelected);

		// Rebind commands it was replaced before
		if (DefaultSkeletonWidget)
		{
			const TSharedRef<FUICommandList>& CommandList = EditorMode->GetEditor()->GetToolkitCommands();

			// Reset transforms
			CommandList->MapAction(EditorMode->GetEditor()->GetResetBoneTransformsCommand(), DefaultResetBoneTransformsAction);
			CommandList->MapAction(EditorMode->GetEditor()->GetResetAllBonesTransformsCommand(), DefaultResetAllBoneTransformsAction);
		}
	}	
}

void FSkeletalMeshModelingToolsEditorModeToolkit::RefreshMorphTargetManager()
{
	MorphTargetManager->RefreshList();
}

TSharedPtr<SWidget> FSkeletalMeshModelingToolsEditorModeToolkit::MakeFooterWidget()
{
	// LOD picker widget
	{
		AssetLODMode = SNew(STextComboBox)
			.OptionsSource(&AssetLODModes)
			.IsEnabled(this, &FSkeletalMeshModelingToolsEditorModeToolkit::CanChangeAssetEditingSettings)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> String, ESelectInfo::Type)
			{
				int32 Index = AssetLODModes.IndexOfByKey(String);
				check(AssetAvailableLODs.Num() == AssetLODModes.Num());
				EMeshLODIdentifier NewSelectedLOD = AssetAvailableLODs[Index];

				if (EditorMode.IsValid())
				{
					EditorMode->SetEditingLOD(NewSelectedLOD);
				}
			});

		AssetLODModeLabel = SNew(STextBlock)
		.Text(LOCTEXT("ActiveLODLabel", "Editing LOD"))
		.ToolTipText(LOCTEXT("ActiveLODLabelToolTip", "Select the LOD to be used when editing an existing mesh."));
	}

	// Apply Mode Picker Widget
	{
		FText ToolTip = LOCTEXT("ApplyModeToolTip", "Select when tool changes should be applied to the asset.\n"
								"When using \"Apply Manually\", you can switch between different tools directly without waiting for changes to be applied to the asset");
		ApplyModeLabel =
			SNew(STextBlock)
			.Text(LOCTEXT("ApplyModeLabel", "Apply Mode"))
			.ToolTipText(ToolTip);

		ApplyModeEnums.Add(USkeletalMeshModelingToolsEditorMode::EApplyMode::ApplyOnToolExit);
		ApplyModeStrings.Add(MakeShared<FString>(TEXT("Apply On Tool Exit")));
		
		ApplyModeEnums.Add(USkeletalMeshModelingToolsEditorMode::EApplyMode::ApplyManually);
		ApplyModeStrings.Add(MakeShared<FString>(TEXT("Apply Manually")));
			
		ApplyModeComboBox =
			SNew(STextComboBox)
			.ToolTipText(ToolTip)
			.OptionsSource(&ApplyModeStrings)
			.IsEnabled(this, &FSkeletalMeshModelingToolsEditorModeToolkit::CanChangeAssetEditingSettings)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> String, ESelectInfo::Type)
			{
				int32 Index = ApplyModeStrings.IndexOfByKey(String);
				USkeletalMeshModelingToolsEditorMode::EApplyMode ApplyMode = ApplyModeEnums[Index];

				if (EditorMode.IsValid())
				{
					EditorMode->SetApplyMode(ApplyMode);
				}
			});
	}
	

	
	const TSharedPtr<SVerticalBox> Content = SNew(SVerticalBox)
	+ SVerticalBox::Slot().HAlign(HAlign_Fill)
	.Padding(0, 8, 0, 4)
		[
		
			SNew(SVerticalBox)	
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().Padding(0).HAlign(HAlign_Left).VAlign(VAlign_Center).FillWidth(2.f)
					[ AssetLODModeLabel->AsShared() ]
				+ SHorizontalBox::Slot().Padding(0).HAlign(HAlign_Fill).Padding(0).FillWidth(4.f)
					[ AssetLODMode->AsShared() ]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().Padding(0).HAlign(HAlign_Left).VAlign(VAlign_Center).FillWidth(2.f)
					[ ApplyModeLabel->AsShared() ]
				+ SHorizontalBox::Slot().Padding(0).HAlign(HAlign_Fill).Padding(0).FillWidth(4.f)
					[ ApplyModeComboBox->AsShared() ]
			]
			
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.HeightOverride(50.f)
					.HAlign(HAlign_Fill)
					.Padding(0, 10, 5,10)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.ToolTipText(LOCTEXT("DiscardButtonToolTip", "Discard Pending Changes"))
						.ForegroundColor(FLinearColor::White)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.IsEnabled(this, &FSkeletalMeshModelingToolsEditorModeToolkit::IsDiscardButtonEnabled)
						.OnClicked(this, &FSkeletalMeshModelingToolsEditorModeToolkit::OnDiscardButtonPressed)
						[
							SNew( SImage )
							.Image(FAppStyle::GetBrush("Icons.Delete"))
							.ColorAndOpacity( FSlateColor::UseForeground() )
						]		
					]	
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SBox)
					.HeightOverride(50.f)
					.HAlign(HAlign_Fill)
					.Padding(5, 10, 0, 10)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), FName("FlatButton.Success"))
						.ForegroundColor(FLinearColor::White)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.IsEnabled(this, &FSkeletalMeshModelingToolsEditorModeToolkit::IsApplyButtonEnabled)
						.OnClicked(this, &FSkeletalMeshModelingToolsEditorModeToolkit::OnApplyButtonPressed)
						[
							SNew(STextBlock)
							.Text(this, &FSkeletalMeshModelingToolsEditorModeToolkit::GetApplyButtonText)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						]		
					]	
				]	
			
			]
		];

		AssetConfigPanel = SNew(SExpandableArea)
		.HeaderPadding(FMargin(0.f))
		.Padding(FMargin(8.f))
		.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
		.AreaTitleFont(FAppStyle::Get().GetFontStyle("EditorModesPanel.CategoryFontStyle"))
		.BodyContent()
		[
			Content->AsShared()
		]
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).FillWidth(2.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SkeletalMeshModelingToolFooterHeader", "Asset Editing Settings"))
			]	
		];
	
	return AssetConfigPanel;
}

bool FSkeletalMeshModelingToolsEditorModeToolkit::IsApplyButtonEnabled() const
{
	return CanChangeAssetEditingSettings() && EditorMode->HasUnappliedChanges();
}

FReply FSkeletalMeshModelingToolsEditorModeToolkit::OnApplyButtonPressed()
{
	FScopedTransaction Transaction(LOCTEXT("ApplyChangesToAsset","Apply Changes To Skeletal Mesh"));
	EditorMode->ApplyChanges();
	
	return FReply::Handled();
}

FText FSkeletalMeshModelingToolsEditorModeToolkit::GetApplyButtonText() const
{
	int32 ChangeCount = EditorMode->GetCurrentEditingCache()->GetEditingMeshComponent()->GetChangeCount();
	if (ChangeCount == 0)
	{
		return FText::Format(LOCTEXT("ApplyButtonZeroChangeText", "No Pending Changes"), FText::AsNumber(ChangeCount));
	}
	
	return FText::Format(LOCTEXT("ApplyButtonChangeText", "Apply {0} {0}|plural(one=Change, other=Changes)"), FText::AsNumber(ChangeCount));
}

bool FSkeletalMeshModelingToolsEditorModeToolkit::IsDiscardButtonEnabled() const
{
	return IsApplyButtonEnabled();
}

FReply FSkeletalMeshModelingToolsEditorModeToolkit::OnDiscardButtonPressed()
{
	FScopedTransaction Transaction(LOCTEXT("DiscardChanges","Discard Changes"));
	EditorMode->DiscardChanges();
	return FReply::Handled();
}

bool FSkeletalMeshModelingToolsEditorModeToolkit::CanChangeAssetEditingSettings() const
{
	return !GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->HasAnyActiveTool();
}

void FSkeletalMeshModelingToolsEditorModeToolkit::UpdateActiveToolProperties(UInteractiveTool* Tool)
{
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveTool(EToolSide::Left);
	if (CurTool == nullptr)
	{
		return;
	}
		
	ModeDetailsView->SetObjects(CurTool->GetToolProperties(true));
}

#undef LOCTEXT_NAMESPACE
