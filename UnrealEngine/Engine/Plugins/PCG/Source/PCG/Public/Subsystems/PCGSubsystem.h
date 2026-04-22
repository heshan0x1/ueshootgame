// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/IPCGBaseSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

#include "PCGCommon.h"
#include "Elements/PCGActorSelector.h"
#include "Grid/PCGGridDescriptor.h"
#include "Grid/PCGComponentOctree.h"
#include "Utils/PCGNodeVisualLogs.h"

#include "UObject/ObjectKey.h"

#include "PCGSubsystem.generated.h"

#define UE_API PCG_API

class APCGPartitionActor;
class APCGWorldActor;
class FPCGActorAndComponentMapping;
class FPCGGenSourceManager;
class FPCGRuntimeGenScheduler;
class UPCGComputeGraph;
class UPCGData;
class UPCGGraph;
class UPCGLandscapeCache;

enum class EPCGComponentDirtyFlag : uint8;
enum class ETickableTickType : uint8;

class IPCGGraphCache;
class FPCGGraphCompiler;
class FPCGGraphExecutor;
struct FPCGContext;
struct FPCGDataCollection;
struct FPCGScheduleGenericParams;
struct FPCGStack;
class UPCGSettings;

class IPCGElement;
typedef TSharedPtr<IPCGElement, ESPMode::ThreadSafe> FPCGElementPtr;

class UWorld;

#if WITH_EDITOR
/** Deprecated - use FPCGOnPCGComponentUnregistered */
DECLARE_MULTICAST_DELEGATE(FPCGOnComponentUnregistered);
/** Deprecated - use FPCGOnPCGSourceGenerationDone */
DECLARE_MULTICAST_DELEGATE_OneParam(FPCGOnComponentGenerationCompleteOrCancelled, UPCGSubsystem*);

DECLARE_MULTICAST_DELEGATE_OneParam(FPCGOnPCGComponentUnregistered, UPCGComponent*);

/** Deprecated - use FPCGOnPCGSourceGenerationDone */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FPCGOnPCGComponentGenerationDone, UPCGSubsystem*, UPCGComponent*, EPCGGenerationStatus);
#endif // WITH_EDITOR

/**
* UPCGSubsystem
*/
UCLASS(MinimalAPI)
class UPCGSubsystem : public UTickableWorldSubsystem, public IPCGBaseSubsystem
{
	GENERATED_BODY()

public:
	friend class UPCGComponent;
	friend FPCGActorAndComponentMapping;
	friend struct FPCGWorldPartitionBuilder;

	UE_API UPCGSubsystem();

	/** Add UObject references for GC */
	static PCG_API void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/** To be used when a PCG component can not have a world anymore, to unregister itself. */
	static UE_API UPCGSubsystem* GetSubsystemForCurrentWorld();

	virtual UWorld* GetSubsystemWorld() const override { return GetWorld(); }

	//~ Begin USubsystem Interface.
	UE_API virtual void Deinitialize() override;
	//~ End USubsystem Interface

	//~ Begin UWorldSubsystem Interface.
	UE_API virtual void PostInitialize() override;
	// need UpdateStreamingState? 
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject
	UE_API virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickableInEditor() const override { return true; }
	UE_API virtual ETickableTickType GetTickableTickType() const override;
	UE_API virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** Will return the subsystem from the World if it exists and if it is initialized */
	static UE_API UPCGSubsystem* GetInstance(UWorld* World);

	/** Adds an action that will be executed once at the beginning of this subsystem's next Tick(). */
	using FTickAction = TFunction<void()>;

	UE_DEPRECATED(5.6, "Use FPCGModule::ExecuteNextTick instead")
	UE_API void RegisterBeginTickAction(FTickAction&& Action);

#if WITH_EDITOR
	/** Returns PIE world if it is active, otherwise returns editor world. */
	static UE_API UPCGSubsystem* GetActiveEditorInstance();

	UE_API void SetConstructionScriptSourceComponent(UPCGComponent* InComponent);
	UE_API bool RemoveAndCopyConstructionScriptSourceComponent(AActor* InComponentOwner, FName InComponentName, UPCGComponent*& OutSourceComponent);
#endif

	UE_API APCGWorldActor* GetPCGWorldActor();
	UE_API APCGWorldActor* FindPCGWorldActor();

	/** Returns current quality level between Low (0) and Cinematic (4). */
	static UE_API int32 GetPCGQualityLevel();
	UE_API void OnPCGQualityLevelChanged();

#if WITH_EDITOR
	UE_API void DestroyAllPCGWorldActors();
	UE_API void DestroyCurrentPCGWorldActor();
	UE_API void LogAbnormalComponentStates(bool bGroupByState) const;
#endif
	UE_API void RegisterPCGWorldActor(APCGWorldActor* InActor);
	UE_API void UnregisterPCGWorldActor(APCGWorldActor* InActor);

	UE_API void OnOriginalComponentRegistered(UPCGComponent* InComponent);
	UE_API void OnOriginalComponentUnregistered(UPCGComponent* InComponent);

	UE_API UPCGLandscapeCache* GetLandscapeCache();

	/** Schedule graph(owner->graph) */
	UE_API FPCGTaskId ScheduleComponent(UPCGComponent* PCGComponent, EPCGHiGenGrid Grid, bool bForce, const TArray<FPCGTaskId>& InDependencies);

	/** Schedule cleanup(owner->graph). */
	UE_API FPCGTaskId ScheduleCleanup(UPCGComponent* PCGComponent, bool bRemoveComponents, const TArray<FPCGTaskId>& Dependencies);

	using IPCGBaseSubsystem::ScheduleGraph;
	
	UE_API FPCGTaskId ScheduleGraph(
		UPCGGraph* Graph,
		UPCGComponent* SourceComponent,
		FPCGElementPtr PreGraphElement,
		FPCGElementPtr InputElement,
		const TArray<FPCGTaskId>& Dependencies,
		const FPCGStack* InFromStack,
		bool bAllowHierarchicalGeneration);

	// Schedule graph (used internally for dynamic subgraph execution)
	UE_API FPCGTaskId ScheduleGraph(UPCGComponent* SourceComponent, const TArray<FPCGTaskId>& Dependencies);

	using IPCGBaseSubsystem::ScheduleGeneric;

	/** General job scheduling
	*  @param InOperation:               Callback that returns true if the task is done, false otherwise.
	*  @param SourceComponent:           PCG component associated with this task. Can be null.
	*  @param TaskExecutionDependencies: Task will wait on these tasks to execute and won't take their output data as input.
	*/
	UE_API FPCGTaskId ScheduleGeneric(TFunction<bool()> InOperation, UPCGComponent* SourceComponent, const TArray<FPCGTaskId>& TaskExecutionDependencies);

	/** General job scheduling
	*  @param InOperation:               Callback that returns true if the task is done, false otherwise.
	*  @param InAbortOperation:          Callback that will be called if the generic task is cancelled for any reason.
	*  @param SourceComponent:           PCG component associated with this task. Can be null.
	*  @param TaskExecutionDependencies: Task will wait on these tasks to execute and won't take their output data as input.
	*/
	UE_API FPCGTaskId ScheduleGeneric(TFunction<bool()> InOperation, TFunction<void()> InAbortOperation, UPCGComponent* SourceComponent, const TArray<FPCGTaskId>& TaskExecutionDependencies);

	/** General job scheduling with context
	*  @param InOperation:               Callback that takes a Context as argument and returns true if the task is done, false otherwise.
	*  @param SourceComponent:           PCG component associated with this task. Can be null.
	*  @param TaskExecutionDependencies: Task will wait on these tasks to execute and won't take their output data as input.
	*  @param TaskDataDependencies:      Task will wait on these tasks to execute and will take their output data as input.
	*/
	UE_API FPCGTaskId ScheduleGenericWithContext(TFunction<bool(FPCGContext*)> InOperation, UPCGComponent* SourceComponent, const TArray<FPCGTaskId>& TaskExecutionDependencies, const TArray<FPCGTaskId>& TaskDataDependencies);

	/** General job scheduling with context
	*  @param InOperation:               Callback that takes a Context as argument and returns true if the task is done, false otherwise.
	*  @param InAbortOperation:          Callback that will be called if the generic task is cancelled for any reason.
	*  @param SourceComponent:           PCG component associated with this task. Can be null.
	*  @param TaskExecutionDependencies: Task will wait on these tasks to execute and won't take their output data as input.
	*  @param TaskDataDependencies:      Task will wait on these tasks to execute and will take their output data as input.
	*/
	UE_API FPCGTaskId ScheduleGenericWithContext(TFunction<bool(FPCGContext*)> InOperation, TFunction<void(FPCGContext*)> InAbortOperation, UPCGComponent* SourceComponent, const TArray<FPCGTaskId>& TaskExecutionDependencies, const TArray<FPCGTaskId>& TaskDataDependencies);

	/** Asks the runtime generation scheduler to refresh a given GenerateAtRuntime component. ChangeType should be 'GenerationGrid' to perform a full cleanup of PAs and local components. */
	UE_API void RefreshRuntimeGenComponent(UPCGComponent* RuntimeComponent, EPCGChangeType ChangeType = EPCGChangeType::None);

	/** Asks the runtime generation scheduler to refresh all GenerateAtRuntime components. ChangeType should be 'GenerationGrid' to perform a full cleanup of PAs and local components. */
	UE_API void RefreshAllRuntimeGenComponents(EPCGChangeType ChangeType = EPCGChangeType::None);

#if WITH_EDITOR
	/** Refresh all components selected by the filter (runtime generated or otherwise). */
	UE_API void RefreshAllComponentsFiltered(const TFunction<bool(UPCGComponent*)>& ComponentFilter, EPCGChangeType ChangeType = EPCGChangeType::None);
#endif

	FPCGRuntimeGenScheduler* GetRuntimeGenScheduler() const { return RuntimeGenScheduler; }

	/** Register a new PCG Component or update it, will be added to the octree if it doesn't exists yet. Returns true if it was added/updated. Thread safe */
	UE_API bool RegisterOrUpdatePCGComponent(UPCGComponent* InComponent, bool bDoActorMapping = true);

	/** In case of BP Actors, we need to remap the old component destroyed by the construction script to the new one. Returns true if re-mapping succeeded. */
	UE_API bool RemapPCGComponent(const UPCGComponent* OldComponent, UPCGComponent* NewComponent, bool bDoActorMapping);

	/** Unregister a PCG Component, will be removed from the octree. Can force it, if we have a delayed unregister. Thread safe */
	UE_API void UnregisterPCGComponent(UPCGComponent* InComponent, bool bForce = false);

	/** Register a new Partition actor, will be added to a map and will query all intersecting volume to bind to them if asked. Thread safe */
	UE_API void RegisterPartitionActor(APCGPartitionActor* InActor);

	/** Unregister a Partition actor, will be removed from the map and remove itself to all intersecting volumes. Thread safe */
	UE_API void UnregisterPartitionActor(APCGPartitionActor* InActor);

	UE_API TSet<UPCGComponent*> GetAllRegisteredPartitionedComponents() const;
	UE_API TSet<UPCGComponent*> GetAllRegisteredComponents() const;

	/** Call the InFunc function to all local component registered to the original component. Thread safe*/
	UE_API void ForAllRegisteredLocalComponents(UPCGComponent* InOriginalComponent, const TFunctionRef<void(UPCGComponent*)>& InFunc) const;

	/** Call the InFunc function to all local component registered to the original component within some bounds. Thread safe*/
	UE_API void ForAllRegisteredIntersectingLocalComponents(UPCGComponent* InOriginalComponent, const FBoxCenterAndExtent& InBounds, const TFunctionRef<void(UPCGComponent*)>& InFunc) const;

	/** Get all components in specified bounds. */
	UE_API void ForAllIntersectingPartitionedComponents(const FBoxCenterAndExtent& InBounds, TFunctionRef<void(UPCGComponent*)> InFunc) const;

	/** Gather all the PCG components within some bounds. */
	UE_API TArray<UPCGComponent*> GetAllIntersectingComponents(const FBoxCenterAndExtent& InBounds) const;

	/** Traverses the hierarchy associated with the given component and calls InFunc for each overlapping component. */
	UE_API void ForAllOverlappingComponentsInHierarchy(UPCGComponent* InComponent, const TFunctionRef<void(UPCGComponent*)>& InFunc) const;

	/**
	 * Call InFunc to all partition grid cells matching 'InGridSizes' and overlapping with 'InBounds'. 'InFunc' can schedule work or execute immediately.
	 * 'InGridSizes' should be sorted in descending order. If 'bCanCreateActor' is true, it will create the partition actor at that cell if necessary.
	 */
	UE_DEPRECATED(5.5, "Use version with UPCGComponent")
	FPCGTaskId ForAllOverlappingCells(const FBox& InBounds, const PCGHiGenGrid::FSizeArray& InGridSizes, bool bCanCreateActor, const TArray<FPCGTaskId>& Dependencies, TFunctionRef<FPCGTaskId(APCGPartitionActor*, const FBox&)> InFunc) const { return InvalidPCGTaskId;  }

	UE_API FPCGTaskId ForAllOverlappingCells(
		UPCGComponent* InPCGComponent,
		const FBox& InBounds,
		const PCGHiGenGrid::FSizeArray& InGridSizes,
		bool bCanCreateActor,
		const TArray<FPCGTaskId>& Dependencies,
		TFunctionRef<FPCGTaskId(APCGPartitionActor*, const FBox&)> InFunc,
		TFunctionRef<FPCGTaskId(const FPCGGridDescriptor&, const FIntVector&, const FBox&)> InUnloadedFunc = [](const FPCGGridDescriptor&, const FIntVector&, const FBox&) { return InvalidPCGTaskId; }) const;
		
	/** Immediately cleanup the local components associated with an original component. */
	UE_API void CleanupLocalComponentsImmediate(UPCGComponent* InOriginalComponent, bool bRemoveComponents);

	UE_DEPRECATED(5.5, "Use FPCGGridDescriptor version")
	UE_API UPCGComponent* GetLocalComponent(uint32 GridSize, const FIntVector& CellCoords, const UPCGComponent* InOriginalComponent, bool bTransient = false) const;

	UE_DEPRECATED(5.5, "Use FPCGGridDescriptor version")
	UE_API APCGPartitionActor* GetRegisteredPCGPartitionActor(uint32 GridSize, const FIntVector& GridCoords, bool bRuntimeGenerated = false) const;

	UE_DEPRECATED(5.5, "Use FPCGGridDescriptor verison")
	UE_API APCGPartitionActor* FindOrCreatePCGPartitionActor(const FGuid& Guid, uint32 GridSize, const FIntVector& GridCoords, bool bRuntimeGenerated, bool bCanCreateActor = true) const;

	/** Retrieves a local component using grid descriptor and grid coordinates, returns nullptr if no such component is found. */
	UE_API UPCGComponent* GetLocalComponent(const FPCGGridDescriptor& GridDescriptor, const FIntVector& CellCoords, const UPCGComponent* InOriginalComponent) const;

	/** Retrieves a registered partition actor using grid size and grid coordinates, returns nullptr if no such partition actor is found. */
	UE_API APCGPartitionActor* GetRegisteredPCGPartitionActor(const FPCGGridDescriptor& GridDescriptor, const FIntVector& GridCoords) const;

	/** Creates a new partition actor if one does not already exist with the same grid size, coords, and generation mode. */
	UE_API APCGPartitionActor* FindOrCreatePCGPartitionActor(const FPCGGridDescriptor& GridDescriptor, const FIntVector& GridCoords, bool bCanCreateActor = true, bool bHideFromOutliner = false) const;
	
	/** Retrieves partition actors for this original component */
	UE_API TSet<TObjectPtr<APCGPartitionActor>> GetPCGComponentPartitionActorMappings(UPCGComponent* InComponent) const;

	UE_API FPCGGenSourceManager* GetGenSourceManager() const;

#if WITH_EDITOR
public:
	/** Schedule refresh on the current or next frame */
	UE_API FPCGTaskId ScheduleRefresh(UPCGComponent* SourceComponent, bool bForceRefresh);

	/** Immediately dirties the partition actors in the given bounds */
	UE_API void DirtyGraph(UPCGComponent* Component, const FBox& InBounds, EPCGComponentDirtyFlag DirtyFlag);

	/** Delete serialized partition actors in the level. If 'bOnlyDeleteUnused' is true, only PAs with no graph instances will be deleted. */
	UE_API void DeleteSerializedPartitionActors(bool bOnlyDeleteUnused, bool bOnlyChildren = false);

	/** Update the tracking on a given component. */
	UE_API void UpdateComponentTracking(UPCGComponent* InComponent, bool bShouldDirtyActors, const TArray<FPCGSelectionKey>* OptionalChangedKeys = nullptr);

	/** Propagates transient state change from an original component to the relevant partition actors */
	UE_API void PropagateEditingModeToLocalComponents(UPCGComponent* InOriginalComponent, EPCGEditorDirtyMode EditingMode);

	/** Move all resources from sub actors to a new actor */
	UE_API void ClearPCGLink(UPCGComponent* InComponent, const FBox& InBounds, AActor* InNewActor);

	/** If the partition grid size change, call this to empty the Partition actors map */
	UE_API void ResetPartitionActorsMap();

	/** Builds the landscape data cache. If force build is true, then it will build the cache even if it is never serialized. */
	UE_API void BuildLandscapeCache(bool bQuiet = false, bool bForceBuild = true);

	/** Clears the landscape data cache */
	UE_API void ClearLandscapeCache();

	/** Will gather all the components registered, and ask for generate. */
	UE_API void GenerateAllPCGComponents(bool bForce) const;

	/** Will gather all the components registered, and ask for cleanup. */
	UE_API void CleanupAllPCGComponents(bool bPurge) const;

	/** Notification that a Selection key needs refreshing */
	UE_API void NotifySelectionKeyChanged(const FPCGSelectionKey& InSelectionKey, const UObject* InOriginatingChangeObject, TArrayView<const FBox> InBounds);

	/** Get graph warnings and errors for all nodes. */
	UE_DEPRECATED(5.6, "Use IPCGEditorModule::GetNodeVisualLogs instead")
	UE_API const FPCGNodeVisualLogs& GetNodeVisualLogs() const;
	
	UE_DEPRECATED(5.6, "Use IPCGEditorModule::GetNodeVisualLogsMutable instead")
	UE_API FPCGNodeVisualLogs& GetNodeVisualLogsMutable();

	/** Notify that we exited the Landscape edit mode. */
	UE_API void NotifyLandscapeEditModeExited();

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API void ClearExecutionMetadata(UPCGComponent* InComponent);

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API void ClearExecutionMetadata(const FPCGStack& BaseStack);

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API void ClearExecutedStacks(const UPCGComponent* InRootComponent);

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API void ClearExecutedStacks(const UPCGGraph* InContainingGraph);

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API TArray<FPCGStack> GetExecutedStacks(const UPCGComponent* InComponent, const UPCGGraph* InSubgraph, bool bOnlyWithSubgraphAsCurrentFrame = true);

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API TArray<FPCGStack> GetExecutedStacks(const FPCGStack& BeginningWithStack);

	UE_DEPRECATED(5.6, "No longer supported")
	UE_API void ClearExecutedStacks(const FPCGStack& BeginningWithStack);

	UE_DEPRECATED(5.5, "Deprecated in favor of OnPCGComponentUnregistered, will not be notified anymore")
	FPCGOnComponentUnregistered OnComponentUnregistered;

	UE_DEPRECATED(5.5, "Deprecated in favor of OnPCGComponentGenerationDone, will not be notified anymore")
	FPCGOnComponentGenerationCompleteOrCancelled OnComponentGenerationCompleteOrCancelled;

	FPCGOnPCGComponentUnregistered OnPCGComponentUnregistered;
	
	UE_DEPRECATED(5.7, "Deprecated in favor of OnPCGSourceGenerationDone, will not be notified anymore")
	FPCGOnPCGComponentGenerationDone OnPCGComponentGenerationDone;

	UE_API void CreateMissingPartitionActors();
private:
	UE_API void OnPCGGraphCancelled(UPCGComponent* InComponent);
	UE_API void OnPCGGraphStartGenerating(UPCGComponent* InComponent);
	UE_API void OnPCGGraphGenerated(UPCGComponent* InComponent);
	UE_API void OnPCGGraphCleaned(UPCGComponent* InComponent);

	UE_API void CreatePartitionActorsWithinBounds(UPCGComponent* InComponent, const FBox& InBounds, const PCGHiGenGrid::FSizeArray& InGridSizes);
	UE_API void UpdateMappingPCGComponentPartitionActor(UPCGComponent* InComponent);

	UE_API void SetChainedDispatchToLocalComponents(bool bInChainedDispatch);

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& InOldToNewInstances);
#endif // WITH_EDITOR
	
private:
	/** When registering PAs, we might not have a PCG World Actor already registered (at runtime). In that case we'll look for a World Actor in the same level.*/
	UE_API APCGWorldActor* GetPCGWorldActorForPartitionActor(APCGPartitionActor* InActor);

	UPCGData* GetPCGData(FPCGTaskId InGraphExecutionTaskId, bool& bOutFound);
	UPCGData* GetInputPCGData(FPCGTaskId InGraphExecutionTaskId, bool& bOutFound);
	UPCGData* GetActorPCGData(FPCGTaskId InGraphExecutionTaskId, bool& bOutFound);
	UPCGData* GetLandscapePCGData(FPCGTaskId InGraphExecutionTaskId, bool& bOutFound);
	UPCGData* GetLandscapeHeightPCGData(FPCGTaskId InGraphExecutionTaskId, bool& bOutFound);
	UPCGData* GetOriginalActorPCGData(FPCGTaskId InGraphExecutionTaskId, bool& bOutFound);
	TOptional<FBox> GetBounds(FPCGTaskId InGraphExecutionTaskId);
	TOptional<FBox> GetOriginalBounds(FPCGTaskId InGraphExecutionTaskId);
	TOptional<FBox> GetLocalSpaceBounds(FPCGTaskId InGraphExecutionTaskId);
	TOptional<FBox> GetOriginalLocalSpaceBounds(FPCGTaskId InGraphExecutionTaskId);

	void SetPCGData(FPCGTaskId InGraphExecutionTaskId, UPCGData* InData);
	void SetInputPCGData(FPCGTaskId InGraphExecutionTaskId, UPCGData* InData);
	void SetActorPCGData(FPCGTaskId InGraphExecutionTaskId, UPCGData* InData);
	void SetLandscapePCGData(FPCGTaskId InGraphExecutionTaskId, UPCGData* InData);
	void SetLandscapeHeightPCGData(FPCGTaskId InGraphExecutionTaskId, UPCGData* InData);
	void SetOriginalActorPCGData(FPCGTaskId InGraphExecutionTaskId, UPCGData* InData);
	void SetBounds(FPCGTaskId InGraphExecutionTaskId, const FBox& InBounds);
	void SetOriginalBounds(FPCGTaskId InGraphExecutionTaskId, const FBox& InBounds);
	void SetLocalSpaceBounds(FPCGTaskId InGraphExecutionTaskId, const FBox& InBounds);
	void SetOriginalLocalSpaceBounds(FPCGTaskId InGraphExecutionTaskId, const FBox& InBounds);

	UE_API void ExecuteBeginTickActions();

	APCGWorldActor* PCGWorldActor = nullptr;
	FPCGRuntimeGenScheduler* RuntimeGenScheduler = nullptr;
	bool bHasTickedOnce = false;
	TUniquePtr<FPCGActorAndComponentMapping> ActorAndComponentMapping;

	/** Functions will be executed at the beginning of the tick and then removed from this array. */
	TArray<FTickAction> BeginTickActions;

	FCriticalSection PCGWorldActorLock;

#if WITH_EDITOR
	using FConstructionScriptSourceComponents = TMap<FName, TObjectKey<UPCGComponent>>;
	TMap<TObjectKey<AActor>, FConstructionScriptSourceComponents> PerActorConstructionScriptSourceComponents;

	static UE_API TSet<UWorld*> DisablePartitionActorCreationForWorld;

	// Used by UPCGWorldPartitonBuilder to disable PA creation while outside of a certain scope
	static void SetDisablePartitionActorCreationForWorld(UWorld* InWorld, bool bDisable) 
	{ 
		if (bDisable)
		{
			DisablePartitionActorCreationForWorld.Add(InWorld);
		}
		else
		{
			DisablePartitionActorCreationForWorld.Remove(InWorld);
		}
	}

	static bool IsPartitionActorCreationDisabledForWorld(UWorld* InWorld)
	{
		return DisablePartitionActorCreationForWorld.Contains(InWorld);
	}
#endif

protected:
	PCG_API virtual void CancelGenerationInternal(IPCGGraphExecutionSource* Source, bool bCleanupUnusedResources) override;
};

#undef UE_API
