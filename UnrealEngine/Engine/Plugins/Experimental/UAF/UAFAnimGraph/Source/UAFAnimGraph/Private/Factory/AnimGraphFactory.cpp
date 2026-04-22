// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/AnimGraphFactory.h"

#include "Factory/AnimGraphBuilderContext.h"
#include "Containers/StripedMap.h"
#include "Factory/AnimNextAnimGraphBuilder.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Misc/CoreDelegates.h"
#include "TraitCore/TraitWriter.h"

namespace UE::UAF::Private
{

static FDelegateHandle GAnimGraphFactoryPreExitHandle;

// References to all generated factory graphs, keyed by method
static TStripedMap<32, uint64, TWeakObjectPtr<const UAnimNextAnimationGraph>> GAnimationGraphMap;

// All registered object -> param mappings
static TMap<const UClass*, TPair<FAnimNextFactoryParams, FAnimGraphFactory::FParamsInitializer>> GDefaultParamsForObject;
static TArray<UClass*> GAllFactoryClasses;

const TPair<FAnimNextFactoryParams, FAnimGraphFactory::FParamsInitializer>* FindParamsForClass(const UClass* InClass)
{
	const UClass* CurrentClass = InClass;
	const TPair<FAnimNextFactoryParams, FAnimGraphFactory::FParamsInitializer>* FoundParams = nullptr;
	while (FoundParams == nullptr && CurrentClass != nullptr)
	{
		FoundParams = GDefaultParamsForObject.Find(CurrentClass);
		CurrentClass = CurrentClass->GetSuperClass();
	}

	return FoundParams;
}

}

namespace UE::UAF
{

uint64 IAnimGraphBuilder::GetKey() const
{
	return 0;
}

const UAnimNextAnimationGraph* FAnimGraphFactory::GetDefaultGraphHost()
{
	const TPair<FAnimNextFactoryParams, FParamsInitializer>* ParamsTask = Private::FindParamsForClass(UAnimNextAnimationGraph::StaticClass());
	return BuildGraph(ParamsTask->Key.GetBuilder());
}

const UAnimNextAnimationGraph* FAnimGraphFactory::GetOrBuildGraph(const UObject* InObject, const IAnimGraphBuilder& InBuilder)
{
	if (const UAnimNextAnimationGraph* AnimationGraph = Cast<UAnimNextAnimationGraph>(InObject))
	{
		return AnimationGraph;
	}

	return BuildGraph(InBuilder);
}

const UAnimNextAnimationGraph* FAnimGraphFactory::BuildGraph(const IAnimGraphBuilder& InBuilder)
{
	uint64 Key = InBuilder.GetKey();
	if (Key == 0)
	{
		return nullptr;
	}

	auto ProduceGraph = [&InBuilder]() -> TWeakObjectPtr<const UAnimNextAnimationGraph>
	{
		FAnimGraphBuilderContext Context;
		if (InBuilder.Build(Context))
		{
			return Context.Build();
		}
		return nullptr;
	};

	TWeakObjectPtr<const UAnimNextAnimationGraph> AnimationGraph;
	auto ApplyWeakGraph = [&ProduceGraph, &AnimationGraph](TWeakObjectPtr<const UAnimNextAnimationGraph>& InFoundAnimationGraph)
	{
		if (InFoundAnimationGraph.IsStale())
		{
			// Found a stale weak ptr, so regenerate (this may be a procedural graph or a previously loaded graph that has been GCed)
			InFoundAnimationGraph = ProduceGraph();
		}
		AnimationGraph = InFoundAnimationGraph;
	};

	Private::GAnimationGraphMap.FindOrProduceAndApplyForWrite(Key, ProduceGraph, ApplyWeakGraph);
	return AnimationGraph.Get();
}

const FAnimNextFactoryParams& FAnimGraphFactory::GetDefaultParamsForClass(const UClass* InClass)
{
	check(InClass);
	const TPair<FAnimNextFactoryParams, FParamsInitializer>* ParamsTask = Private::FindParamsForClass(InClass);
	check(ParamsTask != nullptr);	// Unregistered object type
	return ParamsTask->Key;
}

FAnimNextFactoryParams FAnimGraphFactory::GetDefaultParamsForObject(const UObject* InObject)
{
	check(InObject);
	const TPair<FAnimNextFactoryParams, FParamsInitializer>* ParamsTask = Private::FindParamsForClass(InObject->GetClass());
	check(ParamsTask != nullptr);	// Unregistered object type

	FAnimNextFactoryParams ParamsCopy = ParamsTask->Key;
	if (ParamsCopy.Builder.Stacks.Num() > 0)
	{
		FFactoryParamsInitializerContext Context;
		Context.Object = InObject;
		Context.StructDatas = ParamsCopy.Builder.Stacks[0].TraitStructs;
		ParamsTask->Value(Context);
	}
	return ParamsCopy;
}

void FAnimGraphFactory::InitializeDefaultParamsForObject(const UObject* InObject, FAnimNextFactoryParams& InOutParams)
{
	check(InObject);
	const TPair<FAnimNextFactoryParams, FParamsInitializer>* ParamsTask = Private::FindParamsForClass(InObject->GetClass());
	check(ParamsTask != nullptr);	// Unregistered object type

	if (InOutParams.Builder.Stacks.Num() > 0)
	{
		FFactoryParamsInitializerContext Context;
		Context.Object = InObject;
		Context.StructDatas = InOutParams.Builder.Stacks[0].TraitStructs;
		ParamsTask->Value(Context);
	}
}

void FAnimGraphFactory::RegisterDefaultParamsForClass(const UClass* InClass, FAnimNextFactoryParams&& InParams, FParamsInitializer&& InInitializer)
{
	Private::GDefaultParamsForObject.Add(InClass, { MoveTemp(InParams), MoveTemp(InInitializer) });
	Private::GAllFactoryClasses.Add(const_cast<UClass*>(InClass));
}

TConstArrayView<UClass*> FAnimGraphFactory::GetRegisteredClasses()
{
	return Private::GAllFactoryClasses;
}

void FAnimGraphFactory::Init()
{
	Private::GAnimGraphFactoryPreExitHandle = FCoreDelegates::OnEnginePreExit.AddStatic(&FAnimGraphFactory::OnPreExit);
}

void FAnimGraphFactory::Destroy()
{
	FCoreDelegates::OnEnginePreExit.Remove(Private::GAnimGraphFactoryPreExitHandle);
	Private::GAnimGraphFactoryPreExitHandle.Reset();
}

void FAnimGraphFactory::OnPreExit()
{
	Private::GAnimationGraphMap.Empty();
	Private::GDefaultParamsForObject.Empty();
	Private::GAllFactoryClasses.Empty();
}

}
