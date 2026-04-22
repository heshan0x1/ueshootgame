// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverProcessor.h"
#include "MassObserverRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassObserverProcessor)

//----------------------------------------------------------------------//
// UMassObserverProcessor
//----------------------------------------------------------------------//
UMassObserverProcessor::UMassObserverProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
#if WITH_EDITORONLY_DATA
	bCanShowUpInSettings = false;
#endif // WITH_EDITORONLY_DATA
}

void UMassObserverProcessor::PostInitProperties()
{
	Super::PostInitProperties();

	UClass* MyClass = GetClass();
	CA_ASSUME(MyClass);

	if (HasAnyFlags(RF_ClassDefaultObject) && MyClass->HasAnyClassFlags(CLASS_Abstract) == false)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (Operation != EMassObservedOperation::MAX)
		{
			ObservedOperations |= (Operation == EMassObservedOperation::Add)
				? EMassObservedOperationFlags::Add
				: EMassObservedOperationFlags::Remove;
			Operation = EMassObservedOperation::MAX;
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (ObservedType != nullptr && ObservedOperations != EMassObservedOperationFlags::None)
		{
			Register();
		}
		else if (bAutoRegisterWithObserverRegistry)
		{
			UE_LOG(LogMass, Error, TEXT("%hs attempting to register %s while it\'s misconfigured, Type: %s, OperationFlags: %#x")
				, __FUNCTION__, *MyClass->GetName(), *GetNameSafe(ObservedType), ObservedOperations);
		}
	}
}

void UMassObserverProcessor::Register()
{
	if (bAutoRegisterWithObserverRegistry)
	{
		check(ObservedType);
		UMassObserverRegistry::GetMutable().RegisterObserver(ObservedType, static_cast<uint8>(ObservedOperations), GetClass());
	}
}


