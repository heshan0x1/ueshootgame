// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compute/PrimitiveFactories/IPCGRuntimePrimitiveFactory.h"

#include "UObject/WeakObjectPtr.h"

class AActor;
struct FPCGContext;
struct FPCGProceduralISMComponentDescriptor;

class IPCGPrimitiveFactoryISMBase : public IPCGRuntimePrimitiveFactory
{
public:
	struct FParameters
	{
		TArray<FPCGProceduralISMComponentDescriptor> Descriptors;
		AActor* TargetActor = nullptr; // Optional, unless creating actor primitive components.
	};

	virtual void Initialize(FParameters&& InParameters) = 0;
	virtual bool Create(FPCGContext* InContext) = 0;
	virtual FBox GetMeshBounds(int32 InPrimitiveIndex) const = 0;
};

namespace PCGPrimitiveFactoryHelpers
{
	TSharedPtr<IPCGPrimitiveFactoryISMBase> GetFastGeoPrimitiveFactory();

	// todo_pcg: Remove and integrate FastGeo component spawning directly into PCG once FastGeo exits experimental.
	namespace Private
	{
		UE_EXPERIMENTAL(5.7, "This API is internal and not intended to be used.")
		PCG_API void SetupFastGeoPrimitiveFactory(TFunction<TSharedPtr<IPCGPrimitiveFactoryISMBase>()>&& Getter);

		UE_EXPERIMENTAL(5.7, "This API is internal and not intended to be used.")
		PCG_API void ResetFastGeoPrimitiveFactory();
	}
}
