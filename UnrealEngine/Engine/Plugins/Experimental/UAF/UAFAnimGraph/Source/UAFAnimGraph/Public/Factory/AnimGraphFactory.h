// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Factory/AnimNextFactoryParams.h"

class UAnimNextAnimationGraph;
class UObject;

namespace UE::UAF
{
	class FTraitWriter;
	struct IAnimGraphBuilder;
	struct FAnimGraphFactory;

	namespace AnimGraph
	{
		class FAnimNextAnimGraphModule;
	}
}

namespace UE::UAF
{

struct FFactoryParamsInitializerContext
{
	// Get the object used to initialize the params (e.g. AnimSequence for a player)
	const UObject* GetObject() const
	{
		return Object;
	}

	// Access a struct during initialization (for instance to inject an AnimSequence)
	template<typename StructType>
	bool AccessStruct(TFunctionRef<void(StructType&)> InFunction) const
	{
		for (TInstancedStruct<FAnimNextTraitSharedData>& StructData : StructDatas)
		{
			if (StructData.GetScriptStruct() == TBaseStructure<StructType>::Get())
			{
				InFunction(StructData.GetMutable<StructType>());
				return true;
			}
		}
		return false;
	}

private:
	friend FAnimGraphFactory;

	TArrayView<TInstancedStruct<FAnimNextTraitSharedData>> StructDatas;
	const UObject* Object = nullptr;
};

// Creates or recycles programmatically-generated graphs
// Uses the hash of the recipe to determine if the graph has been created already.
// If the hash matches one that has already been created but graph has already been GCed it will be re-created as needed.
struct FAnimGraphFactory
{
	// Make a graph from the specified recipe
	// If the object is an UAnimNextAnimationGraph, it will be returned rather than build a new one
	UAFANIMGRAPH_API static const UAnimNextAnimationGraph* GetOrBuildGraph(const UObject* InObject, const IAnimGraphBuilder& InBuilder);

	// Make a graph from the specified recipe
	UAFANIMGRAPH_API static const UAnimNextAnimationGraph* BuildGraph(const IAnimGraphBuilder& InBuilder);

	// Make the default graph for running a UAnimNextAnimationGraph asset (allowing graphs to blend between each other)
	UAFANIMGRAPH_API static const UAnimNextAnimationGraph* GetDefaultGraphHost();

	// Get default params used to build & initialize a graph according to registered object class
	UAFANIMGRAPH_API static const FAnimNextFactoryParams& GetDefaultParamsForClass(const UClass* InClass);

	// Get default params used to build & initialize a graph according to registered object class, copying InObject via the registered initializer
	// Effective equivalent of GetDefaultParamsForClass and InitializeDefaultParamsForObject.
	UAFANIMGRAPH_API static FAnimNextFactoryParams GetDefaultParamsForObject(const UObject* InObject);

	// Applies default initializer to the params (usually injecting the object pointer) 
	UAFANIMGRAPH_API static void InitializeDefaultParamsForObject(const UObject* InObject, FAnimNextFactoryParams& InOutParams);

	// Function used to initialize the object for the factory-generated graph
	using FParamsInitializer = TFunction<void(const FFactoryParamsInitializerContext&)>;

	// Register default params used to build & initialize a graph according to the supplied object class
	UAFANIMGRAPH_API static void RegisterDefaultParamsForClass(const UClass* InClass, FAnimNextFactoryParams&& InParams, FParamsInitializer&& InInitializer);

	// Get all registered classes used to generate factory graphs
	UAFANIMGRAPH_API static TConstArrayView<UClass*> GetRegisteredClasses();

private:
	// Called on module init
	UAFANIMGRAPH_API static void Init();

	// Called on module shutdown to unload everything
	UAFANIMGRAPH_API static void Destroy();

	// Called before engine shutdown to clear internal state while UObjects are still valid
	static void OnPreExit();

	friend AnimGraph::FAnimNextAnimGraphModule;
};

}