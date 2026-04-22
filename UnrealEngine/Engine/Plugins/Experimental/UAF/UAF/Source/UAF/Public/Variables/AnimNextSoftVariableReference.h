// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextSoftVariableReference.generated.h"

class UAnimNextRigVMAsset;
struct FAnimNextVariableReference;

// A soft reference to an AnimNext variable
USTRUCT(BlueprintType)
struct FAnimNextSoftVariableReference
{
	GENERATED_BODY()

	FAnimNextSoftVariableReference() = default;

	// Construct a soft reference to an asset/struct variable
	explicit FAnimNextSoftVariableReference(FName InName, const FSoftObjectPath& InSoftObjectPath)
		: Name(InName)
		, SoftObjectPath(InSoftObjectPath)
	{}

	// Construct a soft reference from a hard reference
	UAF_API explicit FAnimNextSoftVariableReference(const FAnimNextVariableReference& InVariableReference);

	// Get the name of the variable
	FName GetName() const
	{
		return Name;
	}

	// Get the asset or struct that the variable reference is contained in
	const FSoftObjectPath& GetSoftObjectPath() const
	{
		return SoftObjectPath;
	}

	// Check whether this can ever refer to a valid variable
	bool IsNone() const
	{
		return Name.IsNone() || SoftObjectPath.IsNull();
	}

	// Set this reference to None
	void Reset()
	{
		Name = NAME_None; 
		SoftObjectPath.Reset();
	}

	bool operator==(const FAnimNextSoftVariableReference& InOther) const
	{
		return Name == InOther.Name && SoftObjectPath == InOther.SoftObjectPath;
	}

	bool operator!=(const FAnimNextSoftVariableReference& InOther) const
	{
		return !(*this == InOther);
	}

	friend uint32 GetTypeHash(const FAnimNextSoftVariableReference& InKey)
	{
		return HashCombineFast(GetTypeHash(InKey.Name), GetTypeHash(InKey.SoftObjectPath));
	}

private:
	// The name of the variable
	UPROPERTY(EditAnywhere, Category = Variable)
	FName Name;

	// The asset or struct that the variable reference is contained in
	UPROPERTY(EditAnywhere, Category = Variable)
	FSoftObjectPath SoftObjectPath;
};