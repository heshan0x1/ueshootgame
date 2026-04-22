// Copyright Epic Games, Inc. All Rights Reserved.
#include "SPVExportSelectionDialog.h"
#include "Widgets/Layout/SSeparator.h"
#include "SPrimaryButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Editor.h"
#include "SPVExportItem.h"
#include "ProceduralVegetation.h"
#include "PVEditorSettings.h"
#include "Nodes/PCGEditorGraphNodeBase.h"
#include "Nodes/PVOutputSettings.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SPVExportSelectionDialog"

void SPVExportSelectionDialog::Construct(const FArguments& InArgs)
{
	Graph = InArgs._Graph;
	SelectedNodes = InArgs._SelectedNodes;

	BuildExportTypeOptions();
	
	SWindow::Construct(SWindow::FArguments()
		.Title(InArgs._Title)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.ClientSize(FVector2D(350, 450))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2, 2, 2, 0)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(2, 10, 20, 10)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PVExportSelectionDialog_ExportTypeLabel", "Export type"))
					]
					
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Fill)
					[
						SNew(STextComboBox)
						.OptionsSource(&ExportTypes)
						.InitiallySelectedItem(CurrentExportType)
						.OnSelectionChanged(this, &SPVExportSelectionDialog::OnExportTypeChanged)
					]
				]
			]
			+ SVerticalBox::Slot() // Add user input block
			.Padding(2, 2, 2, 4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1)
					[
						SAssignNew(ScrollBox,SScrollBox)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(8.f, 16.f)
			[
				SNew(SUniformGridPanel)
				.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				.SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SAssignNew(ExportButton, SPrimaryButton)
					.Text(LOCTEXT("Export", "Export"))
					.OnClicked(this, &SPVExportSelectionDialog::OnButtonClick, EAppReturnType::Ok)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked(this, &SPVExportSelectionDialog::OnButtonClick, EAppReturnType::Cancel)
				]
			]
		]);

	AddExportItems();
}

EPVExportType SPVExportSelectionDialog::GetExportType()
{
	EPVExportType ExportType = EPVExportType::Selection;
	
	if (ExportTypes.Contains(CurrentExportType))
	{
		int SelectionIndex = ExportTypes.IndexOfByKey(CurrentExportType);
		ExportType = (EPVExportType)SelectionIndex;
	}

	return ExportType;
}

void SPVExportSelectionDialog::AddExportItems()
{
	ScrollBox->ClearChildren();
	
	if (GetExportType() == EPVExportType::BatchExport)
	{
		Graph->ForEachNode([this](UPCGNode* Node)
		{
			const UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(Node->GetSettings());

			if (OutputSettings)
			{
				FText OutputName = Node->GetNodeTitle(EPCGNodeTitleType::FullTitle);
				
				ScrollBox->AddSlot()
				.Padding(5)
				[
					SNew(SPVExportItem)
					.Name(OutputName)
					.OnOutputSelectionChanged(this, &SPVExportSelectionDialog::OnOutputSelectionChanged)
					.bIsSelected(OutputSettings->ExportSettings.bShouldExport)
				];
				ScrollBox->AddSlot()
				[
					SNew(SSeparator)
					.Thickness(1)
				];
			}

			return true;
		});
	}
	else
	{
		FString OutputName;

		if (SelectedNodes.IsEmpty())
		{
			OutputName = "None";
		}

		for (int i = 0; i < SelectedNodes.Num(); i++)
		{
			if (const UPCGNode* SelectedNode = SelectedNodes[i] ? SelectedNodes[i]->GetPCGNode() : nullptr)
			{
				if (const UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(SelectedNode->GetSettings()))
				{
					if (OutputName.IsEmpty())
					{
						OutputName = FString::Printf(TEXT("%s"), *SelectedNode->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());
					}
					else
					{
						OutputName = FString::Printf(TEXT("%s\n%s"), *OutputName,*SelectedNode->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());
					}
				}	
			}
		}
		
		FText Outputs = FText::FromString(FString::Printf(TEXT("%s"), *OutputName));
			
		ScrollBox->AddSlot()
		.Padding(5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Current outputs(s): "))))	
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Outputs)	
			]
		];
	}

	UpdateExportButtonState();
}
FReply SPVExportSelectionDialog::OnButtonClick(EAppReturnType::Type ButtonID)
{
	UserResponse = ButtonID;
	if (ButtonID == EAppReturnType::Cancel || ButtonID == EAppReturnType::Ok)
	{
		// Only close the window if canceling or if the ok
		RequestDestroyWindow();
	}
	else
	{
		// reset the user response in case the window is closed using 'x'.
		UserResponse = EAppReturnType::Cancel;
	}
	return FReply::Handled();
}

void SPVExportSelectionDialog::OnExportTypeChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (!Selection->IsEmpty())
	{
		const int SelectionIndex = ExportTypes.IndexOfByKey(Selection);

		if (ExportTypes.IsValidIndex(SelectionIndex))
		{
			CurrentExportType = ExportTypes[SelectionIndex];
		
			UPVEditorSettings* EditorSettings = GetMutableDefault<UPVEditorSettings>();
			EditorSettings->ExportType = (EPVExportType)SelectionIndex;

			AddExportItems();	
		}
	}
}

void SPVExportSelectionDialog::UpdateExportButtonState()
{
	bool HasValidExport = false;

	if (GetExportType() == EPVExportType::BatchExport)
	{
		if (Graph)
		{
			Graph->ForEachNode([this, &HasValidExport](UPCGNode* Node)
			{
				if (UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(Node->GetSettings()))
				{
					HasValidExport |= OutputSettings->ExportSettings.bShouldExport;
				}
		
				return true;
			});	
		}	
	}
	else
	{
		for (int i = 0; i < SelectedNodes.Num(); i++)
		{
			if (const UPCGNode* SelectedNode = SelectedNodes[i] ? SelectedNodes[i]->GetPCGNode() : nullptr)
			{
				const UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(SelectedNode->GetSettings());

				if (OutputSettings)
				{
					HasValidExport = true;
				}
			}
		}
	}

	ExportButton->SetEnabled(HasValidExport);
	
}

EAppReturnType::Type SPVExportSelectionDialog::ShowModal()
{
	GEditor->EditorAddModalWindow(SharedThis(this));
	return UserResponse;
}
void SPVExportSelectionDialog::OnOutputSelectionChanged(const FString ItemName, ECheckBoxState NewState)
{
	Graph->ForEachNode([this, &ItemName, &NewState](UPCGNode* Node)
	{
		if(ItemName == Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString())
		{
			if (UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(Node->GetSettings()))
			{
				OutputSettings->ExportSettings.bShouldExport = NewState ==ECheckBoxState::Checked;
			}
		}
		
		return true;
	});

	UpdateExportButtonState();
}

void SPVExportSelectionDialog::BuildExportTypeOptions()
{
	const UEnum* EnumPtr = StaticEnum<EPVExportType>();
	                      
	if (EnumPtr)
	{
		for (int32 i = 0; i < EnumPtr->NumEnums() - 1; i++)
		{
			FText ModeName = EnumPtr->GetDisplayNameTextByIndex(i);
			ExportTypes.Add(MakeShared<FString>(ModeName.ToString()));
		}
	}

	if (ExportTypes.Num() > 0)
	{
		const UPVEditorSettings* EditorSettings = GetDefault<UPVEditorSettings>();
		const int ExportTypeIndex = (int)EditorSettings->ExportType; 
		
		if (ExportTypeIndex >= 0 && ExportTypeIndex < ExportTypes.Num())
		{
			CurrentExportType = ExportTypes[ExportTypeIndex];
		}
	}
}
#undef LOCTEXT_NAMESPACE
