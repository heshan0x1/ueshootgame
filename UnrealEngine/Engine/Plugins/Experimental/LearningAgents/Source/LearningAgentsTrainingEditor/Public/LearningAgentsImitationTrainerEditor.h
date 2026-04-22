// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsImitationTrainer.h"
#include "LearningAgentsImitationTrainerEditor.generated.h"

#define UE_API LEARNINGAGENTSTRAININGEDITOR_API

/** Editor callable imitation learning trainer. */
UCLASS(BlueprintType, Blueprintable)
class ALearningAgentsImitationTrainerEditor : public AActor
{
	GENERATED_BODY()

public:
	UE_API ALearningAgentsImitationTrainerEditor();

	/** Setup the imitation trainer with necessary components in blueprints. */
	UFUNCTION(BlueprintImplementableEvent, Category = "LearningAgents")
	UE_API void SetupTraining();

	/** Start training. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API void StartTraining();

	/** Stop training. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API void StopTraining();

	/** Make File communicator to file training materials for external training. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API FLearningAgentsCommunicator MakeFileCommunicator(FDirectoryPath EditorIntermediateRelativePath);

	UE_API bool IsTraining() const;

	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	float TrainingTickInterval = 0.0f;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "LearningAgents")
    TObjectPtr<ULearningAgentsManager> LearningAgentsManager;

	UPROPERTY(BlueprintReadWrite, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsImitationTrainer> LearningAgentsImitationTrainer;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsRecording> Recording;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "LearningAgents")
	FLearningAgentsImitationTrainerSettings ImitationTrainerSettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "LearningAgents")
	FLearningAgentsImitationTrainerTrainingSettings ImitationTrainerTrainingSettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "LearningAgents")
	FLearningAgentsTrainerProcessSettings ImitationTrainerPathSettings;
};

#undef UE_API
