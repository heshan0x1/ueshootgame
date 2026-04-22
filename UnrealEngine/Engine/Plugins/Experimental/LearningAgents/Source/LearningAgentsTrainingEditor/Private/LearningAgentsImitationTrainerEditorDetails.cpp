// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsImitationTrainerEditorDetails.h"
#include "LearningAgentsImitationTrainerEditor.h"
#include "LearningLog.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "Misc/Attribute.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "LearningAgentsImitationTrainerEditorDetails"

TSharedRef<IDetailCustomization> FLearningAgentsImitationTrainerEditorDetails::MakeInstance()
{
	return MakeShareable(new FLearningAgentsImitationTrainerEditorDetails);
}

void FLearningAgentsImitationTrainerEditorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.GetObjectsBeingCustomized(EditedObjects);
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("LearningAgents");

	Category.AddCustomRow(LOCTEXT("RunTrainingCategory", "Run Training"))
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("RunTrainingLabel", "Run Training"))
				.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
						.Text(LOCTEXT("RunTrainingButton", "Run"))
						.OnClicked(FOnClicked::CreateSP(this, &FLearningAgentsImitationTrainerEditorDetails::OnRunClicked))
						.IsEnabled_Lambda([this]() 
						{
							if (EditedObjects.Num() > 0)
							{
								if (ALearningAgentsImitationTrainerEditor* ImitationTrainerEditor = Cast<ALearningAgentsImitationTrainerEditor>(EditedObjects[0].Get()))
								{
									return !ImitationTrainerEditor->IsTraining();
								}
							}
							return false;
						})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
						.Text(LOCTEXT("StopTrainingButton", "Stop"))
						.OnClicked(FOnClicked::CreateSP(this, &FLearningAgentsImitationTrainerEditorDetails::OnStopClicked))
						.IsEnabled_Lambda([this]()
						{
							if (EditedObjects.Num() > 0)
							{
								if (ALearningAgentsImitationTrainerEditor* ImitationTrainerEditor = Cast<ALearningAgentsImitationTrainerEditor>(EditedObjects[0].Get()))
								{
									return ImitationTrainerEditor->IsTraining();
								}
							}
							return false;
						})
				]
		];
}

FReply FLearningAgentsImitationTrainerEditorDetails::OnRunClicked()
{
	if (EditedObjects.Num() > 0)
	{
		if (ALearningAgentsImitationTrainerEditor* ImitationTrainerEditor = Cast<ALearningAgentsImitationTrainerEditor>(EditedObjects[0].Get()))
		{
			ImitationTrainerEditor->StartTraining();
		}
	}
	return FReply::Handled();
}

FReply FLearningAgentsImitationTrainerEditorDetails::OnStopClicked()
{
	if (EditedObjects.Num() > 0)
	{
		if (ALearningAgentsImitationTrainerEditor* ImitationTrainerEditor = Cast<ALearningAgentsImitationTrainerEditor>(EditedObjects[0].Get()))
		{
			ImitationTrainerEditor->StopTraining();
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
