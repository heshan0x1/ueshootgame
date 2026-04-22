// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/AnimNextSharedVariablesFactory.h"
#include "Variables/AnimNextSharedVariables.h"
#include "Variables/AnimNextSharedVariables_EditorData.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSharedVariablesFactory)

UAnimNextSharedVariablesFactory::UAnimNextSharedVariablesFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UAnimNextSharedVariables::StaticClass();
}

bool UAnimNextSharedVariablesFactory::ConfigureProperties()
{
	return true;
}

UObject* UAnimNextSharedVariablesFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	EObjectFlags FlagsToUse = Flags | RF_Public | RF_Standalone | RF_Transactional | RF_LoadCompleted;
	if(InParent == GetTransientPackage())
	{
		FlagsToUse &= ~RF_Standalone;
	}

	UAnimNextSharedVariables* NewDataInterface = NewObject<UAnimNextSharedVariables>(InParent, Class, Name, FlagsToUse);

	// Create internal editor data
	UAnimNextSharedVariables_EditorData* EditorData = NewObject<UAnimNextSharedVariables_EditorData>(NewDataInterface, TEXT("EditorData"), RF_Transactional);
	NewDataInterface->EditorData = EditorData;
	EditorData->bUsesExternalPackages = false;
	EditorData->Initialize(/*bRecompileVM*/false);

	// Compile the initial skeleton
	EditorData->RecompileVM();
	check(!EditorData->bErrorsDuringCompilation);

	return NewDataInterface;
}
