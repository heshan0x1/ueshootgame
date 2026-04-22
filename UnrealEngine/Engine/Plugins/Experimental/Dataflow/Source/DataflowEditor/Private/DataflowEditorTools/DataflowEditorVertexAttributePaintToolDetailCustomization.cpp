// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowEditorTools/DataflowEditorVertexAttributePaintToolDetailCustomization.h"

#include "DataflowEditorTools/DataflowEditorToolEnums.h"
#include "DataflowEditorTools/DataflowEditorVertexAttributePaintTool.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ModelingToolsEditorModeStyle.h"
#include "SColorGradientEditor.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Drawing/MeshElementsVisualizer.h"

#define LOCTEXT_NAMESPACE "VertexAttributePaintToolDetailCustomization"

namespace UE::Dataflow::Editor
{
	// layout constants
	float FVertexAttributePaintToolDetailCustomization::WeightSliderWidths = 150.0f;
	float FVertexAttributePaintToolDetailCustomization::WeightEditingLabelsPercent = 0.40f;
	float FVertexAttributePaintToolDetailCustomization::WeightEditVerticalPadding = 4.0f;
	float FVertexAttributePaintToolDetailCustomization::WeightEditHorizontalPadding = 2.0f;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedRef<IDetailCustomization> FVertexAttributePaintToolDetailCustomization::MakeInstance()
	{
		return MakeShareable(new FVertexAttributePaintToolDetailCustomization);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	FVertexAttributePaintToolDetailCustomization::~FVertexAttributePaintToolDetailCustomization()
	{
		if (Tool.IsValid())
		{
			// TODO(ccaillaud) ==>>  Tool->OnSelectionChanged.RemoveAll(this);
		}

		Tool.Reset();
		ToolProperties.Reset();
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::OnSelectionChanged()
	{
		// TODO(ccaillaud) ==>>  ToolProperties.Get()->DirectEditState.Reset();
	};

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddEditingModeRow(IDetailCategoryBuilder& EditModeCategory) const
	{
		// Add segmented control toggle for editing modes ("Brush" or "Selection")
		EditModeCategory.AddCustomRow(LOCTEXT("EditModeCategory", "Value Editing Mode"), false)
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(2.0f)
			[
				SNew(SSegmentedControl<EDataflowEditorToolEditMode>)
				.ToolTipText(LOCTEXT("EditingModeTooltip",
					"Brush: edit values by painting on mesh.\n"
					"Mesh: select vertices/edges/faces to edit values directly.\n"
				))
				.Value_Lambda([this]()
					{
						return ToolProperties.IsValid() ? ToolProperties->EditingMode : EDataflowEditorToolEditMode::Brush;
					})
				.OnValueChanged_Lambda([this](EDataflowEditorToolEditMode Mode)
					{
						if (ToolProperties.IsValid())
						{
							ToolProperties->EditingMode = Mode;
							// TODO(ccaillaud) ==>> ToolProperties->OwnerTool->ToggleEditingMode();
							if (CurrentDetailBuilder)
							{
								CurrentDetailBuilder->ForceRefreshDetails();
							}
						}
					})
				+ SSegmentedControl<EDataflowEditorToolEditMode>::Slot(EDataflowEditorToolEditMode::Brush)
				.Text(LOCTEXT("BrushEditMode", "Brush"))
				+ SSegmentedControl<EDataflowEditorToolEditMode>::Slot(EDataflowEditorToolEditMode::Mesh)
				.Text(LOCTEXT("MeshEditMode", "Mesh"))
			]
		];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddColorModeRow(IDetailCategoryBuilder& MeshDisplayCategory) const
	{
		MeshDisplayCategory.AddCustomRow(LOCTEXT("ColorModeCategory", "Color Mode"), false)
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(2.0f)
			.HAlign(HAlign_Center)
			[
				SNew(SSegmentedControl<EDataflowEditorToolColorMode>)
				.ToolTipText(LOCTEXT("WeightColorTooltip",
					"Adjust the value display in the viewport.\n\n"
					"Greyscale: Displays values by blending from black (0) to white (1).\n"
					"Ramp: Values 0 and 1 use the min and max colors. values in between 0 and 1 use the ramp colors.\n"
					"Full Material: Displays normal mesh materials with textures.\n"))
				.Value_Lambda([this]()
					{
						return ToolProperties.IsValid() ? ToolProperties->DisplayProperties.ColorMode : EDataflowEditorToolColorMode::Greyscale;
					})
				.OnValueChanged_Lambda([this](EDataflowEditorToolColorMode Mode)
					{
						if (Tool.IsValid())
						{
							Tool->SetColorMode(Mode);
						}
					})
				+ SSegmentedControl<EDataflowEditorToolColorMode>::Slot(EDataflowEditorToolColorMode::Greyscale)
				.Text(LOCTEXT("GreyscaleMode", "Greyscale"))
				+ SSegmentedControl<EDataflowEditorToolColorMode>::Slot(EDataflowEditorToolColorMode::Ramp)
				.Text(LOCTEXT("RampMode", "Ramp"))
				+ SSegmentedControl<EDataflowEditorToolColorMode>::Slot(EDataflowEditorToolColorMode::FullMaterial)
				.Text(LOCTEXT("MaterialMode", "Full Material"))
			]
		];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddColorRampRow(IDetailCategoryBuilder& EditModeCategory) const
	{
		auto MakeColorRampWidget =
			[](
				EDataflowEditorToolColorMode ColorMode, 
				FCurveOwnerInterface* CurveOwner,
				TWeakObjectPtr<UDataflowEditorVertexAttributePaintToolProperties> ToolProperties
				)
			{
				TSharedRef<SColorGradientEditor> GradientEditor = SNew(SColorGradientEditor)
					.ViewMinInput(0.0f)
					.ViewMaxInput(1.0f)
					.ClampStopsToViewRange(true)
					.DrawColorAndAlphaSeparate(false)
					.IsEditingEnabled_Lambda([ToolProperties, ColorMode]()
						{
							// Only the ramp one is editable 
							return ToolProperties.IsValid() && (ToolProperties->DisplayProperties.ColorMode == EDataflowEditorToolColorMode::Ramp);
						})
					.Visibility_Lambda([ToolProperties, ColorMode]()
						{
							return (ToolProperties.IsValid() && (ToolProperties->DisplayProperties.ColorMode == ColorMode))
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
					;
				if (ToolProperties.IsValid())
				{
					GradientEditor->SetCurveOwner(CurveOwner);
				}
				return GradientEditor;
			};

		if (ToolProperties.IsValid())
		{
			TSharedRef<SColorGradientEditor> GreyScaleRampWidget =
				MakeColorRampWidget(EDataflowEditorToolColorMode::Greyscale, &ToolProperties->DisplayProperties.GreyScaleColorRamp, ToolProperties);

			TSharedRef<SColorGradientEditor> CustomRampWidget =
				MakeColorRampWidget(EDataflowEditorToolColorMode::Ramp, &ToolProperties->DisplayProperties.ColorRamp.ColorRamp, ToolProperties);

			TSharedRef<SColorGradientEditor> MaterialRampWidget =
				MakeColorRampWidget(EDataflowEditorToolColorMode::FullMaterial, &ToolProperties->DisplayProperties.WhiteColorRamp, ToolProperties);

			EditModeCategory.AddCustomRow(LOCTEXT("ColorRampCategory", "Color Ramp"), false)
			.WholeRowContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					GreyScaleRampWidget
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					CustomRampWidget
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MaterialRampWidget
				]
			];
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		CurrentDetailBuilder = &DetailBuilder;
	
		TArray<TWeakObjectPtr<UObject>> DetailObjects;
		DetailBuilder.GetObjectsBeingCustomized(DetailObjects);

		// Should be impossible to get multiple settings objects for a single tool
		ensure(DetailObjects.Num()==1);
		ToolProperties = Cast<UDataflowEditorVertexAttributePaintToolProperties>(DetailObjects[0]);
		Tool = Cast<UDataflowEditorVertexAttributePaintTool>(ToolProperties->GetOuter());
		// TODO(ccaillaud) ==>> Tool->OnSelectionChanged.AddSP(this, &FVertexAttributePaintToolDetailCustomization::OnSelectionChanged);
	
		// Editing Mode category 
		IDetailCategoryBuilder& EditModeCategory = DetailBuilder.EditCategory("Value Editing Mode", FText::GetEmpty(), ECategoryPriority::Important);
		{
			// Editing mode row [ Brush | Mesh ]
			AddEditingModeRow(EditModeCategory);

			switch (ToolProperties->EditingMode)
			{
			case EDataflowEditorToolEditMode::Brush:
				AddBrushUI(DetailBuilder);
				break;
			case EDataflowEditorToolEditMode::Mesh:
				AddSelectionUI(DetailBuilder);
				break;
			}
		}

		// Mesh Display category
		IDetailCategoryBuilder& MeshDisplayCategory = DetailBuilder.EditCategory("MeshDisplay", FText::GetEmpty(), ECategoryPriority::Important);
		MeshDisplayCategory.InitiallyCollapsed(false);
		{
			AddColorModeRow(MeshDisplayCategory);

			AddColorRampRow(MeshDisplayCategory);
		}

		// Hide all customized properties 
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(UDataflowEditorVertexAttributePaintToolProperties, EditingMode));
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushMode));
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushSize));
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushAreaMode));
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, ValueAtBrush));
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, AttributeValue));
		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolDisplayProperties, ColorMode));

		HideProperty(DetailBuilder, GET_MEMBER_NAME_CHECKED(UMeshElementsVisualizerProperties, bVisible));
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddBrushModeRow(IDetailCategoryBuilder& BrushCategory) const
	{
		// add segmented control toggle for brush behavior modes ("Add", "Replace", etc..)
		BrushCategory.AddCustomRow(LOCTEXT("BrushModeCategory", "Brush Mode"), false)
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(2.0f)
			[
				SNew(SSegmentedControl<EDataflowEditorToolEditOperation>)
				.ToolTipText(LOCTEXT("BrushTooltip",
					"Select the operation to apply when using the brush.\n"
					"Add: applies the current value plus the Strength value to the new value.\n"
					"Replace: applies the current value minus the Strength value to the new value.\n"
					"Multiply: applies the current value multiplied by the Strength value to the new value.\n"
					"Relax: applies the average of the connected (by edge) vertex value to the new vertex value, blended by the Strength."))
				.Value_Lambda([this]()
					{
						return ToolProperties.IsValid() ? ToolProperties->BrushProperties.BrushMode : EDataflowEditorToolEditOperation::Add;
					})
				.OnValueChanged_Lambda([this](EDataflowEditorToolEditOperation Mode)
					{
						if (Tool.IsValid())
						{
							Tool->SetBrushMode(Mode);
						}
					})
				+ SSegmentedControl<EDataflowEditorToolEditOperation>::Slot(EDataflowEditorToolEditOperation::Add)
				.Text(LOCTEXT("BrushAddMode", "Add"))
				+ SSegmentedControl<EDataflowEditorToolEditOperation>::Slot(EDataflowEditorToolEditOperation::Replace)
				.Text(LOCTEXT("BrushReplaceMode", "Replace"))
				+ SSegmentedControl<EDataflowEditorToolEditOperation>::Slot(EDataflowEditorToolEditOperation::Multiply)
				.Text(LOCTEXT("BrushMultiplyMode", "Multiply"))
				+ SSegmentedControl<EDataflowEditorToolEditOperation>::Slot(EDataflowEditorToolEditOperation::Relax)
				.Text(LOCTEXT("BrushRelaxMode", "Relax"))
			]
		];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddBrushFalloffModeRow(IDetailCategoryBuilder& BrushCategory) const
	{
		// add segmented control toggle for brush falloff modes ("Surface" or "Volume")
		BrushCategory.AddCustomRow(LOCTEXT("BrushFalloffModeCategory", "Brush Falloff Mode"), false)
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(2.0f)
			[
				SNew(SSegmentedControl<EMeshVertexPaintBrushAreaType>)
				.ToolTipText(LOCTEXT("BrushFalloffModeTooltip",
					"Surface: falloff is based on the distance along the surface from the brush center to nearby connected vertices.\n"
					"Volume: falloff is based on the straight-line distance from the brush center to surrounding vertices.\n"))
				.Value_Lambda([this]()
					{
						return ToolProperties.IsValid() ? ToolProperties->BrushProperties.BrushAreaMode : EMeshVertexPaintBrushAreaType::Connected;
					})
				.OnValueChanged_Lambda([this](EMeshVertexPaintBrushAreaType Mode)
					{
						if (ToolProperties.IsValid())
						{
							ToolProperties->BrushProperties.BrushAreaMode = Mode; // TODO(ccaillaud) ==>> 
							// TODO(ccaillaud) ==>> ToolProperties->SetFalloffMode(Mode);
						}
					})
				+ SSegmentedControl<EMeshVertexPaintBrushAreaType>::Slot(EMeshVertexPaintBrushAreaType::Connected)
				.Text(LOCTEXT("SurfaceMode", "Surface"))
				+ SSegmentedControl<EMeshVertexPaintBrushAreaType>::Slot(EMeshVertexPaintBrushAreaType::Volumetric)
				.Text(LOCTEXT("VolumeMode", "Volume"))
			]
		];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddBrushRadiusRow(IDetailCategoryBuilder& BrushCategory) const
	{
		constexpr float MinBrushRadius = 0.01f;
		constexpr float MaxBrushRadius = 20.f;
		constexpr float DefaultBrushRadius = 10.0f;

		BrushCategory.AddCustomRow(LOCTEXT("BrushSizeCategory", "Brush Radius"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BrushRadiusLabel", "Radius"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.ToolTipText(LOCTEXT("BrushRadiusTooltip", "The radius of the brush in scene units."))
		]
		.ValueContent()
		[
			SNew(SSpinBox<float>)
			.MinValue_Lambda([this]() 
				{ 
					return Tool.IsValid() ? (Tool->GetBrushMinRadius()) : MinBrushRadius;
				})
			.MaxSliderValue_Lambda([this]()
				{
					return Tool.IsValid() ? (Tool->GetBrushMaxRadius()) : MaxBrushRadius;
				})
			.SupportDynamicSliderMaxValue(true)
			.Value_Lambda([this]()
				{
					// BrushSize is adaptive ( [0,1] range ) , we need to scale it to the max value 
					const float MaxRadius = Tool.IsValid() ? (Tool->GetBrushMaxRadius()) : MaxBrushRadius;
					return (ToolProperties.IsValid()) ? (ToolProperties->BrushProperties.BrushSize * MaxRadius) : MaxRadius;
				})
			.OnValueChanged_Lambda([this](float NewValue)
				{
					if (ToolProperties.IsValid())
					{
						// BrushSize is adaptive ( [0,1] range ), we need to divide by max value to set it 
						const float MaxRadius = FMath::Max((Tool.IsValid() ? (Tool->GetBrushMaxRadius()) : MaxBrushRadius), MinBrushRadius);
						ToolProperties->BrushProperties.BrushSize = (NewValue / MaxRadius);
						FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushSize)));
						ToolProperties->PostEditChangeProperty(PropertyChangedEvent);
					}
				})
			.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					if (ToolProperties.IsValid())
					{
						// TODO(ccaillaud) ==>> ToolProperties->SaveConfig();
					}
				})
		];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddBrushValueRow(IDetailCategoryBuilder& BrushCategory) const
	{
		BrushCategory.AddCustomRow(LOCTEXT("ValueCategory", "Value"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ValueLabel", "Value"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.ToolTipText(LOCTEXT("ValueTooltip", "The value of the attribute to be applied. Exact effect depends on the selected brush mode."))
		]
		.ValueContent()
		[
			SNew(SSpinBox<float>)
			.MinValue(0.f)
			.MaxValue(1.0f)
			.MaxSliderValue(1.f)
			.Value(1.0f)
			.SupportDynamicSliderMaxValue(true)
			.Value_Lambda([this]()
				{
					return ToolProperties.IsValid() ? ToolProperties->BrushProperties.AttributeValue : 1.f;
				})
			.OnValueChanged_Lambda([this](float NewValue)
				{
					if (ToolProperties.IsValid())
					{
						ToolProperties->BrushProperties.AttributeValue = FMath::Clamp(NewValue, 0.f, 1.f);
						FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, AttributeValue)));
						ToolProperties->PostEditChangeProperty(PropertyChangedEvent);
					}
				})
			.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					if (ToolProperties.IsValid())
					{
						// TODO(ccaillaud) ==>> ToolProperties->SaveConfig();
					}
				})
		];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddBrushUI(IDetailLayoutBuilder& DetailBuilder) const
	{
		IDetailCategoryBuilder& BrushCategory = DetailBuilder.EditCategory("Brush", FText::GetEmpty(), ECategoryPriority::Important);
		{
			// Brush modes [ Add | Replace | Multiply | Relax ]
			AddBrushModeRow(BrushCategory);

			// Brush Falloff modes [ Surface | Volume ]
			AddBrushFalloffModeRow(BrushCategory);

			// Brush radius field
			AddBrushRadiusRow(BrushCategory);

			// Brush value field
			AddBrushValueRow(BrushCategory);
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedRef<SWidget> FVertexAttributePaintToolDetailCustomization::MakeSelectionElementsToolbar() const
	{
		FSlimHorizontalToolBarBuilder ToolbarBuilder(MakeShared<FUICommandList>(), FMultiBoxCustomization::None);
		ToolbarBuilder.SetStyle(FModelingToolsEditorModeStyle::Get().Get(), "PolyEd.SelectionToolbar");
		ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

		ToolbarBuilder.BeginSection("SelectionFilter");
		ToolbarBuilder.BeginBlockGroup();

		auto AddToggleButtonForBool = [&ToolbarBuilder, this](EComponentSelectionMode Mode, const FText& Label, const FText& Tooltip, const FName IconName)
			{
				ToolbarBuilder.AddToolBarButton(FUIAction(
					FExecuteAction::CreateLambda([this, Mode]()
						{
							if (Tool.IsValid())
							{
								Tool->SetComponentSelectionMode(Mode);
							}
						}),
					FCanExecuteAction::CreateLambda([this]()
						{
							return ToolProperties.IsValid() ? ToolProperties->EditingMode == EDataflowEditorToolEditMode::Mesh : false;
						}),
					FIsActionChecked::CreateLambda([this, Mode]()
						{
							return ToolProperties.IsValid() ? (ToolProperties->SelectionProperties.ComponentSelectionMode == Mode) : false;
						})),
					NAME_None,	// Extension hook
					Label,		// Label
					Tooltip,	// Tooltip
					FSlateIcon(FModelingToolsEditorModeStyle::Get()->GetStyleSetName(), IconName),
					EUserInterfaceActionType::ToggleButton);
			};

		AddToggleButtonForBool(
			EComponentSelectionMode::Vertices,
			LOCTEXT("VerticesLabel", "Vertices"),
			LOCTEXT("VerticesTooltip", "Select mesh vertices."),
			"PolyEd.SelectCorners");
		AddToggleButtonForBool(
			EComponentSelectionMode::Edges,
			LOCTEXT("EdgesLabel", "Edges"),
			LOCTEXT("EdgesTooltip", "Select mesh edges."),
			"PolyEd.SelectEdges");
		AddToggleButtonForBool(
			EComponentSelectionMode::Faces,
			LOCTEXT("FacesLabel", "Faces"),
			LOCTEXT("FacesTooltip", "Select mesh faces."),
			"PolyEd.SelectFaces");

		ToolbarBuilder.EndBlockGroup();
		ToolbarBuilder.EndSection();

		return ToolbarBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FVertexAttributePaintToolDetailCustomization::MakeSelectionIsolationWidget() const
	{
#if 0
		return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.HAlign(HAlign_Center)
		.ToolTipText(LOCTEXT("IsolateSelectedTooltip",
			"Shows only the selected faces in the viewport.\n"
			"Weight editing operations will not affect hidden vertices.\n"
			"NOTE: This only works on the target (main) mesh."))
		.IsEnabled_Lambda([this]()
			{
				if (Tool.IsValid())
				{
					// isolated selection only available on main mesh (for now)
					const bool bHasSelection = Tool->GetMainMeshSelector()->IsAnyComponentSelected();
					const bool bAlreadyIsolatingSelection = Tool->GetSelectionIsolator()->IsSelectionIsolated();
					return bHasSelection || bAlreadyIsolatingSelection;
				}
				return false;
			})
		.IsChecked_Lambda([this]()
			{
				if (Tool.IsValid())
				{
					const bool bAlreadyIsolatingSelection = Tool->GetSelectionIsolator()->IsSelectionIsolated();
					return bAlreadyIsolatingSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}
				return ECheckBoxState::Unchecked;
			})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState InCheckBoxState)
			{
				if (UWeightToolSelectionIsolator* SelectionIsolator = Tool.IsValid() ? Tool->GetSelectionIsolator() : nullptr)
				{
					if (InCheckBoxState == ECheckBoxState::Checked)
					{
						SelectionIsolator->IsolateSelectionAsTransaction();
					}
					else
					{
						SelectionIsolator->UnIsolateSelectionAsTransaction();
					}
				}
			})
		[
			SNew(STextBlock)
				.Text_Lambda([this]()
					{
						if (!Tool.IsValid() || Tool->GetSelectionIsolator()->IsSelectionIsolated())
						{
							return LOCTEXT("ShowAllButtonLabel", "Show Full Mesh");
						}

						return LOCTEXT("IsolateButtonLabel", "Isolate Selected");
					})
		];

#else //0
		return SNullWidget::NullWidget;

#endif //0
	}

	TSharedRef<SWidget> FVertexAttributePaintToolDetailCustomization::MakeSelectionEditActionsToolbar() const
	{
		// [ GROW / SHRINK / FLOOD ]
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2.f, WeightEditVerticalPadding)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]
						{
							return Tool.IsValid() ? Tool->HasSelection() : false;
						})
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("GrowSelectionButtonLabel", "Grow"))
					.ToolTipText(LOCTEXT("GrowSelectionTooltip",
						"Grow the current selection by adding connected neighbors to current selection.\n"))
					.OnClicked_Lambda([this]()
						{
							if (Tool.IsValid())
							{
								Tool->GrowSelection();
							}
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot()
				.Padding(2.f, WeightEditVerticalPadding)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]
						{
							return Tool.IsValid() ? Tool->HasSelection() : false;
						})
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("ShrinkSelectionButtonLabel", "Shrink"))
					.ToolTipText(LOCTEXT("ShrinkSelectionTooltip",
						"Shrink the current selection by removing components on the border of the current selection.\n"))
					.OnClicked_Lambda([this]()
						{
							if (Tool.IsValid())
							{
								Tool->ShrinkSelection();
							}
							return FReply::Handled();
						})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2.f, WeightEditVerticalPadding)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]
						{
							return Tool.IsValid() ? Tool->HasSelection() : false;
						})
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("FloodSelectionButtonLabel", "Flood"))
					.ToolTipText(LOCTEXT("FloodSelectionTooltip",
						"Flood the current selection by adding all connected components to the current selection.\n"))
					.OnClicked_Lambda([this]()
						{
							if (Tool.IsValid())
							{
								Tool->FloodSelection();
							}
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot()
				.Padding(2.f, WeightEditVerticalPadding)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]
						{
							return Tool.IsValid() ? Tool->HasSelection() : false;
						})
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("BorderSelectionButtonLabel", "Convert to Border"))
					.ToolTipText(LOCTEXT("BorderSelectionTooltip", "Select vertices on the border of the current selection."))
					.OnClicked_Lambda([this]()
						{
							if (Tool.IsValid())
							{
								Tool->SelectBorder();
							}
							return FReply::Handled();
						})
				]
			];
	}

	void FVertexAttributePaintToolDetailCustomization::AddSelectionElementsRow(IDetailCategoryBuilder& EditSelectionCategory) const
	{
		TSharedRef<SWidget> ElementsToolBar = MakeSelectionElementsToolbar();
		TSharedRef<SWidget> IsolationWidget = MakeSelectionIsolationWidget();
		TSharedRef<SWidget> SelectionEditActionsToolBar = MakeSelectionEditActionsToolbar();

		EditSelectionCategory.AddCustomRow(LOCTEXT("EditSelectionRow", "Edit Selection"), false)
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2.0f, WeightEditVerticalPadding)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						ElementsToolBar
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						IsolationWidget
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SelectionEditActionsToolBar
			]
		];
	}

	void FVertexAttributePaintToolDetailCustomization::AddEmptySelectionWarningRow( IDetailCategoryBuilder& EditValuesCategory) const
	{
		EditValuesCategory.AddCustomRow(LOCTEXT("SelectMessageRow", "Select Vertices"), false)
		.WholeRowContent()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]()
				{
					return Tool.IsValid() && !Tool->HasSelection() ? EVisibility::Visible : EVisibility::Collapsed;
				})
			.Text_Lambda([this]()
				{
					return LOCTEXT("NothingSelectedLabel", "Select vertices on target mesh to edit weights...");
				})
		];
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionOperationRow(
		IDetailCategoryBuilder& EditValuesCategory,
		const FText& RowName,
		const TSharedRef<SWidget>& ButtonWidget, 
		const TSharedRef<SWidget>& ValueWidget) const
	{
		EditValuesCategory.AddCustomRow(RowName, false)
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			.IsEnabled_Lambda([this]
				{
					return Tool.IsValid() ? Tool->HasSelection() : false;
				})
			+ SHorizontalBox::Slot()
			[
				SNew(SBox)[ButtonWidget]
			]
			+ SHorizontalBox::Slot()
			[
				ValueWidget
			]
		];
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionAddMultiplySliderOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SSegmentedControl<EDataflowEditorToolEditOperation>> AddMultiplyButtonsWidget =
			SNew(SSegmentedControl<EDataflowEditorToolEditOperation>)
			.ToolTipText(LOCTEXT("InteractiveEditModeTooltip",
				"Add: applies the current value plus the flood value to the new value.\n"
				"Multiply: applies the current value multiplied by the flood value to the new value.\n"
				"This operation applies interactively while dragging the slider. It operates on the currently selected vertices."))
			.Value(EDataflowEditorToolEditOperation::Add)
			+ SSegmentedControl<EDataflowEditorToolEditOperation>::Slot(EDataflowEditorToolEditOperation::Add)
			.Text(LOCTEXT("BrushAddMode", "Add"))
			+ SSegmentedControl<EDataflowEditorToolEditOperation>::Slot(EDataflowEditorToolEditOperation::Multiply)
			.Text(LOCTEXT("BrushMultiplyMode", "Multiply"))
			;

		auto GetMinValue = [AddMultiplyButtonsWidget]() -> float
			{
				switch (AddMultiplyButtonsWidget->GetValue())
				{
				case EDataflowEditorToolEditOperation::Add: return -1.f;
				case EDataflowEditorToolEditOperation::Multiply: return 0.f;
				}
				return 0.f;
			};

		auto GetMaxValue = [AddMultiplyButtonsWidget]() -> float
			{
				switch (AddMultiplyButtonsWidget->GetValue())
				{
				case EDataflowEditorToolEditOperation::Add: return 1.f;
				case EDataflowEditorToolEditOperation::Multiply: return 10.f;
				}
				return 0.f;
			};

		struct FSliderState
		{
			float Value = 0.f;
		};

		TSharedPtr<FSliderState> SliderState = MakeShared<FSliderState>();

		auto GetCurrentValue = [SliderState]()
			{
				return SliderState->Value;
			};

		auto ResetValue = [SliderState, AddMultiplyButtonsWidget]()
			{
				switch (AddMultiplyButtonsWidget->GetValue())
				{
				case EDataflowEditorToolEditOperation::Add: 
					SliderState->Value = 0.f;
					break;
				case EDataflowEditorToolEditOperation::Multiply:
					SliderState->Value = 1.f;
					break;
				}
			};

		auto ApplyOperation = [this, SliderState, AddMultiplyButtonsWidget](float NewValue)
			{
				if (Tool.IsValid())
				{
					SliderState->Value = NewValue;
					Tool->ApplyValueToSelection(AddMultiplyButtonsWidget->GetValue(), NewValue);
				}
			};

		TSharedRef<SWidget> SliderValueWidget = 
			SNew(SSpinBox<float>)
			.MinSliderValue_Lambda(GetMinValue)
			.MaxSliderValue_Lambda(GetMaxValue)
			.MinValue_Lambda(GetMinValue)
			.MaxValue_Lambda(GetMaxValue)
			.Value_Lambda(GetCurrentValue)
			.OnValueChanged_Lambda([ApplyOperation](float NewValue)
				{
					ApplyOperation(NewValue);
				})
			.OnValueCommitted_Lambda([ApplyOperation](float NewValue, ETextCommit::Type CommitType)
				{
					ApplyOperation(NewValue);
				})
			.OnBeginSliderMovement_Lambda([this]()
				{
					// TODO(ccaillaud)
					/*if (Tool.IsValid())
					{
						Tool->BeginChange();
					}*/
				})
			.OnEndSliderMovement_Lambda([this, ResetValue](float)
				{
					const FText TransactionLabel = LOCTEXT("FloodWeightChange", "Flood values on vertices.");
					// TODO(ccaillaud)
					/*if (Tool.IsValid())
					{
						Tool->EndChange(TransactionLabel);
					}*/

					// reset the value
					//SliderState-
					ResetValue();
				})
			.ToolTipText(LOCTEXT("FloodWeightsToolTip", "Drag the slider to interactively adjust values on the selected vertices."))
			;

		MakeSelectionOperationRow(EditValuesCategory, LOCTEXT("FloodValuesRow", "Flood Values Slider"), AddMultiplyButtonsWidget, SliderValueWidget);
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionAddOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SSpinBox<float>> AddValueWidget =
			SNew(SSpinBox<float>)
			.MinValue(0)
			.MaxValue(1)
			.Value(1)
			.ToolTipText(LOCTEXT("AddValueSliderToolTip", "Adjust the value to Add to the selected vertices."));

		TSharedRef<SWidget> AddButtonWidget =
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("AddValueButtonLabel", "Add"))
			.ToolTipText(LOCTEXT("AddValueButtonTooltip",
				"Add: applies the current value plus the flood value to the new weight.\n"
				"This operation applies to the currently selected vertices."))
			.OnClicked_Lambda([this, AddValueWidget]()
				{
					if (Tool.IsValid())
					{
						Tool->ApplyValueToSelection(EDataflowEditorToolEditOperation::Add, AddValueWidget->GetValue());
					}
					return FReply::Handled();
				});

		MakeSelectionOperationRow(EditValuesCategory, LOCTEXT("SelectionAddOperationRow", "Selection Add Operation"), AddButtonWidget, AddValueWidget);
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionReplaceOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SSpinBox<float>> ReplaceValueWidget =
			SNew(SSpinBox<float>)
			.MinValue(0)
			.MaxValue(1)
			.Value(1)
			.ToolTipText(LOCTEXT("ReplaceValueSliderToolTip", "Adjust the value to be replace on the selected vertices."));

		TSharedRef<SWidget> ReplaceButtonWidget =
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("ReplaceValueButtonLabel", "Replace"))
			.ToolTipText(LOCTEXT("ReplaceValueButtonTooltip",
				"Replace: the value of selected vertices is replaced by the specified value.\n"
				"This operation applies to the currently selected vertices."))
			.OnClicked_Lambda([this, ReplaceValueWidget]()
				{
					if (Tool.IsValid())
					{
						Tool->ApplyValueToSelection(EDataflowEditorToolEditOperation::Replace, ReplaceValueWidget->GetValue());
					}
					return FReply::Handled();
				});

		MakeSelectionOperationRow(EditValuesCategory, LOCTEXT("SelectionReplaceOperationRow", "Selection Replace Operation"), ReplaceButtonWidget, ReplaceValueWidget);
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionInvertOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SWidget> NoValueValueWidget = SNullWidget::NullWidget;

		TSharedRef<SWidget> ReplaceButtonWidget =
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("InvertValueButtonLabel", "Invert"))
			.ToolTipText(LOCTEXT("InvertValueButtonTooltip",
				"Invert: the value of selected vertices is replaced by opne minus the specified value.\n"
				"This operation applies to the currently selected vertices."))
			.OnClicked_Lambda([this]()
				{
					if (Tool.IsValid())
					{
						Tool->ApplyValueToSelection(EDataflowEditorToolEditOperation::Invert, 0.0f);
					}
					return FReply::Handled();
				});

		MakeSelectionOperationRow(EditValuesCategory, LOCTEXT("SelectionInvertOperationRow", "Selection Invert Operation"), ReplaceButtonWidget, NoValueValueWidget);
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionRelaxOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SSpinBox<float>> RelaxValueWidget =
			SNew(SSpinBox<float>)
			.MinValue(0)
			.MaxValue(1)
			.Value(0.5)
			.ToolTipText(LOCTEXT("RelaxValueSliderToolTip", "Amount to blend when relaxing  the values"));

		TSharedRef<SWidget> RelaxButtonWidget =
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("RelaxValueButtonLabel", "Relax"))
			.ToolTipText(LOCTEXT("RelaxValueButtonTooltip",
				"Relax: the value of each selected vertex is replaced by the average of it's neighbors. \n"
				"This smooths values across the mesh."))
			.OnClicked_Lambda([this, RelaxValueWidget]()
				{
					if (Tool.IsValid())
					{
						Tool->ApplyValueToSelection(EDataflowEditorToolEditOperation::Relax, RelaxValueWidget->GetValue());
					}
					return FReply::Handled();
				});

		MakeSelectionOperationRow(EditValuesCategory, LOCTEXT("SelectionRelaxOperationRow", "Selection Relax Operation"), RelaxButtonWidget, RelaxValueWidget);
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionPruneOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SSpinBox<float>> PruneValueWidget =
			SNew(SSpinBox<float>)
			.MinValue(0.f)
			.MaxValue(1.f)
			.Value(0.01)
			.ToolTipText(LOCTEXT("PruneValueSliderToolTip", "Prune Threshold - Values below this threshold will be set to zero"))
			.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					if (ToolProperties.IsValid())
					{
						// TOOD(ccaillaud) ==> ToolProperties->SaveConfig();
					}
				});

		TSharedRef<SWidget> PruneButtonWidget =
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("PruneValuesButtonLabel", "Prune"))
			.ToolTipText(LOCTEXT("PruneButtonTooltip",
				"Zero values below the given threshold value.\n"
				"This command operates on the selected vertices."))
			.OnClicked_Lambda([this, PruneValueWidget]()
				{
					if (Tool.IsValid())
					{
						Tool->PruneSelection(PruneValueWidget->GetValue());
					}
					return FReply::Handled();
				});

		MakeSelectionOperationRow(EditValuesCategory, LOCTEXT("SelectionPruneOperationRow", "Selection Prune Operation"), PruneButtonWidget, PruneValueWidget);
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionCopyAndPasteRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		// COPY/PASTE WEIGHTS category
		EditValuesCategory.AddCustomRow(LOCTEXT("CopyPasteValuesRow", "Copy Paste"), false)
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			.IsEnabled_Lambda([this]
				{
					return Tool.IsValid() ? Tool->HasSelection() : false;
				})

			+ SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SBox)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("CopyValuesButtonLabel", "Copy"))
					.ToolTipText(LOCTEXT("CopyButtonTooltip",
						"Copy the average of the selected values to the clipboard. \n"
						"This is designed to work with the Paste command."))
					.OnClicked_Lambda([this]()
						{
							if (Tool.IsValid())
							{
								Tool->CopyAverageFromSelectionToClipboard();
							}
							return FReply::Handled();
						})
				]
			]

			+ SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("PasteValuesButtonLabel", "Paste"))
				.ToolTipText(LOCTEXT("PasteButtonTooltip",
					"Paste the value on the selected vertices.\n"
					"This command requires the clipboard contain the selection average value from the Copy command."))
				.OnClicked_Lambda([this]()
					{
						if (ToolProperties.IsValid())
						{
							Tool->PasteValueToSelectionFromClipboard();
						}
						return FReply::Handled();
					})
			]
		];
	}

	void FVertexAttributePaintToolDetailCustomization::MakeSelectionMirrorOperationRow(IDetailCategoryBuilder& EditValuesCategory) const
	{
		TSharedRef<SSegmentedControl<EAxis::Type>> XYZButtonsWidget =
			SNew(SSegmentedControl<EAxis::Type>)
			.ToolTipText(LOCTEXT("MirrorAxisTooltip",
				"X: copies weights across the YZ plane.\n"
				"Y: copies weights across the XZ plane.\n"
				"Z: copies weights across the XY plane."))
			.Value_Lambda([this]()
				{
					return ToolProperties.IsValid() ? ToolProperties->MirrorProperties.MirrorAxis : TEnumAsByte<EAxis::Type>(EAxis::X);
				})
			.OnValueChanged_Lambda([this](EAxis::Type Mode)
				{
					if (ToolProperties.IsValid())
					{
						ToolProperties->MirrorProperties.MirrorAxis = Mode;
					}
				})
			+ SSegmentedControl<EAxis::Type>::Slot(EAxis::X)
			.Text(LOCTEXT("MirrorXLabel", "X"))
			+ SSegmentedControl<EAxis::Type>::Slot(EAxis::Y)
			.Text(LOCTEXT("MirrorYLabel", "Y"))
			+ SSegmentedControl<EAxis::Type>::Slot(EAxis::Z)
			.Text(LOCTEXT("MirrorZLabel", "Z"));

			TSharedRef<SSegmentedControl<EDataflowEditorToolMirrorDirection>> MirrortSideButtonsWidget =
				SNew(SSegmentedControl<EDataflowEditorToolMirrorDirection>)
				.ToolTipText(LOCTEXT("MirrorDirectionTooltip", "The direction that determines what side of the plane to copy weights from."))
				.Value_Lambda([this]()
					{
						return ToolProperties.IsValid() ? ToolProperties->MirrorProperties.MirrorDirection : EDataflowEditorToolMirrorDirection::PositiveToNegative;
					})
				.OnValueChanged_Lambda([this](EDataflowEditorToolMirrorDirection Mode)
					{
						if (ToolProperties.IsValid())
						{
							ToolProperties->MirrorProperties.MirrorDirection = Mode;
						}
					})
				+ SSegmentedControl<EDataflowEditorToolMirrorDirection>::Slot(EDataflowEditorToolMirrorDirection::PositiveToNegative)
				.Text(LOCTEXT("MirrorPosToNegLabel", "+ to -"))
				+ SSegmentedControl<EDataflowEditorToolMirrorDirection>::Slot(EDataflowEditorToolMirrorDirection::NegativeToPositive)
				.Text(LOCTEXT("MirrorNegToPosLabel", "- to +"));

		TSharedRef<SButton> MirrorButtonWidget = 
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("MirrorWeightsButtonLabel", "Mirror"))
			.ToolTipText(LOCTEXT("MirrorButtonTooltip",
				"Values are copied across the given plane in the given direction.\n"
				"This command operates on the selected vertices."))
			.OnClicked_Lambda([this]()
				{
					if (Tool.IsValid())
					{
						Tool->MirrorValues();
					}
					return FReply::Handled();
				});

		// MIRROR WEIGHTS category
		EditValuesCategory.AddCustomRow(LOCTEXT("MirrorWeightsRow", "Mirror"), false)
			.WholeRowContent()
			[
				SNew(SVerticalBox)
					.IsEnabled_Lambda([this] 
						{ 
							return Tool.IsValid() ? Tool->HasSelection() : false; 
						})
					+ SVerticalBox::Slot()
					.Padding(WeightEditHorizontalPadding, WeightEditVerticalPadding)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.FillWidth(WeightEditingLabelsPercent)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("MirrorPlaneLabel", "Mirror Plane"))
								.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
								.ToolTipText(LOCTEXT("MirrorPlaneTooltip", "The plane to copy weights across."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							[
								XYZButtonsWidget
							]

							+ SHorizontalBox::Slot()
							[
								MirrortSideButtonsWidget
							]
						]
					]
					+ SVerticalBox::Slot()
					.Padding(0.f, WeightEditVerticalPadding)
					[
						SNew(SBox)
						[
							MirrorButtonWidget
						]
					]
			];
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FVertexAttributePaintToolDetailCustomization::AddSelectionUI(IDetailLayoutBuilder& DetailBuilder) const
	{
		// custom display of selection editing tools
		IDetailCategoryBuilder& EditSelectionCategory = DetailBuilder.EditCategory("Edit Selection", FText::GetEmpty(), ECategoryPriority::Important);
		EditSelectionCategory.InitiallyCollapsed(false);
		{
			// create a toolbar for the selection filter [ Vtx |  Edges | Faces ] (Isolate)
			AddSelectionElementsRow(EditSelectionCategory);
		}
	
		IDetailCategoryBuilder& EditValuesCategory = DetailBuilder.EditCategory("Edit Values", FText::GetEmpty(), ECategoryPriority::Important);
		EditValuesCategory.InitiallyCollapsed(false);
		{
			AddEmptySelectionWarningRow(EditValuesCategory);

			MakeSelectionAddMultiplySliderOperationRow(EditValuesCategory);

			MakeSelectionAddOperationRow(EditValuesCategory);

			MakeSelectionReplaceOperationRow(EditValuesCategory);

			MakeSelectionInvertOperationRow(EditValuesCategory);

			MakeSelectionRelaxOperationRow(EditValuesCategory);

			MakeSelectionMirrorOperationRow(EditValuesCategory);

			MakeSelectionPruneOperationRow(EditValuesCategory);

			MakeSelectionCopyAndPasteRow(EditValuesCategory);
		}
	}

	void FVertexAttributePaintToolDetailCustomization::HideProperty(IDetailLayoutBuilder& DetailBuilder, FName PropertyName) const
	{
		TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(PropertyName);
		DetailBuilder.HideProperty(Property);
	}
}

#undef LOCTEXT_NAMESPACE
