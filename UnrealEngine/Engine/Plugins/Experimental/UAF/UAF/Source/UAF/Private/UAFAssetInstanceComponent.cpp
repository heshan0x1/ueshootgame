// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFAssetInstanceComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFAssetInstanceComponent)

namespace UE::UAF::Private
{
	static thread_local FUAFAssetInstance* GCurrentAssetInstance = nullptr;
}

FUAFAssetInstanceComponent::FScopedConstructorHelper::FScopedConstructorHelper(FUAFAssetInstance& InInstance)
{
	UE::UAF::Private::GCurrentAssetInstance = &InInstance;
}

FUAFAssetInstanceComponent::FScopedConstructorHelper::~FScopedConstructorHelper()
{
	UE::UAF::Private::GCurrentAssetInstance = nullptr;
}

FUAFAssetInstanceComponent::FUAFAssetInstanceComponent()
{
	Instance = UE::UAF::Private::GCurrentAssetInstance;
}
