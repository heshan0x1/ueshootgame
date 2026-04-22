// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextModuleContextData.h"
#include "Module/AnimNextModuleInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextModuleContextData)

FAnimNextModuleContextData::FAnimNextModuleContextData(FAnimNextModuleInstance* InModuleInstance)
	: ModuleInstance(InModuleInstance)
	, Instance(InModuleInstance)
{
}

FAnimNextModuleContextData::FAnimNextModuleContextData(FAnimNextModuleInstance* InModuleInstance, const FUAFAssetInstance* InInstance)
	: ModuleInstance(InModuleInstance)
	, Instance(InInstance)
{
}

UObject* FAnimNextModuleContextData::GetObject() const
{
	return ModuleInstance ? ModuleInstance->GetObject() : nullptr;
}
