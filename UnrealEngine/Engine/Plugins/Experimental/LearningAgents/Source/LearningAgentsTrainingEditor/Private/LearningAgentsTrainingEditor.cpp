// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsTrainingEditor.h"
#include "LearningAgentsImitationTrainerEditor.h"
#include "LearningAgentsImitationTrainerEditorDetails.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Misc/CoreDelegates.h"

void FLearningAgentsTrainingEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		ALearningAgentsImitationTrainerEditor::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FLearningAgentsImitationTrainerEditorDetails::MakeInstance)
	);
}

void FLearningAgentsTrainingEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ALearningAgentsImitationTrainerEditor::StaticClass()->GetFName());
	}
}
	
IMPLEMENT_MODULE(FLearningAgentsTrainingEditorModule, LearningAgentsTrainingEditor)
