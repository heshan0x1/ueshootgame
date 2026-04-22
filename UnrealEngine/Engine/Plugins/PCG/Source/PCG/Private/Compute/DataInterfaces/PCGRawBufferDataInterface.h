// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compute/DataInterfaces/PCGComputeDataInterface.h"

#include "RHIResources.h"

#include "PCGRawBufferDataInterface.generated.h"

class FPCGRawBufferDataInterfaceParameters;
class UPCGRawBufferData;

/** A data interface for a simple array of uint values. No data format header or attributes, just raw array access. */
UCLASS(ClassGroup = (Procedural))
class UPCGRawBufferDataInterface : public UPCGExportableDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("PCGRawBuffer"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override { return GetSupportedOutputs(OutFunctions); }
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	TCHAR const* GetShaderVirtualPath() const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	virtual bool GetRequiresReadback() const override { return true; }
	UComputeDataProvider* CreateDataProvider() const override;
	//~ End UComputeDataInterface Interface

	bool GetRequiresZeroInitialization() const { return bRequiresZeroInitialization; }
	void SetRequiresZeroInitialization(bool bInZeroInit) { bRequiresZeroInitialization = bInZeroInit; }

protected:
	/** Whether to perform full 0-initialization of the buffer. */
	UPROPERTY()
	bool bRequiresZeroInitialization = false;
};

UCLASS()
class UPCGRawBufferDataProvider : public UPCGExportableDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	virtual void Initialize(const UComputeDataInterface* InDataInterface, UObject* InBinding, uint64 InInputMask, uint64 InOutputMask) override;
	virtual bool PrepareForExecute_GameThread(UPCGDataBinding* InBinding) override;
	virtual FComputeDataProviderRenderProxy* GetRenderProxy() override;
	virtual void Reset() override;
	//~ End UComputeDataProvider Interface

protected:
	int32 SizeBytes = INDEX_NONE;

	bool bZeroInitialize = false;

	UPROPERTY()
	TObjectPtr<const UPCGRawBufferData> DataToUpload;

	struct FReadbackState
	{
		TArray<uint32> Data;
	};
	TSharedPtr<FReadbackState, ESPMode::ThreadSafe> ReadbackState;
};

class FPCGRawBufferDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	struct FParams
	{
		int32 SizeBytes = 0;
		bool bZeroInitialize = false;
		const UPCGRawBufferData* DataToUpload = nullptr;
		EPCGExportMode ExportMode = EPCGExportMode::NoExport;
		FName OutputPinLabel;
		FName OutputPinLabelAlias;
		TWeakObjectPtr<UPCGRawBufferDataProvider> DataProviderWeakPtr;
		FReadbackCallback AsyncReadbackCallback_RenderThread;
	};

	FPCGRawBufferDataProviderProxy(const FParams& InParams);

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder, FAllocationData const& InAllocationData);
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	void GetReadbackData(TArray<FReadbackData>& OutReadbackData) const override;
	//~ End FComputeDataProviderRenderProxy Interface

protected:
	using FParameters = FPCGRawBufferDataInterfaceParameters;

	int32 SizeBytes = INDEX_NONE;

	bool bZeroInitialize = false;

	EPCGExportMode ExportMode = EPCGExportMode::NoExport;

	FName OutputPinLabel;
	FName OutputPinLabelAlias;

	FRDGBufferRef Data = nullptr;
	FRDGBufferUAVRef DataUAV = nullptr;

	TArray<uint32> DataToUpload;

	TWeakObjectPtr<UPCGRawBufferDataProvider> DataProviderWeakPtr;

	/** Called from render thread when readback from GPU is complete. */
	FReadbackCallback AsyncReadbackCallback_RenderThread;
};
