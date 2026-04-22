// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.generated.h"

#define UE_API MASSENTITY_API


namespace UE::Mass::ObserverManager
{
	struct FObserverContextIterator;
}

/**
 * Instances of the type will be fed into FMassRuntimePipeline.AuxData and at execution time will
 * be available to observer processors via FMassExecutionContext.GetAuxData() 
 */
USTRUCT()
struct FMassObserverExecutionContext
{
	GENERATED_BODY()

	FMassObserverExecutionContext() = default;
	FMassObserverExecutionContext(const EMassObservedOperation InOperation, const TArrayView<const UScriptStruct*> InTypesInOperation)
		: Operation(InOperation), TypesInOperation(InTypesInOperation)
	{	
	}

	EMassObservedOperation GetOperationType() const
	{
		return Operation;
	}

	TConstArrayView<const UScriptStruct*> GetTypesInOperation() const
	{
		return TypesInOperation;
	}

	const UScriptStruct* GetCurrentType() const
	{
		return TypesInOperation[CurrentTypeIndex];
	}

	bool IsValid() const
	{
		return Operation <  EMassObservedOperation::MAX
			&& TypesInOperation.IsValidIndex(CurrentTypeIndex);
	}

private:
	friend UE::Mass::ObserverManager::FObserverContextIterator;
	EMassObservedOperation Operation = EMassObservedOperation::MAX;
	TArrayView<const UScriptStruct*> TypesInOperation;
	int32 CurrentTypeIndex = INDEX_NONE;
};

/**
 * Base class for Processors that are used as "observers" of entity operations.
 * An observer declares the type of Mass element it cares about (Fragments and Tags supported at the moment) - via
 * the ObservedType property - and the types of operations it wants to be notified of - via ObservedOperations.
 *
 * When an observed operation takes place the processor's regular execution will take place, with
 * ExecutionContext's "auxiliary data" (obtained by calling GetAuxData) being filled with an instance of FMassObserverExecutionContext,
 * that can be used to get information about the type being handled and the kind of operation. 
 */
UCLASS(MinimalAPI, abstract)
class UMassObserverProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UE_API UMassObserverProcessor();

	EMassObservedOperationFlags GetObservedOperations() const;
	TNotNull<const UScriptStruct*> GetObservedTypeChecked() const;

protected:
	UE_API virtual void PostInitProperties() override;

	/** 
	 * By default, registers this class as Operation observer of ObservedType. Override to register for multiple 
	 * operations and/or types 
	 */
	UE_API virtual void Register();

protected:
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	bool bAutoRegisterWithObserverRegistry = true;

	/** Determines which Fragment or Tag type this given UMassObserverProcessor will be observing */
	UPROPERTY()
	TObjectPtr<const UScriptStruct> ObservedType = nullptr;

	UE_DEPRECATED(5.7, "UMassObserverProcessor::Operation is deprecated. Use ObservedOperations instead")
	EMassObservedOperation Operation = EMassObservedOperation::MAX;

	EMassObservedOperationFlags ObservedOperations = EMassObservedOperationFlags::None;
};


//----------------------------------------------------------------------//
// inlines
//----------------------------------------------------------------------//
inline EMassObservedOperationFlags UMassObserverProcessor::GetObservedOperations() const
{
	return ObservedOperations;
}

inline TNotNull<const UScriptStruct*> UMassObserverProcessor::GetObservedTypeChecked() const
{
	return ObservedType;
}

#undef UE_API
