// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeContext.h"

#include <limits>
#include "Factory/AnimGraphFactory.h"
#include "TraitCore/NodeInstance.h"
#include "TraitCore/TraitStackBinding.h"
#include "TraitInterfaces/IBlendStack.h"
#include "TraitInterfaces/ITimeline.h"

bool FAnimNextStateTreeTraitContext::PushAssetOntoBlendStack(TNonNullPtr<UObject> InAsset, const FAlphaBlendArgs& InBlendArguments, const FAnimNextFactoryParams& InFactoryParams) const
{
	using namespace UE::UAF;

	TTraitBinding<IBlendStack> BlendStackBinding;
	if(!Binding->GetStackInterface<IBlendStack>(BlendStackBinding))
	{
		return false;
	}

	// Instantiate/get a graph from the params
	const UAnimNextAnimationGraph* AnimationGraph = FAnimGraphFactory::GetOrBuildGraph(InAsset, InFactoryParams.GetBuilder());
	if(AnimationGraph == nullptr)
	{
		return false;
	}

	IBlendStack::FGraphRequest NewGraphRequest;
	NewGraphRequest.BlendArgs = InBlendArguments;
	NewGraphRequest.FactoryObject = InAsset;
	NewGraphRequest.AnimationGraph = AnimationGraph;
	NewGraphRequest.FactoryParams = InFactoryParams;

	BlendStackBinding.PushGraph(*Context, MoveTemp(NewGraphRequest));

	return true;
}

float FAnimNextStateTreeTraitContext::QueryPlaybackRatio() const
{
	UE::UAF::TTraitBinding<UE::UAF::ITimeline> Timeline;
	if (Binding->GetStackInterface<UE::UAF::ITimeline>(Timeline))
	{
		return Timeline.GetState(*Context).GetPositionRatio();
	}

	return 1.0f;
}

float FAnimNextStateTreeTraitContext::QueryTimeLeft() const
{
	UE::UAF::TTraitBinding<UE::UAF::ITimeline> Timeline;
	if (Binding->GetStackInterface<UE::UAF::ITimeline>(Timeline))
	{
		return Timeline.GetState(*Context).GetTimeLeft();
	}

	return std::numeric_limits<float>::infinity();
}

float FAnimNextStateTreeTraitContext::QueryDuration() const
{
	UE::UAF::TTraitBinding<UE::UAF::ITimeline> Timeline;
	if (Binding->GetStackInterface<UE::UAF::ITimeline>(Timeline))
	{
		return Timeline.GetState(*Context).GetDuration();
	}

	return 0.0f;
}

bool FAnimNextStateTreeTraitContext::QueryIsLooping() const
{
	UE::UAF::TTraitBinding<UE::UAF::ITimeline> Timeline;
	if (Binding->GetStackInterface<UE::UAF::ITimeline>(Timeline))
	{
		return Timeline.GetState(*Context).IsLooping();
	}

	return false;
}

FAnimNextGraphInstance& FAnimNextStateTreeTraitContext::GetGraphInstance() const
{
	return Binding->GetTraitPtr().GetNodeInstance()->GetOwner();
}