// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compute/PCGComputeKernel.h"

#include "PCGCountUniqueAttributeValuesKernel.generated.h"

namespace PCGCountUniqueAttributeValuesConstants
{
	const FName ValueAttributeName = TEXT("UniqueValue");
	const FName ValueCountAttributeName = TEXT("UniqueValueCount");
}

/**
* Counts how many unique values of a string key attribute are present in an input data collection. Output attribute set is a table of unique string key
* values and corresponding counts. Can output an attribute set per input data, or a single attribute set that counts across all data.
*/
UCLASS()
class UPCGCountUniqueAttributeValuesKernel : public UPCGComputeKernel
{
	GENERATED_BODY()

public:
	void SetAttributeName(FName InAttributeName) { AttributeName = InAttributeName; }

	/** Whether to produce one set of element counts that count across all input data, rather than producing counters per input data. */
	void SetEmitPerDataCounts(bool bInEmitPerDataCounts) { bEmitPerDataCounts = bInEmitPerDataCounts; }

	/** Whether to output a raw array of (string key value, instance count) values from analysis instead of constructing an attribute set. */
	void SetOutputRawBuffer(bool bInOutputRawBuffer) { bOutputRawBuffer = bInOutputRawBuffer; }

	virtual bool IsKernelDataValid(FPCGContext* InContext) const override;
	virtual TSharedPtr<const FPCGDataCollectionDesc> ComputeOutputBindingDataDesc(FName InOutputPinLabel, UPCGDataBinding* InBinding) const override;
	virtual int ComputeThreadCount(const UPCGDataBinding* Binding) const override;
	virtual bool DoesOutputPinRequireZeroInitialization(FName InOutputPinLabel) const override;
#if WITH_EDITOR
	virtual FString GetCookedSource(FPCGGPUCompilationContext& InOutContext) const override;
	virtual const TCHAR* GetSourceFilePath() const override { return TEXT("/Plugin/PCG/Private/BuiltInKernels/PCGCountUniqueAttributeValues.usf"); }
	virtual FString GetEntryPoint() const override { return TEXT("PCGCountUniqueAttributeValuesCS"); }
	void CreateAdditionalInputDataInterfaces(FPCGGPUCompilationContext& InOutContext, UObject* InObjectOuter, TArray<TObjectPtr<UComputeDataInterface>>& OutDataInterfaces) const override;
	// Split graph to read back analysis results.
	virtual bool SplitGraphAtOutput() const override { return true; }
#endif
	virtual void GetKernelAttributeKeys(TArray<FPCGKernelAttributeKey>& OutKeys) const override;

	virtual void GetInputPins(TArray<FPCGPinProperties>& OutPins) const override;
	virtual void GetOutputPins(TArray<FPCGPinPropertiesGPU>& OutPins) const override;

protected:
	UPROPERTY()
	FName AttributeName;

	/** Produce one set of element counts that count across all input data, rather than producing counters per input data. */
	UPROPERTY()
	bool bEmitPerDataCounts = true;

	UPROPERTY()
	bool bOutputRawBuffer = false;
};
