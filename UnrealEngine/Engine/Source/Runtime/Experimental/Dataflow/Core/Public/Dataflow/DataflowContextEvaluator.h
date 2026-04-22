// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Array.h"
#include "Misc/Guid.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

struct FDataflowNode;
struct FDataflowOutput;

namespace UE::Dataflow
{
	class FContext;

	using FOnPostEvaluationFunction = TFunction<void(FContext&)>;

	/*
	* Asynchronously evaluate a dataflow nodes 
	* This may be slower than executing the graph in one go synchronously but this offers the following advantage:
	* - this can be cancelled at anytime ( only the in progress node will have finish evaluating while all the pending ones will be discarded )
	* - nodes that need to run on the game thread will do so ( see FDataflowNode EvaluateOnGameThreadOnly method )
	*/
	struct FContextEvaluator
	{
	public:
		using FNodeId = FGuid;

		struct FEvaluationEntry
		{
			FNodeId Id;
			TWeakPtr<const FDataflowNode> WeakNode;
			FOnPostEvaluationFunction OnPostEvaluation;

			bool operator == (const FEvaluationEntry& Other) const
			{
				return Id == Other.Id;
			}
			bool operator == (const FNodeId& Other) const
			{
				return (Id == Other);
			}

			FString ToString() const;
		};

		FContextEvaluator(FContext& InOwningContext)
			: OwningContext(InOwningContext)
		{}

		void ScheduleNodeEvaluation(const FDataflowNode& Node, FOnPostEvaluationFunction OnPostEvaluation);
		void ScheduleOutputEvaluation(const FDataflowOutput& Output, FOnPostEvaluationFunction OnPostEvaluation);

		void Process();
		void Cancel();
		void GetStats(int32& OutNumPendingTasks, int32& OutNumRunningTasks, int32& OutNumCompletedTasks) const;

		int32 GetNumPendingTasks() const { return PendingEvaluationEntries.Num(); }
		int32 GetNumRunningTasks() const { return RunningTasks.Num(); }
		int32 GetNumCompletedTasks() const { return CompletedTasks.Num(); }

	private:
		bool IsScheduledOrRunning(const FNodeId& Id) const;

		void FindInvalidUpstreamNodes(const FDataflowNode& Node, TArray<const FDataflowNode*>& OutInvalidUpstreamNodes);
		bool ShouldRunOnGameThread(const FDataflowNode& Node);
		void ScheduleEvaluation(const FEvaluationEntry& Entry);

		bool TryScheduleTask(const FEvaluationEntry& Entry);
		void ScheduleTask(const FEvaluationEntry& Entry);
		void ClearCompletedTasks();

		FContext& OwningContext;
		TMap<FNodeId, FEvaluationEntry> PendingEvaluationEntries;
		TMap<FNodeId, FGraphEventRef> RunningTasks;
		TSet<FNodeId> CompletedTasks;
	};
}


