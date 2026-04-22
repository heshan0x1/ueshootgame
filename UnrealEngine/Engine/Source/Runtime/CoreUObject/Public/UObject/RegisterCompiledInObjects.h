// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"

#if UE_WITH_CONSTINIT_UOBJECT 

#include "Containers/ArrayView.h"
#include "Misc/TVariant.h"
#include "UObject/UObjectGlobals.h"

class UClass;
class UDelegateFunction;
class UObject;
class UPackage;

// Register partially initialized objects such as packages, classes, structs and enums for runtime construction
// This structure is stored as a linked list where possible to avoid dynamic allocation during startup 
struct FRegisterCompiledInObjects
{
	using FIntrinsicClassConstructor = void();

	// Register an intrinsic class with a special initialization function
	FRegisterCompiledInObjects(UClass* InClass, FIntrinsicClassConstructor* InConstructor)
		: FRegisterCompiledInObjects(FIntrinsicClassRegistrant{InClass, InConstructor})
	{
	}

	// Register a package and some objects that might otherwise not be registered - e.g. noexport structs and delegate
	// function signatures. ArrayView should point to static memory
	FRegisterCompiledInObjects(UPackage* InPackage, TConstArrayView<UObject*> Others)
		: FRegisterCompiledInObjects(FPackageRegistrants{InPackage, Others})
	{
	}

	// Primary entry point - register a batch of objects from a single generated cpp file all together
	// ArrayView should point to static memory
	// Constructors for each possible set of parameters are provided to reduce code size at the call site
	FRegisterCompiledInObjects(
		TConstArrayView<UClass*> InClasses,
		TConstArrayView<UScriptStruct*> InStructs,
		TConstArrayView<UEnum*> InEnums)
		: FRegisterCompiledInObjects(FClassRegistrants{InClasses, InStructs, InEnums})
	{
	}
	FRegisterCompiledInObjects(
		TConstArrayView<UClass*> InClasses)
		: FRegisterCompiledInObjects(FClassRegistrants{InClasses, {}, {}})
	{
	}
	FRegisterCompiledInObjects(
		TConstArrayView<UClass*> InClasses,
		TConstArrayView<UScriptStruct*> InStructs)
		: FRegisterCompiledInObjects(FClassRegistrants{InClasses, InStructs, {}})
	{
	}
	FRegisterCompiledInObjects(
		TConstArrayView<UClass*> InClasses,
		TConstArrayView<UEnum*> InEnums)
		: FRegisterCompiledInObjects(FClassRegistrants{InClasses, {}, InEnums})
	{
	}
	FRegisterCompiledInObjects(
		TConstArrayView<UScriptStruct*> InStructs,
		TConstArrayView<UEnum*> InEnums)
		: FRegisterCompiledInObjects(FClassRegistrants{{}, InStructs, InEnums})
	{
	}
	FRegisterCompiledInObjects(
		TConstArrayView<UScriptStruct*> InStructs)
		: FRegisterCompiledInObjects(FClassRegistrants{{}, InStructs, {}})
	{
	}
	FRegisterCompiledInObjects(
		TConstArrayView<UEnum*> InEnums)
		: FRegisterCompiledInObjects(FClassRegistrants{{}, {}, InEnums})
	{
	}

	struct FPackageRegistrants
	{
		UPackage* Package = nullptr;
		TConstArrayView<UObject*> Objects;
	};

	struct FClassRegistrants
	{
		TConstArrayView<UClass*> Classes;
		TConstArrayView<UScriptStruct*> Structs;
		TConstArrayView<UEnum*> Enums;
	};

	struct FIntrinsicClassRegistrant
	{
		UClass* IntrinsicClass = nullptr;
		FIntrinsicClassConstructor* Constructor = nullptr;
	};

	TVariant<FPackageRegistrants, FClassRegistrants, FIntrinsicClassRegistrant> Registrants;
	FRegisterCompiledInObjects* ListNext = nullptr;
	
	// Delegating constructor
	template<typename VARIANT>
	FRegisterCompiledInObjects(VARIANT&& Variant)
	: Registrants(TInPlaceType<VARIANT>{}, Forward<VARIANT>(Variant))
	{
		Register();
	}

	// Register all our objects for deferred construction
	COREUOBJECT_API void Register();
};

#endif