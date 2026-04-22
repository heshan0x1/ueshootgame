// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/PrimitiveFactories/PCGPrimitiveFactoryPISMC.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "Components/PCGProceduralISMComponent.h"
#include "Compute/PCGComputeCommon.h"

#include "PrimitiveSceneInfo.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"

FPrimitiveSceneProxy* FPCGPrimitiveFactoryPISMC::GetSceneProxy(int32 InPrimitiveIndex) const
{
	if (ensure(Components.IsValidIndex(InPrimitiveIndex)))
	{
		return Components[InPrimitiveIndex].IsValid() ? Components[InPrimitiveIndex]->GetSceneProxy() : nullptr;
	}
	else
	{
		return nullptr;
	}
}

int32 FPCGPrimitiveFactoryPISMC::GetNumInstances(int32 InPrimitiveIndex) const
{
	return NumInstances.IsValidIndex(InPrimitiveIndex) ? NumInstances[InPrimitiveIndex] : -1;
}

bool FPCGPrimitiveFactoryPISMC::IsRenderStateCreated() const
{
	for (int32 Index = 0; Index < GetNumPrimitives(); ++Index)
	{
		FPrimitiveSceneProxy* SceneProxy = GetSceneProxy(Index);
		if (!SceneProxy || !SceneProxy->GetPrimitiveSceneInfo() || SceneProxy->GetPrimitiveSceneInfo()->GetInstanceSceneDataOffset() == -1)
		{
			return false;
		}
	}

	return true;
}

void FPCGPrimitiveFactoryPISMC::Initialize(FParameters&& InParameters)
{
	Descriptors = InParameters.Descriptors;
	TargetActor = InParameters.TargetActor;
}

bool FPCGPrimitiveFactoryPISMC::Create(FPCGContext* InContext)
{
	UPCGComponent* SourceComponent = Cast<UPCGComponent>(InContext->ExecutionSource.Get());
	if (!SourceComponent)
	{
		if (InContext->ExecutionSource.IsValid())
		{
			UE_LOG(LogPCG, Error, TEXT("FPCGPrimitiveFactoryPISMC: This primitive factory currently requires a PCG component execution source."));
		}

		return true;
	}

	FPCGProceduralISMCBuilderParameters Params;
	Params.bAllowDescriptorChanges = false;

	const int32 NumToCreate = FMath::Min(Descriptors.Num(), PCGComputeConstants::MAX_PRIMITIVE_COMPONENTS_PER_SPAWNER);

	while (NumPrimitivesProcessed < NumToCreate)
	{
		const FPCGProceduralISMComponentDescriptor& Desc = Descriptors[NumPrimitivesProcessed];

		Params.Descriptor = Desc;

		UPCGManagedProceduralISMComponent* ManagedComponent = PCGManagedProceduralISMComponent::GetOrCreateManagedProceduralISMC(TargetActor, SourceComponent, /*InSettingsUID=*/0, Params);

		if (ManagedComponent)
		{
			Components.Add(ManagedComponent->GetComponent());
			NumInstances.Add(Desc.NumInstances);
			MeshBounds.Add(Desc.StaticMesh->GetBoundingBox());
		}

		++NumPrimitivesProcessed;

		if (NumPrimitivesProcessed < NumToCreate && InContext->AsyncState.ShouldStop())
		{
			return false;
		}
	}

	return NumPrimitivesProcessed == NumToCreate;
}

FBox FPCGPrimitiveFactoryPISMC::GetMeshBounds(int32 InPrimitiveIndex) const
{
	return ensure(MeshBounds.IsValidIndex(InPrimitiveIndex)) ? MeshBounds[InPrimitiveIndex] : FBox();
}
