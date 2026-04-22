// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowContextEvaluator.h"
#include "Dataflow/DataflowNode.h"

namespace UE::Dataflow
{
	void FContextEvaluator::ScheduleNodeEvaluation(const FDataflowNode& Node, FOnPostEvaluationFunction OnPostEvaluation)
	{
		FEvaluationEntry Entry
		{
			.Id = FNodeId{ Node.GetGuid() },
			.WeakNode = Node.AsWeak(),
			.OnPostEvaluation = OnPostEvaluation,
		};
		ScheduleEvaluation(Entry);
		Process();
	}

	void FContextEvaluator::ScheduleOutputEvaluation(const FDataflowOutput& Output, FOnPostEvaluationFunction OnPostEvaluation)
	{
		if (const FDataflowNode* Node = Output.GetOwningNode())
		{
			ScheduleNodeEvaluation(*Node, OnPostEvaluation);
		}
	}

	void FContextEvaluator::ScheduleEvaluation(const FEvaluationEntry& Entry)
	{
		if (IsScheduledOrRunning(Entry.Id))
		{
			UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::ScheduleEvaluation : skipped [%s]"), *Entry.ToString());
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::ScheduleEvaluation : [%s]"), *Entry.ToString());
		PendingEvaluationEntries.Add(Entry.Id, Entry);

		// add upstream nodes
		if (TSharedPtr<const FDataflowNode> Node = Entry.WeakNode.Pin())
		{
			TArray<const FDataflowNode*> InvalidUpstreamNodes;
			FindInvalidUpstreamNodes(*Node, InvalidUpstreamNodes);
			for (const FDataflowNode* UpstreamNode: InvalidUpstreamNodes)
			{
				UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::ScheduleEvaluation :  [%s] -- Invalid Upstream Node [%s]"), *Entry.ToString(), *UpstreamNode->GetName().ToString());
			}
			for (const FDataflowNode* UpstreamNode : InvalidUpstreamNodes)
			{
				FEvaluationEntry UpstreamEntry
				{
					.Id = FNodeId{ UpstreamNode->GetGuid() },
					.WeakNode = UpstreamNode->AsWeak(),
					.OnPostEvaluation = {},
				};
				ScheduleEvaluation(UpstreamEntry);
			}
		}
	}

	bool FContextEvaluator::IsScheduledOrRunning(const FNodeId& Id) const
	{
		return (RunningTasks.Contains(Id) || PendingEvaluationEntries.Contains(Id));
	}

	void FContextEvaluator::Cancel()
	{
		PendingEvaluationEntries.Reset();
		CompletedTasks.Reset();
	}

	void FContextEvaluator::FindInvalidUpstreamNodes(const FDataflowNode& Node, TArray<const FDataflowNode*>& OutInvalidUpstreamNodes)
	{
		for (const FDataflowInput* Input : Node.GetInputs())
		{
			if (Input)
			{
				if (const FDataflowOutput* UpstreamOutput = Input->GetConnection())
				{
					UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::FindInvalidUpstreamOutputs :  [%s] input[%s] -> output [%s]"),
						*Node.GetName().ToString(),
						*Input->GetName().ToString(),
						*UpstreamOutput->GetName().ToString()
					);

					if (!UpstreamOutput->HasValidData(OwningContext))
					{
						if (const FDataflowNode* UpstreamNode = UpstreamOutput->GetOwningNode())
						{
							OutInvalidUpstreamNodes.AddUnique(UpstreamNode);
						}
					}
				}
			}
		}
	}

	bool FContextEvaluator::ShouldRunOnGameThread(const FDataflowNode& Node)
	{
		for (const FDataflowInput* Input : Node.GetInputs())
		{
			if (Input)
			{
				const FString InputTypeName = Input->GetType().ToString();
				// skeletal mesh and static mesh do support asynchronous loading and do not allow for accessing 
				// their property from elsewhere than the gamethread
				if (InputTypeName.Contains("UStaticMesh") || InputTypeName.Contains("USkeletalMesh"))
				{
					return true;
				}
			}
		}
		return Node.EvaluateOnGameThreadOnly();
	}

	bool FContextEvaluator::TryScheduleTask(const FEvaluationEntry& Entry)
	{
		if (TSharedPtr<const FDataflowNode> Node = Entry.WeakNode.Pin())
		{
			// todo(ccaillaud) : can optimize by findingthe first one
			TArray<const FDataflowNode*> InvalidUpstreamNodes;
			FindInvalidUpstreamNodes(*Node, InvalidUpstreamNodes);
			if (InvalidUpstreamNodes.IsEmpty())
			{
				ScheduleTask(Entry);
				return true;
			}
		}
		return false;
	}

	void FContextEvaluator::GetStats(int32& OutNumPendingTasks, int32& OutNumRunningTasks, int32& OutNumCompletedTasks) const
	{
		// ugly but we need to make sure the stats are up to date 
		const_cast<FContextEvaluator*>(this)->ClearCompletedTasks();

		OutNumPendingTasks = PendingEvaluationEntries.Num();
		OutNumRunningTasks = RunningTasks.Num();
		OutNumCompletedTasks = CompletedTasks.Num();
	}

	void FContextEvaluator::Process()
	{
		int32 NumScheduleTasks = 0;
		for (auto Iter = PendingEvaluationEntries.CreateIterator(); Iter; ++Iter)
		{
			if (TryScheduleTask(Iter->Value))
			{
				NumScheduleTasks++;
				Iter.RemoveCurrent();
			}
		}

		if (NumScheduleTasks == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::Process : No Task Scheduled NumPendingTasks=[%d]"), PendingEvaluationEntries.Num());
			for (const TPair<FNodeId, FEvaluationEntry>& Entry : PendingEvaluationEntries)
			{
				UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::Process : \t -[%s]"), *Entry.Value.ToString());
			}
		}

		ClearCompletedTasks();
	}

	void FContextEvaluator::ClearCompletedTasks()
	{
		for (auto Iter = RunningTasks.CreateIterator(); Iter; ++Iter)
		{
			const FGraphEventRef& Task = (*Iter).Value;
			const FNodeId TaskId = (*Iter).Key;
			if (Task->IsCompleted())
			{
				CompletedTasks.Add(TaskId);
				Iter.RemoveCurrent();
			}
		}
	}

	void FContextEvaluator::ScheduleTask(const FEvaluationEntry& Entry)
	{
		if (TSharedPtr<const FDataflowNode> Node = Entry.WeakNode.Pin())
		{
			FGraphEventArray ExistingTasks;
			if (FGraphEventRef* ExistingTask = RunningTasks.Find(Entry.Id))
			{ 
				ExistingTasks.Add(*ExistingTask);
			}

			const bool bUseGameThread = ShouldRunOnGameThread(*Node);

			UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::ScheduleTask : [%s] GameThread=[%d] previousTasks=[%d]"),
				*Entry.ToString(),
				(int32)bUseGameThread,
				(int32)ExistingTasks.Num()
			);

			FContext* ContextPtr = &OwningContext;
			FGraphEventRef NewTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[ContextPtr, Entry]
				{
					if (ContextPtr)
					{
						if (TSharedPtr<const FDataflowNode> Node = Entry.WeakNode.Pin())
						{
							Node->SetAsyncEvaluating(true);
							if (Node->NumOutputs() == 0)
							{
								Node->Evaluate(*ContextPtr, nullptr);
							}
							else
							{
								const TArray<FDataflowOutput*> Outputs = Node->GetOutputs();
								for (const FDataflowOutput* Output : Outputs)
								{
									// todo(ccaillaud) : should we check if the oputput is frozen?
									// todo(ccaillaud) : shoudl we have an option to skip the non connected outputs ? 
									if (!Output->HasValidData(*ContextPtr))
									{
										Node->Evaluate(*ContextPtr, Output);
									}
								}
							}
							Node->SetAsyncEvaluating(false);
							UE_LOG(LogTemp, Warning, TEXT("FContextEvaluator::EndTask : [%s]"), *Entry.ToString());
						}
					}
				},
				TStatId(),
				&ExistingTasks, /* prerequisites - make sure we wait on the previous one if any */
				bUseGameThread ? ENamedThreads::GameThread : ENamedThreads::AnyThread
			);

			auto OnFinishEvaluating = 
				[Evaluator = this, ContextPtr, OnPostEvaluation = Entry.OnPostEvaluation]()
				{
					Evaluator->Process();
					if (OnPostEvaluation.IsSet() && ContextPtr)
					{
						OnPostEvaluation(*ContextPtr);
					}
				};

			// handle post evaluation and run it on the game thread 
			NewTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
				OnFinishEvaluating,
				TStatId(),
				NewTask, /* prerequisites */
				ENamedThreads::GameThread
			);

			RunningTasks.Add(Entry.Id, NewTask);
		}
	}

	FString FContextEvaluator::FEvaluationEntry::ToString() const
	{
		static FName UnknownName("-Unknown-");
		FName NodeName = UnknownName;
		if (TSharedPtr<const FDataflowNode> Node = WeakNode.Pin())
		{
			NodeName = Node->GetName();
		}
		return NodeName.ToString();
	}
};

