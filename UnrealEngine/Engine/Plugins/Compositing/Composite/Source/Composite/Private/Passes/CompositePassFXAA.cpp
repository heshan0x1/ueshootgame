// Copyright Epic Games, Inc. All Rights Reserved.

#include "Passes/CompositePassFXAA.h"

#include "Passes/CompositeCorePassFXAAProxy.h"

UCompositePassFXAA::UCompositePassFXAA(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Quality(5)
{
}

UCompositePassFXAA::~UCompositePassFXAA() = default;

bool UCompositePassFXAA::GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	FFXAAPassProxy* Proxy = InFrameAllocator.Create<FFXAAPassProxy>(UE::CompositeCore::FPassInputDeclArray{ InputDecl });
	Proxy->QualityOverride = Quality;

	OutProxy = Proxy;
	return true;
}

