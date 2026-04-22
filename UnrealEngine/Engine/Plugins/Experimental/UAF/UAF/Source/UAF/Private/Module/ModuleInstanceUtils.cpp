// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/ModuleInstanceUtils.h"

#include "Component/AnimNextComponentWorldSubsystem.h"
#include "Engine/Engine.h"
#include "StructUtils/PropertyBag.h"
#include "Variables/AnimNextVariableReference.h"

namespace UE::UAF
{

EPropertyBagResult SetModuleVariable(FModuleContext&& InModuleContext, const FAnimNextVariableReference& InVariable
	, const FAnimNextParamType& InType, TConstArrayView<uint8> InData)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(InModuleContext.ContextObject, EGetWorldErrorMode::ReturnNull))
	{
		if (UAnimNextComponentWorldSubsystem* Subsystem = World->GetSubsystem<UAnimNextComponentWorldSubsystem>())
		{
			return Subsystem->SetVariableHandle(InModuleContext.Handle, InVariable, InType, InData);
		}
	}

	UE_LOG(LogAnimation, Error, TEXT("Unable to set variable '%s'. Cause: unable to access world system from the provided context object '%s'")
		, *InVariable.GetName().ToString()
		, *GetFullNameSafe(InModuleContext.ContextObject));
	return EPropertyBagResult::PropertyNotFound;
}

EPropertyBagResult WriteModuleVariable(FModuleContext&& InModuleContext, const FAnimNextVariableReference& InVariable
	, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(InModuleContext.ContextObject, EGetWorldErrorMode::ReturnNull))
	{
		if (UAnimNextComponentWorldSubsystem* Subsystem = World->GetSubsystem<UAnimNextComponentWorldSubsystem>())
		{
			return Subsystem->WriteVariableHandle(InModuleContext.Handle, InVariable, InType, InFunction);
		}
	}

	UE_LOG(LogAnimation, Error, TEXT("Unable to set variable '%s'. Cause: unable to access world system from the provided context object '%s'")
		, *InVariable.GetName().ToString()
		, *GetFullNameSafe(InModuleContext.ContextObject));
	return EPropertyBagResult::PropertyNotFound;
}

} // UE::UAF