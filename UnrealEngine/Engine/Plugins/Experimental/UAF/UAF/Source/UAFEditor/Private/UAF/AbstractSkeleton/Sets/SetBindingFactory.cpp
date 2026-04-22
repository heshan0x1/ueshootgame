// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/AbstractSkeleton/Sets/SetBindingFactory.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetBinding.h"
#include "UObject/Package.h"

UAbstractSkeletonSetBindingFactory::UAbstractSkeletonSetBindingFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UAbstractSkeletonSetBinding::StaticClass();
}

bool UAbstractSkeletonSetBindingFactory::ConfigureProperties()
{
	return true;
}

UObject* UAbstractSkeletonSetBindingFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	EObjectFlags FlagsToUse = Flags | RF_Public | RF_Standalone | RF_Transactional | RF_LoadCompleted;
	if (InParent == GetTransientPackage())
	{
		FlagsToUse &= ~RF_Standalone;
	}

	TObjectPtr<UAbstractSkeletonSetBinding> NewSetBinding = NewObject<UAbstractSkeletonSetBinding>(InParent, Class, Name, FlagsToUse);

	return NewSetBinding;
}
