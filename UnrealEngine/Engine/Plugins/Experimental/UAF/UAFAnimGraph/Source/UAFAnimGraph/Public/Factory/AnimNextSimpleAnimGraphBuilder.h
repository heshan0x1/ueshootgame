// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextAnimGraphBuilder.h"
#include "StructUtils/StructView.h"
#include "AnimNextSimpleAnimGraphBuilder.generated.h"

struct FAnimNextTraitSharedData;
struct FAnimNextSimpleAnimGraphBuilder;
class FAnimationAnimNextRuntimeTest_SimpleGraphBuilder;
struct FAnimNextGraphInstanceTaskInstanceData;
struct FAnimNextFactoryParams;

namespace UE::UAF
{
	struct FAnimGraphFactory;
};

namespace UE::UAF::Editor
{
	class FAnimNextFactoryParamsDetails;
};

USTRUCT()
struct FAnimNextSimpleAnimGraphBuilderTraitStackDesc
{
	GENERATED_BODY()

private:
	friend UE::UAF::FAnimGraphFactory;
	friend FAnimNextSimpleAnimGraphBuilder;
	friend FAnimNextGraphInstanceTaskInstanceData;	// For old data upgrade
	friend UE::UAF::Editor::FAnimNextFactoryParamsDetails;
	friend FAnimNextFactoryParams;

	// Traits for this stack
	UPROPERTY(EditAnywhere, Category = Parameters)
	TArray<TInstancedStruct<FAnimNextTraitSharedData>> TraitStructs;
};

USTRUCT()
struct FAnimNextSimpleAnimGraphBuilder : public FAnimNextAnimGraphBuilder
{
	GENERATED_BODY()

	// Push a trait struct onto the supplied stack. Traits are ordered from bottom to top, stack-wise.
	// Struct must be a sub-struct of FAnimNextTraitSharedData
	UAFANIMGRAPH_API void PushTraitStructViewToStack(int32 InStackIndex, TConstStructView<FAnimNextTraitSharedData> InStruct);
	UAFANIMGRAPH_API void PushTraitInstancedStructToStack(int32 InStackIndex, TInstancedStruct<FAnimNextTraitSharedData>&& InStruct);
	
	// Get the number of stacks from this graph
	int32 GetNumStacks() const
	{
		return Stacks.Num();
	}

	// Check whether this method is empty or not
	bool IsValid() const
	{
		ensure(Stacks.Num() <= 1);	// Currently only support a single stack
		return Stacks.Num() > 0;
	}

	// Reset back to empty state
	void Reset()
	{
		Stacks.Empty();
	}
	
private:
	friend UE::UAF::FAnimGraphFactory;
	friend FAnimationAnimNextRuntimeTest_SimpleGraphBuilder;
	friend FAnimNextGraphInstanceTaskInstanceData;	// For old data upgrade
	friend UE::UAF::Editor::FAnimNextFactoryParamsDetails;
	friend FAnimNextFactoryParams;

	// Internal helper
	void ValidateTraitStruct(int32 InStackIndex, TConstStructView<FAnimNextTraitSharedData> InStruct);

	// FAnimNextAnimGraphBuilder interface
	UAFANIMGRAPH_API virtual bool Build(UE::UAF::FAnimGraphBuilderContext& InContext) const override;
	UAFANIMGRAPH_API virtual uint64 RecalculateKey() const override;

	// The trait stacks we will use to generate our data
	UPROPERTY(EditAnywhere, Category = Parameters)
	TArray<FAnimNextSimpleAnimGraphBuilderTraitStackDesc> Stacks;
};