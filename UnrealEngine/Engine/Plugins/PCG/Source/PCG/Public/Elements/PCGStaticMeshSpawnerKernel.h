// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compute/PCGComputeKernel.h"

#include "PCGStaticMeshSpawnerKernel.generated.h"

class UPCGStaticMeshSpawnerSettings;

namespace PCGStaticMeshSpawnerKernelConstants
{
	const FName InstanceCountsPinLabel = TEXT("InstanceCounts");
}

UCLASS()
class UPCGStaticMeshSpawnerKernel : public UPCGComputeKernel
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<const FPCGDataCollectionDesc> ComputeOutputBindingDataDesc(FName InOutputPinLabel, UPCGDataBinding* InBinding) const override;
	virtual int ComputeThreadCount(const UPCGDataBinding* Binding) const override;
#if WITH_EDITOR
	virtual const TCHAR* GetSourceFilePath() const override { return TEXT("/Plugin/PCG/Private/Elements/PCGStaticMeshSpawner.usf"); }
	virtual FString GetEntryPoint() const override { return TEXT("PCGStaticMeshSpawnerCS"); }
	void CreateAdditionalInputDataInterfaces(FPCGGPUCompilationContext& InOutContext, UObject* InObjectOuter, TArray<TObjectPtr<UComputeDataInterface>>& OutDataInterfaces) const override;
	void CreateAdditionalOutputDataInterfaces(FPCGGPUCompilationContext& InOutContext, UObject* InObjectOuter, TArray<TObjectPtr<UComputeDataInterface>>& OutDataInterfaces) const override;
#endif
	virtual void AddStaticCreatedStrings(TArray<FString>& InOutStringTable) const override;
	virtual void GetKernelAttributeKeys(TArray<FPCGKernelAttributeKey>& OutKeys) const override;

	virtual void GetInputPins(TArray<FPCGPinProperties>& OutPins) const override;
	virtual void GetOutputPins(TArray<FPCGPinPropertiesGPU>& OutPins) const override;

	void SetUseRawInstanceCountsBuffer(bool bInUseRawInstanceCountsBuffer) { bUseRawInstanceCountsBuffer = bInUseRawInstanceCountsBuffer; }

protected:
#if WITH_EDITOR
	virtual bool PerformStaticValidation() override;
#endif

protected:
	/** Consume the instance counts as a raw buffer instead of an attribute set. */
	UPROPERTY()
	bool bUseRawInstanceCountsBuffer = false;
};
