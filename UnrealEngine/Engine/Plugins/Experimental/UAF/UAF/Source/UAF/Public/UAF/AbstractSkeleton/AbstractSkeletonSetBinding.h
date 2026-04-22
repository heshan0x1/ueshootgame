// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimData/AttributeIdentifier.h"

#include "AbstractSkeletonSetBinding.generated.h"

#define UE_API UAF_API

class UAbstractSkeletonSetCollection;
class USkeleton;

USTRUCT()
struct FAbstractSkeleton_BoneBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Default)
	FName SetName;

	UPROPERTY(EditAnywhere, Category = Default)
	FName BoneName;
};

// Curves are represented as float attributes detached from any bone
USTRUCT()
struct FAbstractSkeleton_AttributeBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Default)
	FName SetName;

	UPROPERTY(EditAnywhere, Category = Default)
	FAnimationAttributeIdentifier Attribute;
};

UCLASS(MinimalAPI, BlueprintType)
class UAbstractSkeletonSetBinding : public UObject
{
	GENERATED_BODY()

public:
	UE_API bool SetSkeleton(const TObjectPtr<USkeleton> InSkeleton);

	UE_API TObjectPtr<USkeleton> GetSkeleton() const;

	UE_API bool SetSetCollection(const TObjectPtr<const UAbstractSkeletonSetCollection> InSetCollection);
	
	UE_API TObjectPtr<const UAbstractSkeletonSetCollection> GetSetCollection() const;
	
	// Bone Bindings

	UE_API bool AddBoneToSet(const FName BoneName, const FName SetName);

	UE_API bool RemoveBoneFromSet(const FName BoneName);

	UE_API bool IsBoneInSet(const FName BoneName) const;

	UE_API bool IsBoneInSet(const FName BoneName, const FName SetName) const;

	// Returns the name of the set that the provided bone is in or NAME_None if bone is unbound.
	UE_API FName GetBoneSet(const FName BoneName);

	UE_API const TConstArrayView<FAbstractSkeleton_BoneBinding> GetBoneBindings() const;
		
	// Attribute Bindings

	UE_API bool AddAttributeToSet(const FAnimationAttributeIdentifier Attribute, const FName SetName);

	UE_API bool RemoveAttributeFromSet(const FAnimationAttributeIdentifier Attribute);
	
	// Returns true if the provided attribute is in ANY set
	UE_API bool IsAttributeInSet(const FAnimationAttributeIdentifier Attribute) const;

	// Returns true if the provided attribute is in the provided set
	UE_API bool IsAttributeInSet(const FAnimationAttributeIdentifier Attribute, const FName SetName) const;

	UE_API const TConstArrayView<FAbstractSkeleton_AttributeBinding> GetAttributeBindings() const;

private:
	UPROPERTY()
	TObjectPtr<const UAbstractSkeletonSetCollection> SetCollection;
	
	UPROPERTY(AssetRegistrySearchable)
	TObjectPtr<USkeleton> Skeleton;

	UPROPERTY()
	TArray<FAbstractSkeleton_BoneBinding> BoneBindings;

	UPROPERTY()
	TArray<FAbstractSkeleton_AttributeBinding> AttributeBindings;
};

#undef UE_API
