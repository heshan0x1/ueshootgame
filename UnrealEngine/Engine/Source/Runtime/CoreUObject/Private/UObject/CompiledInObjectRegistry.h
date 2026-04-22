// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"

#if UE_WITH_CONSTINIT_UOBJECT 

#include "Misc/TVariant.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/RegisterCompiledInObjects.h"

class FCompiledInObjectRegistry
{
    UE_NONCOPYABLE(FCompiledInObjectRegistry);
    FCompiledInObjectRegistry() = default;
    ~FCompiledInObjectRegistry() = default;

    // Linked list of objects to construct. New items are added here, before items that may have
    // already been partially processed which are indicated by the following pointers.
    FRegisterCompiledInObjects* ListHead = nullptr;
    // First block of objects that have already been processed by AddAndHashObjects
    FRegisterCompiledInObjects* AlreadyAdded = nullptr;
    // First block of objects that have already been processed by FinishConstructingObjects
    FRegisterCompiledInObjects* AlreadyConstructed = nullptr;
    // First block of objects that have already been processed by CreateClassDefaultObjects
    FRegisterCompiledInObjects* AlreadyConstructedDefaultObjects = nullptr;

public:
    static FCompiledInObjectRegistry& Get()
    {
        static FCompiledInObjectRegistry Singleton;
        return Singleton;
    }

    /** Add the given set of objects to the linked list of items to process. */
    void AddObjects(FRegisterCompiledInObjects* Info);

    /** Remove all objects after all construction work is complete. */
    void EmptyObjects();

    /** Return whether there are any registrants that not been processed by AddAndHashObjects or FinishConstructingObjects. */
    [[nodiscard]] bool HasObjectsPendingConstruction() const;

    /** Return whether there are any registrants that have done all phases of construction. */
    [[nodiscard]] bool HasPendingObjects() const;

    /** Initialize the name and index of all pending objects and add them to the object hash. */
    void AddAndHashObjects();

    /** 
     * Finalize construction of all pending compiled in objects but not class default objects
     */
    void FinishConstructingObjects();

    /** 
     * Create class default objects for all pending classes 
     * Note that InModuleName does not filter which objects are constructed at this point, it is just forwarded
     * to FCoreUObjectDelegates::CompiledInUObjectsRegisteredDelegate. Per-module initialization for modular builds
     * and hot reload is not yet implemented. 
     */
    void CreateClassDefaultObjects(FName InModuleName);

    /** Assemble reference token stream for all registered classes. */
    void AssembleReferenceTokenStream();

private:
    // Iterate all linked FRegisterCompiledInObjects until Stop (not including it) and call F with each as a concrete variant
    template<typename... FUNCTORS>
    void Iterate(FRegisterCompiledInObjects* Stop, FUNCTORS&&... FS);

    template<typename FUNCTOR>
    void IterateObjects(FRegisterCompiledInObjects* Stop, FUNCTOR&& F);

    template<typename FUNCTOR>
    void IterateScriptStructs(FRegisterCompiledInObjects* Stop, FUNCTOR&& F);

    template<typename FUNCTOR>
    void IterateClasses(FRegisterCompiledInObjects* Stop, FUNCTOR&& F);

    template<typename FUNCTOR>
    void IterateIntrinsicClasses(FRegisterCompiledInObjects* Stop, FUNCTOR&& F);

#if WITH_METADATA
	static TConstArrayView<UE::CodeGen::ConstInit::FMetaData> GetCompiledInMetaData(UEnum* InEnum);
	static TConstArrayView<UE::CodeGen::ConstInit::FMetaData> GetCompiledInMetaData(UStruct* InStruct);
	static void AddMetaData(UObject* Object, TConstArrayView<UE::CodeGen::ConstInit::FMetaData> InMetaData);
#endif
	static void AddMetaData(UStruct* Object);
	static void AddMetaData(UEnum* Object);
};


inline void FCompiledInObjectRegistry::AddObjects(FRegisterCompiledInObjects* Info)
{
    // Insert new objects at the head of the list - AlreadyAdded/AlreadyConstructed keeps track of what we've already processed
    Info->ListNext = ListHead;
    ListHead = Info;
}

#endif // UE_WITH_CONSTINIT_UOBJECT 