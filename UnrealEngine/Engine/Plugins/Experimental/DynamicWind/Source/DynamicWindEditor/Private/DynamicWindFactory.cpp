// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicWindFactory.h"
#include "DynamicWindProvider.h"
#include "DynamicWindData.h"

UDynamicWindDataFactory::UDynamicWindDataFactory(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	SupportedClass = UDynamicWindData::StaticClass();
	ProviderDataClass = UDynamicWindData::StaticClass();
}

bool UDynamicWindDataFactory::ConfigureProperties()
{
	return true;
}
