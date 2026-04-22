// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"
#include "Utils/PCGExtraCapture.h"

#include "CoreMinimal.h"
#include "Misc/SpinLock.h"
#include "UObject/Interface.h"

#include "PCGGraphExecutionStateInterface.generated.h"

class IPCGBaseSubsystem;
class FPCGGraphExecutionInspection;
class IPCGGraphExecutionSource;
class UPCGData;
class UPCGGraph;
class UPCGGraphInstance;
class UPCGManagedResource;
struct FPCGGridDescriptor;

namespace PCGUtils
{
	class FExtraCapture;
}

using FPCGDynamicTrackingPriority = double;

/**
* Interface returned by a IPCGGraphExecutionSource that is queried / updated during execution of a PCG Graph.
*/
class IPCGGraphExecutionState
{
public:
	virtual ~IPCGGraphExecutionState() = default;

	/** Returns a UPCGData representation of the ExecutionState. */
	virtual UPCGData* GetSelfData() const = 0;

	/** Returns a Seed for graph execution. */
	virtual int32 GetSeed() const = 0;

	/** Returns a Debug name that can be used for logging. */
	virtual FString GetDebugName() const = 0;

	/** Returns a World, can be null */
	virtual UWorld* GetWorld() const = 0;

	/** Returns true if the ExecutionState has network authority */
	virtual bool HasAuthority() const = 0;

	/** Returns a Transform if the ExecutionState is a spatial one. */
	virtual FTransform GetTransform() const = 0;

	/** Returns a the original source Transform if the ExecutionState is a local source. */
	virtual FTransform GetOriginalTransform() const { return GetTransform(); }

	/** Returns the ExecutionState bounds if the ExecutionState is a spatial one. */
	virtual FBox GetBounds() const = 0;

	/** Returns the ExecutionState's original source bounds if this ExecutionState is a local source. */
	virtual FBox GetOriginalBounds() const { return GetBounds(); }

	/** Returns the ExecutionState's local bounds if this ExecutionState is a spatial one. */
	virtual FBox GetLocalSpaceBounds() const { return GetBounds(); }

	/** Returns the ExecutionState's original source local space bounds if this ExecutionState is a local source. */
	virtual FBox GetOriginalLocalSpaceBounds() const { return GetBounds(); }

	/** Returns the UPCGGraph this ExecutionState is executing. */
	virtual UPCGGraph* GetGraph() const = 0;

	/** Returns the UPCGGrahInstance this ExecutionState is executing. */
	virtual UPCGGraphInstance* GetGraphInstance() const = 0;

	/** Cancel execution of this ExecutionState. */
	virtual void Cancel() = 0;

	/** Notify ExecutionState that its execution is being aborted. */
	virtual void OnGraphExecutionAborted(bool bQuiet = false, bool bCleanupUnusedResources = true) = 0;

	/** Indicates if the current source is generating. */
	virtual bool IsGenerating() const = 0;

	/** Gets the task ID for the current generation task if it exists. */
	virtual FPCGTaskId GetGenerationTaskId() const { return InvalidPCGTaskId; }

	/** Requests the execution source to generate only on the specified grid level (all grid levels if EPCGHiGenGrid::Uninitialized). */
	virtual FPCGTaskId GenerateLocalGetTaskId(EPCGHiGenGrid Grid = EPCGHiGenGrid::Uninitialized) { return InvalidPCGTaskId; }

	/** Store data with a resource key that identifies the pin. */
	virtual void StoreOutputDataForPin(const FString& InResourceKey, const FPCGDataCollection& InData) const {}

	/** Lookup data using a resource key that identifies the pin. */
	virtual const FPCGDataCollection* RetrieveOutputDataForPin(const FString& InResourceKey) const { return nullptr; }

	virtual void AddToManagedResources(UPCGManagedResource* InResource);

	/** Returns the subsystem associated with this source. By default, it will return the UPCGSubsystem if the source has a world, otherwise, the EditorSubsystem (in editor only, obviously). */
	PCG_API virtual IPCGBaseSubsystem* GetSubsystem() const;

	/** Cache any data that is not thread safe or that needs to be computed only once for the whole execution */
	virtual void ExecutePreGraph(FPCGContext* InContext) {}

#if WITH_EDITOR

	/** Returns the FExtraCapture object for this ExecutionState */
	virtual const PCGUtils::FExtraCapture& GetExtraCapture() const = 0;

	virtual PCGUtils::FExtraCapture& GetExtraCapture() = 0;

	/** Returns the FPCGGraphExecutionInspection object for this ExecutionState */
	virtual const FPCGGraphExecutionInspection& GetInspection() const = 0;

	virtual FPCGGraphExecutionInspection& GetInspection() = 0;

	/** Get an execution priority */
	PCG_API virtual FPCGDynamicTrackingPriority GetDynamicTrackingPriority() const;

	/** Register tracking dependencies, so ExecutionState can be updated when they change */
	virtual void RegisterDynamicTracking(const UPCGSettings* InSettings, const TArrayView<TPair<FPCGSelectionKey, bool>>& InDynamicKeysAndCulling) = 0;

	/** Register multiple tracking dependencies, so ExecutionState can be updated when they change */
	virtual void RegisterDynamicTracking(const FPCGSelectionKeyToSettingsMap& InKeysToSettings) = 0;

	/** Indicates if the current source is waiting for a refresh */
	virtual bool IsRefreshInProgress() const = 0;

#endif

	/** If this is a local execution source returns the original execution source, otherwise returns self. */
	virtual IPCGGraphExecutionSource* GetOriginalSource() const = 0;

	/** Retrieves a local execution source using grid descriptor and grid coordinates, returns nullptr if no such execution source is found. */
	virtual IPCGGraphExecutionSource* GetLocalSource(const FPCGGridDescriptor& GridDescriptor, const FIntVector& CellCoords) const { return nullptr; }

	/** Returns true if this execution source uses partitioned generation. */
	virtual bool IsPartitioned() const { return false; }

	/** Returns true if this execution source is a local source of a partitioned execution source. */
	virtual bool IsLocalSource() const { return false; }

	// @todo_pcg: The execution source should probably just have an execution domain instead of directly querying runtime gen vs etc.
	/** Returns true if the execution source is managed by the runtime generation system. Nothing else should generate or cleanup this execution source. */
	virtual bool IsManagedByRuntimeGenSystem() const { return false; }

	/** Returns true if execution source should output on a 2D grid. */
	virtual bool Use2DGrid() const { return false; }

	/** Returns a GridDescriptor based on this execution source for the specified grid size. */
	virtual FPCGGridDescriptor GetGridDescriptor(uint32 InGridSize) const;

	/** Get the size for the HiGen grid this execution source executes on. */
	virtual uint32 GetGenerationGridSize() const { return PCGHiGenGrid::UninitializedGridSize(); }
};

UINTERFACE(BlueprintType, MinimalAPI)
class UPCGGraphExecutionSource : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

/**
* Interface used by the FPCGGraphExecutor to get an IPCGGraphExecutionState used to query/update execution.
*/
class IPCGGraphExecutionSource
{
	GENERATED_IINTERFACE_BODY()

public:
	virtual IPCGGraphExecutionState& GetExecutionState() = 0;
	virtual const IPCGGraphExecutionState& GetExecutionState() const = 0;
};

USTRUCT()
struct FPCGSoftGraphExecutionSource
{
	GENERATED_BODY()

	FPCGSoftGraphExecutionSource() = default;
	PCG_API explicit FPCGSoftGraphExecutionSource(const TSoftObjectPtr<UObject>& InSoftObjectPtr);
	PCG_API FPCGSoftGraphExecutionSource(IPCGGraphExecutionSource* InSource);
	PCG_API FPCGSoftGraphExecutionSource(const IPCGGraphExecutionSource* InSource);

	// Need the copy/move constructor/assignment because of the lock.
	PCG_API FPCGSoftGraphExecutionSource(const FPCGSoftGraphExecutionSource& Other);
	PCG_API FPCGSoftGraphExecutionSource(FPCGSoftGraphExecutionSource&& Other);
	PCG_API FPCGSoftGraphExecutionSource& operator=(const FPCGSoftGraphExecutionSource& Other);
	PCG_API FPCGSoftGraphExecutionSource& operator=(FPCGSoftGraphExecutionSource&& Other);

	/** Can assign an execution source. Will reset the cached weak pointer. */
	PCG_API FPCGSoftGraphExecutionSource& operator=(const IPCGGraphExecutionSource* InSource);

	/** De-reference the weak pointer if it is valid, otherwise will resolve the soft pointer and update the cached weak. Threadsafe. */
	PCG_API IPCGGraphExecutionSource* Get() const;

	/** Resolve the soft pointer as a UObject. */
	PCG_API UObject* GetObject() const;

	/** Reset the soft pointer to null and invalidate the cached weak. */
	PCG_API void Reset();
	
	bool IsValid() const { return Get() != nullptr; }
	
	IPCGGraphExecutionSource* operator->() const { return Get(); }

	PCG_API bool operator==(const FPCGSoftGraphExecutionSource& Other) const;

	PCG_API friend int32 GetTypeHash(const FPCGSoftGraphExecutionSource& This);
	
private:
	UPROPERTY()
	TSoftObjectPtr<UObject> SoftObjectPtr;

	mutable UE::FSpinLock SpinLock;
	mutable TWeakInterfacePtr<IPCGGraphExecutionSource> CachedWeakPtr;
};
