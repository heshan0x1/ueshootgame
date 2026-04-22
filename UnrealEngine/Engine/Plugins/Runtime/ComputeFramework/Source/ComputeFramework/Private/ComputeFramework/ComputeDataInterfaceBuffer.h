// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include "ComputeDataInterfaceBuffer.generated.h"

class FBufferDataInterfaceParameters;

/** Compute data interface used to own and give access to a GPU buffer. */
UCLASS(MinimalAPI, Category = ComputeFramework)
class UComputeDataInterfaceBuffer : public UComputeDataInterface
{
	GENERATED_BODY()

	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("Buffer"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	TCHAR const* GetShaderVirtualPath() const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	UComputeDataProvider* CreateDataProvider() const override;
	//~ End UComputeDataInterface Interface

public:
	/** The value type for the buffer. */
	UPROPERTY()
	FShaderValueTypeHandle ValueType;

	/** The size of the buffer. */
	UPROPERTY()
	int32 ElementCount = 0;

	/** Whether to allow read/write access. */
	UPROPERTY()
	bool bAllowReadWrite = false;

	/** Whether to clear the buffer before use. */
	UPROPERTY()
	bool bClearBeforeUse = false;

private:
	static TCHAR const* TemplateFilePath;
};

/** Compute data provider implementation for UComputeDataInterfaceBuffer. */
UCLASS(MinimalAPI)
class UBufferDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

public:
	int32 ElementCount = 0;
	int32 ElementStride = 4;
	bool bClearBeforeUse;
};

/** Compute data provider proxy implementation for UComputeDataInterfaceBuffer. */
class FBufferDataProviderProxy : public FComputeDataProviderRenderProxy
{
	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder, FAllocationData const& InAllocationData) override;
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

public:
	FBufferDataProviderProxy(int32 InElementCount, int32 InElementStride, bool bInClearBeforeUse);

private:
	using FParameters = FBufferDataInterfaceParameters;

	const int32 ElementCount;
	const int32 ElementStride;
	const bool bClearBeforeUse;

	FRDGBufferRef Buffer;
	FRDGBufferSRVRef BufferSRV;
	FRDGBufferUAVRef BufferUAV;
};
