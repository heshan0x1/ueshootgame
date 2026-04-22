// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/ModuleTaskContext.h"

#include "Module/AnimNextModuleInstance.h"

namespace UE::UAF
{

FModuleTaskContext::FModuleTaskContext(FAnimNextModuleInstance& InModuleInstance)
	: FInstanceTaskContext(InModuleInstance)
	, ModuleInstance(InModuleInstance)
{
}

void FModuleTaskContext::QueueInputTraitEvent(FAnimNextTraitEventPtr Event) const
{
	ModuleInstance.QueueInputTraitEvent(MoveTemp(Event));
}

void FModuleTaskContext::TryAccessComponent(UScriptStruct* InComponentType, TFunctionRef<void(FUAFModuleInstanceComponent&)> InFunction) const
{
	FUAFModuleInstanceComponent* Component = static_cast<FUAFModuleInstanceComponent*>(ModuleInstance.TryGetComponent(InComponentType));
	if(Component == nullptr)
	{
		return;
	}

	InFunction(*Component);
}

FAnimNextModuleInstance* const FModuleTaskContext::GetModuleInstance() const
{
	return &ModuleInstance;
}

}
