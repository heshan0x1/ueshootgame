// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"
#include "Async/AsyncWork.h"
#include "Async/StopToken.h"

#include <atomic>

class FAbortableAsyncTask
{
public:
	using FTaskFunction = TFunction<void(const FStopToken&)>;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAbortableAsyncTask(FTaskFunction InTaskFunction) :
		AsyncTask(MakeUnique<FAsyncTask<FAsyncTaskInternal>>(StopToken, MoveTemp(InTaskFunction)))
	{
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	~FAbortableAsyncTask()
	{
		Abort();
		AsyncTask->EnsureCompletion();
	}

	FAbortableAsyncTask(const FAbortableAsyncTask&) = delete;
	FAbortableAsyncTask& operator=(const FAbortableAsyncTask&) = delete;
	FAbortableAsyncTask(FAbortableAsyncTask&&) = delete;
	FAbortableAsyncTask& operator=(FAbortableAsyncTask&&) = delete;

	bool IsDone()
	{
		return AsyncTask->IsDone();
	}

	void StartSync()
	{
		AsyncTask->StartSynchronousTask();
	}

	void StartAsync()
	{
		AsyncTask->StartBackgroundTask();
	}

	void Abort()
	{
		StopToken.RequestStop();
	}

private:
	class FAsyncTaskInternal : public FNonAbandonableTask
	{
	public:
		FAsyncTaskInternal(const FStopToken& InStopToken, FTaskFunction&& InTaskFunction) :
			StopToken(InStopToken), TaskFunction(MoveTemp(InTaskFunction))
		{
			check(TaskFunction != nullptr);
		}

		void DoWork()
		{
			TaskFunction(StopToken);
		}

		inline TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FAbortableAsyncTask_FAsyncTaskInternal, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		const FStopToken& StopToken;
		FTaskFunction TaskFunction;
	};

	FStopToken StopToken;
	TUniquePtr<FAsyncTask<FAsyncTaskInternal>> AsyncTask;
};
