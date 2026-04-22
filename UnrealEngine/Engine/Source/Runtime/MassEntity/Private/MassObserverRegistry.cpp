// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverRegistry.h"
#include "Algo/Find.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassObserverRegistry)

//----------------------------------------------------------------------//
// UMassObserverRegistry
//----------------------------------------------------------------------//
UMassObserverRegistry::UMassObserverRegistry()
{
	// there can be only one!
	check(HasAnyFlags(RF_ClassDefaultObject));

	if (!ModulesUnloadedHandle.IsValid())
	{
		ModulesUnloadedHandle = FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate.AddUObject(this, &UMassObserverRegistry::OnModulePackagesUnloaded);
	}
}

void UMassObserverRegistry::RegisterObserver(TNotNull<const UScriptStruct*> ObservedType, const uint8 OperationFlags, TSubclassOf<UMassProcessor> ObserverClass)
{
	check(ObserverClass);
	const bool bIsFragment = UE::Mass::IsA<FMassFragment>(ObservedType);
	checkSlow(bIsFragment || UE::Mass::IsA<FMassTag>(ObservedType));

	FObserverClassesMap* ObserversMap = bIsFragment
		? FragmentObserverMaps
		: TagObserverMaps; 

	for (uint8 OperationIndex = 0; OperationIndex < static_cast<uint8>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		if ((OperationFlags & (1 << OperationIndex)))
		{
			ObserversMap[OperationIndex].FindOrAdd(ObservedType).AddUnique(FSoftClassPath(ObserverClass));
		}
	}
}

void UMassObserverRegistry::RegisterObserver(const UScriptStruct& ObservedType, const EMassObservedOperation Operation, TSubclassOf<UMassProcessor> ObserverClass)
{
	RegisterObserver(&ObservedType, static_cast<uint8>(1 << static_cast<uint8>(Operation)), ObserverClass);
}

void UMassObserverRegistry::OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMassObserverRegistry::OnModulePackagesUnloaded);

	const auto ProcessObserversMap = [&Packages](FObserverClassesMap& ObserversMap)
	{
		for (auto MapIt = ObserversMap.CreateIterator(); MapIt; ++MapIt)
		{
			TArray<FSoftClassPath>& ObserverClasses = MapIt->Value;

			for (auto ObserverIt = ObserverClasses.CreateIterator(); ObserverIt; ++ObserverIt)
			{
				const FSoftClassPath& ObservedClass = *ObserverIt;
				const FName PackageName = ObservedClass.GetLongPackageFName();

				bool bRemove = !!Algo::FindByPredicate(Packages, [PackageName](const UPackage* Package)
				{
					return PackageName == Package->GetFName();
				});

				if (bRemove)
				{
					UE_LOG(LogMass, Log, TEXT("%hs: removed observer %s (%s)"), __FUNCTION__, *ObservedClass.ToString(), *PackageName.ToString());
					ObserverIt.RemoveCurrent();
				}
			}

			if (ObserverClasses.Num() == 0)
			{
				MapIt.RemoveCurrent();
			}
		}
	};

	for (uint8 OperationIndex = 0; OperationIndex < static_cast<uint8>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		ProcessObserversMap(FragmentObserverMaps[OperationIndex]);
		ProcessObserversMap(TagObserverMaps[OperationIndex]);
	}
}
