// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextVariableReference.generated.h"

class UAnimNextRigVMAsset;
struct FAnimNextSoftVariableReference;

// A reference to an AnimNext variable
USTRUCT(BlueprintType)
struct FAnimNextVariableReference
{
	GENERATED_BODY()

	FAnimNextVariableReference() = default;

	// Legacy constructor from an FName
	UE_DEPRECATED(5.6, "Constructor should no longer be used, please use constructor that takes an asset or a struct")
	explicit FAnimNextVariableReference(FName InName)
		: Name(InName)
	{
	}

	// Construct a reference to an asset variable from a soft reference. NOTE: can load the object
	UAF_API explicit FAnimNextVariableReference(const FAnimNextSoftVariableReference& InSoftReference);

	// Construct a reference to an asset variable
	UAF_API explicit FAnimNextVariableReference(FName InName, const UAnimNextRigVMAsset* InAsset);

	// Construct a reference to a struct variable
	UAF_API explicit FAnimNextVariableReference(FName InName, const UScriptStruct* InStruct);

	// Get the name of the variable
	FName GetName() const
	{
		return Name;
	}

	// Get the asset or struct that the variable reference is contained in
	TObjectPtr<const UObject> GetObject() const
	{
		return Object;
	}

	// Check whether this can ever refer to a valid variable
	bool IsNone() const
	{
		// TODO: Once we deprecate the name-based lookup path, we can expand this to check the object ptr too
		return Name.IsNone();
	}

	// Set this reference to None
	void Reset()
	{
		Name = NAME_None;
		Object = nullptr;
	}

	// Check whether refers to a valid variable
	UAF_API bool IsValid() const;

	bool operator==(const FAnimNextVariableReference& InOther) const
	{
		return Name == InOther.Name && Object == InOther.Object;
	}

	bool operator!=(const FAnimNextVariableReference& InOther) const
	{
		return !(*this == InOther);
	}

	friend uint32 GetTypeHash(const FAnimNextVariableReference& InKey)
	{
		return HashCombineFast(GetTypeHash(InKey.Name), GetTypeHash(InKey.Object));
	}

	// Get the property associated with this variable. Returns null for invalid variables.
	UAF_API const FProperty* ResolveProperty() const;

private:
	// The name of the variable
	UPROPERTY(EditAnywhere, Category = Variable)
	FName Name;

	// The asset or struct that the variable reference is contained in
	// Note: Only deprecated paths allow this to be empty, so all variables in a context will be searched
	UPROPERTY(EditAnywhere, Category = Variable)
	TObjectPtr<const UObject> Object;

	// Cached property used for resolving in GetProperty
	mutable TFieldPath<const FProperty> CachedProperty;
};