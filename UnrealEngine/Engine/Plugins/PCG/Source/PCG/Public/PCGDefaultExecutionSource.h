// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGGraphExecutionInspection.h"
#include "PCGGraphExecutionStateInterface.h"

#include "PCGDefaultExecutionSource.generated.h"

class UPCGDefaultExecutionSource;
class UPCGGraph;
class UPCGGraphInstance;
class UPCGGraphInterface;

class FPCGDefaultExecutionState : public IPCGGraphExecutionState
{
public:
	FPCGDefaultExecutionState() = default;
	explicit FPCGDefaultExecutionState(UPCGDefaultExecutionSource* InSource) : Source(InSource){}
	
	PCG_API virtual UPCGData* GetSelfData() const override;
	PCG_API virtual int32 GetSeed() const override;
	PCG_API virtual FString GetDebugName() const override;
	PCG_API virtual FTransform GetTransform() const override;
	PCG_API virtual UWorld* GetWorld() const override;
	PCG_API virtual bool HasAuthority() const override;
	PCG_API virtual FBox GetBounds() const override;
	PCG_API virtual UPCGGraph* GetGraph() const override;
	PCG_API virtual UPCGGraphInstance* GetGraphInstance() const override;
	PCG_API virtual void OnGraphExecutionAborted(bool bQuiet, bool bCleanupUnusedResources) override;
	PCG_API virtual void Cancel() override;
	PCG_API virtual bool IsGenerating() const override;
	PCG_API virtual IPCGGraphExecutionSource* GetOriginalSource() const override;

#if WITH_EDITOR
	PCG_API virtual const PCGUtils::FExtraCapture& GetExtraCapture() const override;
	PCG_API virtual PCGUtils::FExtraCapture& GetExtraCapture() override;

	PCG_API virtual const FPCGGraphExecutionInspection& GetInspection() const override;
	PCG_API virtual FPCGGraphExecutionInspection& GetInspection() override;

	// No dynamic tracking for editor resources (at the moment)
	virtual void RegisterDynamicTracking(const UPCGSettings* InSettings, const TArrayView<TPair<FPCGSelectionKey, bool>>& InDynamicKeysAndCulling) override {};
	virtual void RegisterDynamicTracking(const FPCGSelectionKeyToSettingsMap& InKeysToSettings) override {};
	
	virtual bool IsRefreshInProgress() const override {return IsGenerating();}

	virtual FPCGDynamicTrackingPriority GetDynamicTrackingPriority() const override { return FPCGDynamicTrackingPriority(); }
#endif // WITH_EDITOR

private:
	UPCGDefaultExecutionSource* Source = nullptr;
};

struct FPCGDefaultExecutionSourceParams
{
	UPCGGraphInterface* GraphInterface = nullptr;
	int32 Seed = 42;
};

UCLASS(MinimalAPI)
class UPCGDefaultExecutionSource : public UObject, public IPCGGraphExecutionSource
{
	friend FPCGDefaultExecutionState;
	
	GENERATED_BODY()
	
public:
	PCG_API UPCGDefaultExecutionSource();
	PCG_API ~UPCGDefaultExecutionSource();

	PCG_API void Initialize(const FPCGDefaultExecutionSourceParams& InParams);

	PCG_API static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	
	virtual IPCGGraphExecutionState& GetExecutionState() override { return State; }
	virtual const IPCGGraphExecutionState& GetExecutionState() const override { return State; }

	PCG_API void SetGraphInterface(UPCGGraphInterface* InGraphInterface);

	UPCGGraphInterface* GetGraphInterface() { return GraphInterface.Get(); }
	PCG_API UPCGGraphInstance* GetGraphInstance();
	PCG_API UPCGGraph* GetGraph();

	PCG_API void SetSeed(int32 InSeed);

#if WITH_EDITOR
	PCG_API void OnGraphChanged(UPCGGraphInterface* GraphInterface, EPCGChangeType ChangeType);
#endif // WITH_EDITOR
	
	PCG_API void Generate();

	PCG_API void Sunset();

private:
	FPCGTaskId CurrentGenerationTask = InvalidPCGTaskId;
	
	UPROPERTY()
	TObjectPtr<UPCGGraphInterface> GraphInterface;
	
	FPCGDefaultExecutionState State;
	int32 Seed = 42;

#if WITH_EDITOR
	PCGUtils::FExtraCapture ExtraCapture;
	FPCGGraphExecutionInspection Inspection;
#endif // WITH_EDITOR
};