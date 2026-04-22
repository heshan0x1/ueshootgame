// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PCGProceduralISMComponentDescriptor.h"
#include "Compute/PrimitiveFactories/PCGPrimitiveFactoryISMBase.h"

#include "FastGeoWeakElement.h"
#include "UObject/WeakObjectPtr.h"

struct FPCGContext;
class FPrimitiveSceneProxy;
class UPrimitiveComponent;

class FPCGPrimitiveFactoryFastGeoPISMC : public IPCGPrimitiveFactoryISMBase
{
public:
	//~ Begin IPCGRuntimePrimitiveFactory interface
	virtual bool IsRenderStateCreated() const override;
	virtual int32 GetNumPrimitives() const override { return Components.Num(); }
	virtual FPrimitiveSceneProxy* GetSceneProxy(int32 InPrimitiveIndex) const override;
	virtual int32 GetNumInstances(int32 InPrimitiveIndex) const override;
	//~ End IPCGRuntimePrimitiveFactory interface

	//~ Begin IPCGPrimitiveFactoryISMBase interface
	virtual void Initialize(FParameters&& InParameters) override;
	virtual bool Create(FPCGContext* InContext) override;
	virtual FBox GetMeshBounds(int32 InPrimitiveIndex) const override;
	//~ End IPCGPrimitiveFactoryISMBase interface

protected:
	TArray<TObjectPtr<UObject>> CollectObjectReferences();

protected:
	TArray<FPCGProceduralISMComponentDescriptor> Descriptors;

	TArray<FWeakFastGeoComponent> Components;
	TArray<int32> InstanceCounts;
	TArray<FBox> MeshBounds;
};
