// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextAnimGraphBuilder.h"
#include "InstanceTask.h"
#include "InstanceTaskContext.h"
#include "AnimNextSimpleAnimGraphBuilder.h"
#include "TraitCore/TraitSharedData.h"
#include "AnimNextFactoryParams.generated.h"

struct FAnimNextAnimGraphFactoryMethod;
struct FAnimNextGraphInstance;
class UAnimNextAnimGraphSettings;
class UAnimNextRigVMAsset;
struct FAnimNextFactoryParams;
struct FAnimNextGraphInstanceTaskInstanceData;

namespace UE::UAF
{
	struct FAnimGraphFactory;
}

namespace UE::UAF::Editor
{
	class FAnimNextFactoryParamsDetails;
};

// Set of parameters that describe a way of generating & initializing an animation graph
// TODO: To add an internal trait, use AddInternalTrait.
// TODO: Interface to map public interface to internal traits 
USTRUCT(BlueprintType)
struct FAnimNextFactoryParams
{
	GENERATED_BODY()

	FAnimNextFactoryParams() = default;

	// Whether these params are valid for application
	bool IsValid() const
	{
		return Builder.IsValid();
	}

	// Push a trait struct onto the specified stack in the graph builder, initialized to the struct's defaults.
	// The trait struct forms part of the created instance's 'public interface'.
	// If a trait already exists of this type then its values be overriden by virtue of the initializer running later
	// Returns self for chaining.
	template<typename StructType>
	FAnimNextFactoryParams& PushPublicTraitStruct(int32 InStackIndex = 0)
	{
		TInstancedStruct<FAnimNextTraitSharedData> InstancedStruct;
		InstancedStruct.InitializeAsScriptStruct(TBaseStructure<StructType>::Get());
		Builder.PushTraitInstancedStructToStack(InStackIndex, MoveTemp(InstancedStruct));
		return *this;
	}

	// Push a trait struct onto the specified stack in the graph builder, initialized to the supplied value
	// The trait struct forms part of the created instance's 'public interface'.
	// If a trait already exists of this type then its values be overriden by virtue of the initializer running later
	// Returns self for chaining.
	template<typename StructType>
	FAnimNextFactoryParams& PushPublicTrait(const StructType& InValue, int32 InStackIndex = 0)
	{
		Builder.PushTraitStructViewToStack(InStackIndex, InValue);
		return *this;
	}

	// Get the builder that these params hold
	const UE::UAF::IAnimGraphBuilder& GetBuilder() const
	{
		return Builder;
	}

	// Add an initialize task used to set up an instance when these params are applied
	FAnimNextFactoryParams& AddInitializeTask(UE::UAF::FInstanceTask&& InTask)
	{
		InitializeTasks.Add(MoveTemp(InTask));
		return *this;
	}

	// Apply these params to the supplied instance.
	UAFANIMGRAPH_API void InitializeInstance(FUAFAssetInstance& InInstance) const;

	// Clear these parameters back to empty
	UAFANIMGRAPH_API void Reset();

private:
	friend UE::UAF::FAnimGraphFactory;
	friend UE::UAF::Editor::FAnimNextFactoryParamsDetails;
	friend FAnimNextGraphInstanceTaskInstanceData;	// For old data upgrade

	// Method used to create a graph
	UPROPERTY(EditAnywhere, Category = Parameters)
	FAnimNextSimpleAnimGraphBuilder Builder;

	// Tasks to run on the factory-generated graph instance via InitializeInstance, used for initial setup of variables etc.
	TArray<UE::UAF::FInstanceTask> InitializeTasks;
};
