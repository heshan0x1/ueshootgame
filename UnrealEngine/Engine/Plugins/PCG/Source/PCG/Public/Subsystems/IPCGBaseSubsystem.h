// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "PCGCommon.h"
#include "Elements/PCGActorSelector.h"
#include "Grid/PCGComponentOctree.h"
#include "Utils/PCGNodeVisualLogs.h"
#include "UObject/Interface.h"

#include "UObject/ObjectKey.h"

#include "IPCGBaseSubsystem.generated.h"

class UPCGComputeGraph;
class UPCGData;
class UPCGGraph;

class IPCGBaseSubsystem;
class IPCGElement;
class IPCGGraphCache;
class FPCGGraphCompiler;
class FPCGGraphExecutor;
struct FPCGDataCollection;
struct FPCGScheduleGenericParams;
struct FPCGStack;
class UPCGSettings;
class UWorld;

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_ThreeParams(FPCGOnPCGSourceGenerationDone, IPCGBaseSubsystem*, IPCGGraphExecutionSource*, EPCGGenerationStatus);
#endif // WITH_EDITOR

UINTERFACE()
class UPCGBaseSubsystem : public UInterface
{
	GENERATED_BODY()
};

class IPCGBaseSubsystem
{
	GENERATED_BODY()

public:
	virtual UWorld* GetSubsystemWorld() const { return nullptr; }

	PCG_API void InitializeBaseSubsystem();
	PCG_API void DeinitializeBaseSubsystem();
	
	/** Subsystem must not be used without this condition being true. */
	// @todo_pcg: Hides function in WorldSubsystem
	//bool IsInitialized() const { return GraphExecutor != nullptr; }

	/** Called by graph executor when a graph is scheduled. */
	PCG_API void OnScheduleGraph(const FPCGStackContext& StackContext);

	// Schedule graph (used internally for dynamic subgraph execution)
	PCG_API FPCGTaskId ScheduleGraph(const FPCGScheduleGraphParams& InParams);

	// General job scheduling
	PCG_API FPCGTaskId ScheduleGeneric(const FPCGScheduleGenericParams& InParams);
	
	/** Cancels currently running generation */
	PCG_API void CancelGeneration(IPCGGraphExecutionSource* Source);

	/** Cancels currently running generation on given graph */
	PCG_API void CancelGeneration(UPCGGraph* Graph);

	/** Returns true if there are any tasks for this graph currently scheduled or executing. */
	PCG_API bool IsGraphCurrentlyExecuting(UPCGGraph* Graph);

	/** Returns true if any task is scheduled or executing for any graph */
	PCG_API bool IsAnyGraphCurrentlyExecuting() const;

	/** Cancels everything running */
	PCG_API void CancelAllGeneration();

	/** Gets the output data for a given task */
	PCG_API bool GetOutputData(FPCGTaskId InTaskId, FPCGDataCollection& OutData);

	/** Clears the output data for a given task. Should only be called on tasks with bNeedsManualClear set to true */
	PCG_API void ClearOutputData(FPCGTaskId InTaskId);

	/** Returns the interface to the cache, required for element per-data caching */
	PCG_API IPCGGraphCache* GetCache();

	/** Flushes the graph cache completely, use only for debugging */
	PCG_API void FlushCache();

	/** True if graph cache debugging is enabled. */
	PCG_API bool IsGraphCacheDebuggingEnabled() const;
	
	PCG_API FPCGGraphCompiler* GetGraphCompiler();
	PCG_API UPCGComputeGraph* GetComputeGraph(const UPCGGraph* InGraph, uint32 GridSize, uint32 ComputeGraphIndex);

#if WITH_EDITOR
public:
	/** Propagate to the graph compiler graph changes */
	PCG_API virtual void NotifyGraphChanged(UPCGGraph* InGraph, EPCGChangeType ChangeType);

	/** Cleans up the graph cache on an element basis. InSettings is used for debugging and is optional. */
	PCG_API void CleanFromCache(const IPCGElement* InElement, const UPCGSettings* InSettings = nullptr);

	/** Gets the execution stack information for the given component (depending on partitioning, grid size, etc.) but with no component frames. */
	PCG_API bool GetStackContext(const IPCGGraphExecutionSource* InSource, FPCGStackContext& OutStackContext);

	/** Gets the base execution stack information for a specific graph & grid size. */
	PCG_API bool GetStackContext(UPCGGraph* InGraph, uint32 GridSize, bool bIsPartitioned, FPCGStackContext& OutStackContext);

	/** Returns how many times InElement is present in the cache. */
	PCG_API uint32 GetGraphCacheEntryCount(IPCGElement* InElement) const;
	
	PCG_API virtual void OnPCGSourceGenerationDone(IPCGGraphExecutionSource* InExecutionSource, EPCGGenerationStatus InStatus);

	FPCGOnPCGSourceGenerationDone& GetOnPCGSourceGenerationDone() { return OnPCGSourceGenerationDoneDelegate; }
#endif // WITH_EDITOR
	
protected:
	/** Returns the expected end time of the tick. By default, it's the current time + the budget of the graph executor. */
	PCG_API virtual double GetTickEndTime() const;

	/** Tick the graph executor. Return expected end time. */
	PCG_API double Tick();
	
	PCG_API void CancelGeneration(IPCGGraphExecutionSource* Source, bool bCleanupUnusedResources);
	
	virtual void CancelGenerationInternal(IPCGGraphExecutionSource* Source, bool bCleanupUnusedResources) {};
	
	TSharedPtr<FPCGGraphExecutor> GraphExecutor;

#if WITH_EDITOR
	PCG_API void SetDisableClearResults(bool bInDisableClearResults);

	FPCGOnPCGSourceGenerationDone OnPCGSourceGenerationDoneDelegate;
#endif
};
