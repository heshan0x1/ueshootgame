// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitInterfaces/IGraphFactory.h"

#include "Factory/AnimGraphFactory.h"
#include "Factory/AnimNextFactoryParams.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "TraitCore/ExecutionContext.h"

namespace UE::UAF
{
	AUTO_REGISTER_ANIM_TRAIT_INTERFACE(IGraphFactory)

#if WITH_EDITOR
	const FText& IGraphFactory::GetDisplayName() const
	{
		static FText InterfaceName = NSLOCTEXT("TraitInterfaces", "TraitInterface_IGraphFactory_Name", "Graph Factory");
		return InterfaceName;
	}
	const FText& IGraphFactory::GetDisplayShortName() const
	{
		static FText InterfaceShortName = NSLOCTEXT("TraitInterfaces", "TraitInterface_IGraphFactory_ShortName", "GF");
		return InterfaceShortName;
	}
#endif // WITH_EDITOR

	void IGraphFactory::GetFactoryParams(FExecutionContext& Context, const TTraitBinding<IGraphFactory>& Binding, const UObject* InObject, FAnimNextFactoryParams& InOutParams) const
	{
		TTraitBinding<IGraphFactory> SuperBinding;
		if (Binding.GetStackInterfaceSuper(SuperBinding))
		{
			SuperBinding.GetFactoryParams(Context, InObject, InOutParams);
		}
	}

	const UAnimNextAnimationGraph* IGraphFactory::GetOrBuildGraph(FExecutionContext& Context, const FTraitBinding& Binding, const UObject* InObject, FAnimNextFactoryParams& InOutParams)
	{
		// If a graph was provided directly, just use it
		if (const UAnimNextAnimationGraph* AnimationGraph = Cast<UAnimNextAnimationGraph>(InObject))
		{
			return AnimationGraph;
		}
		else if (InObject != nullptr) // Otherwise attempt to build a graph procedurally from the supplied params
		{
			if (!InOutParams.IsValid())
			{
				InOutParams = FAnimGraphFactory::GetDefaultParamsForClass(InObject->GetClass());
			}

			// Apply default initializer (trait stacks can still alter this below)
			FAnimGraphFactory::InitializeDefaultParamsForObject(InObject, InOutParams);

			// Defer to the trait stack to gather any additional traits to instantiate
			TTraitBinding<IGraphFactory> GraphBuilderBinding;
			if (Binding.GetStackInterface(GraphBuilderBinding))
			{
				GraphBuilderBinding.GetFactoryParams(Context, InObject, InOutParams);
			}

			return FAnimGraphFactory::BuildGraph(InOutParams.GetBuilder());
		}

		return nullptr;
	}
}
