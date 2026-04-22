// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorBodyModelToolView.h"

#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "MetaHumanCharacterEditorModule.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "UI/Widgets/SMetaHumanCharacterEditorParametricView.h"
#include "UI/Widgets/SMetaHumanCharacterEditorFixedCompatibilityPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorBodyModelToolView"

void SMetaHumanCharacterEditorBodyModelToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorToolWithSubTools* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorBodyModelToolView::GetToolProperties() const
{
	TArray<UObject*> ToolProperties;
	const UMetaHumanCharacterEditorBodyModelTool* BodyModelTool = Cast<UMetaHumanCharacterEditorBodyModelTool>(Tool);
	
	constexpr bool bOnlyEnabled = true;
	if (IsValid(BodyModelTool))
	{
		ToolProperties = BodyModelTool->GetToolProperties(bOnlyEnabled);
	}

	UObject* const* SubToolProperties = Algo::FindByPredicate(ToolProperties,
		[](UObject* ToolProperty)
		{
			return IsValid(Cast<UMetaHumanCharacterBodyModelSubToolBase>(ToolProperty));
		});

	return SubToolProperties ? Cast<UInteractiveToolPropertySet>(*SubToolProperties) : nullptr;
}

void SMetaHumanCharacterEditorBodyModelToolView::MakeToolView()
{
	if(ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->AddSlot()
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ParametricSubToolView, SVerticalBox)
					.Visibility(this, &SMetaHumanCharacterEditorBodyModelToolView::GetParametricSubToolViewVisibility)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ParametricFixedWarningView, SVerticalBox)
					.Visibility(this, &SMetaHumanCharacterEditorBodyModelToolView::GetParametricSubToolFixedWarningVisibility)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(FixedCompatibilitySubToolView, SVerticalBox)
					.Visibility(this, &SMetaHumanCharacterEditorBodyModelToolView::GetFixedCompatibilitySubToolViewVisibility)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(FixedCompatibilityWarningView, SVerticalBox)
					.Visibility(this, &SMetaHumanCharacterEditorBodyModelToolView::GetFixedCompatibilitySubToolWarningVisibility)
				]
			];

		MakeParametricSubToolView();
		MakeParametricFixedWarningView();
		MakeFixedCompatibilitySubToolView();
		MakeFixedCompatibilityWarningView();

		Tool.Pin()->OnPropertySetsModified.AddSP(this, &SMetaHumanCharacterEditorBodyModelToolView::OnPropertySetsModified);
	}
}

void SMetaHumanCharacterEditorBodyModelToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	OnPreEditChangeProperty(PropertyAboutToChange, PropertyAboutToChange->GetName());
}

void SMetaHumanCharacterEditorBodyModelToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
	OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorBodyModelToolView::GetParametricProperties() const
{
	UMetaHumanCharacterParametricBodyProperties* ParametricToolProperties = nullptr;
	if (Tool.IsValid())
	{
		constexpr bool bOnlyEnabled = false;
		Tool->GetToolProperties(bOnlyEnabled).FindItemByClass<UMetaHumanCharacterParametricBodyProperties>(&ParametricToolProperties);
	}

	return ParametricToolProperties;
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorBodyModelToolView::GetFixedCompatibilityProperties() const
{
	UMetaHumanCharacterFixedCompatibilityBodyProperties* FixedCompatibilityToolProperties = nullptr;
	if (Tool.IsValid())
	{
		constexpr bool bOnlyEnabled = false;
		Tool->GetToolProperties(bOnlyEnabled).FindItemByClass<UMetaHumanCharacterFixedCompatibilityBodyProperties>(&FixedCompatibilityToolProperties);
	}

	return FixedCompatibilityToolProperties;
}

void SMetaHumanCharacterEditorBodyModelToolView::MakeParametricSubToolView()
{
	if (ParametricSubToolView.IsValid())
	{
		ParametricSubToolView->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateParametricSubToolViewSection()
				]
			];
	}
}

void SMetaHumanCharacterEditorBodyModelToolView::MakeParametricFixedWarningView()
{
	if (ParametricFixedWarningView.IsValid())
	{
		ParametricFixedWarningView->AddSlot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4.f)
			.AutoHeight()
			[
				SNew(SWarningOrErrorBox)
				.AutoWrapText(false)
				.MessageStyle(EMessageStyle::Warning)
				.Message(LOCTEXT("MetaHumanBodyParametricFixedWarning", "This Asset uses a Fixed Body type, Fixed Body types can't\n"
															"be modified or used for modelling without fitting them first to\n"
															"the Parametric Model. This is an approximation and can\n"
															"result in some visual differences."))
			]
			+ SVerticalBox::Slot()
			.Padding(4.f)
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorToolPanel)
				.Label(LOCTEXT("FixedBodyTypeLabel", "Fixed Body Type"))
				.Content()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(4.f)
					[
						SNew(SButton)
						.OnClicked(this, &SMetaHumanCharacterEditorBodyModelToolView::OnPerformParametricFitButtonClicked)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PerformParametricFit", "Perform Parametric Fit"))
						]
					]
				]
			]
		];
	}
}

void SMetaHumanCharacterEditorBodyModelToolView::MakeFixedCompatibilitySubToolView()
{
	if (FixedCompatibilitySubToolView.IsValid())
	{
		FixedCompatibilitySubToolView->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateFixedCompatibilitySubToolViewSection()
				]
			];
	}
}

void SMetaHumanCharacterEditorBodyModelToolView::MakeFixedCompatibilityWarningView()
{
	if (FixedCompatibilityWarningView.IsValid())
	{
		FixedCompatibilityWarningView->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					SNew(SWarningOrErrorBox)
					.AutoWrapText(false)
					.MessageStyle(EMessageStyle::Warning)
					.Message(LOCTEXT("MetaHumanBodyFixedCompatibilityWarning", "Fixed (Compatibility) body types require\n"
																"MetaHuman: Optional Content to be installed. Please install Optional\n"
																"Content to use Fixed (Compatibility) bodies."))
				]
			];
	}
}


TSharedRef<SWidget> SMetaHumanCharacterEditorBodyModelToolView::CreateParametricSubToolViewSection()
{
	UMetaHumanCharacterParametricBodyProperties* ParametricBodyProperties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetToolProperties());
	if (!ParametricBodyProperties)
	{
		return SNullWidget::NullWidget;
	}
	FProperty* ShowMeasurementsProperty = UMetaHumanCharacterParametricBodyProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterParametricBodyProperties, bShowMeasurements));
	FProperty* ScaleMeasurementRangesWithHeight = UMetaHumanCharacterParametricBodyProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterParametricBodyProperties, bScaleRangesByHeight));

	const UMetaHumanCharacterEditorBodyModelTool* BodyModelTool = Cast<UMetaHumanCharacterEditorBodyModelTool>(Tool);
	if (!IsValid(BodyModelTool))
	{
		return SNullWidget::NullWidget;
	}
	UMetaHumanCharacterEditorBodyParameterProperties* BodyParameterProperties = BodyModelTool->BodyParameterProperties;
	FProperty* GlobalDeltaProperty = UMetaHumanCharacterEditorBodyParameterProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorBodyParameterProperties, GlobalDelta));

	const TArray<FName> GlobalConstraintsNames =
	{
		TEXT("Masculine/Feminine"),
		TEXT("Muscularity"),
		TEXT("Fat"),
		TEXT("Height")
	};

	const TArray<FName> UpperTorsoConstraintsNames =
	{
		TEXT("Across Shoulder"),
		TEXT("Shoulder to Apex"),
		TEXT("Front Interscye Length"),
		TEXT("Chest"),
		TEXT("Bust Span"),
		TEXT("Underbust"),
		TEXT("Neck to Waist")
	};

	const TArray<FName> LowerTorsoConstraintsNames =
	{
		TEXT("Waist"),
		TEXT("High Hip"),
		TEXT("Hip")
	};

	const TArray<FName> NeckConstraintsNames =
	{
		TEXT("Neck"),
		TEXT("Neck Base"),
		TEXT("Neck Length")
	};

	const TArray<FName> ArmsConstraintsNames =
	{
		TEXT("Upper Arm Length"),
		TEXT("Lower Arm Length"),
		TEXT("Forearm"),
		TEXT("Bicep"),
		TEXT("Elbow"),
		TEXT("Wrist")
	};

	const TArray<FName> LegsConstraintsNames =
	{
		TEXT("Inseam"),
		TEXT("Thigh"),
		TEXT("Knee"),
		TEXT("Calf")
	};

	const TArray<FName> DiagnosticsConstraintsNames =
	{
		TEXT("Shoulder Height"),
		TEXT("Rise")
	};

	const TSharedRef<SWidget> ParametricConstraintsWidget =
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelGlobal", "Global"), GlobalConstraintsNames)
		]
		
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelUpperTorso", "Upper Torso"), UpperTorsoConstraintsNames)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelLowerTorso", "Lower Torso"), LowerTorsoConstraintsNames)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelNeck", "Neck"), NeckConstraintsNames)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelArms", "Arms"), ArmsConstraintsNames)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelLegs", "Legs"), LegsConstraintsNames)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4.f)
		.AutoHeight()
		[
			CreateParametricConstraintsPanel(LOCTEXT("ConstraintGroupLabelDiagnostics", "Diagnostics"), DiagnosticsConstraintsNames, /* bDisagnosticsView */ true)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		.Padding(4.f)
		.AutoHeight()
		[
			SNew(SMetaHumanCharacterEditorToolPanel)
			.Label(LOCTEXT("ParametricBodyParameters", "Body Parameters"))
			.Content()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					CreatePropertyCheckBoxWidget(ShowMeasurementsProperty->GetDisplayNameText(), ShowMeasurementsProperty, ParametricBodyProperties)
				]

				+ SVerticalBox::Slot()
				[
					CreatePropertyCheckBoxWidget(ScaleMeasurementRangesWithHeight->GetDisplayNameText(), ScaleMeasurementRangesWithHeight, ParametricBodyProperties)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					CreatePropertySpinBoxWidget(GlobalDeltaProperty->GetDisplayNameText(), GlobalDeltaProperty, BodyParameterProperties)
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ForegroundColor(FLinearColor::White)
					.OnClicked(this, &SMetaHumanCharacterEditorBodyModelToolView::OnResetButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResetParametricBody", "Reset Body"))
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					]
				]
			]
		];


	return ParametricConstraintsWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBodyModelToolView::CreateParametricConstraintsPanel(const FText& Label, const TArray<FName>& ConstraintNames, bool bDiagnosticsView)
{
	UMetaHumanCharacterParametricBodyProperties* ParametricBodyProperties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetParametricProperties());
	TArray<FMetaHumanCharacterBodyConstraintItemPtr> Constraints = ParametricBodyProperties->GetConstraintItems(ConstraintNames);

	return 
		SNew(SMetaHumanCharacterEditorParametricConstraintsPanel)
		.Label(Label)
		.ListItemsSource(&Constraints)
		.OnBeginConstraintEditing(this, &SMetaHumanCharacterEditorBodyModelToolView::OnBeginConstraintEditing)
		.OnConstraintsChanged(this, &SMetaHumanCharacterEditorBodyModelToolView::OnParametricConstraintsChanged)
		.DiagnosticsView(bDiagnosticsView);
}

void SMetaHumanCharacterEditorBodyModelToolView::OnBeginConstraintEditing()
{
	if (UMetaHumanCharacterParametricBodyProperties* ParametricBodyProerties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetParametricProperties()))
	{
		ParametricBodyProerties->OnBeginConstraintEditing();
	}
}

void SMetaHumanCharacterEditorBodyModelToolView::OnParametricConstraintsChanged(bool bInCommit)
{
	if (UMetaHumanCharacterParametricBodyProperties* ParametricBodyProerties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetParametricProperties()))
	{
		ParametricBodyProerties->OnConstraintItemsChanged(bInCommit);
	}
}

FReply SMetaHumanCharacterEditorBodyModelToolView::OnResetButtonClicked() const
{
	if (UMetaHumanCharacterParametricBodyProperties* ParametricBodyProerties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetParametricProperties()))
	{
		ParametricBodyProerties->ResetConstraints();
	}
	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorBodyModelToolView::OnPerformParametricFitButtonClicked() const
{
	if (UMetaHumanCharacterParametricBodyProperties* ParametricBodyProerties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetParametricProperties()))
	{
		ParametricBodyProerties->PerformParametricFit();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBodyModelToolView::CreateFixedCompatibilitySubToolViewSection()
{
	UMetaHumanCharacterFixedCompatibilityBodyProperties* FixedCompatibilityBodyProperties = Cast<UMetaHumanCharacterFixedCompatibilityBodyProperties>(GetFixedCompatibilityProperties());
	FProperty* BodyTypeProperty = UMetaHumanCharacterFixedCompatibilityBodyProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterFixedCompatibilityBodyProperties, MetaHumanBodyType));

	void* FixedProperties = IsValid(FixedCompatibilityBodyProperties) ? FixedCompatibilityBodyProperties : nullptr;
	if (!FixedProperties)
	{
		return SNullWidget::NullWidget;
	}

	return
		SAssignNew(FixedCompatibilityPanel, SMetaHumanCharacterEditorFixedCompatibilityPanel)
		.FixedCompatabilityProperties(FixedCompatibilityBodyProperties)
		.OnSelectionChanged_Lambda([this](uint8 InMetaHumanBodyType)
		{
			UMetaHumanCharacterFixedCompatibilityBodyProperties* FixedCompatibilityBodyProperties = Cast<UMetaHumanCharacterFixedCompatibilityBodyProperties>(GetFixedCompatibilityProperties());
			FixedCompatibilityBodyProperties->MetaHumanBodyType = static_cast<EMetaHumanBodyType>(InMetaHumanBodyType);
			FixedCompatibilityBodyProperties->OnMetaHumanBodyTypeChanged();
		});
}

void SMetaHumanCharacterEditorBodyModelToolView::OnPropertySetsModified()
{
	UMetaHumanCharacterBodyModelSubToolBase* EnabledSubToolProperties = Cast<UMetaHumanCharacterBodyModelSubToolBase>(GetToolProperties());
	if (EnabledSubToolProperties)
	{
		UMetaHumanCharacterEditorBodyModelTool* BodyModelTool = Cast<UMetaHumanCharacterEditorBodyModelTool>(Tool);
		if (BodyModelTool)
		{
			BodyModelTool->SetEnabledSubTool(EnabledSubToolProperties, true);
	
			TArray<UObject*> AllSubToolProperties = Tool->GetToolProperties(false);
			for (int32 Ind = 0; Ind < AllSubToolProperties.Num(); ++Ind)
			{
				if (AllSubToolProperties[Ind] != EnabledSubToolProperties)
				{
					UMetaHumanCharacterBodyModelSubToolBase* SubTool = Cast<UMetaHumanCharacterBodyModelSubToolBase>(AllSubToolProperties[Ind]);
					if (SubTool)
					{
						BodyModelTool->SetEnabledSubTool(SubTool, false);
						FixedCompatibilityPanel->UpdateItemListFromProperties();
					}
				}
			}
		}
	}
}

EVisibility SMetaHumanCharacterEditorBodyModelToolView::GetParametricSubToolViewVisibility() const
{
	UMetaHumanCharacterParametricBodyProperties* ParametricBodyProperties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetToolProperties());
	if (IsValid(ParametricBodyProperties))
	{
		if (!ParametricBodyProperties->IsFixedBodyType())
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorBodyModelToolView::GetParametricSubToolFixedWarningVisibility() const
{
	UMetaHumanCharacterParametricBodyProperties* ParametricBodyProperties = Cast<UMetaHumanCharacterParametricBodyProperties>(GetToolProperties());
	if (IsValid(ParametricBodyProperties))
	{
		if (ParametricBodyProperties->IsFixedBodyType())
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorBodyModelToolView::GetFixedCompatibilitySubToolViewVisibility() const
{
	const bool bIsVisible = IsValid(Cast<UMetaHumanCharacterFixedCompatibilityBodyProperties>(GetToolProperties()));
	const bool bMetaHumanContentAvailable = FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled();
	
	return (bIsVisible && bMetaHumanContentAvailable) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorBodyModelToolView::GetFixedCompatibilitySubToolWarningVisibility() const
{
	const bool bIsVisible = IsValid(Cast<UMetaHumanCharacterFixedCompatibilityBodyProperties>(GetToolProperties()));
	const bool bMetaHumanContentAvailable = FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled();
	
	return (bIsVisible && !bMetaHumanContentAvailable) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
