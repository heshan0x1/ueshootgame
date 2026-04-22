// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPVEditorViewport.h"

#include "AdvancedPreviewScene.h"
#include "AdvancedPreviewSceneMenus.h"
#include "PCGEditorSettings.h"
#include "PVEditorCommands.h"
#include "PVEditorSettings.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"

#include "Brushes/SlateColorBrush.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"

#include "DataTypes/PVData.h"

#include "Editor/EditorPerProjectUserSettings.h"

#include "Engine/StaticMesh.h"

#include "Nodes/PVBaseSettings.h"

#include "ViewportToolbar/UnrealEdViewportToolbar.h"

#include "Visualizations/PVScaleVisualizationComponent.h"

#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "PVEditorViewportToolbarSections"

SPVEditorViewport::SPVEditorViewport()
{
	PreviewNodeBackgroundBrush = MakeShared<FSlateColorBrush>(FColor{70, 100, 200});
	
	if (AdvancedPreviewScene)
	{
		if (!GetPreviewProfileController()->SetActiveProfile(UDefaultEditorProfiles::GreyAmbientProfileName.ToString()))
		{
			AdvancedPreviewScene->SetProfileIndex(GetDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex);
		}
		if (!AdvancedPreviewScene->IsPostProcessingEnabled())
		{
			AdvancedPreviewScene->HandleTogglePostProcessing();
		}
	}
}

SPVEditorViewport::~SPVEditorViewport()
{
	const FName ToolbarName = "PVE.ViewportToolbar";

	if (UToolMenus::Get()->IsMenuRegistered(ToolbarName))
	{
		UToolMenus::Get()->RemoveMenu(ToolbarName);
	}
}

void SPVEditorViewport::Construct(const FArguments& InArgs)
{
	SPCGEditorViewport::Construct(SPCGEditorViewport::FArguments());

	UPCGEditorSettings* PCGEditorSettings = GetMutableDefault<UPCGEditorSettings>();

	if (PCGEditorSettings)
	{
		PCGEditorSettings->bAutoFocusViewport = IsAutoFocusViewportChecked();
	}
}

TSharedPtr<SWidget> SPVEditorViewport::BuildViewportToolbar()
{
	check(AdvancedPreviewScene.IsValid());

	const FName ToolbarName = "PVE.ViewportToolbar";
	
	if (!UToolMenus::Get()->IsMenuRegistered(ToolbarName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(ToolbarName, /*Parent=*/NAME_None, EMultiBoxType::SlimHorizontalToolBar);
		Menu->StyleName = "ViewportToolbar";
		Menu->AddSection("Left");
		
		FToolMenuSection& RightSection = Menu->AddSection("Right");
		RightSection.Alignment = EToolMenuSectionAlign::Last;
		RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
		RightSection.AddEntry(UE::UnrealEd::CreateViewModesSubmenu());
		RightSection.AddEntry(UE::UnrealEd::CreateDefaultShowSubmenu());
		RightSection.AddEntry(UE::UnrealEd::CreateAssetViewerProfileSubmenu());
		UE::AdvancedPreviewScene::Menus::ExtendAdvancedPreviewSceneSettings(
			"PVE.ViewportToolbar.AssetViewerProfile",
			UE::AdvancedPreviewScene::Menus::FSettingsOptions().ShowToggleGrid(false)
		);

		RightSection.AddEntry(CreateVisualizationModeToolbarMenu());
		RightSection.AddEntry(CreateSettingsToolbarMenu());
	}

	FToolMenuContext Context;
	{
		Context.AppendCommandList(AdvancedPreviewScene->GetCommandList());
		Context.AppendCommandList(GetCommandList());
		Context.AddExtender(GetExtenders());
		Context.AddObject(UE::UnrealEd::CreateViewportToolbarDefaultContext(GetViewportWidget()));
		Context.AddObject(CreateMannequinWidgetContext(CreateMannequinOffsetWidget()));
	}
	
	return UToolMenus::Get()->GenerateWidget(ToolbarName, Context);
}

void SPVEditorViewport::BindCommands()
{
	SPCGEditorViewport::BindCommands();

	FPVEditorCommands& Commands = FPVEditorCommands::Get();

	CommandList->MapAction(
		Commands.ShowMannequin,
		FExecuteAction::CreateSP(this, &SPVEditorViewport::ToggleShowMannequin),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SPVEditorViewport::IsShowMannequinChecked));

	CommandList->MapAction(
		Commands.ShowScaleVisualization,
		FExecuteAction::CreateSP(this, &SPVEditorViewport::ToggleShowScaleVis),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SPVEditorViewport::IsShowScaleVisChecked));

	CommandList->MapAction(
		Commands.AutoFocusViewport,
		FExecuteAction::CreateSP(this, &SPVEditorViewport::ToggleAutofocusViewport),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SPVEditorViewport::IsAutoFocusViewportChecked));
}

TSharedRef<FEditorViewportClient> SPVEditorViewport::MakeEditorViewportClient()
{
	TSharedRef<FEditorViewportClient> ViewportClient = SPCGEditorViewport::MakeEditorViewportClient();
	ViewportClient->ExposureSettings.bFixed = true;
	ViewportClient->ExposureSettings.FixedEV100 = -0.2f;
	return ViewportClient;
}

void SPVEditorViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
	SPCGEditorViewport::PopulateViewportOverlays(Overlay);

	Overlay
		->AddSlot()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(PreviewNodeBackgroundBrush.Get())
			.IsEnabled_Lambda([this]
				{
					return bIsPreviewingLockedNode;
				})
			.Visibility_Lambda([this]
				{
					return bIsPreviewingLockedNode
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
			[
				SAssignNew(OverlayText, SRichTextBlock)
				.Justification(ETextJustify::Center)
			]
		];
	
	Overlay
		->AddSlot()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(6.0f, 25.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("FloatingBorder"))
			.Visibility_Lambda([this]
				{
					return StatsOverlayText && !StatsOverlayText->GetText().IsEmpty()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
			.Padding(4.f)
			[
				SAssignNew(StatsOverlayText, SRichTextBlock)
			]
		];
}

void SPVEditorViewport::InitVisualizationScene()
{
	const FString MannequinPath = TEXT("/ProceduralVegetationEditor/Mannequin/Viewport_Mannequin_T_Pose.Viewport_Mannequin_T_Pose");
	if (UStaticMesh* MannequinMesh = LoadObject<UStaticMesh>(nullptr, MannequinPath))
	{
		MannequinComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		MannequinComponent->SetStaticMesh(MannequinMesh);
		const FBoxSphereBounds MannequinBounds = MannequinComponent->CalcLocalBounds();

		ManagedResources.Add(MannequinComponent);
		AdvancedPreviewScene->AddComponent(MannequinComponent, FTransform::Identity);
		SetMannequinOffset((-FocusBounds.BoxExtent.X - MannequinBounds.BoxExtent.X) * 1.5f, true);
	}

	ScaleVisualizationComponent = NewObject<UPVScaleVisualizationComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	ScaleVisualizationComponent->SetScaleBounds(FocusBounds);

	ManagedResources.Add(ScaleVisualizationComponent);
	AdvancedPreviewScene->AddComponent(ScaleVisualizationComponent, FTransform::Identity);
	for (const TObjectPtr<UTextRenderComponent>& TextRenderComponent : ScaleVisualizationComponent->GetTextRenderComponents())
	{
		ManagedResources.Add(TextRenderComponent);
		AdvancedPreviewScene->AddComponent(TextRenderComponent, FTransform::Identity);
	}
}

void SPVEditorViewport::ResetVisualizationScene()
{
	if (MannequinComponent)
	{
		AdvancedPreviewScene->RemoveComponent(MannequinComponent);
		ManagedResources.Remove(MannequinComponent);
		MannequinComponent->MarkAsGarbage();
		MannequinComponent = nullptr;
	}

	if (ScaleVisualizationComponent)
	{
		for (const TObjectPtr<UTextRenderComponent>& TextRenderComponent : ScaleVisualizationComponent->GetTextRenderComponents())
		{
			if (TextRenderComponent)
			{
				AdvancedPreviewScene->RemoveComponent(TextRenderComponent);
				ManagedResources.Remove(TextRenderComponent);
			}
		}
		AdvancedPreviewScene->RemoveComponent(ScaleVisualizationComponent);
		ManagedResources.Remove(ScaleVisualizationComponent);
		ScaleVisualizationComponent->MarkAsGarbage();
		ScaleVisualizationComponent = nullptr;
	}
}

TSharedRef<SWidget> SPVEditorViewport::CreateMannequinOffsetWidget() const
{
	return SNew(SNumericEntryBox<float>)
		.AllowSpin(true)
		.AllowWheel(true)
		.Delta(5.0f)
		.MinValue(-2000.0f)
		.MaxValue(2000.0f)
		.MinSliderValue(-2000.0f)
		.MaxSliderValue(2000.0f)
		.Value_Lambda([this]{ return GetMannequinOffset(); })
		.OnValueChanged_Lambda([this](const float NewValue)
			{
				SetMannequinOffset(NewValue, false);
			})
		.OnValueCommitted_Lambda([this](const float NewValue, ETextCommit::Type)
			{
				SetMannequinOffset(NewValue, true);
			});
}

void SPVEditorViewport::ToggleShowMannequin()
{
	UPVEditorSettings* EditorSettings = GetMutableDefault<UPVEditorSettings>();
	EditorSettings->bShowMannequin = !EditorSettings->bShowMannequin;
	EditorSettings->SaveConfig();

	SetMannequinState(EditorSettings->bShowMannequin);
}

bool SPVEditorViewport::IsShowMannequinChecked() const
{
	return GetDefault<UPVEditorSettings>()->bShowMannequin;
}

void SPVEditorViewport::SetMannequinState(bool InEnable)
{
	if (MannequinComponent)
	{
		MannequinComponent->SetVisibility(InEnable, true);
		Client->Invalidate();
	}
}

void SPVEditorViewport::ToggleShowScaleVis()
{
	UPVEditorSettings* EditorSettings = GetMutableDefault<UPVEditorSettings>();
	EditorSettings->bShowScaleVisualization = !EditorSettings->bShowScaleVisualization;
	EditorSettings->SaveConfig();
	
	SetScaleVisState(EditorSettings->bShowScaleVisualization);
}

bool SPVEditorViewport::IsShowScaleVisChecked() const
{
	return GetMutableDefault<UPVEditorSettings>()->bShowScaleVisualization;
}

void SPVEditorViewport::SetScaleVisState(bool InEnable)
{
	if (ScaleVisualizationComponent)
	{
		ScaleVisualizationComponent->SetVisibility(InEnable, true);
		Client->Invalidate();
	}
}

void SPVEditorViewport::ToggleAutofocusViewport()
{
	UPVEditorSettings* EditorSettings = GetMutableDefault<UPVEditorSettings>();
	EditorSettings->bAutoFocusViewport = !EditorSettings->bAutoFocusViewport;
	EditorSettings->SaveConfig();

	UPCGEditorSettings* PCGEditorSettings = GetMutableDefault<UPCGEditorSettings>();
	PCGEditorSettings->bAutoFocusViewport = IsAutoFocusViewportChecked();
}

bool SPVEditorViewport::IsAutoFocusViewportChecked() const
{
	return GetDefault<UPVEditorSettings>()->bAutoFocusViewport;
}

float SPVEditorViewport::GetMannequinOffset() const
{
	return GetDefault<UPVEditorSettings>()->MannequinOffset;
}

void SPVEditorViewport::SetMannequinOffset(const float NewValue, const bool bSaveConfig) const
{
	UPVEditorSettings* const EditorSettings = GetMutableDefault<UPVEditorSettings>();
	EditorSettings->MannequinOffset = NewValue;
	if (bSaveConfig)
	{
		EditorSettings->SaveConfig();
	}

	if (MannequinComponent)
	{
		MannequinComponent->SetRelativeLocation(FVector(EditorSettings->MannequinOffset, 0.0, 0.0));
		Client->Invalidate();
	}
}

UPVMannequinWidgetContext* SPVEditorViewport::CreateMannequinWidgetContext(TSharedPtr<SWidget> InOffsetWidget)
{
	UPVMannequinWidgetContext* Context = NewObject<UPVMannequinWidgetContext>();
	Context->MannequinOffsetWidget = InOffsetWidget;
	return Context;
}

void SPVEditorViewport::OnSetupScene()
{
	SPCGEditorViewport::OnSetupScene();

	InitVisualizationScene();
	
	SetMannequinState(IsShowMannequinChecked());
	SetScaleVisState(IsShowScaleVisChecked());

	if (bFocusOnNextUpdate)
	{
		OnFocusViewportToSelection();

		bFocusOnNextUpdate = false;
	}
}

void SPVEditorViewport::OnResetScene()
{
	SPCGEditorViewport::OnResetScene();

	ResetVisualizationScene();
}

FToolMenuEntry SPVEditorViewport::CreateSettingsToolbarMenu()
{
	return FToolMenuEntry::InitDynamicEntry(TEXT("PVE Settings"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
		{
			FToolMenuEntry& Entry = Section.AddSubMenu(
				/*Name=*/TEXT("PVESettingsSubmenu"),
				FText::GetEmpty(),
				FText::GetEmpty(),
				FNewToolMenuDelegate::CreateLambda([](UToolMenu* Submenu)
					{
						const UPVMannequinWidgetContext* Context = Submenu->FindContext<UPVMannequinWidgetContext>();
						
						// Add preference toggles to this section.
						FToolMenuSection& MannequinSection = Submenu->FindOrAddSection(TEXT("PVE_MannequinSettings"),
							LOCTEXT("PVE_MannequinSettingsSectionLabel", "Mannequin Settings"));
						MannequinSection.AddMenuEntry(FPVEditorCommands::Get().ShowMannequin);
						MannequinSection.AddEntry(
							FToolMenuEntry::InitWidget(
								"PVE_MannequinOffset",
								Context->MannequinOffsetWidget.ToSharedRef(),
								LOCTEXT("PVE_MannequinOffsetLabel", "Mannequin Offset"),
								false,
								true,
								false,
								LOCTEXT("PVE_MannequinOffsetTooltip", "Manually Offset the Mannequin")
							)
						);
						FToolMenuSection& ScaleVisSection = Submenu->FindOrAddSection(TEXT("PVE_ScaleVisSettings"),
							LOCTEXT("PVE_ScaleVisSettingsSectionLabel", "Scale Vis. Settings"));
						ScaleVisSection.AddMenuEntry(FPVEditorCommands::Get().ShowScaleVisualization);

						FToolMenuSection& AutoFocusViewportSection = Submenu->FindOrAddSection(TEXT("PVE_InteractionSettings"),
							LOCTEXT("PVE_InteractionSettingsSectionLabel", "Interaction Settings"));
						AutoFocusViewportSection.AddMenuEntry(FPVEditorCommands::Get().AutoFocusViewport);
					}),
				/*bInOpenSubMenuOnClick=*/false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));
		}));
}

EPVVisualizationMode PVRenderModesToVisualizationMode(const TArray<EPVRenderType>& InRenderType)
{
	if (InRenderType.Contains(EPVRenderType::Foliage) && InRenderType.Contains(EPVRenderType::Mesh))
	{
		return EPVVisualizationMode::FoliageMesh;
	}
	else if (InRenderType.Contains(EPVRenderType::FoliageGrid))
	{
		return EPVVisualizationMode::FoliageGrid;
	}
	else if (InRenderType.Contains(EPVRenderType::Mesh))
	{
		return EPVVisualizationMode::Mesh;
	}
	else if (InRenderType.Contains(EPVRenderType::Bones))
	{
		return EPVVisualizationMode::Bones;
	}
	else if (InRenderType.Contains(EPVRenderType::PointData))
	{
		return EPVVisualizationMode::PointData;
	}

	return EPVVisualizationMode::PointData;
}

TArray<EPVRenderType> VisualizationModeToRenderModes(EPVVisualizationMode InMode, TArray<EPVRenderType> DefautlSettings = TArray<EPVRenderType>{})
{
	switch (InMode)
	{
	case EPVVisualizationMode::Default:
		return DefautlSettings;
		
	case EPVVisualizationMode::FoliageMesh:
		return {EPVRenderType::Mesh, EPVRenderType::Foliage};			

	case EPVVisualizationMode::FoliageGrid:
		return {EPVRenderType::FoliageGrid};

	case EPVVisualizationMode::Bones:
		return {EPVRenderType::Bones};
	
	case EPVVisualizationMode::Mesh:
		return {EPVRenderType::Mesh};

	case EPVVisualizationMode::PointData:
		return {EPVRenderType::PointData};

	case EPVVisualizationMode::BonesMesh:
		return {EPVRenderType::Bones, EPVRenderType::Mesh};

	case EPVVisualizationMode::PointDataMesh:
		return {EPVRenderType::PointData, EPVRenderType::Mesh};

	default:
		return TArray<EPVRenderType>{};
		
	}
}

FToolMenuEntry SPVEditorViewport::CreateVisualizationModeToolbarMenu()
{
	return FToolMenuEntry::InitDynamicEntry
	(
	    "VisualizationMode",
	    FNewToolMenuSectionDelegate::CreateSPLambda(this,[this](FToolMenuSection& InnerSection) -> void
	       {
	       	TAttribute<FText> LabelAttribute = UE::UnrealEd::GetViewModesSubmenuLabel(nullptr);

	       	LabelAttribute = TAttribute<FText>::CreateSPLambda(this,
					[this]()
					{
						UEnum* EnumPtr = StaticEnum<EPVVisualizationMode>();
							return EnumPtr->GetDisplayNameTextByIndex((int)CurrentVisualizationMode);
					}
				);
	       		
	          FToolMenuEntry& Entry = InnerSection.AddSubMenu(
	             "VisualizationModes",
	             LabelAttribute,
	             FText::GetEmpty(),
	             FNewToolMenuDelegate::CreateSPLambda
	             (this, [this](UToolMenu* Submenu) -> void
	                   {
	                      FToolMenuSection& RenderModeSelectionSection = Submenu->FindOrAddSection(
	                         "VisualizationModeSection",
	                         LOCTEXT("FoliageVisualizationModeSelectionSectionLabel", "Visualization modes")
	                      );

	                      UEnum* EnumPtr = StaticEnum<EPVVisualizationMode>();
	                      
	                      if (EnumPtr)
	                      {
	                         for (int32 i = 0; i < EnumPtr->NumEnums() - 1; i++) // last entry is _MAX or hidden
	                         {
	                            FText ModeName = EnumPtr->GetDisplayNameTextByIndex(i);
	                            EPVVisualizationMode Mode = static_cast<EPVVisualizationMode>(EnumPtr->GetValueByIndex(i));
	                         	
	                         	RenderModeSelectionSection.AddMenuEntry
	                         	(
								NAME_None,
								ModeName,
								FText(),
								FSlateIcon(),
								FUIAction(
								   FExecuteAction::CreateSPLambda(this,
									  [this, Mode]()
									  {
									  		OnVisualizationModeChanged(Mode);
									  }
								   ),
								   FCanExecuteAction::CreateSPLambda(this,[this, Mode]()
									   {
								   		if (Mode != EPVVisualizationMode::Default)
								   		{
								   			auto RenderTypes = VisualizationModeToRenderModes(Mode);

											   for (int i = 0; i < RenderTypes.Num(); i++)
											   {
												   if (!SupportedRenderTypes.Contains(RenderTypes[i]))
												   {
													   return false;
												   }
											   }
								   		}
								   		
								   		return true;
								   		
									   }),
								   FIsActionChecked::CreateSPLambda(this,
									  [this, Mode]()
									  {
									  		return CurrentVisualizationMode == Mode;
									  }
								   )
								),EUserInterfaceActionType::RadioButton
								);
	                         }
	                      }
						}
	             ),
	             false,
	             FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Visualizers")
	          );
	          Entry.ToolBarData.ResizeParams.ClippingPriority = 1100;
	       }
	    )
	 );
}

void SPVEditorViewport::OnVisualizationModeChanged(EPVVisualizationMode InMode)
{
	CurrentVisualizationMode = InMode;

	if (InspectedNodeSettings)
	{
		const TArray<EPVRenderType> RenderTypes = VisualizationModeToRenderModes(CurrentVisualizationMode, InspectedNodeSettings->GetDefaultRenderType());
												
		InspectedNodeSettings->SetCurrentRenderType(RenderTypes);

		Invalidate();

		if (IsAutoFocusViewportChecked())
		{
			bFocusOnNextUpdate = true;
		}
	}
}

void SPVEditorViewport::OnNodeInspectionChanged(UPVBaseSettings* InSettings)
{
	SupportedRenderTypes = InSettings->GetSupportedRenderTypes();
	CurrentVisualizationMode = EPVVisualizationMode::Default;
	InspectedNodeSettings = InSettings;
	
	InspectedNodeSettings->SetCurrentRenderType(InSettings->GetDefaultRenderType());

	// TODO: @Tayyab add lock visualization based on the inspection lock state
	// InSettings->bLockInspection

	if (IsAutoFocusViewportChecked())
	{
		bFocusOnNextUpdate = true;
	}
}

void SPVEditorViewport::SetOverlayText(const FText& CurrentlyLockedNodeName)
{
	bIsPreviewingLockedNode = !CurrentlyLockedNodeName.IsEmpty();
	static FText NormalTextStyle = FText::FromString(TEXT("<TextBlock.ShadowedText>Previewing Output of Node: {0}</>"));
	FTextBuilder FinalText;
	FinalText.AppendLineFormat(NormalTextStyle, CurrentlyLockedNodeName);
	OverlayText->SetText(FinalText.ToText());
}

void SPVEditorViewport::PopulateStatsOverlayText(const TArrayView<FText> TextItems)
{
	static FText NormalTextStyle = FText::FromString(TEXT("<TextBlock.ShadowedText>{0}</>"));
	
	FTextBuilder FinalText;
	for (const FText& TextItem : TextItems)
	{
		FinalText.AppendLineFormat(NormalTextStyle, TextItem);
	}
	
	StatsOverlayText->SetText(FinalText.ToText());
}

#undef LOCTEXT_NAMESPACE
