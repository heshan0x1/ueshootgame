// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/UAFModuleInstanceComponent.h"
#include "Module/AnimNextModuleInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFModuleInstanceComponent)

FAnimNextModuleInstance* FUAFModuleInstanceComponent::GetModuleInstancePtr()
{
	return static_cast<FAnimNextModuleInstance*>(GetAssetInstancePtr());
}

FAnimNextModuleInstance& FUAFModuleInstanceComponent::GetModuleInstance()
{
	return static_cast<FAnimNextModuleInstance&>(GetAssetInstance());
}

const FAnimNextModuleInstance& FUAFModuleInstanceComponent::GetModuleInstance() const
{
	return static_cast<const FAnimNextModuleInstance&>(GetAssetInstance());
}
