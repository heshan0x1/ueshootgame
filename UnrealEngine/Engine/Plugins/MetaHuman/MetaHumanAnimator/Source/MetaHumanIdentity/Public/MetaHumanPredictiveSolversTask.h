// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "UObject/WeakObjectPtr.h"

#define UE_API METAHUMANIDENTITY_API

/**
 * Predictive solvers configuration.
 */
struct FPredictiveSolversTaskConfig
{
	FString TemplateDescriptionJson;
	FString ConfigurationJson;
	TWeakObjectPtr<class UDNAAsset> DNAAsset;
	TArray<uint8> PredictiveSolverGlobalTeethTrainingData;
	TArray<uint8> PredictiveSolverTrainingData;
	bool bTrainPreviewSolvers = true;
};

/**
 * Predictive solvers training result.
 */
struct FPredictiveSolversResult
{
	TArray<uint8> PredictiveWithoutTeethSolver;
	TArray<uint8> PredictiveSolvers;
	bool bSuccess = false;
};

/**
 * Predictive solvers worker that actually does calculations.
 */
class FPredictiveSolversWorker : public FNonAbandonableTask
{
public:
	using SolverProgressFunc = TFunction<void(float)>;
	using SolverCompletedFunc = TFunction<void(void)>;

	FPredictiveSolversWorker(bool bInIsAsync, const FPredictiveSolversTaskConfig& InConfig, SolverProgressFunc InOnProgress, SolverCompletedFunc InOnCompleted, std::atomic<bool>& bInIsCancelled, std::atomic<float>& InProgress);

	inline TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPredictiveSolversWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

	void DoWork();

private:
	friend class FPredictiveSolversTask;
	void RunTraining();

	bool bIsAsync = true;
	FPredictiveSolversTaskConfig Config;
	SolverProgressFunc OnProgress;
	SolverCompletedFunc OnCompleted;
	std::atomic<bool>& bIsCancelled;
	std::atomic<float>& Progress;
	float LastProgress = 0.0f;
	std::atomic<bool> bIsDone = false;
	FPredictiveSolversResult Result;
};

DECLARE_DELEGATE_OneParam(FOnPredictiveSolversCompleted, const FPredictiveSolversResult&);
DECLARE_DELEGATE_OneParam(FOnPredictiveSolversProgress, float);

/**
 * Predictive solver task that creates new worker for predictive solver calculations.
 */
class FPredictiveSolversTask
{
public:
	UE_API FPredictiveSolversTask(const FPredictiveSolversTaskConfig& InConfig);

	UE_API FPredictiveSolversResult StartSync();
	UE_API void StartAsync();

	/** Only triggered when task is executed asynchronously */
	UE_API FOnPredictiveSolversCompleted& OnCompletedCallback();
	UE_API FOnPredictiveSolversProgress& OnProgressCallback();

	UE_API bool IsDone() const;
	UE_API bool WasCancelled() const;

	UE_API void Cancel();
	UE_API void Stop();

	/** Obtains current training progress in range [0..1]. Return true if it was successful (task is still active), false otherwise */
	UE_API bool PollProgress(float& OutProgress) const;

private:
	FPredictiveSolversTaskConfig Config;
	TUniquePtr<FAsyncTask<class FPredictiveSolversWorker>> Task;
	std::atomic<bool> bCancelled = false;
	std::atomic<float> Progress = 0.0f;
	std::atomic<bool> bSkipCallback = false;
	FOnPredictiveSolversCompleted OnCompletedDelegate;
	FOnPredictiveSolversProgress OnProgressDelegate;

	UE_API void OnProgress_Thread(float InProgress);
	UE_API void OnCompleted_Thread();
};

/**
 * Singleton responsible for managing and owning predictive solver tasks.
 */
class FPredictiveSolversTaskManager
{
public:
	static UE_API FPredictiveSolversTaskManager& Get();

	FPredictiveSolversTaskManager() = default;
	~FPredictiveSolversTaskManager() = default;

	FPredictiveSolversTaskManager(FPredictiveSolversTaskManager const&) = delete;
	FPredictiveSolversTaskManager& operator=(const FPredictiveSolversTaskManager&) = delete;

	/** Creates new solver tasks and adds to the list of active tasks */
	UE_API FPredictiveSolversTask* New(const FPredictiveSolversTaskConfig& InConfig);

	/** Removes given task from the list and nullifies the reference */
	UE_API bool Remove(FPredictiveSolversTask*& InOutTask);

	/** Stops all active tasks */
	UE_API void StopAll();

private:
	TArray<TUniquePtr<FPredictiveSolversTask>> Tasks;
};

#undef UE_API
