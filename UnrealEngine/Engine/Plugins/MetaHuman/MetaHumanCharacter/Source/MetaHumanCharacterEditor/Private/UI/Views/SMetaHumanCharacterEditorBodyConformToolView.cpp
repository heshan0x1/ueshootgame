// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorBodyConformToolView.h"

#include "Algo/Find.h"
#include "IDetailsView.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Tools/MetaHumanCharacterEditorBodyConformTool.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "InteractiveToolManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorBodyConformToolView"

void SMetaHumanCharacterEditorBodyConformToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorToolWithSubTools* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorBodyConformToolView::GetToolProperties() const
{
	TArray<UObject*> ToolProperties;
	const UMetaHumanCharacterEditorBodyConformTool* BodyConformTool = Cast<UMetaHumanCharacterEditorBodyConformTool>(Tool);

	constexpr bool bOnlyEnabled = true;
	if (IsValid(BodyConformTool))
	{
		ToolProperties = BodyConformTool->GetToolProperties(bOnlyEnabled);
	}

	UObject* const* SubToolProperties = Algo::FindByPredicate(ToolProperties,
		[](UObject* ToolProperty)
		{
			return IsValid(Cast<UMetaHumanCharacterImportBodySubToolBase>(ToolProperty));
		});

	return SubToolProperties ? Cast<UInteractiveToolPropertySet>(*SubToolProperties) : nullptr;
}

void SMetaHumanCharacterEditorBodyConformToolView::MakeToolView()
{
	if(ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->AddSlot()
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateConformToolViewWarningSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateConformToolViewImportSection()
			];
	}
}

void SMetaHumanCharacterEditorBodyConformToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	OnPreEditChangeProperty(PropertyAboutToChange, PropertyAboutToChange->GetName());
}

void SMetaHumanCharacterEditorBodyConformToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
	OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBodyConformToolView::CreateConformToolViewWarningSection()
{
	const TSharedRef<SWidget> WarningSectionWidget =
		SNew(SBox)
		.Padding(4.f)
		[
			SNew(SWarningOrErrorBox)
			.AutoWrapText(false)
			.MessageStyle(EMessageStyle::Warning)
			.Visibility(this, &SMetaHumanCharacterEditorBodyConformToolView::GetWarningVisibility)
			.Message_Lambda([this] { return GetWarning(); })		
		];

	return WarningSectionWidget;
}

EVisibility SMetaHumanCharacterEditorBodyConformToolView::GetWarningVisibility() const
{
	return EVisibility::Collapsed;
}

FText SMetaHumanCharacterEditorBodyConformToolView::GetWarning() const
{
	return FText::GetEmpty();
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBodyConformToolView::CreateConformToolViewImportSection()
{
	UMetaHumanCharacterImportBodySubToolBase* SubToolProperties = Cast<UMetaHumanCharacterImportBodySubToolBase>(GetToolProperties());
	if (!Tool.IsValid() || !SubToolProperties)
	{
		return SNullWidget::NullWidget;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(SubToolProperties);

	Tool.Pin()->OnPropertySetsModified.AddSP(this, &SMetaHumanCharacterEditorBodyConformToolView::OnPropertySetsModified);

	const TSharedRef<SWidget> ImportSectionWidget =
		SNew(SBorder)
		.Padding(-4.f)
		.BorderImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.ActiveToolLabel"))
		[
			SNew(SVerticalBox)

			// SubTool properties details view section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				DetailsView.ToSharedRef()
			]
		];

	return ImportSectionWidget;
}

void SMetaHumanCharacterEditorBodyConformToolView::OnPropertySetsModified()
{
	UMetaHumanCharacterImportBodySubToolBase* SubToolProperties = Cast<UMetaHumanCharacterImportBodySubToolBase>(GetToolProperties());
	if (DetailsView.IsValid() && IsValid(SubToolProperties))
	{
		constexpr bool bForceRefresh = true;
		DetailsView->SetObject(SubToolProperties, bForceRefresh);
		// clear any warning
		UMetaHumanCharacterEditorToolWithSubTools* OwnerTool = SubToolProperties->GetTypedOuter<UMetaHumanCharacterEditorToolWithSubTools>();
		if (OwnerTool)
		{
			OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
		}
	}
}

#undef LOCTEXT_NAMESPACE
