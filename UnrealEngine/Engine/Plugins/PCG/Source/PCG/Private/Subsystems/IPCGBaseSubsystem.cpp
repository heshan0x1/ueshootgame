// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/IPCGBaseSubsystem.h"

#include "PCGActorAndComponentMapping.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGModule.h"
#include "Graph/PCGGraphCache.h"
#include "Graph/PCGGraphCompiler.h"
#include "Graph/PCGGraphExecutor.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "RuntimeGen/PCGGenSourceManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "PackageSourceControlHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#endif // WITH_EDITOR

#include UE_INLINE_GENERATED_CPP_BY_NAME(IPCGBaseSubsystem)

void IPCGBaseSubsystem::InitializeBaseSubsystem()
{
	check(!GraphExecutor);
	GraphExecutor = MakeShared<FPCGGraphExecutor>(GetSubsystemWorld());

#if WITH_EDITOR
	FPCGModule::GetPCGModuleChecked().OnGraphChanged().AddRaw(this, &IPCGBaseSubsystem::NotifyGraphChanged);
#endif
}

void IPCGBaseSubsystem::DeinitializeBaseSubsystem()
{
#if WITH_EDITOR
	FPCGModule::GetPCGModuleChecked().OnGraphChanged().RemoveAll(this);
#endif

	GraphExecutor = nullptr;
}

#if WITH_EDITOR
void IPCGBaseSubsystem::OnScheduleGraph(const FPCGStackContext& StackContext)
{
	// nothing to do for now
}
#endif

FPCGTaskId IPCGBaseSubsystem::ScheduleGraph(const FPCGScheduleGraphParams& InParams)
{
	if (InParams.ExecutionSource)
	{
		return GraphExecutor->ScheduleGraph(InParams);
	}
	else
	{
		return InvalidPCGTaskId;
	}
}

FPCGTaskId IPCGBaseSubsystem::ScheduleGeneric(const FPCGScheduleGenericParams& InParams)
{
	check(GraphExecutor);
	return GraphExecutor->ScheduleGeneric(InParams);
}

void IPCGBaseSubsystem::CancelGeneration(IPCGGraphExecutionSource* Source)
{
	CancelGeneration(Source, /*bCleanupUnusedResources=*/true);
}

double IPCGBaseSubsystem::GetTickEndTime() const
{
	return FPlatformTime::Seconds() + FPCGGraphExecutor::GetTickBudgetInSeconds();
}

double IPCGBaseSubsystem::Tick()
{
	double EndTime = GetTickEndTime();

	// If we have any tasks to execute, schedule some
	if (GraphExecutor)
	{
		GraphExecutor->Execute(EndTime);
	}

	return EndTime;
}

void IPCGBaseSubsystem::CancelGeneration(IPCGGraphExecutionSource* Source, bool bCleanupUnusedResources)
{
	check(GraphExecutor && IsInGameThread());
	if (!Source || !Source->GetExecutionState().IsGenerating())
	{
		return;
	}

	CancelGenerationInternal(Source, bCleanupUnusedResources);

	TArray<IPCGGraphExecutionSource*> CancelledExecutionSources = GraphExecutor->Cancel(Source);
	for (IPCGGraphExecutionSource* CancelledExecutionSource : CancelledExecutionSources)
	{
		if (CancelledExecutionSource)
		{
			CancelledExecutionSource->GetExecutionState().OnGraphExecutionAborted(/*bQuiet=*/true, bCleanupUnusedResources);
		}
	}	
}

void IPCGBaseSubsystem::CancelGeneration(UPCGGraph* Graph)
{
	check(GraphExecutor);

	if (!Graph)
	{
		return;
	}

	TArray<IPCGGraphExecutionSource*> CancelledExecutionSources = GraphExecutor->Cancel(Graph);
	for (IPCGGraphExecutionSource* CancelledExecutionSource : CancelledExecutionSources)
	{
		if (ensure(CancelledExecutionSource))
		{
			CancelledExecutionSource->GetExecutionState().OnGraphExecutionAborted(/*bQuiet=*/true);
		}
	}
}

void IPCGBaseSubsystem::CancelAllGeneration()
{
	check(GraphExecutor);

	TArray<IPCGGraphExecutionSource*> CancelledExecutionSources = GraphExecutor->CancelAll();
	for (IPCGGraphExecutionSource* CancelledExecutionSource : CancelledExecutionSources)
	{
		if (ensure(CancelledExecutionSource))
		{
			CancelledExecutionSource->GetExecutionState().OnGraphExecutionAborted(/*bQuiet=*/true);
		}
	}
}

bool IPCGBaseSubsystem::IsGraphCurrentlyExecuting(UPCGGraph* Graph)
{
	check(GraphExecutor);

	if (!Graph)
	{
		return false;
	}

	return GraphExecutor->IsGraphCurrentlyExecuting(Graph);
}

bool IPCGBaseSubsystem::IsAnyGraphCurrentlyExecuting() const
{
	return GraphExecutor && GraphExecutor->IsAnyGraphCurrentlyExecuting();
}

bool IPCGBaseSubsystem::IsGraphCacheDebuggingEnabled() const
{
	return GraphExecutor && GraphExecutor->IsGraphCacheDebuggingEnabled();
}

FPCGGraphCompiler* IPCGBaseSubsystem::GetGraphCompiler()
{
	return GraphExecutor ? GraphExecutor->GetCompiler() : nullptr;
}

UPCGComputeGraph* IPCGBaseSubsystem::GetComputeGraph(const UPCGGraph* InGraph, uint32 GridSize, uint32 ComputeGraphIndex)
{
	if (FPCGGraphCompiler* GraphCompiler = GetGraphCompiler())
	{
		return GraphCompiler->GetComputeGraph(InGraph, GridSize, ComputeGraphIndex);
	}

	return nullptr;
}

bool IPCGBaseSubsystem::GetOutputData(FPCGTaskId TaskId, FPCGDataCollection& OutData)
{
	check(GraphExecutor);
	return GraphExecutor->GetOutputData(TaskId, OutData);
}

void IPCGBaseSubsystem::ClearOutputData(FPCGTaskId TaskId)
{
	check(GraphExecutor);
	GraphExecutor->ClearOutputData(TaskId);
}

#if WITH_EDITOR
void IPCGBaseSubsystem::NotifyGraphChanged(UPCGGraph* InGraph, EPCGChangeType ChangeType)
{
	if (GraphExecutor)
	{
		GraphExecutor->NotifyGraphChanged(InGraph, ChangeType);
	}
}

void IPCGBaseSubsystem::CleanFromCache(const IPCGElement* InElement, const UPCGSettings* InSettings/*= nullptr*/)
{
	if (GraphExecutor)
	{
		GraphExecutor->GetCache().CleanFromCache(InElement, InSettings);
	}
}

bool IPCGBaseSubsystem::GetStackContext(UPCGGraph* InGraph, uint32 InGridSize, bool bIsPartitioned, FPCGStackContext& OutStackContext)
{
	if (!InGraph)
	{
		return false;
	}

	// A non-partitioned component generally executes (original component or local component).
	if(bIsPartitioned)
	{
		// A partitioned higen original component will execute if the graph has UB grid level.
		if (InGraph->IsHierarchicalGenerationEnabled())
		{
			PCGHiGenGrid::FSizeArray GridSizes;
			bool bHasUnbounded = false;
			InGraph->GetGridSizes(GridSizes, bHasUnbounded);

			if (!bHasUnbounded)
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	if (FPCGGraphCompiler* GraphCompiler = GetGraphCompiler())
	{
		GraphCompiler->GetCompiledTasks(InGraph, InGridSize, OutStackContext, /*bIsTopGraph=*/false);
		return true;
	}
	else
	{
		return false;
	}
}

bool IPCGBaseSubsystem::GetStackContext(const IPCGGraphExecutionSource* InSource, FPCGStackContext& OutStackContext)
{
	// @todo_pcg: Extend execution state.
	const UPCGComponent* Component = Cast<UPCGComponent>(InSource);
	const bool bIsPartitioned = Component && Component->IsPartitioned();
	const uint32 GridSize = (!Component || bIsPartitioned) ?  PCGHiGenGrid::UnboundedGridSize() : Component->GetGenerationGridSize();
	
	return InSource && GetStackContext(InSource->GetExecutionState().GetGraph(),GridSize, bIsPartitioned, OutStackContext);
}

uint32 IPCGBaseSubsystem::GetGraphCacheEntryCount(IPCGElement* InElement) const
{
	return GraphExecutor ? GraphExecutor->GetGraphCacheEntryCount(InElement) : 0;
}

void IPCGBaseSubsystem::OnPCGSourceGenerationDone(IPCGGraphExecutionSource* InExecutionSource, EPCGGenerationStatus InStatus)
{
	OnPCGSourceGenerationDoneDelegate.Broadcast(this, InExecutionSource, InStatus);
}

void IPCGBaseSubsystem::SetDisableClearResults(bool bInDisableClearResults)
{
	if (GraphExecutor)
	{
		GraphExecutor->SetDisableClearResults(bInDisableClearResults);
	}
}

#endif // WITH_EDITOR

IPCGGraphCache* IPCGBaseSubsystem::GetCache()
{
	return GraphExecutor ? &(GraphExecutor->GetCache()) : nullptr;
}

void IPCGBaseSubsystem::FlushCache()
{
	if (GraphExecutor && GraphExecutor->GetCompiler())
	{
		GraphExecutor->GetCache().ClearCache();
		GraphExecutor->GetCompiler()->ClearCache();
	}

#if WITH_EDITOR
	// Garbage collection is very seldom run in the editor, but we currently can consume a lot of memory in the cache.
	const UWorld* World = GetSubsystemWorld();
	if (!World || !World->IsGameWorld())
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);
	}
#endif
}
