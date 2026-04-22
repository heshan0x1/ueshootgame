// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassViewerFilter.h"
#include "VerseVM/VVMVerseClass.h"

class FVerseConcreteClassFilter : public IClassViewerFilter
{
	// Filter for Verse classes only such that all classes must have or inherit explicit concrete-ness
public:
	static bool StaticIsClassAllowed(const UClass* InClass)
	{
		if (const UVerseClass* VerseClass = Cast<UVerseClass>(InClass))
		{
			return VerseClass->IsConcrete();
		}
		return false;
	}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return StaticIsClassAllowed(InClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const class IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<class FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return false;
	}
};

class FVerseCastableClassFilter : public IClassViewerFilter
{
	// Filter for Verse classes only such that all classes must have or inherit explicit castable-ness
public:
	static bool StaticIsClassAllowed(const UClass* InClass)
	{
		if (const UVerseClass* VerseClass = Cast<UVerseClass>(InClass))
		{
			return VerseClass->IsExplicitlyCastable();
		}
		return false;
	}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return StaticIsClassAllowed(InClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const class IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<class FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return false;
	}
};
