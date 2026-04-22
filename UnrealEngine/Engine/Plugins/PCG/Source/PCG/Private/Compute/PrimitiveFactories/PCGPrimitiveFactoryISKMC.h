// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compute/PrimitiveFactories/IPCGRuntimePrimitiveFactory.h"

class AActor;
class UPrimitiveComponent;
struct FPCGContext;
struct FPCGCrc;
struct FPCGSoftSkinnedMeshComponentDescriptor;

class FPCGPrimitiveFactoryISKMC : public IPCGRuntimePrimitiveFactory
{
public:
	//~ Begin IPCGRuntimePrimitiveFactory interface
	virtual int32 GetNumPrimitives() const override { return Components.Num(); }
	virtual FPrimitiveSceneProxy* GetSceneProxy(int32 InPrimitiveIndex) const override;
	virtual int32 GetNumInstances(int32 InPrimitiveIndex) const override;
	virtual bool IsRenderStateCreated() const override;
	//~ End IPCGRuntimePrimitiveFactory interface

	struct FParameters
	{
		TArray<FPCGSoftSkinnedMeshComponentDescriptor> Descriptors;
		AActor* TargetActor = nullptr;
	};

	virtual void Initialize(FParameters&& InParameters);
	virtual bool Create(FPCGContext* InContext);
	virtual FBox GetMeshBounds(int32 InPrimitiveIndex) const;

protected:
	TArray<FPCGSoftSkinnedMeshComponentDescriptor> Descriptors;
	AActor* TargetActor = nullptr;

	int32 NumPrimitivesProcessed = 0;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Components;
	TArray<int32> NumInstances;
	TArray<FBox> MeshBounds;
};
