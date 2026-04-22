// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"
#include "PCGSettings.h"
#include "Compute/PCGComputeCommon.h"
#include "Compute/PCGComputeKernel.h"
#include "Graph/PCGGPUGraphCompilationContext.h"

class UPCGSettings;

namespace PCGKernelHelpers
{
#if WITH_EDITOR
	struct FCreateKernelParams
	{
		FCreateKernelParams(UObject* InObjectOuter, const UPCGSettings* InSettings = nullptr)
			: ObjectOuter(InObjectOuter)
			, OwnerSettings(InSettings)
			, bDumpDataDescriptions(InSettings && InSettings->bDumpDataDescriptions)
		{
		}

		UObject* ObjectOuter = nullptr;

		bool bRequiresOverridableParams = false;

		const UPCGSettings* OwnerSettings = nullptr;

		bool bDumpDataDescriptions = false;

		/** Create edges from node input pins to kernel inputs, assuming label is same on both. */
		TArray<FName> NodeInputPinsToWire = { PCGPinConstants::DefaultInputLabel };

		/** Create edges from node output pins to kernel outputs, assuming label is same on both. */
		TArray<FName> NodeOutputPinsToWire = { PCGPinConstants::DefaultOutputLabel };
	};

	template<class KernelType>
	KernelType* CreateKernel(FPCGGPUCompilationContext& InCompilationContext, const FCreateKernelParams& InParams, TArray<UPCGComputeKernel*>& OutKernels, TArray<FPCGKernelEdge>& OutKernelEdges)
	{
		FPCGComputeKernelParams KernelParams;
		KernelParams.Settings = InParams.OwnerSettings;
		KernelParams.bLogDescriptions = InParams.bDumpDataDescriptions;
		KernelParams.bRequiresOverridableParams = InParams.bRequiresOverridableParams;

		KernelType* Kernel = InCompilationContext.NewObject_AnyThread<KernelType>(InParams.ObjectOuter);

		Kernel->Initialize(KernelParams);
		OutKernels.Add(Kernel);

		// Connect node pins to kernel pins
		for (FName PinLabel : InParams.NodeInputPinsToWire)
		{
			OutKernelEdges.Emplace(FPCGPinReference(PinLabel), FPCGPinReference(Kernel, PinLabel));
		}

		for (FName PinLabel : InParams.NodeOutputPinsToWire)
		{
			OutKernelEdges.Emplace(FPCGPinReference(Kernel, PinLabel), FPCGPinReference(PinLabel));
		}

		return CastChecked<KernelType>(Kernel);
	}
#endif // WITH_EDITOR
}
