// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"

#include "PCGAssetExporter.h"
#include "PCGContext.h"
#include "Async/PCGAsyncLoadingContext.h"

#include "PCGNaniteAssemblyStaticMeshBuilder.generated.h"

/**
 * [Experimental] Create a Static Mesh using Nanite assemblies from the input point data. Points are expected to be in local coordinates.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGNaniteAssemblyStaticMeshBuilderSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
    virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface
	
public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties, PCG_Overridable))
	FPCGAssetExporterParameters ExportParams;

	/** Attribute for the mesh to spawn for a given point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector MeshAttribute;

	/** Array of attributes for material overrides. 1 Attribute per slot. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGAttributePropertyInputSelector> MaterialOverrides;

	/** Meshes/Materials will be synchronously loaded before spawning instead of asynchronously. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Debug")
	bool bSynchronousLoad = false;
};

struct FPCGNaniteAssemblyStaticMeshBuilderContext : public FPCGContext, public IPCGAsyncLoadingContext
{
	TArray<TTuple<TSoftObjectPtr<UStaticMesh>, TArray<TSoftObjectPtr<UMaterialInterface>>, TArray<int32>>> Mapping;
	bool bPartitionDone = false;
	bool bPrepareValid = false;
};

class FPCGNaniteAssemblyStaticMeshBuilderElement : public IPCGElementWithCustomContext<FPCGNaniteAssemblyStaticMeshBuilderContext>
{
protected:
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
	
    virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
	
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool IsCancellable() const override { return false; }
};

