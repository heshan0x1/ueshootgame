// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestSubsystem.h"

#include "GauntletModule.h"
#include "Modules/ModuleManager.h"
#include "AutomatedPerfTestControllerBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AutomatedPerfTestSubsystem)

FString UAutomatedPerfTestSubsystem::GetTestID()
{
	if(FGauntletModule* ParentModule = &FModuleManager::Get().GetModuleChecked<FGauntletModule>(TEXT("Gauntlet")))
	{
		if(UAutomatedPerfTestControllerBase* Controller = ParentModule->GetTestController<UAutomatedPerfTestControllerBase>())
		{
			return Controller->GetTestID();
		}
	}
	return TEXT("");
}
