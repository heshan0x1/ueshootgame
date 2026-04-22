// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/TraitPtr.h"
#include "Graph/UAFGraphInstanceComponent.h"
#include "GC_GraphInstanceComponent.generated.h"

class FReferenceCollector;

/**
 * FGCGraphInstanceComponent
 *
 * This component maintains the necessary state to garbage collection.
 */
USTRUCT()
struct FGCGraphInstanceComponent : public FUAFGraphInstanceComponent
{
	GENERATED_BODY()

	FGCGraphInstanceComponent() = default;

	// Registers the provided trait with the GC system
	// Once registered, IGarbageCollection::AddReferencedObjects will be called on it during GC
	void Register(const UE::UAF::FWeakTraitPtr& InTraitPtr);

	// Unregisters the provided trait from the GC system
	void Unregister(const UE::UAF::FWeakTraitPtr& InTraitPtr);

	// Called during garbage collection to collect strong object references
	void AddStructReferencedObjects(FReferenceCollector& Collector);

private:
	// List of trait handles that contain UObject references and implement IGarbageCollection
	TArray<UE::UAF::FWeakTraitPtr> TraitsWithReferences;
};

template<>
struct TStructOpsTypeTraits<FGCGraphInstanceComponent> : public TStructOpsTypeTraitsBase2<FGCGraphInstanceComponent>
{
	enum
	{
		WithAddStructReferencedObjects = true,
	};
};
