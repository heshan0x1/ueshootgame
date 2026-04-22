// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsImitationTrainerEditor.h"
#include "LearningExternalTrainer.h"
#include "LearningLog.h"
#include "Containers/Ticker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LearningAgentsImitationTrainerEditor)

ALearningAgentsImitationTrainerEditor::ALearningAgentsImitationTrainerEditor()
{
	LearningAgentsManager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));
}

void ALearningAgentsImitationTrainerEditor::StartTraining()
{
	SetupTraining();
	
	if (!LearningAgentsImitationTrainer)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Training has failed. Invalid imitation trainer. Was the imitation trainer set with SetupTraining?"), *GetName());
		return;
	}

	if (LearningAgentsImitationTrainer->IsTraining())
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Cannot start training when an existing training process is active!"), *GetName());
		return;
	}

	UE_LOG(LogLearning, Display, TEXT("%s: Starting training..."), *GetName());

	LearningAgentsImitationTrainer->BeginTraining(Recording, ImitationTrainerSettings, ImitationTrainerTrainingSettings, ImitationTrainerPathSettings);

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime)->bool {
			if (LearningAgentsImitationTrainer->IsTraining())
			{
				LearningAgentsImitationTrainer->IterateTraining();
				return true;
			}
			return false;
			}),
		TrainingTickInterval
	);
}

void ALearningAgentsImitationTrainerEditor::StopTraining()
{
	if (!LearningAgentsImitationTrainer)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Cannot stop training with an invalid imitation trainer."), *GetName());
		return;
	}
	if (!LearningAgentsImitationTrainer->IsTraining())
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: There is no training active. Cannot stop training when training has not started."), *GetName());
		return;
	}

	UE_LOG(LogLearning, Display, TEXT("%s: Ending training..."), *GetName());
	LearningAgentsImitationTrainer->EndTraining();
}

FLearningAgentsCommunicator ALearningAgentsImitationTrainerEditor::MakeFileCommunicator(FDirectoryPath EditorIntermediateRelativePath)
{
	FLearningAgentsCommunicator Communicator;
	Communicator.Trainer = MakeShared<UE::Learning::FFileTrainer>(UE::Learning::Trainer::GetIntermediatePath(EditorIntermediateRelativePath.Path));
	return Communicator;
}

bool ALearningAgentsImitationTrainerEditor::IsTraining() const
{
	if (LearningAgentsImitationTrainer)
	{
		return LearningAgentsImitationTrainer->IsTraining();
	}
	return false;
}
