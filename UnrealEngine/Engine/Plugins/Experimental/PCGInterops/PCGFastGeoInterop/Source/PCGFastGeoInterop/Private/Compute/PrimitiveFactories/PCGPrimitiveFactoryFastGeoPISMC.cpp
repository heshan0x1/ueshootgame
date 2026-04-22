// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/PrimitiveFactories/PCGPrimitiveFactoryFastGeoPISMC.h"

#include "Components/PCGManagedFastGeoContainer.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PrimitiveSceneInfo.h"
#include "Components/PCGProceduralISMComponentDescriptor.h"
#include "Compute/PCGComputeCommon.h"
#include "Engine/World.h"

bool FPCGPrimitiveFactoryFastGeoPISMC::IsRenderStateCreated() const
{
	if (Components.IsEmpty())
	{
		return false;
	}

	// Use first component to get to container.
	FFastGeoComponent* Component = Components[0].Get();
	UFastGeoContainer* Container = Component ? Component->GetOwnerContainer() : nullptr;

	if (!Container)
	{
		return false;
	}

	if (Container->HasAnyPendingTasks())
	{
		Container->Tick();

		if (Container->HasAnyPendingTasks())
		{
			return false;
		}
	}

	for (int32 PrimitiveIndex = 0; PrimitiveIndex < GetNumPrimitives(); ++PrimitiveIndex)
	{
		FPrimitiveSceneProxy* SceneProxy = GetSceneProxy(PrimitiveIndex);
		if (!SceneProxy || !SceneProxy->GetPrimitiveSceneInfo() || SceneProxy->GetPrimitiveSceneInfo()->GetInstanceSceneDataOffset() == -1)
		{
			return false;
		}
	}

	return true;
}

FPrimitiveSceneProxy* FPCGPrimitiveFactoryFastGeoPISMC::GetSceneProxy(int32 InPrimitiveIndex) const
{
	if (ensure(Components.IsValidIndex(InPrimitiveIndex)) && Components[InPrimitiveIndex].Get())
	{
		return static_cast<FFastGeoPrimitiveComponent*>(Components[InPrimitiveIndex].Get())->GetSceneProxy();
	}
	else
	{
		return nullptr;
	}
}

int32 FPCGPrimitiveFactoryFastGeoPISMC::GetNumInstances(int32 InPrimitiveIndex) const
{
	return ensure(InstanceCounts.IsValidIndex(InPrimitiveIndex)) ? InstanceCounts[InPrimitiveIndex] : 0;
}

void FPCGPrimitiveFactoryFastGeoPISMC::Initialize(FParameters&& InParameters)
{
	Descriptors = InParameters.Descriptors;
}

bool FPCGPrimitiveFactoryFastGeoPISMC::Create(FPCGContext* InContext)
{
	IPCGGraphExecutionSource* ExecutionSource = InContext->ExecutionSource.Get();
	if (!ExecutionSource)
	{
		return true;
	}

	UWorld* World = InContext->ExecutionSource->GetExecutionState().GetWorld();
	ULevel* Level = World ? World->GetCurrentLevel() : nullptr;

	if (!Level)
	{
		UE_LOG(LogPCG, Error, TEXT("No persistent level found, geometry will not be created."));
		return true;
	}

	// Currently only support transient editor world.
	if (!ensure(InContext->ExecutionSource->GetExecutionState().GetWorld()->IsGameWorld()))
	{
		return true;
	}

	// Must be outer'd to level - so pick same level as pcg component.
	UFastGeoContainer* FastGeo = FPCGContext::NewObject_AnyThread<UFastGeoContainer>(InContext, Level);

	const FString DebugName = ExecutionSource->GetExecutionState().GetDebugName();
	TUniquePtr<FFastGeoComponentCluster> ComponentCluster = MakeUnique<FFastGeoComponentCluster>(FastGeo, *FString::Printf(TEXT("FastGeoComponentCluster_%s"), *DebugName));

	const int32 NumToCreate = FMath::Min(Descriptors.Num(), PCGComputeConstants::MAX_PRIMITIVE_COMPONENTS_PER_SPAWNER);

	MeshBounds.SetNumUninitialized(NumToCreate);

	for (int32 DescIndex = 0; DescIndex < NumToCreate; ++DescIndex)
	{
		const FPCGProceduralISMComponentDescriptor& Desc = Descriptors[DescIndex];

		FFastGeoProceduralISMComponent& FastGeoComponent = static_cast<FFastGeoProceduralISMComponent&>(ComponentCluster->AddComponent(FFastGeoProceduralISMComponent::Type));
		FastGeoComponent.InitializeFromComponentDescriptor(Desc);

		MeshBounds[DescIndex] = Desc.StaticMesh->GetBoundingBox();
	}

	ensure(ComponentCluster->HasComponents());

	FastGeo->AddComponentCluster(ComponentCluster.Get());

	// Disable the default reference collection path as it is currently too costly for runtime use. Instead manually collect references specific to the PISMC below.
	FastGeo->OnCreated(/*bCollectReferences=*/false);

	FastGeo->PrecachePSOs();

	FastGeo->Register();

	int32 ComponentIndex = 0;
	FastGeo->ForEachComponentCluster([This=this, &ComponentIndex](FFastGeoComponentCluster& InComponentCluster)
	{
		InComponentCluster.ForEachComponent([This, &ComponentIndex](FFastGeoComponent& InComponent)
		{
			check(This->Descriptors.IsValidIndex(ComponentIndex));
			const FPCGProceduralISMComponentDescriptor& Desc = This->Descriptors[ComponentIndex];

			// If not true, weakptr assignment below will fail.
			check(InComponent.GetOwnerComponentCluster());

			This->Components.Add(&InComponent);
			This->InstanceCounts.Add(Desc.NumInstances);

			++ComponentIndex;
		});
	});

	UPCGManagedFastGeoContainer* ManagedPrimitives = FPCGContext::NewObject_AnyThread<UPCGManagedFastGeoContainer>(InContext, Cast<UObject>(ExecutionSource));
	ManagedPrimitives->SetFastGeoContainer(FastGeo);
	ManagedPrimitives->SetObjectReferences(CollectObjectReferences());

	ExecutionSource->GetExecutionState().AddToManagedResources(ManagedPrimitives);

	return true;
}

FBox FPCGPrimitiveFactoryFastGeoPISMC::GetMeshBounds(int32 InPrimitiveIndex) const
{
	return ensure(MeshBounds.IsValidIndex(InPrimitiveIndex)) ? MeshBounds[InPrimitiveIndex] : FBox();
}

TArray<TObjectPtr<UObject>> FPCGPrimitiveFactoryFastGeoPISMC::CollectObjectReferences()
{
	TArray<TObjectPtr<UObject>> ObjectReferences;
	ObjectReferences.Reserve(Descriptors.Num() * 4); // Heuristic

	for (const FPCGProceduralISMComponentDescriptor& Descriptor : Descriptors)
	{
		ObjectReferences.Add(Descriptor.StaticMesh.Get());

		if (Descriptor.OverlayMaterial)
		{
			ObjectReferences.Add(Descriptor.OverlayMaterial);
		}

		ObjectReferences.Append(Descriptor.OverrideMaterials);
	}

	return ObjectReferences;
}
