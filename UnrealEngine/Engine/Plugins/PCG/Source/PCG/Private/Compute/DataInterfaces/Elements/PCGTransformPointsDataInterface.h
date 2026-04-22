// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compute/DataInterfaces/PCGComputeDataInterface.h"

#include "PCGTransformPointsDataInterface.generated.h"

class FPCGTransformPointsDataInterfaceParameters;

/** Data Interface to marshal transform points settings to the GPU. */
UCLASS(ClassGroup = (Procedural))
class UPCGTransformPointsDataInterface : public UPCGComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("PCGTransformPointsDataInterface"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	UComputeDataProvider* CreateDataProvider() const override;
	//~ End UComputeDataInterface Interface
};

UCLASS()
class UPCGTransformPointsDataProvider : public UPCGComputeDataProvider
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	virtual void Reset() override;
	virtual bool PrepareForExecute_GameThread(UPCGDataBinding* InBinding) override;
	//~ End UComputeDataProvider Interface

	/** Output attribute Id. */
	UPROPERTY()
	int32 AttributeId = INDEX_NONE;
};

class FPCGTransformPointsDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	struct FData
	{
		FVector OffsetMin = FVector::ZeroVector;
		FVector OffsetMax = FVector::ZeroVector;
		uint32 bAbsoluteOffset = 0;
		FVector RotationMin = FVector::ZeroVector;
		FVector RotationMax = FVector::ZeroVector;
		uint32 bAbsoluteRotation = 0;
		FVector ScaleMin = FVector::OneVector;
		FVector ScaleMax = FVector::OneVector;
		uint32 bAbsoluteScale = 0;
		uint32 bUniformScale = 1;
		uint32 bRecomputeSeed = 0;
		uint32 bApplyToAttribute = 0;
		int32 AttributeId = INDEX_NONE;
	};

	FPCGTransformPointsDataProviderProxy(FData InData)
		: Data(MoveTemp(InData))
	{}

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

protected:
	using FParameters = FPCGTransformPointsDataInterfaceParameters;

	FData Data;
};
