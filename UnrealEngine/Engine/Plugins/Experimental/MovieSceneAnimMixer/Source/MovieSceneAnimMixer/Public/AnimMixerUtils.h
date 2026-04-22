// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Component/AnimNextComponent.h"

struct FAnimMixerUtils
{

	static void RegisterWithUAFSubsystem(UAnimNextComponent* InComponent)
	{
		if (InComponent)
		{
			InComponent->RegisterWithSubsystem();
		}
	}

	static void UnregisterWithUAFSubsystem(UAnimNextComponent* InComponent)
	{
		if (InComponent)
		{
			InComponent->UnregisterWithSubsystem();
		}
	}

	static void SetUAFComponentModule(UAnimNextComponent* InComponent, TObjectPtr<UAnimNextModule> InModule)
	{
		if (InComponent)
		{
			InComponent->SetModule(InModule);
		}
	}

	static bool IsUAFModuleValid(UAnimNextComponent* InComponent)
	{
		return InComponent ? InComponent->IsModuleValid() : false;
	}

	static TObjectPtr<UAnimNextModule> GetUAFModule(UAnimNextComponent* InComponent)
	{
		return InComponent ? InComponent->Module : nullptr;
	}
	
};
