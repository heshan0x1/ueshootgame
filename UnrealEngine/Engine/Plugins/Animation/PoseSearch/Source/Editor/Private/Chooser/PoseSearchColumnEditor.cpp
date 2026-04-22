// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchColumnEditor.h"
#include "Animation/AnimationAsset.h"
#include "ChooserColumnHeader.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "ObjectChooserWidgetFactories.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "ScopedTransaction.h"
#include "SPropertyAccessChainWidget.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "PoseSearchColumnEditor"

namespace UE::PoseSearchEditor
{

TSharedRef<SWidget> CreatePoseSearchColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int RowIndex)
{
	FPoseSearchColumn* PoseSearchColumn = static_cast<FPoseSearchColumn*>(Column);
	
	if (RowIndex == UE::ChooserEditor::ColumnWidget_SpecialIndex_Fallback)
	{
		return SNullWidget::NullWidget;
	}
	else if (RowIndex == UE::ChooserEditor::ColumnWidget_SpecialIndex_Header)
	{
		// @todo: avoid reopening the asset picker after tyhe first user choice, or prevent the FPoseSearchColumn creation if no schemas has been selected!
		if (!PoseSearchColumn->GetDatabaseSchema())
		{
			TSharedPtr<SWindow> PickerWindow;

			// asset picker config to select Schema
			FAssetPickerConfig AssetPickerConfig;
			AssetPickerConfig.Filter.ClassPaths.Add(UPoseSearchSchema::StaticClass()->GetClassPathName());
			AssetPickerConfig.Filter.bRecursiveClasses = true;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([PoseSearchColumn, &PickerWindow](const FAssetData& SelectedAsset)
				{
					if (UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(SelectedAsset.GetAsset()))
					{
						FScopedTransaction ScopedTransaction(LOCTEXT("AssignPoseSearchColumnDatabaseSchema", "Assign Pose Search Column Database Schema"));
						PoseSearchColumn->SetDatabaseSchema(Schema);
					}
					PickerWindow->RequestDestroyWindow();
				});

			// Load the content browser module to display an asset picker
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			PickerWindow = SNew(SWindow)
				.Title(LOCTEXT("SelectPoseSearchColumnDatabaseSchema", "Pick Schema for owned Pose Search Database"))
				.ClientSize(FVector2D(500, 600))
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("Menu.Background"))
						[
							ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
						]
				];
		
			GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
			PickerWindow.Reset();
		}

		// create column header widget
		const FSlateBrush* ColumnIcon = FAppStyle::Get().GetBrush("Icons.Search");
		const FText ColumnTooltip = LOCTEXT("Pose Match Tooltip", "Pose Match: Selects a single result based on the animation with the best matching pose, and outputs the StartTime for the frame with that pose. Animation Assets must contain \"Pose Match Branch In\" Notify State. AutoPopulate will fill in Column data with result Animation Assets.");
		const FText ColumnName = LOCTEXT("Pose Match","Pose Match");
        		
		TSharedPtr<SWidget> DebugWidget = nullptr;

		return UE::ChooserEditor::MakeColumnHeaderWidget(Chooser, Column, ColumnName, ColumnTooltip, ColumnIcon, DebugWidget);
	}

	// create cell widget
	return
		SNew(STextBlock)
			.Text_Lambda([RowIndex, PoseSearchColumn]()
			{
				if (const UObject* Asset = PoseSearchColumn->GetDatabaseAsset(RowIndex))
				{
					return FText::FromString(Asset->GetName());
				}
				return FText();
			});
}

void RegisterPoseSearchChooserWidgets()
{
	UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FPoseSearchColumn::StaticStruct(), CreatePoseSearchColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
