// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimStateTreeTrait.h"
#include "StructUtils/InstancedStruct.h"

#include "AnimNextStateTreeContext.generated.h"

namespace UE::UAF
{
struct FTraitBinding;
struct FExecutionContext;
struct FStateTreeTrait;
}

class UAnimNextAnimationGraph;
struct FAlphaBlendArgs;
struct FAnimNextFactoryParams;

USTRUCT()
struct FAnimNextStateTreeTraitContext
{	
	GENERATED_BODY()
	
	friend UE::UAF::FStateTreeTrait;
	friend UE::UAF::FStateTreeTrait::FInstanceData;

	FAnimNextStateTreeTraitContext() = default;

	FAnimNextStateTreeTraitContext(UE::UAF::FExecutionContext& InContext, const UE::UAF::FTraitBinding& InBinding)
		: Context(&InContext)
		, Binding(&InBinding)
	{}

	bool PushAssetOntoBlendStack(TNonNullPtr<UObject> InAsset, const FAlphaBlendArgs& InBlendArguments, const FAnimNextFactoryParams& InFactoryParams) const;
	float QueryPlaybackRatio() const;
	float QueryTimeLeft() const;
	float QueryDuration() const;
	bool QueryIsLooping() const;
	FAnimNextGraphInstance& GetGraphInstance() const;

	UE::UAF::FExecutionContext& GetContext() const
	{
		check(Context != nullptr);
		return *Context;
	}

	const UE::UAF::FTraitBinding& GetBinding() const
	{
		check(Binding != nullptr);
		return *Binding;
	}

private:
	UE::UAF::FExecutionContext* Context = nullptr;
	const UE::UAF::FTraitBinding* Binding = nullptr;
};

