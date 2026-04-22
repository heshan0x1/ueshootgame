// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerMainRender.h"

#include "CompositeActor.h"

UCompositeLayerMainRender::UCompositeLayerMainRender(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Operation = ECompositeCoreMergeOp::Over;
}

UCompositeLayerMainRender::~UCompositeLayerMainRender() = default;

bool UCompositeLayerMainRender::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);

	FPassInternalResourceDesc Desc0;
	Desc0.Index = 0;
	Desc0.bOriginalCopyBeforePasses = true;
	Inputs[0].Set<FPassInternalResourceDesc>(Desc0);
	Inputs[1] = GetDefaultSecondInput(InContext);

	InContext.bNeedsSceneTextures = true;
	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("MainRender"));
	return true;
}

