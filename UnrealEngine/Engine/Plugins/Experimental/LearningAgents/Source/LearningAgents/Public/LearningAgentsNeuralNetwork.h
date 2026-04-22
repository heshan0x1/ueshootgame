// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#include "LearningAgentsNeuralNetwork.generated.h"

#define UE_API LEARNINGAGENTS_API

/** Activation functions for neural networks. */
UENUM(BlueprintType, Category = "LearningAgents")
enum class ELearningAgentsActivationFunction : uint8
{
	/** ReLU Activation - Fast to train and evaluate but occasionally causes gradient collapse and untrainable networks. */
	ReLU	UMETA(DisplayName = "ReLU"),

	/** ELU Activation - Generally performs better than ReLU and is not prone to gradient collapse but slower to evaluate. */
	ELU		UMETA(DisplayName = "ELU"),

	/** TanH Activation - Smooth activation function that is slower to train and evaluate but sometimes more stable for certain tasks. */
	TanH	UMETA(DisplayName = "TanH"),

	/** GELU Activation - Gaussian Error Linear Unit. */
	GELU	UMETA(DisplayName = "GELU"),
};

class ULearningNeuralNetworkData;

/** A neural network data asset. */
UCLASS(MinimalAPI, BlueprintType)
class ULearningAgentsNeuralNetwork : public UDataAsset
{
	GENERATED_BODY()

public:
	// These constructors/destructors are needed to make forward declarations happy
	UE_API ULearningAgentsNeuralNetwork();
	UE_API ULearningAgentsNeuralNetwork(FVTableHelper& Helper);
	UE_API virtual ~ULearningAgentsNeuralNetwork();

	/** Resets this network asset to be empty. */
	UFUNCTION(CallInEditor, Category = "LearningAgents")
	UE_API void ResetNetwork();

	/**
	 * Load this network from a snapshot.
	 * 
	 * @param File The snapshot file.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (RelativePath))
	UE_API void LoadNetworkFromSnapshot(const FFilePath& File);

	/**
	 * Save this network into a snapshot.
	 * 
	 * @param File The snapshot file.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (RelativePath))
	UE_API void SaveNetworkToSnapshot(const FFilePath& File);

	/**
	 * Copy another asset into this network.
	 * 
	 * @param NeuralNetworkAsset The asset to load from.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API void LoadNetworkFromAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset);

	/**
	 * Copy this network into another asset.
	 * 
	 * @param NeuralNetworkAsset The asset to save to.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	UE_API void SaveNetworkToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset);

public:

	/** Marks this asset as modified even during PIE */
	UE_API void ForceMarkDirty();

public:

	/** The internal Neural Network Data */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents");
	TObjectPtr<ULearningNeuralNetworkData> NeuralNetworkData;
};

#undef UE_API
