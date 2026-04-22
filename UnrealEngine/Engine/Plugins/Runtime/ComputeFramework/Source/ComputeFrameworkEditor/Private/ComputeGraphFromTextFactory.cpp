// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphFromTextFactory.h"

#include "ComputeFramework/ComputeGraphFromText.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComputeGraphFromTextFactory)

UComputeGraphFromTextFactory::UComputeGraphFromTextFactory()
{
	SupportedClass = UComputeGraphFromText::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UComputeGraphFromTextFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UComputeGraphFromText* Kernel = NewObject<UComputeGraphFromText>(InParent, InClass, InName, Flags);

	return Kernel;
}
