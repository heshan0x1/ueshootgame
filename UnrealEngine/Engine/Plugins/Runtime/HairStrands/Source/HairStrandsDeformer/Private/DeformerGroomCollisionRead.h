// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "OptimusComputeDataInterface.h"
#include "HairStrandsInterface.h"
#include "IOptimusDeformerInstanceAccessor.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "DeformerGroomCollisionRead.generated.h"

class FOptimusGroomCollisionReadParameters;
class UGroomComponent;
struct FHairGroupInstance;
class UGroomAsset;
class UMeshComponent;
class FSkeletalMeshObject;
class UGroomSolverComponent;

/** Compute Framework Data Interface for reading groom collision. */
UCLASS(Category = ComputeFramework)
class UOptimusGroomCollisionReadDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	
	//~ Begin UOptimusComputeDataInterface Interface
	virtual FString GetDisplayName() const override;
	virtual TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	virtual TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	virtual TCHAR const* GetClassName() const override { return TEXT("GroomCollisionRead"); }
	virtual void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	virtual void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	virtual TCHAR const* GetShaderVirtualPath() const override;
	virtual void GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const override;
	virtual void GetShaderHash(FString& InOutKey) const override;
	virtual void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	virtual UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

private:
	/** File holding the hlsl implementation */
	static TCHAR const* TemplateFilePath;
};

/** Compute Framework Data Provider for reading groom meshes. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusGroomCollisionReadDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<UGroomSolverComponent> SolverComponent = nullptr;

	//~ Begin UComputeDataProvider Interface
	virtual FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FOptimusGroomCollisionReadDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusGroomCollisionReadDataProviderProxy(UGroomSolverComponent* SolverComponent);

	//~ Begin FComputeDataProviderRenderProxy Interface
	virtual bool IsValid(FValidationData const& InValidationData) const override;
	virtual void AllocateResources(FRDGBuilder& GraphBuilder, FAllocationData const& InAllocationData) override;
	virtual void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	using FParameters = FOptimusGroomCollisionReadParameters;
	
	/** Skeletal mesh objects to extract the bones from */
	TMap<TObjectPtr<UMeshComponent>, int32> CollisionObjects;

	/** Collision resources used to dispatch CS on GPU */
	struct FCollisionResources
	{
		FRHIShaderResourceView* VertexPositions = nullptr;
		FRHIShaderResourceView* TriangleIndices = nullptr;
		int32 VertexOffset = 0;
		int32 TriangleOffset = 0;
		int32 NumVertices = 0;
		int32 NumTriangles = 0;
	};
	
	/**List of collision resources per invocations */
	TArray<FCollisionResources> CollisionResources;

	/** Fallback resources */
	FRDGBufferSRVRef FallbackStructuredSRV = nullptr;
};
