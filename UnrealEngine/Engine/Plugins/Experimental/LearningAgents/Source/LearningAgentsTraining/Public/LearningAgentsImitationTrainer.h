// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsManagerListener.h"
#include "LearningAgentsTrainer.h"

#include "GameplayTagContainer.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsImitationTrainer.generated.h"

#define UE_API LEARNINGAGENTSTRAINING_API

namespace UE::Learning
{
	struct FReplayBuffer;
	struct IExternalTrainer;
}

class ULearningAgentsInteractor;
class ULearningAgentsPolicy;
class ULearningAgentsRecording;

/** The configurable settings for a ULearningAgentsImitationTrainer. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsImitationTrainerSettings
{
	GENERATED_BODY()

public:

	/** Time in seconds to wait for the training process before timing out. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TrainerCommunicationTimeout = 10.0f;

	/** Tag used to filter records from the recording. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "LearningAgents")
	FGameplayTag RecordingFilterTag;

};

/** The configurable settings for the training process. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsImitationTrainerTrainingSettings
{
	GENERATED_BODY()

public:

	/** The number of iterations to run before ending training. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 NumberOfIterations = 1000000;

	/** Learning rate of the policy network. Typical values are between 0.001 and 0.0001. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float LearningRate = 0.001f;

	/** Amount by which to multiply the learning rate every time it decays. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float LearningRateDecay = 1.0f;

	/** The number of iterations to train before updating the learning rate. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 LearningRateDecayStepSize = 1000;

	/**
	 * Amount of weight decay to apply to the network. Larger values encourage network weights to be smaller but too
	 * large a value can cause the network weights to collapse to all zeros.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float WeightDecay = 0.0001f;

	/**
	 * Batch size to use for training. Smaller values tend to produce better results at the cost of slowing down
	 * training. Large batch sizes are much more computationally efficient when training on the GPU.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1", UIMax = "4096"))
	uint32 BatchSize = 128;

	/**
	 * The number of consecutive steps of observations and actions over which to train the policy. Increasing this value will encourage the policy to use its memory 
	 * effectively. Too large and training can become unstable. Given we don't know the memory state during imitation learning it is better this is 
	 * slightly larger than when we are doing reinforcement learning.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1", UIMax = "512"))
	uint32 Window = 64;

	/**
	 * Weight used to regularize actions. Larger values will encourage smaller actions but too large will cause actions to become always zero.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionRegularizationWeight = 0.001f;

	/**
	 * Weighting used for the entropy bonus. Larger values encourage larger action noise and therefore greater exploration but can make actions very 
	 * noisy.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionEntropyWeight = 0.0f;

	/**
	 * A multiplicative scaling factor that controls the observation noise that increases the perturbations added to observations.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ObservationNoiseScale = 0.0f;

	/** The seed used for any random sampling the trainer will perform, e.g. for weight initialization. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 RandomSeed = 1234;

	/** The device to train on. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsTrainingDevice Device = ELearningAgentsTrainingDevice::CPU;

	/**
	 * If true, TensorBoard logs will be emitted to the intermediate directory.
	 *
	 * TensorBoard will only work if it is installed in Unreal Engine's python environment. This can be done by
	 * enabling the "Tensorboard" plugin in your project.
	 */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseTensorboard = false;

	/** If true, snapshots of the trained networks will be emitted to the intermediate directory. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bSaveSnapshots = false;

	/** If bSaveSnapshots is true, the snapshots will be saved at an interval defined by the specified number of iterations. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 IterationsPerSnapshot = 1000;

	/** If true, MLflow will be used for experiment tracking. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseMLflow = false;

	/** The URI of the MLflow Tracking Server to log to. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	FString MLflowTrackingUri = "";

	/** Should evaluation run during the training process. Currently not used in Python */
	UPROPERTY(VisibleDefaultsOnly, Category = "LearningAgents")
	bool bRunEvaluation = true;

	/** How much data should be used for evaluation. Currently not used in Python */
	UPROPERTY(VisibleDefaultsOnly, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0", EditCondition = "bRunEvaluation"))
	float TrainEvalDatasetSplit = 0.1f;

	/** How many training iteration loops between an evaluation run. Currently not used in Python */
	UPROPERTY(VisibleDefaultsOnly, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0", EditCondition = "bRunEvaluation"))
	int32 EvaluationFrequency = 50;

	/** How many batches to perform evaluation on? Randomly chosen each evaluation. Currently not used in Python */
	UPROPERTY(VisibleDefaultsOnly, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0", EditCondition = "bRunEvaluation"))
	int32 BatchCountPerEvaluation = 10;


	UE_API TSharedRef<FJsonObject> AsJsonConfig() const;
};

/**
 * The ULearningAgentsImitationTrainer enable imitation learning, i.e. learning from human/AI demonstrations.
 * Imitation training is typically much faster than reinforcement learning, but requires gathering large amounts of
 * data in order to generalize. This can be used to initialize a reinforcement learning policy to speed up initial
 * exploration.
 * @see ULearningAgentsInteractor to understand how observations and actions work.
 * @see ULearningAgentsController to understand how we can manually perform actions via a human or AI.
 * @see ULearningAgentsRecorder to understand how to make new recordings.
 */
UCLASS(MinimalAPI, BlueprintType, Blueprintable)
class ULearningAgentsImitationTrainer : public ULearningAgentsManagerListener
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	UE_API ULearningAgentsImitationTrainer();
	UE_API ULearningAgentsImitationTrainer(FVTableHelper& Helper);
	UE_API virtual ~ULearningAgentsImitationTrainer();

	/** Will automatically call EndTraining if training is still in-progress when the object is destroyed. */
	UE_API virtual void BeginDestroy() override;

	/**
	 * Constructs the imitation trainer and runs the setup functions.
	 * 
	 * @param InManager			The agent manager we are using.
	 * @param InInteractor		The agent interactor we are recording with.
	 * @param InPolicy			The policy we are using.
	 * @param Communicator		The communicator.
	 * @param Class				The trainer class.
	 * @param Name				The trainer name.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (Class = "/Script/LearningAgentsTraining.LearningAgentsImitationTrainer", DeterminesOutputType = "Class"))
	static UE_API ULearningAgentsImitationTrainer* MakeImitationTrainer(
		UPARAM(ref) ULearningAgentsManager*& InManager,
		UPARAM(ref) ULearningAgentsInteractor*& InInteractor,
		UPARAM(ref) ULearningAgentsPolicy*& InPolicy,
		const FLearningAgentsCommunicator& Communicator,
		TSubclassOf<ULearningAgentsImitationTrainer> Class,
		const FName Name = TEXT("ImitationTrainer"));

	/**
	 * Initializes the imitation trainer and runs the setup functions.
	 * 
	 * @param InManager			The agent manager we are using.
	 * @param InInteractor		The agent interactor we are recording with.
	 * @param InPolicy			The policy we are using.
	 * @param InCommunicator	The communicator.
	 */
	UE_API void SetupImitationTrainer(
		ULearningAgentsManager* InManager,
		ULearningAgentsInteractor* InInteractor,
		ULearningAgentsPolicy* InPolicy,
		const FLearningAgentsCommunicator& Communicator);

	/** Returns true if the trainer is currently training; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	UE_API bool IsTraining() const;

	/**
	 * Returns true if the trainer has failed to communicate with the external training process. This can be used in
	 * combination with RunTraining to avoid filling the logs with errors.
	 *
	 * @returns				True if the training has failed. Otherwise, false.
	 */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	UE_API bool HasTrainingFailed() const;

	/**
	 * Begins the training process with the provided settings.
	 * 
	 * @param Recording							The data to train on.
	 * @param ImitationTrainerSettings			The settings for this trainer.
	 * @param ImitationTrainerTrainingSettings	The training settings for this network.
	 * @param ImitationTrainerPathSettings		The path settings used by the imitation trainer.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AutoCreateRefTerm = "ImitationTrainerSettings,ImitationTrainerTrainingSettings,ImitationTrainerPathSettings"))
	UE_API void BeginTraining(
		const ULearningAgentsRecording* Recording,
		const FLearningAgentsImitationTrainerSettings& ImitationTrainerSettings = FLearningAgentsImitationTrainerSettings(),
		const FLearningAgentsImitationTrainerTrainingSettings& ImitationTrainerTrainingSettings = FLearningAgentsImitationTrainerTrainingSettings(),
		const FLearningAgentsTrainerProcessSettings& ImitationTrainerPathSettings = FLearningAgentsTrainerProcessSettings());

	/** Stops the training process. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API void EndTraining();

	/** Iterates the training process and gets the updated policy network. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API void IterateTraining();

	/**
	 * Convenience function that runs a basic training loop. If training has not been started, it will start it. On 
	 * each following call to this function, it will call IterateTraining.
	 * 
	 * @param Recording							The data to train on.
	 * @param ImitationTrainerSettings			The settings for this trainer.
	 * @param ImitationTrainerTrainingSettings	The training settings for this network.
	 * @param ImitationTrainerPathSettings		The path settings used by the imitation trainer.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AutoCreateRefTerm = "ImitationTrainerSettings,ImitationTrainerTrainingSettings,ImitationTrainerPathSettings"))
	UE_API void RunTraining(
		const ULearningAgentsRecording* Recording,
		const FLearningAgentsImitationTrainerSettings& ImitationTrainerSettings = FLearningAgentsImitationTrainerSettings(),
		const FLearningAgentsImitationTrainerTrainingSettings& ImitationTrainerTrainingSettings = FLearningAgentsImitationTrainerTrainingSettings(),
		const FLearningAgentsTrainerProcessSettings& ImitationTrainerPathSettings = FLearningAgentsTrainerProcessSettings());

// ----- Private Data -----
private:

	/** The interactor being trained. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsInteractor> Interactor;

	/** The policy being trained. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsPolicy> Policy;

	/** True if training is currently in-progress. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	bool bIsTraining = false;

	/**
	 * True if trainer encountered an unrecoverable error during training (e.g. the trainer process timed out). Otherwise, false.
	 * This exists mainly to keep the editor from locking up if something goes wrong during training.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	bool bHasTrainingFailed = false;

	UE_API TSharedRef<FJsonObject> CreateDataConfig() const;
	UE_API TSharedRef<FJsonObject> CreateTrainerConfig(const FLearningAgentsImitationTrainerTrainingSettings& TrainingSettings) const;
	UE_API void SendConfigs(const TSharedRef<FJsonObject>& DataConfigObject, const TSharedRef<FJsonObject>& TrainerConfigObject);

	UE_API void DoneTraining();

	TUniquePtr<UE::Learning::FReplayBuffer> ReplayBuffer;
	TSharedPtr<UE::Learning::IExternalTrainer> Trainer;

	int32 PolicyNetworkId = INDEX_NONE;
	int32 EncoderNetworkId = INDEX_NONE;
	int32 DecoderNetworkId = INDEX_NONE;

	int32 ReplayBufferId = INDEX_NONE;

	int32 ObservationId = INDEX_NONE;
	int32 ActionId = INDEX_NONE;
	int32 MemoryStateId = INDEX_NONE;
};

#undef UE_API
